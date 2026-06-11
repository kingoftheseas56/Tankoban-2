# THEATRE_DOWNLOADS_OVERHAUL_V2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the read-only Theatre Downloads page with a Master–Detail command center (pause/resume/cancel/retry/bump/play per item), add a pack-first Season Checkout, instant click feedback, and a global 3-concurrent download cap.

**Architecture:** Three pure-logic cores land first under TDD (TransferQueue global cap, `EpisodeDisplayState::Queued`, `DownloadsCommandModel` aggregation), then the UI consumes them: the existing `StreamDownloadsPage` class is rebuilt in place (same MainWindow wiring/signals) around a left status-sectioned list + right `DownloadDetailPane` that re-hosts the Tankorent property tabs; `SeasonCheckoutPanel` is a new dialog driven by `UnifiedPackSearchEngine`. No new processes, no new deps.

**Tech Stack:** Qt6 Widgets/C++ (existing app), GoogleTest (`tankoban_tests`), libtorrent via existing `TorrentClient` (never touched directly by UI).

**Spec:** `docs/superpowers/specs/2026-06-11-theatre-downloads-overhaul-design.md` (decision log inside). Spec naming note: the spec's `DownloadsMasterDetailPage` is implemented as the rebuilt `StreamDownloadsPage` (same class/file, MainWindow wiring untouched).

**Build/test commands (agent4 lane):**
- Compile check: PowerShell `$env:TANKOBAN_BUILD_LANE='agent4'; & ".\build_check.bat"` → expect `BUILD OK`
- Tests: `out_agent4/tankoban_tests.exe --gtest_filter='<Filter>*'` (build target `tankoban_tests` first: `cmake --build out_agent4 --target tankoban_tests`)
- Rules: `taskkill //F //IM Tankoban.exe` before any build; never `git reset --hard`; commit after every task.

---

## Phase 1 — Engine: global concurrency cap

### Task 1: TransferQueue `setMaxActive` (TDD) + TorrentClient gate fix

