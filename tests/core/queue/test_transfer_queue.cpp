#include <gtest/gtest.h>
#include "core/queue/TransferQueue.h"

using namespace tankoban::queue;

namespace {
TransferItem makeItem(const QString& tid, const QString& show, int ep = -1) {
    TransferItem it;
    it.transferId = tid;
    it.showId = show;
    it.displayTitle = tid;
    if (ep >= 0) it.episodeNumber = ep;
    return it;
}
}  // namespace

TEST(TransferQueueTest, EnqueueFirstItemReturnsZero) {
    TransferQueue q;
    EXPECT_EQ(q.enqueue(makeItem("t1", "imdb:tt0001", 1)), 0);
}

TEST(TransferQueueTest, EnqueueSecondItemSameShowReturnsOne) {
    TransferQueue q;
    q.enqueue(makeItem("t1", "imdb:tt0001", 1));
    EXPECT_EQ(q.enqueue(makeItem("t2", "imdb:tt0001", 2)), 1);
}

TEST(TransferQueueTest, EnqueueDifferentShowsBothReturnZero) {
    TransferQueue q;
    EXPECT_EQ(q.enqueue(makeItem("t1", "imdb:tt0001", 1)), 0);
    EXPECT_EQ(q.enqueue(makeItem("t2", "imdb:tt0002", 1)), 0);
}

TEST(TransferQueueTest, FinishCurrentReturnsNextQueued) {
    TransferQueue q;
    q.enqueue(makeItem("t1", "imdb:tt0001", 1));
    q.enqueue(makeItem("t2", "imdb:tt0001", 2));
    auto next = q.finishCurrent("imdb:tt0001", TransferState::Completed);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->transferId, "t2");
}

TEST(TransferQueueTest, FinishCurrentOnEmptyLaneReturnsNullopt) {
    TransferQueue q;
    auto next = q.finishCurrent("imdb:nonexistent", TransferState::Completed);
    EXPECT_FALSE(next.has_value());
}

TEST(TransferQueueTest, FinishLastItemEmptiesLane) {
    TransferQueue q;
    q.enqueue(makeItem("t1", "imdb:tt0001", 1));
    q.finishCurrent("imdb:tt0001", TransferState::Completed);
    EXPECT_FALSE(q.laneFor("imdb:tt0001").has_value());
}

TEST(TransferQueueTest, PauseCurrentMarksPausedButLaneDoesNotAdvance) {
    TransferQueue q;
    q.enqueue(makeItem("t1", "imdb:tt0001", 1));
    q.enqueue(makeItem("t2", "imdb:tt0001", 2));
    ASSERT_TRUE(q.pauseCurrent("imdb:tt0001"));
    auto lane = q.laneFor("imdb:tt0001");
    ASSERT_TRUE(lane.has_value());
    EXPECT_EQ(lane->items.size(), 2u);
    EXPECT_EQ(lane->items.front().state, TransferState::Paused);
}

TEST(TransferQueueTest, ResumeCurrentReturnsItemAndMarksRunning) {
    TransferQueue q;
    q.enqueue(makeItem("t1", "imdb:tt0001", 1));
    q.pauseCurrent("imdb:tt0001");
    auto resumed = q.resumeCurrent("imdb:tt0001");
    ASSERT_TRUE(resumed.has_value());
    EXPECT_EQ(resumed->transferId, "t1");
    EXPECT_EQ(q.laneFor("imdb:tt0001")->items.front().state, TransferState::Running);
}

TEST(TransferQueueTest, CancelQueuedItemRemovesWithoutAdvancing) {
    TransferQueue q;
    q.enqueue(makeItem("t1", "imdb:tt0001", 1));
    q.enqueue(makeItem("t2", "imdb:tt0001", 2));
    q.enqueue(makeItem("t3", "imdb:tt0001", 3));
    std::optional<TransferItem> next;
    ASSERT_TRUE(q.cancel("t2", &next));
    EXPECT_FALSE(next.has_value());  // current (t1) unchanged
    EXPECT_EQ(q.laneFor("imdb:tt0001")->items.size(), 2u);
    EXPECT_EQ(q.laneFor("imdb:tt0001")->items[1].transferId, "t3");
}

TEST(TransferQueueTest, CancelCurrentItemAdvancesLane) {
    TransferQueue q;
    q.enqueue(makeItem("t1", "imdb:tt0001", 1));
    q.enqueue(makeItem("t2", "imdb:tt0001", 2));
    std::optional<TransferItem> next;
    ASSERT_TRUE(q.cancel("t1", &next));
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->transferId, "t2");
}

TEST(TransferQueueTest, CancelUnknownIdReturnsFalse) {
    TransferQueue q;
    q.enqueue(makeItem("t1", "imdb:tt0001", 1));
    std::optional<TransferItem> next;
    EXPECT_FALSE(q.cancel("ghost", &next));
}
