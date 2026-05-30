// src/core/stream/EpisodeDisplayState.cpp
#include "core/stream/EpisodeDisplayState.h"

namespace tankostream::stream {

EpisodeDisplayState deriveEpisodeDisplayState(const EpisodeStateInputs& in)
{
    // Disk is the single source of truth — a file on disk is Downloaded,
    // full stop, regardless of any stale transfer/index record.
    if (in.onDisk)       return EpisodeDisplayState::Downloaded;
    if (!in.hasTransfer) return EpisodeDisplayState::NotDownloaded;
    if (in.failed)       return EpisodeDisplayState::Failed;
    if (in.paused)       return EpisodeDisplayState::Paused;
    return EpisodeDisplayState::Downloading;
}

}  // namespace tankostream::stream
