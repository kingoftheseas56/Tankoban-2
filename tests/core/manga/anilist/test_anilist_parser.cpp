// tests/core/manga/anilist/test_anilist_parser.cpp
#include <gtest/gtest.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include "core/manga/anilist/AniListParser.h"

using namespace tankoban::manga::anilist;

namespace {
QJsonObject parseObj(const char* json)
{
    return QJsonDocument::fromJson(QByteArray(json)).object();
}
} // namespace

TEST(AniListParser, ParsesBaselineDeathNoteShape)
{
    const QJsonObject media = parseObj(R"({
        "id": 30021,
        "title": {
            "english": "Death Note",
            "romaji": "Death Note",
            "native": "デスノート",
            "userPreferred": "Death Note"
        },
        "synonyms": [],
        "coverImage": {
            "medium": "https://example.com/dn-med.jpg",
            "large":  "https://example.com/dn-lg.jpg",
            "extraLarge": "https://example.com/dn-xl.jpg"
        },
        "bannerImage": "https://example.com/dn-banner.jpg",
        "format": "MANGA",
        "status": "FINISHED",
        "startDate": { "year": 2003 },
        "genres": ["Drama", "Mystery", "Supernatural", "Thriller"],
        "description": "Light Yagami finds a mysterious notebook..."
    })");

    const MediaPreview p = parseMediaPreviewFromJson(media);
    EXPECT_EQ(p.anilistId, 30021);
    EXPECT_EQ(p.title, QStringLiteral("Death Note"));
    EXPECT_EQ(p.coverThumbUrl, QStringLiteral("https://example.com/dn-med.jpg"));
    EXPECT_EQ(p.coverFullUrl, QStringLiteral("https://example.com/dn-lg.jpg"));
    EXPECT_EQ(p.bannerUrl, QStringLiteral("https://example.com/dn-banner.jpg"));
    EXPECT_EQ(p.format, QStringLiteral("MANGA"));
    EXPECT_EQ(p.status, QStringLiteral("FINISHED"));
    EXPECT_EQ(p.yearStarted, 2003);
    EXPECT_EQ(p.genres.size(), 4);
    EXPECT_EQ(p.genres.at(0), QStringLiteral("Drama"));
    // Task 3 will extend with assertions on staff/tags/countryOfOrigin.
}

TEST(AniListParser, ParsesStaffEdgesWithRoles)
{
    const QJsonObject media = parseObj(R"({
        "id": 30021,
        "title": { "english": "Death Note" },
        "staff": {
            "edges": [
                { "role": "Story", "node": { "name": { "full": "Tsugumi Ohba" } } },
                { "role": "Art",   "node": { "name": { "full": "Takeshi Obata" } } }
            ]
        }
    })");

    const MediaPreview p = parseMediaPreviewFromJson(media);
    ASSERT_EQ(p.staff.size(), 2);
    EXPECT_EQ(p.staff.at(0).name, QStringLiteral("Tsugumi Ohba"));
    EXPECT_EQ(p.staff.at(0).role, QStringLiteral("Story"));
    EXPECT_EQ(p.staff.at(1).name, QStringLiteral("Takeshi Obata"));
    EXPECT_EQ(p.staff.at(1).role, QStringLiteral("Art"));
}

TEST(AniListParser, ParsesEmptyStaffEdgesAsEmptyList)
{
    const QJsonObject media = parseObj(R"({
        "id": 30021,
        "title": { "english": "Foo" },
        "staff": { "edges": [] }
    })");
    const MediaPreview p = parseMediaPreviewFromJson(media);
    EXPECT_TRUE(p.staff.isEmpty());
}

TEST(AniListParser, ParsesMissingStaffKeyAsEmptyList)
{
    const QJsonObject media = parseObj(R"({
        "id": 30021,
        "title": { "english": "Foo" }
    })");
    const MediaPreview p = parseMediaPreviewFromJson(media);
    EXPECT_TRUE(p.staff.isEmpty());
}

TEST(AniListParser, ParsesRankedTagsPreservingRankAndSpoilerFlag)
{
    const QJsonObject media = parseObj(R"({
        "id": 30021,
        "title": { "english": "Death Note" },
        "tags": [
            { "name": "Detective",     "rank": 95, "isMediaSpoiler": false },
            { "name": "Psychological", "rank": 93, "isMediaSpoiler": false },
            { "name": "Anti-Hero",     "rank": 82, "isMediaSpoiler": true  }
        ]
    })");

    const MediaPreview p = parseMediaPreviewFromJson(media);
    ASSERT_EQ(p.tags.size(), 3);
    EXPECT_EQ(p.tags.at(0).name,      QStringLiteral("Detective"));
    EXPECT_EQ(p.tags.at(0).rank,      95);
    EXPECT_FALSE(p.tags.at(0).isSpoiler);
    EXPECT_EQ(p.tags.at(2).name,      QStringLiteral("Anti-Hero"));
    EXPECT_TRUE(p.tags.at(2).isSpoiler);
}

TEST(AniListParser, ParsesCountryOfOrigin)
{
    const QJsonObject media = parseObj(R"({
        "id": 30021,
        "title": { "english": "Death Note" },
        "countryOfOrigin": "JP"
    })");
    EXPECT_EQ(parseMediaPreviewFromJson(media).countryOfOrigin,
              QStringLiteral("JP"));
}
