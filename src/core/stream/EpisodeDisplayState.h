// src/core/stream/EpisodeDisplayState.h
#pragma once

namespace tankostream::stream {

// The single display state for one episode row. Disk-first, but a file merely
// EXISTING is not enough: the torrent engine pre-allocates the destination file
// at its final path the instant a download starts, so a 0-byte/partial file is
// on disk throughout an active download. "Downloaded" therefore means the
// authoritative record is Complete AND the file is present — an ACTIVE transfer
// beats a pre-allocated partial file.
enum class EpisodeDisplayState {
    NotDownloaded,  // no completed file, no active transfer -> Download affordance
    Downloading,    // active transfer (file may be pre-allocated) -> Pause + N%
    Paused,         // active transfer paused                 -> Resume + N%
    Failed,         // transfer errored                        -> Retry affordance
    Downloaded,     // completed file present                  -> Play affordance
};

// Inputs gathered by the caller (authoritative index entry + engine snapshot).
struct EpisodeStateInputs {
    bool onDisk      = false;  // bestEntryForEpisode->canonicalPath + QFileInfo::exists
    bool complete    = false;  // authoritative entry's state == Complete
    bool hasTransfer = false;  // a LIVE transfer covers this episode (index Pending/
                               // Downloading OR engine snapshot entry)
    bool paused      = false;  // that transfer is paused
    bool failed      = false;  // that transfer is errored
    int  progressPct = 0;      // 0..100 (only meaningful when hasTransfer)
};

// Priority: (onDisk && complete) > failed > paused > downloading > bare-onDisk > none.
// A pre-allocated partial file (onDisk but NOT complete) yields Downloading while a
// transfer is live, never Downloaded.
EpisodeDisplayState deriveEpisodeDisplayState(const EpisodeStateInputs& in);

}  // namespace tankostream::stream
