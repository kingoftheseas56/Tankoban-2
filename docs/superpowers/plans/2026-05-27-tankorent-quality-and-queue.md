# TANKORENT_QUALITY_AND_QUEUE Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Tankorent search faithful to source-site results, recognize season packs, run downloads per-show-sequentially across parallel show lanes, and surface all of it inside Theatre's series-view source sidebar with a Netflix-clean Downloads page.

**Architecture:** Six sequential phases. Phase 1 introduces a `TransferQueue` keyed by show ID (libtorrent's global `active_downloads=1` cap gets replaced with per-show lane discipline). Phase 2 strips Nyaa indexer filtering for source-mirror parity. Phase 3 makes Tankorent a source-addon inside Theatre's series-view Sources sidebar (Torrentio-style). Phase 4 adds title-heuristic season classification with badges and a filter chip. Phase 5 rebuilds the Theatre Downloads page Netflix-style around lanes. Phase 6 fans the Phase 2 parity pattern out to the other six indexers.

**Tech Stack:** Qt6 (Widgets + Core + Network + Sql), C++20, MSVC2022, libtorrent RC_2_0, GoogleTest (pure-logic primitives only), ninja build via `build_check.bat`.

**Scope guard:** Theatre only this arc. Comics + Books series-view Tankorent addon is a future arc owned by Agent 1 / Agent 2. Comics + Books integration into `TransferQueue` is a future arc by the owning agents — the queue interface ships public, they plug in on their own schedule.

**Discipline standing rules:**
- TDD applies to pure-logic primitives only (TransferQueue, SeasonClassifier). UI / IPC / libtorrent-integration code ships under code-walk verification + Hemanth smoke.
- Every task ends with `build_check.bat` BUILD OK before commit. Kill `Tankoban.exe` first (Rule 1).
- Per-task commits, RTC at end of phase per `feedback_commit_protocol.md`.

---

## File Structure

**New files (Phase 1 — lane queue):**
- `src/core/queue/TransferLane.h` — POD: lane key (show ID) + ordered vector of `TransferItem`.
- `src/core/queue/TransferItem.h` — POD: `transferId`, `showId`, `displayTitle`, `episodeNumber` (optional), state enum.
- `src/core/queue/TransferQueue.h` / `.cpp` — singleton-style service; enqueue / cancel / reorder / bump / pause-resume; emits per-lane state-change signals.
- `tests/core/queue/test_transfer_queue.cpp` — GoogleTest pure-logic suite.

**New files (Phase 2 — Nyaa parity):**
- `scripts/nyaa-parity-probe.ps1` — one-off harness comparing Nyaa.si row count vs `TankorentSearchService` count.

**New files (Phase 3 — Tankorent source-addon):**
- `src/ui/pages/stream/TankorentSourceAddon.h` / `.cpp` — addon widget hosted by `StreamDetailView`.

**New files (Phase 4 — season classifier):**
- `src/core/torrent/SeasonClassifier.h` / `.cpp` — pure-logic title parser → `MULTI_SEASON / SEASON_PACK / EPISODE / UNCLASSIFIED`.
- `tests/core/torrent/test_season_classifier.cpp` — GoogleTest suite with 50+ ground-truth cases.

**Modified files:**
- `src/core/torrent/TorrentClient.{cpp,h}` — wire add-torrent path through `TransferQueue` (Phase 1); detect pack episode order on add (Phase 4).
- `src/core/torrent/TorrentEngine.cpp` — revert `active_downloads=1` to default unlimited (Phase 1).
- `src/core/stream/stremio/StreamServerClient.{cpp,h}` — wire stream-server downloads through `TransferQueue` (Phase 1).
- `src/core/indexers/NyaaIndexer.{cpp,h}` — strip seeder threshold + trust filter (Phase 2).
- `src/core/TankorentSearchService.{cpp,h}` — preserve raw count, expose to UI; lose drop-filter (Phase 2).
- `src/ui/pages/stream/StreamDetailView.{cpp,h}` — host Tankorent addon panel (Phase 3).
- `src/ui/pages/stream/StreamDownloadsPage.{cpp,h}` — Netflix-revision: one card per show lane (Phase 5).
- `CMakeLists.txt` — register new source files + GoogleTest entries.
- `src/core/indexers/{ExtTorrentsIndexer,EztvIndexer,PirateBayIndexer,TorrentsCsvIndexer,X1337xIndexer,YtsIndexer}.{cpp,h}` — same parity pattern (Phase 6).

---

## Phase 1: Per-show lane queue infrastructure

**Context the engineer needs:** libtorrent's session currently caps to `active_downloads=1` (TorrentEngine.cpp:380). This is a GLOBAL cap shipped as `SEQUENTIAL_DOWNLOADS_FIX T1` on 2026-05-21. The new model is per-show lanes: parallel across shows, sequential inside each show. The libtorrent cap must come off; `TransferQueue` becomes the gate.

### Task 1.1: `TransferItem` + `TransferLane` PODs

**Files:**
- Create: `src/core/queue/TransferItem.h`
- Create: `src/core/queue/TransferLane.h`
- Modify: `CMakeLists.txt` (add to Tankoban sources list)

- [ ] **Step 1: Create `TransferItem.h`**

```cpp
// src/core/queue/TransferItem.h
#pragma once
#include <QString>
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

struct TransferItem {
    QString transferId;           // unique per add (infohash for torrents)
    QString showId;               // lane key (imdb:tt..., anilist:N, book:..., or "" for standalone)
    QString displayTitle;         // shown on cards (raw torrent title preserved separately)
    std::optional<int> episodeNumber;  // for pack-internal ordering
    std::optional<int> seasonNumber;
    TransferState state = TransferState::Queued;
};

}  // namespace tankoban::queue
```

- [ ] **Step 2: Create `TransferLane.h`**

```cpp
// src/core/queue/TransferLane.h
#pragma once
#include "TransferItem.h"
#include <QString>
#include <vector>

namespace tankoban::queue {

// One lane per show. Items run strictly sequential inside a lane.
// Lanes across different shows run in parallel.
struct TransferLane {
    QString showId;                       // "" means standalone (one-item lane, immediate)
    std::vector<TransferItem> items;      // index 0 is current (running or paused); rest queued
};

}  // namespace tankoban::queue
```

- [ ] **Step 3: Wire into CMakeLists.txt**

Locate the Tankoban target sources block (around line 144 where `TorrentEngine.cpp` lives) and add:

```cmake
    src/core/queue/TransferItem.h
    src/core/queue/TransferLane.h
```

Header-only at this stage; `.cpp` arrives in Task 1.3.

- [ ] **Step 4: Verify configure + compile**

```
taskkill //F //IM Tankoban.exe
./build_check.bat
```
Expected: `BUILD OK` (no .cpp added yet, just header inclusion via the next task).

- [ ] **Step 5: Commit**

```
git add src/core/queue/TransferItem.h src/core/queue/TransferLane.h CMakeLists.txt
git commit -m "TANKORENT_QUALITY_AND_QUEUE T1.1: TransferItem + TransferLane PODs"
```

### Task 1.2: `TransferQueue` skeleton (no logic yet)

**Files:**
- Create: `src/core/queue/TransferQueue.h`
- Create: `src/core/queue/TransferQueue.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create `TransferQueue.h`**

```cpp
// src/core/queue/TransferQueue.h
#pragma once
#include "TransferLane.h"
#include <QObject>
#include <QHash>
#include <QString>

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

signals:
    void laneChanged(const QString& showId);
    void itemStateChanged(const QString& transferId, TransferState newState);

private:
    QHash<QString, TransferLane> m_lanes;
};

}  // namespace tankoban::queue
```

- [ ] **Step 2: Create stub `TransferQueue.cpp`**

```cpp
// src/core/queue/TransferQueue.cpp
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
```

- [ ] **Step 3: Add to CMakeLists.txt**

```cmake
    src/core/queue/TransferQueue.cpp
    src/core/queue/TransferQueue.h
```

- [ ] **Step 4: Build verify**

```
taskkill //F //IM Tankoban.exe
./build_check.bat
```
Expected: `BUILD OK`.

- [ ] **Step 5: Commit**

```
git add src/core/queue/TransferQueue.h src/core/queue/TransferQueue.cpp CMakeLists.txt
git commit -m "TANKORENT_QUALITY_AND_QUEUE T1.2: TransferQueue skeleton (stubs)"
```

### Task 1.3: TDD — enqueue + finishCurrent + advance

**Files:**
- Create: `tests/core/queue/test_transfer_queue.cpp`
- Modify: `src/core/queue/TransferQueue.cpp`
- Modify: `CMakeLists.txt` (under the `tankoban_tests` target sources)

- [ ] **Step 1: Locate the `tankoban_tests` target block in CMakeLists.txt**

Search for `add_executable(tankoban_tests` and add the new test file to its sources. The existing `TorrentRepoCrudTest` pattern is the reference.

- [ ] **Step 2: Write failing tests**

```cpp
// tests/core/queue/test_transfer_queue.cpp
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
}

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
```

- [ ] **Step 3: Run tests — expect FAIL**

```
./build_check.bat
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R TransferQueueTest
```
Expected: 6 FAIL (stubs return -1/nullopt/false).

- [ ] **Step 4: Implement enqueue + finishCurrent**

Replace the stubs in `TransferQueue.cpp`:

```cpp
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

    it->items.front().state = finalState;
    emit itemStateChanged(it->items.front().transferId, finalState);
    it->items.erase(it->items.begin());

    if (it->items.empty()) {
        m_lanes.erase(it);
        emit laneChanged(showId);
        return std::nullopt;
    }
    emit laneChanged(showId);
    return it->items.front();
}

