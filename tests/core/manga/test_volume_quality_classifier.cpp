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

TEST(VolumeQualityClassifier, VolumeXChapterSpanFloorsMinCeilsMax) {
    auto span = volumeXChapterSpan({1183.0, 1184.5});
    EXPECT_EQ(span.first, 1183);   // floor(min)
    EXPECT_EQ(span.second, 1185);  // ceil(max)
}

TEST(VolumeQualityClassifier, VolumeXChapterSpanEmptyIsZero) {
    auto span = volumeXChapterSpan({});
    EXPECT_EQ(span.first, 0);
    EXPECT_EQ(span.second, 0);
}

namespace {
MangaVolume volCover(int n, int start, int end) {
    MangaVolume v = vol(n, start, end);
    v.coverUrlJapanese = QStringLiteral("http://cover/v%1.jpg").arg(n);
    return v;
}
} // namespace

// A cover-less catalog volume is a MangaFire auto-bucket that isn't a real
// tankobon (e.g. One Piece "vol 116/117"). Its chapters must fold into Volume X,
// and it must NOT be emitted as its own classified volume.
TEST(VolumeQualityClassifier, CoverlessTrailingVolumesFoldIntoVolumeX) {
    const auto out = VolumeQualityClassifier::classify(
        { volCover(1, 1, 2), vol(2, 3, 4) },   // vol 2 has no cover -> fake
        { chap(1, true), chap(2, true), chap(3, false), chap(4, false) });
    ASSERT_EQ(out.size(), 2);            // real vol 1 + Volume X (NOT fake vol 2)
    EXPECT_EQ(out[0].volumeNumber, 1);
    EXPECT_FALSE(out[0].isVolumeX);
    EXPECT_TRUE(out[1].isVolumeX);
    EXPECT_EQ(out[1].chapterNumbers.size(), 2);  // ch 3 + 4 from the fake volume
}

// Fallback: when NO catalog volume carries a cover (some series), treat all as
// real so we don't dump everything into Volume X. (Mirrors the legacy tests'
// cover-less helper.)
TEST(VolumeQualityClassifier, NoCoversAnywhereTreatsAllVolumesAsReal) {
    const auto out = VolumeQualityClassifier::classify(
        { vol(1, 1, 2), vol(2, 3, 4) },
        { chap(1, true), chap(2, true), chap(3, true), chap(4, true) });
    ASSERT_EQ(out.size(), 2);            // both volumes real, no Volume X
    EXPECT_EQ(out[0].volumeNumber, 1);
    EXPECT_EQ(out[1].volumeNumber, 2);
    EXPECT_FALSE(out[1].isVolumeX);
}
