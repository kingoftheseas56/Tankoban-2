// tests/core/manga/WesternSeriesParseTest.cpp
#include <gtest/gtest.h>
#include <QJsonObject>
#include "core/manga/WesternSeriesParse.h"

using namespace tankoban::manga::western;

TEST(WesternSeriesParse, EditionTierByKeyword) {
    EXPECT_EQ(editionTier("_The Lost Year Compendium"), 0);
    EXPECT_EQ(editionTier("Omnibus Vol 1"), 1);        // omnibus rule before vol
    EXPECT_EQ(editionTier("_TPB 25 The End"), 2);
    EXPECT_EQ(editionTier("Trade Paperback"), 2);
    EXPECT_EQ(editionTier("Complete Collection"), 2);
    EXPECT_EQ(editionTier("Absolute Vol 1"), 3);       // absolute before vol
    EXPECT_EQ(editionTier("Library Edition"), 3);
    EXPECT_EQ(editionTier("Vol. 3"), 4);
    EXPECT_EQ(editionTier("Issue 144"), 99);
    EXPECT_EQ(editionTier("Invincible (2003) #32"), 99);
}

TEST(WesternSeriesParse, IsCollected) {
    EXPECT_TRUE(isCollected("_TPB 25 The End"));
    EXPECT_TRUE(isCollected("Compendium One"));
    EXPECT_TRUE(isCollected("Vol. 3"));               // soft Vol, no issue marker
    EXPECT_FALSE(isCollected("Vol 2 Issue #5"));      // Vol + issue marker -> single issue
    EXPECT_FALSE(isCollected("Issue #144"));
    EXPECT_FALSE(isCollected("Invincible (2003) #32"));
}

TEST(WesternSeriesParse, SlugToLabel) {
    EXPECT_EQ(slugToLabel("/Comic/Invincible/TPB-25-The-End"), "TPB 25 The End");
    EXPECT_EQ(slugToLabel("/Comic/Saga/Compendium-One/"), "Compendium One");
}

TEST(WesternSeriesParse, ParseSeriesItemsDedupes) {
    const QString html = R"(
        <a href="/Comic/Invincible/TPB-25-The-End?id=1"><img/></a>
        <a href="/Comic/Invincible/TPB-25-The-End?id=1">TPB 25</a>
        <a href="/Comic/Invincible/Issue-144?id=2">Issue 144</a>
    )";
    const auto items = parseSeriesItems(html);
    ASSERT_EQ(items.size(), 2);
    EXPECT_EQ(items[0].href, "/Comic/Invincible/TPB-25-The-End");
    EXPECT_EQ(items[0].label, "TPB 25 The End");
    EXPECT_EQ(items[1].href, "/Comic/Invincible/Issue-144");
}

TEST(WesternSeriesParse, ParseSeriesCover) {
    const QString html = R"(<link rel="image_src" href="/Uploads/Etc/3-25-2016/42392826.jpg">)";
    EXPECT_EQ(parseSeriesCover(html), "/Uploads/Etc/3-25-2016/42392826.jpg");
    EXPECT_EQ(parseSeriesCover("<html>no cover</html>"), "");
}

TEST(WesternSeriesParse, ParseSeriesSummary) {
    const QString html =
        R"(<span class="info">Summary:</span> <p>A man named <b>Mark</b> &amp; his dad.</p>)";
    EXPECT_EQ(parseSeriesSummary(html), "A man named Mark & his dad.");
    EXPECT_EQ(parseSeriesSummary("<html>no summary</html>"), "");
}

TEST(WesternSeriesParse, NeedsSummaryFallback) {
    EXPECT_TRUE(needsSummaryFallback("too short"));
    EXPECT_FALSE(needsSummaryFallback(QString(200, 'x')));
}

TEST(WesternSeriesParse, BuildEditionsFiltersAndSorts) {
    QList<SeriesItem> items = {
        {"Vol. 1",            "/Comic/X/Vol-1"},
        {"Issue 5",           "/Comic/X/Issue-5"},     // dropped (single issue)
        {"_TPB 25 The End",   "/Comic/X/TPB-25"},
        {"Compendium One",    "/Comic/X/Compendium-One"},
    };
    const QJsonArray eds = buildEditions(items);
    ASSERT_EQ(eds.size(), 3);                          // Issue 5 filtered out
    EXPECT_EQ(eds[0].toObject()["formatTier"].toInt(), 0);  // Compendium first
    EXPECT_EQ(eds[1].toObject()["formatTier"].toInt(), 2);  // TPB
    EXPECT_EQ(eds[2].toObject()["formatTier"].toInt(), 4);  // Vol last
    EXPECT_EQ(eds[0].toObject()["label"].toString(), "Compendium One");
}
