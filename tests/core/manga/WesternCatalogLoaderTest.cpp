#include <gtest/gtest.h>

#include "core/manga/WesternCatalogLoader.h"

#include <QJsonArray>
#include <QJsonObject>

using tankoban::manga::WesternCatalogLoader;

namespace {

QJsonObject baseObj()
{
    QJsonObject o;
    o["seriesId"]      = "saga";
    o["seriesTitle"]   = "Saga";
    o["source"]        = "rco";
    o["synopsis"]      = "Two soldiers from opposite sides of a galactic war.";
    o["schemaVersion"] = 2;
    return o;
}

} // namespace

// Regression (2026-06-02): a live-fetched series whose RCO page has only single
// issues (no collected editions) must still produce a VALID catalog so the UI
// shows the series page (synopsis/cover) with an empty-editions state — NOT an
// empty catalog that the pick handler silently swallows, hanging on "Loading".
// (spec 2026-06-01-comics-western-search-design.md §8.)
TEST(WesternCatalogLoader, EmptyEditionsStillValid)
{
    QJsonObject o = baseObj();
    o["editions"] = QJsonArray();   // no collected editions

    const auto cat = WesternCatalogLoader::loadFromJsonObject(o);

    EXPECT_EQ(cat.seriesId, "saga");                 // valid, not the {} sentinel
    EXPECT_EQ(cat.seriesTitle, "Saga");
    EXPECT_FALSE(cat.seriesSynopsis.isEmpty());
    EXPECT_TRUE(cat.volumes.isEmpty());              // empty editions -> no volumes
}

// A genuinely invalid record (no seriesId) is still rejected to the {} sentinel.
TEST(WesternCatalogLoader, MissingSeriesIdRejected)
{
    QJsonObject o = baseObj();
    o.remove("seriesId");
    o["editions"] = QJsonArray();

    const auto cat = WesternCatalogLoader::loadFromJsonObject(o);
    EXPECT_TRUE(cat.seriesId.isEmpty());             // {} sentinel
}

// With editions present, they map to volumes in record order.
TEST(WesternCatalogLoader, EditionsMapToVolumes)
{
    QJsonObject o = baseObj();
    QJsonArray eds;
    QJsonObject e0; e0["label"] = "Compendium One"; e0["href"] = "/Comic/Saga/Compendium-One"; e0["formatTier"] = 0;
    QJsonObject e1; e1["label"] = "TPB 2";          e1["href"] = "/Comic/Saga/TPB-2";          e1["formatTier"] = 2;
    eds.append(e0); eds.append(e1);
    o["editions"] = eds;

    const auto cat = WesternCatalogLoader::loadFromJsonObject(o);
    ASSERT_EQ(cat.volumes.size(), 2);
    EXPECT_EQ(cat.volumes[0].titleEnglish, "Compendium One");
    EXPECT_EQ(cat.volumes[0].groupingLabel, "Compendium");
    EXPECT_EQ(cat.volumes[1].groupingLabel, "TPB");
}
