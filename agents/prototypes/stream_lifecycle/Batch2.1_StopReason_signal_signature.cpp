// =================================================================
// Agent 7 (Codex) Prototype - Reference Only
// =================================================================
// For: Agent 4, Batch 2.1 (STREAM_LIFECYCLE)
// Date: 2026-04-16
// References consulted:
//   - STREAM_LIFECYCLE_FIX_TODO.md:152
//   - agents/audits/tankostream_session_lifecycle_2026-04-15.md:104
//   - src/ui/pages/stream/StreamPlayerController.h:30
//   - src/ui/pages/stream/StreamPlayerController.h:39
//   - src/ui/pages/stream/StreamPlayerController.cpp:26
//   - src/ui/pages/stream/StreamPlayerController.cpp:65
//
// THIS FILE IS NOT COMPILED. The domain agent implements their own
// version after reading this for perspective. Do not #include from
// src/. Do not add to CMakeLists.txt.
// =================================================================

// Scope:
//   Split stream stop identity at the controller API boundary. This batch
//   does not decide StreamPage's branch behavior; Batch 2.2 consumes the
//   reason. The important contract is that startStream() marks its
//   defensive stop as Replacement, while existing direct user stops keep
//   UserEnd by default.
//
// Concrete from current src:
//   - startStream() calls stopStream() as its first line.
//   - stopStream() emits streamStopped() synchronously.
//   - StreamPage currently connects to streamStopped() with no args.
//
// Guess:
//   - Q_ENUM is useful for logs, queued connections, and future debug UI.

namespace agent7_stream_lifecycle_batch_2_1 {

// ---------------- StreamPlayerController.h shape ----------------

class StreamPlayerController : public QObject
{
    Q_OBJECT

public:
    enum class StopReason {
        UserEnd,
        Replacement,
        Failure,
    };
    Q_ENUM(StopReason)

    void startStream(const QString& imdbId, const QString& mediaType,
                     int season, int episode,
                     const tankostream::addon::Stream& selectedStream);

    void stopStream(StopReason reason = StopReason::UserEnd);

signals:
    void bufferUpdate(const QString& statusText, double percent);
    void readyToPlay(const QString& httpUrl);
    void streamFailed(const QString& message);
    void streamStopped(StreamPlayerController::StopReason reason);
};

// ---------------- StreamPlayerController.cpp shape ----------------

void StreamPlayerController::startStream(const QString& imdbId,
                                         const QString& mediaType,
                                         int season,
                                         int episode,
                                         const Stream& selectedStream)
{
    stopStream(StopReason::Replacement);

    m_active = true;
    m_imdbId = imdbId;
    m_mediaType = mediaType;
    m_season = season;
    m_episode = episode;
    m_selectedStream = selectedStream;
    m_pollCount = 0;
    m_startTimeMs = QDateTime::currentMSecsSinceEpoch();
    m_lastMetadataChangeMs = m_startTimeMs;
    m_lastErrorCode.clear();

    // Current implementation continues here unchanged:
    // - direct HTTP/URL one-shot handoff
    // - YouTube unsupported branch
    // - magnet polling path
}

void StreamPlayerController::stopStream(StopReason reason)
{
    if (!m_active)
        return;

    m_pollTimer.stop();
    m_active = false;

    if (m_selectedStream.source.kind == StreamSource::Kind::Magnet
        && !m_infoHash.isEmpty()) {
        m_engine->stopStream(m_infoHash);
    }

    m_infoHash.clear();
    m_selectedStream = {};

    emit streamStopped(reason);
}

// Optional helper for failure sites in later batches. Batch 2.1 can leave
// streamFailed() paths untouched, but this shows how Agent 4 can keep the
// reason vocabulary consistent once failure cleanup is unified.
void StreamPlayerController::emitFailure(const QString& message)
{
    m_pollTimer.stop();
    m_active = false;
    m_infoHash.clear();
    m_selectedStream = {};
    emit streamFailed(message);
    // Do not emit streamStopped(Failure) unless StreamPage's UX is ready to
    // receive both signals for the same failure. Agent 4 decides this in
    // Batch 2.2 / Phase 3.
}

} // namespace agent7_stream_lifecycle_batch_2_1
