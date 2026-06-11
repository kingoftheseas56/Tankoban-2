#include <gtest/gtest.h>
#include "core/stream/DownloadsCommandModel.h"

using namespace tankostream::stream;
using tankoban::queue::TransferItem;
using tankoban::queue::TransferLane;
using tankoban::queue::TransferState;

namespace {
StreamDownloadIndex::Entry entry(const QString& imdb, int s, int e,
                                 StreamDownloadIndex::Entry::State st, int pct,
                                 qint64 addedAt = 1000) {
    StreamDownloadIndex::Entry x;
    x.imdbId = imdb; x.type = "series"; x.season = s; x.episode = e;
    x.state = st; x.progressPct = pct; x.addedAt = addedAt;
    x.canonicalPath = "C:/v/" + imdb + ".mkv";
    return x;
}
TransferLane lane(const QString& imdb, TransferState headState, int s, int e,
                  const QString& hash) {
    TransferLane l; l.showId = "imdb:" + imdb;
    TransferItem it; it.transferId = hash; it.showId = l.showId;
    it.seasonNumber = s; it.episodeNumber = e; it.state = headState;
    l.items.push_back(it);
    return l;
}
}  // namespace

TEST(DownloadsCommandModelTest, DownloadingEntryWithRunningLaneIsActive) {
    DownloadsSnapshot snap;
    snap.indexEntries = { entry("tt1", 1, 12, StreamDownloadIndex::Entry::Downloading, 62) };
    snap.lanes.insert("imdb:tt1", lane("tt1", TransferState::Running, 1, 12, "h1"));
    const auto rows = buildDownloadRows(snap, /*nowMs=*/0, /*trim=*/0);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].section, DownloadSection::Active);
    EXPECT_EQ(rows[0].pct, 62);
    EXPECT_EQ(rows[0].infoHash, "h1");
}

TEST(DownloadsCommandModelTest, FailedLaneItemIsFailedSection) {
    DownloadsSnapshot snap;
    snap.indexEntries = { entry("tt1", 1, 12, StreamDownloadIndex::Entry::Downloading, 30) };
    snap.lanes.insert("imdb:tt1", lane("tt1", TransferState::Failed, 1, 12, "h1"));
    const auto rows = buildDownloadRows(snap, 0, 0);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].section, DownloadSection::Failed);
}

TEST(DownloadsCommandModelTest, PendingWithQueuedLaneIsQueued) {
    DownloadsSnapshot snap;
    snap.indexEntries = { entry("tt1", 1, 13, StreamDownloadIndex::Entry::Pending, 0) };
    snap.lanes.insert("imdb:tt1", lane("tt1", TransferState::Queued, 1, 13, "h2"));
    const auto rows = buildDownloadRows(snap, 0, 0);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].section, DownloadSection::Queued);
}

TEST(DownloadsCommandModelTest, PausedLaneItemIsActivePaused) {
    DownloadsSnapshot snap;
    snap.indexEntries = { entry("tt1", 1, 12, StreamDownloadIndex::Entry::Downloading, 45) };
    snap.lanes.insert("imdb:tt1", lane("tt1", TransferState::Paused, 1, 12, "h1"));
    const auto rows = buildDownloadRows(snap, 0, 0);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].section, DownloadSection::Active);
    EXPECT_TRUE(rows[0].paused);
}

TEST(DownloadsCommandModelTest, CompleteIsCompletedAndTrims) {
    DownloadsSnapshot snap;
    snap.indexEntries = {
        entry("tt1", 1, 11, StreamDownloadIndex::Entry::Complete, 100, /*addedAt=*/1000),
        entry("tt1", 1, 10, StreamDownloadIndex::Entry::Complete, 100, /*addedAt=*/100),
    };
    // now=1400, trim=500 -> age(ep10)=1400-100=1300 > 500 -> dropped;
    // age(ep11)=1400-1000=400 <= 500 -> kept
    const auto rows = buildDownloadRows(snap, 1400, 500);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].episode, 11);
    EXPECT_EQ(rows[0].section, DownloadSection::Completed);
}

TEST(DownloadsCommandModelTest, SeasonPackLaneItemMatchesAnyEpisode) {
    DownloadsSnapshot snap;
    snap.indexEntries = {
        entry("tt1", 1, 3, StreamDownloadIndex::Entry::Downloading, 20),
        entry("tt1", 1, 4, StreamDownloadIndex::Entry::Downloading, 10),
    };
    TransferLane l; l.showId = "imdb:tt1";
    TransferItem pack; pack.transferId = "packhash"; pack.showId = l.showId;
    pack.seasonNumber = 1;   // no episodeNumber -> season pack
    pack.state = TransferState::Running;
    l.items.push_back(pack);
    snap.lanes.insert("imdb:tt1", l);
    const auto rows = buildDownloadRows(snap, 0, 0);
    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows[0].infoHash, "packhash");
    EXPECT_EQ(rows[1].infoHash, "packhash");
    EXPECT_EQ(rows[0].section, DownloadSection::Active);
}

