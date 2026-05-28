#include <gtest/gtest.h>
#include <QFile>
#include <QString>

#include "core/book/FictionDbClient.h"

namespace {
QString loadFixture(const char* relPath) {
    const QString base = QStringLiteral(TANKOBAN_TEST_FIXTURE_DIR);
    QFile f(base + QLatin1Char('/') + QString::fromLatin1(relPath));
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}
}  // namespace

// ── Book page ────────────────────────────────────────────────────────────────

TEST(FictionDbClientParser, ParsesDuneBookPage) {
    const QString html = loadFixture("book_catalogue/fictiondb_dune_book.html");
    ASSERT_FALSE(html.isEmpty());

    const BookCatalogueResult r =
        FictionDbClient::parseBookPage(html, QStringLiteral("dune~frank-herbert~99723"));

    EXPECT_EQ(r.catalogueId.toStdString(), "fictiondb:dune~frank-herbert~99723");
    EXPECT_EQ(r.title.toStdString(), "Dune");
    EXPECT_EQ(r.author.toStdString(), "Frank Herbert");
    EXPECT_EQ(r.isbn.toStdString(), "9780441172719");
    EXPECT_FALSE(r.coverUrl.isEmpty());
    EXPECT_TRUE(r.coverUrl.contains("covers/"));
    EXPECT_FALSE(r.description.isEmpty());
    EXPECT_EQ(r.year.toStdString(), "1965");
    EXPECT_FALSE(r.isSeries);
    // Book page self-declares its series: "Dune Chronicles - 1".
    EXPECT_EQ(r.seriesName.toStdString(), "Dune Chronicles");
    EXPECT_EQ(r.seriesPosition, 1);
    EXPECT_FALSE(r.seriesId.isEmpty());
}

// ── Series page ──────────────────────────────────────────────────────────────

TEST(FictionDbClientParser, ParsesDuneSeriesPageInOrder) {
    const QString html = loadFixture("book_catalogue/fictiondb_dune_series.html");
    ASSERT_FALSE(html.isEmpty());

    const auto books = FictionDbClient::parseSeriesPage(
        html, QStringLiteral("dune-chronicles-frank-herbert~3735"));

    ASSERT_GE(books.size(), 6);
    EXPECT_EQ(books[0].title.toStdString(), "Dune");
    EXPECT_EQ(books[0].seriesPosition, 1);
    EXPECT_EQ(books[1].seriesPosition, 2);
    EXPECT_TRUE(books[1].title.contains("Messiah", Qt::CaseInsensitive));
    for (const auto& b : books) {
        EXPECT_FALSE(b.title.isEmpty());
        EXPECT_EQ(b.seriesId.toStdString(), "dune-chronicles-frank-herbert~3735");
        EXPECT_TRUE(b.catalogueId.startsWith("fictiondb:"));
        EXPECT_GE(b.seriesTotal, 6);
    }
}

// ── Search page ──────────────────────────────────────────────────────────────

TEST(FictionDbClientParser, ParsesSearchResultsAsBooks) {
    const QString html = loadFixture("book_catalogue/fictiondb_search_dune.html");
    ASSERT_FALSE(html.isEmpty());

    const auto books = FictionDbClient::parseSearchPage(html);

    // The "dune" search returns a large flat book table.
    EXPECT_GE(books.size(), 50);
    for (const auto& b : books) {
        EXPECT_TRUE(b.catalogueId.startsWith("fictiondb:"));
        EXPECT_FALSE(b.title.isEmpty());
        EXPECT_FALSE(b.isSeries);
    }
    // At least one result is a recognizable Dune title.
    bool foundDune = false;
    for (const auto& b : books)
        if (b.title.contains("Dune", Qt::CaseInsensitive)) { foundDune = true; break; }
    EXPECT_TRUE(foundDune);
}

// ── slugFromHref helper ──────────────────────────────────────────────────────

TEST(FictionDbClientParser, SlugFromHref) {
    EXPECT_EQ(FictionDbClient::slugFromHref(
                  QStringLiteral("../title/dune~frank-herbert~99723.htm"),
                  QStringLiteral("title")).toStdString(),
              "dune~frank-herbert~99723");
    EXPECT_EQ(FictionDbClient::slugFromHref(
                  QStringLiteral("../series/dune-chronicles-frank-herbert~3735.htm"),
                  QStringLiteral("series")).toStdString(),
              "dune-chronicles-frank-herbert~3735");
    // A non-matching href yields empty.
    EXPECT_TRUE(FictionDbClient::slugFromHref(
                    QStringLiteral("../series/series-lists.htm"),
                    QStringLiteral("series")).isEmpty());
}

// ── A–Z series-index page ─────────────────────────────────────────────────────

TEST(FictionDbClientParser, ParsesAuthorSeriesIndexPage) {
    const QString html = loadFixture("book_catalogue/fictiondb_author_series_a.html");
    ASSERT_FALSE(html.isEmpty());

    bool hasNext = false;
    const auto entries = FictionDbClient::parseSeriesIndexPage(html, &hasNext);

    // Letter "A" page 1 is dense (hundreds of series).
    ASSERT_GT(entries.size(), 50);
    EXPECT_TRUE(hasNext);  // page 1 of many → a "»" next link is present

    for (const auto& e : entries) {
        EXPECT_FALSE(e.seriesId.isEmpty());
        EXPECT_TRUE(e.seriesId.contains(QChar('~')));            // slug carries ~id
        EXPECT_FALSE(e.seriesId.startsWith("author-series"));    // nav links excluded
        EXPECT_FALSE(e.seriesName.isEmpty());
    }
}
