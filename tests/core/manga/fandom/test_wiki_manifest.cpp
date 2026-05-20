// tests/core/manga/fandom/test_wiki_manifest.cpp
//
// GoogleTest verification for WikiManifest::fromJson (plan Task 2).
// Loads tests/fixtures/fandom/manifests/death-note.json + asserts every
// field round-trips correctly.

#include <gtest/gtest.h>

#include "core/manga/fandom/WikiManifest.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

using tankoban::manga::fandom::ExtractorType;
using tankoban::manga::fandom::FieldExpectation;
using tankoban::manga::fandom::PageModel;
using tankoban::manga::fandom::PaginationModel;
using tankoban::manga::fandom::WikiManifest;

namespace {

QJsonObject loadFixtureObject(const QString& relPath)
{
    QFile f(QStringLiteral(TANKOBAN_TEST_FIXTURE_DIR) + QStringLiteral("/") + relPath);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    return doc.object();
}

} // anonymous

TEST(WikiManifestTest, ParsesDeathNoteManifestCorrectly)
{
    const QJsonObject obj = loadFixtureObject(QStringLiteral("fandom/manifests/death-note.json"));
    ASSERT_FALSE(obj.isEmpty()) << "death-note.json fixture missing or invalid JSON";

    WikiManifest m = WikiManifest::fromJson(obj);

    EXPECT_EQ(m.seriesId.toStdString(),       "death-note");
    EXPECT_EQ(m.wikidataQid.toStdString(),    "Q14559");
    EXPECT_EQ(m.fandomWikiId.toStdString(),   "deathnote");
    EXPECT_EQ(m.volumePagePath.toStdString(), "/wiki/List_of_Death_Note_chapters");
    EXPECT_EQ(m.pageModel,                    PageModel::ChapterBranded);
    EXPECT_EQ(m.extractorType,                ExtractorType::Table);
    EXPECT_EQ(m.chapterKeyword.toStdString(), "Chapter");
    EXPECT_EQ(m.groupingSemantics.toStdString(), "subsection-headers");

    EXPECT_EQ(m.editionFilters.size(),        2);
    EXPECT_TRUE(m.editionFilters.contains(QStringLiteral("Volume 13: How to Read")));
    EXPECT_TRUE(m.editionFilters.contains(QStringLiteral("Short Stories")));

    EXPECT_EQ(m.unitHierarchy.size(), 2);
    EXPECT_EQ(m.unitHierarchy.at(0).toStdString(), "chapter");
    EXPECT_EQ(m.unitHierarchy.at(1).toStdString(), "volume");

    EXPECT_EQ(m.expectedFields.value(QStringLiteral("titleEnglish")), FieldExpectation::Required);
    EXPECT_EQ(m.expectedFields.value(QStringLiteral("titleJapanese")), FieldExpectation::Required);
    EXPECT_EQ(m.expectedFields.value(QStringLiteral("isbnEn")),       FieldExpectation::Expected);
    EXPECT_EQ(m.expectedFields.value(QStringLiteral("isbnJp")),       FieldExpectation::Expected);
    EXPECT_EQ(m.expectedFields.value(QStringLiteral("coverUrlEnglish")), FieldExpectation::Expected);
    EXPECT_EQ(m.expectedFields.value(QStringLiteral("synopsis")),     FieldExpectation::Absent);

    // No pagination fields on Death Note (single chapter-branded page).
    EXPECT_EQ(m.paginationModel, PaginationModel::None);
    EXPECT_TRUE(m.pagePathPattern.isEmpty());
    EXPECT_EQ(m.pageRangeSize, 0);

    EXPECT_TRUE(m.isValid());
}

TEST(WikiManifestTest, EmptyJsonProducesInvalidManifest)
{
    QJsonObject empty;
    WikiManifest m = WikiManifest::fromJson(empty);
    EXPECT_FALSE(m.isValid());
    EXPECT_TRUE(m.seriesId.isEmpty());
    EXPECT_TRUE(m.fandomWikiId.isEmpty());
}

TEST(WikiManifestTest, MissingFandomWikiIdMarksManifestInvalid)
{
    QJsonObject partial;
    partial.insert(QStringLiteral("seriesId"), QStringLiteral("foo"));
    // no fandomWikiId
    WikiManifest m = WikiManifest::fromJson(partial);
    EXPECT_FALSE(m.isValid());
}

TEST(WikiManifestTest, UnknownPageModelStringDefaultsToMonolith)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("seriesId"),     QStringLiteral("test"));
    obj.insert(QStringLiteral("fandomWikiId"), QStringLiteral("testwiki"));
    obj.insert(QStringLiteral("pageModel"),    QStringLiteral("unknown-shape"));

    WikiManifest m = WikiManifest::fromJson(obj);
    EXPECT_EQ(m.pageModel, PageModel::Monolith);
}

TEST(WikiManifestTest, KingdomStylePaginationFieldsRoundTrip)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("seriesId"),         QStringLiteral("kingdom"));
    obj.insert(QStringLiteral("fandomWikiId"),     QStringLiteral("kingdom"));
    obj.insert(QStringLiteral("pageModel"),        QStringLiteral("hierarchy"));
    obj.insert(QStringLiteral("paginationModel"),  QStringLiteral("range-pages"));
    obj.insert(QStringLiteral("pagePathPattern"),  QStringLiteral("/wiki/Vol.%1"));
    obj.insert(QStringLiteral("pageRangeSize"),    10);
    obj.insert(QStringLiteral("maxVolumeProbe"),   80);

    WikiManifest m = WikiManifest::fromJson(obj);
    EXPECT_EQ(m.paginationModel, PaginationModel::RangePages);
    EXPECT_EQ(m.pagePathPattern.toStdString(), "/wiki/Vol.%1");
    EXPECT_EQ(m.pageRangeSize, 10);
    EXPECT_EQ(m.maxVolumeProbe, 80);
}
