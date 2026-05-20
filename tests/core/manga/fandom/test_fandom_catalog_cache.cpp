// tests/core/manga/fandom/test_fandom_catalog_cache.cpp

#include <gtest/gtest.h>

#include "core/manga/fandom/FandomCatalogCache.h"
#include "core/manga/fandom/FandomTypes.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

using namespace tankoban::manga::fandom;

namespace {

FandomCatalog makeDeathNoteCatalog()
{
    FandomCatalog c;
    c.seriesId         = QStringLiteral("death-note");
    c.wikidataQid      = QStringLiteral("Q14559");
    c.fandomWikiId     = QStringLiteral("deathnote");
    c.fandomVolumePath = QStringLiteral("/wiki/List_of_Death_Note_chapters");
    c.seriesSynopsis   = QStringLiteral("Light Yagami finds a notebook.");
    c.schemaVersion    = kFandomCatalogSchemaVersion;
    c.fetchedAt        = QDateTime::currentDateTimeUtc();

    FandomVolume v1;
    v1.volumeNumber     = 1;
    v1.titleEnglish     = QStringLiteral("Boredom");
    v1.titleJapanese    = QString::fromUtf8("\xe9\x80\x80\xe5\xb1\x88"); // 退屈
    v1.titleRomaji      = QStringLiteral("Taikutsu");
    v1.releaseDateJp    = QDate(2004, 4, 2);
    v1.releaseDateEn    = QDate(2005, 10, 10);
    v1.isbnJp           = QStringLiteral("978-4-088-73621-1");
    v1.isbnEn           = QStringLiteral("978-1-421-50168-0");
    v1.coverUrlJapanese = QStringLiteral("https://static.wikia.nocookie.net/deathnote/Vol1.jpg");
    v1.coverUrlEnglish  = QStringLiteral("https://static.wikia.nocookie.net/deathnote/Vol1en.jpg");
    c.volumes.append(v1);

    FandomVolume v2;
    v2.volumeNumber  = 2;
    v2.titleEnglish  = QStringLiteral("Confluence");
    v2.groupingLabel = QStringLiteral("Black Notebook Arc");
    c.volumes.append(v2);

    return c;
}

void clearCacheFile(const QString& qid)
{
    QFile::remove(FandomCatalogCache::cacheFilePath(qid));
}

} // anonymous

// ──────────────────────────────────────────────────────────────────────────
// Task 12 — FandomCatalog serde + 7d cache
// ──────────────────────────────────────────────────────────────────────────

TEST(FandomCatalogCacheTest, RoundTripStoreThenLoadAllFieldsEqual)
{
    // Smoke that requires a writable AppDataLocation — skip if not set up.
    const QString base = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        GTEST_SKIP() << "no writable AppDataLocation";

    clearCacheFile(QStringLiteral("Q14559"));

    FandomCatalog c = makeDeathNoteCatalog();
    ASSERT_TRUE(FandomCatalogCache::storeByQid(QStringLiteral("Q14559"), c));

    auto loaded = FandomCatalogCache::loadByQid(QStringLiteral("Q14559"));
    ASSERT_TRUE(loaded.has_value());

    EXPECT_EQ(loaded->seriesId.toStdString(),         "death-note");
    EXPECT_EQ(loaded->wikidataQid.toStdString(),      "Q14559");
    EXPECT_EQ(loaded->fandomWikiId.toStdString(),     "deathnote");
    EXPECT_EQ(loaded->fandomVolumePath.toStdString(), "/wiki/List_of_Death_Note_chapters");
    EXPECT_EQ(loaded->seriesSynopsis.toStdString(),   "Light Yagami finds a notebook.");
    EXPECT_EQ(loaded->schemaVersion, kFandomCatalogSchemaVersion);
    ASSERT_EQ(loaded->volumes.size(), 2);

    const FandomVolume& v1 = loaded->volumes[0];
    EXPECT_EQ(v1.volumeNumber, 1);
    EXPECT_EQ(v1.titleEnglish.toStdString(), "Boredom");
    EXPECT_EQ(v1.titleRomaji.toStdString(), "Taikutsu");
    EXPECT_EQ(v1.releaseDateJp, QDate(2004, 4, 2));
    EXPECT_EQ(v1.releaseDateEn, QDate(2005, 10, 10));
    EXPECT_EQ(v1.isbnJp.toStdString(), "978-4-088-73621-1");
    EXPECT_EQ(v1.isbnEn.toStdString(), "978-1-421-50168-0");
    EXPECT_FALSE(v1.titleJapanese.isEmpty());

    const FandomVolume& v2 = loaded->volumes[1];
    EXPECT_EQ(v2.volumeNumber, 2);
    EXPECT_EQ(v2.titleEnglish.toStdString(), "Confluence");
    EXPECT_EQ(v2.groupingLabel.toStdString(), "Black Notebook Arc");

    clearCacheFile(QStringLiteral("Q14559"));
}

