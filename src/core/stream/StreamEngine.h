#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QTimer>

#include <atomic>
#include <memory>

#include "addon/StreamInfo.h"
#include "StreamSession.h"

// ─────────────────────────────────────────────────────────────────────────────
// Slice A architectural non-goals (STREAM_ENGINE_FIX Phase 4.2 — codified
// 2026-04-17 per agents/audits/stream_a_engine_2026-04-16.md validation).
//
// Deliberate scope boundaries for Stream-A substrate; downstream slice audits
// (D / 3a / C / 3b / 3c) must not re-flag any item below as a gap.
//
//   - No HLS / adaptive transcoding routes. Native sidecar demuxes anything
//     ffmpeg supports; the HTML-video constraint driving Stremio's HLS
//     layer doesn't apply to a Qt desktop player.
//     See: agents/audits/stream_a_engine_2026-04-16.md Axis 6
//          (corroborated by stream_d_player_2026-04-17.md D-15 +
//           Cross-Slice Appendix)
//
//   - No subtitle VTT proxy routes. Sidecar decodes ASS / SSA / PGS / text
//     via libass + SubtitleRenderer directly.
//     See: agents/audits/stream_a_engine_2026-04-16.md Axis 5
//
//   - No /create endpoint. fileIdx is pre-resolved in onMetadataReady via
//     autoSelectVideoFile + largest-video heuristic + behaviorHints.filename;
//     functionally equivalent to Stremio's /create for single-tenant native.
//     See: agents/audits/stream_a_engine_2026-04-16.md Axis 10
//
//   - No archive / YouTube / NZB substrate. YouTube returns
//     UNSUPPORTED_SOURCE; archives surface via Sources (Agent 4B's domain),
//     not Stream-A.
//     See: agents/audits/stream_a_engine_2026-04-16.md Axis 11
//
//   - No bare-hash /{infoHash}/{fileIdx} routes. Single-tenant in-process
//     sidecar consumer; the only route shape is /stream/{hash}/{file}.
//     Stremio's React-shell consumer assumptions don't apply.
//     See: agents/audits/stream_a_engine_2026-04-16.md Axis 2 H1
//
//   - No multi-range HTTP byte serving. Single-range parser is RFC 9110
//     compliant + decoder-contract sufficient.
//     See: agents/audits/stream_a_engine_2026-04-16.md Axis 2
//
//   - No backend abstraction / dual-backend support (librqbit swap path).
//     Memory storage / piece waiters / tracker policy evolve WITHIN
//     TorrentEngine without an abstraction layer.
//     See: agents/audits/stream_a_engine_2026-04-16.md Axis 8
//
//   - No memory-first storage model. QFile-from-disk is durable across
//     restarts, simpler, larger-than-RAM possible, lower memory pressure
//     on long sessions. Strategic choice, not a defect.
//     See: agents/audits/stream_a_engine_2026-04-16.md Axis 11 H2
//
// ─────────────────────────────────────────────────────────────────────────────

class TorrentEngine;
class StreamHttpServer;
class StreamPieceWaiter;

enum class StreamPlaybackMode {
    LocalHttp, // torrent-backed (magnet) path served via local HTTP
    DirectUrl, // addon-provided direct URL (http/https/url kinds)
};

struct StreamFileResult {
    bool ok = false;
    StreamPlaybackMode playbackMode = StreamPlaybackMode::LocalHttp;
    QString url;                // http://127.0.0.1:{port}/stream/{hash}/{fileIndex} OR direct URL
    QString infoHash;           // canonical 40-char hex hash from libtorrent (magnet only)
    QString errorCode;          // METADATA_NOT_READY, FILE_NOT_READY, UNKNOWN_TORRENT, ENGINE_ERROR, UNSUPPORTED_SOURCE
    QString errorMessage;
    bool readyToStart = false;
    bool queued = false;
    double fileProgress = 0.0;  // 0.0 - 1.0
    qint64 downloadedBytes = 0;
    qint64 fileSize = 0;
    int selectedFileIndex = -1;
    QString selectedFileName;
};

struct StreamTorrentStatus {
    int peers = 0;
    int dlSpeed = 0;            // bytes/sec
};