std::optional<TransferLane> TransferQueue::laneFor(const QString& showId) const {
    auto it = m_lanes.find(showId);
    if (it == m_lanes.end()) return std::nullopt;
    return *it;
}
```

- [ ] **Step 5: Run tests — expect PASS**

```
cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R TransferQueueTest
```
Expected: 6 PASS.

- [ ] **Step 6: Commit**

```
git add tests/core/queue/test_transfer_queue.cpp src/core/queue/TransferQueue.cpp CMakeLists.txt
git commit -m "TANKORENT_QUALITY_AND_QUEUE T1.3: enqueue + finishCurrent (TDD)"
```

### Task 1.4: TDD — pause / resume / cancel

**Files:**
- Modify: `tests/core/queue/test_transfer_queue.cpp`
- Modify: `src/core/queue/TransferQueue.cpp`

- [ ] **Step 1: Append failing tests**

```cpp
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
}

TEST(TransferQueueTest, CancelUnknownIdReturnsFalse) {
    TransferQueue q;
    q.enqueue(makeItem("t1", "imdb:tt0001", 1));
    std::optional<TransferItem> next;
    EXPECT_FALSE(q.cancel("ghost", &next));
}
```

- [ ] **Step 2: Run — expect 5 FAIL**

```
cmake --build out --target tankoban_tests && cd out && ctest --output-on-failure -R TransferQueueTest
```

- [ ] **Step 3: Implement pause/resume/cancel**

```cpp
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
```

- [ ] **Step 4: Run — expect PASS**

- [ ] **Step 5: Commit**

```
git add tests/core/queue/test_transfer_queue.cpp src/core/queue/TransferQueue.cpp
git commit -m "TANKORENT_QUALITY_AND_QUEUE T1.4: pause + resume + cancel (TDD)"
```

### Task 1.5: TDD — reorder + bumpToFront

**Files:**
- Modify: `tests/core/queue/test_transfer_queue.cpp`
- Modify: `src/core/queue/TransferQueue.cpp`

- [ ] **Step 1: Append failing tests**

```cpp
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
```

- [ ] **Step 2: Run — expect 5 FAIL**

- [ ] **Step 3: Implement**

```cpp
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
```

- [ ] **Step 4: Run — expect PASS**

- [ ] **Step 5: Commit**

```
git add tests/core/queue/test_transfer_queue.cpp src/core/queue/TransferQueue.cpp
git commit -m "TANKORENT_QUALITY_AND_QUEUE T1.5: reorder + bumpToFront (TDD)"
```

### Task 1.6: Revert libtorrent global active_downloads cap

**Files:**
- Modify: `src/core/torrent/TorrentEngine.cpp` (lines 344, 380)

**Context:** Line 380 currently sets `active_downloads = 1` (the 2026-05-21 SEQUENTIAL_DOWNLOADS_FIX). The new per-show lane model handles sequencing in `TransferQueue`. Libtorrent must NOT also gate to 1 or per-show parallelism stops working.

- [ ] **Step 1: Read context block at lines 340-385**

Open `src/core/torrent/TorrentEngine.cpp` around line 344. Read the existing comment block that documents the `active_downloads = 1` choice. The change below replaces it.

- [ ] **Step 2: Edit lines 344 + 380**

Replace the comment block at line 344 with:

```cpp
    //   active_downloads = 8 (default): libtorrent no longer enforces a global
    //   serial cap. Per-show sequencing now lives in tankoban::queue::TransferQueue
    //   (Phase 1 of TANKORENT_QUALITY_AND_QUEUE), which gates add-torrent
    //   start across all consumers. Inside a show, one transfer at a time;
    //   across shows, parallel up to active_downloads.
```

Replace line 380:

```cpp
    sp.set_int(lt::settings_pack::active_downloads, 8);
```

Line 951 (`maxDownloads == 0 ? -1 : maxDownloads`) stays unchanged — that's user-tunable.

- [ ] **Step 3: Build verify**

```
taskkill //F //IM Tankoban.exe
./build_check.bat
```
Expected: `BUILD OK`.

- [ ] **Step 4: Commit**

```
git add src/core/torrent/TorrentEngine.cpp
git commit -m "TANKORENT_QUALITY_AND_QUEUE T1.6: revert libtorrent global active_downloads=1 (per-show gating moves to TransferQueue)"
```

### Task 1.7: Own a singleton TransferQueue + expose to consumers

**Files:**
- Modify: `src/core/CoreBridge.h` / `src/core/CoreBridge.cpp` (or whichever class wires Tankoban's lifetime — search for the long-lived owner of `TorrentClient`).

- [ ] **Step 1: Locate the owner of TorrentClient instance**

```
grep -rn "new TorrentClient\|TorrentClient(" src/ | head -5
```
Expected hit: `src/core/CoreBridge.cpp` or `src/ui/MainWindow.cpp`. Identify the constructor where `m_torrentClient` is instantiated; that class becomes `TransferQueue`'s parent.

- [ ] **Step 2: Add `TransferQueue* m_transferQueue` member**

In the owner's header, alongside `m_torrentClient`:

```cpp
#include "core/queue/TransferQueue.h"
// ...
tankoban::queue::TransferQueue* m_transferQueue = nullptr;
```

And in the owner's constructor, before `m_torrentClient` is constructed:

```cpp
m_transferQueue = new tankoban::queue::TransferQueue(this);
```

Add an accessor:

```cpp
tankoban::queue::TransferQueue* transferQueue() const { return m_transferQueue; }
```

- [ ] **Step 3: Build verify**

```
taskkill //F //IM Tankoban.exe && ./build_check.bat
```

- [ ] **Step 4: Commit**

```
git add <owner files>
git commit -m "TANKORENT_QUALITY_AND_QUEUE T1.7: own TransferQueue singleton in CoreBridge/MainWindow"
```

### Task 1.8: Wire TorrentClient::addTorrent through TransferQueue

**Files:**
- Modify: `src/core/torrent/TorrentClient.h` (add `TransferQueue*` parameter or setter; add `TransferStartArgs` POD)
- Modify: `src/core/torrent/TorrentClient.cpp` (gate addTorrent on enqueue + react to finish)

- [ ] **Step 1: Add `TransferStartArgs` POD + setter on TorrentClient**

```cpp
// in TorrentClient.h, above the class declaration:
struct TransferStartArgs {
    QString magnetOrPath;   // magnet URI or .torrent path
    QString showId;         // lane key
    QString transferId;     // infohash (or unique id)
    QString displayTitle;
    bool isMagnet = true;   // false → magnetOrPath is a file path
};

// inside TorrentClient class, public section:
void setTransferQueue(tankoban::queue::TransferQueue* q);

// private:
tankoban::queue::TransferQueue* m_transferQueue = nullptr;
QHash<QString, TransferStartArgs> m_pendingByTransferId;
```

In TorrentClient.cpp implement the setter and wire the consumed signals:

```cpp
void TorrentClient::setTransferQueue(tankoban::queue::TransferQueue* q) {
    m_transferQueue = q;
    if (!q) return;
    connect(q, &tankoban::queue::TransferQueue::itemStateChanged,
            this, [this](const QString& tid, tankoban::queue::TransferState s) {
        // hook used by Task 1.9 when queue advances a new transfer
        Q_UNUSED(tid); Q_UNUSED(s);
    });
}
```

- [ ] **Step 2: In CoreBridge/MainWindow constructor, call setter**

```cpp
m_torrentClient->setTransferQueue(m_transferQueue);
```

- [ ] **Step 3: Build verify**

```
taskkill //F //IM Tankoban.exe && ./build_check.bat
```

- [ ] **Step 4: Commit**

```
git add src/core/torrent/TorrentClient.h src/core/torrent/TorrentClient.cpp <owner.cpp>
git commit -m "TANKORENT_QUALITY_AND_QUEUE T1.8: TorrentClient learns about TransferQueue (wire-only)"
```

### Task 1.9: Gate addTorrent through enqueue + handle queue advance

**Files:**
- Modify: `src/core/torrent/TorrentClient.cpp` — find existing `addTorrent` (magnet + .torrent paths) and route through `TransferQueue`.

- [ ] **Step 1: Locate addTorrent entry points**

```
grep -n 'TorrentClient::addTorrent\|TorrentClient::addMagnet' src/core/torrent/TorrentClient.cpp
```

There are usually two: `addTorrent(QString path, ...)` (file-based) and `addMagnet(QString magnet, ...)`. Identify both.

- [ ] **Step 2: Extract shared "actually-start-this-torrent" code into private helper**

Refactor existing logic into:

```cpp
// private
void TorrentClient::beginTransferOnEngine(const TransferStartArgs& args);
```

Both addTorrent/addMagnet call this. (`TransferStartArgs` is a small POD with magnet/path + showId + transferId + display info. Define in the same header.)

- [ ] **Step 3: Add enqueue gate at addTorrent/addMagnet entry**

```cpp
// Locate the existing addMagnet body and find where infoHash + title are
// already computed before the libtorrent add_torrent_params is built (existing
// code; usually right after lt::parse_magnet_uri). Use those values to build
// TransferItem. Concrete pattern below — adjust local variable names to match
// the actual addMagnet implementation:

void TorrentClient::addMagnet(const QString& magnet, const QString& showId, const QString& displayHint) {
    using namespace tankoban::queue;

    // Existing parse to get atp / infohash — DO NOT remove, the rest of
    // addMagnet still needs it. Just hoist the computation above the
    // queue gate so we can populate TransferItem.
    lt::error_code ec;
    lt::add_torrent_params atp = lt::parse_magnet_uri(magnet.toStdString(), ec);
    if (ec) { /* existing error path */ return; }
    const QString infoHash = QString::fromStdString(
        lt::aux::to_hex(atp.info_hashes.get_best().to_string()));
    const QString title = displayHint.isEmpty()
        ? QString::fromStdString(atp.name)
        : displayHint;

    TransferItem item;
    item.transferId = infoHash;
    item.showId = showId;
    item.displayTitle = title;
    // episodeNumber + seasonNumber populated by SeasonClassifier in Task 4.4

    TransferStartArgs args;
    args.magnetOrPath = magnet;
    args.showId = showId;
    args.transferId = infoHash;
    args.displayTitle = title;
    args.isMagnet = true;

    if (m_transferQueue) {
        const int pos = m_transferQueue->enqueue(item);
        if (pos > 0) {
            // Queued behind current — defer libtorrent start until queue advances.
            m_pendingByTransferId[infoHash] = args;
            return;
        }
    }
    beginTransferOnEngine(args);
}
```

Apply the analogous shape to `addTorrent(const QString& path, ...)`: hoist the existing parse + infoHash extraction above the queue gate; build `TransferItem` + `TransferStartArgs`; gate via `enqueue`; defer to `m_pendingByTransferId` if `pos > 0`.

And implement the queue-advance handler (replacing the stub from Task 1.8):

```cpp
connect(q, &TransferQueue::itemStateChanged,
        this, [this](const QString& tid, TransferState s) {
    if (s != TransferState::Running) return;
    auto it = m_pendingByTransferId.find(tid);
    if (it == m_pendingByTransferId.end()) return;
    beginTransferOnEngine(it.value());
    m_pendingByTransferId.erase(it);
});
```

Add `QHash<QString, TransferStartArgs> m_pendingByTransferId;` to the class.

- [ ] **Step 4: On torrent-complete alert, call `finishCurrent`**

In the existing alert handler that fires when a torrent completes (search for `torrent_finished_alert` in TorrentClient.cpp), add:

```cpp
if (m_transferQueue) {
    QString showId;
    // TORRENT_PERSISTENCE_COLLAPSE migrated the per-torrent state from
    // m_records (in-memory QHash) to m_repo (TorrentRepository/SQL). Use
    // whichever is current at implementation time:
    //
    //   auto record = m_records.find(infoHash);
    //   if (record != m_records.end()) showId = record->showId;
    //
    // OR (post-collapse):
    //
    //   if (auto row = m_repo.getTorrent(infoHash); row.has_value())
    //       showId = row->showId;
    //
    // Verify the current storage shape via:
    //   grep -nE 'm_records\.find|m_repo\.getTorrent' src/core/torrent/TorrentClient.cpp
    if (!showId.isEmpty()) {
        m_transferQueue->finishCurrent(showId, TransferState::Completed);
        // Queue will fire itemStateChanged(Running) for the next item and
        // the handler from Step 3 picks it up. Nothing more to do here.
    }
}
```

**Schema addition:** the per-torrent record needs a `showId` field. If `m_records` is still in use, add `QString showId;` to its POD definition (search `grep -n 'struct TorrentRecord\|struct Record' src/core/torrent/TorrentClient.h`). If `TorrentRepository`/`TorrentRow` is current, add a `show_id TEXT` column to the schema + a `showId` field to `TorrentRow.h` + a migration step in `TorrentRepository::ensureSchema()`. Verify schema location: `grep -n 'CREATE TABLE\|ensureSchema' src/core/torrent/TorrentRepository.cpp`.

- [ ] **Step 5: Build verify**

```
taskkill //F //IM Tankoban.exe && ./build_check.bat
```

- [ ] **Step 6: Commit**

```
git add src/core/torrent/TorrentClient.h src/core/torrent/TorrentClient.cpp
git commit -m "TANKORENT_QUALITY_AND_QUEUE T1.9: gate addTorrent/addMagnet through TransferQueue"
```

### Task 1.10: Wire StreamServerClient downloads through TransferQueue

**Files:**
- Modify: `src/core/stream/stremio/StreamServerClient.h` / `.cpp`

- [ ] **Step 1: Add `setTransferQueue` + `m_transferQueue` mirroring TorrentClient Task 1.8 shape.**

- [ ] **Step 2: At each download-start entry point** (search for `startDownload`, `requestStream`, or wherever the REST POST to stream-server's `/init` endpoint fires), apply the same enqueue-gate pattern from Task 1.9.

- [ ] **Step 3: On stream-server progress callback "download complete", call `finishCurrent`.**

- [ ] **Step 4: Build verify + commit**

```
git commit -m "TANKORENT_QUALITY_AND_QUEUE T1.10: gate stream-server downloads through TransferQueue"
```

### Task 1.11: Phase 1 RTC

- [ ] **Step 1: Smoke test manually**

Queue Daredevil S02 pack via Tankorent, then queue Invincible S04 pack. Open Theatre Downloads. Confirm both shows show as actively downloading (parallel across shows). Within a show, only one episode runs at a time.

- [ ] **Step 2: Add RTC line to `agents/chat.md`**

```
READY TO COMMIT — [Agent 4, TANKORENT_QUEUE_P1]: Per-show TransferQueue infrastructure shipped. libtorrent active_downloads reverted from 1 to 8; per-show gating now lives in tankoban::queue::TransferQueue (T1.1-1.5 TDD, T1.6 revert, T1.7-1.10 wiring). Files: src/core/queue/{TransferItem,TransferLane,TransferQueue}.{h,cpp}, src/core/torrent/{TorrentClient,TorrentEngine}.{h,cpp}, src/core/stream/stremio/StreamServerClient.{h,cpp}, tests/core/queue/test_transfer_queue.cpp, CMakeLists.txt. Tests: 16 PASS in TransferQueueTest. Build: OK. Smoke: 2-show parallel + per-show sequential VERIFIED by Hemanth.
```

---

## Phase 2: Nyaa parity audit

**Context:** Tankorent's Nyaa indexer drops rows compared to Nyaa.si's site results. The drops come from (a) seeder threshold + (b) trust filtering. Phase 2 removes both, keeps dedupe, re-sorts by seeders, and exposes the honest count to the UI.

### Task 2.1: Audit NyaaIndexer for drop logic

**Files:**
- Read-only: `src/core/indexers/NyaaIndexer.cpp` / `.h`

- [ ] **Step 1: Identify drop sites**

```
grep -nE '(seeder|trusted|filter|skip|continue)' src/core/indexers/NyaaIndexer.cpp
```

Document each filter site (line number + what it drops). Common patterns: `if (seeders < THRESHOLD) continue;`, `if (!isTrustedUploader(row)) continue;`.

- [ ] **Step 2: Identify count-tracking**

Find where the indexer reports "N results" to the search service. We need to preserve the raw count for honest disclosure.

- [ ] **Step 3: No commit yet — research only.**

### Task 2.2: Remove seeder threshold + trust filter from NyaaIndexer

**Files:**
- Modify: `src/core/indexers/NyaaIndexer.cpp`

- [ ] **Step 1: Delete the seeder threshold check (whichever line(s) Task 2.1 identified).**

- [ ] **Step 2: Delete the trusted-uploader filter (if present).**

- [ ] **Step 3: Preserve dedupe.** If the indexer uses an `infoHash` set to dedupe, keep that.

- [ ] **Step 4: Sort the result vector by seeders descending before emitting.**

```cpp
std::sort(results.begin(), results.end(),
          [](const auto& a, const auto& b) { return a.seeders > b.seeders; });
