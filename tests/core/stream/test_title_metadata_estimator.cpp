#include <gtest/gtest.h>

#include "core/stream/TitleMetadataEstimator.h"

using tankoban::stream::theatre::estimate;
using tankoban::stream::theatre::ScopeEstimate;

TEST(TitleMetadataEstimatorTest, CompleteSeries_NoEpisodeCount) {
    const auto s = estimate("Sopranos.Complete.Series.1080p.BluRay");
    EXPECT_TRUE(s.isCompleteSeries);
    EXPECT_TRUE(s.episodes.isEmpty());
    EXPECT_TRUE(s.detectedSeasons.isEmpty());
}

TEST(TitleMetadataEstimatorTest, SeasonPack_DefaultEpisodeCount) {
    const auto s = estimate("Daredevil.Born.Again.S01.COMPLETE.1080p");
    EXPECT_FALSE(s.isCompleteSeries);
    EXPECT_EQ(QList<int>{1}, s.detectedSeasons);
    // Default to 10-episode estimate for a season pack with no count hint.
    EXPECT_EQ(10, s.episodes.size());
    EXPECT_EQ(1,  s.episodes.first().season);
    EXPECT_EQ(1,  s.episodes.first().episode);
    EXPECT_EQ(1,  s.episodes.last().season);
    EXPECT_EQ(10, s.episodes.last().episode);
}

TEST(TitleMetadataEstimatorTest, SeasonPack_ExplicitEpisodeCount) {
    const auto s = estimate("Show.S02.Complete.13.Episodes.1080p");
    EXPECT_TRUE(s.hasExplicitEpisodeCount);
    EXPECT_EQ(13, s.episodes.size());
}

TEST(TitleMetadataEstimatorTest, MultiEpisode_RangeFromTitle) {
    const auto s = estimate("Show.S01E01-E03.1080p");
    EXPECT_FALSE(s.isCompleteSeries);
    EXPECT_TRUE(s.hasExplicitEpisodeCount);
    EXPECT_EQ(3, s.episodes.size());
    EXPECT_EQ(1, s.episodes.first().episode);
    EXPECT_EQ(3, s.episodes.last().episode);
}

TEST(TitleMetadataEstimatorTest, SingleEpisode_OneTile) {
    const auto s = estimate("Show.S01E05.1080p");
    EXPECT_EQ(1, s.episodes.size());
    EXPECT_EQ(1, s.episodes.first().season);
    EXPECT_EQ(5, s.episodes.first().episode);
}

TEST(TitleMetadataEstimatorTest, MultiSeason_DefaultEpisodesPerSeason) {
    const auto s = estimate("Show.S01-S03.1080p.BluRay");
    EXPECT_EQ((QList<int>{1, 2, 3}), s.detectedSeasons);
    // 3 seasons * 10 default episodes each = 30 episodes total.
    EXPECT_EQ(30, s.episodes.size());
}

TEST(TitleMetadataEstimatorTest, SortedByEpisodeOrder) {
    const auto s = estimate("Show.S03.S01.S02.Complete");
    EXPECT_EQ((QList<int>{1, 2, 3}), s.detectedSeasons);
    // First episode should be S1E1, last should be S3E10.
    EXPECT_EQ(1, s.episodes.first().season);
    EXPECT_EQ(1, s.episodes.first().episode);
    EXPECT_EQ(3, s.episodes.last().season);
    EXPECT_EQ(10, s.episodes.last().episode);
}

TEST(TitleMetadataEstimatorTest, EmptyTitle_EmptyEstimate) {
    const auto s = estimate("");
    EXPECT_FALSE(s.isCompleteSeries);
    EXPECT_TRUE(s.episodes.isEmpty());
    EXPECT_TRUE(s.detectedSeasons.isEmpty());
}
