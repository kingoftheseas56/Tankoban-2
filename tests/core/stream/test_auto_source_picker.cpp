#include <gtest/gtest.h>
#include "core/stream/AutoSourcePicker.h"

using tankostream::stream::AutoSourcePicker;
using tankostream::stream::SourceCandidate;

static SourceCandidate c(const QString& title, int seeders, qint64 sizeBytes, int qualitySort) {
    SourceCandidate s; s.title = title; s.seeders = seeders; s.sizeBytes = sizeBytes; s.qualitySort = qualitySort; return s;
}

TEST(AutoSourcePicker, PicksHighestSeeded1080p) {
    QList<SourceCandidate> v {
        c("One Piece S01E01 [SubsPlease] 1080p", 1200, 1400000000LL, 3),
        c("One Piece S01E01 1080p WEB-DL",          300, 1500000000LL, 3),
    };
    auto idx = AutoSourcePicker::pick(v, 24);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 0);
}

TEST(AutoSourcePicker, ExcludesNon1080p) {
    QList<SourceCandidate> v {
        c("Show S01E01 720p",  900, 700000000LL, 2),
        c("Show S01E01 2160p", 800, 9000000000LL, 5),
    };
    EXPECT_FALSE(AutoSourcePicker::pick(v, 24).has_value());
}

TEST(AutoSourcePicker, ExcludesZeroSeeders) {
    QList<SourceCandidate> v { c("Show S01E01 1080p", 0, 1400000000LL, 3) };
    EXPECT_FALSE(AutoSourcePicker::pick(v, 24).has_value());
}

TEST(AutoSourcePicker, ExcludesCamRips) {
    EXPECT_TRUE(AutoSourcePicker::isCamRip("Movie 2024 1080p CAM"));
    EXPECT_TRUE(AutoSourcePicker::isCamRip("Movie.2024.TELESYNC.1080p"));
    EXPECT_TRUE(AutoSourcePicker::isCamRip("Movie.2024.HDCAM.1080p"));
    EXPECT_FALSE(AutoSourcePicker::isCamRip("Movie.2024.1080p.WEB-DL"));
    EXPECT_FALSE(AutoSourcePicker::isCamRip("GUTS.S01E01.1080p"));  // no false-positive on 'TS'

    QList<SourceCandidate> v { c("Movie 2024 1080p CAM", 500, 3000000000LL, 3) };
    EXPECT_FALSE(AutoSourcePicker::pick(v, 120).has_value());
}

TEST(AutoSourcePicker, WellSeededIgnoresSize) {
    QList<SourceCandidate> v { c("Show S01E01 1080p", 1200, 30000000000LL, 3) };
    auto idx = AutoSourcePicker::pick(v, 24);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 0);
}

TEST(AutoSourcePicker, LowSeedTailDropsReencode) {
    QList<SourceCandidate> v { c("Show S01E01 1080p", 4, 180000000LL, 3) };
    EXPECT_FALSE(AutoSourcePicker::pick(v, 24).has_value());
}

TEST(AutoSourcePicker, LowSeedTailDropsRemux) {
    QList<SourceCandidate> v { c("Show S01E01 1080p", 6, 13000000000LL, 3) };
    EXPECT_FALSE(AutoSourcePicker::pick(v, 24).has_value());
}

TEST(AutoSourcePicker, LowSeedTailKeepsSaneSized) {
    QList<SourceCandidate> v { c("Show S01E01 1080p", 5, 1400000000LL, 3) };
    auto idx = AutoSourcePicker::pick(v, 24);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 0);
}

TEST(AutoSourcePicker, UnknownRuntimeSkipsSizeGuardrail) {
    QList<SourceCandidate> v { c("Show S01E01 1080p", 5, 180000000LL, 3) };
    auto idx = AutoSourcePicker::pick(v, 0);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 0);
}

TEST(AutoSourcePicker, EmptyListReturnsNone) {
    EXPECT_FALSE(AutoSourcePicker::pick({}, 24).has_value());
}
