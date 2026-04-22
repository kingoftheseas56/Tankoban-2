// ── test_audiobook_meta_cache.cpp ──────────────────────────────────────────
// Unit tests for AudiobookMetaCache (AUDIOBOOK_PAIRED_READING_FIX Phase 1.1).
//
// Pure-function paths (path resolution, cache file layout, stale-check) are
// covered by unconditional tests that seed `.audiobook_meta.json` directly
// without invoking ffprobe — fast, deterministic, CI-portable.
//
// A gated real-probe test runs only when the env vars
// TANKOBAN_TEST_AUDIOBOOK_FIXTURE + TANKOBAN_TEST_FFPROBE point at existing
// files. Useful for local Phase 1.1 verification, skipped automatically in CI.

#include "core/AudiobookMetaCache.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>

namespace {

QString writeJson(const QString& filePath, const QJsonObject& obj)
{
    QSaveFile f(filePath);
    EXPECT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    EXPECT_TRUE(f.commit());
    return filePath;
}

QJsonObject seedCacheObject(const QString& key,
                            qint64 durationMs,
                            qint64 mtimeMs)
{
    QJsonObject entry;
    entry["durationMs"] = static_cast<double>(durationMs);
    entry["mtimeMs"]    = static_cast<double>(mtimeMs);

    QJsonObject chapters;
    chapters[key] = entry;

    QJsonObject root;
    root["schemaVersion"] = 1;
    root["chapters"]      = chapters;
    root["updatedAt"]     = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    return root;
}

QString writeDummyAudioFile(const QDir& dir, const QString& fileName)
{
    const QString p = dir.absoluteFilePath(fileName);
    QFile f(p);
    EXPECT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("M", 1);
    f.close();
    return p;
}

}  // namespace

// ── 1. cache hit: seeded cache with fresh mtime returns stored value ───────
TEST(AudiobookMetaCacheTest, CacheHitReturnsStoredDuration)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QDir folder(tmp.path());

    const QString audio = writeDummyAudioFile(folder, "01 - Chapter.mp3");
    const qint64 audioMtime = QFileInfo(audio).lastModified().toMSecsSinceEpoch();

    const qint64 seededDuration = 4328813;
    writeJson(folder.absoluteFilePath(".audiobook_meta.json"),
              seedCacheObject("01 - Chapter.mp3", seededDuration, audioMtime));

    const QString prevOverride = AudiobookMetaCache::setFfprobePathOverrideForTest(
        folder.absoluteFilePath("no-such-ffprobe.exe"));

    const qint64 ms = AudiobookMetaCache::durationMsFor(folder.path(), audio);
    AudiobookMetaCache::setFfprobePathOverrideForTest(prevOverride);

    EXPECT_EQ(seededDuration, ms);
}

// ── 2. cache file schema: multi-entry roundtrip ───────────────────────────
TEST(AudiobookMetaCacheTest, CacheFileRoundTripsMultipleEntries)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QDir folder(tmp.path());

    const QString a = writeDummyAudioFile(folder, "ch01.mp3");
    const QString b = writeDummyAudioFile(folder, "ch02.mp3");
    const qint64 aMtime = QFileInfo(a).lastModified().toMSecsSinceEpoch();
    const qint64 bMtime = QFileInfo(b).lastModified().toMSecsSinceEpoch();

    QJsonObject cacheRoot;
    QJsonObject chapters;
    {
        QJsonObject ea; ea["durationMs"] = 123456.0; ea["mtimeMs"] = double(aMtime);
        QJsonObject eb; eb["durationMs"] = 789012.0; eb["mtimeMs"] = double(bMtime);
        chapters["ch01.mp3"] = ea;
        chapters["ch02.mp3"] = eb;
    }
    cacheRoot["schemaVersion"] = 1;
    cacheRoot["chapters"]      = chapters;
    cacheRoot["updatedAt"]     = 0.0;
    writeJson(folder.absoluteFilePath(".audiobook_meta.json"), cacheRoot);

    const QString prevOverride = AudiobookMetaCache::setFfprobePathOverrideForTest(
        folder.absoluteFilePath("no-such-ffprobe.exe"));

    EXPECT_EQ(123456, AudiobookMetaCache::durationMsFor(folder.path(), a));
    EXPECT_EQ(789012, AudiobookMetaCache::durationMsFor(folder.path(), b));

    AudiobookMetaCache::setFfprobePathOverrideForTest(prevOverride);
}

