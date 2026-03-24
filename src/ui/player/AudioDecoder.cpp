#include "ui/player/AudioDecoder.h"
#include "ui/player/SyncClock.h"

#include <QDebug>
#include <cstring>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#pragma comment(lib, "winmm.lib")
#endif

AudioDecoder::AudioDecoder(SyncClock* clock, QObject* parent)
    : QThread(parent)
    , m_clock(clock)
{}

AudioDecoder::~AudioDecoder()
{
    stop();
    close();
}

bool AudioDecoder::openFile(const QString& filePath)
{
    close();

    QByteArray path = filePath.toUtf8();
    int ret = avformat_open_input(&m_fmtCtx, path.constData(), nullptr, nullptr);
    if (ret < 0) {
        emit errorOccurred(QStringLiteral("Audio: cannot open %1").arg(filePath));
        return false;
    }

    ret = avformat_find_stream_info(m_fmtCtx, nullptr);
    if (ret < 0) { close(); return false; }

    const AVCodec* codec = nullptr;
    m_audioStreamIdx = av_find_best_stream(m_fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (m_audioStreamIdx < 0) {
        // No audio stream — not an error, just silent video
        close();
        return false;
    }

    m_timeBase = m_fmtCtx->streams[m_audioStreamIdx]->time_base;

    m_codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_codecCtx, m_fmtCtx->streams[m_audioStreamIdx]->codecpar);

    ret = avcodec_open2(m_codecCtx, codec, nullptr);
    if (ret < 0) {
        emit errorOccurred(QStringLiteral("Audio: cannot open codec"));
        close();
        return false;
    }

    m_sampleRate = m_codecCtx->sample_rate;
    m_channels   = 2; // always output stereo

    // Create resampler: any input → float32 stereo
    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
    ret = swr_alloc_set_opts2(&m_swrCtx,
        &outLayout, AV_SAMPLE_FMT_FLT, m_sampleRate,
        &m_codecCtx->ch_layout, m_codecCtx->sample_fmt, m_sampleRate,
        0, nullptr);
    if (ret < 0 || !m_swrCtx) {
        emit errorOccurred(QStringLiteral("Audio: cannot create resampler"));
        close();
        return false;
    }
    swr_init(m_swrCtx);

    return true;
}

void AudioDecoder::close()
{
    if (m_swrCtx)   { swr_free(&m_swrCtx); }
    if (m_codecCtx) { avcodec_free_context(&m_codecCtx); }
    if (m_fmtCtx)   { avformat_close_input(&m_fmtCtx); }
    m_audioStreamIdx = -1;
}

void AudioDecoder::play()
{
    if (isRunning()) {
        m_paused.store(false);
        m_pauseCond.wakeOne();
    } else {
        m_stop.store(false);
        m_paused.store(false);
        m_seekTargetUs.store(-1);
        start();
    }
}

void AudioDecoder::pause()
{
    m_paused.store(true);
}

void AudioDecoder::stop()
{
    if (!isRunning()) return;
    m_stop.store(true);
    m_paused.store(false);
    m_pauseCond.wakeAll();
    wait();
}

void AudioDecoder::seek(qint64 ms)
{
    m_seekTargetUs.store(ms * 1000);
    if (m_paused.load()) {
        m_paused.store(false);
        m_pauseCond.wakeOne();
    }
}

void AudioDecoder::setVolume(float vol)
{
    m_volume.store(vol);
}

// ── Decode + output loop ────────────────────────────────────────────────────

