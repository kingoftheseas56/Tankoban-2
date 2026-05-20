// tankoban_tests — LegacyImporter::parseStreamDownloads tests (P1.4)
//
// Fixture-driven coverage of the legacy stream_downloads.json parser. The
// on-disk shape under test is the path-keyed `byPath` object schema written
// by StreamDownloadIndex::save() at src/core/stream/StreamDownloadIndex.cpp
// lines 104-133 (kSchemaVersion = 2). See parseStreamDownloads's header
// block in src/core/torrent/LegacyImporter.cpp for the divergence note
// (real shape vs the Wave 3 prompt's "Expected legacy JSON shape").
//
// State-string preservation contract: this index has its own state
// vocabulary distinct from TorrentState. The parser maps the on-disk
// numeric enum value (Complete=0, Pending=1, Downloading=2, Failed=3) to
// the canonical lowercase string for that value — it does NOT route via
// TorrentState. These tests pin that contract.
//
// See docs/superpowers/plans/2026-05-19-torrent-persistence-collapse.md
// Phase 1 Task 1.4 for the parser contract this exercises.

#include <gtest/gtest.h>

#include "core/torrent/LegacyImporter.h"
#include "core/torrent/TorrentRow.h"

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

namespace {

using tankoban::torrent::LegacyImporter;
using tankoban::torrent::StreamDownloadRow;

class LegacyImporterDownloadsTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
    }

    QString writeFixture(const QByteArray& contents,
                         const QString& name = QStringLiteral("stream_downloads.json")) {
        const QString path = m_tmpDir.path() + QLatin1Char('/') + name;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return QString();
        }
        f.write(contents);
        f.close();
        return path;
    }

    QString missingPath() const {
        return m_tmpDir.path() + QStringLiteral("/does_not_exist.json");
    }

    // Find row in vector by canonicalPath (byPath iteration order is
    // lexicographic on QJsonObject keys, which the parser does not control —
    // tests look up by stable identity rather than depending on iteration
    // order).
    static const StreamDownloadRow* findByPath(
        const std::vector<StreamDownloadRow>& rows,
        const QString& canonicalPath) {
        for (const auto& r : rows) {
            if (r.canonicalPath == canonicalPath) return &r;
        }
        return nullptr;
    }

    QTemporaryDir m_tmpDir;
};

}  // namespace

// ─── 1. Three rows in different states, full field round-trip ───────────────
TEST_F(LegacyImporterDownloadsTest, ParsesPendingDownloadingComplete) {
    // addedAt values: ms-since-epoch (registerEpisode path) for two,
    // secs-since-epoch (registerPendingEpisode path) for one — exercises the
    // ms/sec magnitude heuristic in a single fixture.
    //   1748736000000 ms  = 2025-06-01 00:00:00 UTC
    //   1748822400000 ms  = 2025-06-02 00:00:00 UTC
    //   1748908800     s  = 2025-06-03 00:00:00 UTC
    const QByteArray fixture = R"({
      "version": 2,
      "byPath": {
        "c:\\media\\show\\s01e01.mkv": {
          "imdbId": "tt1000001",
          "type": "series",
          "season": 1,
          "episode": 1,
          "canonicalPath": "C:/Media/Show/S01E01.mkv",
          "addedAt": 1748736000000,
          "sourceGroupId": "grp-a",
          "fileSizeBytes": 1500000000,
          "state": 0,
          "progressPct": 100
        },
        "c:\\media\\show\\s01e02.mkv": {
          "imdbId": "tt1000001",
          "type": "series",
          "season": 1,
          "episode": 2,
          "canonicalPath": "C:/Media/Show/S01E02.mkv",
          "addedAt": 1748822400000,
          "sourceGroupId": "grp-a",
          "fileSizeBytes": 1400000000,
          "state": 2,
          "progressPct": 42
        },
        "c:\\media\\show\\s01e03.mkv": {
          "imdbId": "tt1000001",
          "type": "series",
          "season": 1,
          "episode": 3,
          "canonicalPath": "C:/Media/Show/S01E03.mkv",
          "addedAt": 1748908800,
          "sourceGroupId": "grp-a",
          "fileSizeBytes": 0,
          "state": 1,
          "progressPct": 0
        }
      }
    })";

    LegacyImporter imp;
    QStringList warnings;
    const auto rows = imp.parseStreamDownloads(writeFixture(fixture), &warnings);

    ASSERT_EQ(rows.size(), 3u);
    EXPECT_TRUE(warnings.isEmpty());

    // Complete row (state=0).
    const StreamDownloadRow* r1 = findByPath(rows, QStringLiteral("C:/Media/Show/S01E01.mkv"));
    ASSERT_NE(r1, nullptr);
    EXPECT_EQ(r1->imdbId,        QStringLiteral("tt1000001"));
    EXPECT_EQ(r1->season,        1);
    EXPECT_EQ(r1->episode,       1);
    EXPECT_EQ(r1->state,         QStringLiteral("complete"));
    EXPECT_TRUE(r1->infoHash.isEmpty());
    EXPECT_TRUE(r1->addedAt.isValid());
    EXPECT_EQ(r1->addedAt.toMSecsSinceEpoch(), 1748736000000LL);
    // Phase 3.4.0 schema bump — parser now extracts sourceGroupId + progressPct
    // from the legacy entry shape (which already wrote both fields).
    EXPECT_EQ(r1->sourceGroupId, QStringLiteral("grp-a"));
    EXPECT_EQ(r1->progressPct,   100);

    // Downloading row (state=2).
    const StreamDownloadRow* r2 = findByPath(rows, QStringLiteral("C:/Media/Show/S01E02.mkv"));
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(r2->episode,       2);
    EXPECT_EQ(r2->state,         QStringLiteral("downloading"));
    EXPECT_TRUE(r2->infoHash.isEmpty());
    EXPECT_EQ(r2->addedAt.toMSecsSinceEpoch(), 1748822400000LL);
    EXPECT_EQ(r2->sourceGroupId, QStringLiteral("grp-a"));
    EXPECT_EQ(r2->progressPct,   42);

    // Pending row (state=1) — addedAt in seconds; heuristic upgrades to ms.
    const StreamDownloadRow* r3 = findByPath(rows, QStringLiteral("C:/Media/Show/S01E03.mkv"));
    ASSERT_NE(r3, nullptr);
    EXPECT_EQ(r3->episode,       3);
    EXPECT_EQ(r3->state,         QStringLiteral("pending"));
    EXPECT_TRUE(r3->infoHash.isEmpty());
    EXPECT_EQ(r3->addedAt.toSecsSinceEpoch(), 1748908800LL);
    EXPECT_EQ(r3->sourceGroupId, QStringLiteral("grp-a"));
    EXPECT_EQ(r3->progressPct,   0);
}

