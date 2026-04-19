#pragma once

#include <QList>
#include <QPair>
#include <QtGlobal>

#include "StreamSeekClassifier.h"

// STREAM_ENGINE_REBUILD P3 — deadline-math calculator ported from
// Stremio Reference.
//
// Two entry points:
//
// 1. `calculateStreamingPriorities(Params)` — port of
//    `enginefs/src/backend/priorities.rs:56-225` `calculate_priorities`.
//    Drives the normal-streaming re-assert tick (Sequential SeekType):
//    urgency window = `max(15, bitrate × 15 s / piece_len)` +
//    `proactive_bonus` (up to 45 s at bitrate if speed > 1.5 × bitrate,
//    or +20 pieces if speed > 5 MB/s with unknown bitrate), clamped to
//    `min(cacheMaxPieces, 300)`. Head window = `5 s × max(bitrate, speed)`
//    clamped to [5 MB, 50 MB], then divided by `pieceLength` and clamped
//    to [5, 250] pieces. Per-piece deadline by priorityLevel:
//      ≥ 250 : 50 ms flat (metadata probes)
//      ≥ 100 : 10 + d × 10 ms  (seeking)
//      = 0   : 20000 + d × 200 ms (background pre-cache)
//      else  : normal streaming staircase:
//              distance < 5  → 10 + d × 50 ms (CRITICAL HEAD: 10/60/110/160/210)
//              distance < head_window → 250 + (d-5) × 50 ms (HEAD linear)
//              d > urgent_base → 10000 + d × 50 ms (proactive area)
//              else → 5000 + d × 20 ms (standard body)
//
// 2. `seekDeadlines(SeekType, ...)` — port of `handle.rs:260-365` per-seek-
//    type windows, returning (pieceIdx, deadlineMs) pairs. Complements
//    `calculateStreamingPriorities`: the seek path gets a small, aggressive
//    window (2-4 pieces) at URGENT / CRITICAL / CONTAINER-INDEX deadlines;
//    the streaming path re-asserts the larger urgency window on Sequential
//    ticks. Caller sets priority=7 on the returned pieces (handle.rs:308 —
//    priority + deadline BOTH required; Phase 2.6.1 telemetry confirmed
//    deadline-alone is insufficient under swarm pressure).
//
// M4 (MAX_STARTUP_PIECES re-verification, integration-memo §5) —
// re-verified 2026-04-18 at Stremio `priorities.rs:6-12`:
//   MIN_STARTUP_BYTES  = 1 MB
//   MAX_STARTUP_PIECES = 2
//   MIN_STARTUP_PIECES = 1
// Pinned as compile-time constants here. R21 mtime still matches motion
// snapshot (no drift).
//
// M5 (first-piece-target clarification, integration-memo §5) — InitialPlayback
// targets `handle.rs:305-311` URGENT tier: base deadline 0 ms, staircase
// `adjusted_deadline + i × 10 ms` (so piece 0 = 0 ms, piece 1 = 10 ms, etc.).
// This is NOT the `calculate_priorities` CRITICAL HEAD branch (`10 + d × 50 ms`
// at `priorities.rs:194-198`) — that branch applies to normal-streaming
// pieces within 5 of the current, not to cold-open first pieces.
//
// Pure functions — no QObject, no state, no mutex. StreamSession owns the
// state (current position, cached bitrate, EMA speed) and feeds inputs in.
namespace StreamPrioritizer {

// Compile-time constants re-verified from Stremio priorities.rs:6-12 per M4.
constexpr qint64 kMinStartupBytes   = 1LL * 1024 * 1024;  // 1 MB
constexpr int    kMaxStartupPieces  = 2;
constexpr int    kMinStartupPieces  = 1;

// Input bag for calculateStreamingPriorities. Matches Stremio's
// calculate_priorities signature semantically; we use a struct to keep the
// call site readable (7 positional args becomes a mess).
struct Params {
    int     currentPiece     = 0;
    int     totalPieces      = 0;
    qint64  pieceLength      = 0;
    qint64  cacheSizeBytes   = 10LL * 1024 * 1024 * 1024;  // 10 GB default
    bool    cacheEnabled     = true;
    quint8  priorityLevel    = 1;      // 0/1=normal, >=100 seeking, >=250 metadata
    qint64  downloadSpeed    = 0;      // bytes/sec (EMA)
    qint64  bitrate          = 0;      // bytes/sec (0 = unknown)
};

// Normal-streaming deadlines across the urgency + buffer window. Returns
// (pieceIdx, deadlineMs) pairs. Caller does NOT set priority=7 on these —
// the deadline staircase alone is what Stremio emits here (priority=1 from
// `config.enabled` branch `handle.set_piece_deadline` without preceding
// `set_piece_priority(p, 7)` in stream.rs poll_read path).
QList<QPair<int, int>> calculateStreamingPriorities(const Params& p);

// Seek-path deadlines per handle.rs:260-365 seek_type match. Returns
// (pieceIdx, deadlineMs) for the window; caller sets priority=7 on every
// returned pieceIdx (mandatory per STREAM_ENGINE_FIX Phase 2.6.3 empirical
// finding — priority + deadline together win against libtorrent's general
// piece scheduler, deadline alone does not).
//
// Window sizes (handle.rs):
//   InitialPlayback : clamp(ceil(MIN_STARTUP_BYTES/pieceLen), MIN, MAX) = 1-2 pieces
//                     base deadline = 0 ms ("URGENT"); tail-metadata set
//                     separately via initialPlaybackTailDeadlines.
//   UserScrub       : 4 pieces, base 300 ms ("CRITICAL")
//   ContainerMetadata: 2 pieces, base 100 ms ("CONTAINER-INDEX")
//   Sequential      : returns empty — use calculateStreamingPriorities instead
//
// `speedFactor` multiplies base deadline per handle.rs:287-292 (URGENT stays
// at 0 ms regardless). Default 1.0 == no adjustment; dynamic speed-factor
// wiring deferred until empirical evidence motivates it.
QList<QPair<int, int>> seekDeadlines(StreamSeekType type,
                                      int startPiece,
                                      int lastPiece,
                                      qint64 fileSize,
                                      qint64 pieceLength,
                                      double speedFactor = 1.0);

// Tail-metadata deadlines for InitialPlayback ONLY — handle.rs:322-331 says
// tail is "deferred until after startup" so the first frame is not delayed
// by end-of-file work. Returns 1-2 pairs (last_piece @ 1200 ms, last_piece-1
// @ 1250 ms) if the tail is beyond the head window; empty if the head window
// already covers the full file.
QList<QPair<int, int>> initialPlaybackTailDeadlines(int startPiece,
                                                     int lastPiece,
                                                     int headWindowSize);

// Size of the URGENT window for InitialPlayback. Separated so the caller
// can compute `startPiece + window` for the tail-deferral test above
// without duplicating the Stremio clamping math.
int initialPlaybackWindowSize(qint64 fileSize, qint64 pieceLength);

}  // namespace StreamPrioritizer