// Review C1 — failure normally arrives via the INDEX: TransferQueue erases
// items on terminal states, so lanes never carry Failed in production.
TEST(DownloadsCommandModelTest, IndexFailedWithNoLaneIsFailedSection) {
    DownloadsSnapshot snap;
    snap.indexEntries = { entry("tt1", 1, 12, StreamDownloadIndex::Entry::Failed, 30) };
    const auto rows = buildDownloadRows(snap, 0, 0);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].section, DownloadSection::Failed);
    EXPECT_TRUE(rows[0].infoHash.isEmpty());
}

// Review C1 ordering — a retry re-queues a lane item while the index still
// says Failed; the lane state must win so the row shows Queued, not Failed.
TEST(DownloadsCommandModelTest, IndexFailedWithQueuedLaneIsQueued) {
    DownloadsSnapshot snap;
    snap.indexEntries = { entry("tt1", 1, 12, StreamDownloadIndex::Entry::Failed, 30) };
    snap.lanes.insert("imdb:tt1", lane("tt1", TransferState::Queued, 1, 12, "h9"));
    const auto rows = buildDownloadRows(snap, 0, 0);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].section, DownloadSection::Queued);
    EXPECT_EQ(rows[0].infoHash, "h9");
}

// Review I1 (plan-owner decision, pinned): index Downloading with NO lane item
// is the app-restart shape — resumed torrents download with an empty queue.
// The transfer genuinely runs in the engine (progress keeps flowing via
// updateEpisodeProgress), so the row stays Active despite the empty infoHash.
TEST(DownloadsCommandModelTest, DownloadingWithNoLaneIsActiveOrphan) {
    DownloadsSnapshot snap;
    snap.indexEntries = { entry("tt1", 1, 12, StreamDownloadIndex::Entry::Downloading, 40) };
    const auto rows = buildDownloadRows(snap, 0, 0);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].section, DownloadSection::Active);
    EXPECT_TRUE(rows[0].infoHash.isEmpty());
}

// Movie rows (season 0, episode 0) match a lane item with nullopt season/
// episode through the same no-episodeNumber path season packs use.
TEST(DownloadsCommandModelTest, MovieRowMatchesLaneItemWithoutSeasonEpisode) {
    DownloadsSnapshot snap;
    StreamDownloadIndex::Entry m = entry("tt1", 0, 0, StreamDownloadIndex::Entry::Downloading, 55);
    m.type = "movie";
    snap.indexEntries = { m };
    TransferLane l; l.showId = "imdb:tt1";
    TransferItem it; it.transferId = "mh1"; it.showId = l.showId;
    it.state = TransferState::Running;   // no seasonNumber, no episodeNumber
    l.items.push_back(it);
    snap.lanes.insert("imdb:tt1", l);
    const auto rows = buildDownloadRows(snap, 0, 0);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].infoHash, "mh1");
    EXPECT_EQ(rows[0].section, DownloadSection::Active);
}

// Fallback pinned: Pending with no lane item at all (lane not visible yet,
// e.g. enqueue raced the snapshot) -> Queued.
TEST(DownloadsCommandModelTest, PendingWithNoLaneIsQueued) {
    DownloadsSnapshot snap;
    snap.indexEntries = { entry("tt1", 1, 13, StreamDownloadIndex::Entry::Pending, 0) };
    const auto rows = buildDownloadRows(snap, 0, 0);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].section, DownloadSection::Queued);
}

// Review C2/I1 — rows carry the index entry's sourceGroupId so the page can
// evict ghost rows and derive an engine hash when the lane item is gone.
// infoHashFromGroup honors the "tankorent:<lowercase-infohash>" convention
// stamped at the TorrentClient registration sites.
TEST(DownloadsCommandModelTest, RowCarriesSourceGroupIdAndHashRoundTrips) {
    DownloadsSnapshot snap;
    auto e = entry("tt1", 1, 12, StreamDownloadIndex::Entry::Failed, 30);
    e.sourceGroupId = "tankorent:abcdef0123456789abcdef0123456789abcdef01";
    snap.indexEntries = { e };
    const auto rows = buildDownloadRows(snap, 0, 0);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].sourceGroupId, e.sourceGroupId);
    EXPECT_EQ(infoHashFromGroup(rows[0].sourceGroupId),
              "abcdef0123456789abcdef0123456789abcdef01");
    EXPECT_TRUE(infoHashFromGroup(QString()).isEmpty());
    EXPECT_TRUE(infoHashFromGroup(QStringLiteral("getcomics:xyz")).isEmpty());
}

TEST(DownloadsCommandModelTest, SectionOrderThenShowSeasonEpisode) {
    DownloadsSnapshot snap;
    snap.indexEntries = {
        entry("tt2", 1, 1, StreamDownloadIndex::Entry::Complete, 100),
        entry("tt1", 1, 2, StreamDownloadIndex::Entry::Downloading, 10),
        entry("tt1", 1, 1, StreamDownloadIndex::Entry::Downloading, 50),
    };
    snap.lanes.insert("imdb:tt1", lane("tt1", TransferState::Running, 1, 1, "h1"));
    const auto rows = buildDownloadRows(snap, 0, 0);
    ASSERT_EQ(rows.size(), 3);
    EXPECT_EQ(rows[0].section, DownloadSection::Active);
    EXPECT_EQ(rows[0].episode, 1);
    EXPECT_EQ(rows[1].section, DownloadSection::Active);
    EXPECT_EQ(rows[2].section, DownloadSection::Completed);
}
