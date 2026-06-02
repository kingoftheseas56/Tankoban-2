// tests/core/manga/GetComicsParseTest.cpp
#include <gtest/gtest.h>
#include "core/manga/GetComicsParse.h"

using namespace tankoban::manga::getcomics;

TEST(GetComicsParse, ExtractDownloadsFiltersAndKinds) {
    const QString html = R"HTML(
        <a href="magnet:?xt=urn:btih:ABC">MAGNET Link</a>
        <a href="https://getcomics.org/dls/tok1">Main Server</a>
        <a href="https://getcomics.org/dls/tok2">Pixeldrain</a>
        <a href="https://craveu.example/ad">Hot Singles</a>
        <a href="https://getcomics.org/dls/tok3">MEGA</a>
    )HTML";
    const auto dls = extractDownloads(html);
    ASSERT_EQ(dls.size(), 4);                 // ad link dropped
    EXPECT_EQ(dls[0].kind, "magnet");
    EXPECT_EQ(dls[1].kind, "main_server");
    EXPECT_EQ(dls[2].kind, "pixeldrain");
    EXPECT_EQ(dls[3].kind, "mega");
}

TEST(GetComicsParse, PickBestPriority) {
    QList<DownloadLink> links = {
        {"mega", "u-mega"}, {"pixeldrain", "u-pd"}, {"magnet", "u-mag"}, {"main_server", "u-ms"}};
    EXPECT_EQ(pickBest(links).kind, "magnet");
    QList<DownloadLink> noMagnet = {{"mega", "u-mega"}, {"pixeldrain", "u-pd"}, {"main_server", "u-ms"}};
    EXPECT_EQ(pickBest(noMagnet).kind, "main_server");
    EXPECT_TRUE(pickBest({}).url.isEmpty());
}

TEST(GetComicsParse, ParsePostCover) {
    const QString html = R"HTML(<meta property="og:image" content="https://getcomics.org/x/Invincible-Compendium-1.jpg" />)HTML";
    EXPECT_EQ(parsePostCover(html), "https://getcomics.org/x/Invincible-Compendium-1.jpg");
    EXPECT_EQ(parsePostCover("<html>no og</html>"), "");
}

TEST(GetComicsParse, ScoreMatchPrefersRightTitleYearTier) {
    const QString wanted = "Invincible Compendium Vol. 1";
    const int year = 2011;
    const QString tier = "Compendium";
    const int good = scoreMatch(wanted, year, tier, "Invincible Compendium Vol 1 (2011)");
    const int wrongSeries = scoreMatch(wanted, year, tier, "Saga Compendium One (2019)");
    const int wrongTier = scoreMatch(wanted, year, tier, "Invincible TPB Vol 1 (2003)");
    EXPECT_GT(good, wrongSeries);
    EXPECT_GT(good, wrongTier);
    EXPECT_EQ(scoreMatch(wanted, year, tier, "Batman Year One"), 0);  // no overlap
}

TEST(GetComicsParse, PickBestMatchFailSafe) {
    QList<SearchResult> results = {
        {"Invincible Compendium Vol 1 (2011)", "https://getcomics.org/p/inv-comp-1"},
        {"Saga Compendium One", "https://getcomics.org/p/saga"}};
    EXPECT_EQ(pickBestMatch("Invincible Compendium Vol. 1", 2011, "Compendium", results).postUrl,
              "https://getcomics.org/p/inv-comp-1");
    // No confident match -> empty (fail safe).
    QList<SearchResult> junk = {{"Batman Year One", "u1"}, {"Spawn Origins", "u2"}};
    EXPECT_TRUE(pickBestMatch("Invincible Compendium Vol. 1", 2011, "Compendium", junk).postUrl.isEmpty());
}
