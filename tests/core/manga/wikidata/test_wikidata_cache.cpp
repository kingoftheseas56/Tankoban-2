// tests/core/manga/wikidata/test_wikidata_cache.cpp

#include <gtest/gtest.h>

#include "core/manga/fandom/FandomTypes.h"
#include "core/manga/wikidata/WikidataCache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

using namespace tankoban::manga::wikidata;
using tankoban::manga::fandom::FandomReference;

namespace {

void resetCacheFile()
{
    QFile::remove(WikidataCache::cacheFilePath());
}

FandomReference makeRef(const QString& subdomain)
{
    FandomReference r;
    r.subdomain = subdomain;
    return r;
}

} // anonymous

// ──────────────────────────────────────────────────────────────────────────
// Task 13 — WikidataCache 30d disk cache
// ──────────────────────────────────────────────────────────────────────────

TEST(WikidataCacheTest, RoundTripStoreThenLoadReturnsSubdomain)
{
    if (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).isEmpty())
        GTEST_SKIP() << "no writable AppDataLocation";
    resetCacheFile();

    ASSERT_TRUE(WikidataCache::storeByQid(QStringLiteral("Q14559"),
                                          makeRef(QStringLiteral("deathnote"))));
    auto loaded = WikidataCache::loadByQid(QStringLiteral("Q14559"));
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->subdomain.toStdString(), "deathnote");

    resetCacheFile();
}

TEST(WikidataCacheTest, SecondStoreOverwritesEntryNotWholeFile)
{
    if (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).isEmpty())
        GTEST_SKIP();
    resetCacheFile();

    ASSERT_TRUE(WikidataCache::storeByQid(QStringLiteral("Q14559"),
                                          makeRef(QStringLiteral("deathnote"))));
    ASSERT_TRUE(WikidataCache::storeByQid(QStringLiteral("Q633292"),
                                          makeRef(QStringLiteral("berserk"))));

    auto dn = WikidataCache::loadByQid(QStringLiteral("Q14559"));
    auto bk = WikidataCache::loadByQid(QStringLiteral("Q633292"));
    ASSERT_TRUE(dn.has_value());
    ASSERT_TRUE(bk.has_value());
    EXPECT_EQ(dn->subdomain.toStdString(), "deathnote");
    EXPECT_EQ(bk->subdomain.toStdString(), "berserk");

    resetCacheFile();
}

TEST(WikidataCacheTest, LoadReturnsNulloptWhenFileMissing)
{
    resetCacheFile();
    auto loaded = WikidataCache::loadByQid(QStringLiteral("Q14559"));
    EXPECT_FALSE(loaded.has_value());
}

TEST(WikidataCacheTest, LoadReturnsNulloptWhenQidNotPresent)
{
    if (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).isEmpty())
        GTEST_SKIP();
    resetCacheFile();

    ASSERT_TRUE(WikidataCache::storeByQid(QStringLiteral("Q14559"),
                                          makeRef(QStringLiteral("deathnote"))));
    auto loaded = WikidataCache::loadByQid(QStringLiteral("Q_NOT_THERE"));
    EXPECT_FALSE(loaded.has_value());

    resetCacheFile();
}

TEST(WikidataCacheTest, LoadReturnsNulloptWhenTtlExpired)
{
    if (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).isEmpty())
        GTEST_SKIP();
    resetCacheFile();

    // Write a hand-crafted entry with a backdated fetchedAt (31 days ago).
    const QString dir = QFileInfo(WikidataCache::cacheFilePath()).path();
    QDir().mkpath(dir);

    QJsonObject entry;
    entry["subdomain"] = QStringLiteral("expired-wiki");
    entry["fetchedAt"] = QDateTime::currentDateTimeUtc()
                            .addDays(-31)
                            .toString(Qt::ISODateWithMs);
    QJsonObject entries;
    entries[QStringLiteral("Q_EXPIRED")] = entry;

    QJsonObject root;
    root["version"] = 1;
    root["entries"] = entries;

    QFile f(WikidataCache::cacheFilePath());
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(root).toJson());
    f.close();

    auto loaded = WikidataCache::loadByQid(QStringLiteral("Q_EXPIRED"));
    EXPECT_FALSE(loaded.has_value())
        << "31-day-old entry should be rejected by 30d TTL";

    resetCacheFile();
}

TEST(WikidataCacheTest, MalformedJsonReturnsNullopt)
{
    if (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).isEmpty())
        GTEST_SKIP();
    resetCacheFile();

    const QString dir = QFileInfo(WikidataCache::cacheFilePath()).path();
    QDir().mkpath(dir);

    QFile f(WikidataCache::cacheFilePath());
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("{this is not json");
    f.close();

    auto loaded = WikidataCache::loadByQid(QStringLiteral("Q14559"));
    EXPECT_FALSE(loaded.has_value());

    resetCacheFile();
}
