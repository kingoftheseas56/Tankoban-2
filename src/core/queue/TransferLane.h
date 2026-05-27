#pragma once
#include "TransferItem.h"
#include <QString>
#include <vector>

namespace tankoban::queue {

// One lane per show. Items run strictly sequential inside a lane.
// Lanes across different shows run in parallel.
// index 0 is the current item (running or paused); rest are queued.
struct TransferLane {
    QString showId;                       // "" means standalone (one-item lane)
    std::vector<TransferItem> items;
};

}  // namespace tankoban::queue
