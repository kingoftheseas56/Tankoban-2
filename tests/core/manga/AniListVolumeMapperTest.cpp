// tests/core/manga/AniListVolumeMapperTest.cpp
#include "core/manga/anilist/AniListVolumeMapper.h"

#include <gtest/gtest.h>

using namespace tankoban::manga::anilist;

namespace {

AniListChapter ch(const QString& num, int boundVol = -1)
{
    AniListChapter c;
    c.number       = num;
    c.title        = QString();
    c.boundVolume  = boundVol;
    return c;
}

MediaDetail makeDetail(const QString& title, int totalVolumes, int totalChapters,
                       const QString& status, const QList<AniListChapter>& chapters)
{
    MediaDetail d;
    d.preview.title  = title;
    d.preview.status = status;
    d.totalVolumes   = totalVolumes;
    d.totalChapters  = totalChapters;
    d.chapters       = chapters;
    return d;
}

} // namespace

TEST(AniListVolumeMapperTest, ExtractChapterNumericContract)
{
    // Direct coverage for the public extractChapterNumeric helper - header
    // documents these cases but the 5 map() tests only exercise pure-int
    // inputs. Closes the public-API coverage gap.
    EXPECT_EQ(AniListVolumeMapper::extractChapterNumeric(QStringLiteral("12")),         12);
    EXPECT_EQ(AniListVolumeMapper::extractChapterNumeric(QStringLiteral("12.5")),       12);
    EXPECT_EQ(AniListVolumeMapper::extractChapterNumeric(QStringLiteral("Prologue 1")),  1);
    EXPECT_EQ(AniListVolumeMapper::extractChapterNumeric(QString()),                   -1);
    EXPECT_EQ(AniListVolumeMapper::extractChapterNumeric(QStringLiteral("Extras")),    -1);
}

TEST(AniListVolumeMapperTest, CompletedSeriesProducesNVolumesNoVolX)
{
    // Death Note: 12 vols, 108 chapters, status FINISHED.
    QList<AniListChapter> chapters;
    for (int i = 1; i <= 108; ++i) chapters.append(ch(QString::number(i)));
    const auto rows = AniListVolumeMapper::map(
        makeDetail(QStringLiteral("Death Note"), 12, 108, QStringLiteral("FINISHED"), chapters));

    ASSERT_EQ(rows.size(), 12);
    for (int i = 0; i < 12; ++i) {
        EXPECT_EQ(rows[i].volumeNumber, i + 1);
        EXPECT_FALSE(rows[i].isVolumeX);
        EXPECT_EQ(rows[i].chapterCount, 9);  // 108 / 12 = 9
    }
    // No Vol X for completed series.
    bool anyVolX = false;
    for (const auto& r : rows) if (r.isVolumeX) anyVolX = true;
    EXPECT_FALSE(anyVolX);
}

TEST(AniListVolumeMapperTest, LiveFetchShapeProducesVolumesFromTotalsOnly)
{
    // AniList live-detail responses can provide total counts without the
    // chapter edge list. Death Note shape: 12 vols, 108 chapters, FINISHED.
    const auto rows = AniListVolumeMapper::map(
        makeDetail(QStringLiteral("Death Note"), 12, 108, QStringLiteral("FINISHED"), {}));

    ASSERT_EQ(rows.size(), 12);
    for (int i = 0; i < 12; ++i) {
        EXPECT_EQ(rows[i].volumeNumber, i + 1);
        EXPECT_FALSE(rows[i].isVolumeX);
        EXPECT_EQ(rows[i].chapterCount, 9);
        EXPECT_EQ(rows[i].chapterRangeStart, i * 9 + 1);
        EXPECT_EQ(rows[i].chapterRangeEnd, (i + 1) * 9);
    }
    EXPECT_EQ(rows.first().chapterNumbers.first(), QStringLiteral("1"));
    EXPECT_EQ(rows.last().chapterNumbers.last(), QStringLiteral("108"));
}

TEST(AniListVolumeMapperTest, OngoingFullyBoundProducesNVolumesNoVolX)
{
    // Hypothetical: 5 vols, 40 chapters, status RELEASING but all chapters
    // happen to land in bound vols (8 each). No Vol X needed.
    QList<AniListChapter> chapters;
    for (int i = 1; i <= 40; ++i) chapters.append(ch(QString::number(i)));
    const auto rows = AniListVolumeMapper::map(
        makeDetail(QStringLiteral("Hypothetical"), 5, 40, QStringLiteral("RELEASING"), chapters));

    ASSERT_EQ(rows.size(), 5);
    for (const auto& r : rows) EXPECT_FALSE(r.isVolumeX);
}

TEST(AniListVolumeMapperTest, OngoingWithUnboundTailProducesVolX)
{
    // One Piece-ish: 111 bound vols (888 chapters at 8/vol), 1146 latest
    // chapter. Status RELEASING. Vol X should hold chapters 889-1146.
    QList<AniListChapter> chapters;
    for (int i = 1; i <= 1146; ++i) chapters.append(ch(QString::number(i)));
    const auto rows = AniListVolumeMapper::map(
        makeDetail(QStringLiteral("One Piece"), 111, 888, QStringLiteral("RELEASING"), chapters));

    // 111 bound vols + 1 Vol X
    ASSERT_EQ(rows.size(), 112);
    for (int i = 0; i < 111; ++i) {
        EXPECT_EQ(rows[i].volumeNumber, i + 1);
        EXPECT_FALSE(rows[i].isVolumeX);
    }
    EXPECT_TRUE(rows[111].isVolumeX);
    EXPECT_EQ(rows[111].volumeNumber, kVolumeXNumber);
    EXPECT_EQ(rows[111].chapterCount, 1146 - 888);  // 258 chapters in Vol X
}

TEST(AniListVolumeMapperTest, EmptyChaptersListReturnsEmpty)
{
    const auto rows = AniListVolumeMapper::map(
        makeDetail(QStringLiteral("Unknown"), 0, 0, QStringLiteral("NOT_YET_RELEASED"), {}));
    EXPECT_TRUE(rows.isEmpty());
}

TEST(AniListVolumeMapperTest, OngoingWithNoBoundVolumesProducesOnlyVolX)
{
    // Pure-tail series (no vols bound yet, only loose chapters). All
    // chapters should go into Vol X.
    QList<AniListChapter> chapters;
    for (int i = 1; i <= 12; ++i) chapters.append(ch(QString::number(i)));
    const auto rows = AniListVolumeMapper::map(
        makeDetail(QStringLiteral("FreshSeries"), 0, 0, QStringLiteral("RELEASING"), chapters));

    ASSERT_EQ(rows.size(), 1);
    EXPECT_TRUE(rows[0].isVolumeX);
    EXPECT_EQ(rows[0].chapterCount, 12);
}
