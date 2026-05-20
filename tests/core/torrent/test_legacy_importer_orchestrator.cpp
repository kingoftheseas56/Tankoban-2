// tankoban_tests — LegacyImporter::importInto orchestrator tests (P1.5)
//
// Exercises the one-shot importer that binds every per-source parser + every
// per-row upsert into a single repository transaction, stamps
// schema_meta `migration_completed_at` on commit, and surfaces per-row
// warnings to the caller's summary.
//
// Per-parser correctness is owned by test_legacy_importer_{torrents,resume,
// groups,downloads}.cpp — this file is the integration layer test only:
// cross-source orchestration, transaction semantics, and the early-exit
// guards (repo-not-open, begin-transaction-failure).
//
// See docs/superpowers/plans/2026-05-19-torrent-persistence-collapse.md
// Phase 1 Task 1.5.

#include <gtest/gtest.h>

#include "core/torrent/LegacyImporter.h"
#include "core/torrent/TorrentRepository.h"
#include "core/torrent/TorrentRow.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

namespace {

using tankoban::torrent::LegacyImporter;
using tankoban::torrent::LegacyImportSummary;
using tankoban::torrent::LegacySources;
using tankoban::torrent::TorrentRepository;
using tankoban::torrent::TorrentRow;

class LegacyImporterOrchestratorTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
        ASSERT_TRUE(QDir(m_tmpDir.path()).mkpath(QStringLiteral("resume")));
    }

    QString writeFixture(const QByteArray& bytes, const QString& name) {
        const QString path =
            m_tmpDir.path() + QLatin1Char('/') + name;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return QString();
        }
        f.write(bytes);
        f.close();
        return path;
    }

    QString writeResumeBlob(const QString& hashLowercase,
                            const QByteArray& bytes) {
        const QString path = m_tmpDir.path()
            + QStringLiteral("/resume/")
            + hashLowercase + QStringLiteral(".fastresume");
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return QString();
        }
        f.write(bytes);
        f.close();
        return path;
    }

    LegacySources defaultSources() const {
        LegacySources s;
        s.torrentsJsonPath =
            m_tmpDir.path() + QStringLiteral("/torrents.json");
        s.streamBulkGroupsJsonPath =
            m_tmpDir.path() + QStringLiteral("/stream_bulk_groups.json");
        s.streamDownloadsJsonPath =
            m_tmpDir.path() + QStringLiteral("/stream_downloads.json");
        s.resumeCacheDir = m_tmpDir.path() + QStringLiteral("/resume");
        return s;
    }

    QString dbPath() const {
        return m_tmpDir.path() + QStringLiteral("/torrents.db");
    }

    QTemporaryDir m_tmpDir;
};

}  // namespace

// ─── Headline coverage: full round-trip with torrent + resume blob ────────────
TEST_F(LegacyImporterOrchestratorTest, FullImportRoundTrip) {
    const QByteArray torrentsFixture = R"({
      "active": {
        "deadbeef1234567890deadbeef1234567890dead": {
          "state": "completed",
          "name": "X",
          "addedAt": "2026-05-01T10:00:00Z",
          "imdbId": "tt1",
          "season": 0,
          "category": "stream"
        }
      }
    })";
    writeFixture(torrentsFixture, QStringLiteral("torrents.json"));
    writeResumeBlob(QStringLiteral("deadbeef1234567890deadbeef1234567890dead"),
                    QByteArray::fromHex("cafe"));
    writeFixture(R"({"groups":[]})",     QStringLiteral("stream_bulk_groups.json"));
    writeFixture(R"({"byPath":{}})",     QStringLiteral("stream_downloads.json"));

    TorrentRepository repo;
    ASSERT_TRUE(repo.open(dbPath()));

    LegacyImporter imp;
    const auto summary = imp.importInto(repo, defaultSources());

    EXPECT_EQ(summary.torrentsImported, 1);
    EXPECT_EQ(summary.torrentsLegacyNoMagnet, 1);     // fixture has no magnetUri
    EXPECT_EQ(summary.resumeBlobsAttached, 1);
    EXPECT_EQ(summary.streamGroupsImported, 0);
    EXPECT_EQ(summary.streamGroupItemsImported, 0);
    EXPECT_EQ(summary.streamDownloadsImported, 0);
    EXPECT_FALSE(repo.metaValue(QStringLiteral("migration_completed_at")).isEmpty());

    const auto fetched = repo.getTorrent(
        QStringLiteral("deadbeef1234567890deadbeef1234567890dead"));
    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(fetched->resumeData, QByteArray::fromHex("cafe"));
    EXPECT_TRUE(fetched->legacyNoMagnet);
    EXPECT_EQ(fetched->imdbId, QStringLiteral("tt1"));
}