The queue today: per-show lanes, sequential inside, **unbounded parallel across lanes**. Promotion to `Running` happens at 4 sites in `TransferQueue.cpp` (enqueue-into-empty-lane, finishCurrent-advance, resumeCurrent, cancel-current-advance). Add a global cap: at most `m_maxActive` items `Running` across all lanes (0 = unlimited, the default — preserves every existing test and today's behavior; MainWindow opts into 3). When a slot frees, promote the **oldest-enqueued eligible lane head**.

**Consumer contract hazard (load-bearing):** both TorrentClient enqueue sites treat `pos == 0` ("lane head") as "start immediately". Under a cap, lane-head ≠ Running. Both sites must instead ask the queue whether the item was actually promoted.

**Files:**
- Modify: `src/core/queue/TransferItem.h` (add `enqueueSeq`)
- Modify: `src/core/queue/TransferQueue.h`, `src/core/queue/TransferQueue.cpp`
- Modify: `src/core/torrent/TorrentClient.cpp` (~line 893 `addMagnetForShow` gate; ~line 3164 `startDownload` gate)
- Modify: `src/core/torrent/TorrentClient.h` (add `transferQueue()` accessor — Task 11 also needs it)
- Test: `tests/core/queue/test_transfer_queue.cpp` (extend; existing pattern uses `makeItem(tid, show, ep)` helper)

- [ ] **Step 1: Write the failing tests** — append to `tests/core/queue/test_transfer_queue.cpp`:

```cpp
TEST(TransferQueueCapTest, CapGatesThirdLane) {
    TransferQueue q;
    q.setMaxActive(2);
    q.enqueue(makeItem("t1", "imdb:tt0001", 1));
    q.enqueue(makeItem("t2", "imdb:tt0002", 1));
    q.enqueue(makeItem("t3", "imdb:tt0003", 1));   // lane head, but no slot
    EXPECT_EQ(q.runningCount(), 2);
    EXPECT_EQ(q.laneFor("imdb:tt0003")->items.front().state, TransferState::Queued);
}

TEST(TransferQueueCapTest, SlotFreePromotesOldestWaitingHead) {
    TransferQueue q;
    q.setMaxActive(1);
    q.enqueue(makeItem("t1", "imdb:tt0001", 1));
    q.enqueue(makeItem("t2", "imdb:tt0002", 1));   // waits (older)
    q.enqueue(makeItem("t3", "imdb:tt0003", 1));   // waits (newer)
    q.finishCurrent("imdb:tt0001", TransferState::Completed);
    EXPECT_EQ(q.laneFor("imdb:tt0002")->items.front().state, TransferState::Running);
    EXPECT_EQ(q.laneFor("imdb:tt0003")->items.front().state, TransferState::Queued);
}

TEST(TransferQueueCapTest, PauseFreesSlotForWaiter) {
    TransferQueue q;
    q.setMaxActive(1);
    q.enqueue(makeItem("t1", "imdb:tt0001", 1));
    q.enqueue(makeItem("t2", "imdb:tt0002", 1));
    q.pauseCurrent("imdb:tt0001");                 // paused ≠ running
    EXPECT_EQ(q.laneFor("imdb:tt0002")->items.front().state, TransferState::Running);
}

TEST(TransferQueueCapTest, ResumeWithNoSlotBecomesQueuedHead) {
    TransferQueue q;
    q.setMaxActive(1);
    q.enqueue(makeItem("t1", "imdb:tt0001", 1));
    q.enqueue(makeItem("t2", "imdb:tt0002", 1));
    q.pauseCurrent("imdb:tt0001");                 // t2 takes the slot
    EXPECT_FALSE(q.resumeCurrent("imdb:tt0001").has_value());
    EXPECT_EQ(q.laneFor("imdb:tt0001")->items.front().state, TransferState::Queued);
    q.finishCurrent("imdb:tt0002", TransferState::Completed);   // slot frees
    EXPECT_EQ(q.laneFor("imdb:tt0001")->items.front().state, TransferState::Running);
}

TEST(TransferQueueCapTest, RaisingCapPromotesWaiters) {
    TransferQueue q;
    q.setMaxActive(1);
    q.enqueue(makeItem("t1", "imdb:tt0001", 1));
    q.enqueue(makeItem("t2", "imdb:tt0002", 1));
    q.setMaxActive(3);
    EXPECT_EQ(q.runningCount(), 2);
}

TEST(TransferQueueCapTest, ZeroMeansUnlimited) {
    TransferQueue q;   // default 0
    for (int i = 0; i < 5; ++i)
        q.enqueue(makeItem(QStringLiteral("t%1").arg(i), QStringLiteral("imdb:tt%1").arg(i), 1));
    EXPECT_EQ(q.runningCount(), 5);
}

TEST(TransferQueueCapTest, CancelRunningFreesSlot) {
    TransferQueue q;
    q.setMaxActive(1);
    q.enqueue(makeItem("t1", "imdb:tt0001", 1));
    q.enqueue(makeItem("t2", "imdb:tt0002", 1));
    q.cancel("t1");
    EXPECT_EQ(q.laneFor("imdb:tt0002")->items.front().state, TransferState::Running);
}
```

- [ ] **Step 2: Run to verify they fail** — `cmake --build out_agent4 --target tankoban_tests && out_agent4/tankoban_tests.exe --gtest_filter='TransferQueueCap*'` → expect compile error (`setMaxActive` undefined): that IS the failing state.

- [ ] **Step 3: Implement.** `TransferItem.h` — add to the struct:

```cpp
    quint64 enqueueSeq = 0;   // stamped by TransferQueue::enqueue; global FIFO fairness
```

`TransferQueue.h` — add to public/private:

```cpp
public:
    // Global cap on simultaneously-Running items across ALL lanes.
    // 0 = unlimited (default; preserves pre-cap behavior). Raising the cap
    // immediately promotes eligible waiters, oldest enqueue first.
    void setMaxActive(int n);
    int  maxActive() const { return m_maxActive; }
    int  runningCount() const;

private:
    bool canPromote() const;        // running < cap (or unlimited)
    void promoteOldestEligible();   // promote Queued lane-heads while slots free
    int m_maxActive = 0;
    quint64 m_seqCounter = 0;
```

`TransferQueue.cpp` — the helpers:

```cpp
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
        for (auto& lane : m_lanes) {
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
```

Then touch the 5 existing sites:
1. **`enqueue`**: stamp `item.enqueueSeq = ++m_seqCounter;` first thing; the empty-lane promotion (`queued.state = TransferState::Running; emit ...`) becomes conditional on `canPromote()` — otherwise leave `Queued` (no Running emit; `laneChanged` still fires).
2. **`finishCurrent`**: replace the unconditional front-promotion (`it->items.front().state = Running; emit ...`) with `promoteOldestEligible();` after the pop/emit of the finished item (global, not just this lane).
3. **`pauseCurrent`**: after the Paused emit, add `promoteOldestEligible();` (paused frees a slot).
4. **`resumeCurrent`**: gate on `canPromote()`; if no slot → set head state `Queued`, `emit laneChanged(showId)`, return `std::nullopt` (it auto-promotes later).
5. **`cancel`**: where a cancelled current advances the lane (the existing front-promotion at ~L71-74) → remove the inline promotion, call `promoteOldestEligible()` instead (covers both cancel-of-running and cancel-of-queued: the helper no-ops if no slot/no waiter).

- [ ] **Step 4: Run tests** — `out_agent4/tankoban_tests.exe --gtest_filter='TransferQueue*'` → expect ALL pass (new cap tests + every pre-existing TransferQueueTest — default 0 keeps old behavior).

- [ ] **Step 5: Fix the two TorrentClient gates.** In `src/core/torrent/TorrentClient.cpp`:

Site A (`addMagnetForShow`, ~line 893) — replace:

```cpp
    const int pos = m_transferQueue->enqueue(item);
    if (pos == 0) {
        // Lane was empty — we are the new current. Start immediately.
        return addMagnetHeadless(magnetUri, category, destinationPath);
    }
```

with:

```cpp
    m_transferQueue->enqueue(item);
    // Under the global cap (DOWNLOADS_OVERHAUL_V2), lane-head no longer implies
    // Running — ask the queue whether we were actually promoted. If not, stage
    // the args; the setTransferQueue Running handler replays when a slot frees.
    const auto lane = m_transferQueue->laneFor(showId);
    const bool running = lane && !lane->items.empty()
        && lane->items.front().transferId == hash
        && lane->items.front().state == tankoban::queue::TransferState::Running;
    if (running) {
        return addMagnetHeadless(magnetUri, category, destinationPath);
    }
```

(the existing stage-the-args block below already handles the not-running case — it just no longer requires `pos > 0`.)

Site B (`startDownload`, ~line 3164) — replace:

```cpp
        const int pos = m_transferQueue->enqueue(item);
        if (pos > 0) {
            // Behind the current item — stage the config for replay.
            m_pendingStartConfigs.insert(
                hash, QSharedPointer<AddTorrentConfig>::create(config));
            return;
        }
        // pos == 0: we are the new lane head, fall through and run.
```

with:

```cpp
        m_transferQueue->enqueue(item);
        const auto lane = m_transferQueue->laneFor(item.showId);
        const bool running = lane && !lane->items.empty()
            && lane->items.front().transferId == hash
            && lane->items.front().state == tankoban::queue::TransferState::Running;
        if (!running) {
            // Not promoted (behind lane current OR gated by the global cap) —
            // stage the config; the Running handler replays it.
            m_pendingStartConfigs.insert(
                hash, QSharedPointer<AddTorrentConfig>::create(config));
            return;
        }
        // Promoted: we are running, fall through and start.
```

Also add to `TorrentClient.h` next to `setTransferQueue` (~line 140):

```cpp
    tankoban::queue::TransferQueue* transferQueue() const { return m_transferQueue; }
```

**Verify the replay handler is transition-driven:** read the `setTransferQueue` lambda (~`TorrentClient.cpp:814-880`) and confirm it replays staged configs/args on `itemStateChanged(id, Running)` regardless of which lane promoted — it was written for lane-advance; cap-promote emits the identical signal, so it should hold. If it filters by anything else, fix it to key only on (transferId, Running).

- [ ] **Step 6: Build + full test pass** — `build_check.bat` (agent4 lane) → `BUILD OK`; `out_agent4/tankoban_tests.exe` → all green (no filter — TorrentClient gate change can ripple).

- [ ] **Step 7: Commit**

```bash
git add src/core/queue/ src/core/torrent/TorrentClient.cpp src/core/torrent/TorrentClient.h tests/core/queue/test_transfer_queue.cpp
git commit -m "feat(queue): global max-active cap across lanes + TorrentClient promotion-aware gates (DOWNLOADS_OVERHAUL_V2 T1)"
```

---

## Phase 2 — State model

### Task 2: `EpisodeDisplayState::Queued` (TDD)

**Files:**
- Modify: `src/core/stream/EpisodeDisplayState.h`, `src/core/stream/EpisodeDisplayState.cpp`
- Test: `tests/core/stream/test_episode_display_state.cpp` (extend)

- [ ] **Step 1: Failing tests** — append:

```cpp
TEST(EpisodeDisplayStateTest, QueuedWhenRegisteredButNoTransfer) {
    tankostream::stream::EpisodeStateInputs in;
    in.queued = true;
    EXPECT_EQ(deriveEpisodeDisplayState(in),
              tankostream::stream::EpisodeDisplayState::Queued);
}

TEST(EpisodeDisplayStateTest, LiveTransferBeatsQueuedFlag) {
    tankostream::stream::EpisodeStateInputs in;
    in.queued = true;
    in.hasTransfer = true;
    in.progressPct = 12;
    EXPECT_EQ(deriveEpisodeDisplayState(in),
              tankostream::stream::EpisodeDisplayState::Downloading);
}

TEST(EpisodeDisplayStateTest, CompletedOnDiskBeatsQueued) {
    tankostream::stream::EpisodeStateInputs in;
    in.queued = true;
    in.onDisk = true;
    in.complete = true;
    EXPECT_EQ(deriveEpisodeDisplayState(in),
              tankostream::stream::EpisodeDisplayState::Downloaded);
}

TEST(EpisodeDisplayStateTest, QueuedBeatsBarePartialOnDisk) {
    tankostream::stream::EpisodeStateInputs in;
    in.queued = true;
    in.onDisk = true;       // pre-allocated partial, NOT complete
    EXPECT_EQ(deriveEpisodeDisplayState(in),
              tankostream::stream::EpisodeDisplayState::Queued);
}
```

- [ ] **Step 2: Run to fail** — `--gtest_filter='EpisodeDisplayState*'` → compile error on `Queued`/`queued`. Good.

- [ ] **Step 3: Implement.** Header: add `Queued,` to the enum after `NotDownloaded` with comment `// registered in TransferQueue, transfer not started -> "Queued" chip`; add `bool queued = false;  // in TransferQueue, not yet Running` to `EpisodeStateInputs`; update the priority comment to `(onDisk && complete) > failed > paused > downloading > queued > bare-onDisk > none`. Cpp — insert between the `hasTransfer` rule and the bare-onDisk rule:

```cpp
    if (in.queued)                   return EpisodeDisplayState::Queued;
```

- [ ] **Step 4: Run tests** — `--gtest_filter='EpisodeDisplayState*'` → all pass (new + existing 5+).

- [ ] **Step 5: Check exhaustive switches** — `grep -rn "EpisodeDisplayState::" src/ui/ src/core/ | grep -v "EpisodeDisplayState.h\|EpisodeDisplayState.cpp"` — any `switch` over the enum (episode-row painter in `StreamDetailView.cpp`) gets a `Queued` case: render the gray chip text `Queued` (no progress %, no Pause). Build will also flag `-Wswitch` sites.

- [ ] **Step 6: Build + commit**

```bash
git add src/core/stream/EpisodeDisplayState.* tests/core/stream/test_episode_display_state.cpp src/ui/pages/stream/StreamDetailView.cpp
git commit -m "feat(stream): Queued episode display state (DOWNLOADS_OVERHAUL_V2 T2)"
```

---

## Phase 3 — Aggregation model

### Task 3: `DownloadsCommandModel` pure builder (TDD)

One pure function turns the three data feeds into the sectioned row list the page renders. No QObject yet — that's Task 4.

**Files:**
- Create: `src/core/stream/DownloadsCommandModel.h`, `src/core/stream/DownloadsCommandModel.cpp`
- Modify: `cmake/TankobanSources.cmake` (add .cpp to SOURCES, .h to HEADERS, near the other `src/core/stream/` rows)
- Modify: `cmake/TankobanTests.cmake` (add test file + the model .cpp to the test target sources, mirroring how `test_episode_display_state.cpp` is registered)
- Test: `tests/core/stream/test_downloads_command_model.cpp` (new)

- [ ] **Step 1: Define the contract** (`DownloadsCommandModel.h`):

```cpp
#pragma once
// DOWNLOADS_OVERHAUL_V2 (2026-06-11) — pure aggregation for the Downloads
// command center. Inputs are plain snapshots (testable without TorrentClient);
// output is the status-sectioned, show-grouped row list the page renders.
#include "core/stream/StreamDownloadIndex.h"
#include "core/queue/TransferLane.h"
#include <QHash>
#include <QList>
#include <QString>

namespace tankostream::stream {

enum class DownloadSection { Failed, Active, Queued, Completed };

struct DownloadRow {
    QString imdbId;
    QString showTitle;        // enriched later by the page (meta cache); imdbId fallback
    QString type;             // "series" | "movie"
    int     season = 0;
    int     episode = 0;
    QString infoHash;         // carrying transfer (empty when none, e.g. old history)
    QString canonicalPath;    // for Play on Completed rows
    DownloadSection section = DownloadSection::Completed;
    int     pct = 0;
    bool    paused = false;
    qint64  addedAt = 0;      // Completed auto-trim key
};

struct DownloadsSnapshot {
    QList<StreamDownloadIndex::Entry>                 indexEntries;  // StreamDownloadIndex::all()
    QHash<QString, tankoban::queue::TransferLane>     lanes;         // TransferQueue::lanesSnapshot()
};

// Section rules (spec §3.1): Failed/Paused/Running from the lane item state of
// the episode's carrying transfer; index Pending with a Queued lane item (or no
// lane item yet) -> Queued; index Complete -> Completed, trimmed when older
// than maxCompletedAgeMs (0 = no trim). Rows sort: section order
// Failed→Active→Queued→Completed, then showTitle/imdbId, then season, episode.
QList<DownloadRow> buildDownloadRows(const DownloadsSnapshot& snap,
                                     qint64 nowMs,
                                     qint64 maxCompletedAgeMs);

}  // namespace tankostream::stream
```

- [ ] **Step 2: Failing tests** (`tests/core/stream/test_downloads_command_model.cpp`):

```cpp
#include <gtest/gtest.h>
#include "core/stream/DownloadsCommandModel.h"

using namespace tankostream::stream;
using tankoban::queue::TransferItem;
using tankoban::queue::TransferLane;
using tankoban::queue::TransferState;

namespace {
StreamDownloadIndex::Entry entry(const QString& imdb, int s, int e,
                                 StreamDownloadIndex::State st, int pct,
                                 qint64 addedAt = 1000) {
    StreamDownloadIndex::Entry x;
    x.imdbId = imdb; x.type = "series"; x.season = s; x.episode = e;
    x.state = st; x.progressPct = pct; x.addedAt = addedAt;
    x.canonicalPath = "C:/v/" + imdb + ".mkv";
    return x;
}
TransferLane lane(const QString& imdb, TransferState headState, int s, int e,
                  const QString& hash) {
    TransferLane l; l.showId = "imdb:" + imdb;
    TransferItem it; it.transferId = hash; it.showId = l.showId;
    it.seasonNumber = s; it.episodeNumber = e; it.state = headState;
    l.items.push_back(it);
    return l;
}
}  // namespace

TEST(DownloadsCommandModelTest, DownloadingEntryWithRunningLaneIsActive) {
    DownloadsSnapshot snap;
    snap.indexEntries = { entry("tt1", 1, 12, StreamDownloadIndex::State::Downloading, 62) };
    snap.lanes.insert("imdb:tt1", lane("tt1", TransferState::Running, 1, 12, "h1"));
    const auto rows = buildDownloadRows(snap, /*nowMs=*/0, /*trim=*/0);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].section, DownloadSection::Active);
    EXPECT_EQ(rows[0].pct, 62);
    EXPECT_EQ(rows[0].infoHash, "h1");
}

TEST(DownloadsCommandModelTest, FailedLaneItemIsFailedSection) {
    DownloadsSnapshot snap;
    snap.indexEntries = { entry("tt1", 1, 12, StreamDownloadIndex::State::Downloading, 30) };
    snap.lanes.insert("imdb:tt1", lane("tt1", TransferState::Failed, 1, 12, "h1"));
    const auto rows = buildDownloadRows(snap, 0, 0);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].section, DownloadSection::Failed);
}

TEST(DownloadsCommandModelTest, PendingWithQueuedLaneIsQueued) {
    DownloadsSnapshot snap;
    snap.indexEntries = { entry("tt1", 1, 13, StreamDownloadIndex::State::Pending, 0) };
    snap.lanes.insert("imdb:tt1", lane("tt1", TransferState::Queued, 1, 13, "h2"));
    const auto rows = buildDownloadRows(snap, 0, 0);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].section, DownloadSection::Queued);
}

TEST(DownloadsCommandModelTest, CompleteIsCompletedAndTrims) {
    DownloadsSnapshot snap;
    snap.indexEntries = {
        entry("tt1", 1, 11, StreamDownloadIndex::State::Complete, 100, /*addedAt=*/1000),
        entry("tt1", 1, 10, StreamDownloadIndex::State::Complete, 100, /*addedAt=*/100),
    };
    // now=2000, trim=500 -> the addedAt=100 entry is older than 500ms -> dropped
    const auto rows = buildDownloadRows(snap, 2000, 500);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].episode, 11);
    EXPECT_EQ(rows[0].section, DownloadSection::Completed);
}

TEST(DownloadsCommandModelTest, SectionOrderThenShowSeasonEpisode) {
    DownloadsSnapshot snap;
    snap.indexEntries = {
        entry("tt2", 1, 1, StreamDownloadIndex::State::Complete, 100),
        entry("tt1", 1, 2, StreamDownloadIndex::State::Downloading, 10),
        entry("tt1", 1, 1, StreamDownloadIndex::State::Downloading, 50),
    };
    snap.lanes.insert("imdb:tt1", lane("tt1", TransferState::Running, 1, 1, "h1"));
    const auto rows = buildDownloadRows(snap, 0, 0);
    ASSERT_EQ(rows.size(), 3);
    EXPECT_EQ(rows[0].section, DownloadSection::Active);   // tt1 e1 (running)
    EXPECT_EQ(rows[0].episode, 1);
    EXPECT_EQ(rows[1].section, DownloadSection::Active);   // tt1 e2 (downloading, no lane head match)
    EXPECT_EQ(rows[2].section, DownloadSection::Completed);
}
```

(If `StreamDownloadIndex::State` enum values differ — read `src/core/stream/StreamDownloadIndex.h:40-43` and use the real names; the entry struct was verified: `state` defaults `Complete`, `progressPct`, `addedAt`, `canonicalPath` all exist.)

- [ ] **Step 3: Run to fail** — compile error (header doesn't exist). Good.

- [ ] **Step 4: Implement `DownloadsCommandModel.cpp`:**

```cpp
#include "core/stream/DownloadsCommandModel.h"
#include <algorithm>

namespace tankostream::stream {

namespace {
// Find the lane item carrying (season, episode) for this show, if any.
const tankoban::queue::TransferItem* laneItemFor(
    const QHash<QString, tankoban::queue::TransferLane>& lanes,
    const QString& imdbId, int season, int episode)
{
    const auto it = lanes.constFind(QStringLiteral("imdb:") + imdbId);
    if (it == lanes.constEnd()) return nullptr;
    for (const auto& item : it->items) {
        const int s = item.seasonNumber.value_or(0);
        const int e = item.episodeNumber.value_or(0);
        // Season packs carry no episodeNumber: match on season alone then.
        if (s == season && (e == episode || !item.episodeNumber.has_value()))
            return &item;
    }
    return nullptr;
}
}  // namespace

QList<DownloadRow> buildDownloadRows(const DownloadsSnapshot& snap,
                                     qint64 nowMs, qint64 maxCompletedAgeMs)
{
    using tankoban::queue::TransferState;
    QList<DownloadRow> rows;
    rows.reserve(snap.indexEntries.size());

    for (const auto& e : snap.indexEntries) {
        DownloadRow r;
        r.imdbId = e.imdbId; r.showTitle = e.imdbId; r.type = e.type;
        r.season = e.season; r.episode = e.episode;
        r.canonicalPath = e.canonicalPath; r.pct = e.progressPct;
        r.addedAt = e.addedAt;

        const auto* li = laneItemFor(snap.lanes, e.imdbId, e.season, e.episode);
        if (li) r.infoHash = li->transferId;

        if (e.state == StreamDownloadIndex::State::Complete) {
            if (maxCompletedAgeMs > 0 && nowMs - e.addedAt > maxCompletedAgeMs)
                continue;   // display trim only — the index record stays
            r.section = DownloadSection::Completed;
        } else if (li && li->state == TransferState::Failed) {
            r.section = DownloadSection::Failed;
        } else if (li && li->state == TransferState::Paused) {
            r.section = DownloadSection::Active;
            r.paused = true;
        } else if (li && li->state == TransferState::Queued) {
            r.section = DownloadSection::Queued;
        } else if (e.state == StreamDownloadIndex::State::Downloading
                   || (li && li->state == TransferState::Running)) {
            r.section = DownloadSection::Active;
        } else {
            r.section = DownloadSection::Queued;   // Pending, lane not visible yet
        }
        rows.append(r);
    }

    std::stable_sort(rows.begin(), rows.end(),
        [](const DownloadRow& a, const DownloadRow& b) {
            if (a.section != b.section) return a.section < b.section;
            if (a.imdbId != b.imdbId)   return a.imdbId < b.imdbId;
            if (a.season != b.season)   return a.season < b.season;
            return a.episode < b.episode;
        });
    return rows;
}

}  // namespace tankostream::stream
```

- [ ] **Step 5: Register in CMake** (both files per the Files list), build the test target, run `--gtest_filter='DownloadsCommandModel*'` → all pass. **Verify the new .obj actually compiled** (new-source false-green trap): `find out_agent4 -name "DownloadsCommandModel.cpp.obj"`.

- [ ] **Step 6: Commit** — `git add src/core/stream/DownloadsCommandModel.* tests/core/stream/test_downloads_command_model.cpp cmake/ && git commit -m "feat(stream): DownloadsCommandModel pure aggregation (DOWNLOADS_OVERHAUL_V2 T3)"`

---

## Phase 4 — The page

### Task 4: Rebuild `StreamDownloadsPage` as Master–Detail shell

Keep the class name, file, ctor, injection API (`setTorrentClient` / `setStreamDownloadIndex` / `setMetaAggregator`) and existing signals (`backRequested`, `playLocalFileRequested`) — MainWindow wiring stays untouched. Replace the two-section read-only body with: top strip (placeholder buttons, filled in Task 7) + `QSplitter` (left `QTreeWidget`, right pane hosting `DownloadDetailPane` from Task 5).

**Files:**
- Modify: `src/ui/pages/stream/StreamDownloadsPage.h`, `src/ui/pages/stream/StreamDownloadsPage.cpp`

- [ ] **Step 1: Rebuild `buildUi()`** — top strip `QHBoxLayout` (`m_totalsLabel` left; `m_pauseAllBtn`, `m_resumeAllBtn`, `m_clearDoneBtn`, `m_maxActiveCombo` right — all created, wired in Task 7); below it a `QSplitter(Qt::Horizontal)`: left `m_tree = new QTreeWidget` (header hidden, 3 columns: title / progress text / status glyph), right `m_detailPane` (Task 5; until then a placeholder `QLabel("Select a download")`). Splitter stretch 2:3. Keep the page's existing dark styling pattern (copy the current file's stylesheet constants — gray/black/white only).

- [ ] **Step 2: Render from the model.** Replace `refreshActive()`/`refreshHistory()` with one `rebuild()`:

```cpp
void StreamDownloadsPage::rebuild()
{
    if (!m_index) return;
    tankostream::stream::DownloadsSnapshot snap;
    snap.indexEntries = m_index->all();
    if (m_client && m_client->transferQueue())
        snap.lanes = m_client->transferQueue()->lanesSnapshot();
    const auto rows = tankostream::stream::buildDownloadRows(
        snap, QDateTime::currentMSecsSinceEpoch(), kCompletedTrimMs);

    const QString selectedKey = currentSelectionKey();   // "imdb|s|e" of current item
    m_tree->clear();
    QTreeWidgetItem* sectionItems[4] = {};
    static const char* kSectionNames[] = {"Failed", "Active", "Queued", "Completed"};
    QHash<QString, QTreeWidgetItem*> showNodes;   // "section|imdb" -> show node

    for (const auto& r : rows) {
        const int s = int(r.section);
        if (!sectionItems[s]) {
            sectionItems[s] = new QTreeWidgetItem(m_tree, {tr(kSectionNames[s])});
            sectionItems[s]->setExpanded(true);
            sectionItems[s]->setFlags(Qt::ItemIsEnabled);
        }
        const QString showKey = QString::number(s) + '|' + r.imdbId;
        QTreeWidgetItem*& showNode = showNodes[showKey];
        if (!showNode) {
            showNode = new QTreeWidgetItem(sectionItems[s], {displayShowTitle(r.imdbId)});
            showNode->setExpanded(true);
            showNode->setFlags(Qt::ItemIsEnabled);
        }
        auto* item = new QTreeWidgetItem(showNode);
        item->setText(0, r.type == QLatin1String("movie")
            ? tr("Movie")
            : QStringLiteral("S%1E%2").arg(r.season, 2, 10, QLatin1Char('0'))
                                      .arg(r.episode, 2, 10, QLatin1Char('0')));
        item->setText(1, r.section == tankostream::stream::DownloadSection::Completed
            ? QString() : QStringLiteral("%1%").arg(r.pct));
        item->setText(2, statusGlyph(r));
        item->setData(0, Qt::UserRole, QVariant::fromValue(r));
        if (r.section == tankostream::stream::DownloadSection::Failed)
            item->setForeground(0, QBrush(QColor(0xf3, 0xa6, 0xa6)));
    }
    restoreSelection(selectedKey);
    updateTotals();   // Task 7 fills this; stub now
}
```

Supporting bits: `Q_DECLARE_METATYPE(tankostream::stream::DownloadRow)` after the struct (in `DownloadsCommandModel.h`); `kCompletedTrimMs = 30LL * 24 * 60 * 60 * 1000`; `displayShowTitle()` = existing meta-enrichment poster/title cache (the current page already fetches via `setMetaAggregator`/`fetchMetaItem` — keep that machinery, return imdbId until meta lands); `statusGlyph()` returns "x"/"%"/"…"/"✓"-free ASCII text per repo no-emoji rule: `failed` / `paused` / `` / `queued` / `done`; `currentSelectionKey()/restoreSelection()` round-trip the selected row through `imdbId|season|episode`.

- [ ] **Step 3: Re-point the triggers.** The current page refreshes on TorrentClient/Index signals — keep those connects but route to `rebuild()`; add `connect(queue, &TransferQueue::laneChanged, this, &StreamDownloadsPage::rebuild)` + `itemStateChanged` (coalesce with a 250ms single-shot `QTimer m_rebuildDebounce` so signal bursts repaint once — same debounce idiom as `kPieceProgressDebounceMs`).

- [ ] **Step 4: Selection → detail.** `connect(m_tree, &QTreeWidget::currentItemChanged, ...)` → if the item carries a `DownloadRow`, hand it to the detail pane (Task 5) / placeholder label.

- [ ] **Step 5: Build, manual sanity (page renders, sections populate from a live download), commit** — `git commit -m "feat(stream): Downloads page master-detail shell on DownloadsCommandModel (DOWNLOADS_OVERHAUL_V2 T4)"`

### Task 5: `DownloadDetailPane`

**Files:**
- Create: `src/ui/pages/stream/DownloadDetailPane.h`, `src/ui/pages/stream/DownloadDetailPane.cpp`
- Modify: `cmake/TankobanSources.cmake` (both lists), `StreamDownloadsPage.cpp` (replace placeholder)

- [ ] **Step 1: Header:**

```cpp
#pragma once
// DOWNLOADS_OVERHAUL_V2 — right pane of the Downloads command center. Renders
// one DownloadRow: header + numeric stats + controls + the reused Tankorent
// property tabs pointed at the row's carrying torrent. Emits intents only;
// StreamDownloadsPage routes them (this pane never touches TorrentClient
// mutators directly — read-only client use for the tabs).
#include "core/stream/DownloadsCommandModel.h"
#include <QWidget>

class TorrentClient;
class TorrentFilesTab;
class TorrentPeersTab;
class TorrentTrackersTab;
class QLabel;
class QProgressBar;
class QPushButton;
class QTabWidget;

class DownloadDetailPane : public QWidget {
    Q_OBJECT
public:
    explicit DownloadDetailPane(TorrentClient* client, QWidget* parent = nullptr);
    void setRow(const tankostream::stream::DownloadRow& row);
    void clearRow();   // "Select a download" empty state

signals:
    void pauseRequested(const tankostream::stream::DownloadRow& row);
    void resumeRequested(const tankostream::stream::DownloadRow& row);
    void cancelRequested(const tankostream::stream::DownloadRow& row);
    void retryRequested(const tankostream::stream::DownloadRow& row);
    void bumpRequested(const tankostream::stream::DownloadRow& row);
    void playRequested(const tankostream::stream::DownloadRow& row);

private:
    void refreshStats();   // 1s QTimer while visible + row has infoHash
    TorrentClient* m_client;
    tankostream::stream::DownloadRow m_row;
    bool m_hasRow = false;
    QLabel* m_title; QLabel* m_stats; QProgressBar* m_progress;
    QPushButton *m_pauseBtn, *m_resumeBtn, *m_cancelBtn, *m_retryBtn, *m_bumpBtn, *m_playBtn;
    QTabWidget* m_tabs;
    TorrentFilesTab* m_filesTab; TorrentPeersTab* m_peersTab; TorrentTrackersTab* m_trackersTab;
};
```

- [ ] **Step 2: Implement.** Ctor builds: title label (15px 600), `QProgressBar` (0-100), stats label (`speed · ETA · peers · size` — sourced each tick from the same TorrentClient snapshot the Tankorent General tab uses; `grep -n "downloadRate\|dlSpeed\|listActive" src/ui/pages/tankorent/TorrentGeneralTab.cpp` and reuse that exact accessor), a button row, then `m_tabs` with the three reused tabs constructed as `new TorrentFilesTab(m_client, this)` etc. `setRow()`: store, retitle (`showTitle · S01E12`), progress = pct, **button visibility by section** — Active: Pause(or Resume if paused)+Cancel · Queued: Bump+Cancel · Failed: Retry+Cancel · Completed: Play; call `setInfoHash(m_row.infoHash)` + `refresh()` on all three tabs (empty hash → tabs show their existing empty state); start/stop the 1s stats timer. Each button `connect` emits its intent with `m_row`. Gray/black/white styling only.

- [ ] **Step 3: Mount in the page** — replace the Task-4 placeholder: `m_detailPane = new DownloadDetailPane(m_client, splitter);` (construct after `setTorrentClient` — or take client lazily: add `void setClient(TorrentClient*)` called from the page's `setTorrentClient`). Selection handler calls `setRow/clearRow`.

- [ ] **Step 4: Build (verify new .obj compiled), run app, click around a live download, commit** — `git commit -m "feat(stream): DownloadDetailPane with reused Tankorent tabs (DOWNLOADS_OVERHAUL_V2 T5)"`

### Task 6: Intent wiring

**Files:**
- Modify: `src/ui/pages/stream/StreamDownloadsPage.{h,cpp}` (route intents)
- Modify: `src/ui/MainWindow.cpp` (~line 894 area — retry routing), `src/ui/MainWindow.h`
- Modify: `src/ui/pages/StreamPage.h`, `src/ui/pages/StreamPage.cpp` (public retry entry)

- [ ] **Step 1: Route in the page** (it has `m_client` + queue access):

```cpp
connect(m_detailPane, &DownloadDetailPane::pauseRequested, this, [this](const auto& r) {
    if (!m_client) return;
    if (!r.infoHash.isEmpty()) m_client->pauseTorrent(r.infoHash);
    if (auto* q = m_client->transferQueue()) q->pauseCurrent(QStringLiteral("imdb:") + r.imdbId);
});
connect(m_detailPane, &DownloadDetailPane::resumeRequested, this, [this](const auto& r) {
    if (!m_client) return;
    auto* q = m_client->transferQueue();
    // Queue decides if a slot is free; only resume the engine when promoted.
    if (q) { if (q->resumeCurrent(QStringLiteral("imdb:") + r.imdbId).has_value()
                 && !r.infoHash.isEmpty()) m_client->resumeTorrent(r.infoHash); }
    else if (!r.infoHash.isEmpty()) m_client->resumeTorrent(r.infoHash);
});
connect(m_detailPane, &DownloadDetailPane::cancelRequested, this, [this](const auto& r) {
    if (!m_client) return;
    if (auto* q = m_client->transferQueue()) q->cancel(r.infoHash);
    // keepFiles=false deletes partial staging; completed rows never offer Cancel.
    if (!r.infoHash.isEmpty()) m_client->deleteTorrent(r.infoHash, /*deleteFiles=*/true);
});
connect(m_detailPane, &DownloadDetailPane::bumpRequested, this, [this](const auto& r) {
    if (m_client && m_client->transferQueue()) m_client->transferQueue()->bumpToFront(r.infoHash);
});
connect(m_detailPane, &DownloadDetailPane::playRequested, this, [this](const auto& r) {
    emit playLocalFileRequested(r.canonicalPath, r.imdbId, displayShowTitle(r.imdbId),
                                r.season, r.episode);
});
connect(m_detailPane, &DownloadDetailPane::retryRequested, this, [this](const auto& r) {
    emit retryEpisodeRequested(r.imdbId, r.season, r.episode);   // new signal
});
```

Add `void retryEpisodeRequested(const QString& imdbId, int season, int episode);` to the page's signals. **Note:** when the queue's Running-promotion fires for a previously-gated item, the staged-config replay in TorrentClient starts the actual torrent — resume/bump need no extra engine calls for queued items.

- [ ] **Step 2: Retry routing.** MainWindow (next to the existing `playLocalFileRequested` connect for the downloads page, ~line 894): `connect(m_streamDownloadsPage, &StreamDownloadsPage::retryEpisodeRequested, this, [this](const QString& imdb, int s, int e) { if (m_streamPage) m_streamPage->retryEpisodeDownload(imdb, s, e); });` — find the actual member name via `grep -n "StreamDownloadsPage" src/ui/MainWindow.cpp`. StreamPage gets the public method:

```cpp
// DOWNLOADS_OVERHAUL_V2 T6 — re-run the auto source pick for a failed episode.
// The failed transfer was already cancelled/cleaned by the Downloads page.
void StreamPage::retryEpisodeDownload(const QString& imdbId, int season, int episode)
{
    startAutoDownload(imdbId, QStringLiteral("series"), season, episode, /*forStream=*/false);
}
```

Retry-with-no-sources lands in the existing "No 1080p source found" path → the row simply stays Failed (spec §6: no loops, no popups).

- [ ] **Step 3: Build, live-poke each control on a real download (pause flips section, cancel removes, bump reorders lane snapshot, play opens player), commit** — `git commit -m "feat(stream): Downloads page intent wiring — pause/resume/cancel/retry/bump/play (DOWNLOADS_OVERHAUL_V2 T6)"`

### Task 7: Top strip — totals, global controls, max-active knob

**Files:**
- Modify: `src/ui/pages/stream/StreamDownloadsPage.{h,cpp}`
- Modify: `src/ui/MainWindow.cpp` (~line 791, after `torrentClient->setTransferQueue(m_transferQueue);`)

- [ ] **Step 1: Totals.** `updateTotals()` on the rebuild debounce + a 1s timer while the page is visible: `N active · X MB/s` — aggregate download rate from the same TorrentClient snapshot accessor the detail pane stats use; active count = `queue->runningCount()`.

- [ ] **Step 2: Global controls.** `Pause All` = for each lane in `lanesSnapshot()` with a Running head: `client->pauseTorrent(head.transferId); queue->pauseCurrent(lane.showId);`. `Resume All` = for each Paused head: `queue->resumeCurrent(showId)` and engine-resume only the promoted ones (cap still applies). `Clear Done` = `m_clearDoneBeforeMs = QDateTime::currentMSecsSinceEpoch()` (a member the rebuild passes as the trim floor: rows with `addedAt < m_clearDoneBeforeMs` AND Completed are dropped) — persisted via `QSettings("downloads/clearDoneBeforeMs")` so it sticks.

- [ ] **Step 3: Max-active knob.** `m_maxActiveCombo` items `1 / 2 / 3 / 5 / Unlimited` (data 1/2/3/5/0), current from `QSettings` key `downloads/maxActive` (default **3**); on change: write the setting + `m_client->transferQueue()->setMaxActive(v)`. MainWindow startup (line ~791, right after `setTransferQueue`):

```cpp
    // DOWNLOADS_OVERHAUL_V2 T7 — global concurrent-download cap (spec: 3 default).
    m_transferQueue->setMaxActive(
        QSettings().value(QStringLiteral("downloads/maxActive"), 3).toInt());
```

- [ ] **Step 4: Build, verify knob change visibly gates a 4th download, commit** — `git commit -m "feat(stream): Downloads top strip — totals, pause/resume all, clear done, max-active knob (DOWNLOADS_OVERHAUL_V2 T7)"`

---

## Phase 5 — Season Checkout (pack-first)

### Task 8: `SeasonCheckoutPanel`

**Files:**
- Create: `src/ui/pages/stream/SeasonCheckoutPanel.h`, `src/ui/pages/stream/SeasonCheckoutPanel.cpp`
- Modify: `cmake/TankobanSources.cmake` (both lists)

- [ ] **Step 1: Header:**

```cpp
#pragma once
// DOWNLOADS_OVERHAUL_V2 — pack-first Season Checkout (spec §3.2). Modal dialog
// (spec open-item resolved: modal, matching AddTorrentDialog's pattern).
// Caller feeds pack candidates + per-episode gap picks as they resolve; the
// panel renders the plan; Queue all emits one CheckoutPlan. Nothing downloads
// until that click.
#include "core/stream/UnifiedPackSearchEngine.h"
#include <QDialog>
#include <QList>
#include <QSet>

class QLabel; class QListWidget; class QPushButton; class QVBoxLayout;

namespace tankostream::stream {

struct CheckoutPlan {
    bool       usePack = false;
    QString    packMagnet;          // when usePack
    QString    packTitle;
    QList<int> gapEpisodes;         // per-episode auto-pick downloads
};

class SeasonCheckoutPanel : public QDialog {
    Q_OBJECT
public:
    // episodes: every episode number of the season; owned: already-downloaded
    // (greyed + excluded); preselected: empty = whole season (Download Selected
    // passes the ticked subset).
    SeasonCheckoutPanel(const QString& imdbId, const QString& showTitle, int season,
                        const QList<int>& episodes, const QSet<int>& owned,
                        const QList<int>& preselected, QWidget* parent = nullptr);

    // Called by StreamPage as results arrive.
    void setPackCandidates(const QList<tankoban::stream::theatre::EnrichedPack>& packs);
    void setSearchFailed(const QString& message);   // degrade to all-gap mode

signals:
    void queueAllRequested(const CheckoutPlan& plan);

private:
    void rebuildPlanRows();   // coverage math: selected pack covers episode N?
    void updateFooter();      // "N episodes · X GB" + enable Queue all
    QString m_imdbId, m_showTitle; int m_season;
    QList<int> m_wanted;      // requested minus owned
    QSet<int>  m_owned;
    QList<tankoban::stream::theatre::EnrichedPack> m_packs;
    int m_selectedPack = -1;  // -1 = no pack (all per-episode)
    QListWidget* m_packList; QListWidget* m_planList;
    QLabel* m_footer; QPushButton* m_queueAllBtn;
};

}  // namespace tankostream::stream
```

- [ ] **Step 2: Implement.** Ctor: `m_wanted` = (preselected non-empty ? preselected : episodes) minus `owned`; layout = header (`Download Season N — <show>`), `m_packList` ("Season packs" label; starts with a single disabled "Searching packs…" row), `m_planList` (the per-episode plan), footer + `Queue all` (disabled until search settles). `setPackCandidates`: sort by `combinedScore` desc, render top 5 as `"%1 · %2 · ↑%3"` (raw.title, human size, seeders) + a coverage tag from `classification`: `isCompleteSeries || detectedSeasons.contains(m_season)` → "covers this season" → coverage = all of `m_wanted`; otherwise "partial/unknown" → coverage = none (conservative: packs that don't clearly cover the season contribute nothing; per-episode fills the rest). First candidate default-selected; selecting a row (or the explicit "No pack — per-episode only" row appended last) calls `rebuildPlanRows()`. `rebuildPlanRows`: for each episode in `m_wanted` → "E12 — covered by pack" or "E12 — best single-episode source (auto)"; owned episodes render greyed "`E11 — already have`" (display only). `updateFooter`: episode count + summed `raw.sizeBytes` of the pack (gap sizes unknown pre-resolve → footer says "+ N singles"). `Queue all` → emit `queueAllRequested({usePack, pack.raw.magnetUri, pack.raw.title, gaps})` then `accept()`. **Field-name check before coding:** `grep -n "magnetUri\|magnet\|sizeBytes\|title\|seeders" src/core/TorrentResult.h` — use the actual `TorrentResult` member names everywhere `raw.` appears.

- [ ] **Step 3: Build (verify .obj), commit** — `git commit -m "feat(stream): SeasonCheckoutPanel — pack-first checkout dialog (DOWNLOADS_OVERHAUL_V2 T8)"`

### Task 9: Wire checkout into StreamPage + dispatch

**Files:**
- Modify: `src/ui/pages/StreamPage.h`, `src/ui/pages/StreamPage.cpp`

- [ ] **Step 1: Open the checkout.** `onSeasonDownloadRequested(int season)` (currently → `triggerBulkSelectedEpisodes(imdb, season, {})`, StreamPage.cpp:~3096) and `onSelectedEpisodesDownloadRequested(season, episodes)` both route to a new `openSeasonCheckout(season, preselected)`:

```cpp
void StreamPage::openSeasonCheckout(int season, const QList<int>& preselected)
{
    using namespace tankostream::stream;
    if (!m_detailView || m_detailView->currentImdb().isEmpty()) return;
    const QString imdbId = m_detailView->currentImdb();

    QList<int> allEps;
    for (const auto& ep : m_detailView->episodesForSeason(season)) allEps.append(ep.episode);
    QSet<int> owned;
    if (m_streamDownloadIndex)
        for (int e : allEps)
            // bestEntryForEpisode-style ownership: Complete entry exists
            if (auto entry = m_streamDownloadIndex->entryFor(imdbId, season, e);
                entry && entry->state == StreamDownloadIndex::State::Complete)
                owned.insert(e);
    // ^ verify the real lookup name: grep -n "entryFor\|bestEntryForEpisode" src/core/stream/StreamDownloadIndex.h — use what exists.

    auto* panel = new SeasonCheckoutPanel(imdbId, m_detailView->currentTitle(), season,
                                          allEps, owned, preselected, this);
    panel->setAttribute(Qt::WA_DeleteOnClose);
    connect(panel, &SeasonCheckoutPanel::queueAllRequested, this,
            [this, imdbId, season](const CheckoutPlan& plan) {
                executeCheckoutPlan(imdbId, season, plan);
            });

    // Pack search: reuse the existing engine instance (m_unifiedPackSearchEngine).
    connect(m_unifiedPackSearchEngine, &UnifiedPackSearchEngine::packResults, panel,
            [panel, imdbId, season](const QString& id, int s, const auto& packs) {
                if (id == imdbId && s == season) panel->setPackCandidates(packs);
            });
    connect(m_unifiedPackSearchEngine, &UnifiedPackSearchEngine::searchComplete, panel,
            [panel, imdbId, season](const QString& id, int s, int total) {
                if (id == imdbId && s == season && total == 0)
                    panel->setSearchFailed(tr("No season packs found"));
            });
    m_unifiedPackSearchEngine->search(imdbId, m_detailView->currentTitle(), season);
    panel->show();
}
```

(connects use `panel` as context object → auto-disconnect on dialog close.)

- [ ] **Step 2: Execute the plan:**

```cpp
void StreamPage::executeCheckoutPlan(const QString& imdbId, int season,
                                     const tankostream::stream::CheckoutPlan& plan)
{
    if (m_detailView) m_detailView->autoAddToLibrary();
    const QStringList roots = m_bridge ? m_bridge->rootFolders(QStringLiteral("videos"))
                                       : QStringList();
    if (roots.isEmpty() || roots.first().isEmpty()) {
        if (m_detailView)
            m_detailView->setStreamSourcesError(tr("Videos library root is not configured"));
        return;
    }
    if (plan.usePack && !plan.packMagnet.isEmpty() && m_torrentClient) {
        m_torrentClient->addMagnetForShow(plan.packMagnet, QStringLiteral("videos"),
                                          roots.first(), imdbId, season);
    }
    for (int ep : plan.gapEpisodes)
        startAutoDownload(imdbId, QStringLiteral("series"), season, ep, /*forStream=*/false);
}
```

(Pack episodes' per-episode progress into `StreamDownloadIndex` flows through the existing `processPieceFinishedProgress` pack-parse path — that's exactly what it was built for. The old `triggerBulkSelectedEpisodes` machinery stays for any remaining caller; `grep -n "triggerBulkSelectedEpisodes" src/ui/pages/StreamPage.cpp` — if these two handlers were its only triggers, mark it dead-code-candidate in the commit message, don't delete yet.)

- [ ] **Step 3: Build, live-smoke (Download Season on a real show → panel shows packs → Queue all → rows land Queued/Active on the Downloads page), commit** — `git commit -m "feat(stream): season checkout wired — pack-first dispatch + per-episode gaps (DOWNLOADS_OVERHAUL_V2 T9)"`

---

## Phase 6 — Click feedback

### Task 10: `Queued ✓`-morph on every Download affordance + row flip

**Files:**
- Modify: `src/ui/pages/stream/StreamSourceCard.cpp` (Download button morph)
- Modify: `src/ui/pages/stream/StreamDetailView.cpp` (episode-row affordance morph + `queued` input into the row painter)
- Modify: `src/ui/pages/StreamPage.cpp` (feed lane state into the detail view's episode-state gatherer)

- [ ] **Step 1: Source-card Download morph** — in the Task-P2 `downloadBtn` connect (StreamSourceCard.cpp), before emitting: `downloadBtn->setText(tr("Queued"));  downloadBtn->setEnabled(false);` (text only — gray palette rule; no glyph needed beyond the disabled state).

- [ ] **Step 2: Episode-row affordance + state.** The detail view's episode-state gatherer builds `EpisodeStateInputs` (grep `EpisodeStateInputs` in `StreamDetailView.cpp` / `StreamPage.cpp` for the fill site). Set the new flag there: `in.queued = <lane item exists for (imdb, season, ep) with state Queued>` — source it from `m_torrentClient->transferQueue()->lanesSnapshot()` via the same `laneItemFor` matching logic as `DownloadsCommandModel.cpp` (export that helper from the model header as `const TransferItem* laneItemFor(...)` instead of keeping it file-static — adjust Task 3's anonymous namespace accordingly). The Task-2 painter case then renders the `Queued` chip. The per-row download action button (episode table action column) disables when state is `Queued/Downloading`.

- [ ] **Step 3: Immediate flip.** The flip must not wait for source resolution: at the click sites (`onSingleEpisodeDownloadRequested`, checkout `Queue all`, source-card download → `onDirectDownloadRequested`) call the existing `m_streamDownloadIndex->registerPendingEpisode(imdbId, season, ep, ...)` if-and-only-if it isn't already called on that path (`grep -n "registerPendingEpisode" src/ui/pages/StreamPage.cpp` first — the bulk path already does this; mirror its arg shape). Pending entry + index-changed signal → row repaints as Queued instantly.

- [ ] **Step 4: Build, live-verify: click Download on a source → button greys to "Queued", episode row chips to Queued within a frame; commit** — `git commit -m "feat(stream): instant Queued feedback on every download affordance (DOWNLOADS_OVERHAUL_V2 T10)"`

---

## Phase 7 — Ship gates

### Task 11: Verification, reviews, smoke

- [ ] **Step 1: Full build both lanes** — `build_check.bat` (agent4) → `BUILD OK`; verify every new `.obj` exists (`DownloadsCommandModel`, `DownloadDetailPane`, `SeasonCheckoutPanel`).
- [ ] **Step 2: Full test suite** — `out_agent4/tankoban_tests.exe` → ALL green; paste the `[ PASSED ]` line into the RTC (never claim green unread).
- [ ] **Step 3: `/security-review`** — stream/torrent surfaces touched (magnet dispatch, file deletion via Cancel) → run the skill; fix or document findings.
- [ ] **Step 4: Cross-engine review** — package diff + this plan's DoD via `/codex-review` (`python scripts/engines/engine.py review …`); producer≠reviewer; apply findings; rebuild.
- [ ] **Step 5: Hemanth smoke checklist** (maps to spec acceptance):
  1. Open Downloads page with ≥4 queued items → only 3 run; 4th says Queued; finish one → 4th starts.
  2. Pause / Resume / Cancel / Bump from the detail pane all act; Files/Peers/Trackers tabs populate.
  3. Kill a tracker / use a dead magnet → row turns Failed (red, top) → Retry re-picks and re-queues.
  4. Completed row → Play plays it; Clear Done empties the section.
  5. Download Season → checkout shows packs + gaps + owned-greyed → Queue all → one batch lands.
  6. Click any Download button → it greys to "Queued" instantly; episode row chips Queued.
- [ ] **Step 6: Commit ledger + RTC** — post the contracts-v3 RTC line in `agents/chat.md` with `Skills invoked:` provenance; push.

---

## Self-review notes (already applied)

- **Spec coverage:** §3.1 page → T4-T7; §3.2 checkout → T8-T9; §3.3 click feedback → T10; §3.4 queue → T1+T7; §4 `Queued` state → T2; model → T3; §6 error paths → T6 retry + T8 `setSearchFailed`; §7 testing → T1/T2/T3 TDD + T11. Batch aggregate-state rule (spec §3.1): v1 renders pack children as individual episode rows whose section derives per-row — the batch-row presentation is satisfied by the show-group node; if Hemanth wants a dedicated batch row with the red child-count badge, it's a T4 follow-up flagged at smoke.
- **Known judgment calls:** Cancel deletes partial files (`deleteFiles=true`) — staging cleanup; flagged for review. Queue-cap default stays 0 in core (back-compat), 3 applied at MainWindow per settings.
- **Verify-before-use markers:** `StreamDownloadIndex` lookup name (T9), `TorrentResult` field names (T8), TorrentGeneralTab speed accessor (T5), `registerPendingEpisode` arg shape (T10) — each has an explicit grep step in its task.