void AudioDecoder::run()
{
    if (!m_fmtCtx || !m_codecCtx || !m_swrCtx) return;

#ifdef Q_OS_WIN
    // Set up waveOut for float32 stereo output
    WAVEFORMATEX wfx{};
    wfx.wFormatTag      = WAVE_FORMAT_IEEE_FLOAT;
    wfx.nChannels        = static_cast<WORD>(m_channels);
    wfx.nSamplesPerSec   = static_cast<DWORD>(m_sampleRate);
    wfx.wBitsPerSample   = 32;
    wfx.nBlockAlign      = wfx.nChannels * wfx.wBitsPerSample / 8;
    wfx.nAvgBytesPerSec  = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize           = 0;

    HWAVEOUT hwo = nullptr;
    MMRESULT mr = waveOutOpen(&hwo, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
    if (mr != MMSYSERR_NOERROR) {
        emit errorOccurred(QStringLiteral("Audio: cannot open output device"));
        return;
    }

    // Double-buffered headers for smooth output
    constexpr int NUM_BUFS = 4;
    constexpr int BUF_SAMPLES = 2048;
    const int bufBytes = BUF_SAMPLES * m_channels * sizeof(float);

    std::vector<std::vector<float>> bufData(NUM_BUFS, std::vector<float>(BUF_SAMPLES * m_channels));
    std::vector<WAVEHDR> hdrs(NUM_BUFS);
    for (int i = 0; i < NUM_BUFS; ++i) {
        std::memset(&hdrs[i], 0, sizeof(WAVEHDR));
        hdrs[i].lpData         = reinterpret_cast<LPSTR>(bufData[i].data());
        hdrs[i].dwBufferLength = static_cast<DWORD>(bufBytes);
        waveOutPrepareHeader(hwo, &hdrs[i], sizeof(WAVEHDR));
    }
    int curBuf = 0;
#endif

    AVPacket* packet = av_packet_alloc();
    AVFrame*  frame  = av_frame_alloc();

    // Temp buffer for resampled output
    std::vector<float> resampleBuf(BUF_SAMPLES * m_channels * 4);

    m_clock->start();

    while (!m_stop.load()) {
        // ── Pause ──
        {
            QMutexLocker lock(&m_pauseMutex);
            while (m_paused.load() && !m_stop.load()) {
                m_pauseCond.wait(&m_pauseMutex);
            }
        }
        if (m_stop.load()) break;

        // ── Seek ──
        int64_t seekUs = m_seekTargetUs.exchange(-1);
        if (seekUs >= 0) {
            int64_t ts = av_rescale_q(seekUs, {1, 1000000}, m_timeBase);
            av_seek_frame(m_fmtCtx, m_audioStreamIdx, ts, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(m_codecCtx);
            m_clock->seekAnchor(seekUs);

#ifdef Q_OS_WIN
            waveOutReset(hwo);
            for (int i = 0; i < NUM_BUFS; ++i) {
                hdrs[i].dwFlags &= ~WHDR_DONE;
                hdrs[i].dwFlags |= WHDR_PREPARED;
            }
            curBuf = 0;
#endif
            continue;
        }

        // ── Read packet ──
        int ret = av_read_frame(m_fmtCtx, packet);
        if (ret < 0) break;

        if (packet->stream_index != m_audioStreamIdx) {
            av_packet_unref(packet);
            continue;
        }

        ret = avcodec_send_packet(m_codecCtx, packet);
        av_packet_unref(packet);
        if (ret < 0) continue;

        while (avcodec_receive_frame(m_codecCtx, frame) == 0) {
            if (m_stop.load()) break;

            // Resample to float32 stereo
            int outSamples = swr_get_out_samples(m_swrCtx, frame->nb_samples);
            if (static_cast<size_t>(outSamples * m_channels) > resampleBuf.size())
                resampleBuf.resize(outSamples * m_channels);

            uint8_t* outPtr = reinterpret_cast<uint8_t*>(resampleBuf.data());
            int converted = swr_convert(m_swrCtx,
                &outPtr, outSamples,
                const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);

            if (converted <= 0) {
                av_frame_unref(frame);
                continue;
            }

            // Apply volume
            float vol = m_volume.load();
            if (vol < 0.999f) {
                for (int i = 0; i < converted * m_channels; ++i)
                    resampleBuf[i] *= vol;
            }

            // Compute PTS for this buffer
            int64_t ptsUs = 0;
            if (frame->pts != AV_NOPTS_VALUE)
                ptsUs = av_rescale_q(frame->pts, m_timeBase, {1, 1000000});

#ifdef Q_OS_WIN
            // Write to waveOut in chunks
            int samplesWritten = 0;
            while (samplesWritten < converted && !m_stop.load()) {
                WAVEHDR& hdr = hdrs[curBuf];

                // Wait for buffer to become available
                while (!(hdr.dwFlags & WHDR_DONE) && (hdr.dwFlags & WHDR_INQUEUE)) {
                    if (m_stop.load()) break;
                    QThread::msleep(2);
                }
                if (m_stop.load()) break;

                int chunk = qMin(converted - samplesWritten, BUF_SAMPLES);
                int chunkBytes = chunk * m_channels * sizeof(float);

                std::memcpy(bufData[curBuf].data(),
                            resampleBuf.data() + samplesWritten * m_channels,
                            chunkBytes);
                hdr.dwBufferLength = static_cast<DWORD>(chunkBytes);

                waveOutWrite(hwo, &hdr, sizeof(WAVEHDR));

                // Update clock after device accepts buffer
                int64_t chunkDurationUs = static_cast<int64_t>(chunk) * 1000000 / m_sampleRate;
                int64_t bufPtsUs = ptsUs + static_cast<int64_t>(samplesWritten) * 1000000 / m_sampleRate;
                m_clock->update(bufPtsUs + chunkDurationUs);

                samplesWritten += chunk;
                curBuf = (curBuf + 1) % NUM_BUFS;
            }
#endif
            av_frame_unref(frame);
        }
    }

    av_packet_free(&packet);
    av_frame_free(&frame);

#ifdef Q_OS_WIN
    waveOutReset(hwo);
    for (int i = 0; i < NUM_BUFS; ++i)
        waveOutUnprepareHeader(hwo, &hdrs[i], sizeof(WAVEHDR));
    waveOutClose(hwo);
#endif

    m_clock->stop();
}
