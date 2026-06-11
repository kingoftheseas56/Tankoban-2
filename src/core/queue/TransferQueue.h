#pragma once
#include "TransferLane.h"
#include <QObject>
#include <QHash>
#include <QString>
#include <optional>

namespace tankoban::queue {

// Per-show lane registry. enqueue() routes a TransferItem to its show's lane
// (creating the lane if needed). Inside a lane: strictly sequential. Across
// lanes: parallel. The owning subsystem (TorrentClient, StreamServerClient)
// calls beginNext()/finishCurrent() to drive actual transfer start/stop —
// TransferQueue itself does not touch libtorrent or the stream server.
class TransferQueue : public QObject {
    Q_OBJECT
public:
    explicit TransferQueue(QObject* parent = nullptr);

    // Adds an item to its show's lane. Returns the position in the lane
    // (0 = will run immediately if lane was empty).
    int enqueue(const TransferItem& item);

    // Marks the currently-running item in showId's lane as finished
    // (Completed or Failed). Returns the next item to start (lane index 0
    // after advance), or std::nullopt if the lane is now empty.
    std::optional<TransferItem> finishCurrent(const QString& showId, TransferState finalState);

    // Pauses the currently-running item in showId's lane. Lane does NOT
    // advance. Returns true if a paused item exists.
    bool pauseCurrent(const QString& showId);

    // Resumes a paused current item. Returns the item to resume, or nullopt.
    std::optional<TransferItem> resumeCurrent(const QString& showId);

    // Removes a queued item by transferId. If the removed item was current,
    // the lane advances to the next queued item (returned via nextAfterCancel
    // out-param). Returns true if found.
    bool cancel(const QString& transferId, std::optional<TransferItem>* nextAfterCancel = nullptr);

    // Moves a queued item from oldIdx to newIdx within its lane. Current
    // (index 0) cannot be reordered. Returns true on success.
    bool reorder(const QString& showId, int oldIdx, int newIdx);

    // Promotes a queued item to lane position 1 (right after current).
    // Returns true if the item was queued and moved.
    bool bumpToFront(const QString& transferId);

    // Read-only access to lanes for UI rendering.
    QHash<QString, TransferLane> lanesSnapshot() const;
    std::optional<TransferLane> laneFor(const QString& showId) const;

    // Global cap on simultaneously-Running items across ALL lanes.
    // 0 = unlimited (default; preserves pre-cap behavior). Raising the cap
    // immediately promotes eligible waiters, oldest enqueue first.
    void setMaxActive(int n);
    int  maxActive() const { return m_maxActive; }
    int  runningCount() const;

signals:
    void laneChanged(const QString& showId);
    void itemStateChanged(const QString& transferId, TransferState newState);

private:
    bool canPromote() const;        // running < cap (or unlimited)
    void promoteOldestEligible();   // promote Queued lane-heads while slots free
    QHash<QString, TransferLane> m_lanes;
    int m_maxActive = 0;
    quint64 m_seqCounter = 0;
};

}  // namespace tankoban::queue