// Phase 3.4.0 — defensive parse for legacy rows missing the new keys. Older
// stream_downloads.json files written before the schema-bump-aware
// StreamDownloadIndex existed may lack sourceGroupId / progressPct entirely;
// importer must default cleanly without warning.
TEST_F(LegacyImporterDownloadsTest, MissingSourceGroupAndProgressDefault) {
    const QByteArray fixture = R"({
      "version": 1,
      "byPath": {
        "c:/legacy.mkv": {
          "imdbId": "tt7777777",
          "type": "movie",
          "season": 0,
          "episode": 0,
          "canonicalPath": "C:/Legacy.mkv",
          "addedAt": 1748736000000,
          "state": 0
        }
      }
    })";

    LegacyImporter imp;
    QStringList warnings;
    const auto rows = imp.parseStreamDownloads(writeFixture(fixture), &warnings);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].sourceGroupId, QString());
    EXPECT_EQ(rows[0].progressPct,   0);
    EXPECT_TRUE(warnings.isEmpty());
}

// ─── 2. Malformed entry skipped; rest survive + warning ─────────────────────
TEST_F(LegacyImporterDownloadsTest, MalformedEntryWarnsButContinues) {
    // Middle entry is a JSON string instead of an object — parser should skip
    // it with a warning and still emit the two valid rows.
    const QByteArray fixture = R"({
      "version": 2,
      "byPath": {
        "key-a": {
          "imdbId": "tt2000001",
          "type": "series",
          "season": 2,
          "episode": 1,
          "canonicalPath": "C:/Media/Bad/A.mkv",
          "addedAt": 1748736000000,
          "state": 0,
          "progressPct": 100
        },
        "key-b": "this-entry-is-a-string-not-an-object",
        "key-c": {
          "imdbId": "tt2000001",
          "type": "series",
          "season": 2,
          "episode": 3,
          "canonicalPath": "C:/Media/Bad/C.mkv",
          "addedAt": 1748822400000,
          "state": 2,
          "progressPct": 50
        }
      }
    })";

    LegacyImporter imp;
    QStringList warnings;
    const auto rows = imp.parseStreamDownloads(writeFixture(fixture), &warnings);

    ASSERT_EQ(rows.size(), 2u);
    EXPECT_FALSE(warnings.isEmpty());

    EXPECT_NE(findByPath(rows, QStringLiteral("C:/Media/Bad/A.mkv")), nullptr);
    EXPECT_NE(findByPath(rows, QStringLiteral("C:/Media/Bad/C.mkv")), nullptr);

    // The warning's exact phrasing is not load-bearing, but it should mention
    // the offending key so the operator can trace the dropped row.
    bool keyBMentioned = false;
    for (const QString& w : warnings) {
        if (w.contains(QStringLiteral("key-b"))) { keyBMentioned = true; break; }
    }
    EXPECT_TRUE(keyBMentioned);
}