// ─── Cross-source: torrent + group with item + download all land together ────
TEST_F(LegacyImporterOrchestratorTest, ImportsAllFourSourcesInOnePass) {
    writeFixture(R"({
      "active": {
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa": {
          "state": "downloading",
          "name": "BB S01",
          "addedAt": "2026-05-10T08:00:00Z",
          "category": "stream",
          "imdbId": "tt0903747",
          "season": 1,
          "streamGroupId": "grp-bb-s01",
          "magnetUri": "magnet:?xt=urn:btih:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        }
      }
    })", QStringLiteral("torrents.json"));

    writeFixture(R"({
      "groups": [
        {
          "groupId": "grp-bb-s01",
          "imdbId": "tt0903747",
          "season": 1,
          "label": "Breaking Bad S01",
          "state": "Downloading",
          "createdAt": "2026-05-10T08:00:00Z",
          "items": [
            { "itemId": "ep01", "episode": 1,
              "infoHash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
              "state": "Downloading" },
            { "itemId": "ep02", "episode": 2, "state": "Pending" }
          ]
        }
      ]
    })", QStringLiteral("stream_bulk_groups.json"));

    writeFixture(R"({
      "byPath": {
        "c:/media/bb/s01e01.mkv": {
          "imdbId": "tt0903747",
          "type": "series",
          "season": 1,
          "episode": 1,
          "canonicalPath": "C:/Media/BB/S01E01.mkv",
          "addedAt": 1746864000000,
          "state": 0,
          "progressPct": 100
        }
      }
    })", QStringLiteral("stream_downloads.json"));

    TorrentRepository repo;
    ASSERT_TRUE(repo.open(dbPath()));

    LegacyImporter imp;
    const auto summary = imp.importInto(repo, defaultSources());

    EXPECT_EQ(summary.torrentsImported, 1);
    EXPECT_EQ(summary.torrentsLegacyNoMagnet, 0);  // magnetUri present
    EXPECT_EQ(summary.streamGroupsImported, 1);
    EXPECT_EQ(summary.streamGroupItemsImported, 2);
    EXPECT_EQ(summary.streamDownloadsImported, 1);
    EXPECT_FALSE(repo.metaValue(QStringLiteral("migration_completed_at")).isEmpty());

    // Round-trip spot-checks on each table.
    const auto group = repo.getStreamGroup(QStringLiteral("grp-bb-s01"));
    ASSERT_TRUE(group.has_value());
    EXPECT_EQ(group->imdbId, QStringLiteral("tt0903747"));
    EXPECT_EQ(group->season, 1);

    const auto items = repo.listStreamGroupItems(QStringLiteral("grp-bb-s01"));
    EXPECT_EQ(items.size(), 2u);

    const auto download = repo.getStreamDownload(
        QStringLiteral("C:/Media/BB/S01E01.mkv"));
    ASSERT_TRUE(download.has_value());
    EXPECT_EQ(download->state, QStringLiteral("complete"));
}

// ─── Defensive: every legacy file absent → zero imports, no crash, meta stamped
//
// Fresh-install case. The four legacy files don't exist; every parser silent-
// skips (returns empty vector without warning per its missing-file contract).
// importInto still commits the (empty) transaction and stamps
// migration_completed_at so the next-boot check sees "migration ran" and
// skips the importer entirely.
TEST_F(LegacyImporterOrchestratorTest, MissingLegacyFilesYieldEmptyImport) {
    TorrentRepository repo;
    ASSERT_TRUE(repo.open(dbPath()));

    LegacyImporter imp;
    const auto summary = imp.importInto(repo, defaultSources());

    EXPECT_EQ(summary.torrentsImported, 0);
    EXPECT_EQ(summary.streamGroupsImported, 0);
    EXPECT_EQ(summary.streamGroupItemsImported, 0);
    EXPECT_EQ(summary.streamDownloadsImported, 0);
    EXPECT_EQ(summary.resumeBlobsAttached, 0);
    EXPECT_TRUE(summary.warnings.isEmpty());
    EXPECT_FALSE(repo.metaValue(QStringLiteral("migration_completed_at")).isEmpty());
}

// ─── Guard: importInto called on an unopened repo returns warning, no tx ─────
TEST_F(LegacyImporterOrchestratorTest, RepositoryNotOpenReturnsWarning) {
    TorrentRepository repo;
    ASSERT_FALSE(repo.isOpen());

    LegacyImporter imp;
    const auto summary = imp.importInto(repo, defaultSources());

    EXPECT_EQ(summary.torrentsImported, 0);
    ASSERT_FALSE(summary.warnings.isEmpty());
    EXPECT_TRUE(summary.warnings.first().contains(QStringLiteral("not open")));
}

// ─── Resume blob count tracks only non-empty attachments ─────────────────────
//
// Two legacy rows; only one has a matching .fastresume on disk. summary
// should reflect 2 torrents imported but 1 resume blob attached.
TEST_F(LegacyImporterOrchestratorTest, ResumeBlobCountReflectsActualAttachments) {
    writeFixture(R"({
      "active": {
        "1111111111111111111111111111111111111111": {
          "state": "completed",
          "name": "Has resume",
          "addedAt": "2026-05-01T10:00:00Z",
          "category": "stream"
        },
        "2222222222222222222222222222222222222222": {
          "state": "paused",
          "name": "No resume",
          "addedAt": "2026-05-02T10:00:00Z",
          "category": "stream"
        }
      }
    })", QStringLiteral("torrents.json"));
    writeResumeBlob(QStringLiteral("1111111111111111111111111111111111111111"),
                    QByteArray::fromHex("beef"));

    TorrentRepository repo;
    ASSERT_TRUE(repo.open(dbPath()));

    LegacyImporter imp;
    const auto summary = imp.importInto(repo, defaultSources());

    EXPECT_EQ(summary.torrentsImported, 2);
    EXPECT_EQ(summary.resumeBlobsAttached, 1);
    EXPECT_EQ(summary.torrentsLegacyNoMagnet, 2);

    const auto withBlob = repo.getTorrent(
        QStringLiteral("1111111111111111111111111111111111111111"));
    ASSERT_TRUE(withBlob.has_value());
    EXPECT_FALSE(withBlob->resumeData.isEmpty());

    const auto noBlob = repo.getTorrent(
        QStringLiteral("2222222222222222222222222222222222222222"));
    ASSERT_TRUE(noBlob.has_value());
    EXPECT_TRUE(noBlob->resumeData.isEmpty());
}