TEST(FandomCatalogCacheTest, LoadReturnsNulloptWhenFileMissing)
{
    clearCacheFile(QStringLiteral("Q_DOES_NOT_EXIST"));
    auto loaded = FandomCatalogCache::loadByQid(QStringLiteral("Q_DOES_NOT_EXIST"));
    EXPECT_FALSE(loaded.has_value());
}

TEST(FandomCatalogCacheTest, LoadReturnsNulloptWhenTtlExpired)
{
    if (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).isEmpty())
        GTEST_SKIP();

    clearCacheFile(QStringLiteral("Q_TTL_TEST"));

    FandomCatalog c = makeDeathNoteCatalog();
    c.wikidataQid = QStringLiteral("Q_TTL_TEST");
    // Backdate fetchedAt to 8 days ago — past the 7d TTL.
    c.fetchedAt = QDateTime::currentDateTimeUtc().addDays(-8);
    ASSERT_TRUE(FandomCatalogCache::storeByQid(QStringLiteral("Q_TTL_TEST"), c));

    auto loaded = FandomCatalogCache::loadByQid(QStringLiteral("Q_TTL_TEST"));
    EXPECT_FALSE(loaded.has_value())
        << "8-day-old cache should be rejected by 7d TTL";

    clearCacheFile(QStringLiteral("Q_TTL_TEST"));
}

TEST(FandomCatalogCacheTest, LoadReturnsNulloptWhenSchemaVersionMismatch)
{
    if (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).isEmpty())
        GTEST_SKIP();

    // Write a JSON file with a bogus schemaVersion directly.
    clearCacheFile(QStringLiteral("Q_SCHEMA_TEST"));

    QDir().mkpath(QFileInfo(
        FandomCatalogCache::cacheFilePath(QStringLiteral("Q_SCHEMA_TEST"))).path());

    QFile f(FandomCatalogCache::cacheFilePath(QStringLiteral("Q_SCHEMA_TEST")));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    QJsonObject o;
    o["schemaVersion"] = 9999;
    o["seriesId"]      = QStringLiteral("test");
    o["wikidataQid"]   = QStringLiteral("Q_SCHEMA_TEST");
    o["fetchedAt"]     = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    f.write(QJsonDocument(o).toJson());
    f.close();

    auto loaded = FandomCatalogCache::loadByQid(QStringLiteral("Q_SCHEMA_TEST"));
    EXPECT_FALSE(loaded.has_value())
        << "schemaVersion=9999 should be rejected (current is "
        << kFandomCatalogSchemaVersion << ")";

    clearCacheFile(QStringLiteral("Q_SCHEMA_TEST"));
}

TEST(FandomCatalogCacheTest, StoreStampsFetchedAtAndSchemaVersionWhenMissing)
{
    if (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).isEmpty())
        GTEST_SKIP();

    clearCacheFile(QStringLiteral("Q_STAMP_TEST"));

    FandomCatalog c;
    c.seriesId      = QStringLiteral("stamp-test");
    c.wikidataQid   = QStringLiteral("Q_STAMP_TEST");
    c.schemaVersion = 0;   // unset — store should stamp current
    // fetchedAt left invalid — store should stamp now

    FandomVolume v;
    v.volumeNumber = 1;
    c.volumes.append(v);

    ASSERT_TRUE(FandomCatalogCache::storeByQid(QStringLiteral("Q_STAMP_TEST"), c));

    auto loaded = FandomCatalogCache::loadByQid(QStringLiteral("Q_STAMP_TEST"));
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->schemaVersion, kFandomCatalogSchemaVersion);
    EXPECT_TRUE(loaded->fetchedAt.isValid());
    // Stamped within the last minute.
    EXPECT_LE(loaded->fetchedAt.secsTo(QDateTime::currentDateTimeUtc()), 60);

    clearCacheFile(QStringLiteral("Q_STAMP_TEST"));
}

TEST(FandomCatalogCacheTest, JsonHelperRoundTripPreservesEverySerializableField)
{
    FandomCatalog c = makeDeathNoteCatalog();
    QJsonObject obj = FandomCatalogCache::toJson(c);
    FandomCatalog back = FandomCatalogCache::fromJson(obj);

    ASSERT_EQ(back.volumes.size(), c.volumes.size());
    for (int i = 0; i < c.volumes.size(); ++i) {
        EXPECT_EQ(back.volumes[i].volumeNumber, c.volumes[i].volumeNumber);
        EXPECT_EQ(back.volumes[i].titleEnglish, c.volumes[i].titleEnglish);
        EXPECT_EQ(back.volumes[i].titleJapanese, c.volumes[i].titleJapanese);
        EXPECT_EQ(back.volumes[i].groupingLabel, c.volumes[i].groupingLabel);
    }
}
