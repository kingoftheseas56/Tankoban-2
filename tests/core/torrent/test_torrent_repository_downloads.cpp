// tankoban_tests — TorrentRepository stream_downloads_index CRUD tests (P0.7)
//
// Mirrors test_torrent_repository_schema.cpp / test_torrent_repository_crud.cpp
// shape: anonymous-namespace fixture, QTemporaryDir-scoped DB, open in SetUp.
// Exercises the stream_downloads_index CRUD surface plus FK ON DELETE SET NULL
// behaviour against the torrents table.
//
// See docs/superpowers/plans/2026-05-19-torrent-persistence-collapse.md Phase 0
// Task 0.7.

#include <gtest/gtest.h>

#include "core/torrent/TorrentRepository.h"
#include "core/torrent/TorrentRow.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QTemporaryDir>

namespace {

using tankoban::torrent::StreamDownloadRow;
using tankoban::torrent::TorrentRepository;

class TorrentRepoStreamDownloadTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
        ASSERT_TRUE(m_repo.open(m_tmpDir.path() + QStringLiteral("/torrents.db")));
    }

    static StreamDownloadRow sampleRow(const QString& canonicalPath,
                                       const QString& imdbId = QStringLiteral("tt1234567"),
                                       int season = 1,
                                       int episode = 1) {
        StreamDownloadRow r;
        r.canonicalPath = canonicalPath;
        r.imdbId = imdbId;
        r.season = season;
        r.episode = episode;
        r.state = QStringLiteral("downloading");
        r.infoHash = QStringLiteral("abcdef1234567890abcdef1234567890abcdef12");
        r.addedAt = QDateTime::fromString(QStringLiteral("2026-05-19T12:00:00Z"),
                                          Qt::ISODate);
        return r;
    }

    // Raw INSERT into torrents — sidesteps upsertTorrent so this test file is
    // independent of P0.5's CRUD implementation. Schema enforces NOT NULL on
    // hash / state / added_at; everything else has a DEFAULT.
    bool insertRawTorrent(const QString& hash) {
        QSqlDatabase db = QSqlDatabase::database(connectionName());
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO torrents (hash, state, added_at) "
            "VALUES (:hash, :state, :added_at)"));
        q.bindValue(QStringLiteral(":hash"), hash.toLower());
        q.bindValue(QStringLiteral(":state"), QStringLiteral("active"));
        q.bindValue(QStringLiteral(":added_at"),
                    QStringLiteral("2026-05-19T12:00:00Z"));
        return q.exec();
    }

    // Recover the repository's underlying QSqlDatabase connection name. Each
    // TorrentRepository instance computes its connection name from `this`
    // pointer hex; QSqlDatabase::connectionNames() returns the active ones.
    QString connectionName() const {
        for (const QString& name : QSqlDatabase::connectionNames()) {
            if (name.startsWith(QStringLiteral("TorrentRepository_"))) {
                return name;
            }
        }
        return {};
    }

    bool rawDeleteTorrent(const QString& hash) {
        QSqlDatabase db = QSqlDatabase::database(connectionName());
        QSqlQuery q(db);
        q.prepare(QStringLiteral("DELETE FROM torrents WHERE hash = :hash"));
        q.bindValue(QStringLiteral(":hash"), hash.toLower());
        return q.exec();
    }

    QTemporaryDir m_tmpDir;
    TorrentRepository m_repo;
};

}  // namespace

TEST_F(TorrentRepoStreamDownloadTest, UpsertRoundTrip) {
    const QString path = QStringLiteral("C:/Downloads/Show/S01E01.mkv");
    auto row = sampleRow(path);
    row.imdbId = QStringLiteral("tt7654321");
    row.season = 3;
    row.episode = 7;
    row.state = QStringLiteral("complete");
    row.infoHash = QStringLiteral("ABCDEF1234567890ABCDEF1234567890ABCDEF12");
    row.addedAt = QDateTime::fromString(QStringLiteral("2026-05-19T15:30:00Z"),
                                        Qt::ISODate);
    // FK requires the referenced torrent row to exist (PRAGMA foreign_keys=ON).
    ASSERT_TRUE(insertRawTorrent(row.infoHash));
    ASSERT_TRUE(m_repo.upsertStreamDownload(row));

    auto fetched = m_repo.getStreamDownload(path);
    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(fetched->canonicalPath, row.canonicalPath);
    EXPECT_EQ(fetched->imdbId, row.imdbId);
    EXPECT_EQ(fetched->season, row.season);
    EXPECT_EQ(fetched->episode, row.episode);
    EXPECT_EQ(fetched->state, row.state);
    EXPECT_EQ(fetched->infoHash, row.infoHash.toLower());
    EXPECT_EQ(fetched->addedAt, row.addedAt);
}

