#include <gtest/gtest.h>
#include "core/queue/TransferQueue.h"
#include <QList>
#include <QPair>

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
    auto lane = q.laneFor("imdb:tt0001");
    ASSERT_TRUE(lane.has_value());
    EXPECT_EQ(lane->items.front().state, TransferState::Running);
}

TEST(TransferQueueTest, EnqueueSecondItemSameShowReturnsOne) {
    TransferQueue q;
    q.enqueue(makeItem("t1", "imdb:tt0001", 1));
    EXPECT_EQ(q.enqueue(makeItem("t2", "imdb:tt0001", 2)), 1);
    auto lane = q.laneFor("imdb:tt0001");
    ASSERT_TRUE(lane.has_value());
    ASSERT_EQ(lane->items.size(), 2u);
    EXPECT_EQ(lane->items[0].state, TransferState::Running);
    EXPECT_EQ(lane->items[1].state, TransferState::Queued);
}

TEST(TransferQueueTest, EnqueueDifferentShowsBothReturnZero) {
    TransferQueue q;
    EXPECT_EQ(q.enqueue(makeItem("t1", "imdb:tt0001", 1)), 0);
    EXPECT_EQ(q.enqueue(makeItem("t2", "imdb:tt0002", 1)), 0);
    EXPECT_EQ(q.laneFor("imdb:tt0001")->items.front().state, TransferState::Running);
    EXPECT_EQ(q.laneFor("imdb:tt0002")->items.front().state, TransferState::Running);
}

TEST(TransferQueueTest, FinishCurrentReturnsNextQueued) {
    TransferQueue q;
    QList<QPair<QString, TransferState>> stateChanges;
    QObject::connect(&q, &TransferQueue::itemStateChanged,
                     [&stateChanges](const QString& id, TransferState state) {
                         stateChanges.append({id, state});
                     });
    q.enqueue(makeItem("t1", "imdb:tt0001", 1));
    q.enqueue(makeItem("t2", "imdb:tt0001", 2));
    auto next = q.finishCurrent("imdb:tt0001", TransferState::Completed);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->transferId, "t2");
    EXPECT_EQ(next->state, TransferState::Running);
    ASSERT_GE(stateChanges.size(), 3);
    EXPECT_EQ(stateChanges.at(stateChanges.size() - 2).first, "t1");
    EXPECT_EQ(stateChanges.at(stateChanges.size() - 2).second, TransferState::Completed);
    EXPECT_EQ(stateChanges.last().first, "t2");
    EXPECT_EQ(stateChanges.last().second, TransferState::Running);
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
    EXPECT_EQ(next->state, TransferState::Running);
}

TEST(TransferQueueTest, CancelUnknownIdReturnsFalse) {
    TransferQueue q;
    q.enqueue(makeItem("t1", "imdb:tt0001", 1));
    std::optional<TransferItem> next;
    EXPECT_FALSE(q.cancel("ghost", &next));
}

TEST(TransferQueueTest, ReorderMovesQueuedItem) {
    TransferQueue q;
    q.enqueue(makeItem("t1", "S", 1));  // current
    q.enqueue(makeItem("t2", "S", 2));
    q.enqueue(makeItem("t3", "S", 3));
    q.enqueue(makeItem("t4", "S", 4));
    ASSERT_TRUE(q.reorder("S", 3, 1));  // move t4 to position 1
    auto lane = q.laneFor("S");
    EXPECT_EQ(lane->items[0].transferId, "t1");
    EXPECT_EQ(lane->items[1].transferId, "t4");
    EXPECT_EQ(lane->items[2].transferId, "t2");
    EXPECT_EQ(lane->items[3].transferId, "t3");
}

TEST(TransferQueueTest, ReorderCannotMoveCurrent) {
    TransferQueue q;
    q.enqueue(makeItem("t1", "S", 1));
    q.enqueue(makeItem("t2", "S", 2));
    EXPECT_FALSE(q.reorder("S", 0, 1));
}

TEST(TransferQueueTest, ReorderCannotTargetCurrentSlot) {
    TransferQueue q;
    q.enqueue(makeItem("t1", "S", 1));
    q.enqueue(makeItem("t2", "S", 2));
    EXPECT_FALSE(q.reorder("S", 1, 0));
}

TEST(TransferQueueTest, BumpToFrontMovesQueuedToPositionOne) {
    TransferQueue q;
    q.enqueue(makeItem("t1", "S", 1));
    q.enqueue(makeItem("t2", "S", 2));
    q.enqueue(makeItem("t3", "S", 3));
    q.enqueue(makeItem("t4", "S", 4));
    ASSERT_TRUE(q.bumpToFront("t4"));
    auto lane = q.laneFor("S");
    EXPECT_EQ(lane->items[0].transferId, "t1");  // current unchanged
    EXPECT_EQ(lane->items[1].transferId, "t4");  // bumped
}

TEST(TransferQueueTest, BumpCurrentIsNoop) {
    TransferQueue q;
    q.enqueue(makeItem("t1", "S", 1));
    EXPECT_FALSE(q.bumpToFront("t1"));
}
