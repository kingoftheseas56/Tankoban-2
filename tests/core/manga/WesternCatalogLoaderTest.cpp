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

// Regression (2026-06-06, Hemanth-flagged on the Invincible page): RCO catalogues
// list editions newest-first (TPB 25 -> TPB 1) and the harvester only tier-sorts
// them, so the raw `editions` array is reverse-ordered. The loader must normalise
// to forward reading order so "Volume 1" is the FIRST edition, not the finale.
// Before this fix Invincible showed "Volume 1 - TPB 25 The End of All Things Part Two".
TEST(WesternCatalogLoader, RcoEditionsNormalisedToForwardReadingOrder)
{
    QJsonObject o = baseObj();
    o["seriesId"]    = "invincible";
    o["seriesTitle"] = "Invincible";
    QJsonArray eds;   // reverse order, exactly as RCO/harvester emit it
    QJsonObject e0; e0["label"] = "TPB 3 Perfect Strangers"; e0["href"] = "/Comic/Invincible/TPB-3"; e0["formatTier"] = 2;
    QJsonObject e1; e1["label"] = "TPB 2 Eight is Enough";   e1["href"] = "/Comic/Invincible/TPB-2"; e1["formatTier"] = 2;
    QJsonObject e2; e2["label"] = "TPB 1 Family Matters";    e2["href"] = "/Comic/Invincible/TPB-1"; e2["formatTier"] = 2;
    eds.append(e0); eds.append(e1); eds.append(e2);
    o["editions"] = eds;

    const auto cat = WesternCatalogLoader::loadFromJsonObject(o);
    ASSERT_EQ(cat.volumes.size(), 3);
    EXPECT_EQ(cat.volumes[0].volumeNumber, 1);
    EXPECT_EQ(cat.volumes[0].titleEnglish, "TPB 1 Family Matters");   // Volume 1 = first TPB
    EXPECT_EQ(cat.volumes[1].titleEnglish, "TPB 2 Eight is Enough");
    EXPECT_EQ(cat.volumes[2].titleEnglish, "TPB 3 Perfect Strangers");
}

// Mixed tiers (Deadly Class: TPBs + Deluxe) stay tier-grouped, each group in
// forward number order. Edition-number extraction is the first integer in the
// label, covering "TPB Part N" (Watchmen) and "Collection TPB N" (Spawn) too.
TEST(WesternCatalogLoader, RcoMixedTiersGroupedThenForwardNumbered)
{
    QJsonObject o = baseObj();
    QJsonArray eds;
    QJsonObject d1; d1["label"] = "Deluxe Edition 2"; d1["formatTier"] = 3;
    QJsonObject t2; t2["label"] = "TPB 2";            t2["formatTier"] = 2;
    QJsonObject d0; d0["label"] = "Deluxe Edition 1"; d0["formatTier"] = 3;
    QJsonObject t1; t1["label"] = "TPB 1";            t1["formatTier"] = 2;
    eds.append(d1); eds.append(t2); eds.append(d0); eds.append(t1);
    o["editions"] = eds;

    const auto cat = WesternCatalogLoader::loadFromJsonObject(o);
    ASSERT_EQ(cat.volumes.size(), 4);
    EXPECT_EQ(cat.volumes[0].titleEnglish, "TPB 1");            // tier 2 first, forward
    EXPECT_EQ(cat.volumes[1].titleEnglish, "TPB 2");
    EXPECT_EQ(cat.volumes[2].titleEnglish, "Deluxe Edition 1"); // tier 3 next, forward
    EXPECT_EQ(cat.volumes[3].titleEnglish, "Deluxe Edition 2");
}

// schema-v3 GCD branch (COMICS_WESTERN_GCD 2026-06-05): per-volume FORWARD
// editions carry their own ISBN / OL cover / year; covers are per-volume (not the
// shared rco hero) and an empty coverUrl stays empty (title-card fallback).
TEST(WesternCatalogLoader, V3GcdSchemaForwardVolumesPerVolumeCovers)
{
    QJsonObject o;
    o["seriesId"]    = "saga";
    o["seriesTitle"] = "Saga";
    o["source"]      = "gcd";
    o["schemaVersion"] = 3;
    o["seriesCover"] = "https://covers.openlibrary.org/b/isbn/9781607066019-L.jpg";
    QJsonArray eds;
    QJsonObject e0;
    e0["volumeNumber"] = 1; e0["title"] = "Volume One"; e0["isbn"] = "9781607066019";
    e0["coverUrl"] = "https://covers.openlibrary.org/b/isbn/9781607066019-L.jpg";
    e0["year"] = 2012; e0["formatTier"] = 2; e0["tierLabel"] = "TPB";
    QJsonObject e1;
    e1["volumeNumber"] = 2; e1["title"] = "Volume Two"; e1["isbn"] = "9781607066927";
    e1["coverUrl"] = ""; e1["year"] = 2013; e1["formatTier"] = 2; e1["tierLabel"] = "TPB";
    eds.append(e0); eds.append(e1);
    o["editions"] = eds;

    const auto cat = WesternCatalogLoader::loadFromJsonObject(o);
    ASSERT_EQ(cat.volumes.size(), 2);
    EXPECT_EQ(cat.volumes[0].volumeNumber, 1);                          // forward
    EXPECT_EQ(cat.volumes[0].titleEnglish, "Volume One");
    EXPECT_EQ(cat.volumes[0].isbnEn, "9781607066019");
    EXPECT_TRUE(cat.volumes[0].coverUrlJapanese.endsWith("9781607066019-L.jpg"));  // per-vol
    EXPECT_EQ(cat.volumes[0].releaseDateEn.year(), 2012);
    EXPECT_EQ(cat.volumes[0].groupingLabel, "TPB");
    EXPECT_TRUE(cat.volumes[1].coverUrlJapanese.isEmpty());             // empty -> title-card
}