```

- [ ] **Step 5: Build verify + commit**

```
taskkill //F //IM Tankoban.exe && ./build_check.bat
git add src/core/indexers/NyaaIndexer.cpp
git commit -m "TANKORENT_QUALITY_AND_QUEUE T2.2: NyaaIndexer drops seeder threshold + trust filter; re-sort by seeders"
```

### Task 2.3: Expose raw count to TankorentSearchService consumers

**Files:**
- Modify: `src/core/TankorentSearchService.h` / `.cpp`

- [ ] **Step 1: Add `rawCount` to the `resultsReady` signal payload**

If signal is currently `void resultsReady(QString indexerId, QList<SearchResult> results)`, extend to:

```cpp
void resultsReady(const QString& indexerId, const QList<SearchResult>& results, int rawCount);
```

`rawCount` = the count BEFORE dedupe-only is applied (or equal to results.size() if dedupe is also disabled). For Phase 2 this equals `results.size()` since we stripped filtering; the parameter exists for the per-source disclosure UI in Phase 3.

- [ ] **Step 2: Update all signal emit sites + slot consumer signatures.**

```
grep -rn 'connect.*TankorentSearchService::resultsReady\|emit resultsReady' src/
```
Edit each match to the new signature.

- [ ] **Step 3: Build verify + commit**

```
taskkill //F //IM Tankoban.exe && ./build_check.bat
git commit -m "TANKORENT_QUALITY_AND_QUEUE T2.3: TankorentSearchService::resultsReady carries rawCount"
```

### Task 2.4: Nyaa.si parity probe harness

**Files:**
- Create: `scripts/nyaa-parity-probe.ps1`

- [ ] **Step 1: Write the harness**

```powershell
# scripts/nyaa-parity-probe.ps1
# Compares Nyaa.si HTTP results vs Tankorent's NyaaIndexer for a given query.
# Usage: powershell -File scripts/nyaa-parity-probe.ps1 -Query "Daredevil"

param(
    [Parameter(Mandatory=$true)][string]$Query
)

$encoded = [uri]::EscapeDataString($Query)
$url = "https://nyaa.si/?f=0&c=0_0&q=$encoded&s=seeders&o=desc"

$html = Invoke-WebRequest -Uri $url -UseBasicParsing
$rows = ([regex]::Matches($html.Content, '<tr class="(?:default|success)">')).Count

Write-Host "Nyaa.si raw row count for query [$Query]: $rows"
Write-Host ""
Write-Host "To compare with Tankorent's indexer:"
Write-Host "  1. Launch Tankoban with --dev-control."
Write-Host "  2. tankoctl search-results --indexer nyaa --query `"$Query`" | jq '.results | length'"
Write-Host "  3. Counts should match (or be within dedupe slack)."
```

- [ ] **Step 2: Run it for a known query**

```
powershell -File scripts/nyaa-parity-probe.ps1 -Query "Daredevil"
```

Record the count. Then launch Tankoban (`build_and_run.bat`), search for "Daredevil" in Tankorent (Nyaa source), confirm count matches within a small slack (≤5%).

- [ ] **Step 3: Commit**

```
git add scripts/nyaa-parity-probe.ps1
git commit -m "TANKORENT_QUALITY_AND_QUEUE T2.4: Nyaa.si parity probe harness"
```

### Task 2.5: Phase 2 RTC

- [ ] **Step 1: Add RTC line**

```
READY TO COMMIT — [Agent 4, TANKORENT_QUEUE_P2]: NyaaIndexer parity restored. Stripped seeder threshold + trust filter, kept dedupe, re-sort by seeders. TankorentSearchService::resultsReady extended with rawCount. Nyaa.si parity probe harness at scripts/nyaa-parity-probe.ps1 — query "Daredevil" returned matching counts. Files: src/core/indexers/NyaaIndexer.cpp, src/core/TankorentSearchService.{h,cpp}, scripts/nyaa-parity-probe.ps1, downstream consumer files for the signal extension. Build: OK.
```

---

## Phase 3: Tankorent as source-addon inside Theatre series view

**Context:** `StreamDetailView` is the series-view page in Theatre. It already hosts Torrentio results in a Sources sidebar. Tankorent becomes a second addon panel in that sidebar with: indexer dropdown, filter chips (All/Packs/Episodes — chips render in Phase 4), Search button, results list. Query is implicit (the show's title + season context).

### Task 3.1: `TankorentSourceAddon` widget skeleton

**Files:**
- Create: `src/ui/pages/stream/TankorentSourceAddon.h`
- Create: `src/ui/pages/stream/TankorentSourceAddon.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create the header**

```cpp
// src/ui/pages/stream/TankorentSourceAddon.h
#pragma once
#include <QWidget>
#include <QString>

class TankorentSearchService;
class QComboBox;
class QPushButton;
class QListWidget;
class QLabel;

namespace tankoban::ui::stream {

class TankorentSourceAddon : public QWidget {
    Q_OBJECT
public:
    explicit TankorentSourceAddon(TankorentSearchService* svc, QWidget* parent = nullptr);

    // Called by StreamDetailView when the show context loads. Stores the
    // implicit query (title + season) but does NOT fire a search — search is
    // click-only.
    void setShowContext(const QString& title, int season);

signals:
    void downloadRequested(const QString& magnet, const QString& showId,
                           const QString& displayTitle);

private slots:
    void onSearchClicked();
    void onIndexerChanged();

private:
    void buildUi();
    void clearResultsWithPrompt();

    TankorentSearchService* m_svc = nullptr;
    QComboBox* m_indexerDropdown = nullptr;
    QPushButton* m_searchButton = nullptr;
    QListWidget* m_results = nullptr;
    QLabel* m_resultCount = nullptr;
    QString m_implicitQuery;
    QString m_showId;
};

}  // namespace tankoban::ui::stream
```

- [ ] **Step 2: Create the .cpp with minimal `buildUi`**

```cpp
// src/ui/pages/stream/TankorentSourceAddon.cpp
#include "TankorentSourceAddon.h"
#include "core/TankorentSearchService.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>

namespace tankoban::ui::stream {

TankorentSourceAddon::TankorentSourceAddon(TankorentSearchService* svc, QWidget* parent)
    : QWidget(parent), m_svc(svc) {
    buildUi();
}

void TankorentSourceAddon::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto* topRow = new QHBoxLayout();
    m_indexerDropdown = new QComboBox(this);
    m_indexerDropdown->addItems({"Nyaa", "PirateBay", "1337x", "EZTV", "TorrentsCSV", "YTS", "ExtTorrents"});
    m_searchButton = new QPushButton(tr("Search"), this);
    topRow->addWidget(m_indexerDropdown, 1);
    topRow->addWidget(m_searchButton);
    root->addLayout(topRow);

    m_resultCount = new QLabel(this);
    m_resultCount->setStyleSheet("color: #888; font-size: 10px;");
    root->addWidget(m_resultCount);

    m_results = new QListWidget(this);
    root->addWidget(m_results, 1);

    connect(m_searchButton, &QPushButton::clicked,
            this, &TankorentSourceAddon::onSearchClicked);
    connect(m_indexerDropdown, &QComboBox::currentTextChanged,
            this, &TankorentSourceAddon::onIndexerChanged);

    clearResultsWithPrompt();
}

void TankorentSourceAddon::setShowContext(const QString& title, int season) {
    m_implicitQuery = season > 0
        ? QStringLiteral("%1 Season %2").arg(title).arg(season)
        : title;
    clearResultsWithPrompt();
}

void TankorentSourceAddon::clearResultsWithPrompt() {
    m_results->clear();
    m_resultCount->setText(tr("Click Search to load"));
}

void TankorentSourceAddon::onIndexerChanged() {
    // Source switch: clear + force click per spec
    clearResultsWithPrompt();
}

void TankorentSourceAddon::onSearchClicked() {
    if (m_implicitQuery.isEmpty()) return;
    m_results->clear();
    m_resultCount->setText(tr("Searching..."));
    // TODO Task 3.2: actually fire m_svc with current indexer
}

}  // namespace tankoban::ui::stream
```

- [ ] **Step 3: Register in CMakeLists.txt**

```cmake
    src/ui/pages/stream/TankorentSourceAddon.h
    src/ui/pages/stream/TankorentSourceAddon.cpp