TEST_F(TorrentRepoStreamDownloadTest, ListByImdbSeasonFilters) {
    auto a = sampleRow(QStringLiteral("C:/Downloads/A.mkv"),
                       QStringLiteral("tt100"), 1, 1);
    a.infoHash = QString();
    auto b = sampleRow(QStringLiteral("C:/Downloads/B.mkv"),
                       QStringLiteral("tt100"), 2, 1);
    b.infoHash = QString();
    auto c = sampleRow(QStringLiteral("C:/Downloads/C.mkv"),
                       QStringLiteral("tt200"), 1, 1);
    c.infoHash = QString();
    ASSERT_TRUE(m_repo.upsertStreamDownload(a));
    ASSERT_TRUE(m_repo.upsertStreamDownload(b));
    ASSERT_TRUE(m_repo.upsertStreamDownload(c));

    auto results = m_repo.listStreamDownloadsByImdb(QStringLiteral("tt100"), 1);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].canonicalPath, a.canonicalPath);

    auto otherSeason = m_repo.listStreamDownloadsByImdb(QStringLiteral("tt100"), 2);
    ASSERT_EQ(otherSeason.size(), 1u);
    EXPECT_EQ(otherSeason[0].canonicalPath, b.canonicalPath);

    auto otherImdb = m_repo.listStreamDownloadsByImdb(QStringLiteral("tt200"), 1);
    ASSERT_EQ(otherImdb.size(), 1u);
    EXPECT_EQ(otherImdb[0].canonicalPath, c.canonicalPath);
}

TEST_F(TorrentRepoStreamDownloadTest, InfoHashFKSetNullOnTorrentRemove) {
    const QString path = QStringLiteral("C:/Downloads/fk-test.mkv");
    const QString hash =
        QStringLiteral("11111111111111111111111111111111aaaaaaaa");
    ASSERT_TRUE(insertRawTorrent(hash));

    auto row = sampleRow(path);
    row.infoHash = hash;
    ASSERT_TRUE(m_repo.upsertStreamDownload(row));

    // Sanity: the download carries the hash before the parent row goes away.
    {
        auto pre = m_repo.getStreamDownload(path);
        ASSERT_TRUE(pre.has_value());
        EXPECT_EQ(pre->infoHash, hash.toLower());
    }

    ASSERT_TRUE(rawDeleteTorrent(hash));

    // FK ON DELETE SET NULL should have nulled out info_hash; QString round-trip
    // surfaces SQL NULL as an empty string.
    auto post = m_repo.getStreamDownload(path);
    ASSERT_TRUE(post.has_value());
    EXPECT_TRUE(post->infoHash.isEmpty());
}

TEST_F(TorrentRepoStreamDownloadTest, UpsertSamePathReplaces) {
    const QString path = QStringLiteral("C:/Downloads/replace-me.mkv");
    auto first = sampleRow(path);
    first.infoHash = QString();
    first.state = QStringLiteral("pending");
    first.episode = 1;
    ASSERT_TRUE(m_repo.upsertStreamDownload(first));

    auto second = sampleRow(path);
    second.infoHash = QString();
    second.state = QStringLiteral("complete");
    second.episode = 5;
    ASSERT_TRUE(m_repo.upsertStreamDownload(second));

    auto all = m_repo.listStreamDownloads();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].canonicalPath, path);
    EXPECT_EQ(all[0].state, QStringLiteral("complete"));
    EXPECT_EQ(all[0].episode, 5);
}

TEST_F(TorrentRepoStreamDownloadTest, RemoveStreamDownload) {
    const QString path = QStringLiteral("C:/Downloads/remove-me.mkv");
    auto row = sampleRow(path);
    row.infoHash = QString();
    ASSERT_TRUE(m_repo.upsertStreamDownload(row));
    ASSERT_TRUE(m_repo.getStreamDownload(path).has_value());

    ASSERT_TRUE(m_repo.removeStreamDownload(path));
    EXPECT_FALSE(m_repo.getStreamDownload(path).has_value());
}

TEST_F(TorrentRepoStreamDownloadTest, ListAllReturnsEverything) {
    auto a = sampleRow(QStringLiteral("C:/Downloads/all-A.mkv"),
                       QStringLiteral("tt300"), 1, 1);
    a.infoHash = QString();
    auto b = sampleRow(QStringLiteral("C:/Downloads/all-B.mkv"),
                       QStringLiteral("tt300"), 1, 2);
    b.infoHash = QString();
    auto c = sampleRow(QStringLiteral("C:/Downloads/all-C.mkv"),
                       QStringLiteral("tt400"), 2, 3);
    c.infoHash = QString();
    ASSERT_TRUE(m_repo.upsertStreamDownload(a));
    ASSERT_TRUE(m_repo.upsertStreamDownload(b));
    ASSERT_TRUE(m_repo.upsertStreamDownload(c));

    auto all = m_repo.listStreamDownloads();
    EXPECT_EQ(all.size(), 3u);
}
