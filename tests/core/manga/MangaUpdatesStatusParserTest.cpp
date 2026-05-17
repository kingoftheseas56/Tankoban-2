#include "core/manga/mangaupdates/MangaUpdatesStatusParser.h"

#include <gtest/gtest.h>

using namespace tankoban::manga::mangaupdates;

TEST(MangaUpdatesStatusParserTest, OnePieceOngoing)
{
    EXPECT_EQ(MangaUpdatesStatusParser::parseLeadingVolumeCount(
        QStringLiteral("114 Volumes (Ongoing)")), 114);
}

TEST(MangaUpdatesStatusParserTest, BerserkOngoing)
{
    EXPECT_EQ(MangaUpdatesStatusParser::parseLeadingVolumeCount(
        QStringLiteral("43 Volumes (Ongoing)")), 43);
}

TEST(MangaUpdatesStatusParserTest, KingdomOngoing)
{
    EXPECT_EQ(MangaUpdatesStatusParser::parseLeadingVolumeCount(
        QStringLiteral("79 Volumes (Ongoing)")), 79);
}

TEST(MangaUpdatesStatusParserTest, DeathNoteCompleteWithBunkobanIgnoresVariants)
{
    EXPECT_EQ(MangaUpdatesStatusParser::parseLeadingVolumeCount(
        QStringLiteral("12 Volumes + 1 Extra Volume (Complete); 7 Bunkoban Volumes (Complete); 1 Bunkoban Volume (Complete)")),
        12);
}

TEST(MangaUpdatesStatusParserTest, EmptyStringReturnsZero)
{
    EXPECT_EQ(MangaUpdatesStatusParser::parseLeadingVolumeCount(QString()), 0);
}

TEST(MangaUpdatesStatusParserTest, NoVolumesTokenReturnsZero)
{
    EXPECT_EQ(MangaUpdatesStatusParser::parseLeadingVolumeCount(
        QStringLiteral("Hiatus")), 0);
}

TEST(MangaUpdatesStatusParserTest, SingleVolumeMatches)
{
    EXPECT_EQ(MangaUpdatesStatusParser::parseLeadingVolumeCount(
        QStringLiteral("1 Volume (Complete)")), 1);
}
