#include "ui/player/FfmpegDecoder.h"
#include "ui/player/SyncClock.h"

#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#pragma comment(lib, "winmm.lib")
#endif

FfmpegDecoder::FfmpegDecoder(SyncClock* clock, QObject* parent)
    : QThread(parent)
    , m_clock(clock)
{}

FfmpegDecoder::~FfmpegDecoder()
{
    stop();
    closeAll();
}

// ── File opening ────────────────────────────────────────────────────────────

bool FfmpegDecoder::openFile(const QString& filePath)
{
    closeAll();

    QByteArray pathUtf8 = filePath.toUtf8();
    int ret = avformat_open_input(&m_fmtCtx, pathUtf8.constData(), nullptr, nullptr);
    if (ret < 0) {
        emit errorOccurred(QStringLiteral("Cannot open file: %1 (%2)")
                               .arg(filePath, QString::fromStdString(ffmpegError(ret))));
        return false;
    }

    ret = avformat_find_stream_info(m_fmtCtx, nullptr);
    if (ret < 0) {
        emit errorOccurred(QStringLiteral("Cannot read stream info"));
        closeAll();
        return false;
    }

    const AVCodec* codec = nullptr;
    m_videoStreamIdx = av_find_best_stream(m_fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (m_videoStreamIdx < 0) {
        emit errorOccurred(QStringLiteral("No video stream found"));
        closeAll();
        return false;
    }

    m_timeBase = m_fmtCtx->streams[m_videoStreamIdx]->time_base;

    AVStream* vs = m_fmtCtx->streams[m_videoStreamIdx];
    if (vs->duration != AV_NOPTS_VALUE)
        m_durationMs = ptsToMs(vs->duration);
    else if (m_fmtCtx->duration != AV_NOPTS_VALUE)
        m_durationMs = m_fmtCtx->duration / 1000;

    if (!openCodec()) {
        closeAll();
        return false;
    }

    m_frameBuffers[0] = QImage(m_codecCtx->width, m_codecCtx->height, QImage::Format_ARGB32);
    m_frameBuffers[1] = QImage(m_codecCtx->width, m_codecCtx->height, QImage::Format_ARGB32);
    m_writeIdx = 0;

    return true;
}

bool FfmpegDecoder::openCodec()
{
    AVStream* vs = m_fmtCtx->streams[m_videoStreamIdx];

    const AVCodec* codec = avcodec_find_decoder(vs->codecpar->codec_id);
    if (!codec) {
        emit errorOccurred(QStringLiteral("Unsupported codec"));
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_codecCtx, vs->codecpar);
    m_codecCtx->thread_count = 0;
    m_codecCtx->thread_type  = FF_THREAD_FRAME | FF_THREAD_SLICE;

    int ret = avcodec_open2(m_codecCtx, codec, nullptr);
    if (ret < 0) {
        emit errorOccurred(QStringLiteral("Cannot open codec"));
        return false;
    }

    m_swsCtx = sws_getContext(
        m_codecCtx->width, m_codecCtx->height, m_codecCtx->pix_fmt,
        m_codecCtx->width, m_codecCtx->height, AV_PIX_FMT_BGRA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_swsCtx) {
        emit errorOccurred(QStringLiteral("Cannot create pixel format converter"));
        return false;
    }

    return true;
}

void FfmpegDecoder::closeAll()
{
    if (m_swsCtx)  { sws_freeContext(m_swsCtx);       m_swsCtx = nullptr; }
    if (m_codecCtx) { avcodec_free_context(&m_codecCtx); }
    if (m_fmtCtx)   { avformat_close_input(&m_fmtCtx); }
    m_videoStreamIdx = -1;
    m_durationMs = 0;
    m_frameBuffers[0] = QImage();
    m_frameBuffers[1] = QImage();
}

// ── Playback control ────────────────────────────────────────────────────────

void FfmpegDecoder::play()
{
    if (isRunning()) {
        m_paused.store(false);
        m_pauseCond.wakeOne();
    } else {
        m_stop.store(false);
        m_paused.store(false);
        m_seekTargetMs.store(-1);
        m_lastPositionEmitMs = -1;
        start();
    }
}

void FfmpegDecoder::pause()
{
    m_paused.store(true);
}

void FfmpegDecoder::togglePause()
{
    if (m_paused.load()) play(); else pause();
}

void FfmpegDecoder::stop()
{
    if (!isRunning()) return;
    m_stop.store(true);
    m_paused.store(false);
    m_pauseCond.wakeAll();
    wait();
}

void FfmpegDecoder::seek(qint64 ms)
{
    m_seekTargetMs.store(ms);
    if (m_paused.load()) {
        m_paused.store(false);
        m_pauseCond.wakeOne();
    }
}

qint64 FfmpegDecoder::durationMs() const
{
    return m_durationMs;
}

// ── Decode loop — syncs to audio clock ──────────────────────────────────────

void FfmpegDecoder::run()
{
    if (!m_fmtCtx || !m_codecCtx || !m_swsCtx) {
        emit errorOccurred(QStringLiteral("Decoder not initialized"));
        return;
    }

#ifdef Q_OS_WIN
    timeBeginPeriod(1);
#endif

    AVPacket* packet = av_packet_alloc();
    AVFrame*  frame  = av_frame_alloc();
    if (!packet || !frame) {
        av_packet_free(&packet);
        av_frame_free(&frame);
#ifdef Q_OS_WIN
        timeEndPeriod(1);
#endif
        return;
    }

    const int h = m_codecCtx->height;

    // Wait for audio clock to start (up to 2 seconds)
    for (int i = 0; i < 200 && !m_clock->hasStarted() && !m_stop.load(); ++i)
        QThread::msleep(10);

    while (!m_stop.load()) {
        // ── Pause ──
        {
            QMutexLocker lock(&m_pauseMutex);
            while (m_paused.load() && !m_stop.load())
                m_pauseCond.wait(&m_pauseMutex);
        }
        if (m_stop.load()) break;

        // ── Seek ──
        int64_t seekMs = m_seekTargetMs.exchange(-1);
        if (seekMs >= 0) {
            flushAndSeek(seekMs);
            continue;
        }

        // ── Read packet ──
        int ret = av_read_frame(m_fmtCtx, packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF)
                emit playbackFinished();
            break;
        }

        if (packet->stream_index != m_videoStreamIdx) {
            av_packet_unref(packet);
            continue;
        }

        ret = avcodec_send_packet(m_codecCtx, packet);
        av_packet_unref(packet);
        if (ret < 0) continue;

        while (avcodec_receive_frame(m_codecCtx, frame) == 0) {
            if (m_stop.load()) break;

            // Frame PTS in microseconds
            int64_t ptsUs = 0;
            if (frame->pts != AV_NOPTS_VALUE)
                ptsUs = av_rescale_q(frame->pts, m_timeBase, {1, 1000000});

            qint64 ptsMs = ptsUs / 1000;

            // ── Sync to audio clock ──
            // Wait until audio catches up to this frame's PTS
            if (m_clock->hasStarted()) {
                for (;;) {
                    if (m_stop.load()) break;
                    int64_t clockUs = m_clock->positionUs();
                    int64_t aheadUs = ptsUs - clockUs;

                    if (aheadUs <= 15000)  // within 15ms tolerance — show it
                        break;

                    if (aheadUs > 500000) {  // >500ms ahead — something's wrong, bail
                        break;
                    }

                    // Sleep for half the remaining time
                    int sleepMs = static_cast<int>(aheadUs / 2000);
                    if (sleepMs < 1) sleepMs = 1;
                    QThread::msleep(sleepMs);
                }

                // Drop frame if we're too late (>42ms behind audio)
                int64_t clockUs = m_clock->positionUs();
                if (ptsUs < clockUs - 42000) {
                    av_frame_unref(frame);
                    continue;
                }
            }

            // ── Convert to BGRA ──
            QImage& buf = m_frameBuffers[m_writeIdx];
            uint8_t* dstData[1]  = { buf.bits() };
            int dstLinesize[1]   = { static_cast<int>(buf.bytesPerLine()) };
            sws_scale(m_swsCtx, frame->data, frame->linesize, 0, h,
                      dstData, dstLinesize);

            emit frameReady(buf, ptsMs);
            m_writeIdx ^= 1;

            // Throttle position updates to ~4/sec
            if (m_lastPositionEmitMs < 0 || (ptsMs - m_lastPositionEmitMs) >= 250) {
                emit positionChanged(ptsMs);
                m_lastPositionEmitMs = ptsMs;
            }

            av_frame_unref(frame);
        }
    }

    av_packet_free(&packet);
    av_frame_free(&frame);

#ifdef Q_OS_WIN
    timeEndPeriod(1);
#endif
}

// ── Seek helpers ────────────────────────────────────────────────────────────

void FfmpegDecoder::flushAndSeek(int64_t targetMs)
{
    int64_t ts = msToTs(targetMs);
    av_seek_frame(m_fmtCtx, m_videoStreamIdx, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(m_codecCtx);
    m_lastPositionEmitMs = -1;
}

qint64 FfmpegDecoder::ptsToMs(int64_t pts) const
{
    return av_rescale_q(pts, m_timeBase, {1, 1000});
}

int64_t FfmpegDecoder::msToTs(qint64 ms) const
{
    return av_rescale_q(ms, {1, 1000}, m_timeBase);
}