// ─── 3. Missing file: empty vector, no warning ──────────────────────────────
TEST_F(LegacyImporterDownloadsTest, MissingFileReturnsEmpty) {
    LegacyImporter imp;
    QStringList warnings;
    const auto rows = imp.parseStreamDownloads(missingPath(), &warnings);

    EXPECT_TRUE(rows.empty());
    EXPECT_TRUE(warnings.isEmpty());
}

// ─── 4. Malformed JSON: empty vector + warning ──────────────────────────────
TEST_F(LegacyImporterDownloadsTest, MalformedJsonWarnsAndReturnsEmpty) {
    const QByteArray fixture = "{ \"byPath\": { stray comma, broken";

    LegacyImporter imp;
    QStringList warnings;
    const auto rows = imp.parseStreamDownloads(writeFixture(fixture), &warnings);

    EXPECT_TRUE(rows.empty());
    EXPECT_FALSE(warnings.isEmpty());
}

// ─── 5. Empty byPath object: empty vector, no warning ───────────────────────
TEST_F(LegacyImporterDownloadsTest, EmptyEntriesArrayReturnsEmpty) {
    const QByteArray fixture = R"({
      "version": 2,
      "byPath": {}
    })";

    LegacyImporter imp;
    QStringList warnings;
    const auto rows = imp.parseStreamDownloads(writeFixture(fixture), &warnings);

    EXPECT_TRUE(rows.empty());
    EXPECT_TRUE(warnings.isEmpty());
}

// ─── 6. infoHash always empty in output (not present in source) ─────────────
TEST_F(LegacyImporterDownloadsTest, EmptyInfoHashPreserved) {
    // The source schema does not carry per-entry info_hash. The parser must
    // leave row.infoHash as an empty QString — importInto (P1.5) is responsible
    // for backfilling the FK link by canonical-path → torrents.hash matching.
    const QByteArray fixture = R"({
      "version": 2,
      "byPath": {
        "only-key": {
          "imdbId": "tt3000001",
          "type": "movie",
          "season": 0,
          "episode": 0,
          "canonicalPath": "C:/Media/Movie/Solo.mkv",
          "addedAt": 1748736000000,
          "state": 0,
          "progressPct": 100
        }
      }
    })";

    LegacyImporter imp;
    QStringList warnings;
    const auto rows = imp.parseStreamDownloads(writeFixture(fixture), &warnings);

    ASSERT_EQ(rows.size(), 1u);
    EXPECT_TRUE(rows[0].infoHash.isEmpty());
    // Sanity: canonicalPath still populated (i.e. parser ran fully — empty
    // infoHash is not a side-effect of an aborted row).
    EXPECT_FALSE(rows[0].canonicalPath.isEmpty());
    EXPECT_TRUE(warnings.isEmpty());
}

// ─── 7. Invalid addedAt falls back to now + warning ─────────────────────────
TEST_F(LegacyImporterDownloadsTest, InvalidAddedAtFallbackToNow) {
    // addedAt is a string (not a number) — the parser cannot decode it as an
    // epoch, so it must fall back to currentDateTimeUtc() and warn. The row
    // itself survives — only the timestamp is replaced.
    const QByteArray fixture = R"({
      "version": 2,
      "byPath": {
        "bad-added": {
          "imdbId": "tt4000001",
          "type": "series",
          "season": 5,
          "episode": 7,
          "canonicalPath": "C:/Media/Bad/Time.mkv",
          "addedAt": "not-a-number",
          "state": 0,
          "progressPct": 100
        }
      }
    })";

    const QDateTime before = QDateTime::currentDateTimeUtc().addSecs(-5);

    LegacyImporter imp;
    QStringList warnings;
    const auto rows = imp.parseStreamDownloads(writeFixture(fixture), &warnings);

    const QDateTime after = QDateTime::currentDateTimeUtc().addSecs(5);

    ASSERT_EQ(rows.size(), 1u);
    EXPECT_FALSE(warnings.isEmpty());
    EXPECT_TRUE(rows[0].addedAt.isValid());
    EXPECT_GE(rows[0].addedAt, before);
    EXPECT_LE(rows[0].addedAt, after);
    // Other fields still populated despite the timestamp fallback.
    EXPECT_EQ(rows[0].season,  5);
    EXPECT_EQ(rows[0].episode, 7);
    EXPECT_EQ(rows[0].state,   QStringLiteral("complete"));
}
