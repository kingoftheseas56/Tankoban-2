#pragma once

#include <QString>
#include <QtGlobal>

#include <atomic>
#include <memory>

#include "StreamSeekClassifier.h"

// STREAM_ENGINE_REBUILD P3/P6 — per-hash stream record.
//
// Carries the Prioritizer + SeekClassifier state (cached playback
// position, cached duration, EMA download speed, last classified
// SeekType, firstClassification bit) alongside the per-stream lifecycle
// fields and P5 stall-watchdog state.
//
// Kept as a plain movable-copyable struct (NOT a QObject) so
// `QHash<QString, StreamSession>` by-value continues to work. The 1–2 Hz
// re-assert timer + 2 s stall watchdog live on StreamEngine (walk
// m_streams on each tick); Session just holds the state. Integration-
// memo §6 12-method API freeze untouched (StreamSession is private-impl).
struct StreamSession {
    // Observable lifecycle states.
    //   Pending        — magnet added, metadata not yet observed
    //   MetadataOnly   — onMetadataReady fired; waiting for HTTP register
    //                    step (rare — file-not-found edge; also the
    //                    transient path when no video file exists)
    //   Serving        — registered with HTTP server; HTTP workers can
    //                    consume the stream
    enum class State {
        Pending,
        MetadataOnly,
        Serving,
    };

    // Lifecycle fields — do not rename without auditing StreamEngine.cpp's
    // consumer sites.
    QString infoHash;
    QString magnetUri;
    QString savePath;
    int     requestedFileIndex = -1;
    QString fileNameHint;
    int     selectedFileIndex  = -1;
    QString selectedFileName;
    qint64  selectedFileSize   = 0;
    State   state              = State::Pending;
    int     peers              = 0;
    int     dlSpeed            = 0;

    // STREAM_LIFECYCLE_FIX Phase 5 Batch 5.1 — per-stream cancellation
    // token. Default-initialized to a valid shared_ptr so workers that
    // capture the token before stopStream always see a real atomic.
    std::shared_ptr<std::atomic<bool>> cancelled =
        std::make_shared<std::atomic<bool>>(false);

    // STREAM_ENGINE_FIX Phase 1.1 — observability fields (ms since engine
    // clock start; -1 sentinel = not yet observed). Preserved verbatim.
    qint64 metadataReadyMs     = -1;
    qint64 firstPieceArrivalMs = -1;
    int    trackerSourceCount  = 0;

    // STREAM_ENGINE_REBUILD P3 — Prioritizer + SeekClassifier state.
    // Cached playback position from the most recent `updatePlaybackWindow`
    // call (StreamPage's 2 s telemetry tick) — Session's own 1 Hz timer
    // re-asserts against this cached state so libtorrent's deadline table
    // stays warm between StreamPage ticks.
    double lastPlaybackPosSec   = 0.0;
    double lastDurationSec      = 0.0;
    qint64 lastPlaybackOffset   = 0;   // bytes; linear approximation
    qint64 lastPlaybackTickMs   = -1;  // engine-clock; -1 = no tick observed
    qint64 bitrateHint          = 0;   // bytes/sec; 0 = unknown

    // EMA of download speed. alpha=0.2 per TODO §3.2. Updated from
    // TorrentEngine::torrentProgress signal. Seeded on first observation
    // so the filter doesn't take N ticks to warm.
    double downloadSpeedEma     = 0.0;

    // Last SeekType the Prioritizer acted on — used so ContainerMetadata
    // and UserScrub transition back to Sequential after one tick (Stremio
    // `stream.rs:112` self.seek_type = SeekType::Sequential).
    StreamSeekType lastSeekType = StreamSeekType::InitialPlayback;

    // Set on construction; flips to false after the first
    // SeekClassifier::classifySeek call. Distinguishes the cold-open
    // URGENT-tier read from a legitimate offset=0 scrub that happened
    // mid-playback (rare, but we don't want to re-enter URGENT tier on
    // every zero-offset read).
    bool firstClassification   = true;

    // STREAM_ENGINE_REBUILD P5 — stall watchdog state.
    //
    // `stallStartMs` = engine-clock when the stall-detected threshold
    // (longestActiveWaitMs > 4000) first tripped, or -1 if not currently
    // stalled. `stallPiece` = the piece the waiter was blocked on when the
    // stall fired; `stallPeerHaveCount` = peersWithPiece result captured
    // at the same instant (R3 falsification gate — scheduler-starvation
    // with have>0 vs swarm-starvation with have=0). `stallEmitted` dedupes
    // the stall_detected telemetry so one stall = one emit (re-emits on
    // every tick would flood the log).
    qint64 stallStartMs        = -1;
    int    stallPiece          = -1;
    int    stallPeerHaveCount  = -1;
    bool   stallEmitted        = false;

    // STREAM_STALL_FIX Phase 2 — gate-pass sequential toggle (tactic a).
    // Flips to true on the first gate-100% crossing per session; the
    // paired setSequentialDownload(false) call returns pieces past
    // cursor to libtorrent's normal pick_pieces path (Agent 4B B3
    // source-verification of torrent.cpp / peer_connection.cpp). Never
    // flips back — cold-open sequential bias is only useful for head
    // delivery; once the head is filled, post-gate stalls are what we're
    // fighting.
    bool gatePassSequentialOff = false;

    // Convenience — updates downloadSpeedEma with alpha=0.2. Seeds the
    // filter on first call (ema==0 && observedSpeed>0).
    void updateSpeedEma(qint64 observedSpeedBytesPerSec) {
        constexpr double kAlpha = 0.2;
        if (downloadSpeedEma == 0.0 && observedSpeedBytesPerSec > 0) {
            downloadSpeedEma = static_cast<double>(observedSpeedBytesPerSec);
            return;
        }
        downloadSpeedEma =
            (kAlpha * static_cast<double>(observedSpeedBytesPerSec))
          + ((1.0 - kAlpha) * downloadSpeedEma);
    }
};
