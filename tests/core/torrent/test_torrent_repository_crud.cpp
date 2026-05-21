// tankoban_tests — TorrentRepository torrents-table CRUD tests (P0.5)
//
// Mirrors test_torrent_repository_schema.cpp shape: anonymous-namespace fixture,
// QTemporaryDir-scoped DB, open in SetUp. Exercises the torrents-table CRUD
// surface implemented in TorrentRepository.cpp.
//
// See docs/superpowers/plans/2026-05-19-torrent-persistence-collapse.md Phase 0
// Task 0.5.

#include <gtest/gtest.h>

#include "core/torrent/TorrentRepository.h"
#include "core/torrent/TorrentRow.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QTemporaryDir>

namespace {

using tankoban::torrent::TorrentRepository;
using tankoban::torrent::TorrentRow;
using tankoban::torrent::TorrentState;

class TorrentRepoCrudTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tmpDir.isValid());
        ASSERT_TRUE(m_repo.open(m_tmpDir.path() + QStringLiteral("/torrents.db")));
    }

    static TorrentRow sampleRow(const QString& hash,
                                TorrentState state = TorrentState::Active) {
        TorrentRow r;
        r.hash = hash;
        r.state = state;
        r.name = QStringLiteral("Sample Torrent");
        r.addedAt = QDateTime::fromString(QStringLiteral("2026-05-19T12:00:00Z"),
                                          Qt::ISODate);
        r.category = QStringLiteral("stream");
        r.savePath = QStringLiteral("C:/Downloads/Sample");
        r.contentLayout = QStringLiteral("original");
        r.streamGroupId = QString();
        r.sequential = true;
        r.imdbId = QStringLiteral("tt1234567");
        r.season = 1;
        r.magnetUri = QStringLiteral("magnet:?xt=urn:btih:") + hash.toLower();
        r.legacyNoMagnet = false;
        r.errorMessage = QString();
        return r;
    }

    QTemporaryDir m_tmpDir;
    TorrentRepository m_repo;
};

}  // namespace

TEST_F(TorrentRepoCrudTest, HasTorrentReturnsTrueAfterUpsert) {
    const QString hash =
        QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    EXPECT_FALSE(m_repo.hasTorrent(hash));
    ASSERT_TRUE(m_repo.upsertTorrent(sampleRow(hash)));
    EXPECT_TRUE(m_repo.hasTorrent(hash));
}

TEST_F(TorrentRepoCrudTest, HasTorrentReturnsFalseForUnknownHash) {
    const QString unknown =
        QStringLiteral("ffffffffffffffffffffffffffffffffffffffff");
    EXPECT_FALSE(m_repo.hasTorrent(unknown));
}

TEST_F(TorrentRepoCrudTest, HasTorrentIsCaseInsensitive) {
    const QString upper =
        QStringLiteral("BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB");
    ASSERT_TRUE(m_repo.upsertTorrent(sampleRow(upper)));
    EXPECT_TRUE(m_repo.hasTorrent(upper.toLower()));
    EXPECT_TRUE(m_repo.hasTorrent(upper));
}

TEST_F(TorrentRepoCrudTest, UpsertAndGetRoundTrip) {
    const QString hash =
        QStringLiteral("abcdef1234567890abcdef1234567890abcdef12");
    auto row = sampleRow(hash);
    ASSERT_TRUE(m_repo.upsertTorrent(row));

    auto fetched = m_repo.getTorrent(hash);
    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(fetched->hash, hash);
    EXPECT_EQ(fetched->state, TorrentState::Active);
    EXPECT_EQ(fetched->name, row.name);
    EXPECT_EQ(fetched->category, row.category);
    EXPECT_EQ(fetched->savePath, row.savePath);
    EXPECT_EQ(fetched->imdbId, row.imdbId);
    EXPECT_EQ(fetched->season, row.season);
    EXPECT_EQ(fetched->magnetUri, row.magnetUri);
    EXPECT_TRUE(fetched->sequential);
}

TEST_F(TorrentRepoCrudTest, HashIsCaseInsensitive) {
    const QString upper =
        QStringLiteral("ABCDEF1234567890ABCDEF1234567890ABCDEF12");
    const QString lower = upper.toLower();
    auto row = sampleRow(upper);
    ASSERT_TRUE(m_repo.upsertTorrent(row));

    auto fetched = m_repo.getTorrent(lower);
    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(fetched->hash, lower);
}

TEST_F(TorrentRepoCrudTest, UpdateStateOnly) {
    const QString hash =
        QStringLiteral("11111111111111111111111111111111aaaaaaaa");
    auto row = sampleRow(hash);
    ASSERT_TRUE(m_repo.upsertTorrent(row));

    ASSERT_TRUE(m_repo.updateTorrentState(hash, TorrentState::Paused,
                                          QStringLiteral("user paused")));

    auto fetched = m_repo.getTorrent(hash);
    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(fetched->state, TorrentState::Paused);
    EXPECT_EQ(fetched->errorMessage, QStringLiteral("user paused"));
    EXPECT_EQ(fetched->name, row.name);
    EXPECT_EQ(fetched->category, row.category);
}

