// tankoban_tests — TorrentRepository stream_groups + stream_group_items CRUD
// tests (P0.6).
//
// Covers the 9-column stream_groups upsert + the 7-column stream_group_items
// upsert (composite PK), the listStreamGroupsByImdbSeason filter, group/item
// scoping, and — crucially — the two FK actions baked into the schema:
//   1. stream_group_items.group_id -> stream_groups.group_id ON DELETE CASCADE
//      (deleting a group sweeps its items)
//   2. stream_group_items.info_hash -> torrents.hash ON DELETE SET NULL
//      (deleting the torrent leaves the item but nulls its info_hash)
//
// P0.6 test uses a raw INSERT/DELETE via an auxiliary QSqlDatabase connection
// to populate / remove torrents rows. This avoids serializing on P0.5 Jr —
// the test is correct against the committed P0.3 schema regardless of whether
// the P0.5 upsertTorrent / removeTorrent CRUD has landed yet. The aux
// connection sets PRAGMA foreign_keys = ON explicitly so the ON DELETE SET
// NULL action fires on the DELETE issued through it.
//
// See docs/superpowers/plans/2026-05-19-torrent-persistence-collapse.md Phase 0.

#include <gtest/gtest.h>

#include "core/torrent/TorrentRepository.h"
#include "core/torrent/TorrentRow.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QTemporaryDir>
#include <QVariant>

#include <algorithm>

namespace {

using tankoban::torrent::StreamGroupItemRow;
using tankoban::torrent::StreamGroupRow;
using tankoban::torrent::TorrentRepository;

class TorrentRepoStreamGroupTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
        ASSERT_TRUE(m_repo.open(dbPath()));
    }

    QString dbPath() const {
        return m_tmpDir.path() + QStringLiteral("/torrents.db");
    }

    StreamGroupRow sampleGroup(const QString& groupId,
                               const QString& imdbId = QStringLiteral("tt1234567"),
                               int season = 1) const {
        StreamGroupRow g;
        g.groupId         = groupId;
        g.imdbId          = imdbId;
        g.season          = season;
        g.label           = QStringLiteral("Sample Group ") + groupId;
        g.state           = QStringLiteral("active");
        g.retryGeneration = 2;
        g.stagingPath     = QStringLiteral("C:/Downloads/Staging/") + groupId;
        g.createdAt       = QDateTime::fromString(
            QStringLiteral("2026-05-20T12:34:56Z"), Qt::ISODate);
        g.packMode        = true;
        return g;
    }

    StreamGroupItemRow sampleItem(const QString& groupId,
                                  const QString& itemId,
                                  int episode = 1,
                                  const QString& infoHash = {}) const {
        StreamGroupItemRow it;
        it.groupId      = groupId;
        it.itemId       = itemId;
        it.episode      = episode;
        it.infoHash     = infoHash;
        it.state        = QStringLiteral("queued");
        it.errorMessage = QStringLiteral("");
        it.fileIndex    = 3;
        return it;
    }

    // Auxiliary connection helpers — see file-top comment for rationale.
    // PRAGMA foreign_keys = ON is set on the aux connection so the DELETE
    // path fires the ON DELETE SET NULL action against the live row.
    void rawInsertTorrent(const QString& hash) {
        const QString connName =
            QStringLiteral("test_aux_insert_") + hash;
        {
            QSqlDatabase aux =
                QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            aux.setDatabaseName(dbPath());
            ASSERT_TRUE(aux.open());
            QSqlQuery q(aux);
            ASSERT_TRUE(q.exec(QStringLiteral("PRAGMA foreign_keys = ON")));
            q.prepare(QStringLiteral(
                "INSERT INTO torrents (hash, state, added_at) "
                "VALUES (:h, 'active', '2026-05-20T12:00:00Z')"));
            q.bindValue(QStringLiteral(":h"), hash.toLower());
            ASSERT_TRUE(q.exec()) << q.lastError().text().toStdString();
            aux.close();
        }
        QSqlDatabase::removeDatabase(connName);
    }

    void rawDeleteTorrent(const QString& hash) {
        const QString connName =
            QStringLiteral("test_aux_delete_") + hash;
        {
            QSqlDatabase aux =
                QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            aux.setDatabaseName(dbPath());
            ASSERT_TRUE(aux.open());
            QSqlQuery q(aux);
            ASSERT_TRUE(q.exec(QStringLiteral("PRAGMA foreign_keys = ON")));
            q.prepare(QStringLiteral("DELETE FROM torrents WHERE hash = :h"));
            q.bindValue(QStringLiteral(":h"), hash.toLower());
            ASSERT_TRUE(q.exec()) << q.lastError().text().toStdString();
            aux.close();
        }
        QSqlDatabase::removeDatabase(connName);
    }

    QTemporaryDir m_tmpDir;
    TorrentRepository m_repo;
};

}  // namespace

