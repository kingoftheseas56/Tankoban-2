// tankoban_tests — TorrentRepository durability / self-heal tests
// (TORRENT_DB_DURABILITY Part 1, 2026-06-02)
//
// Root cause (Agent 0): a corrupted torrents.db ("database disk image is
// malformed") made every startup hammer the bad file on the UI thread ->
// Windows "Not Responding". Durable fix: open() must run PRAGMA quick_check and,
// on a malformed file, back it up (timestamped) + recreate a fresh schema +
// emit databaseRecovered() — so a corrupt DB self-heals instead of hanging.
//
// Golden fixture (preserved reset-proof by Agent 0):
//   tests/fixtures/torrents_db_malformed_2026-06-02.db
// Signature: PRAGMA quick_check reports malformed; integrity_check shows
// "Tree 8 page 24: btreeInitPage() returns error code 11 / Rowid 34 out of order".
//
// Plan: docs/superpowers/plans/2026-06-02-torrent-db-durability.md

#include <gtest/gtest.h>

#include "core/torrent/TorrentRepository.h"
#include "core/torrent/TorrentRow.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariant>

namespace {

using tankoban::torrent::TorrentRepository;
using tankoban::torrent::TorrentRow;
using tankoban::torrent::TorrentState;

const QString kMalformedFixture =
    QStringLiteral(TANKOBAN_TEST_FIXTURE_DIR
                   "/torrents_db_malformed_2026-06-02.db");

class TorrentRepoDurabilityTest : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(m_tmpDir.isValid()); }

    QString dbPath() const {
        return m_tmpDir.path() + QStringLiteral("/torrents.db");
    }

    // Copy the golden malformed fixture into the writable temp dir so open()'s
    // recovery (which renames the file) operates on a throwaway copy.
    QString stageMalformedCopy() const {
        const QString dst = dbPath();
        EXPECT_TRUE(QFile::copy(kMalformedFixture, dst))
            << "fixture missing at " << kMalformedFixture.toStdString();
        return dst;
    }

    int corruptBackupCount() const {
        return QDir(m_tmpDir.path())
            .entryList(QStringList{QStringLiteral("torrents.db.corrupt-*")},
                       QDir::Files)
            .size();
    }

    static TorrentRow sampleRow(const QString& hash) {
        TorrentRow r;
        r.hash = hash;
        r.state = TorrentState::Active;
        r.name = QStringLiteral("Sample Torrent");
        r.addedAt = QDateTime::fromString(
            QStringLiteral("2026-06-02T12:00:00Z"), Qt::ISODate);
        r.category = QStringLiteral("stream");
        r.savePath = QStringLiteral("C:/Downloads/Sample");
        r.contentLayout = QStringLiteral("original");
        r.sequential = true;
        r.imdbId = QStringLiteral("tt1234567");
        r.season = 1;
        r.magnetUri = QStringLiteral("magnet:?xt=urn:btih:") + hash.toLower();
        return r;
    }

    QTemporaryDir m_tmpDir;
    TorrentRepository m_repo;
};

}  // namespace

// A malformed DB must be detected at open(), backed up, and replaced with a
// fresh usable schema — not silently used (which is what hung the app).
TEST_F(TorrentRepoDurabilityTest, RecoversMalformedDatabaseOnOpen) {
    const QString path = stageMalformedCopy();

    QSignalSpy spy(&m_repo, &TorrentRepository::databaseRecovered);
    ASSERT_TRUE(m_repo.open(path));

    EXPECT_EQ(spy.count(), 1) << "databaseRecovered() should fire exactly once";
    EXPECT_GE(corruptBackupCount(), 1)
        << "the malformed file must be preserved as torrents.db.corrupt-*";

    // The live DB is now a fresh, usable schema (proves recovery, not reuse).
    const QString hash =
        QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    EXPECT_FALSE(m_repo.hasTorrent(hash));  // fresh schema => empty
    ASSERT_TRUE(m_repo.upsertTorrent(sampleRow(hash)));
    EXPECT_TRUE(m_repo.hasTorrent(hash));
}

// A healthy/fresh DB must open normally with NO recovery and NO backup file.
TEST_F(TorrentRepoDurabilityTest, HealthyDatabaseOpensWithoutRecovery) {
    QSignalSpy spy(&m_repo, &TorrentRepository::databaseRecovered);
    ASSERT_TRUE(m_repo.open(dbPath()));

    EXPECT_EQ(spy.count(), 0) << "a fresh DB must not trigger recovery";
    EXPECT_EQ(corruptBackupCount(), 0) << "no backup for a healthy DB";

    const QString hash =
        QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    ASSERT_TRUE(m_repo.upsertTorrent(sampleRow(hash)));
    EXPECT_TRUE(m_repo.hasTorrent(hash));
}

// The reusable health probe (to be lifted into a shared util for the manga/book
// SQLite stores) must flag the malformed fixture and pass a fresh DB.
TEST_F(TorrentRepoDurabilityTest, DatabaseFileIsHealthyDistinguishesMalformedFromFresh) {
    const QString malformed = stageMalformedCopy();
    EXPECT_FALSE(TorrentRepository::databaseFileIsHealthy(malformed));

    // Create a fresh, valid DB at a second path, then probe it.
    const QString freshPath =
        m_tmpDir.path() + QStringLiteral("/fresh.db");
    {
        TorrentRepository fresh;
        ASSERT_TRUE(fresh.open(freshPath));
        fresh.close();
    }
    EXPECT_TRUE(TorrentRepository::databaseFileIsHealthy(freshPath));
}

// Part 2 (WAL hardening): the DB is WAL mode, and a close() (which now
// checkpoint-truncates the WAL) followed by reopen preserves data without
// corruption.
TEST_F(TorrentRepoDurabilityTest, WalModePersistsAndSurvivesReopen) {
    const QString path = dbPath();
    const QString hash =
        QStringLiteral("cccccccccccccccccccccccccccccccccccccccc");
    {
        TorrentRepository repo;
        ASSERT_TRUE(repo.open(path));
        ASSERT_TRUE(repo.upsertTorrent(sampleRow(hash)));
        repo.close();  // checkpoint(TRUNCATE) on a clean exit
    }

    // journal_mode is a persisted, file-level setting — observable from a fresh
    // independent connection.
    {
        const QString conn = QStringLiteral("walprobe_durability");
        {
            QSqlDatabase db =
                QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            db.setDatabaseName(path);
            ASSERT_TRUE(db.open());
            QSqlQuery q(db);
            ASSERT_TRUE(q.exec(QStringLiteral("PRAGMA journal_mode")));
            ASSERT_TRUE(q.next());
            EXPECT_EQ(q.value(0).toString().toLower(), QStringLiteral("wal"));
            db.close();
        }
        QSqlDatabase::removeDatabase(conn);
    }

    // Data survived close+checkpoint+reopen — no loss, no corruption.
    TorrentRepository repo2;
    ASSERT_TRUE(repo2.open(path));
    EXPECT_TRUE(repo2.hasTorrent(hash));
}
