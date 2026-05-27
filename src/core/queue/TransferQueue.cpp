#include "TransferQueue.h"

namespace tankoban::queue {

TransferQueue::TransferQueue(QObject* parent) : QObject(parent) {}

int TransferQueue::enqueue(const TransferItem&) { return -1; }
std::optional<TransferItem> TransferQueue::finishCurrent(const QString&, TransferState) { return std::nullopt; }
bool TransferQueue::pauseCurrent(const QString&) { return false; }
std::optional<TransferItem> TransferQueue::resumeCurrent(const QString&) { return std::nullopt; }
bool TransferQueue::cancel(const QString&, std::optional<TransferItem>*) { return false; }
bool TransferQueue::reorder(const QString&, int, int) { return false; }
bool TransferQueue::bumpToFront(const QString&) { return false; }
QHash<QString, TransferLane> TransferQueue::lanesSnapshot() const { return m_lanes; }
std::optional<TransferLane> TransferQueue::laneFor(const QString&) const { return std::nullopt; }

}  // namespace tankoban::queue
