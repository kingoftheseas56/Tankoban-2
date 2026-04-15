#pragma once

#include <QObject>
#include <QTimer>
#include <QString>
#include <QJsonObject>

#include "core/stream/addon/StreamInfo.h"

class CoreBridge;
class StreamEngine;
class VideoPlayer;

class StreamPlayerController : public QObject
{
    Q_OBJECT

public:
    explicit StreamPlayerController(CoreBridge* bridge, StreamEngine* engine,
                                    QObject* parent = nullptr);

    // Phase 4.3 Stream-based entry point. Branches internally by source kind:
    //   Magnet → existing polling flow.
    //   Http/Url → immediate readyToPlay, no buffer polling.
    //   YouTube → immediate streamFailed("unsupported").
    void startStream(const QString& imdbId, const QString& mediaType,
                     int season, int episode,
                     const tankostream::addon::Stream& selectedStream);

    void stopStream();

    bool isActive() const { return m_active; }
    QString currentInfoHash() const { return m_infoHash; }

signals:
    void bufferUpdate(const QString& statusText, double percent);
    void readyToPlay(const QString& httpUrl);
    void streamFailed(const QString& message);
    void streamStopped();

private slots:
    void pollStreamStatus();

private:
    void onStreamReady(const QString& url);

    CoreBridge*   m_bridge;
    StreamEngine* m_engine;

    QTimer m_pollTimer;
    bool   m_active = false;

    // Current stream state
    QString m_infoHash;
    QString m_imdbId;
    QString m_mediaType;
    int     m_season  = 0;
    int     m_episode = 0;
    tankostream::addon::Stream m_selectedStream;

    // Polling state
    int  m_pollCount = 0;
    qint64 m_startTimeMs = 0;
    qint64 m_lastMetadataChangeMs = 0;

    static constexpr int POLL_FAST_MS          = 300;
    static constexpr int POLL_SLOW_MS          = 1000;
    static constexpr int POLL_SLOW_AFTER       = 100;
    static constexpr int HARD_TIMEOUT_MS       = 120000;
    static constexpr int METADATA_STALL_MS     = 60000;

    QString m_lastErrorCode;
};
