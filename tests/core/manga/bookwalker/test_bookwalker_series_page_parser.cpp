#include "core/manga/bookwalker/BookWalkerSeriesPageParser.h"

#include <gtest/gtest.h>
#include <QFile>
#include <QString>

using tankoban::manga::bookwalker::BookWalkerSeriesPageParser;
using tankoban::manga::bookwalker::BookWalkerSearchHit;

namespace {

QString loadFixture(const QString& relPath) {
    QFile f(QStringLiteral(TANKOBAN_TEST_FIXTURE_DIR) + QStringLiteral("/") + relPath);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}

} // namespace

TEST(BookWalkerSeriesPageParser, ExtractsCoverUrlsFromBerserkFixture) {
    QString html = loadFixture(QStringLiteral("bookwalker/berserk_series_page.html"));
    ASSERT_FALSE(html.isEmpty()) << "Fixture missing";

    auto urls = BookWalkerSeriesPageParser::extractCoverUrls(html);

    EXPECT_GE(urls.size(), 30) << "Live probe 2026-05-18 found 30+ covers; regression if fewer";
    for (const QString& url : urls) {
        EXPECT_TRUE(url.startsWith(QStringLiteral("https://rimg.bookwalker.jp/"))) << url.toStdString();
        EXPECT_TRUE(url.endsWith(QStringLiteral(".jpg")) || url.endsWith(QStringLiteral(".png")) || url.endsWith(QStringLiteral(".webp")));
    }
}

TEST(BookWalkerSeriesPageParser, DeduplicatesCoverUrls) {
    QString html = QStringLiteral(
        R"(<img data-original="https://rimg.bookwalker.jp/AAA.jpg">)"
        R"(<img data-original="https://rimg.bookwalker.jp/BBB.jpg">)"
        R"(<img data-original="https://rimg.bookwalker.jp/AAA.jpg">)"
    );
    auto urls = BookWalkerSeriesPageParser::extractCoverUrls(html);
    ASSERT_EQ(urls.size(), 2);
    EXPECT_EQ(urls[0], QStringLiteral("https://rimg.bookwalker.jp/AAA.jpg"));
    EXPECT_EQ(urls[1], QStringLiteral("https://rimg.bookwalker.jp/BBB.jpg"));
}

TEST(BookWalkerSeriesPageParser, ReturnsEmptyOnNoMatch) {
    QString html = QStringLiteral("<html><body><img src='unrelated.jpg'/></body></html>");
    auto urls = BookWalkerSeriesPageParser::extractCoverUrls(html);
    EXPECT_TRUE(urls.isEmpty());
}

TEST(BookWalkerSeriesPageParser, ExtractsSearchHitsFromFixture) {
    QString html = loadFixture(QStringLiteral("bookwalker/berserk_search_results.html"));
    ASSERT_FALSE(html.isEmpty());

    auto hits = BookWalkerSeriesPageParser::extractSearchHits(html);
    ASSERT_FALSE(hits.isEmpty());

    // Live probe 2026-05-18 found id 16664 with title containing the Berserk kanji.
    bool found = false;
    for (const auto& h : hits) {
        if (h.seriesId == QStringLiteral("16664")) {
            found = true;
            EXPECT_TRUE(h.title.contains(QString::fromUtf8("\xe3\x83\x99\xe3\x83\xab\xe3\x82\xbb\xe3\x83\xab\xe3\x82\xaf"))) // UTF-8 for ベルセルク
                << "Title was: " << h.title.toStdString();
            break;
        }
    }
    EXPECT_TRUE(found) << "Berserk-proper (id 16664) missing from search hits";
}

