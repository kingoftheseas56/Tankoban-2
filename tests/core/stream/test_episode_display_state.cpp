// tests/core/stream/test_episode_display_state.cpp
#include <gtest/gtest.h>
#include "core/stream/EpisodeDisplayState.h"

using tankostream::stream::deriveEpisodeDisplayState;
using tankostream::stream::EpisodeDisplayState;
using tankostream::stream::EpisodeStateInputs;

static EpisodeStateInputs in(bool onDisk, bool hasTransfer, bool paused, bool failed, int pct) {
    EpisodeStateInputs i;
    i.onDisk = onDisk; i.hasTransfer = hasTransfer; i.paused = paused;
    i.failed = failed; i.progressPct = pct;
    return i;
}

TEST(EpisodeDisplayState, OnDiskIsAlwaysDownloaded) {
    // The exact bug: a stale "downloading"/"pending" transfer must NOT override
    // a file that is actually on disk.
    EXPECT_EQ(deriveEpisodeDisplayState(in(true,  true,  false, false, 40)),
              EpisodeDisplayState::Downloaded);
    EXPECT_EQ(deriveEpisodeDisplayState(in(true,  false, false, false, 0)),
              EpisodeDisplayState::Downloaded);
    EXPECT_EQ(deriveEpisodeDisplayState(in(true,  true,  true,  true,  0)),
              EpisodeDisplayState::Downloaded);
}

TEST(EpisodeDisplayState, NoDiskNoTransferIsNotDownloaded) {
    EXPECT_EQ(deriveEpisodeDisplayState(in(false, false, false, false, 0)),
              EpisodeDisplayState::NotDownloaded);
}

TEST(EpisodeDisplayState, ActiveTransferIsDownloading) {
    EXPECT_EQ(deriveEpisodeDisplayState(in(false, true, false, false, 30)),
              EpisodeDisplayState::Downloading);
}

TEST(EpisodeDisplayState, PausedTransferIsPaused) {
    EXPECT_EQ(deriveEpisodeDisplayState(in(false, true, true, false, 30)),
              EpisodeDisplayState::Paused);
}

TEST(EpisodeDisplayState, FailedBeatsPausedAndDownloading) {
    EXPECT_EQ(deriveEpisodeDisplayState(in(false, true, true, true, 10)),
              EpisodeDisplayState::Failed);
    EXPECT_EQ(deriveEpisodeDisplayState(in(false, true, false, true, 10)),
              EpisodeDisplayState::Failed);
}
