#pragma once

// STREAM_SERVER_PIVOT Phase 3 (2026-04-25) — types-only header.
//
// Split out of the deleted StreamEngine.h so that StreamServerEngine and its
// consumers (StreamPage, StreamPlayerController, VideoPlayer, LoadingOverlay,
// SidecarProcess, etc.) have a stable home for the stream-engine-facing data
// shapes without a dependency on a dead legacy engine header. No virtual
// interface, no inheritance, no logic — just structs + one enum.
//
// Pre-Phase-3, these definitions lived in StreamEngine.h:69-135. That header
// was deleted along with the legacy libtorrent engine (Phase 3 of the
// stream-server pivot, 2026-04-25). Phase 2 audited 51 references across 12
// files; consumers now #include this header instead of StreamEngine.h.

#include <QString>

enum class StreamPlaybackMode {
    LocalHttp, // torrent-backed (magnet) path served via local HTTP
    DirectUrl, // addon-provided direct URL (http/https/url kinds)
};

struct StreamFileResult {
    bool ok = false;
    StreamPlaybackMode playbackMode = StreamPlaybackMode::LocalHttp;
    QString url;                // http://127.0.0.1:{port}/{hash}/{fileIndex} OR direct URL
    QString infoHash;           // canonical 40-char hex hash (magnet path only)
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

// Observability snapshot returned by the stream engine's statsSnapshot() for
// any active stream. Consumers: runtime telemetry writes, the player's
// buffering-state surface, the progress-tracking cadence, and agent-side
// log reads. Construction is cheap — pure projection of per-hash state.
//
// Time fields are milliseconds-since-engine-start (monotonic; -1 sentinel =
// event not yet observed). Byte fields are zero-default; piece-range fields
// are -1 sentinel.
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

    // Stall-watchdog projection. `stalled` flips true when the engine
    // observes a wait exceeding the stall threshold on an in-window piece;
    // `stallElapsedMs` carries the elapsed wait at tick time; `stallPiece`
    // + `stallPeerHaveCount` let the UI surface scheduler-starvation vs
    // swarm-starvation without a separate query. StreamServerEngine
    // leaves these at default sentinels (stream-server doesn't expose a
    // per-piece bitfield) — Phase 2B decision, aligns with the text-free
    // indeterminate LoadingOverlay.
    bool    stalled                    = false;
    qint64  stallElapsedMs             = 0;
    int     stallPiece                 = -1;
    int     stallPeerHaveCount         = -1;
};
