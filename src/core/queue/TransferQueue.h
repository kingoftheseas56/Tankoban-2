#pragma once
#include "TransferLane.h"
#include <QObject>
#include <QHash>
#include <QString>
#include <QStringList>
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
    // (0 = lane head). Under a max-active cap the head may still be Queued —
    // consumers must check the item state (or react to itemStateChanged(Running)),
    // never assume position 0 means Running.
    int enqueue(const TransferItem& item);

    // Marks the currently-running item in showId's lane as finished
    // (Completed or Failed). Returns the lane's new head if it was promoted to
    // Running by the freed slot; the freed slot may instead promote an older
    // waiter in ANOTHER lane (global FIFO), in which case nullopt-or-still-Queued
    // semantics apply. Returns std::nullopt if the lane is now empty.
    std::optional<TransferItem> finishCurrent(const QString& showId, TransferState finalState);

    // Pauses the currently-running item in showId's lane. Lane does NOT
    // advance. Returns true if a paused item exists.
    bool pauseCurrent(const QString& showId);

    // Resumes a paused current item. Returns the item to resume, or nullopt.
    std::optional<TransferItem> resumeCurrent(const QString& showId);

    // Pause every Running lane head in one pass WITHOUT promoting waiters —
    // "Pause All" must not start new downloads (T7 review C1). Returns the
    // transferIds that were flipped so the caller can engine-pause them.
    // Emits itemStateChanged(Paused) per flip + laneChanged per affected lane.
    QStringList pauseAll();

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
    // immediately promotes eligible waiters, oldest enqueue first. Lowering
    // the cap below the current running count never demotes in-flight items —
    // it only gates future promotions.
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
