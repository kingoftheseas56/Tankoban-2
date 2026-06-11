#pragma once
#include <QString>
#include <QtGlobal>
#include <optional>

namespace tankoban::queue {

enum class TransferState {
    Queued,
    Running,
    Paused,
    Cancelled,
    Completed,
    Failed,
};

// One queued or running transfer. transferId is unique (infohash for torrents,
// stream-server transfer id for stream-server downloads). showId is the lane
// key — empty means standalone (one-item lane, runs immediately).
struct TransferItem {
    QString transferId;
    QString showId;
    QString displayTitle;
    std::optional<int> episodeNumber;
    std::optional<int> seasonNumber;
    TransferState state = TransferState::Queued;
    quint64 enqueueSeq = 0;   // stamped by TransferQueue::enqueue; global FIFO fairness
};

}  // namespace tankoban::queue
