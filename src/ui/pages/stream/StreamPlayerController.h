#pragma once

#include <QObject>
#include <QTimer>
#include <QString>
#include <QJsonObject>
#include <QList>
#include <QPair>

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
    // Stream-side filename (actual video file inside the torrent — e.g.
    // "One.Piece.S02E01.1080p.WEB-DL.x265-HDHub.mkv"). Populated on the
    // first streamFile() poll that carries a selectedFileName. Consumed
    // by StreamPage::onReadyToPlay as the HUD displayTitle override so
    // the player's bottom bar doesn't show the HTTP URL's last segment
    // ("0" = file index) as the title. Empty until metadata lands.
    QString currentFileName() const { return m_currentFileName; }

    // PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.2 — on-demand buffered-range
    // snapshot + emit. Called from pollStreamStatus each startup tick AND
    // from StreamPage's progressUpdated lambda during playback (at the same
    // site as updatePlaybackWindow's 2s-rate-limited deadline retarget, so
    // buffered-range updates at ~2 Hz during playback without adding a new
    // timer). Kept as a public method so StreamPage's playback lifecycle
    // drives the call — avoids mutating StreamPlayerController's own
    // lifecycle (onStreamReady/stopStream) which Agent 4 flagged as
    // lifecycle-critical post-STREAM_LIFECYCLE_FIX (Rule 10 chat.md:3354).
    // No-op if m_infoHash is empty (pre-metadata) or the current stream
    // is non-magnet (HTTP/URL direct-play). Equality-deduped against the
    // last emitted ranges+fileSize so same-state polls don't repaint.
    void pollBufferedRangesOnce();

signals:
    void bufferUpdate(const QString& statusText, double percent);
    void readyToPlay(const QString& httpUrl);
    void streamFailed(const QString& message);
    // PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.2 — buffered-range snapshot
    // for SeekSlider overlay paint. Ranges are file-local {startByte,
    // endByte} pairs (endByte exclusive), sorted, non-overlapping. fileSize
    // ridealong saves consumer from a second StreamEngine lookup. Emitted
    // only when snapshot differs from last emit (equality-deduped).
    // Consumer: VideoPlayer::onBufferedRangesChanged (Batch 1.3) → forwards
    // to SeekSlider::setBufferedRanges (Batch 1.4) when m_streamMode true.
    void bufferedRangesChanged(const QString& infoHash,
                               const QList<QPair<qint64, qint64>>& ranges,
                               qint64 fileSize);
    // Signal signature extended 2026-04-16 (Batch 2.1). StreamPage's existing
    // connect to `&StreamPage::onStreamStopped` (zero-arg slot) stays
    // connection-compatible per Qt's "slot can have fewer args than signal"
    // rule — reason is silently dropped at the slot. Batch 2.2 migrates
    // StreamPage to accept the reason + branch on it.
    void streamStopped(StreamPlayerController::StopReason reason);

private slots:
    void pollStreamStatus();
    // STREAM_LIFECYCLE_FIX Phase 3 Batch 3.3 — receives StreamEngine's
    // streamError(hash, msg) signal. Gated on hash match against m_infoHash
    // so errors from stale/other streams don't clobber the active session.
    // On match: stopStream(StopReason::Failure) → emit streamFailed — same
    // shape as the poll-timeout + engine-error failure paths (Batch 2.2
    // unification). Pre-3.3: streamError signal existed but was unconnected;
    // error records sat in StreamEngine::m_streams until the 120s hard
    // timeout fired or the user explicitly stopped. Now controller reacts
    // within one Qt event loop iteration.
    void onEngineStreamError(const QString& infoHash, const QString& message);

private:
    void onStreamReady(const QString& url);

    // STREAM_LIFECYCLE_FIX Phase 3 Batch 3.1 — explicit session-state reset
    // helper. Collects every per-session field that needs to cleanly unwind:
    // m_infoHash (audit P1-1 — stale hash leaking into next seek pre-gate),
    // m_selectedStream (release the Stream copy), m_pollCount (reset to 0
    // so the POLL_FAST_MS → POLL_SLOW_MS transition at POLL_SLOW_AFTER fires
    // correctly on the next session), m_lastErrorCode (drop the per-session
    // error-code memory so metadata-stall debouncing starts fresh). Called
    // once per stopStream() invocation — reason-agnostic, covers UserEnd /
    // Replacement / Failure uniformly (post-Batch-2.2 all failure sites
    // route through stopStream(Failure) so the helper cascades there too).
    void clearSessionState();

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

    // PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.2 — equality-dedup state for
    // bufferedRangesChanged emit. Skips re-emit when the snapshot matches
    // the last emitted value, so steady-state polls don't trigger SeekSlider
    // repaints when no new pieces have completed. Reset by clearSessionState
    // alongside m_infoHash etc. so a new session starts with a clean dedup
    // slate (first emit always fires).
    QList<QPair<qint64, qint64>> m_lastBufferedRanges;
    qint64                       m_lastBufferedFileSize = 0;

    // Session-scoped cache of the current stream's file-total byte size,
    // refreshed on every pollStreamStatus tick from streamFile()'s result.
    // Preserved after pollStreamStatus stops (readyToStart) so during-
    // playback pollBufferedRangesOnce calls from StreamPage's
    // progressUpdated lambda have a ready value without a redundant
    // streamFile() invocation (streamFile has streaming-server side
    // effects beyond simple metadata lookup; pollBufferedRangesOnce
    // only needs the byte-size scalar).
    qint64 m_currentFileSize = 0;

    // Stream-side filename cache. Populated on each pollStreamStatus tick
    // that sees a non-empty selectedFileName in StreamFileResult. Reset
    // by clearSessionState so a new session always starts empty (the
    // VideoPlayer then falls back to URL basename until metadata lands,
    // which is already the harmless "0" transient for magnet streams —
    // bottom HUD just flips from "0" to the real filename once the
    // first successful poll completes).
    QString m_currentFileName;
};
