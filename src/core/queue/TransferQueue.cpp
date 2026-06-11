#include "TransferQueue.h"

namespace tankoban::queue {

TransferQueue::TransferQueue(QObject* parent) : QObject(parent) {}

int TransferQueue::enqueue(const TransferItem& item) {
    auto& lane = m_lanes[item.showId];
    lane.showId = item.showId;
    TransferItem queued = item;
    queued.enqueueSeq = ++m_seqCounter;
    const bool wasEmpty = lane.items.empty();
    if (wasEmpty && canPromote())
        queued.state = TransferState::Running;
    lane.items.push_back(queued);
    const int pos = static_cast<int>(lane.items.size()) - 1;
    if (pos == 0 && queued.state == TransferState::Running)
        emit itemStateChanged(queued.transferId, TransferState::Running);
    emit laneChanged(item.showId);
    return pos;
}

std::optional<TransferItem> TransferQueue::finishCurrent(const QString& showId, TransferState finalState) {
    auto it = m_lanes.find(showId);
    if (it == m_lanes.end() || it->items.empty()) return std::nullopt;

    const QString finishedId = it->items.front().transferId;
    it->items.front().state = finalState;
    emit itemStateChanged(finishedId, finalState);
    it->items.erase(it->items.begin());

    if (it->items.empty()) {
        m_lanes.erase(it);
        emit laneChanged(showId);
        promoteOldestEligible();
        return std::nullopt;
    }
    emit laneChanged(showId);
    promoteOldestEligible();
    // Return the lane's new front (may or may not be Running depending on cap).
    auto afterIt = m_lanes.find(showId);
    if (afterIt == m_lanes.end() || afterIt->items.empty()) return std::nullopt;
    return afterIt->items.front();
}

bool TransferQueue::pauseCurrent(const QString& showId) {
    auto it = m_lanes.find(showId);
    if (it == m_lanes.end() || it->items.empty()) return false;
    it->items.front().state = TransferState::Paused;
    emit itemStateChanged(it->items.front().transferId, TransferState::Paused);
    emit laneChanged(showId);
    promoteOldestEligible();
    return true;
}

std::optional<TransferItem> TransferQueue::resumeCurrent(const QString& showId) {
    auto it = m_lanes.find(showId);
    if (it == m_lanes.end() || it->items.empty()) return std::nullopt;
    if (it->items.front().state != TransferState::Paused) return std::nullopt;
    if (!canPromote()) {
        // No slot available — leave as Queued head; will be promoted when a slot frees.
        it->items.front().state = TransferState::Queued;
        emit laneChanged(showId);
        return std::nullopt;
    }
    it->items.front().state = TransferState::Running;
    emit itemStateChanged(it->items.front().transferId, TransferState::Running);
    emit laneChanged(showId);
    return it->items.front();
}

bool TransferQueue::cancel(const QString& transferId, std::optional<TransferItem>* nextAfterCancel) {
    if (nextAfterCancel) *nextAfterCancel = std::nullopt;
    for (auto laneIt = m_lanes.begin(); laneIt != m_lanes.end(); ++laneIt) {
        auto& items = laneIt->items;
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].transferId == transferId) {
                const bool wasCurrent = (i == 0);
                const QString showId = laneIt->showId;
                items.erase(items.begin() + i);
                emit itemStateChanged(transferId, TransferState::Cancelled);
                if (items.empty()) {
                    m_lanes.erase(laneIt);
                }
                emit laneChanged(showId);
                promoteOldestEligible();
                // Populate nextAfterCancel if caller asked and this lane still exists.
                if (wasCurrent && nextAfterCancel) {
                    auto afterIt = m_lanes.find(showId);
                    if (afterIt != m_lanes.end() && !afterIt->items.empty()
                        && afterIt->items.front().state == TransferState::Running)
                        *nextAfterCancel = afterIt->items.front();
                }
                return true;
            }
        }
    }
    return false;
}
bool TransferQueue::reorder(const QString& showId, int oldIdx, int newIdx) {
    auto it = m_lanes.find(showId);
    if (it == m_lanes.end()) return false;
    auto& items = it->items;
    const int n = static_cast<int>(items.size());
    if (oldIdx <= 0 || oldIdx >= n) return false;   // cannot move current
    if (newIdx <= 0 || newIdx >= n) return false;   // cannot target current
    if (oldIdx == newIdx) return false;
    TransferItem moved = items[oldIdx];
    items.erase(items.begin() + oldIdx);
    items.insert(items.begin() + newIdx, moved);
    emit laneChanged(showId);
    return true;
}

bool TransferQueue::bumpToFront(const QString& transferId) {
    for (auto laneIt = m_lanes.begin(); laneIt != m_lanes.end(); ++laneIt) {
        auto& items = laneIt->items;
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].transferId == transferId) {
                if (i == 0 || i == 1) return false;  // already current or already at pos 1
                TransferItem moved = items[i];
                items.erase(items.begin() + i);
                items.insert(items.begin() + 1, moved);
                emit laneChanged(laneIt->showId);
                return true;
            }
        }
    }
    return false;
}
QHash<QString, TransferLane> TransferQueue::lanesSnapshot() const { return m_lanes; }
std::optional<TransferLane> TransferQueue::laneFor(const QString& showId) const {
    auto it = m_lanes.find(showId);
    if (it == m_lanes.end()) return std::nullopt;
    return *it;
}

int TransferQueue::runningCount() const
{
    int n = 0;
    for (const auto& lane : m_lanes)
        if (!lane.items.empty() && lane.items.front().state == TransferState::Running)
            ++n;
    return n;
}

bool TransferQueue::canPromote() const
{
    return m_maxActive == 0 || runningCount() < m_maxActive;
}

void TransferQueue::promoteOldestEligible()
{
    while (canPromote()) {
        TransferLane* best = nullptr;
        for (auto it = m_lanes.begin(); it != m_lanes.end(); ++it) {
            TransferLane& lane = it.value();
            if (lane.items.empty()) continue;
            TransferItem& head = lane.items.front();
            if (head.state != TransferState::Queued) continue;
            if (!best || head.enqueueSeq < best->items.front().enqueueSeq)
                best = &lane;
        }
        if (!best) return;
        best->items.front().state = TransferState::Running;
        emit itemStateChanged(best->items.front().transferId, TransferState::Running);
        emit laneChanged(best->showId);
    }
}

void TransferQueue::setMaxActive(int n)
{
    m_maxActive = qMax(0, n);
    promoteOldestEligible();
}

}  // namespace tankoban::queue
