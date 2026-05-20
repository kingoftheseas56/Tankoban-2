#include <gtest/gtest.h>
#include <QFile>
#include <QByteArray>
#include "core/book/OpenLibraryClient.h"
#include "core/book/BookCatalogueResult.h"

namespace {
QByteArray loadFixture(const char* relPath) {
    // tankoban_tests runs from out/; fixtures resolved relative to project root.
    // CMake sets TANKOBAN_TEST_FIXTURE_DIR via add_compile_definitions.
    const QString base = QStringLiteral(TANKOBAN_TEST_FIXTURE_DIR);
    QFile f(base + QLatin1Char('/') + QString::fromLatin1(relPath));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}
} // namespace

TEST(OpenLibraryClientParserTest, EmptyResponseReturnsZeroResults) {
    auto bytes = loadFixture("book_catalogue/openlib_search_empty.json");
    ASSERT_FALSE(bytes.isEmpty());
    auto results = OpenLibraryClient::parseSearchResponse(bytes);
    EXPECT_EQ(results.size(), 0);
}

TEST(OpenLibraryClientParserTest, ParsesSingleStandaloneBook) {
    auto bytes = loadFixture("book_catalogue/openlib_search_project_hail_mary.json");
    ASSERT_FALSE(bytes.isEmpty());
    auto results = OpenLibraryClient::parseSearchResponse(bytes);
    ASSERT_EQ(results.size(), 1);
    const auto& r = results.first();
    EXPECT_EQ(r.catalogueId, QStringLiteral("openlib:/works/OL27448W"));
    EXPECT_EQ(r.title, QStringLiteral("Project Hail Mary"));
    EXPECT_EQ(r.author, QStringLiteral("Andy Weir"));
    EXPECT_EQ(r.year, QStringLiteral("2021"));
    EXPECT_EQ(r.publisher, QStringLiteral("Ballantine Books"));
    EXPECT_EQ(r.pages, QStringLiteral("480"));
    EXPECT_EQ(r.language, QStringLiteral("eng"));
    EXPECT_TRUE(r.isbn.contains(QStringLiteral("9780593135204")));
    EXPECT_TRUE(r.genres.contains(QStringLiteral("Hard science fiction")));
    EXPECT_FALSE(r.coverUrl.isEmpty());
    EXPECT_FALSE(r.isSeries);
}

TEST(OpenLibraryClientParserTest, ParsesMultipleBooks) {
    auto bytes = loadFixture("book_catalogue/openlib_search_stormlight.json");
    ASSERT_FALSE(bytes.isEmpty());
    auto results = OpenLibraryClient::parseSearchResponse(bytes);
    ASSERT_EQ(results.size(), 3);
    EXPECT_EQ(results[0].title, QStringLiteral("The Way of Kings"));
    EXPECT_EQ(results[1].title, QStringLiteral("Words of Radiance"));
    EXPECT_EQ(results[2].title, QStringLiteral("Oathbringer"));
    // All share the same author key — used by the aggregator to group
    // candidate series by author + title-suffix heuristic in Phase 3.
    for (const auto& r : results) {
        EXPECT_EQ(r.author, QStringLiteral("Brandon Sanderson"));
    }
}

TEST(OpenLibraryClientParserTest, MultipleAuthorsJoinWithAmpersand) {
    QByteArray bytes = R"({
        "numFound": 1, "start": 0,
        "docs": [{
            "key": "/works/OL999W",
            "title": "Good Omens",
            "author_name": ["Neil Gaiman", "Terry Pratchett"],
            "first_publish_year": 1990,
            "subject": ["Fantasy"]
        }]
    })";
    auto results = OpenLibraryClient::parseSearchResponse(bytes);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].author, QStringLiteral("Neil Gaiman & Terry Pratchett"));
}

TEST(OpenLibraryClientParserTest, MissingFieldsLeaveEmpty) {
    QByteArray bytes = R"({
        "numFound": 1, "start": 0,
        "docs": [{
            "key": "/works/OL_minimal_W",
            "title": "Minimal Book"
        }]
    })";
    auto results = OpenLibraryClient::parseSearchResponse(bytes);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].title, QStringLiteral("Minimal Book"));
    EXPECT_TRUE(results[0].author.isEmpty());
    EXPECT_TRUE(results[0].year.isEmpty());
    EXPECT_TRUE(results[0].isbn.isEmpty());
    EXPECT_TRUE(results[0].coverUrl.isEmpty());
}

TEST(OpenLibraryClientParserTest, CoverUrlUsesIdEndpoint) {
    QByteArray bytes = R"({
        "numFound": 1, "start": 0,
        "docs": [{
            "key": "/works/OL_cover_W",
            "title": "Covered Book",
            "cover_i": 12345678
        }]
    })";
    auto results = OpenLibraryClient::parseSearchResponse(bytes);
    ASSERT_EQ(results.size(), 1);
    // L-size endpoint per Open Library convention.
    EXPECT_EQ(results[0].coverUrl,
              QStringLiteral("https://covers.openlibrary.org/b/id/12345678-L.jpg"));
}

TEST(OpenLibraryClientParserTest, GarbageJsonReturnsEmpty) {
    auto results = OpenLibraryClient::parseSearchResponse(QByteArray("not json"));
    EXPECT_EQ(results.size(), 0);
}
