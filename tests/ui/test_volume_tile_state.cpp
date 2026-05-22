// tests/ui/test_volume_tile_state.cpp
//
// TDD red-phase: VolumeTile::computeState pure-logic coverage.
// COMICS_CATALOG_SERIES_VIEW Phase 2 Task 8 (2026-05-23).
//
// No QApplication needed -- computeState is static and takes only
// bool + QString. These tests will fail until Task 9 provides
// the VolumeTile.cpp definition.

#include <gtest/gtest.h>
#include "ui/pages/comics/VolumeTile.h"

using tankoban::ui::comics::VolumeTile;
using State = tankoban::ui::comics::VolumeTileState::State;

TEST(VolumeTileComputeState, NoIndexEntryNoStatus_IsNotStarted) {
    EXPECT_EQ(VolumeTile::computeState(false, QString()), State::NotStarted);
}

TEST(VolumeTileComputeState, IndexEntryPresent_IsComplete) {
    EXPECT_EQ(VolumeTile::computeState(true, QString()), State::Complete);
}

TEST(VolumeTileComputeState, NoEntry_QueuedStatus_IsQueued) {
    EXPECT_EQ(VolumeTile::computeState(false, QStringLiteral("Queued · #3 in queue")), State::Queued);
}

TEST(VolumeTileComputeState, NoEntry_DownloadingStatus_IsDownloading) {
    EXPECT_EQ(VolumeTile::computeState(false, QStringLiteral("Downloading · 38%")), State::Downloading);
}

TEST(VolumeTileComputeState, NoEntry_FailedStatus_IsFailed) {
    EXPECT_EQ(VolumeTile::computeState(false, QStringLiteral("Failed · no seeds")), State::Failed);
}

TEST(VolumeTileComputeState, IndexEntry_BeatsAnyStatus) {
    // Presence in MangaDownloadIndex always wins -- defensive against stale
    // transient status from a prior dispatch run that wasn't cleared.
    EXPECT_EQ(VolumeTile::computeState(true, QStringLiteral("Downloading · 50%")), State::Complete);
}
