#include "TransferQueue.h"

namespace tankoban::queue {

TransferQueue::TransferQueue(QObject* parent) : QObject(parent) {}

int TransferQueue::enqueue(const TransferItem& item) {
    auto& lane = m_lanes[item.showId];
    lane.showId = item.showId;
    lane.items.push_back(item);
    const int pos = static_cast<int>(lane.items.size()) - 1;
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
        return std::nullopt;
    }
    emit laneChanged(showId);
    return it->items.front();
}

bool TransferQueue::pauseCurrent(const QString& showId) {
    auto it = m_lanes.find(showId);
    if (it == m_lanes.end() || it->items.empty()) return false;
    it->items.front().state = TransferState::Paused;
    emit itemStateChanged(it->items.front().transferId, TransferState::Paused);
    emit laneChanged(showId);
    return true;
}

std::optional<TransferItem> TransferQueue::resumeCurrent(const QString& showId) {
    auto it = m_lanes.find(showId);
    if (it == m_lanes.end() || it->items.empty()) return std::nullopt;
    if (it->items.front().state != TransferState::Paused) return std::nullopt;
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
                if (wasCurrent && !items.empty() && nextAfterCancel) {
                    *nextAfterCancel = items.front();
                }
                if (items.empty()) {
                    m_lanes.erase(laneIt);
                }
                emit laneChanged(showId);
                return true;
            }
        }
    }
    return false;
}
bool TransferQueue::reorder(const QString&, int, int) { return false; }
bool TransferQueue::bumpToFront(const QString&) { return false; }
QHash<QString, TransferLane> TransferQueue::lanesSnapshot() const { return m_lanes; }
std::optional<TransferLane> TransferQueue::laneFor(const QString& showId) const {
    auto it = m_lanes.find(showId);
    if (it == m_lanes.end()) return std::nullopt;
    return *it;
}

}  // namespace tankoban::queue
