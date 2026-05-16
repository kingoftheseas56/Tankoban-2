#include <gtest/gtest.h>
#include "core/stream/QualityScorer.h"

using tankostream::stream::QualityScorer;

TEST(QualityScorerTest, ResolutionScore_DetectsCommonTags) {
    EXPECT_EQ(100, QualityScorer::resolutionScore("Show.S01E01.2160p.BluRay.mkv"));
    EXPECT_EQ(100, QualityScorer::resolutionScore("Show.S01E01.4K.HDR.mkv"));
    EXPECT_EQ(90,  QualityScorer::resolutionScore("Show.S01E01.1440p.WEB-DL.mkv"));
    EXPECT_EQ(80,  QualityScorer::resolutionScore("Show.S01E01.1080p.BluRay.mkv"));
    EXPECT_EQ(60,  QualityScorer::resolutionScore("Show.S01E01.720p.HDTV.mkv"));
    EXPECT_EQ(40,  QualityScorer::resolutionScore("Show.S01E01.480p.DVDRip.mkv"));
    EXPECT_EQ(20,  QualityScorer::resolutionScore("Show.S01E01.mkv"));
    // Edge: empty string and extensionless filenames default to 20 (no tag).
    EXPECT_EQ(20, QualityScorer::resolutionScore(""));
    EXPECT_EQ(20, QualityScorer::resolutionScore("Show"));
    // Edge: multi-tag — highest-tier wins via cascade short-circuit.
    EXPECT_EQ(100, QualityScorer::resolutionScore("Show.1080p.2160p.mkv"));
}

TEST(QualityScorerTest, ResolutionScore_CaseInsensitive) {
    EXPECT_EQ(80, QualityScorer::resolutionScore("Show.S01E01.1080P.BluRay.mkv"));
    EXPECT_EQ(80, QualityScorer::resolutionScore("show.s01e01.1080p.bluray.mkv"));
}

TEST(QualityScorerTest, SourceScore_DetectsCommonTags) {
    EXPECT_EQ(100, QualityScorer::sourceScore("Show.S01E01.1080p.BluRay.mkv"));
    EXPECT_EQ(100, QualityScorer::sourceScore("Show.S01E01.1080p.BDRip.mkv"));
    EXPECT_EQ(80,  QualityScorer::sourceScore("Show.S01E01.1080p.WEB-DL.mkv"));
    EXPECT_EQ(80,  QualityScorer::sourceScore("Show.S01E01.1080p.WEBDL.mkv"));
    EXPECT_EQ(60,  QualityScorer::sourceScore("Show.S01E01.720p.HDTV.mkv"));
    EXPECT_EQ(50,  QualityScorer::sourceScore("Show.S01E01.720p.WEBRip.mkv"));
    EXPECT_EQ(40,  QualityScorer::sourceScore("Show.S01E01.480p.DVDRip.mkv"));
    EXPECT_EQ(20,  QualityScorer::sourceScore("Show.S01E01.mkv"));
}

TEST(QualityScorerTest, QualityScore_IsWeightedCombo) {
    // 1080p BluRay: 0.7 * 80 + 0.3 * 100 = 56 + 30 = 86
    EXPECT_EQ(86, QualityScorer::qualityScore("Show.S01E01.1080p.BluRay.mkv"));
    // 720p HDTV: 0.7 * 60 + 0.3 * 60 = 60
    EXPECT_EQ(60, QualityScorer::qualityScore("Show.S01E01.720p.HDTV.mkv"));
    // No tags: 0.7 * 20 + 0.3 * 20 = 20
    EXPECT_EQ(20, QualityScorer::qualityScore("Show.S01E01.mkv"));
}

TEST(QualityScorerTest, HealthScore_LogScale) {
    EXPECT_EQ(0,   QualityScorer::healthScore(0));     // log2(1)*10 = 0
    EXPECT_EQ(10,  QualityScorer::healthScore(1));     // log2(2)*10 = 10
    EXPECT_EQ(20,  QualityScorer::healthScore(3));     // log2(4)*10 = 20
    EXPECT_EQ(50,  QualityScorer::healthScore(31));    // log2(32)*10 = 50
    EXPECT_EQ(100, QualityScorer::healthScore(1023));  // log2(1024)*10 = 100
    EXPECT_EQ(100, QualityScorer::healthScore(5000));  // capped at 100
    // Edge: negative seeders clamp to 0.
    EXPECT_EQ(0, QualityScorer::healthScore(-5));
}

TEST(QualityScorerTest, CombinedScore_WeightedAverage) {
    // (80 * 0.6 + 50 * 0.4) / (0.6 + 0.4) = (48 + 20) / 1.0 = 68
    EXPECT_DOUBLE_EQ(68.0, QualityScorer::combinedScore(80, 50, 0.6, 0.4));
    // All quality, no health: returns quality
    EXPECT_DOUBLE_EQ(80.0, QualityScorer::combinedScore(80, 50, 1.0, 0.0));
    // All health, no quality: returns health
    EXPECT_DOUBLE_EQ(50.0, QualityScorer::combinedScore(80, 50, 0.0, 1.0));
    // Equal weights
    EXPECT_DOUBLE_EQ(65.0, QualityScorer::combinedScore(80, 50, 0.5, 0.5));
}

TEST(QualityScorerTest, CombinedScore_ZeroWeightsGuard) {
    EXPECT_DOUBLE_EQ(0.0, QualityScorer::combinedScore(80, 50, 0.0, 0.0));
}