// STREAM_ENGINE_FIX Phase 1.1 — substrate observability snapshot.
//
// Returned by StreamEngine::statsSnapshot() for any active stream. Consumers:
// (a) Phase 1.2 structured telemetry log facility, (b) future Slice D player
// UI buffering state surface, (c) future Slice 3a progress-tracking cadence,
// (d) Agent 4 agent-side Rule-15 log reads.
//
// All time fields are milliseconds-since-engine-start (monotonic via the
// engine's own QElapsedTimer; -1 sentinel = event not yet observed). Byte
// fields are zero-default; piece-range fields are -1 sentinel.
//
// Construction is cheap — pure projection of StreamSession state + 1-2 calls
// into TorrentEngine for piece coverage data. Safe to call at telemetry
// cadence (5-15s) without engine load impact.
struct StreamEngineStats {
    QString infoHash;
    int     activeFileIndex            = -1;
    qint64  metadataReadyMs            = -1;
    qint64  firstPieceArrivalMs        = -1;
    qint64  gateProgressBytes          = 0;
    qint64  gateSizeBytes              = 0;
    double  gateProgressPct            = 0.0;
    int     prioritizedPieceRangeFirst = -1;
    int     prioritizedPieceRangeLast  = -1;
    int     peers                      = 0;
    qint64  dlSpeedBps                 = 0;
    bool    cancelled                  = false;
    int     trackerSourceCount         = 0;

    // STREAM_ENGINE_REBUILD P5 — stall watchdog projection. Additive per
    // Congress 5 Amendment 2 freeze discipline (signature-frozen, default-
    // valued extensions that read zero on existing consumers). `stalled`
    // flips true when StreamPieceWaiter::longestActiveWaitMs > 4000 on an
    // in-window piece; `stallElapsedMs` carries the elapsed wait at tick
    // time; `stallPiece` + `stallPeerHaveCount` let the UI surface
    // scheduler-starvation vs swarm-starvation without a separate query.
    bool    stalled                    = false;
    qint64  stallElapsedMs             = 0;
    int     stallPiece                 = -1;
    int     stallPeerHaveCount         = -1;
};

class StreamEngine : public QObject
{
    Q_OBJECT

public:
    explicit StreamEngine(TorrentEngine* engine, const QString& cacheDir,
                          QObject* parent = nullptr);
    ~StreamEngine() override;

    // Start/stop the HTTP server
    bool start();
    void stop();
    int httpPort() const;

    // Main streaming API — caller polls this until ok==true.
    // Magnet path: caller polls until ok==true and URL is a local HTTP URL.
    // Direct/HTTP path: returns immediately with ok==true.
    StreamFileResult streamFile(const QString& magnetUri,
                                int fileIndex = -1,
                                const QString& fileNameHint = {});

    // Phase 4.3 addon-aware overload: dispatches by source kind.
    // Magnet → delegates to the magnet streamFile above.
    // Url/Http → immediate DirectUrl result (skips torrent path).
    // YouTube → UNSUPPORTED_SOURCE error.
    StreamFileResult streamFile(const tankostream::addon::Stream& stream);

    // Stop and clean up a stream
    void stopStream(const QString& infoHash);

    // Stop all active streams
    void stopAll();

    // Query torrent status for UI
    StreamTorrentStatus torrentStatus(const QString& infoHash) const;

    // STREAM_ENGINE_FIX Phase 1.1 — substrate observability snapshot.
    // Pure read; safe to call from any thread (locks m_mutex internally).
    // Returns sentinel-defaulted struct for unknown infoHash.
    StreamEngineStats statsSnapshot(const QString& infoHash) const;

    // PLAYER_STREMIO_PARITY_FIX Phase 1 Batch 1.1 — buffered-range
    // observability for the active stream's selected file. Returns sorted
    // non-overlapping {startByte, endByte} ranges (file-local, endByte
    // exclusive) of fully-downloaded pieces within the selected file.
    // Thin wrapper over TorrentEngine::fileByteRangesOfHavePieces that
    // resolves the selected file index under m_mutex. Returns empty list
    // for unknown infoHash, not-yet-metadata-ready streams, or invalid
    // selection. Safe to poll at the 1 Hz cadence SeekSlider will consume
    // it at; cost is O(N_pieces) under m_mutex + TorrentEngine::m_mutex.
    //
    // Consumer: StreamPlayerController::pollStreamStatus (Batch 1.2) emits
    // the snapshot via bufferedRangesChanged signal directly to VideoPlayer
    // per Agent 3's Rule-14 reshape of TODO §Batch-1.2 (skips sidecar
    // round-trip — sidecar has no use for this data, direct main-app flow
    // matches existing bufferUpdate pattern).
    QList<QPair<qint64, qint64>> contiguousHaveRanges(const QString& infoHash) const;