// ── 3. stale cache: older mtime triggers re-probe ─────────────────────────
TEST(AudiobookMetaCacheTest, StaleCacheTriggersReprobe)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QDir folder(tmp.path());

    const QString audio = writeDummyAudioFile(folder, "ch01.mp3");
    const qint64 audioMtime = QFileInfo(audio).lastModified().toMSecsSinceEpoch();

    writeJson(folder.absoluteFilePath(".audiobook_meta.json"),
              seedCacheObject("ch01.mp3", 99999, audioMtime - 3600000));

    const QString prevOverride = AudiobookMetaCache::setFfprobePathOverrideForTest(
        folder.absoluteFilePath("no-such-ffprobe.exe"));

    const qint64 ms = AudiobookMetaCache::durationMsFor(folder.path(), audio);
    AudiobookMetaCache::setFfprobePathOverrideForTest(prevOverride);

    EXPECT_EQ(-1, ms);
}

// ── 4. missing ffprobe returns -1 ─────────────────────────────────────────
TEST(AudiobookMetaCacheTest, MissingFfprobeReturnsNegativeOne)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QDir folder(tmp.path());

    const QString audio = writeDummyAudioFile(folder, "ch.mp3");

    const QString prevOverride = AudiobookMetaCache::setFfprobePathOverrideForTest(
        folder.absoluteFilePath("definitely-not-a-real-ffprobe.exe"));

    const qint64 ms = AudiobookMetaCache::durationMsFor(folder.path(), audio);
    AudiobookMetaCache::setFfprobePathOverrideForTest(prevOverride);

    EXPECT_EQ(-1, ms);
}

// ── 5. missing audio file returns -1 ──────────────────────────────────────
TEST(AudiobookMetaCacheTest, MissingAudioFileReturnsNegativeOne)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QDir folder(tmp.path());

    const QString audio = folder.absoluteFilePath("ghost.mp3");

    const qint64 ms = AudiobookMetaCache::durationMsFor(folder.path(), audio);
    EXPECT_EQ(-1, ms);
}

// ── 6. invalidateFolder removes the cache file ────────────────────────────
TEST(AudiobookMetaCacheTest, InvalidateFolderRemovesCacheFile)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QDir folder(tmp.path());

    writeJson(folder.absoluteFilePath(".audiobook_meta.json"),
              seedCacheObject("ch.mp3", 1000, 0));

    ASSERT_TRUE(QFile::exists(folder.absoluteFilePath(".audiobook_meta.json")));
    AudiobookMetaCache::invalidateFolder(folder.path());
    EXPECT_FALSE(QFile::exists(folder.absoluteFilePath(".audiobook_meta.json")));
}

// ── 7. gated real-probe end-to-end ────────────────────────────────────────
TEST(AudiobookMetaCacheTest, RealProbeEndToEnd_OptInViaEnv)
{
    const QByteArray fixture = qgetenv("TANKOBAN_TEST_AUDIOBOOK_FIXTURE");
    const QByteArray probe   = qgetenv("TANKOBAN_TEST_FFPROBE");
    if (fixture.isEmpty() || probe.isEmpty())
        GTEST_SKIP() << "Set TANKOBAN_TEST_AUDIOBOOK_FIXTURE + TANKOBAN_TEST_FFPROBE to run";

    const QString audioPath = QString::fromLocal8Bit(fixture);
    const QString probePath = QString::fromLocal8Bit(probe);
    if (!QFileInfo::exists(audioPath) || !QFileInfo::exists(probePath))
        GTEST_SKIP() << "Fixture or ffprobe path does not exist";

    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QDir folder(tmp.path());

    const QString destAudio = folder.absoluteFilePath(
        QFileInfo(audioPath).fileName());
    ASSERT_TRUE(QFile::copy(audioPath, destAudio));

    const QString prevOverride =
        AudiobookMetaCache::setFfprobePathOverrideForTest(probePath);

    const qint64 ms = AudiobookMetaCache::durationMsFor(folder.path(), destAudio);
    AudiobookMetaCache::setFfprobePathOverrideForTest(prevOverride);

    EXPECT_GT(ms, 0);
    EXPECT_TRUE(QFile::exists(folder.absoluteFilePath(".audiobook_meta.json")));

    // Second call: cache hit, no ffprobe invocation needed, same value.
    AudiobookMetaCache::setFfprobePathOverrideForTest(
        folder.absoluteFilePath("no-such-ffprobe.exe"));
    const qint64 ms2 = AudiobookMetaCache::durationMsFor(folder.path(), destAudio);
    AudiobookMetaCache::setFfprobePathOverrideForTest(prevOverride);

    EXPECT_EQ(ms, ms2);
}
