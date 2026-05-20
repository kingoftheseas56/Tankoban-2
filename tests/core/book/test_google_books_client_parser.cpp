#include <gtest/gtest.h>
#include <QFile>
#include "core/book/GoogleBooksClient.h"

namespace {
QByteArray loadFixture(const char* relPath) {
    const QString base = QStringLiteral(TANKOBAN_TEST_FIXTURE_DIR);
    QFile f(base + QLatin1Char('/') + QString::fromLatin1(relPath));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}
} // namespace

TEST(GoogleBooksClientParserTest, EmptyResponseReturnsZeroResults) {
    auto bytes = loadFixture("book_catalogue/googlebooks_search_empty.json");
    ASSERT_FALSE(bytes.isEmpty());
    auto results = GoogleBooksClient::parseVolumesResponse(bytes);
    EXPECT_EQ(results.size(), 0);
}

TEST(GoogleBooksClientParserTest, ParsesTwoBooks) {
    auto bytes = loadFixture("book_catalogue/googlebooks_search_stormlight.json");
    ASSERT_FALSE(bytes.isEmpty());
    auto results = GoogleBooksClient::parseVolumesResponse(bytes);
    ASSERT_EQ(results.size(), 2);

    const auto& r0 = results[0];
    EXPECT_EQ(r0.catalogueId, QStringLiteral("googlebooks:qE9SBgAAQBAJ"));
    EXPECT_EQ(r0.title, QStringLiteral("The Way of Kings"));
    EXPECT_EQ(r0.author, QStringLiteral("Brandon Sanderson"));
    EXPECT_EQ(r0.publisher, QStringLiteral("Tor Books"));
    EXPECT_EQ(r0.year, QStringLiteral("2010"));
    EXPECT_EQ(r0.pages, QStringLiteral("1007"));
    EXPECT_EQ(r0.language, QStringLiteral("en"));
    EXPECT_TRUE(r0.isbn.contains(QStringLiteral("9780765326355")));
    EXPECT_TRUE(r0.genres.contains(QStringLiteral("Fiction / Fantasy / Epic")));
    EXPECT_FALSE(r0.coverUrl.isEmpty());
    // Google Books returns http:// URLs; parser rewrites to https:// for Qt6
    // strict-protocol compatibility. This is the most behaviorally-load-bearing
    // line in parseItem — assert it explicitly so a future regression that
    // removes the rewrite fails the test instead of silently breaking covers.
    EXPECT_TRUE(r0.coverUrl.startsWith(QStringLiteral("https://")));
    EXPECT_FALSE(r0.description.isEmpty());

    EXPECT_EQ(results[1].title, QStringLiteral("Words of Radiance"));
}

TEST(GoogleBooksClientParserTest, SmallThumbnailFallbackWhenThumbnailMissing) {
    // imageLinks has smallThumbnail but no thumbnail — parser falls back +
    // applies the HTTP→HTTPS rewrite to the smallThumbnail URL.
    QByteArray bytes = R"({
        "kind":"books#volumes","totalItems":1,
        "items":[{"id":"a","volumeInfo":{"title":"T",
          "imageLinks":{"smallThumbnail":"http://books.google.com/small.jpg"}}}]
    })";
    auto results = GoogleBooksClient::parseVolumesResponse(bytes);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].coverUrl,
              QStringLiteral("https://books.google.com/small.jpg"));
}

TEST(GoogleBooksClientParserTest, EmptyIdSkipsItem) {
    // parseVolumesResponse skips items with empty id even if volumeInfo present.
    QByteArray bytes = R"({
        "kind":"books#volumes","totalItems":2,
        "items":[
            {"id":"","volumeInfo":{"title":"Skipped"}},
            {"id":"keep","volumeInfo":{"title":"Kept"}}
        ]
    })";
    auto results = GoogleBooksClient::parseVolumesResponse(bytes);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].title, QStringLiteral("Kept"));
}

TEST(GoogleBooksClientParserTest, MultipleAuthorsJoinWithAmpersand) {
    QByteArray bytes = R"({
        "kind":"books#volumes","totalItems":1,
        "items":[{"id":"x","volumeInfo":{"title":"X","authors":["A","B"]}}]
    })";
    auto results = GoogleBooksClient::parseVolumesResponse(bytes);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].author, QStringLiteral("A & B"));
}

TEST(GoogleBooksClientParserTest, MissingVolumeInfoSkipsItem) {
    QByteArray bytes = R"({
        "kind":"books#volumes","totalItems":2,
        "items":[
            {"id":"x"},
            {"id":"y","volumeInfo":{"title":"Y"}}
        ]
    })";
    auto results = GoogleBooksClient::parseVolumesResponse(bytes);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].title, QStringLiteral("Y"));
}

TEST(GoogleBooksClientParserTest, GarbageJsonReturnsEmpty) {
    auto results = GoogleBooksClient::parseVolumesResponse(QByteArray("not json"));
    EXPECT_EQ(results.size(), 0);
}

TEST(GoogleBooksClientParserTest, PublishedDateYearOnly) {
    QByteArray bytes = R"({
        "kind":"books#volumes","totalItems":1,
        "items":[{"id":"z","volumeInfo":{"title":"Z","publishedDate":"2024"}}]
    })";
    auto results = GoogleBooksClient::parseVolumesResponse(bytes);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].year, QStringLiteral("2024"));
}