    // STREAM_PLAYBACK_FIX Phase 2 Batch 2.3 — sliding-window deadline
    // retargeting. Called from the StreamPage progressUpdated lambda,
    // rate-limited by the caller (currently once per 2s). Looks up the
    // stream's selected file size + index, converts positionSec to a
    // byte offset, and asks TorrentEngine for deadlines across the next
    // ~windowBytes of pieces. Cheap + idempotent: re-calling with the
    // same position is a no-op on libtorrent's side (same piece, same
    // deadline → update in place).
    void updatePlaybackWindow(const QString& infoHash,
                              double positionSec, double durationSec,
                              qint64 windowBytes = 20LL * 1024 * 1024);

    // Pair with updatePlaybackWindow. Called on player close / back to
    // browse / source switch so libtorrent isn't pre-fetching ahead of a
    // playback position that no longer exists. Safe to call on an unknown
    // infoHash.
    void clearPlaybackWindow(const QString& infoHash);

    // STREAM_PLAYBACK_FIX Phase 2 Batch 2.4 — seek/resume target pre-gate.
    // Called by StreamPage before handing a resume offset to the player.
    // Translates (positionSec / durationSec) to a byte offset, sets urgent
    // (200ms→500ms) deadlines on the first `prefetchBytes` worth of pieces
    // around that offset, and returns whether those pieces are already
    // contiguous. If true, caller opens the player immediately; if false,
    // caller shows a "Seeking..." overlay and retries (either via
    // singleShot or by re-calling this function). Fires the deadline set
    // on every call so the caller's retry cadence keeps libtorrent's
    // urgency up to date without saturating it.
    bool prepareSeekTarget(const QString& infoHash,
                           double positionSec, double durationSec,
                           qint64 prefetchBytes = 3LL * 1024 * 1024);

    // STREAM_LIFECYCLE_FIX Phase 5 Batch 5.1 — per-stream cancellation token.
    // Returned shared_ptr is the same one stored in the StreamSession's
    // `cancelled` field. `stopStream` sets it to true BEFORE erasing the
    // record, so workers already holding the shared_ptr (captured at
    // handleConnection time) observe cancellation even after the record
    // is gone from m_streams. Lock-free check via `token->load()` in the
    // waitForPieces poll loop. Returns an empty shared_ptr if the infoHash
    // isn't registered — StreamHttpServer's handleConnection treats that
    // as "no cancellation token, fall through to pre-5.1 behavior."
    std::shared_ptr<std::atomic<bool>> cancellationToken(const QString& infoHash) const;

    // STREAM_ENGINE_REBUILD P2 — shared piece-wait primitive. Lives on
    // StreamEngine so every HTTP worker thread consults the same
    // (hash, pieceIdx) → Waiter* registry and receives the same
    // TorrentEngine::pieceFinished signal fan-out. Lifetime ties to
    // StreamEngine (parent=this); always non-null post-ctor.
    StreamPieceWaiter* pieceWaiter() const { return m_pieceWaiter; }

    // Clean up orphaned cache data from previous sessions
    void cleanupOrphans();

    // Start periodic cleanup timer (call once after start)
    void startPeriodicCleanup();

signals:
    void streamReady(const QString& infoHash, const QString& url);
    void streamError(const QString& infoHash, const QString& message);

private slots:
    void onMetadataReady(const QString& infoHash, const QString& name,
                         qint64 totalSize, const QJsonArray& files);
    void onTorrentProgress(const QString& infoHash, float progress,
                           int dlSpeed, int ulSpeed, int peers, int seeds);
    void onTorrentError(const QString& infoHash, const QString& message);

    // STREAM_ENGINE_FIX Phase 1.2 — periodic telemetry emit. Fires every 5s
    // when telemetry is enabled; walks active streams, emits a snapshot per
    // stream. No-op when telemetry disabled (env var unset). The 5s cadence
    // covers both gate-open and serving phases without phase-distinguishing
    // logic — log volume bounded at one record per stream per 5s, well
    // under "busy-log" thresholds for typical 1-3 active streams.
    void emitTelemetrySnapshots();

private:
    // STREAM_ENGINE_REBUILD P3/P6 — per-hash record is the externalized
    // `StreamSession` struct at src/core/stream/StreamSession.h. P3 added
    // Prioritizer + SeekClassifier state (cached position, EMA speed,
    // bitrate hint, lastSeekType, firstClassification bit). P6 collapsed
    // the legacy metadataReady/registered bool pair into a stored
    // `StreamSession::state` enum.

