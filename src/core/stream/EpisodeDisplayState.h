// src/core/stream/EpisodeDisplayState.h
#pragma once

namespace tankostream::stream {

// The single display state for one episode row. Derived disk-first:
// on-disk ALWAYS wins (a downloaded file is Downloaded even if a stale
// transfer record says otherwise). Only not-on-disk episodes consult the
// engine for in-progress detail.
enum class EpisodeDisplayState {
    NotDownloaded,  // no file, no active transfer  -> Download affordance
    Downloading,    // not on disk, active transfer -> Pause affordance + N%
    Paused,         // not on disk, transfer paused -> Resume affordance + N%
    Failed,         // not on disk, transfer errored-> Retry affordance
    Downloaded,     // file on disk                 -> Play affordance
};

// Inputs gathered by the caller (disk check + engine transfer lookup).
struct EpisodeStateInputs {
    bool onDisk      = false;  // bestEntryForEpisode path + QFileInfo::exists
    bool hasTransfer = false;  // engine has a transfer covering this episode
    bool paused      = false;  // that transfer is paused
    bool failed      = false;  // that transfer is errored
    int  progressPct = 0;      // 0..100 (only meaningful when hasTransfer)
};

// Priority: onDisk > failed > paused > downloading > none.
EpisodeDisplayState deriveEpisodeDisplayState(const EpisodeStateInputs& in);

}  // namespace tankostream::stream