TEST_F(TorrentRepoStreamGroupTest, UpsertGroupRoundTrip) {
    const auto g = sampleGroup(QStringLiteral("grp-001"));
    ASSERT_TRUE(m_repo.upsertStreamGroup(g));

    auto fetched = m_repo.getStreamGroup(QStringLiteral("grp-001"));
    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(fetched->groupId,         g.groupId);
    EXPECT_EQ(fetched->imdbId,          g.imdbId);
    EXPECT_EQ(fetched->season,          g.season);
    EXPECT_EQ(fetched->label,           g.label);
    EXPECT_EQ(fetched->state,           g.state);
    EXPECT_EQ(fetched->retryGeneration, g.retryGeneration);
    EXPECT_EQ(fetched->stagingPath,     g.stagingPath);
    EXPECT_EQ(fetched->createdAt,       g.createdAt);
    EXPECT_EQ(fetched->packMode,        g.packMode);
}

TEST_F(TorrentRepoStreamGroupTest, ListByImdbSeasonFilters) {
    // Three groups across two IMDb IDs / two seasons. Filter on (tt100, 1)
    // should return only the matching one.
    ASSERT_TRUE(m_repo.upsertStreamGroup(
        sampleGroup(QStringLiteral("g-a"), QStringLiteral("tt100"), 1)));
    ASSERT_TRUE(m_repo.upsertStreamGroup(
        sampleGroup(QStringLiteral("g-b"), QStringLiteral("tt100"), 2)));
    ASSERT_TRUE(m_repo.upsertStreamGroup(
        sampleGroup(QStringLiteral("g-c"), QStringLiteral("tt200"), 1)));

    const auto matches =
        m_repo.listStreamGroupsByImdbSeason(QStringLiteral("tt100"), 1);
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches[0].groupId, QStringLiteral("g-a"));

    // Sanity: listStreamGroups should return all three.
    EXPECT_EQ(m_repo.listStreamGroups().size(), 3u);
}

TEST_F(TorrentRepoStreamGroupTest, UpsertGroupItemRoundTrip) {
    ASSERT_TRUE(m_repo.upsertStreamGroup(sampleGroup(QStringLiteral("grp-x"))));

    StreamGroupItemRow it = sampleItem(QStringLiteral("grp-x"),
                                       QStringLiteral("ep-01"),
                                       /*episode=*/5);
    it.state        = QStringLiteral("downloading");
    it.errorMessage = QStringLiteral("");
    it.fileIndex    = 7;
    ASSERT_TRUE(m_repo.upsertStreamGroupItem(it));

    const auto items = m_repo.listStreamGroupItems(QStringLiteral("grp-x"));
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].groupId,      it.groupId);
    EXPECT_EQ(items[0].itemId,       it.itemId);
    EXPECT_EQ(items[0].episode,      it.episode);
    EXPECT_EQ(items[0].infoHash,     QString());  // bound NULL, read back as empty
    EXPECT_EQ(items[0].state,        it.state);
    EXPECT_EQ(items[0].errorMessage, it.errorMessage);
    EXPECT_EQ(items[0].fileIndex,    it.fileIndex);

    // Second upsert with same composite PK should update, not insert a duplicate.
    it.state    = QStringLiteral("complete");
    it.episode  = 6;
    ASSERT_TRUE(m_repo.upsertStreamGroupItem(it));
    const auto updated = m_repo.listStreamGroupItems(QStringLiteral("grp-x"));
    ASSERT_EQ(updated.size(), 1u);
    EXPECT_EQ(updated[0].state,   QStringLiteral("complete"));
    EXPECT_EQ(updated[0].episode, 6);
}

TEST_F(TorrentRepoStreamGroupTest, GroupItemsScopedToGroup) {
    ASSERT_TRUE(m_repo.upsertStreamGroup(sampleGroup(QStringLiteral("A"))));
    ASSERT_TRUE(m_repo.upsertStreamGroup(sampleGroup(QStringLiteral("B"))));

    ASSERT_TRUE(m_repo.upsertStreamGroupItem(
        sampleItem(QStringLiteral("A"), QStringLiteral("a1"))));
    ASSERT_TRUE(m_repo.upsertStreamGroupItem(
        sampleItem(QStringLiteral("A"), QStringLiteral("a2"))));
    ASSERT_TRUE(m_repo.upsertStreamGroupItem(
        sampleItem(QStringLiteral("B"), QStringLiteral("b1"))));

    const auto bItems = m_repo.listStreamGroupItems(QStringLiteral("B"));
    ASSERT_EQ(bItems.size(), 1u);
    EXPECT_EQ(bItems[0].itemId, QStringLiteral("b1"));

    const auto aItems = m_repo.listStreamGroupItems(QStringLiteral("A"));
    EXPECT_EQ(aItems.size(), 2u);
    for (const auto& it : aItems) {
        EXPECT_NE(it.itemId, QStringLiteral("b1"));
    }
}

