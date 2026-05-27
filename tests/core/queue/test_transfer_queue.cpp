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