```

- [ ] **Step 4: Build verify + commit**

```
taskkill //F //IM Tankoban.exe && ./build_check.bat
git commit -m "TANKORENT_QUALITY_AND_QUEUE T3.1: TankorentSourceAddon widget skeleton"
```

### Task 3.2: Wire onSearchClicked → TankorentSearchService

**Files:**
- Modify: `src/ui/pages/stream/TankorentSourceAddon.cpp` / `.h`

- [ ] **Step 1: Connect to `resultsReady` signal in constructor**

```cpp
connect(m_svc, &TankorentSearchService::resultsReady,
        this, [this](const QString& indexerId, const QList<SearchResult>& results, int rawCount) {
    if (indexerId != m_indexerDropdown->currentText().toLower()) return;
    m_resultCount->setText(tr("%1 results from %2")
                           .arg(rawCount)
                           .arg(m_indexerDropdown->currentText()));
    m_results->clear();
    for (const auto& r : results) {
        auto* item = new QListWidgetItem(r.title, m_results);
        item->setData(Qt::UserRole, r.magnet);
    }
});
```

- [ ] **Step 2: Replace the TODO in `onSearchClicked` with the actual service call**

```cpp
void TankorentSourceAddon::onSearchClicked() {
    if (m_implicitQuery.isEmpty() || !m_svc) return;
    m_results->clear();
    m_resultCount->setText(tr("Searching..."));
    const QString indexer = m_indexerDropdown->currentText().toLower();
    m_svc->search(m_implicitQuery, indexer);
}
```

Note: confirm the actual `TankorentSearchService::search` signature matches; adjust if it takes a different shape (e.g. a struct of params).

- [ ] **Step 3: Add click-to-queue handler on result rows**

```cpp
connect(m_results, &QListWidget::itemDoubleClicked,
        this, [this](QListWidgetItem* item) {
    const QString magnet = item->data(Qt::UserRole).toString();
    emit downloadRequested(magnet, m_showId, item->text());
});
```

- [ ] **Step 4: Build verify + commit**

```
taskkill //F //IM Tankoban.exe && ./build_check.bat
git commit -m "TANKORENT_QUALITY_AND_QUEUE T3.2: wire TankorentSourceAddon to search service + click-to-queue"
```

### Task 3.3: Embed TankorentSourceAddon into StreamDetailView

**Files:**
- Modify: `src/ui/pages/stream/StreamDetailView.h` / `.cpp`

- [ ] **Step 1: Locate the existing Sources sidebar code**

```
grep -nE 'Torrentio|SourcesPanel|m_sources' src/ui/pages/stream/StreamDetailView.cpp | head -20
```
Identify how Torrentio is hosted. This is the pattern Tankorent slots into alongside.

- [ ] **Step 2: Add `TankorentSourceAddon* m_tankorentAddon = nullptr;` member**

In the constructor (after Torrentio addon construction), construct:

```cpp
m_tankorentAddon = new TankorentSourceAddon(m_coreBridge->tankorentSearchService(), this);
m_sourcesPanel->addWidget(m_tankorentAddon);  // or whatever the existing addon-container API is

connect(m_tankorentAddon, &TankorentSourceAddon::downloadRequested,
        this, [this](const QString& magnet, const QString& showId, const QString& title) {
    m_coreBridge->torrentClient()->addMagnet(magnet, showId, title);
});
```

- [ ] **Step 3: On show context load, propagate to addon**

In whatever method sets the show data (likely `setShowMetadata` or `loadShow`):

```cpp
if (m_tankorentAddon) {
    m_tankorentAddon->setShowContext(meta.title, meta.currentSeason);
    m_tankorentAddon->setShowId(QString("imdb:%1").arg(meta.imdbId));
}
```

Add a public `setShowId(const QString&)` to `TankorentSourceAddon` that just assigns `m_showId`.

- [ ] **Step 4: Build verify + Hemanth smoke**

```
taskkill //F //IM Tankoban.exe
./build_check.bat
build_and_run.bat
```

Hemanth smokes: open a show in Theatre; Tankorent section appears below Torrentio in the Sources sidebar; clicking Search runs a real Tankorent query; results render; switching the indexer dropdown clears results with "Click Search to load."

- [ ] **Step 5: Commit**

```
git commit -m "TANKORENT_QUALITY_AND_QUEUE T3.3: embed TankorentSourceAddon in StreamDetailView Sources sidebar"
```

### Task 3.4: Phase 3 RTC

```
READY TO COMMIT — [Agent 4, TANKORENT_QUEUE_P3]: Tankorent now lives as a source-addon inside Theatre's series-view Sources sidebar alongside Torrentio. Indexer dropdown + click-only Search button + result list with honest "N results from Nyaa" disclosure. Source switch clears results + shows "Click Search to load" prompt. Files: src/ui/pages/stream/TankorentSourceAddon.{h,cpp}, src/ui/pages/stream/StreamDetailView.{h,cpp}, CMakeLists.txt. Build: OK. Smoke: Hemanth-verified Tankorent appears + searches + clicks-to-queue.
```

---

## Phase 4: Pack detection + badges + filter chip

### Task 4.1: TDD — `SeasonClassifier`

**Files:**
- Create: `src/core/torrent/SeasonClassifier.h`
- Create: `src/core/torrent/SeasonClassifier.cpp`
- Create: `tests/core/torrent/test_season_classifier.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create header**

```cpp
// src/core/torrent/SeasonClassifier.h
#pragma once
#include <QString>

namespace tankoban::torrent {

enum class SeasonClass {
    MultiSeason,   // "S01-S05", "Complete Series"
    SeasonPack,    // "Season 2", "S02", "S02 Complete"
    Episode,       // "S02E01", "2x01", "Episode 1"
    Unclassified,  // no match
};

struct SeasonInfo {
    SeasonClass kind = SeasonClass::Unclassified;
    int seasonStart = -1;  // for SeasonPack and MultiSeason
    int seasonEnd = -1;    // for MultiSeason (equals seasonStart for SeasonPack)
    int episode = -1;      // for Episode
};

SeasonInfo classify(const QString& title);

}  // namespace tankoban::torrent
```

- [ ] **Step 2: Create empty .cpp returning Unclassified**

```cpp
// src/core/torrent/SeasonClassifier.cpp
#include "SeasonClassifier.h"
namespace tankoban::torrent {
SeasonInfo classify(const QString&) { return {}; }
}
```

- [ ] **Step 3: Add to CMakeLists.txt (both Tankoban + tankoban_tests)**

- [ ] **Step 4: Write failing tests covering the matrix**

```cpp
// tests/core/torrent/test_season_classifier.cpp
#include <gtest/gtest.h>
#include "core/torrent/SeasonClassifier.h"

using namespace tankoban::torrent;

TEST(SeasonClassifier, MultiSeasonExplicitRange) {
    auto info = classify("Daredevil S01-S03 1080p");
    EXPECT_EQ(info.kind, SeasonClass::MultiSeason);
    EXPECT_EQ(info.seasonStart, 1);
    EXPECT_EQ(info.seasonEnd, 3);
}

TEST(SeasonClassifier, MultiSeasonCompleteSeries) {
    auto info = classify("Daredevil Complete Series 1080p");
    EXPECT_EQ(info.kind, SeasonClass::MultiSeason);
}

TEST(SeasonClassifier, SeasonPackSXX) {
    auto info = classify("Daredevil S02 1080p WEB-DL");
    EXPECT_EQ(info.kind, SeasonClass::SeasonPack);
    EXPECT_EQ(info.seasonStart, 2);
    EXPECT_EQ(info.seasonEnd, 2);
}

TEST(SeasonClassifier, SeasonPackVerbose) {
    auto info = classify("Daredevil Season 2 Complete");
    EXPECT_EQ(info.kind, SeasonClass::SeasonPack);
    EXPECT_EQ(info.seasonStart, 2);
}

TEST(SeasonClassifier, EpisodeStandard) {
    auto info = classify("Daredevil S02E01 1080p");
    EXPECT_EQ(info.kind, SeasonClass::Episode);
    EXPECT_EQ(info.seasonStart, 2);
    EXPECT_EQ(info.episode, 1);
}

TEST(SeasonClassifier, EpisodeAltSyntax) {
    auto info = classify("Daredevil 2x01 720p");
    EXPECT_EQ(info.kind, SeasonClass::Episode);
    EXPECT_EQ(info.seasonStart, 2);
    EXPECT_EQ(info.episode, 1);
}

TEST(SeasonClassifier, EpisodeLowercaseS) {
    auto info = classify("Frieren s01e12.mkv");
    EXPECT_EQ(info.kind, SeasonClass::Episode);
    EXPECT_EQ(info.seasonStart, 1);
    EXPECT_EQ(info.episode, 12);
}

TEST(SeasonClassifier, UnclassifiedMovie) {
    auto info = classify("The Matrix 1999 1080p BluRay");
    EXPECT_EQ(info.kind, SeasonClass::Unclassified);
}

TEST(SeasonClassifier, SeasonPackBeatsEpisodeWhenBothMatch) {
    // "S02 Complete" should win over "S02" alone (preferring richer signal)
    auto info = classify("Show S02 Complete Pack");
    EXPECT_EQ(info.kind, SeasonClass::SeasonPack);
}

TEST(SeasonClassifier, MultiSeasonBeatsSeasonPack) {
    auto info = classify("Show S01-S05 Complete Series");
    EXPECT_EQ(info.kind, SeasonClass::MultiSeason);
}
```

- [ ] **Step 5: Build + run tests — expect 10 FAIL**

```
./build_check.bat && cmake --build out --target tankoban_tests
cd out && ctest --output-on-failure -R SeasonClassifier
```

- [ ] **Step 6: Implement using regex**

Replace the stub with:

