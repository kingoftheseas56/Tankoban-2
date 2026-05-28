#include <gtest/gtest.h>
#include "core/manga/VolumeQualityClassifier.h"

using namespace tankoban::manga;

namespace {
MangaVolume vol(int n, int start, int end) {
    MangaVolume v; v.volumeNumber = n;
    v.chapterRangeStart = start; v.chapterRangeEnd = end; return v;
}
ChapterInfo chap(double n, bool scanned) {
    ChapterInfo c; c.chapterNumber = n; c.isVolumeScanned = scanned; return c;
}
} // namespace

TEST(VolumeQualityClassifier, AllVioletVolumeIsClean) {
    const auto out = VolumeQualityClassifier::classify(
        { vol(1, 1, 8) },
        { chap(1, true), chap(5, true), chap(8, true) });
    ASSERT_EQ(out.size(), 1);
    EXPECT_EQ(out[0].volumeNumber, 1);
    EXPECT_FALSE(out[0].isVolumeX);
    EXPECT_EQ(out[0].quality, VolumeQuality::Clean);
}

TEST(VolumeQualityClassifier, AnyGrayChapterMakesVolumeMagazine) {
    const auto out = VolumeQualityClassifier::classify(
        { vol(110, 1100, 1109) },
        { chap(1100, true), chap(1105, false), chap(1109, true) });
    ASSERT_EQ(out.size(), 1);
    EXPECT_EQ(out[0].quality, VolumeQuality::Magazine);
}

TEST(VolumeQualityClassifier, ChaptersPastLastCatalogVolumeBecomeVolumeX) {
    const auto out = VolumeQualityClassifier::classify(
        { vol(110, 1100, 1109) },
        { chap(1100, true), chap(1110, false), chap(1111, false) });
    ASSERT_EQ(out.size(), 2);
    EXPECT_EQ(out[0].volumeNumber, 110);
    EXPECT_TRUE(out[1].isVolumeX);
    EXPECT_EQ(out[1].volumeNumber, tankoban::manga::anilist::kVolumeXNumber);
    EXPECT_EQ(out[1].quality, VolumeQuality::Magazine);
    ASSERT_EQ(out[1].chapterNumbers.size(), 2);
    EXPECT_DOUBLE_EQ(out[1].chapterNumbers[0], 1110.0);
    EXPECT_DOUBLE_EQ(out[1].chapterNumbers[1], 1111.0);
}

TEST(VolumeQualityClassifier, VolumeWithNoMemberChaptersIsOmitted) {
    const auto out = VolumeQualityClassifier::classify(
        { vol(1, 1, 8), vol(2, 9, 17) },
        { chap(1, true), chap(8, true) });   // no ch in vol 2 range
    ASSERT_EQ(out.size(), 1);
    EXPECT_EQ(out[0].volumeNumber, 1);
}

TEST(VolumeQualityClassifier, NoChaptersYieldsEmpty) {
    const auto out = VolumeQualityClassifier::classify({ vol(1, 1, 8) }, {});
    EXPECT_TRUE(out.isEmpty());
}
