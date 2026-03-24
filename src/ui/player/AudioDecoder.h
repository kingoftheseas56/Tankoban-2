#pragma once

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>
#include <cstdint>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

class SyncClock;

class AudioDecoder : public QThread {
    Q_OBJECT

public:
    explicit AudioDecoder(SyncClock* clock, QObject* parent = nullptr);
    ~AudioDecoder() override;

    bool openFile(const QString& filePath);
    void close();

public slots:
    void play();
    void pause();
    void stop();
    void seek(qint64 ms);
    void setVolume(float vol);  // 0.0 – 1.0

signals:
    void errorOccurred(const QString& msg);

protected:
    void run() override;

private:
    static std::string ffmpegError(int errnum) {
        char buf[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(errnum, buf, sizeof(buf));
        return buf;
    }

    SyncClock* m_clock;

    // FFmpeg state
    AVFormatContext*  m_fmtCtx   = nullptr;
    AVCodecContext*   m_codecCtx = nullptr;
    SwrContext*       m_swrCtx   = nullptr;
    int               m_audioStreamIdx = -1;
    AVRational        m_timeBase = {0, 1};
    int               m_sampleRate = 0;
    int               m_channels  = 2;

    // Thread control
    std::atomic<bool>    m_stop{false};
    std::atomic<bool>    m_paused{false};
    std::atomic<int64_t> m_seekTargetUs{-1};
    std::atomic<float>   m_volume{1.0f};

    QMutex         m_pauseMutex;
    QWaitCondition m_pauseCond;
};
