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

bool TransferQueue::pauseCurrent(const QString&) { return false; }
std::optional<TransferItem> TransferQueue::resumeCurrent(const QString&) { return std::nullopt; }
bool TransferQueue::cancel(const QString&, std::optional<TransferItem>*) { return false; }
bool TransferQueue::reorder(const QString&, int, int) { return false; }
bool TransferQueue::bumpToFront(const QString&) { return false; }
QHash<QString, TransferLane> TransferQueue::lanesSnapshot() const { return m_lanes; }
std::optional<TransferLane> TransferQueue::laneFor(const QString& showId) const {
    auto it = m_lanes.find(showId);
    if (it == m_lanes.end()) return std::nullopt;
    return *it;
}

}  // namespace tankoban::queue
