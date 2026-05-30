// tests/core/stream/test_episode_display_state.cpp
#include <gtest/gtest.h>
#include "core/stream/EpisodeDisplayState.h"

using tankostream::stream::deriveEpisodeDisplayState;
using tankostream::stream::EpisodeDisplayState;
using tankostream::stream::EpisodeStateInputs;

static EpisodeStateInputs in(bool onDisk, bool complete, bool hasTransfer,
                             bool paused, bool failed, int pct) {
    EpisodeStateInputs i;
    i.onDisk = onDisk; i.complete = complete; i.hasTransfer = hasTransfer;
    i.paused = paused; i.failed = failed; i.progressPct = pct;
    return i;
}

TEST(EpisodeDisplayState, CompleteFileOnDiskIsDownloaded) {
    // A genuinely-completed file present on disk is Downloaded — and a Complete
    // record beats any stale transfer snapshot (the original "Queued over a
    // finished episode" bug).
    EXPECT_EQ(deriveEpisodeDisplayState(in(true, true, true,  false, false, 100)),
              EpisodeDisplayState::Downloaded);
    EXPECT_EQ(deriveEpisodeDisplayState(in(true, true, false, false, false, 0)),
              EpisodeDisplayState::Downloaded);
    EXPECT_EQ(deriveEpisodeDisplayState(in(true, true, true,  true,  true,  0)),
              EpisodeDisplayState::Downloaded);
}

TEST(EpisodeDisplayState, PreAllocatedPartialFileIsDownloadingNotDownloaded) {
    // THE Hemanth-smoke bug (2026-05-30): the engine pre-allocates the file at
    // its final path when a download starts, so the file is on disk at 98% — but
    // it is NOT complete, and an active transfer must win.
    EXPECT_EQ(deriveEpisodeDisplayState(in(true, false, true, false, false, 98)),
              EpisodeDisplayState::Downloading);
    EXPECT_EQ(deriveEpisodeDisplayState(in(true, false, true, true,  false, 40)),
              EpisodeDisplayState::Paused);
}

TEST(EpisodeDisplayState, NoFileNoTransferIsNotDownloaded) {
    EXPECT_EQ(deriveEpisodeDisplayState(in(false, false, false, false, false, 0)),
              EpisodeDisplayState::NotDownloaded);
}

TEST(EpisodeDisplayState, ActiveTransferIsDownloading) {
    EXPECT_EQ(deriveEpisodeDisplayState(in(false, false, true, false, false, 30)),
              EpisodeDisplayState::Downloading);
}

TEST(EpisodeDisplayState, PausedTransferIsPaused) {
    EXPECT_EQ(deriveEpisodeDisplayState(in(false, false, true, true, false, 30)),
              EpisodeDisplayState::Paused);
}

TEST(EpisodeDisplayState, FailedBeatsPausedAndDownloading) {
    EXPECT_EQ(deriveEpisodeDisplayState(in(false, false, true, true, true, 10)),
              EpisodeDisplayState::Failed);
    EXPECT_EQ(deriveEpisodeDisplayState(in(false, false, true, false, true, 10)),
              EpisodeDisplayState::Failed);
}

TEST(EpisodeDisplayState, BareFileNoTransferIsDownloaded) {
    // Local-scan import: a file on disk with no record + no transfer is Downloaded.
    EXPECT_EQ(deriveEpisodeDisplayState(in(true, false, false, false, false, 0)),
              EpisodeDisplayState::Downloaded);
}