    // STREAM_ENGINE_FIX Phase 1.1 — gate target. Hoisted from streamFile so
    // statsSnapshot reports the same gate the streaming path enforces.
    // STREAM_ENGINE_REBUILD P4.2 — dropped 5MB → 1MB to align with sidecar
    // Tier-1 probe (512KB probesize / 750ms analyzeduration); gate at 2x
    // probe budget gives probe room to escalate to Tier 2 (2MB) if needed
    // without the gate artificially withholding pieces the probe wants.
    // Single source of truth.
    static constexpr qint64 kGateBytes = 1LL * 1024 * 1024;

    int autoSelectVideoFile(const QJsonArray& files, const QString& hint) const;
    QString buildStreamUrl(const QString& infoHash, int fileIndex) const;
    void applyStreamPriorities(const QString& infoHash, int fileIndex, int totalFiles);

    static bool isVideoExtension(const QString& path);

    TorrentEngine* m_torrentEngine;
    // STREAM_ENGINE_REBUILD P2 — declaration order matters: the piece-waiter
    // is created FIRST (Qt child index 0 on `this`) so it is destroyed LAST.
    // The HTTP server is created second, and its dtor drains in-flight
    // worker threads (which may still hold StreamPieceWaiter pointers) with
    // a 2 s timeout. Destroying the waiter first would risk use-after-free
    // from a wedged worker that outlives the drain budget.
    StreamPieceWaiter* m_pieceWaiter;
    StreamHttpServer* m_httpServer;
    QString m_cacheDir;
    QTimer* m_cleanupTimer = nullptr;

    // STREAM_ENGINE_FIX Phase 1.2 — periodic telemetry timer. Started in
    // ctor at 5000ms interval; fires emitTelemetrySnapshots(). When the
    // env-var gate is off the slot short-circuits cheaply.
    QTimer* m_telemetryTimer = nullptr;

    // STREAM_ENGINE_REBUILD P3 — 1 Hz re-assert tick. Walks m_streams; for
    // each Serving session with a recent `updatePlaybackWindow` feed
    // (lastPlaybackTickMs within the past 10 s), routes through the
    // Prioritizer to re-emit the normal-streaming deadline window so
    // libtorrent's time-critical table stays warm between the 2 s StreamPage
    // telemetry ticks. Cheap when no stream is Serving; skipped when the
    // session has no position feed yet (cold-open before the first
    // updatePlaybackWindow call).
    QTimer* m_reassertTimer = nullptr;

    // STREAM_ENGINE_REBUILD P5/P6 — 2 s stall watchdog tick. Reads
    // m_pieceWaiter->longestActiveWait(); if the longest in-flight wait is
    // > 4000 ms on an in-window piece and not already flagged on the
    // session, marks the session stalled, re-asserts priority 7, emits
    // `stall_detected` telemetry, and leaves the UI projection flipped on
    // in StreamEngineStats. On the first tick after the stall clears
    // (either the blocked piece arrived or the waiter count dropped),
    // emits `stall_recovered` and resets the session fields.
    QTimer* m_stallTimer = nullptr;

    // STREAM_ENGINE_FIX Phase 1.1 — monotonic clock started in ctor; supplies
    // ms-since-engine-start timestamps for StreamSession observability fields
    // (metadataReadyMs / firstPieceArrivalMs). Monotonic to survive
    // wall-clock jumps (NTP / DST); engine-relative because absolute epoch
    // is irrelevant for telemetry deltas.
    QElapsedTimer m_clock;

    mutable QMutex m_mutex;
    QHash<QString, StreamSession> m_streams;

    // STREAM_ENGINE_REBUILD P3 — internal dispatch helpers. Both run under
    // m_mutex acquired by the caller; neither acquires it themselves.
    void onReassertTick();
    void reassertStreamingPriorities(StreamSession& s);

    // STREAM_ENGINE_REBUILD P5 — stall watchdog tick. Acquires m_mutex
    // itself; calls m_pieceWaiter->longestActiveWait() outside the
    // StreamEngine-mutex scope to avoid nesting against the waiter's own
    // lock.
    void onStallTick();
};
