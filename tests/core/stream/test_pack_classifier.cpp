#include <gtest/gtest.h>

#include "core/stream/PackClassifier.h"

using tankoban::stream::theatre::classify;
using tankoban::stream::theatre::PackType;

TEST(PackClassifierTest, CompleteSeries_LiteralString) {
    const auto r = classify("Daredevil.Born.Again.Complete.Series.1080p.WEB-DL");
    EXPECT_EQ(PackType::CompleteSeries, r.type);
    EXPECT_TRUE(r.isCompleteSeries);
}

TEST(PackClassifierTest, CompleteSeries_BoxSet) {
    const auto r = classify("The.Sopranos.Complete.Box.Set.1080p.BluRay");
    EXPECT_EQ(PackType::CompleteSeries, r.type);
}

TEST(PackClassifierTest, CompleteSeries_Collection) {
    const auto r = classify("Seinfeld Complete Collection 1989-1998 720p");
    EXPECT_EQ(PackType::CompleteSeries, r.type);
}

TEST(PackClassifierTest, MultiSeason_ExplicitRange) {
    const auto r = classify("Breaking.Bad.S01-S05.1080p.BluRay");
    EXPECT_EQ(PackType::MultiSeason, r.type);
    EXPECT_EQ(5, r.detectedSeasons.size());
    EXPECT_TRUE(r.detectedSeasons.contains(1));
    EXPECT_TRUE(r.detectedSeasons.contains(5));
}

TEST(PackClassifierTest, MultiSeason_DottedRange) {
    const auto r = classify("Game.Of.Thrones.S01.S02.S03.S04.S05.S06.S07.S08.1080p");
    EXPECT_EQ(PackType::MultiSeason, r.type);
    EXPECT_EQ(8, r.detectedSeasons.size());
}

TEST(PackClassifierTest, SeasonPack_CompleteKeyword) {
    const auto r = classify("Daredevil.Born.Again.S01.COMPLETE.1080p.DSNP.WEB-DL");
    EXPECT_EQ(PackType::SeasonPack, r.type);
    EXPECT_EQ(QSet<int>{1}, r.detectedSeasons);
}

TEST(PackClassifierTest, SeasonPack_FullKeyword) {
    const auto r = classify("Sopranos.Season.1.COMPLETE.S01.Full-MIK");
    EXPECT_EQ(PackType::SeasonPack, r.type);
    EXPECT_EQ(QSet<int>{1}, r.detectedSeasons);
}

TEST(PackClassifierTest, SeasonPack_SeasonTagNoCompleteWord) {
    // Just S02 without "complete" / "full" - still likely a season pack if
    // size context says so, but title-only we conservatively call this
    // SeasonPack only when paired with a size or filename hint. With no
    // hint, fall back to SeasonPack since the season tag is the strongest
    // signal absent SxxExx episode patterns.
    const auto r = classify("Sopranos.S02.2160p.HDR.WEB-DL");
    EXPECT_EQ(PackType::SeasonPack, r.type);
}

TEST(PackClassifierTest, SingleEpisode_StandardSxxExx) {
    const auto r = classify("Daredevil.Born.Again.S01E03.1080p.WEB-DL");
    EXPECT_EQ(PackType::SingleEpisode, r.type);
    EXPECT_EQ(QSet<int>{1}, r.detectedSeasons);
}

TEST(PackClassifierTest, SingleEpisode_LowercaseE) {
    const auto r = classify("Daredevil Born Again.2025.Season.01.e03.1080p");
    EXPECT_EQ(PackType::SingleEpisode, r.type);
    EXPECT_EQ(QSet<int>{1}, r.detectedSeasons);
}

TEST(PackClassifierTest, MultiEpisode_RangeWithinSeason) {
    const auto r = classify("Daredevil Born Again S01E01-E03 1080p");
    EXPECT_EQ(PackType::MultiEpisode, r.type);
    EXPECT_EQ(QSet<int>{1}, r.detectedSeasons);
    EXPECT_EQ(3, r.detectedEpisodeCount);
}

TEST(PackClassifierTest, MultiEpisode_DottedEpisodes) {
    const auto r = classify("Daredevil.Born.Again.2025.Season.01.e01.e02.1080p");
    EXPECT_EQ(PackType::MultiEpisode, r.type);
    EXPECT_EQ(QSet<int>{1}, r.detectedSeasons);
}

TEST(PackClassifierTest, Unknown_NoSignal) {
    const auto r = classify("Random.Movie.2025.1080p.WEB-DL");
    EXPECT_EQ(PackType::Unknown, r.type);
    EXPECT_TRUE(r.detectedSeasons.isEmpty());
}

TEST(PackClassifierTest, Unknown_EmptyString) {
    const auto r = classify("");
    EXPECT_EQ(PackType::Unknown, r.type);
}

TEST(PackClassifierTest, LabelForType_HumanReadable) {
    EXPECT_EQ(QStringLiteral("Single Episode"),  tankoban::stream::theatre::labelForType(PackType::SingleEpisode));
    EXPECT_EQ(QStringLiteral("Multi-Episode"),   tankoban::stream::theatre::labelForType(PackType::MultiEpisode));
    EXPECT_EQ(QStringLiteral("Season Pack"),     tankoban::stream::theatre::labelForType(PackType::SeasonPack));
    EXPECT_EQ(QStringLiteral("Multi-Season"),    tankoban::stream::theatre::labelForType(PackType::MultiSeason));
    EXPECT_EQ(QStringLiteral("Complete Series"), tankoban::stream::theatre::labelForType(PackType::CompleteSeries));
}

TEST(PackClassifierTest, PriorityOrdering_CompleteSeriesBeatsEpisode) {
    // Both "Complete Series" marker AND SxxExx present.
    // Step 1 (CompleteSeries short-circuit) must win.
    const auto r = classify("Random.Show.Complete.Series.S01E03.1080p");
    EXPECT_EQ(PackType::CompleteSeries, r.type);
    EXPECT_TRUE(r.isCompleteSeries);
}

TEST(PackClassifierTest, PriorityOrdering_SeasonRangeBeatsEpisode) {
    // Both Sxx-Syy range AND SxxExx present.
    // Step 2 (MultiSeason range) must win over step 5 (SingleEpisode).
    const auto r = classify("Random.Show.S01-S03.S01E01.1080p");
    EXPECT_EQ(PackType::MultiSeason, r.type);
    EXPECT_EQ(3, r.detectedSeasons.size());
}

TEST(PackClassifierTest, PriorityOrdering_EpisodeRangeBeatsSeasonPack) {
    // Both SxxEyy-Ezz range AND a single S-tag present.
    // Step 4 (MultiEpisode) must win over step 6 (SeasonPack fallback).
    const auto r = classify("Random.Show.S02.S02E01-E03.1080p");
    EXPECT_EQ(PackType::MultiEpisode, r.type);
    EXPECT_EQ(3, r.detectedEpisodeCount);
}

TEST(PackClassifierTest, DegenerateRange_HiBelowLoFallsThrough) {
    // S05-S01 has hi < lo; the guard at step 2 should reject it.
    // The dotted-season collector at step 3 will pick up both S05 and S01
    // (2 distinct seasons), so the result should be MultiSeason (NOT
    // SeasonPack - both tags are present).
    const auto r = classify("Random.Show.S05-S01.1080p");
    EXPECT_EQ(PackType::MultiSeason, r.type);
    EXPECT_EQ(2, r.detectedSeasons.size());
    EXPECT_TRUE(r.detectedSeasons.contains(1));
    EXPECT_TRUE(r.detectedSeasons.contains(5));
}
