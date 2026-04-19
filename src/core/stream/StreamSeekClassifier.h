#pragma once

#include <QtGlobal>

// STREAM_ENGINE_REBUILD P3 — deterministic seek classification.
//
// Ported shape from Stremio Reference `stream-server-master/enginefs/src/
// backend/libtorrent/stream.rs:11-20` (enum) + `stream.rs:452-459` (classifier)
// + `priorities.rs:14-20` (container_metadata_start).
//
// Four SeekType values drive divergent deadline semantics in StreamPrioritizer:
//
// - Sequential        — normal forward read, no deadline cleanup, just extend
//                       the urgency window from the current piece.
// - InitialPlayback   — cold-open first request (offset=0). URGENT tier in
//                       Stremio (0 ms deadline, `MIN_STARTUP_PIECES`..
//                       `MAX_STARTUP_PIECES` window per `handle.rs:305-311`)
//                       plus deferred tail-metadata at 1200 ms.
// - UserScrub         — user seek to a new mid-file position. Clears existing
//                       piece deadlines + rebuilds around the new offset
//                       (matches Stremio `stream.rs:101-108`). Tail metadata
//                       deadlines are NOT cleared — defensive M6 invariant per
//                       integration memo §5 (Agent 4 Rule-14 call, 2026-04-18:
//                       path (b) defensive-preserve chosen over path (a)
//                       empirical-repro; the invariant is sound regardless of
//                       whether Tankoban's current `clearPieceDeadlines` call
//                       empirically stalls on the tail).
// - ContainerMetadata — seek target is in the last 10 MB OR last 5 % of the
//                       file (whichever starts earlier — Stremio
//                       `priorities.rs:16-20`). Adds priorities for the
//                       container-metadata region WITHOUT clearing head
//                       deadlines (preserves cold-open head work per Stremio
//                       `stream.rs:92-99`).
//
// `classifySeek` is the pure entry point: given the new target byte offset
// and the file size, returns the SeekType. `InitialPlayback` is injected by
// the caller on the very first classification after Session creation; every
// subsequent call returns Sequential / UserScrub / ContainerMetadata based
// on the current offset alone.
enum class StreamSeekType {
    Sequential,
    InitialPlayback,
    UserScrub,
    ContainerMetadata,
};

namespace StreamSeekClassifier {

// Stremio `container_metadata_start` at priorities.rs:16-20:
//   saturating_sub(10 MB).min(file_size * 95 / 100)
// i.e. the region is "last 10 MB OR last 5 %, whichever starts earlier" so
// that small files (< 200 MB) still get a meaningful tail-of-file region
// without the 10 MB subtraction underflow-clamping to zero.
qint64 containerMetadataStart(qint64 fileSize);

// Classify a seek to `targetByteOffset` within a file of `fileSize`.
// Pure function — no TorrentEngine call, no Session lookup. Caller is
// responsible for signalling the `isFirstClassification` bit on the very
// first call after Session construction (cold-open cases where we want
// Stremio's URGENT tier semantics instead of the CRITICAL-UserScrub
// semantics for the 0-offset read).
StreamSeekType classifySeek(qint64 targetByteOffset,
                            qint64 fileSize,
                            bool isFirstClassification);

}  // namespace StreamSeekClassifier
