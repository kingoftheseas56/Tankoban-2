// src/core/stream/EpisodeDisplayState.cpp
#include "core/stream/EpisodeDisplayState.h"

namespace tankostream::stream {

EpisodeDisplayState deriveEpisodeDisplayState(const EpisodeStateInputs& in)
{
    // A genuinely-completed file present on disk is Downloaded — and a Complete
    // record beats any stale transfer snapshot (the original "Queued over a
    // finished episode" bug). But a file merely EXISTING is NOT enough: the
    // engine pre-allocates the destination file when a download starts, so an
    // active transfer (even at 98%) must win over a partial on-disk file.
    if (in.onDisk && in.complete) return EpisodeDisplayState::Downloaded;
    if (in.hasTransfer && in.failed) return EpisodeDisplayState::Failed;
    if (in.hasTransfer && in.paused) return EpisodeDisplayState::Paused;
    if (in.hasTransfer)              return EpisodeDisplayState::Downloading;
    if (in.queued)                   return EpisodeDisplayState::Queued;
    // No live transfer: a bare file on disk (local-scan import, or a completed
    // file whose snapshot has drained without a Complete flag) is Downloaded.
    if (in.onDisk)                   return EpisodeDisplayState::Downloaded;
    return EpisodeDisplayState::NotDownloaded;
}

}  // namespace tankostream::stream