TEST(BookWalkerSeriesPageParser, PicksSeriesIdByExactTitleAfterStrippingParens) {
    QList<BookWalkerSearchHit> hits = {
        {QStringLiteral("16664"),  QString::fromUtf8("\xe3\x83\x99\xe3\x83\xab\xe3\x82\xbb\xe3\x83\xab\xe3\x82\xaf\xef\xbc\x88\xe3\x83\xa4\xe3\x83\xb3\xe3\x82\xb0\xe3\x82\xa2\xe3\x83\x8b\xe3\x83\xa1\xe3\x83\xab\xef\xbc\x89")}, // ベルセルク（ヤングアニマル）
        {QStringLiteral("175790"), QString::fromUtf8("\xe6\x9a\xb4\xe9\xa3\x9f\xe3\x81\xae\xe3\x83\x99\xe3\x83\xab\xe3\x82\xbb\xe3\x83\xab\xe3\x82\xaf")},                                                                           // 暴食のベルセルク
        {QStringLiteral("139162"), QString::fromUtf8("\xe3\x83\x99\xe3\x83\xab\xe3\x82\xbb\xe3\x83\xab\xe3\x82\xaf \xe3\x82\xa2\xe3\x83\x8a\xe3\x83\xaa\xe3\x82\xb9\xe3\x83\x88\xe3\x83\x96\xe3\x83\x83\xe3\x82\xaf")},           // ベルセルク アナリストブック
    };
    QString id = BookWalkerSeriesPageParser::pickSeriesIdByTitle(
        hits, QString::fromUtf8("\xe3\x83\x99\xe3\x83\xab\xe3\x82\xbb\xe3\x83\xab\xe3\x82\xaf")); // ベルセルク
    EXPECT_EQ(id, QStringLiteral("16664"));
}

TEST(BookWalkerSeriesPageParser, ReturnsFirstHitWhenNoExactMatch) {
    // Pass 3 fallback: when neither exact nor starts-with matches, take the
    // first hit (BookWalker relevance is usually right). Old behavior returned
    // empty; new behavior is best-effort.
    QList<BookWalkerSearchHit> hits = {
        {QStringLiteral("175790"), QString::fromUtf8("\xe6\x9a\xb4\xe9\xa3\x9f\xe3\x81\xae\xe3\x83\x99\xe3\x83\xab\xe3\x82\xbb\xe3\x83\xab\xe3\x82\xaf")}, // 暴食のベルセルク
    };
    QString id = BookWalkerSeriesPageParser::pickSeriesIdByTitle(
        hits, QString::fromUtf8("\xe3\x83\x99\xe3\x83\xab\xe3\x82\xbb\xe3\x83\xab\xe3\x82\xaf")); // ベルセルク
    EXPECT_EQ(id, QStringLiteral("175790"));
}

TEST(BookWalkerSeriesPageParser, PicksByStartsWithForLatinTitleWithSuffix) {
    // Death Note case: AniList native title is "DEATH NOTE" (latin); BookWalker
    // ships editions like "DEATH NOTE モノクロ版" with non-paren edition suffix.
    QList<BookWalkerSearchHit> hits = {
        {QStringLiteral("1234"), QStringLiteral("DEATH NOTE \xe3\x83\xa2\xe3\x83\x8e\xe3\x82\xaf\xe3\x83\xad\xe7\x89\x88")}, // DEATH NOTE モノクロ版
        {QStringLiteral("5678"), QStringLiteral("DEATH NOTE Another Note")},
    };
    QString id = BookWalkerSeriesPageParser::pickSeriesIdByTitle(
        hits, QStringLiteral("Death Note"));
    EXPECT_EQ(id, QStringLiteral("1234"));  // first starts-with case-insensitive
}

TEST(BookWalkerSeriesPageParser, ReturnsEmptyIdOnEmptyHits) {
    QList<BookWalkerSearchHit> hits;
    QString id = BookWalkerSeriesPageParser::pickSeriesIdByTitle(
        hits, QString::fromUtf8("\xe3\x83\x99\xe3\x83\xab\xe3\x82\xbb\xe3\x83\xab\xe3\x82\xaf")); // ベルセルク
    EXPECT_TRUE(id.isEmpty());
}
