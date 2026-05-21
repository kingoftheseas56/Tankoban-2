// tests/core/manga/fandom/test_local_fandom_catalog_loader.cpp
#include <gtest/gtest.h>

#include "core/manga/fandom/LocalFandomCatalogLoader.h"
#include "core/manga/fandom/FandomTypes.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

using namespace tankoban::manga::fandom;

namespace {

QString writeFixture(QTemporaryDir& dir, const QString& filename, const QString& content) {
    const QString path = dir.filePath(filename);
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << content;
    f.close();
    return path;
}

const QString kOnePieceMinimal = R"({
  "seriesId": "one-piece",
  "anilistId": 30013,
  "fandomUrl": "https://onepiece.fandom.com/wiki/Chapters_and_Volumes/Volumes",
  "scrapedAt": "2026-05-21T11:33:39Z",
  "volumes": [
    {"number": 1, "title": "Romance Dawn", "synopsis": "", "coverUrl": "https://static.wikia.nocookie.net/onepiece/images/0/0e/Volume_1.png", "chapterStart": "1", "chapterEnd": "8"},
    {"number": 2, "title": "Buggy the Clown", "synopsis": "", "coverUrl": "https://static.wikia.nocookie.net/onepiece/images/2/2f/Volume_2.png", "chapterStart": "9", "chapterEnd": "17"}
  ]
})";

} // namespace

TEST(LocalFandomCatalogLoader, LoadsValidTwoVolumeCatalog) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = writeFixture(tmp, "one-piece.json", kOnePieceMinimal);

    const auto result = LocalFandomCatalogLoader::loadFromFile(path);
    ASSERT_TRUE(result.has_value());

    const FandomCatalog& cat = *result;
    EXPECT_EQ(cat.seriesId, "one-piece");
    EXPECT_EQ(cat.fandomWikiId, "onepiece");
    EXPECT_EQ(cat.fandomVolumePath, "/wiki/Chapters_and_Volumes/Volumes");
    EXPECT_EQ(cat.schemaVersion, kFandomCatalogSchemaVersion);
    ASSERT_EQ(cat.volumes.size(), 2);

    EXPECT_EQ(cat.volumes[0].volumeNumber, 1);
    EXPECT_EQ(cat.volumes[0].titleEnglish, "Romance Dawn");
    EXPECT_EQ(cat.volumes[0].synopsis, "");
    EXPECT_EQ(cat.volumes[0].coverUrlJapanese,
              "https://static.wikia.nocookie.net/onepiece/images/0/0e/Volume_1.png");
    EXPECT_EQ(cat.volumes[0].chapterRangeStart, 1);
    EXPECT_EQ(cat.volumes[0].chapterRangeEnd, 8);

    EXPECT_EQ(cat.volumes[1].volumeNumber, 2);
    EXPECT_EQ(cat.volumes[1].chapterRangeStart, 9);
    EXPECT_EQ(cat.volumes[1].chapterRangeEnd, 17);
}

TEST(LocalFandomCatalogLoader, ReturnsNulloptOnMissingFile) {
    const auto result = LocalFandomCatalogLoader::loadFromFile("/tmp/does_not_exist_xyz.json");
    EXPECT_FALSE(result.has_value());
}

TEST(LocalFandomCatalogLoader, ReturnsNulloptOnMalformedJson) {
    QTemporaryDir tmp;
    const QString path = writeFixture(tmp, "bad.json", "{ this is not json");
    const auto result = LocalFandomCatalogLoader::loadFromFile(path);
    EXPECT_FALSE(result.has_value());
}

TEST(LocalFandomCatalogLoader, ReturnsNulloptOnMissingRequiredFields) {
    QTemporaryDir tmp;
    const QString path = writeFixture(tmp, "stub.json", R"({"seriesId": ""})");
    const auto result = LocalFandomCatalogLoader::loadFromFile(path);
    EXPECT_FALSE(result.has_value());
}

TEST(LocalFandomCatalogLoader, ParsesDecimalChapterStringsByTruncation) {
    // The Python scraper preserves "12.5", "Extra 1" etc as strings.
    // The FandomVolume struct uses int — we truncate to the integer prefix.
    QTemporaryDir tmp;
    const QString fixture = R"({
      "seriesId": "test",
      "anilistId": 1,
      "fandomUrl": "https://test.fandom.com/wiki/X",
      "volumes": [
        {"number": 1, "title": "T", "synopsis": "", "coverUrl": "", "chapterStart": "12.5", "chapterEnd": "20"}
      ]
    })";
    const QString path = writeFixture(tmp, "test.json", fixture);
    const auto result = LocalFandomCatalogLoader::loadFromFile(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->volumes[0].chapterRangeStart, 12);
    EXPECT_EQ(result->volumes[0].chapterRangeEnd, 20);
}

TEST(LocalFandomCatalogLoader, PreservesEmptySynopsisAsEmptyString) {
    // One Piece honest-empties: Fandom doesn't carry per-volume synopses for it.
    // The loader must not invent placeholder text.
    QTemporaryDir tmp;
    const QString path = writeFixture(tmp, "x.json", kOnePieceMinimal);
    const auto result = LocalFandomCatalogLoader::loadFromFile(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->volumes[0].synopsis.isEmpty());
}

TEST(LocalFandomCatalogLoader, MapsScrapedAtToFetchedAt) {
    QTemporaryDir tmp;
    const QString path = writeFixture(tmp, "x.json", kOnePieceMinimal);
    const auto result = LocalFandomCatalogLoader::loadFromFile(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->fetchedAt.toUTC().toString(Qt::ISODate), "2026-05-21T11:33:39Z");
}
