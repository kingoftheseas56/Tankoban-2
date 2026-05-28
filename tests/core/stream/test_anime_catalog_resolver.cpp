#include <gtest/gtest.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "core/stream/AnimeCatalogResolver.h"
#include "core/stream/AnimeIdMapCache.h"

using tankostream::stream::AnimeIdMap;
using tankostream::stream::AnimeIdMapCache;
using tankostream::stream::confirmsKitsuMatch;
using tankostream::stream::isAnimeSeries;

// --- Task 1.1: anime detection predicate ---
TEST(AnimeDetect, AnimationPlusJapanIsAnime) {
    EXPECT_TRUE(isAnimeSeries({"Animation", "Action", "Adventure"}, "Japan"));
}
TEST(AnimeDetect, AnimationNonJapanIsNot) {
    EXPECT_FALSE(isAnimeSeries({"Animation", "Comedy"}, "United States"));
}
TEST(AnimeDetect, JapanNonAnimationIsNot) {
    EXPECT_FALSE(isAnimeSeries({"Drama"}, "Japan"));
}
TEST(AnimeDetect, CaseInsensitive) {
    EXPECT_TRUE(isAnimeSeries({"animation"}, "japan"));
}
TEST(AnimeDetect, EmptyIsNot) {
    EXPECT_FALSE(isAnimeSeries({}, ""));
}

// --- Task 1.2: AnimeIdMap parse + lookup ---
TEST(AnimeIdMap, ParsesImdbToKitsu) {
    AnimeIdMap m;
    m.loadFromJson(QByteArrayLiteral(
        R"([{"imdb_id":"tt0388629","kitsu_id":12,"mal_id":21},)"
        R"({"imdb_id":"tt0409591","kitsu_id":11}])"));
    EXPECT_EQ(m.kitsuIdForImdb("tt0388629").value_or(-1), 12);
    EXPECT_EQ(m.kitsuIdForImdb("tt0409591").value_or(-1), 11);
    EXPECT_FALSE(m.kitsuIdForImdb("tt9999999").has_value());
}
TEST(AnimeIdMap, SkipsEntriesWithoutImdbOrKitsu) {
    AnimeIdMap m;
    m.loadFromJson(QByteArrayLiteral(
        R"([{"kitsu_id":5},{"imdb_id":"","kitsu_id":6},{"imdb_id":"tt7"}])"));
    EXPECT_EQ(m.size(), 0);
}

// --- Task 1.3: Kitsu-match confirm predicate ---
TEST(KitsuConfirm, ImdbRoundTripEqualMatches) {
    EXPECT_TRUE(confirmsKitsuMatch("tt0388629", "tt0388629"));
}
TEST(KitsuConfirm, MismatchOrEmptyRejected) {
    EXPECT_FALSE(confirmsKitsuMatch("tt0388629", "tt1234567"));
    EXPECT_FALSE(confirmsKitsuMatch("tt0388629", ""));
    EXPECT_FALSE(confirmsKitsuMatch("", "tt0388629"));
}

// --- Task 1.4: AnimeIdMapCache (file-backed, network-free) ---
TEST(AnimeIdMapCache, LoadsCachedFileAndLooksUp) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QFile f(QDir(dir.path()).filePath(QStringLiteral("anime-id-map.json")));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(QByteArrayLiteral(R"([{"imdb_id":"tt0388629","kitsu_id":12}])"));
    f.close();
    AnimeIdMapCache cache(dir.path());
    EXPECT_EQ(cache.kitsuIdForImdb("tt0388629").value_or(-1), 12);
}
TEST(AnimeIdMapCache, SaveJsonPersistsAndReloads) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    AnimeIdMapCache cache(dir.path());
    EXPECT_FALSE(cache.kitsuIdForImdb("tt0388629").has_value());
    cache.saveJson(QByteArrayLiteral(R"([{"imdb_id":"tt0388629","kitsu_id":12}])"));
    EXPECT_EQ(cache.kitsuIdForImdb("tt0388629").value_or(-1), 12);
    // A fresh instance reads the persisted file from disk.
    AnimeIdMapCache reopened(dir.path());
    EXPECT_EQ(reopened.kitsuIdForImdb("tt0388629").value_or(-1), 12);
}
TEST(AnimeIdMapCache, MissingFileIsStale) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    AnimeIdMapCache cache(dir.path());
    EXPECT_TRUE(cache.isStale(60000));
}
