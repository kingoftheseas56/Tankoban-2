#pragma once

#include <QThread>
#include <QImage>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>
#include <chrono>
#include <cstdint>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

class FfmpegDecoder : public QThread {
    Q_OBJECT

public:
    explicit FfmpegDecoder(QObject* parent = nullptr);
    ~FfmpegDecoder() override;

    bool openFile(const QString& filePath);
    qint64 durationMs() const;

public slots:
    void play();
    void pause();
    void togglePause();
    void stop();
    void seek(qint64 ms);

signals:
    void frameReady(const QImage& frame, qint64 ptsMs);
    void positionChanged(qint64 ptsMs);
    void playbackFinished();
    void errorOccurred(const QString& msg);

protected:
    void run() override;

private:
    bool openCodec();
    void closeAll();
    void flushAndSeek(int64_t targetMs);
    qint64 ptsToMs(int64_t pts) const;
    int64_t msToTs(qint64 ms) const;

    static qint64 nowNs() {
        return std::chrono::steady_clock::now().time_since_epoch().count();
    }

    static std::string ffmpegError(int errnum) {
        char buf[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(errnum, buf, sizeof(buf));
        return buf;
    }

    // FFmpeg state
    AVFormatContext*  m_fmtCtx   = nullptr;
    AVCodecContext*   m_codecCtx = nullptr;
    SwsContext*       m_swsCtx   = nullptr;
    int               m_videoStreamIdx = -1;
    AVRational        m_timeBase = {0, 1};
    qint64            m_durationMs = 0;

    // Thread control
    std::atomic<bool>    m_stop{false};
    std::atomic<bool>    m_paused{false};
    std::atomic<int64_t> m_seekTargetMs{-1};

    // Pause synchronization
    QMutex         m_pauseMutex;
    QWaitCondition m_pauseCond;

    // Timing
    qint64 m_clockBaseNs  = 0;
    qint64 m_firstPtsMs   = 0;
    qint64 m_pauseStartNs = 0;

    // Pre-allocated frame buffer (reused across frames)
    QImage m_frameBuffer;

    // Position throttle (don't emit every frame)
    qint64 m_lastPositionEmitMs = -1;
};
