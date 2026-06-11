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

// THEATRE_DOWNLOAD_SIMPLIFY (2026-05-30) — duplicate-resolution: when an episode
// has both a stale Pending (.tankoban-partial) entry and the final Complete
// entry, pickBestEntry must return Complete (else the UI paints "Queued" over a
// downloaded episode). Pure-logic, no repo needed.
static StreamDownloadIndex::Entry mkPickEntry(StreamDownloadIndex::Entry::State state,
                                              int progressPct, qint64 addedAt,
                                              const QString& path)
{
    StreamDownloadIndex::Entry e;
    e.imdbId = QStringLiteral("tt3322312");
    e.season = 2;
    e.episode = 4;
    e.canonicalPath = path;
    e.state = state;
    e.progressPct = progressPct;
    e.addedAt = addedAt;
    return e;
}

TEST(StreamDownloadIndexPickBest, PrefersCompleteOverStalePending)
{
    QList<StreamDownloadIndex::Entry> cands {
        mkPickEntry(StreamDownloadIndex::Entry::Pending, 0, 100,
                    QStringLiteral("C:/dl/.tankoban-partial/x/Daredevil.S02E04.mkv")),
        mkPickEntry(StreamDownloadIndex::Entry::Complete, 100, 200,
                    QStringLiteral("C:/Media/TV/Daredevil/Daredevil.S02E04.mkv")),
    };
    const auto best = StreamDownloadIndex::pickBestEntry(cands);
    ASSERT_TRUE(best.has_value());
    EXPECT_EQ(best->state, StreamDownloadIndex::Entry::Complete);
}

TEST(StreamDownloadIndexPickBest, OrderIndependentForCompleteVsPending)
{
    QList<StreamDownloadIndex::Entry> cands {
        mkPickEntry(StreamDownloadIndex::Entry::Complete, 100, 200,
                    QStringLiteral("C:/Media/TV/Daredevil/Daredevil.S02E04.mkv")),
        mkPickEntry(StreamDownloadIndex::Entry::Pending, 0, 100,
                    QStringLiteral("C:/dl/.tankoban-partial/x/Daredevil.S02E04.mkv")),
    };
    const auto best = StreamDownloadIndex::pickBestEntry(cands);
    ASSERT_TRUE(best.has_value());
    EXPECT_EQ(best->state, StreamDownloadIndex::Entry::Complete);
}

TEST(StreamDownloadIndexPickBest, DownloadingBeatsPendingTieByProgress)
{
    QList<StreamDownloadIndex::Entry> cands {
        mkPickEntry(StreamDownloadIndex::Entry::Pending, 0, 300, QStringLiteral("a")),
        mkPickEntry(StreamDownloadIndex::Entry::Downloading, 40, 100, QStringLiteral("b")),
        mkPickEntry(StreamDownloadIndex::Entry::Downloading, 70, 100, QStringLiteral("c")),
    };
    const auto best = StreamDownloadIndex::pickBestEntry(cands);
    ASSERT_TRUE(best.has_value());
    EXPECT_EQ(best->state, StreamDownloadIndex::Entry::Downloading);
    EXPECT_EQ(best->progressPct, 70);
}

TEST(StreamDownloadIndexPickBest, EmptyCandidatesReturnsNullopt)
{
    EXPECT_FALSE(StreamDownloadIndex::pickBestEntry({}).has_value());
}

// ── DOWNLOADS_OVERHAUL_V2 T3.2 — markFailedByGroup ──────────────────────────

TEST_F(StreamDownloadIndexStateTest, MarkFailedByGroup_FlipsPendingAndDownloading)
{
    // Register one Pending + one Downloading (simulated via progress update).
    m_index->registerPendingEpisode(
        QStringLiteral("tt1"), 1, 1,
        QStringLiteral("C:/dl/S01E01.mkv"),
        QStringLiteral("tankorent:aabbcc"), 1500000000LL);
    m_index->registerPendingEpisode(
        QStringLiteral("tt1"), 1, 2,
        QStringLiteral("C:/dl/S01E02.mkv"),
        QStringLiteral("tankorent:aabbcc"), 1500000000LL);
    m_index->updateEpisodeProgress(QStringLiteral("tt1"), 1, 2, 45);  // → Downloading

    QSignalSpy stateSpy(m_index.get(), &StreamDownloadIndex::entryStateChanged);
    QSignalSpy entriesSpy(m_index.get(), &StreamDownloadIndex::entriesChanged);

    m_index->markFailedByGroup(QStringLiteral("tankorent:aabbcc"));

    const auto entries = m_index->entriesForImdb(QStringLiteral("tt1"));
    ASSERT_EQ(entries.size(), 2);
    for (const auto& e : entries)
        EXPECT_EQ(e.state, StreamDownloadIndex::Entry::Failed);

    // entryStateChanged fires once per changed entry; entriesChanged fires once.
    EXPECT_EQ(stateSpy.count(), 2);
    EXPECT_GE(entriesSpy.count(), 1);
}

TEST_F(StreamDownloadIndexStateTest, MarkFailedByGroup_DoesNotDowngradeComplete)
{
    // Register an episode as Pending then flip it to Complete via progress=100.
    m_index->registerPendingEpisode(
        QStringLiteral("tt2"), 1, 1,
        QStringLiteral("C:/dl/tt2/S01E01.mkv"),
        QStringLiteral("tankorent:ddeeff"), 1500000000LL);
    m_index->updateEpisodeProgress(QStringLiteral("tt2"), 1, 1, 100);  // → Complete

    m_index->markFailedByGroup(QStringLiteral("tankorent:ddeeff"));

    const auto entries = m_index->entriesForImdb(QStringLiteral("tt2"));
    ASSERT_EQ(entries.size(), 1);
    EXPECT_EQ(entries[0].state, StreamDownloadIndex::Entry::Complete);
}

TEST_F(StreamDownloadIndexStateTest, MarkFailedByGroup_LeavesOtherGroupsUntouched)
{
    m_index->registerPendingEpisode(
        QStringLiteral("tt3"), 1, 1,
        QStringLiteral("C:/dl/tt3/S01E01.mkv"),
        QStringLiteral("tankorent:group1"), 1500000000LL);
    m_index->registerPendingEpisode(
        QStringLiteral("tt4"), 2, 3,
        QStringLiteral("C:/dl/tt4/S02E03.mkv"),
        QStringLiteral("tankorent:group2"), 1500000000LL);

    m_index->markFailedByGroup(QStringLiteral("tankorent:group1"));

    // Only group1's entry is Failed.
    const auto e3 = m_index->entriesForImdb(QStringLiteral("tt3"));
    ASSERT_EQ(e3.size(), 1);
    EXPECT_EQ(e3[0].state, StreamDownloadIndex::Entry::Failed);

    // group2's entry is untouched (still Pending).
    const auto e4 = m_index->entriesForImdb(QStringLiteral("tt4"));
    ASSERT_EQ(e4.size(), 1);
    EXPECT_EQ(e4[0].state, StreamDownloadIndex::Entry::Pending);
}