```cpp
#include <QRegularExpression>

namespace tankoban::torrent {

SeasonInfo classify(const QString& titleIn) {
    const QString title = titleIn.trimmed();
    SeasonInfo info;

    // 1. Multi-season explicit range: S01-S05 / S01-05 / S1-S3
    static const QRegularExpression rxRange(
        R"(\bS(\d{1,2})\s*-\s*S?(\d{1,2})\b)",
        QRegularExpression::CaseInsensitiveOption);
    auto mRange = rxRange.match(title);
    if (mRange.hasMatch()) {
        info.kind = SeasonClass::MultiSeason;
        info.seasonStart = mRange.captured(1).toInt();
        info.seasonEnd = mRange.captured(2).toInt();
        return info;
    }

    // 2. Multi-season verbose: "Complete Series"
    static const QRegularExpression rxCompleteSeries(
        R"(\bComplete\s+Series\b)",
        QRegularExpression::CaseInsensitiveOption);
    if (rxCompleteSeries.match(title).hasMatch()) {
        info.kind = SeasonClass::MultiSeason;
        return info;
    }

    // 3. Episode: S02E01 / S2E1 / 2x01
    static const QRegularExpression rxEpisode(
        R"(\bS(\d{1,2})E(\d{1,3})\b)",
        QRegularExpression::CaseInsensitiveOption);
    auto mEp = rxEpisode.match(title);
    if (mEp.hasMatch()) {
        info.kind = SeasonClass::Episode;
        info.seasonStart = mEp.captured(1).toInt();
        info.episode = mEp.captured(2).toInt();
        return info;
    }
    static const QRegularExpression rxEpAlt(
        R"(\b(\d{1,2})x(\d{1,3})\b)");
    auto mEpAlt = rxEpAlt.match(title);
    if (mEpAlt.hasMatch()) {
        info.kind = SeasonClass::Episode;
        info.seasonStart = mEpAlt.captured(1).toInt();
        info.episode = mEpAlt.captured(2).toInt();
        return info;
    }

    // 4. Season pack: "Season 2" / "S02" / "S02 Complete"
    static const QRegularExpression rxSeasonVerbose(
        R"(\bSeason\s+(\d{1,2})\b)",
        QRegularExpression::CaseInsensitiveOption);
    auto mSv = rxSeasonVerbose.match(title);
    if (mSv.hasMatch()) {
        info.kind = SeasonClass::SeasonPack;
        info.seasonStart = info.seasonEnd = mSv.captured(1).toInt();
        return info;
    }
    static const QRegularExpression rxSeasonShort(
        R"(\bS(\d{1,2})\b)",
        QRegularExpression::CaseInsensitiveOption);
    auto mSs = rxSeasonShort.match(title);
    if (mSs.hasMatch()) {
        info.kind = SeasonClass::SeasonPack;
        info.seasonStart = info.seasonEnd = mSs.captured(1).toInt();
        return info;
    }

    info.kind = SeasonClass::Unclassified;
    return info;
}

}  // namespace tankoban::torrent
```

- [ ] **Step 7: Run tests — expect PASS**

- [ ] **Step 8: Commit**

```
git add src/core/torrent/SeasonClassifier.{h,cpp} tests/core/torrent/test_season_classifier.cpp CMakeLists.txt
git commit -m "TANKORENT_QUALITY_AND_QUEUE T4.1: SeasonClassifier (TDD, 10 cases)"
```

### Task 4.2: Badge rendering in TankorentSourceAddon

**Files:**
- Modify: `src/ui/pages/stream/TankorentSourceAddon.cpp`

- [ ] **Step 1: Replace the QListWidgetItem create with a custom row widget**

In the `resultsReady` handler from Task 3.2, swap the simple list item for a row widget with badge + title:

```cpp
for (const auto& r : results) {
    auto* row = new QWidget(m_results);
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(6, 4, 6, 4);

    auto info = tankoban::torrent::classify(r.title);
    auto* badge = new QLabel(row);
    badge->setStyleSheet("padding: 2px 6px; border-radius: 3px; font-size: 9px; font-weight: bold; color: white;");
    switch (info.kind) {
        case tankoban::torrent::SeasonClass::MultiSeason:
            badge->setText(QString("S%1-S%2").arg(info.seasonStart).arg(info.seasonEnd));
            badge->setStyleSheet(badge->styleSheet() + "background: #3a8a3a;");
            break;
        case tankoban::torrent::SeasonClass::SeasonPack:
            badge->setText(QString("S%1").arg(info.seasonStart));
            badge->setStyleSheet(badge->styleSheet() + "background: #3a6a8a;");
            break;
        case tankoban::torrent::SeasonClass::Episode:
            badge->setText("EP");
            badge->setStyleSheet(badge->styleSheet() + "background: #6a6a6a;");
            break;
        case tankoban::torrent::SeasonClass::Unclassified:
            badge->setText("?");
            badge->setStyleSheet(badge->styleSheet() + "background: #444;");
            break;
    }
    lay->addWidget(badge);

    auto* title = new QLabel(r.title, row);
    title->setStyleSheet("color: #ccc; font-size: 11px;");
    lay->addWidget(title, 1);

    auto* item = new QListWidgetItem(m_results);
    item->setSizeHint(row->sizeHint());
    item->setData(Qt::UserRole, r.magnet);
    item->setData(Qt::UserRole + 1, static_cast<int>(info.kind));
    m_results->setItemWidget(item, row);
}
```

- [ ] **Step 2: Include SeasonClassifier header**

Add `#include "core/torrent/SeasonClassifier.h"` at the top.

- [ ] **Step 3: Build verify + Hemanth smoke**

```
taskkill //F //IM Tankoban.exe && ./build_check.bat && build_and_run.bat
```

Hemanth: open a show, search Nyaa, confirm badges render with the right colors.

- [ ] **Step 4: Commit**

```
git commit -m "TANKORENT_QUALITY_AND_QUEUE T4.2: badges (S1-S3 green / S2 blue / EP grey) in addon results"
```

### Task 4.3: Filter chip (All / Packs / Episodes)

**Files:**
- Modify: `src/ui/pages/stream/TankorentSourceAddon.{h,cpp}`

- [ ] **Step 1: Add three `QPushButton*` chips + an enum for current selection**

```cpp
// in header private section:
enum class FilterChip { All, Packs, Episodes };
FilterChip m_chip = FilterChip::All;
QPushButton* m_chipAll = nullptr;
QPushButton* m_chipPacks = nullptr;
QPushButton* m_chipEpisodes = nullptr;

void styleChip(QPushButton* btn, bool selected);
void onChipClicked(FilterChip kind);
```

- [ ] **Step 2: In `buildUi`, add a chip row beneath the search row**

```cpp
auto* chipRow = new QHBoxLayout();
m_chipAll = new QPushButton(tr("All"), this);
m_chipPacks = new QPushButton(tr("Packs"), this);
m_chipEpisodes = new QPushButton(tr("Episodes"), this);
chipRow->addWidget(m_chipAll);
chipRow->addWidget(m_chipPacks);
chipRow->addWidget(m_chipEpisodes);
chipRow->addStretch(1);
root->insertLayout(2, chipRow);  // after search row + resultCount

connect(m_chipAll, &QPushButton::clicked, this, [this]{ onChipClicked(FilterChip::All); });
connect(m_chipPacks, &QPushButton::clicked, this, [this]{ onChipClicked(FilterChip::Packs); });
connect(m_chipEpisodes, &QPushButton::clicked, this, [this]{ onChipClicked(FilterChip::Episodes); });

styleChip(m_chipAll, true);
styleChip(m_chipPacks, false);
styleChip(m_chipEpisodes, false);
```

And implement:

```cpp
void TankorentSourceAddon::styleChip(QPushButton* btn, bool selected) {
    btn->setStyleSheet(selected
        ? "background: #2a2a4a; border: 1px solid #4a4a7a; color: #ccc; padding: 3px 10px; border-radius: 3px;"
        : "background: #1a1a1a; border: 1px solid #2a2a2a; color: #888; padding: 3px 10px; border-radius: 3px;");
}

void TankorentSourceAddon::onChipClicked(FilterChip kind) {
    if (m_chip == kind) return;
    m_chip = kind;
    styleChip(m_chipAll, kind == FilterChip::All);
    styleChip(m_chipPacks, kind == FilterChip::Packs);
    styleChip(m_chipEpisodes, kind == FilterChip::Episodes);
    clearResultsWithPrompt();  // chip change requires Search click per spec
}
```

- [ ] **Step 3: Apply filter when rendering results**

In the `resultsReady` handler, wrap the per-result loop:

```cpp
for (const auto& r : results) {
    auto info = tankoban::torrent::classify(r.title);
    bool include = true;
    if (m_chip == FilterChip::Packs) {
        include = (info.kind == tankoban::torrent::SeasonClass::SeasonPack
                || info.kind == tankoban::torrent::SeasonClass::MultiSeason);
    } else if (m_chip == FilterChip::Episodes) {
        include = (info.kind == tankoban::torrent::SeasonClass::Episode);
    }
    if (!include) continue;
    // ... (existing badge + row creation)
}
```

- [ ] **Step 4: Build + Hemanth smoke**

```
taskkill //F //IM Tankoban.exe && ./build_check.bat && build_and_run.bat
```

Hemanth: search, then click Packs → results clear with "Click Search to load" → click Search → only pack-badged rows appear.

- [ ] **Step 5: Commit**

```
git commit -m "TANKORENT_QUALITY_AND_QUEUE T4.3: filter chips (All/Packs/Episodes) with click-to-refresh"
```

### Task 4.4: Pack-internal episode ordering on add

**Files:**
- Modify: `src/core/torrent/TorrentClient.cpp` (the file-priority assignment that happens after metadata-received alert)