TEST_F(TorrentRepoCrudTest, UpsertPreservesExistingMagnetWhenPartialEmpty) {
    const QString hash =
        QStringLiteral("22222222222222222222222222222222bbbbbbbb");
    auto row = sampleRow(hash);
    const QString originalMagnet = row.magnetUri;
    ASSERT_FALSE(originalMagnet.isEmpty());
    ASSERT_TRUE(m_repo.upsertTorrent(row));

    // Simulated partial update: caller doesn't carry the magnet URI forward.
    row.magnetUri = QString();
    row.state = TorrentState::Completed;
    ASSERT_TRUE(m_repo.upsertTorrent(row));

    auto fetched = m_repo.getTorrent(hash);
    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(fetched->state, TorrentState::Completed);
    EXPECT_EQ(fetched->magnetUri, originalMagnet);
}

TEST_F(TorrentRepoCrudTest, ResumeDataBlobRoundTrip) {
    const QString hash =
        QStringLiteral("33333333333333333333333333333333cccccccc");
    auto row = sampleRow(hash);
    ASSERT_TRUE(m_repo.upsertTorrent(row));

    const QByteArray blob = QByteArray::fromHex("deadbeefcafebabe1337c0de");
    ASSERT_TRUE(m_repo.updateTorrentResumeData(hash, blob));

    auto fetched = m_repo.getTorrent(hash);
    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(fetched->resumeData, blob);
}

TEST_F(TorrentRepoCrudTest, ListByStateFilters) {
    auto a = sampleRow(QStringLiteral("44444444444444444444444444444444dddddddd"),
                       TorrentState::Active);
    auto b = sampleRow(QStringLiteral("55555555555555555555555555555555eeeeeeee"),
                       TorrentState::Paused);
    auto c = sampleRow(QStringLiteral("66666666666666666666666666666666ffffffff"),
                       TorrentState::Completed);
    ASSERT_TRUE(m_repo.upsertTorrent(a));
    ASSERT_TRUE(m_repo.upsertTorrent(b));
    ASSERT_TRUE(m_repo.upsertTorrent(c));

    auto activeRows = m_repo.listTorrentsByState(TorrentState::Active);
    ASSERT_EQ(activeRows.size(), 1u);
    EXPECT_EQ(activeRows[0].hash, a.hash.toLower());

    auto pausedRows = m_repo.listTorrentsByState(TorrentState::Paused);
    ASSERT_EQ(pausedRows.size(), 1u);
    EXPECT_EQ(pausedRows[0].hash, b.hash.toLower());

    auto completedRows = m_repo.listTorrentsByState(TorrentState::Completed);
    ASSERT_EQ(completedRows.size(), 1u);
    EXPECT_EQ(completedRows[0].hash, c.hash.toLower());
}

TEST_F(TorrentRepoCrudTest, ListByImdbSeasonFilters) {
    auto a = sampleRow(QStringLiteral("77777777777777777777777777777777aaaaaaaa"));
    a.imdbId = QStringLiteral("tt100");
    a.season = 1;
    auto b = sampleRow(QStringLiteral("88888888888888888888888888888888bbbbbbbb"));
    b.imdbId = QStringLiteral("tt100");
    b.season = 2;
    auto c = sampleRow(QStringLiteral("99999999999999999999999999999999cccccccc"));
    c.imdbId = QStringLiteral("tt200");
    c.season = 1;
    ASSERT_TRUE(m_repo.upsertTorrent(a));
    ASSERT_TRUE(m_repo.upsertTorrent(b));
    ASSERT_TRUE(m_repo.upsertTorrent(c));

    auto results = m_repo.listTorrentsByImdb(QStringLiteral("tt100"), 1);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].hash, a.hash.toLower());

    auto otherSeason = m_repo.listTorrentsByImdb(QStringLiteral("tt100"), 2);
    ASSERT_EQ(otherSeason.size(), 1u);
    EXPECT_EQ(otherSeason[0].hash, b.hash.toLower());

    auto otherImdb = m_repo.listTorrentsByImdb(QStringLiteral("tt200"), 1);
    ASSERT_EQ(otherImdb.size(), 1u);
    EXPECT_EQ(otherImdb[0].hash, c.hash.toLower());
}

TEST_F(TorrentRepoCrudTest, ListByStreamGroupFilters) {
    auto a = sampleRow(QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa11111111"));
    a.streamGroupId = QStringLiteral("group-A");
    auto b = sampleRow(QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb22222222"));
    b.streamGroupId = QStringLiteral("group-A");
    auto c = sampleRow(QStringLiteral("cccccccccccccccccccccccccccccccc33333333"));
    c.streamGroupId = QStringLiteral("group-B");
    ASSERT_TRUE(m_repo.upsertTorrent(a));
    ASSERT_TRUE(m_repo.upsertTorrent(b));
    ASSERT_TRUE(m_repo.upsertTorrent(c));

    auto groupA = m_repo.listTorrentsByStreamGroup(QStringLiteral("group-A"));
    ASSERT_EQ(groupA.size(), 2u);

    auto groupB = m_repo.listTorrentsByStreamGroup(QStringLiteral("group-B"));
    ASSERT_EQ(groupB.size(), 1u);
    EXPECT_EQ(groupB[0].hash, c.hash.toLower());
}

TEST_F(TorrentRepoCrudTest, RemoveTorrent) {
    const QString hash =
        QStringLiteral("dddddddddddddddddddddddddddddddd44444444");
    auto row = sampleRow(hash);
    ASSERT_TRUE(m_repo.upsertTorrent(row));
    ASSERT_TRUE(m_repo.getTorrent(hash).has_value());

    ASSERT_TRUE(m_repo.removeTorrent(hash));
    EXPECT_FALSE(m_repo.getTorrent(hash).has_value());
}
