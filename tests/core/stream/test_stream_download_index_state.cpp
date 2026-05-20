#include <gtest/gtest.h>

#include "core/stream/StreamDownloadIndex.h"
#include "core/torrent/TorrentRepository.h"

#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

namespace {

class StreamDownloadIndexStateTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_tmpDir.isValid());
        // Phase 3.4 — StreamDownloadIndex now backed by TorrentRepository.
        m_repo = std::make_unique<tankoban::torrent::TorrentRepository>();
        ASSERT_TRUE(m_repo->open(m_tmpDir.path() + QStringLiteral("/torrents.db")));
        m_index = std::make_unique<StreamDownloadIndex>();
        m_index->setRepository(m_repo.get());
    }

    QTemporaryDir                                              m_tmpDir;
    std::unique_ptr<tankoban::torrent::TorrentRepository>      m_repo;
    std::unique_ptr<StreamDownloadIndex>                       m_index;
};

}  // namespace

TEST_F(StreamDownloadIndexStateTest, RegisterPendingThenProgressFlipsToDownloading)
{
    QSignalSpy spy(m_index.get(), &StreamDownloadIndex::entryStateChanged);
    m_index->registerPendingEpisode(
        QStringLiteral("tt18923754"), 1, 1,
        QStringLiteral("C:/dl/Daredevil.S01E01.mkv"),
        QStringLiteral("tankorent:abc"),
        1500000000LL);

    auto entries = m_index->entriesForImdb(QStringLiteral("tt18923754"));
    ASSERT_EQ(entries.size(), 1);
    EXPECT_EQ(entries[0].state, StreamDownloadIndex::Entry::Pending);
    EXPECT_EQ(entries[0].progressPct, 0);
    EXPECT_GE(spy.count(), 1);

    m_index->updateEpisodeProgress(QStringLiteral("tt18923754"), 1, 1, 25);

    entries = m_index->entriesForImdb(QStringLiteral("tt18923754"));
    ASSERT_EQ(entries.size(), 1);
    EXPECT_EQ(entries[0].state, StreamDownloadIndex::Entry::Downloading);
    EXPECT_EQ(entries[0].progressPct, 25);
}

TEST_F(StreamDownloadIndexStateTest, ProgressAt100FlipsToComplete)
{
    m_index->registerPendingEpisode(
        QStringLiteral("tt18923754"), 1, 1,
        QStringLiteral("C:/dl/Daredevil.S01E01.mkv"),
        QStringLiteral("tankorent:abc"),
        1500000000LL);
    m_index->updateEpisodeProgress(QStringLiteral("tt18923754"), 1, 1, 100);

    auto entries = m_index->entriesForImdb(QStringLiteral("tt18923754"));
    ASSERT_EQ(entries.size(), 1);
    EXPECT_EQ(entries[0].state, StreamDownloadIndex::Entry::Complete);
    EXPECT_EQ(entries[0].progressPct, 100);
}

TEST_F(StreamDownloadIndexStateTest, ProgressUpdateOnUnknownEntryIsNoOp)
{
    QSignalSpy spy(m_index.get(), &StreamDownloadIndex::entryStateChanged);
    m_index->updateEpisodeProgress(QStringLiteral("tt99999"), 1, 1, 50);
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(StreamDownloadIndexStateTest, EvictBySourceGroupDropsAllPackEntries)
{
    // Register 3 episodes under same sourceGroupId + 1 under a different one.
    m_index->registerPendingEpisode(
        QStringLiteral("tt18923754"), 1, 1,
        QStringLiteral("C:/dl/A/S01E01.mkv"),
        QStringLiteral("tankorent:groupA"), 1500000000LL);
    m_index->registerPendingEpisode(
        QStringLiteral("tt18923754"), 1, 2,
        QStringLiteral("C:/dl/A/S01E02.mkv"),
        QStringLiteral("tankorent:groupA"), 1500000000LL);
    m_index->registerPendingEpisode(
        QStringLiteral("tt18923754"), 1, 3,
        QStringLiteral("C:/dl/A/S01E03.mkv"),
        QStringLiteral("tankorent:groupA"), 1500000000LL);
    m_index->registerPendingEpisode(
        QStringLiteral("tt0141842"), 6, 2,
        QStringLiteral("C:/dl/B/S06E02.mkv"),
        QStringLiteral("tankorent:groupB"), 1500000000LL);

    m_index->evictBySourceGroup(QStringLiteral("tankorent:groupA"));

    EXPECT_EQ(m_index->entriesForImdb(QStringLiteral("tt18923754")).size(), 0);
    EXPECT_EQ(m_index->entriesForImdb(QStringLiteral("tt0141842")).size(), 1);
    EXPECT_FALSE(m_index->hasAnyForImdb(QStringLiteral("tt18923754")));
    EXPECT_TRUE(m_index->hasAnyForImdb(QStringLiteral("tt0141842")));
}

TEST_F(StreamDownloadIndexStateTest, RegisterPendingMovieDefaultsTypeMovie)
{
    m_index->registerPendingMovie(
        QStringLiteral("tt0137523"),
        QStringLiteral("C:/dl/Fight.Club.mkv"),
        QStringLiteral("tankorent:fc"),
        5000000000LL);
    auto entries = m_index->entriesForImdb(QStringLiteral("tt0137523"));
    ASSERT_EQ(entries.size(), 1);
    EXPECT_EQ(entries[0].type, QStringLiteral("movie"));
    EXPECT_EQ(entries[0].season, 0);
    EXPECT_EQ(entries[0].episode, 0);
    EXPECT_EQ(entries[0].state, StreamDownloadIndex::Entry::Pending);
}
