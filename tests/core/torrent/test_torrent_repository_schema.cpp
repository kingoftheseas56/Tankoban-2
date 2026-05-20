// tankoban_tests — TorrentRepository schema + meta tests (P0.4)
//
// Validates the schema lifecycle, idempotency, meta key-value roundtrip, and
// table presence. CRUD operations are exercised in test_torrent_repository_crud.cpp
// (P0.5/P0.6/P0.7).
//
// See docs/superpowers/plans/2026-05-19-torrent-persistence-collapse.md Phase 0.

#include <gtest/gtest.h>

#include "core/torrent/TorrentRepository.h"
#include "core/torrent/TorrentRow.h"

#include <QTemporaryDir>
#include <QString>

namespace {

using tankoban::torrent::TorrentRepository;

class TorrentRepositorySchemaTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
    }

    QString dbPath() const {
        return m_tmpDir.path() + QStringLiteral("/torrents.db");
    }

    QTemporaryDir m_tmpDir;
};

}  // namespace

TEST_F(TorrentRepositorySchemaTest, OpensAndCreatesSchema) {
    TorrentRepository repo;
    ASSERT_TRUE(repo.open(dbPath()));
    EXPECT_TRUE(repo.isOpen());
    EXPECT_EQ(repo.schemaVersion(), TorrentRepository::kSchemaVersion);
}

TEST_F(TorrentRepositorySchemaTest, AllExpectedTablesExist) {
    TorrentRepository repo;
    ASSERT_TRUE(repo.open(dbPath()));
    EXPECT_TRUE(repo.hasTable(QStringLiteral("torrents")));
    EXPECT_TRUE(repo.hasTable(QStringLiteral("stream_groups")));
    EXPECT_TRUE(repo.hasTable(QStringLiteral("stream_group_items")));
    EXPECT_TRUE(repo.hasTable(QStringLiteral("stream_downloads_index")));
    EXPECT_TRUE(repo.hasTable(QStringLiteral("schema_meta")));
    EXPECT_FALSE(repo.hasTable(QStringLiteral("nonexistent_table")));
}

TEST_F(TorrentRepositorySchemaTest, IdempotentReopen) {
    {
        TorrentRepository repo;
        ASSERT_TRUE(repo.open(dbPath()));
        repo.setMetaValue(QStringLiteral("persisted_key"), QStringLiteral("persisted_value"));
    }
    // Second open of the same DB should not wipe data and should not fail
    // initSchema (CREATE TABLE IF NOT EXISTS is idempotent).
    TorrentRepository repo2;
    ASSERT_TRUE(repo2.open(dbPath()));
    EXPECT_EQ(repo2.schemaVersion(), TorrentRepository::kSchemaVersion);
    EXPECT_EQ(repo2.metaValue(QStringLiteral("persisted_key")),
              QStringLiteral("persisted_value"));
}

TEST_F(TorrentRepositorySchemaTest, MetaRoundTrip) {
    TorrentRepository repo;
    ASSERT_TRUE(repo.open(dbPath()));

    EXPECT_TRUE(repo.metaValue(QStringLiteral("absent_key")).isEmpty());

    repo.setMetaValue(QStringLiteral("k1"), QStringLiteral("v1"));
    EXPECT_EQ(repo.metaValue(QStringLiteral("k1")), QStringLiteral("v1"));

    repo.setMetaValue(QStringLiteral("k1"), QStringLiteral("v1-updated"));
    EXPECT_EQ(repo.metaValue(QStringLiteral("k1")), QStringLiteral("v1-updated"));

    repo.setMetaValue(QStringLiteral("migration_completed_at"),
                      QStringLiteral("2026-05-19T12:00:00Z"));
    EXPECT_EQ(repo.metaValue(QStringLiteral("migration_completed_at")),
              QStringLiteral("2026-05-19T12:00:00Z"));
}

TEST_F(TorrentRepositorySchemaTest, SchemaVersionStampedOnFirstOpen) {
    TorrentRepository repo;
    ASSERT_TRUE(repo.open(dbPath()));
    // schema_meta should already have schema_version stamped by initSchema.
    EXPECT_EQ(repo.metaValue(QStringLiteral("schema_version")),
              QString::number(TorrentRepository::kSchemaVersion));
}

TEST_F(TorrentRepositorySchemaTest, TransactionWrappersWorkWhenOpen) {
    TorrentRepository repo;
    ASSERT_TRUE(repo.open(dbPath()));
    EXPECT_TRUE(repo.beginTransaction());
    repo.setMetaValue(QStringLiteral("tx_key"), QStringLiteral("tx_value"));
    EXPECT_TRUE(repo.commit());
    EXPECT_EQ(repo.metaValue(QStringLiteral("tx_key")), QStringLiteral("tx_value"));
}

TEST_F(TorrentRepositorySchemaTest, RollbackUndoesUncommittedWrites) {
    TorrentRepository repo;
    ASSERT_TRUE(repo.open(dbPath()));
    repo.setMetaValue(QStringLiteral("before"), QStringLiteral("set"));

    EXPECT_TRUE(repo.beginTransaction());
    repo.setMetaValue(QStringLiteral("inside_tx"), QStringLiteral("written"));
    EXPECT_TRUE(repo.rollback());

    EXPECT_EQ(repo.metaValue(QStringLiteral("before")), QStringLiteral("set"));
    EXPECT_TRUE(repo.metaValue(QStringLiteral("inside_tx")).isEmpty());
}

TEST_F(TorrentRepositorySchemaTest, ClosedRepositoryRejectsOperations) {
    TorrentRepository repo;
    ASSERT_TRUE(repo.open(dbPath()));
    repo.close();
    EXPECT_FALSE(repo.isOpen());
    EXPECT_TRUE(repo.metaValue(QStringLiteral("any")).isEmpty());
    EXPECT_FALSE(repo.beginTransaction());
}