- [ ] **Step 1: Locate the metadata-received alert handler**

```
grep -nE 'metadata_received_alert|metadata_failed_alert' src/core/torrent/TorrentClient.cpp
```

In that handler, after file list is available, parse each file's name for episode number and apply piece-priority ordering.

- [ ] **Step 2: Add episode-number parsing helper**

```cpp
// In TorrentClient.cpp, free function above the class methods:
namespace {
int parseEpisodeNumber(const QString& fileName) {
    using namespace tankoban::torrent;
    auto info = classify(fileName);
    if (info.kind == SeasonClass::Episode) return info.episode;
    // Fallback: filename digit run after 'E' or 'Ep' or 'Episode'
    static const QRegularExpression rxFallback(
        R"((?:E|Ep|Episode)\s*0*(\d{1,3}))",
        QRegularExpression::CaseInsensitiveOption);
    auto m = rxFallback.match(fileName);
    if (m.hasMatch()) return m.captured(1).toInt();
    return -1;
}
}
```

- [ ] **Step 3: In the metadata-received handler, sort files by episode + set sequential priorities**

```cpp
// After file_storage is available:
auto& fs = ti->files();
const int numFiles = fs.num_files();
std::vector<std::pair<int /*orig idx*/, int /*ep num*/>> ordered;
for (int i = 0; i < numFiles; ++i) {
    QString name = QString::fromStdString(fs.file_path(lt::file_index_t{i}));
    ordered.emplace_back(i, parseEpisodeNumber(name));
}
std::sort(ordered.begin(), ordered.end(),
          [](auto& a, auto& b) {
    if (a.second < 0 && b.second < 0) return a.first < b.first;
    if (a.second < 0) return false;
    if (b.second < 0) return true;
    return a.second < b.second;
});

// Assign descending priorities so libtorrent fetches E01 first
std::vector<lt::download_priority_t> prios(numFiles, lt::download_priority_t{1});
int prio = 7;
for (auto [idx, ep] : ordered) {
    prios[idx] = lt::download_priority_t{static_cast<std::uint8_t>(std::max(prio--, 1))};
}
it->handle.prioritize_files(prios);
```

- [ ] **Step 4: Build + smoke**

```
taskkill //F //IM Tankoban.exe && ./build_check.bat && build_and_run.bat
```

Hemanth: queue a season pack, observe that E01 finishes before E02 starts.

- [ ] **Step 5: Commit**

```
git commit -m "TANKORENT_QUALITY_AND_QUEUE T4.4: pack-internal episode ordering (E01 → E02 → ...) via file-priority"
```

### Task 4.5: Phase 4 RTC

```
READY TO COMMIT — [Agent 4, TANKORENT_QUEUE_P4]: Season classification + badges + filter chips + pack-internal episode ordering. SeasonClassifier (10-case TDD GREEN). Tankorent result rows now show color-coded badges (green multi-season, blue single-season, grey episode). Filter chips (All / Packs / Episodes) with click-to-refresh. On pack add, libtorrent file priorities sorted by parsed episode number so E01 finishes before E02. Files: src/core/torrent/SeasonClassifier.{h,cpp}, src/core/torrent/TorrentClient.cpp, src/ui/pages/stream/TankorentSourceAddon.{h,cpp}, tests/core/torrent/test_season_classifier.cpp, CMakeLists.txt. Build: OK. Smoke: Hemanth-verified badges + chip filtering + ordered pack download.
```

---

## Phase 5: Theatre Downloads page Netflix revision

### Task 5.1: One-card-per-show layout

**Files:**
- Modify: `src/ui/pages/stream/StreamDownloadsPage.{h,cpp}`

**Context:** The current page renders one card per IMDb show grouped by season. The Netflix revision: poster + clean title + season summary + current episode progress + queued episode list + Pause + Cancel + reorder + bump. No torrent metadata anywhere.

- [ ] **Step 1: Read current `refreshActive` shape**

```
grep -n 'refreshActive\|refreshHistory' src/ui/pages/stream/StreamDownloadsPage.cpp
```

Read those two methods top-to-bottom. Identify how show data + queue state is currently rendered.

- [ ] **Step 2: Inject TransferQueue dependency**

In `StreamDownloadsPage.h`, add:

```cpp
void setTransferQueue(tankoban::queue::TransferQueue* q);
private:
tankoban::queue::TransferQueue* m_transferQueue = nullptr;
```

In `.cpp`, implement + subscribe to `laneChanged` signal to trigger refresh.

In `MainWindow.cpp` (constructor of StreamDownloadsPage), call `setTransferQueue(m_coreBridge->transferQueue())`.

- [ ] **Step 3: Replace `refreshActive` body**

For each show with an active lane, render a card:

```cpp
void StreamDownloadsPage::refreshActive() {
    // Clear existing card widgets
    QLayoutItem* child;
    while ((child = m_activeContainer->layout()->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
    if (!m_transferQueue) return;

    const auto lanes = m_transferQueue->lanesSnapshot();
    for (auto it = lanes.constBegin(); it != lanes.constEnd(); ++it) {
        const auto& lane = it.value();
        if (lane.items.empty()) continue;

        auto* card = new QWidget(m_activeContainer);
        card->setStyleSheet("background: #1a1a1a; border: 1px solid #2a2a2a; border-radius: 8px;");
        auto* layout = new QHBoxLayout(card);
        layout->setContentsMargins(12, 12, 12, 12);

        // Poster (resolved from show metadata cache)
        auto* poster = new QLabel(card);
        poster->setFixedSize(60, 90);
        poster->setPixmap(resolveShowPoster(lane.showId));  // helper, Task 5.2
        poster->setScaledContents(true);
        layout->addWidget(poster);

        auto* right = new QVBoxLayout();
        auto* title = new QLabel(resolveShowTitle(lane.showId), card);
        title->setStyleSheet("color: #eee; font-size: 13px; font-weight: 600;");
        right->addWidget(title);

        auto* meta = new QLabel(QString("Season %1 · %2 episodes")
                                  .arg(lane.items.front().seasonNumber.value_or(0))
                                  .arg(static_cast<int>(lane.items.size())), card);
        meta->setStyleSheet("color: #888; font-size: 10px;");
        right->addWidget(meta);

        // Current episode progress
        const auto& cur = lane.items.front();
        auto* progress = new QLabel(
            QString("▶ Episode %1 · %2%")
                .arg(cur.episodeNumber.value_or(0))
                .arg(resolveProgress(cur.transferId)), card);
        progress->setStyleSheet("color: #a0a0ff; font-size: 10px;");
        right->addWidget(progress);

        // Queued episodes list (clean, no filenames)
        if (lane.items.size() > 1) {
            QStringList queued;
            for (size_t i = 1; i < lane.items.size() && i < 5; ++i) {
                queued << QString("Episode %1 · Queued").arg(lane.items[i].episodeNumber.value_or(0));
            }
            if (lane.items.size() > 5) {
                queued << QString("+%1 more").arg(lane.items.size() - 5);
            }
            auto* qLabel = new QLabel(queued.join(" · "), card);
            qLabel->setStyleSheet("color: #888; font-size: 9px;");
            right->addWidget(qLabel);
        }

        // Controls
        auto* controls = new QHBoxLayout();
        auto* pause = new QPushButton(tr("Pause"), card);
        auto* cancel = new QPushButton(tr("Cancel show"), card);
        connect(pause, &QPushButton::clicked, this, [this, showId = lane.showId]() {
            m_transferQueue->pauseCurrent(showId);
        });
        connect(cancel, &QPushButton::clicked, this, [this, showId = lane.showId]() {
            // Cancel each item in the lane in order
            auto lane = m_transferQueue->laneFor(showId);
            if (lane) {
                for (const auto& item : lane->items) {
                    m_transferQueue->cancel(item.transferId);
                }
            }
        });
        controls->addWidget(pause);
        controls->addWidget(cancel);
        controls->addStretch(1);
        right->addLayout(controls);

        layout->addLayout(right, 1);
        m_activeContainer->layout()->addWidget(card);
    }
}
```

- [ ] **Step 4: Implement `resolveShowPoster` / `resolveShowTitle` / `resolveProgress` helpers**

These read from existing caches:
- Poster: `CoreBridge::posterCache()` keyed by show ID
- Title: `CoreBridge::streamLibrary()` or similar; fallback to lane.items.front().displayTitle if missing
- Progress: query `TorrentClient::progressForInfohash(transferId)` (existing method; if missing, add a thin getter)

Implement each helper as a small private method on `StreamDownloadsPage`.

- [ ] **Step 5: Build + Hemanth smoke**

```
taskkill //F //IM Tankoban.exe && ./build_check.bat && build_and_run.bat
```

Hemanth: queue two shows in parallel. Open Theatre Downloads. Confirm: two cards visible, no filenames anywhere, poster + clean title + season + episode-by-number.

- [ ] **Step 6: Commit**

```
git commit -m "TANKORENT_QUALITY_AND_QUEUE T5.1: Netflix-clean one-card-per-show Downloads layout"
```

### Task 5.2: Drag-reorder + bump-to-front controls