TEST_F(TorrentRepoStreamGroupTest, RemoveGroupCascadesItems) {
    ASSERT_TRUE(m_repo.upsertStreamGroup(sampleGroup(QStringLiteral("doomed"))));
    ASSERT_TRUE(m_repo.upsertStreamGroupItem(
        sampleItem(QStringLiteral("doomed"), QStringLiteral("e1"))));
    ASSERT_TRUE(m_repo.upsertStreamGroupItem(
        sampleItem(QStringLiteral("doomed"), QStringLiteral("e2"))));
    ASSERT_EQ(m_repo.listStreamGroupItems(QStringLiteral("doomed")).size(), 2u);

    ASSERT_TRUE(m_repo.removeStreamGroup(QStringLiteral("doomed")));

    // Group gone.
    EXPECT_FALSE(m_repo.getStreamGroup(QStringLiteral("doomed")).has_value());
    // FK ON DELETE CASCADE swept the items.
    EXPECT_TRUE(m_repo.listStreamGroupItems(QStringLiteral("doomed")).empty());
}

TEST_F(TorrentRepoStreamGroupTest, ItemInfoHashFKSetNullOnTorrentRemove) {
    const QString hash =
        QStringLiteral("abcdef0123456789abcdef0123456789abcdef01");
    rawInsertTorrent(hash);

    ASSERT_TRUE(m_repo.upsertStreamGroup(sampleGroup(QStringLiteral("g-fk"))));

    StreamGroupItemRow it = sampleItem(QStringLiteral("g-fk"),
                                       QStringLiteral("ep-fk"),
                                       /*episode=*/1,
                                       /*infoHash=*/hash);
    ASSERT_TRUE(m_repo.upsertStreamGroupItem(it));

    {
        const auto items = m_repo.listStreamGroupItems(QStringLiteral("g-fk"));
        ASSERT_EQ(items.size(), 1u);
        EXPECT_EQ(items[0].infoHash, hash.toLower());
    }

    // Remove the torrent; FK ON DELETE SET NULL should null out info_hash on
    // the item, leaving the item row itself intact.
    rawDeleteTorrent(hash);

    const auto afterItems = m_repo.listStreamGroupItems(QStringLiteral("g-fk"));
    ASSERT_EQ(afterItems.size(), 1u);
    EXPECT_TRUE(afterItems[0].infoHash.isEmpty())
        << "info_hash should be NULL (empty QString) after referenced "
           "torrent row is removed; got: "
        << afterItems[0].infoHash.toStdString();
    EXPECT_EQ(afterItems[0].itemId, QStringLiteral("ep-fk"));
}

TEST_F(TorrentRepoStreamGroupTest, CompositePrimaryKeyDistinguishes) {
    // Same item_id used inside two different groups should NOT collide —
    // composite PK is (group_id, item_id), so both rows coexist.
    ASSERT_TRUE(m_repo.upsertStreamGroup(sampleGroup(QStringLiteral("G1"))));
    ASSERT_TRUE(m_repo.upsertStreamGroup(sampleGroup(QStringLiteral("G2"))));

    StreamGroupItemRow it1 = sampleItem(QStringLiteral("G1"),
                                        QStringLiteral("shared-id"));
    it1.state = QStringLiteral("downloading");
    StreamGroupItemRow it2 = sampleItem(QStringLiteral("G2"),
                                        QStringLiteral("shared-id"));
    it2.state = QStringLiteral("complete");

    ASSERT_TRUE(m_repo.upsertStreamGroupItem(it1));
    ASSERT_TRUE(m_repo.upsertStreamGroupItem(it2));

    const auto g1Items = m_repo.listStreamGroupItems(QStringLiteral("G1"));
    const auto g2Items = m_repo.listStreamGroupItems(QStringLiteral("G2"));
    ASSERT_EQ(g1Items.size(), 1u);
    ASSERT_EQ(g2Items.size(), 1u);
    EXPECT_EQ(g1Items[0].state, QStringLiteral("downloading"));
    EXPECT_EQ(g2Items[0].state, QStringLiteral("complete"));

    // Removing one shouldn't affect the other.
    ASSERT_TRUE(m_repo.removeStreamGroupItem(QStringLiteral("G1"),
                                             QStringLiteral("shared-id")));
    EXPECT_TRUE(m_repo.listStreamGroupItems(QStringLiteral("G1")).empty());
    EXPECT_EQ(m_repo.listStreamGroupItems(QStringLiteral("G2")).size(), 1u);
}
