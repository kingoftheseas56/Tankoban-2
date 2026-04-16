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
    // STREAM_LIFECYCLE_FIX Phase 2 Batch 2.1 — stop reason vocabulary.
    // Distinguishes the three lifecycle events that today collide under a
    // single parameterless stopStream() + streamStopped() shape:
    //   UserEnd     — user closed player / Escape / back button. Session
    //                 ends; StreamPage returns to browse.
    //   Replacement — startStream()'s first-line defensive stop. A NEW
    //                 session is about to start; StreamPage should NOT
    //                 tear down UX state that the incoming session will
    //                 re-use (buffer overlay, epKey, mainStack index).
    //                 Closes audit P0-1 (source-switch reentrancy flash
    //                 to browse) once Batch 2.2 routes the reason to the
    //                 StreamPage handler's branch logic.
    //   Failure     — controller hit timeout / engine error. Emits
    //                 streamFailed(msg) ALSO. Batch 2.1 does not route
    //                 failure paths through Failure yet — keeps the
    //                 failure-to-streamStopped split the way it is now;
    //                 Phase 3 closes that.
    // Q_ENUM registration enables queued-connection serialization + debug
    // log stringification. Shape adopted from Agent 7's prototype at
    // agents/prototypes/stream_lifecycle/Batch2.1_StopReason_signal_signature.cpp.
    enum class StopReason {
        UserEnd,
        Replacement,
        Failure,
    };
    Q_ENUM(StopReason)

    explicit StreamPlayerController(CoreBridge* bridge, StreamEngine* engine,
                                    QObject* parent = nullptr);

    // Phase 4.3 Stream-based entry point. Branches internally by source kind:
    //   Magnet → existing polling flow.
    //   Http/Url → immediate readyToPlay, no buffer polling.
    //   YouTube → immediate streamFailed("unsupported").
    void startStream(const QString& imdbId, const QString& mediaType,
                     int season, int episode,
                     const tankostream::addon::Stream& selectedStream);

    // Default-arg keeps every existing caller on UserEnd semantics.
    // startStream() internally now calls stopStream(StopReason::Replacement).
    void stopStream(StopReason reason = StopReason::UserEnd);

    bool isActive() const { return m_active; }
    QString currentInfoHash() const { return m_infoHash; }

signals:
    void bufferUpdate(const QString& statusText, double percent);
    void readyToPlay(const QString& httpUrl);
    void streamFailed(const QString& message);
    // Signal signature extended 2026-04-16 (Batch 2.1). StreamPage's existing
    // connect to `&StreamPage::onStreamStopped` (zero-arg slot) stays
    // connection-compatible per Qt's "slot can have fewer args than signal"
    // rule — reason is silently dropped at the slot. Batch 2.2 migrates
    // StreamPage to accept the reason + branch on it.
    void streamStopped(StreamPlayerController::StopReason reason);

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