**Files:**
- Modify: `src/ui/pages/stream/StreamDownloadsPage.cpp` (queue rendering inside each card)

- [ ] **Step 1: Render queued items as draggable rows instead of inline join**

Replace the `qLabel->join(" · ")` block with a small QListWidget per card configured for internal drag:

```cpp
if (lane.items.size() > 1) {
    auto* queueList = new QListWidget(card);
    queueList->setDragDropMode(QAbstractItemView::InternalMove);
    queueList->setStyleSheet("background: transparent; border: none;");
    for (size_t i = 1; i < lane.items.size(); ++i) {
        auto* row = new QWidget(queueList);
        auto* rowLay = new QHBoxLayout(row);
        rowLay->setContentsMargins(4, 2, 4, 2);
        auto* label = new QLabel(QString("Episode %1 · Queued")
                                   .arg(lane.items[i].episodeNumber.value_or(0)), row);
        label->setStyleSheet("color: #888; font-size: 9px;");
        auto* bump = new QPushButton(tr("Bump"), row);
        bump->setStyleSheet("font-size: 8px; padding: 1px 6px;");
        auto* x = new QPushButton(tr("X"), row);
        x->setStyleSheet("font-size: 8px; padding: 1px 6px;");
        const QString tid = lane.items[i].transferId;
        connect(bump, &QPushButton::clicked, this, [this, tid]() {
            m_transferQueue->bumpToFront(tid);
        });
        connect(x, &QPushButton::clicked, this, [this, tid]() {
            m_transferQueue->cancel(tid);
        });
        rowLay->addWidget(label, 1);
        rowLay->addWidget(bump);
        rowLay->addWidget(x);
        auto* item = new QListWidgetItem(queueList);
        item->setSizeHint(row->sizeHint());
        item->setData(Qt::UserRole, tid);
        queueList->setItemWidget(item, row);
    }

    // Drag-reorder hook: on rowsMoved, translate to TransferQueue::reorder
    connect(queueList->model(), &QAbstractItemModel::rowsMoved,
            this, [this, showId = lane.showId, queueList](
                const QModelIndex&, int sourceStart, int, const QModelIndex&, int destRow) {
        // queueList indices are 0-based for queued (lane index 1+)
        const int oldIdx = sourceStart + 1;
        const int newIdx = (destRow > sourceStart ? destRow - 1 : destRow) + 1;
        m_transferQueue->reorder(showId, oldIdx, newIdx);
    });

    right->addWidget(queueList);
}
```

- [ ] **Step 2: Build + Hemanth smoke**

Hemanth: queue 4 items in one show. Drag a queued item up/down — confirm the queue order persists across the next refresh. Click Bump on Episode 4 — it jumps to position 2.

- [ ] **Step 3: Commit**

```
git commit -m "TANKORENT_QUALITY_AND_QUEUE T5.2: drag-reorder + bump-to-front controls on Downloads cards"
```

### Task 5.3: Completed shows lean row

**Files:**
- Modify: `src/ui/pages/stream/StreamDownloadsPage.cpp` (refreshHistory)

- [ ] **Step 1: Replace existing refreshHistory body**

Use the existing `StreamDownloadIndex` (already in scope per the bridge work this wake). For each completed show, render a lean row:

```cpp
auto* row = new QWidget(m_historyContainer);
row->setStyleSheet("background: #1a1a1a; border: 1px solid #2a2a2a; border-radius: 6px;");
auto* lay = new QHBoxLayout(row);
lay->setContentsMargins(8, 6, 8, 6);

auto* poster = new QLabel(row);
poster->setFixedSize(32, 48);
poster->setPixmap(resolveShowPoster(showId));
poster->setScaledContents(true);
lay->addWidget(poster);

auto* col = new QVBoxLayout();
auto* title = new QLabel(resolveShowTitle(showId), row);
title->setStyleSheet("color: #eee; font-size: 11px; font-weight: 600;");
col->addWidget(title);
auto* meta = new QLabel(QString("Season %1 · %2 episodes")
                          .arg(seasonNum)
                          .arg(episodeCount), row);
meta->setStyleSheet("color: #666; font-size: 9px;");
col->addWidget(meta);
lay->addLayout(col, 1);

m_historyContainer->layout()->addWidget(row);
```

No file list, no torrent metadata, no progress bar.

- [ ] **Step 2: Build + Hemanth smoke + commit**

```
git commit -m "TANKORENT_QUALITY_AND_QUEUE T5.3: Netflix-clean Completed shows row (no torrent metadata)"
```

### Task 5.4: Phase 5 RTC

```
READY TO COMMIT — [Agent 4, TANKORENT_QUEUE_P5]: Theatre Downloads page rebuilt Netflix-style around per-show lanes. One card per active show, poster + official title + season + current episode progress + queued episode rows + Pause + Cancel show + drag-reorder + bump-to-front. Completed shows render as lean rows below (poster + title + season summary). NO torrent metadata anywhere on this page (filenames, release groups, codecs, quality strings all stripped). Files: src/ui/pages/stream/StreamDownloadsPage.{h,cpp}. Build: OK. Smoke: Hemanth-verified clean cards + working pause/cancel/reorder/bump on a 4-item lane.
```

---

## Phase 6: Other six indexer parity

**Each indexer = one task following the Phase 2 template. Mechanical fan-out.**

For each of these six files in `src/core/indexers/`:
- `PirateBayIndexer.cpp`
- `X1337xIndexer.cpp`
- `EztvIndexer.cpp`
- `YtsIndexer.cpp`
- `TorrentsCsvIndexer.cpp`
- `ExtTorrentsIndexer.cpp`

### Task 6.N (one per indexer)

**Files:**
- Modify: `src/core/indexers/<Name>Indexer.cpp`

- [ ] **Step 1: Audit drop logic**

```
grep -nE '(seeder|trusted|filter|skip|continue)' src/core/indexers/<Name>Indexer.cpp
```

- [ ] **Step 2: Strip seeder threshold + trust filter**

- [ ] **Step 3: Preserve dedupe; re-sort by seeders descending**

- [ ] **Step 4: Build + parity probe against source site**

For sources with a parity probe equivalent (1337x, PB), adapt `nyaa-parity-probe.ps1` to fetch from that site and compare. For sources with no scrape-friendly HTML (YTS API, TorrentsCSV API), compare API row count to indexer row count.

- [ ] **Step 5: Commit**

```
git commit -m "TANKORENT_QUALITY_AND_QUEUE T6.N: <Name>Indexer parity (strip filters, re-sort)"
```

### Task 6.7: Phase 6 RTC

```
READY TO COMMIT — [Agent 4, TANKORENT_QUEUE_P6]: Parity fix fanned out across the remaining six indexers (PirateBay, 1337x, EZTV, YTS, TorrentsCSV, ExtTorrents). Each: stripped seeder threshold + trust filter, kept dedupe, re-sort by seeders. Parity probes adapted per-source. Files: src/core/indexers/{PirateBay,X1337x,Eztv,Yts,TorrentsCsv,ExtTorrents}Indexer.cpp. Build: OK. Smoke: Hemanth-verified counts match source sites within slack.
```

---

## Out of scope (deferred to future arcs)

- Comics series-view Tankorent addon (Agent 1's future arc).
- Books series-view Tankorent addon (Agent 2's future arc).
- Comics + Books integration into `TransferQueue` — interface ships in Phase 1; A1/A2 plug in on their schedule.
- Cross-mode topbar download indicator.
- Pack detection signals beyond title heuristics (source metadata, post-download verification).
- Smart filter chip defaults (auto-pick by query content).

---

## Spec coverage self-check

| Spec section | Plan task(s) |
|--------------|--------------|
| Click-only search, source switch clears | T3.1 (clearResultsWithPrompt + onIndexerChanged), T3.2 (onSearchClicked), T4.3 (chip clears) |
| Filter chip default All | T4.3 (constructor initializes m_chip = All) |
| Title-heuristic season classifier (3 classes + Unclassified) | T4.1 |
| Pack episodes detected post-add, sorted by episode | T4.4 |
| Nyaa parity: strip filtering, keep dedupe, re-sort by seeders | T2.2 |
| Honest "N results from X" disclosure | T2.3, T3.2 |
| Other 6 indexer parity | T6.1-T6.6 |
| Per-show lanes, parallel across shows | T1.3-T1.5 |
| Lane key = show ID, standalone = own lane | T1.1 (showId field), T1.9 (showId passed through addTorrent) |
| Pack = one lane occupancy | Implicit: TorrentClient enqueues one TransferItem per torrent regardless of file count |
| Pause / cancel / reorder / bump | T1.4, T1.5, T5.2 |
| Tankorent as source-addon inside Theatre series view | T3.1-T3.3 |
| Standalone Tankorent tab survives | Untouched by this plan (no removal task) |
| Netflix-clean Downloads page (one card per show, no torrent metadata) | T5.1, T5.3 |
| Search page keeps raw titles | T4.2 (badge + raw title in addon rows) |
| No cross-mode topbar indicator | Explicitly deferred (out of scope) |
| Rollout phasing 1-6 | Plan structure matches |
