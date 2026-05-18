# STREAM_DOWNLOADS_NETFLIX_OVERHAUL Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Excise Tankorent from the user's stream-download flow; inline per-row + per-season-header trigger/control affordances on `StreamDetailView`; new sidebar-drawer "Downloads" entry routing to a new `StreamDownloadsPage`; new `DOWNLOADING` tile chip parallel to Layer 3's `DOWNLOADED`; preserve libtorrent engine + cohort scheduler + canonical naming pipeline unchanged.

**Architecture:** Render-layer overhaul on top of preserved engine and persistence substrate. Zero new persistence files. TankorentPage gains a one-line filter on `streamGroupId`; StreamDetailView gains a checkbox column + a morphing-icon column with state-driven dispatch; StreamLibraryLayout gains a sibling `DOWNLOADING` chip; a brand-new `StreamDownloadsPage` reads existing `stream_bulk_groups.json` with extended 90-day TTL; a SidebarDrawer entry routes to it. The existing `StreamBulkPreflightDialog` from V2 Phase 1 is deleted. TorrentClient gains four small additions (one signal, one constant, two query methods, two API extensions); no engine algorithm changes.

**Tech Stack:** Qt 6.10 / C++20. `QTableWidget`, `QCheckBox`, `QPushButton` (flat icon-only), `QHBoxLayout`. libtorrent unchanged. JSON persistence unchanged. `feedback_no_color_no_emoji.md` discipline preserved throughout (grayscale, no emoji, no color accents).

---

## Spec source

`docs/superpowers/specs/2026-05-12-stream-downloads-netflix-overhaul-design.md` (brainstorm 2026-05-12, awaiting Hemanth review at plan-fire time but with all 8 product calls P1–P8 ratified during the brainstorm session). All section references in this plan (`§3 / §5.2 / §7.1` etc.) refer to that spec.

## Pre-flight: in-flight uncommitted carry-through (read once)

This plan executes against a working tree that contains UNCOMMITTED hotfix work from the 2026-05-12 wake. All of it is RTC'd in `agents/chat.md` as #30 / #31 / #32 and will be swept by Agent 0 alongside (or before) this plan's executing-plans phase. The executor must NOT undo or rewrite these in-flight pieces:

- `src/core/torrent/TorrentClient.{h,cpp}` — `setStreamDownloadIndex` body moved from header-inline to .cpp + new `backfillStreamDownloadIndex()` method (RTC #31).
- `src/ui/MainWindow.cpp` — `m_streamDownloadIndex = new StreamDownloadIndex(...)` hoist to before `buildPageStack()` (RTC #32).
- `src/ui/pages/stream/StreamLibraryLayout.cpp` — DOWNLOADED chip cosmetic tidy (border-radius 3→4, padding 1×5→3×7, position 8→10) (RTC #30).
- `src/ui/pages/stream/StreamDetailView.cpp` — diagnostic `DebugLogBuffer::instance().info(...)` lines in `onEpisodeActivated` + `#include "core/DebugLogBuffer.h"` (RTC #30 diagnostic).

The diagnostic instrumentation is no longer needed once this plan ships (visual behavior will be enough evidence). **Task 25** removes those four diagnostic lines + the include.

## Brotherhood convention — "Commit" step in this plan

This plan does NOT do per-task `git commit`. Agent 0 sweeps RTCs from `agents/chat.md`. Each task's "Commit" step in this plan is interpreted as:

1. Run `build_check.bat` and confirm `BUILD OK` is in the tail.
2. Append a single RTC line to `agents/chat.md` matching the contract in `agents/CONTRACTS.md` (`READY TO COMMIT - [Agent 4, ...] | Skills invoked: [...] | files: ...`).
3. Do NOT run `git add` or `git commit` — Agent 0 sweeps the batch.

There is ONE exception: at the very end (Task 28), a single closing RTC summarizes the whole overhaul.

The plan still works as a sequenced unit; intermediate RTCs are not strictly required between every task. **Convention:** RTC per **phase** (10 RTCs total, one per phase closeout), not per task. Tasks within a phase build incrementally and the build_check verification happens at each task; the RTC line lands at phase-close.

---

## File Structure

| Path | Action | Responsibility |
|---|---|---|
| `src/core/torrent/TorrentClient.h` | Modify | Declarations for new APIs + signal + state constant |
| `src/core/torrent/TorrentClient.cpp` | Modify | kStatePaused + transitions + new APIs + TTL bump + signal emits |
| `src/ui/pages/TankorentPage.cpp` | Modify | One-line filter on `streamGroupId.isNotEmpty()` |
| `src/ui/pages/stream/StreamDetailView.h` | Modify | Column count + new members (checkbox cells, m_selectedEpisodes, m_downloadSeasonBtn, m_downloadSelectedBtn) |
| `src/ui/pages/stream/StreamDetailView.cpp` | Modify | Checkbox column + action-icon column + state-derivation + season header + context menu + remove diagnostic lines |
| `src/ui/pages/stream/StreamLibraryLayout.h` | Modify | DOWNLOADING-chip member |
| `src/ui/pages/stream/StreamLibraryLayout.cpp` | Modify | DOWNLOADING chip + streamBulkGroupsChanged subscription |
| `src/ui/pages/stream/StreamDownloadsPage.h` | Create | New full-page class |
| `src/ui/pages/stream/StreamDownloadsPage.cpp` | Create | Active/History sections + show cards + subscriptions |
| `src/ui/pages/StreamPage.h` | Modify | TorrentClient* getter + slot rename |
| `src/ui/pages/StreamPage.cpp` | Modify | Direct dispatch (no preflight); rename slot |
| `src/ui/widgets/SidebarDrawer.h` | Modify | New entry registration |
| `src/ui/widgets/SidebarDrawer.cpp` | Modify | Render new entry |
| `src/ui/MainWindow.h` | Modify | New page member + slot rename |
| `src/ui/MainWindow.cpp` | Modify | Construct StreamDownloadsPage, add to stack, wire sidebar, rename slot |
| `src/core/stream/StreamLibrary.cpp` | Modify | `remove(imdb)` cancels active cohorts for that imdb |
| `src/ui/dialogs/StreamBulkPreflightDialog.h` | Delete | V2 Phase 1 surface superseded |
| `src/ui/dialogs/StreamBulkPreflightDialog.cpp` | Delete | V2 Phase 1 surface superseded |
| `resources/icons/downloads.svg` | Create | Sidebar entry icon |
| `resources/icons/download-arrow.svg` | Create | Per-row [↓] trigger glyph (or reuse if exists) |
| `resources/icons/pause-circle.svg` | Create | Per-row [⏸] glyph |
| `resources/icons/play-circle.svg` | Create | Per-row [▶] glyph |
| `resources/icons/retry-arrow.svg` | Create | Per-row [↻] glyph |
| `resources/resources.qrc` | Modify | Register new icons |
| `CMakeLists.txt` | Modify | +StreamDownloadsPage.{h,cpp}, −StreamBulkPreflightDialog.{h,cpp} |

---

## Phase 1 — TorrentClient engine substrate

Goal: land the engine-side substrate (constants, APIs, signals, TTL) BEFORE any UI work so the UI tasks have everything they need to call.

Phase RTC tag at close: `Agent 4, STREAM_DOWNLOADS_NETFLIX_OVERHAUL P1 engine substrate`.

### Task 1: Add `kStatePaused` constant + state predicates + enum extension

**Files:**
- Modify: `src/core/torrent/TorrentClient.h:30-60` (StreamBulkItemState enum) and `src/core/torrent/TorrentClient.cpp:22-104` (constants + helper functions)

- [ ] **Step 1: Add `kStatePaused` constant in the file-scope anonymous namespace.**

Open `src/core/torrent/TorrentClient.cpp`. Find the constant block at lines 22–32 (`kStatePending`, `kStateDownloading`, ..., `kStateOrphaned`). Add at the end of that block:

```cpp
constexpr const char* kStatePaused = "Paused";
```

- [ ] **Step 2: Add `Paused` to the `StreamBulkItemState` enum in the header.**

Open `src/core/torrent/TorrentClient.h`. Find the `enum class StreamBulkItemState` (the enum that contains `Pending`, `Downloading`, `Publishing`, `Published`, `MissingSource`, `MetadataFailed`, `PublishFailed`, `Failed`, `Completed`, `Cancelled`, `Orphaned`). Add `Paused` to the enum — alphabetical order doesn't matter; append at end after `Orphaned`:

```cpp
enum class StreamBulkItemState {
    Pending,
    Downloading,
    Publishing,
    Published,
    MissingSource,
    MetadataFailed,
    PublishFailed,
    Failed,
    Completed,
    Cancelled,
    Orphaned,
    Paused,
};
```

- [ ] **Step 3: Extend `streamBulkItemStateToString` to cover `Paused`.**

In `TorrentClient.cpp`, find `streamBulkItemStateToString` (around line 35–50). Add a `case` for `Paused`:

```cpp
case StreamBulkItemState::Paused:      return QStringLiteral("Paused");
```

- [ ] **Step 4: Extend `streamBulkItemStateFromString` to recognize `Paused`.**

In the same file, find `streamBulkItemStateFromString` (around line 52–65). Add a check above the final fallback:

```cpp
if (state == QLatin1String(kStatePaused))    return StreamBulkItemState::Paused;
```

- [ ] **Step 5: Confirm `isTerminalStreamBulkState` does NOT include `Paused`.**

Read lines 67–77 of `TorrentClient.cpp`. The function should NOT contain a check for `kStatePaused`. If a future hand has added one, remove it. Paused is a control state, not terminal — the cohort scheduler must continue to hold the slot.

- [ ] **Step 6: Build verify.**

Run `taskkill //F //IM Tankoban.exe 2>nul || true; powershell -NoProfile -File scripts/stop-tankoban.ps1; .\build_check.bat 2>&1 | Select-Object -Last 5`. Expected last line: `BUILD OK`.

### Task 2: Add `streamBulkGroupsChanged` signal

**Files:**
- Modify: `src/core/torrent/TorrentClient.h:220-230` (signals block)
- Modify: `src/core/torrent/TorrentClient.cpp` (signal emit sites)

- [ ] **Step 1: Declare the signal in TorrentClient.h.**

Open `src/core/torrent/TorrentClient.h`. Find the `signals:` block (currently has `torrentAdded`, `torrentUpdated`, `torrentRemoved`, `torrentCompleted`, `groupPublishComplete`, `streamBulkRetrySourcePickRequested`). Add a new signal after `groupPublishComplete`:

```cpp
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — emitted on any cohort-item state
    // transition (Pending→Downloading→Publishing→Published, or any failure-
    // state edge, or user-paused transition). Subscribers (StreamLibraryLayout
    // tile chips, StreamDownloadsPage cards, StreamDetailView rows) repaint
    // their derived state. groupId identifies which group changed; subscribers
    // who care about a specific imdb walk groups themselves via
    // imdbHasActiveCohort or streamBulkGroupsSnapshot.
    void streamBulkGroupsChanged(const QString& groupId);
```

- [ ] **Step 2: Emit at every state-mutation site.**

In `src/core/torrent/TorrentClient.cpp`, find every function that writes `item["itemState"] = ...` or assigns to a cohort item's state field and follows with a `saveStreamBulkGroups()` call. Add `emit streamBulkGroupsChanged(groupId);` after `saveStreamBulkGroups()` at each of these sites:

- `markStreamBulkItemsForTorrent` — after the existing `saveStreamBulkGroups()` call near function end. Replace the existing emit pattern (if it emits `torrentUpdated`) with both.
- `publishStreamBulkItemsForTorrent` — at the end after the `Publishing → Published` transition block (where the comment says `STREAM_DOWNLOADED_LIBRARY Phase 2 — register the published file`).
- `cancelStreamBulkGroup` (3-arg overload) — after the final `saveStreamBulkGroups()`.
- `restartStreamBulkGroup` — after the final `saveStreamBulkGroups()`.
- `retryStreamBulkGroupFailedItems` — after the final `saveStreamBulkGroups()`.
- `reconcileStreamBulkGroups` — if it ends up writing state changes, emit a single signal with empty `groupId` to indicate "any group might have changed" so downstream subscribers do a full re-read.

Skeleton for any new emit point:

```cpp
saveStreamBulkGroups();
emit streamBulkGroupsChanged(groupId);
```

For the reconcile case where multiple groups may have changed:

```cpp
if (changed) {
    saveStreamBulkGroups();
    emit streamBulkGroupsChanged(QString());  // empty groupId = full refresh
}
```

- [ ] **Step 3: Build verify.**

Run `taskkill //F //IM Tankoban.exe 2>nul || true; .\build_check.bat 2>&1 | Select-Object -Last 5`. Expected: `BUILD OK`.

### Task 3: Add `streamBulkGroupsSnapshot()` public accessor

**Files:**
- Modify: `src/core/torrent/TorrentClient.h:215-220` (public API block)
- Modify: `src/core/torrent/TorrentClient.cpp` (implementation)

- [ ] **Step 1: Declare the public accessor in the header.**

In `src/core/torrent/TorrentClient.h`, just after the existing `streamBulkSnapshotForImdbSeason` declaration (it's in the public section), add:

```cpp
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — full snapshot of m_streamBulkGroups
    // for cross-show consumers (StreamDownloadsPage). Returns a deep copy so
    // the caller can iterate without holding the mutex.
    QJsonObject streamBulkGroupsSnapshot() const;
```

- [ ] **Step 2: Implement in TorrentClient.cpp.**

Place the implementation next to `streamBulkSnapshotForImdbSeason` (search for that function definition). Add:

```cpp
QJsonObject TorrentClient::streamBulkGroupsSnapshot() const
{
    QMutexLocker lock(&m_streamBulkGroupsMutex);
    return m_streamBulkGroups;   // QJsonObject is implicitly shared, this is cheap
}
```

If `m_streamBulkGroups` is NOT mutex-guarded (read the existing code to confirm), drop the locker. Re-read `streamBulkSnapshotForImdbSeason` to see what synchronization style applies — match it. The current Layer 2 implementation accesses `m_streamBulkGroups` from the GUI thread only, so a plain return-by-value may be sufficient.

- [ ] **Step 3: Build verify.**

Run `.\build_check.bat 2>&1 | Select-Object -Last 5`. Expected: `BUILD OK`.

### Task 4: Add `imdbHasActiveCohort(imdbId)` public accessor

**Files:**
- Modify: `src/core/torrent/TorrentClient.h` (public API)
- Modify: `src/core/torrent/TorrentClient.cpp` (implementation)

- [ ] **Step 1: Declare in the header.**

Add to the public section near the other stream-bulk queries:

```cpp
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — true iff any group keyed
    // "stream:<imdbId>:*" has any non-terminal item (Pending, Downloading,
    // Publishing, or Paused). Drives the DOWNLOADING tile chip on Stream
    // library home. Paused counts as non-terminal so the chip stays visible
    // while a cohort is user-paused.
    bool imdbHasActiveCohort(const QString& imdbId) const;
```

- [ ] **Step 2: Implement in TorrentClient.cpp.**

Place near `streamBulkSnapshotForImdbSeason`:

```cpp
bool TorrentClient::imdbHasActiveCohort(const QString& imdbId) const
{
    if (imdbId.isEmpty())
        return false;
    const QString prefix = QStringLiteral("stream:") + imdbId + QLatin1Char(':');
    for (auto it = m_streamBulkGroups.constBegin();
         it != m_streamBulkGroups.constEnd(); ++it) {
        if (!it.key().startsWith(prefix))
            continue;
        const QJsonArray items = it.value().toObject()
            .value(QStringLiteral("items")).toArray();
        for (const auto& v : items) {
            const QString state = v.toObject()
                .value(QStringLiteral("itemState")).toString();
            if (!isTerminalStreamBulkState(state))
                return true;
        }
    }
    return false;
}
```

- [ ] **Step 3: Build verify.**

Run `.\build_check.bat 2>&1 | Select-Object -Last 5`. Expected: `BUILD OK`.

### Task 5: Add `dispatchStreamBulkSingleEpisode` wrapper + extend `retryStreamBulkGroupFailedItems` with optional itemKey filter

**Files:**
- Modify: `src/core/torrent/TorrentClient.h`
- Modify: `src/core/torrent/TorrentClient.cpp`

- [ ] **Step 1: Declare `dispatchStreamBulkSingleEpisode` in the public section.**

```cpp
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — single-episode dispatch wrapper.
    // Synthesizes a 1-item bulk plan and routes through the existing
    // dispatchStreamBulkGroup machinery. If a group keyed
    // "stream:<imdbId>:s<NN>:*" already exists, joins as a new item;
    // otherwise creates a new group. Used by the per-row [↓] action icon.
    // Returns the groupId. Empty return = dispatch failed (e.g. no sources).
    QString dispatchStreamBulkSingleEpisode(const QString& imdbId,
                                            int season,
                                            int episode);
```

- [ ] **Step 2: Implement `dispatchStreamBulkSingleEpisode`.**

The wrapper builds a 1-item `BulkDownloadItem` list and calls the existing dispatch path. The source-pick fan-out happens inside `StreamBulkDownloader` per Layer 1 §4. This wrapper is a convenience for the UI; the heavy lifting still lives in `StreamPage::triggerBulkSeasonDownload`. Actually — looking at the flow, the orchestrator lives at `StreamPage` level, not `TorrentClient`. So this wrapper should NOT live on TorrentClient.

**Revised plan:** the single-episode dispatch path is added on `StreamPage`, not `TorrentClient`. Drop the `TorrentClient::dispatchStreamBulkSingleEpisode` declaration from Step 1. Instead, **Task 11 (Phase 3)** wires the action-icon click to a new `StreamPage::triggerBulkSingleEpisodeDownload(imdbId, season, episode)` slot which calls into the existing orchestrator with a filtered episode list of size 1.

Remove the Step 1 declaration. Skip to Step 3.

- [ ] **Step 3: Extend `retryStreamBulkGroupFailedItems` to accept optional itemKey filter.**

Find the existing `retryStreamBulkGroupFailedItems(const QString& groupId)` declaration in `TorrentClient.h`. Add an overload (or replace with default-parameter):

```cpp
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — per-item retry filter. If itemKey
    // is empty, retries all failed items in the group (existing v1 behavior).
    // If non-empty, retries ONLY the matching item.
    void retryStreamBulkGroupFailedItems(const QString& groupId,
                                         const QString& itemKey = QString());
```

(Keep the existing single-arg version compiling by giving the new param a default value.)

- [ ] **Step 4: Implement the filter in TorrentClient.cpp.**

Find `retryStreamBulkGroupFailedItems` implementation. At the top of the loop that walks items[], add:

```cpp
for (int i = 0; i < items.size(); ++i) {
    QJsonObject item = items[i].toObject();
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — single-item filter.
    if (!itemKey.isEmpty() &&
        item.value(QStringLiteral("itemKey")).toString() != itemKey) {
        continue;
    }
    // ... existing retry logic
}
```

- [ ] **Step 5: Build verify.**

Run `.\build_check.bat 2>&1 | Select-Object -Last 5`. Expected: `BUILD OK`.

### Task 6: Extend `pruneTerminalStreamBulkGroups` TTL from 7 days to 90 days

**Files:**
- Modify: `src/core/torrent/TorrentClient.cpp:330-340` (or wherever `kGcAgeMs` constant lives)

- [ ] **Step 1: Bump the constant.**

Find `pruneTerminalStreamBulkGroups`. Locate the line:

```cpp
constexpr qint64 kGcAgeMs = 7LL * 24LL * 60LL * 60LL * 1000LL;
```

Change to:

```cpp
// STREAM_DOWNLOADS_NETFLIX_OVERHAUL — TTL bumped 7d → 90d so the
// StreamDownloadsPage History section reads completed cohorts directly
// from stream_bulk_groups.json. No new persistence file; zero schema
// change. Pruning still happens at-save + at-load — no scheduled pruner.
constexpr qint64 kGcAgeMs = 90LL * 24LL * 60LL * 60LL * 1000LL;
```

- [ ] **Step 2: Build verify.**

Run `.\build_check.bat 2>&1 | Select-Object -Last 5`. Expected: `BUILD OK`.

### Task 7: Phase 1 closeout — RTC

- [ ] **Step 1: Confirm the working tree contains exactly the expected delta.**

Run `git diff --stat src/core/torrent/TorrentClient.h src/core/torrent/TorrentClient.cpp`. Should show ~50-100 LOC added to .cpp + ~10-20 LOC to .h.

- [ ] **Step 2: Append Phase 1 RTC to `agents/chat.md`.**

Use the contract from `agents/CONTRACTS.md`. Body (single line, no internal newlines):

```
READY TO COMMIT - [Agent 4, STREAM_DOWNLOADS_NETFLIX_OVERHAUL P1 engine substrate 2026-05-12 — Phase 1 of the Netflix-overhaul plan ships the engine-side substrate the UI phases depend on. Five additions to TorrentClient: (1) kStatePaused state constant + extension of StreamBulkItemState enum + streamBulkItemStateToString/FromString coverage, with isTerminalStreamBulkState intentionally NOT including Paused so the cohort scheduler treats user-paused as slot-occupied (spec §8.5). (2) streamBulkGroupsChanged(QString groupId) signal emitted at every state-mutation site (markStreamBulkItemsForTorrent + publishStreamBulkItemsForTorrent + cancelStreamBulkGroup + restartStreamBulkGroup + retryStreamBulkGroupFailedItems + reconcileStreamBulkGroups). Empty groupId from reconcile = "full refresh" signal. (3) streamBulkGroupsSnapshot() public accessor returning a deep copy of m_streamBulkGroups for cross-show consumers. (4) imdbHasActiveCohort(imdbId) public accessor returning true iff any "stream:<imdbId>:*" group has any non-terminal item. (5) retryStreamBulkGroupFailedItems extended with optional itemKey filter for per-row retry. Plus pruneTerminalStreamBulkGroups TTL bumped 7d → 90d so the StreamDownloadsPage History section reads stream_bulk_groups.json directly with no new persistence file. Zero UI changes in this phase. build_check.bat BUILD OK. Spec source: docs/superpowers/specs/2026-05-12-stream-downloads-netflix-overhaul-design.md §5.2 + §6 + Rule-14 architectural picks. Plan source: docs/superpowers/plans/2026-05-12-stream-downloads-netflix-overhaul.md Phase 1.] | Skills invoked: [/superpowers:executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review] | files: src/core/torrent/TorrentClient.h, src/core/torrent/TorrentClient.cpp, agents/chat.md
```

---

## Phase 2 — Tankorent excise (one-line filter)

Goal: stream-grouped rows disappear from Tankorent UI. Underlying engine + persistence stays.

Phase RTC tag at close: `Agent 4, STREAM_DOWNLOADS_NETFLIX_OVERHAUL P2 Tankorent excise`.

### Task 8: TankorentPage filters out stream-grouped rows

**Files:**
- Modify: `src/ui/pages/TankorentPage.cpp` (search for the function that iterates m_cachedActive or m_transfersTable population, currently called `refreshTransfers` or `renderTorrentRow`)

- [ ] **Step 1: Locate the render loop.**

Open `src/ui/pages/TankorentPage.cpp`. Grep for `m_cachedActive` or `renderTorrentRow` to find the loop that walks active torrents and emits rows. The loop currently has a top-level branch for "stream-group rows" (those with non-empty `streamGroupId`) that builds the group-row presentation. We will SKIP the entire iteration for those entries.

- [ ] **Step 2: Add the filter clause.**

At the very top of the per-torrent loop, before any other branching, add:

```cpp
for (const TorrentInfo& info : m_cachedActive) {
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — stream-originated transfers are
    // managed by the Stream-mode UI (StreamDetailView inline + Stream
    // Downloads page). Skip rendering entirely here; the underlying
    // libtorrent records and stream_bulk_groups.json state stay
    // intact. Tankorent now hosts ONLY non-stream torrents (manual
    // magnet adds, manual torrent search). Spec §7.6.
    if (!info.streamGroupId.isEmpty())
        continue;

    // ... existing per-torrent rendering
}
```

If the existing code has a "group row" branch with a different shape (e.g. groups iterated separately from individual torrents), apply the same skip predicate to the group iteration:

```cpp
for (auto groupIt = ...; groupIt != ...; ++groupIt) {
    // Skip stream-group rows (they render in Stream Downloads page now).
    if (groupIt.key().startsWith(QStringLiteral("stream:")))
        continue;
    // ... existing group row rendering
}
```

- [ ] **Step 3: Verify the totals row / aggregate counters still tally correctly.**

If TankorentPage maintains aggregate counters (total bytes, total speed, etc.) that previously included stream-grouped rows, those should now exclude them. Trace through `refreshTransfers` and ensure the totals are computed from rows that survive the filter.

- [ ] **Step 4: Build verify + manual smoke.**

Run `.\build_check.bat 2>&1 | Select-Object -Last 5`. Expected: `BUILD OK`.

Then `build_and_run.bat` and verify in Tankoban:
1. Tankorent tab → no stream-bulk group rows visible (Daredevil S02 disappears if it was there).
2. Manual-magnet adds (right-click any stream source → Add to Tankorent on a non-bulk source) still render as flat rows.
3. Existing "Add URL" dialog still works.

Stop Tankoban: `powershell -NoProfile -File scripts/stop-tankoban.ps1`.

- [ ] **Step 5: Phase 2 RTC.**

Append to `agents/chat.md`:

```
READY TO COMMIT - [Agent 4, STREAM_DOWNLOADS_NETFLIX_OVERHAUL P2 Tankorent excise 2026-05-12 — Phase 2 of the Netflix-overhaul plan adds a one-clause filter to TankorentPage's render loop so transfers with non-empty TorrentInfo.streamGroupId are skipped entirely. Stream-originated bulks no longer surface in Tankorent UI; manual-magnet torrents continue to render flat as before. Underlying libtorrent records + stream_bulk_groups.json state untouched (engine layer is the shared substrate; only the rendering changes). Build_check.bat BUILD OK. Smoke verified: Daredevil S02 cohort no longer appears in Tankorent; manual magnet adds still work. Spec §7.6.] | Skills invoked: [/superpowers:executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion] | files: src/ui/pages/TankorentPage.cpp, agents/chat.md
```

---

## Phase 3 — StreamDetailView inline trigger UX

Goal: episode rows gain checkbox + morphing action icon; season header gains morphing button + Download-Selected button + right-click Cancel Season; right-click row gains Cancel.

Phase RTC tag at close: `Agent 4, STREAM_DOWNLOADS_NETFLIX_OVERHAUL P3 StreamDetailView inline UX`.

### Task 9: Add column-shift accommodation + `m_selectedEpisodes` member

**Files:**
- Modify: `src/ui/pages/stream/StreamDetailView.h` (members + column constants)
- Modify: `src/ui/pages/stream/StreamDetailView.cpp` (column constants + setup)

This task introduces NEW column indices to avoid magic numbers. The existing code references columns 0/1/2/3/4 directly; we will replace all those literal references with named constants.

- [ ] **Step 1: Define column constants in StreamDetailView.cpp anonymous namespace.**

At the top of `StreamDetailView.cpp` inside the existing anonymous namespace (or create one if absent), add:

```cpp
namespace {
// STREAM_DOWNLOADS_NETFLIX_OVERHAUL — episode-table columns. Prepended
// checkbox at col 0; appended action icon at col last. Existing columns
// shift +1. All previous references to literal column indices (0..4)
// must use these constants instead. Spec §7.1.
constexpr int kColCheckbox  = 0;
constexpr int kColEpisode   = 1;   // was col 0 (#)
constexpr int kColThumb     = 2;   // was col 1
constexpr int kColTitle     = 3;   // was col 2
constexpr int kColProgress  = 4;   // was col 3
constexpr int kColStatus    = 5;   // was col 4
constexpr int kColAction    = 6;   // NEW
constexpr int kColumnCount  = 7;
}
```

- [ ] **Step 2: Update `buildUI` (or wherever the table column-count is set).**

Grep `StreamDetailView.cpp` for `setColumnCount(5)`. Change to `setColumnCount(kColumnCount)`. Find the `setHorizontalHeaderLabels` call; extend the QStringList:

```cpp
m_episodeTable->setColumnCount(kColumnCount);
m_episodeTable->setHorizontalHeaderLabels({
    QString(),         // checkbox — no header text
    QStringLiteral("#"),
    QString(),         // thumbnail — no header text
    tr("Title"),
    tr("Progress"),
    tr("Status"),
    QString(),         // action — no header text
});
```

Also update any header resize / fixed-width calls for the new columns:

```cpp
m_episodeTable->horizontalHeader()->setSectionResizeMode(kColCheckbox, QHeaderView::Fixed);
m_episodeTable->setColumnWidth(kColCheckbox, 32);
m_episodeTable->horizontalHeader()->setSectionResizeMode(kColAction, QHeaderView::Fixed);
m_episodeTable->setColumnWidth(kColAction, 36);
```

- [ ] **Step 3: Add `m_selectedEpisodes` + `m_downloadSeasonBtn` + `m_downloadSelectedBtn` members in the header.**

Open `src/ui/pages/stream/StreamDetailView.h`. Add to the private section:

```cpp
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — inline trigger UX.
    // Per-(show, season) selection state. Reset whenever the season-combo
    // changes (showEntry / setSeason path). NOT persisted; per-launch only.
    QSet<int> m_selectedEpisodes;

    // Season-header morphing primary button: state-driven label & icon.
    QPushButton* m_downloadSeasonBtn = nullptr;

    // Season-header secondary button: visible only when m_selectedEpisodes
    // is non-empty. Label "Download Selected (N)".
    QPushButton* m_downloadSelectedBtn = nullptr;
```

Add `<QSet>` to the includes:

```cpp
#include <QSet>
```

And forward-declare `QPushButton` (likely already present).

- [ ] **Step 4: Update every existing `item(row, 0)` reference in StreamDetailView.cpp.**

Grep for `m_episodeTable->item(row, 0)` and `m_episodeTable->item(idx.row(), 0)` and similar — these are reads of the # column, which is now `kColEpisode` (i.e., 1). Replace each literal `0` with `kColEpisode`.

Likewise: any reference to col 2 (`m_episodeTable->setItem(row, 2, ...)` for title) becomes `kColTitle`. Etc.

A complete enumeration of sites to update (verify each):

- `onEpisodeActivated(int row, int /*col*/)` — `m_episodeTable->item(row, 0)` → `m_episodeTable->item(row, kColEpisode)`.
- `onEpisodeContextMenu(const QPoint& pos)` — same pattern.
- `populateEpisodeTable(int season)` — `setItem(row, 0, numItem)` → `setItem(row, kColEpisode, numItem)`; `setItem(row, 1, ...)` thumb → `kColThumb`; `setItem(row, 2, ...)` title → `kColTitle`; `setItem(row, 3, ...)` progress → `kColProgress`; `setItem(row, 4, ...)` status → `kColStatus`.
- `refreshEpisodeMarkers` — `m_episodeTable->item(row, 0)` → `kColEpisode`.
- `refreshEpisodeBulkProgress` — `m_episodeTable->item(row, 0)` → `kColEpisode`; `m_episodeTable->item(row, 4)` (status) → `kColStatus`.

The `numItem->setData(Qt::UserRole, ...)` and `setData(Qt::UserRole + 1, ...)` calls stay on the same Qt::UserRole slots; only the column index changes.

- [ ] **Step 5: Build verify.**

Run `.\build_check.bat 2>&1 | Select-Object -Last 5`. Expected: `BUILD OK`. The build at this point compiles but the new col 0 (checkbox) and col 6 (action) are empty — UI looks broken; that's fixed in Tasks 10 + 11.

### Task 10: Populate the checkbox column

**Files:**
- Modify: `src/ui/pages/stream/StreamDetailView.cpp` (populateEpisodeTable + showEntry)

- [ ] **Step 1: Add a checkbox cell widget per row in `populateEpisodeTable`.**

Inside the `for (const auto& ep : episodes)` loop in `populateEpisodeTable`, immediately after `m_episodeTable->insertRow(row)` and setRowHeight, add:

```cpp
// STREAM_DOWNLOADS_NETFLIX_OVERHAUL — col 0 checkbox cell. State synced
// to m_selectedEpisodes via stateChanged slot below. Default unchecked.
auto* cbHolder = new QWidget(m_episodeTable);
auto* cbLayout = new QHBoxLayout(cbHolder);
cbLayout->setContentsMargins(8, 0, 8, 0);
cbLayout->setAlignment(Qt::AlignCenter);
auto* checkbox = new QCheckBox(cbHolder);
checkbox->setChecked(m_selectedEpisodes.contains(ep.episode));
checkbox->setProperty("episodeNum", ep.episode);
connect(checkbox, &QCheckBox::stateChanged, this,
        [this, episode = ep.episode](int state) {
            if (state == Qt::Checked) {
                m_selectedEpisodes.insert(episode);
            } else {
                m_selectedEpisodes.remove(episode);
            }
            updateDownloadSelectedButton();
        });
cbLayout->addWidget(checkbox);
m_episodeTable->setCellWidget(row, kColCheckbox, cbHolder);
```

Add `<QCheckBox>` + `<QHBoxLayout>` to the includes if not already present.

- [ ] **Step 2: Declare `updateDownloadSelectedButton` in the header.**

In `StreamDetailView.h` private slots:

```cpp
    // Updates m_downloadSelectedBtn visibility + label based on
    // m_selectedEpisodes.size(). Called whenever a checkbox toggles
    // or the season changes.
    void updateDownloadSelectedButton();
```

- [ ] **Step 3: Implement `updateDownloadSelectedButton`.**

In `StreamDetailView.cpp`, add the implementation (location: near the season-header building code):

```cpp
void StreamDetailView::updateDownloadSelectedButton()
{
    if (!m_downloadSelectedBtn)
        return;
    const int n = m_selectedEpisodes.size();
    if (n == 0) {
        m_downloadSelectedBtn->setVisible(false);
    } else {
        m_downloadSelectedBtn->setVisible(true);
        m_downloadSelectedBtn->setText(tr("Download Selected (%1)").arg(n));
    }
}
```

- [ ] **Step 4: Reset `m_selectedEpisodes` on season change.**

Find the path where the season-combo changes the active season. There should be a slot or callback that calls `populateEpisodeTable(newSeason)`. At the entry of that slot, add:

```cpp
m_selectedEpisodes.clear();
updateDownloadSelectedButton();
```

Also clear in `showEntry`:

```cpp
void StreamDetailView::showEntry(const QString& imdbId, ...)
{
    m_currentImdb = imdbId;
    // ... existing
    m_selectedEpisodes.clear();
    updateDownloadSelectedButton();
    // ... rest of existing
}
```

- [ ] **Step 5: Build verify.**

`.\build_check.bat 2>&1 | Select-Object -Last 5`. Expected: `BUILD OK`.

### Task 11: Populate the action icon column + state-driven click handling

**Files:**
- Modify: `src/ui/pages/stream/StreamDetailView.cpp`

- [ ] **Step 1: Add a helper struct + state-resolution function.**

Add at the top of `StreamDetailView.cpp` (anonymous namespace, after the column constants):

```cpp
// STREAM_DOWNLOADS_NETFLIX_OVERHAUL — derived row state for paint pass.
// Resolves to a single enum value covering both Layer 3 (download index)
// and Layer 2 (cohort snapshot) sources. Spec §7.1 state map.
enum class RowState {
    Idle,           // not downloaded, no cohort entry
    Queued,         // cohort item state = Pending, no infoHash yet
    Downloading,
    Publishing,
    Published,      // downloaded; auto-play on click
    Paused,
    Failed,
};

struct RowStateInput {
    bool   downloadIndexHit  = false;
    bool   cohortHit         = false;
    QString cohortState;     // raw item state from snapshot
};

RowState resolveRowState(const RowStateInput& in)
{
    if (in.downloadIndexHit) return RowState::Published;
    if (!in.cohortHit)       return RowState::Idle;
    if (in.cohortState == QLatin1String("Paused"))      return RowState::Paused;
    if (in.cohortState == QLatin1String("Downloading")) return RowState::Downloading;
    if (in.cohortState == QLatin1String("Publishing"))  return RowState::Publishing;
    if (in.cohortState == QLatin1String("Pending"))     return RowState::Queued;
    // All other cohort states are terminal-failure variants per
    // isStreamBulkFailureState (MissingSource / MetadataFailed /
    // PublishFailed / Failed / Orphaned / Cancelled).
    return RowState::Failed;
}
```

- [ ] **Step 2: Add action-icon paint helper.**

```cpp
struct ActionIconSpec {
    QString iconResource;   // resource path
    QString tooltip;
    bool    enabled = true;
};

ActionIconSpec actionIconForState(RowState st)
{
    switch (st) {
    case RowState::Idle:        return { ":/icons/download-arrow.svg", QObject::tr("Download episode"), true };
    case RowState::Queued:      return { ":/icons/download-arrow.svg", QObject::tr("Queued — waiting for cohort head"), false };
    case RowState::Downloading: return { ":/icons/pause-circle.svg",   QObject::tr("Pause download"), true };
    case RowState::Publishing:  return { ":/icons/pause-circle.svg",   QObject::tr("Publishing"), false };
    case RowState::Published:   return { ":/icons/downloaded.svg",     QObject::tr("On disk — click row to play"), false };
    case RowState::Paused:      return { ":/icons/play-circle.svg",    QObject::tr("Continue download"), true };
    case RowState::Failed:      return { ":/icons/retry-arrow.svg",    QObject::tr("Retry"), true };
    }
    return { ":/icons/download-arrow.svg", QString(), true };
}
```

- [ ] **Step 3: Populate the action-icon cell in `populateEpisodeTable`.**

Inside the same per-row loop where the checkbox was added, AFTER the checkbox cell widget is set, add:

```cpp
// STREAM_DOWNLOADS_NETFLIX_OVERHAUL — col 6 action-icon cell.
// Initial glyph chosen by resolveRowState; updated in
// refreshEpisodeMarkers + refreshEpisodeBulkProgress on subsequent
// ticks.
auto* iconHolder = new QWidget(m_episodeTable);
auto* iconLayout = new QHBoxLayout(iconHolder);
iconLayout->setContentsMargins(4, 0, 4, 0);
iconLayout->setAlignment(Qt::AlignCenter);
auto* btn = new QPushButton(iconHolder);
btn->setFlat(true);
btn->setFixedSize(24, 24);
btn->setIconSize(QSize(16, 16));
btn->setCursor(Qt::PointingHandCursor);
btn->setProperty("episodeNum", ep.episode);
connect(btn, &QPushButton::clicked, this, [this, episode = ep.episode]() {
    onActionIconClicked(episode);
});
iconLayout->addWidget(btn);
m_episodeTable->setCellWidget(row, kColAction, iconHolder);
```

- [ ] **Step 4: Declare `onActionIconClicked` slot in the header.**

```cpp
private slots:
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — action-icon click dispatch.
    // Resolves current row state and routes to download / pause /
    // resume / retry. Spec §7.1 row state map.
    void onActionIconClicked(int episode);
```

- [ ] **Step 5: Implement `onActionIconClicked`.**

```cpp
void StreamDetailView::onActionIconClicked(int episode)
{
    if (m_currentImdb.isEmpty() || episode <= 0)
        return;

    int activeSeason = 1;
    if (m_seasonCombo) {
        const int idx = m_seasonCombo->currentIndex();
        if (idx >= 0)
            activeSeason = m_seasonCombo->itemData(idx).toInt();
    }
    if (activeSeason <= 0) return;

    // Resolve current state to decide dispatch.
    RowStateInput in;
    if (m_downloadIndex) {
        in.downloadIndexHit =
            m_downloadIndex->filePathFor(m_currentImdb, activeSeason, episode).has_value();
    }
    if (m_torrentClient) {
        const auto snap =
            m_torrentClient->streamBulkSnapshotForImdbSeason(m_currentImdb, activeSeason);
        const auto it = snap.constFind(episode);
        if (it != snap.cend()) {
            in.cohortHit = true;
            in.cohortState = it->first;
        }
    }
    const RowState st = resolveRowState(in);

    switch (st) {
    case RowState::Idle:
        // Single-episode dispatch — synthesizes a 1-item bulk via StreamPage.
        emit singleEpisodeDownloadRequested(activeSeason, episode);
        break;
    case RowState::Queued:
        // Click on a queued row is a no-op; cohort head must advance first.
        break;
    case RowState::Downloading: {
        // Pause: pauseTorrent + set cohort state to Paused.
        const QString infoHash =
            findInfoHashForEpisode(activeSeason, episode);
        if (!infoHash.isEmpty() && m_torrentClient) {
            m_torrentClient->pauseTorrent(infoHash);
            m_torrentClient->setStreamBulkItemPaused(infoHash, /*paused=*/true);
        }
        break;
    }
    case RowState::Paused: {
        const QString infoHash =
            findInfoHashForEpisode(activeSeason, episode);
        if (!infoHash.isEmpty() && m_torrentClient) {
            m_torrentClient->resumeTorrent(infoHash);
            m_torrentClient->setStreamBulkItemPaused(infoHash, /*paused=*/false);
        }
        break;
    }
    case RowState::Failed: {
        const QString groupId = findGroupIdForCohort(activeSeason);
        const QString itemKey = QStringLiteral("%1:S%2E%3")
            .arg(m_currentImdb)
            .arg(activeSeason, 2, 10, QLatin1Char('0'))
            .arg(episode, 2, 10, QLatin1Char('0'));
        if (!groupId.isEmpty() && m_torrentClient) {
            m_torrentClient->retryStreamBulkGroupFailedItems(groupId, itemKey);
        }
        break;
    }
    case RowState::Published:
    case RowState::Publishing:
        // No-op — these are stable states. (Published has its own click
        // path via onEpisodeActivated for playback.)
        break;
    }
}
```

- [ ] **Step 6: Add the helper methods + signal declaration in the header.**

In `StreamDetailView.h`:

```cpp
signals:
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — single-episode dispatch ask.
    // Routed to StreamPage::triggerBulkSingleEpisodeDownload.
    void singleEpisodeDownloadRequested(int season, int episode);

private:
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — helpers used by onActionIconClicked.
    QString findInfoHashForEpisode(int season, int episode) const;
    QString findGroupIdForCohort(int season) const;
```

- [ ] **Step 7: Implement `findInfoHashForEpisode` + `findGroupIdForCohort`.**

```cpp
QString StreamDetailView::findInfoHashForEpisode(int season, int episode) const
{
    if (!m_torrentClient || m_currentImdb.isEmpty())
        return {};
    const QString prefix =
        QStringLiteral("stream:") + m_currentImdb + QLatin1Char(':');
    const QJsonObject groups = m_torrentClient->streamBulkGroupsSnapshot();
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        if (!it.key().startsWith(prefix)) continue;
        const QJsonObject g = it.value().toObject();
        if (g.value("sourceIds").toObject()
              .value("season").toInt(-1) != season) {
            continue;
        }
        for (const auto& v : g.value("items").toArray()) {
            const QJsonObject item = v.toObject();
            const QString itemKey = item.value("itemKey").toString();
            const int eIdx = itemKey.lastIndexOf(QLatin1Char('E'));
            if (eIdx <= 0) continue;
            if (itemKey.mid(eIdx + 1).toInt() != episode) continue;
            return item.value("infoHash").toString();
        }
    }
    return {};
}

QString StreamDetailView::findGroupIdForCohort(int season) const
{
    if (!m_torrentClient || m_currentImdb.isEmpty())
        return {};
    const QString prefix =
        QStringLiteral("stream:") + m_currentImdb + QLatin1Char(':');
    const QJsonObject groups = m_torrentClient->streamBulkGroupsSnapshot();
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        if (!it.key().startsWith(prefix)) continue;
        const QJsonObject g = it.value().toObject();
        if (g.value("sourceIds").toObject()
              .value("season").toInt(-1) != season) {
            continue;
        }
        return it.key();
    }
    return {};
}
```

- [ ] **Step 8: Add `setStreamBulkItemPaused` to TorrentClient.**

In `src/core/torrent/TorrentClient.h` public section:

```cpp
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — flip a cohort item's state
    // between Paused and Downloading. Caller is responsible for the
    // libtorrent pauseTorrent/resumeTorrent call that mirrors this.
    void setStreamBulkItemPaused(const QString& infoHash, bool paused);
```

In `TorrentClient.cpp`:

```cpp
void TorrentClient::setStreamBulkItemPaused(const QString& infoHash, bool paused)
{
    if (infoHash.isEmpty()) return;
    bool changed = false;
    QString affectedGroupId;
    for (auto groupIt = m_streamBulkGroups.begin();
         groupIt != m_streamBulkGroups.end(); ++groupIt) {
        QJsonObject group = groupIt.value().toObject();
        QJsonArray items = group.value("items").toArray();
        for (int i = 0; i < items.size(); ++i) {
            QJsonObject item = items[i].toObject();
            if (item.value("infoHash").toString() != infoHash) continue;
            const QString cur = item.value("itemState").toString();
            if (paused && cur == QLatin1String(kStateDownloading)) {
                item["itemState"] = QString::fromLatin1(kStatePaused);
                items.replace(i, item);
                changed = true;
                affectedGroupId = groupIt.key();
            } else if (!paused && cur == QLatin1String(kStatePaused)) {
                item["itemState"] = QString::fromLatin1(kStateDownloading);
                items.replace(i, item);
                changed = true;
                affectedGroupId = groupIt.key();
            }
        }
        if (changed) {
            group["items"] = items;
            group["updatedAtMs"] = QDateTime::currentMSecsSinceEpoch();
            *groupIt = group;
            break;
        }
    }
    if (changed) {
        saveStreamBulkGroups();
        emit streamBulkGroupsChanged(affectedGroupId);
    }
}
```

- [ ] **Step 9: Build verify.**

`.\build_check.bat 2>&1 | Select-Object -Last 5`. Expected: `BUILD OK`.

### Task 12: Repaint action icons on every refresh tick

**Files:**
- Modify: `src/ui/pages/stream/StreamDetailView.cpp`

- [ ] **Step 1: Add a paint helper that resolves state + sets the cell button icon.**

```cpp
void StreamDetailView::repaintActionIconForRow(int row,
                                                int episode,
                                                int season,
                                                const QHash<int, QPair<QString, int>>& cohortSnap)
{
    auto* holder = qobject_cast<QWidget*>(m_episodeTable->cellWidget(row, kColAction));
    if (!holder) return;
    auto* btn = holder->findChild<QPushButton*>();
    if (!btn) return;

    RowStateInput in;
    if (m_downloadIndex) {
        in.downloadIndexHit =
            m_downloadIndex->filePathFor(m_currentImdb, season, episode).has_value();
    }
    auto it = cohortSnap.constFind(episode);
    if (it != cohortSnap.cend()) {
        in.cohortHit = true;
        in.cohortState = it->first;
    }
    const RowState st = resolveRowState(in);
    const ActionIconSpec spec = actionIconForState(st);

    btn->setIcon(QIcon(spec.iconResource));
    btn->setToolTip(spec.tooltip);
    btn->setEnabled(spec.enabled);
}
```

Declare in `StreamDetailView.h` private section:

```cpp
    void repaintActionIconForRow(int row, int episode, int season,
                                  const QHash<int, QPair<QString, int>>& cohortSnap);
```

- [ ] **Step 2: Call from `refreshEpisodeBulkProgress` at the end of each row iteration.**

In the existing `refreshEpisodeBulkProgress` loop (which already walks rows and updates status text), after the existing status-cell update, add:

```cpp
repaintActionIconForRow(row, episode, activeSeason, snapshot);
```

- [ ] **Step 3: Also paint once at initial populate (in `populateEpisodeTable`).**

At the end of `populateEpisodeTable` (after the existing `refreshEpisodeMarkers()` call), add a one-shot paint sweep:

```cpp
const auto initialSnap =
    m_torrentClient ? m_torrentClient->streamBulkSnapshotForImdbSeason(m_currentImdb, season)
                    : QHash<int, QPair<QString, int>>();
for (int row = 0; row < m_episodeTable->rowCount(); ++row) {
    QTableWidgetItem* numItem = m_episodeTable->item(row, kColEpisode);
    if (!numItem) continue;
    const int ep = numItem->data(Qt::UserRole).toInt();
    if (ep <= 0) continue;
    repaintActionIconForRow(row, ep, season, initialSnap);
}
```

- [ ] **Step 4: Subscribe to TorrentClient::streamBulkGroupsChanged.**

In `StreamDetailView`'s ctor (or wherever signals are wired), add:

```cpp
if (m_torrentClient) {
    connect(m_torrentClient, &TorrentClient::streamBulkGroupsChanged,
            this, [this](const QString& /*groupId*/) {
                refreshEpisodeBulkProgress();
            }, Qt::QueuedConnection);
}
```

- [ ] **Step 5: Build verify.**

`.\build_check.bat 2>&1 | Select-Object -Last 5`. Expected: `BUILD OK`.

### Task 13: Season header morphing button + Download Selected button

**Files:**
- Modify: `src/ui/pages/stream/StreamDetailView.cpp` (buildUI / season-row layout)
- Modify: `src/ui/pages/stream/StreamDetailView.h`

- [ ] **Step 1: Insert the new buttons into the season-row layout.**

Find the season-combo construction in `StreamDetailView.cpp` (search for `m_seasonCombo`). Right after the combo is added to its layout, add:

```cpp
// STREAM_DOWNLOADS_NETFLIX_OVERHAUL — season-header morphing button.
m_downloadSeasonBtn = new QPushButton(this);
m_downloadSeasonBtn->setCursor(Qt::PointingHandCursor);
m_downloadSeasonBtn->setText(tr("Download Season"));
m_downloadSeasonBtn->setIcon(QIcon(":/icons/download-arrow.svg"));
m_downloadSeasonBtn->setContextMenuPolicy(Qt::CustomContextMenu);
connect(m_downloadSeasonBtn, &QPushButton::clicked,
        this, &StreamDetailView::onDownloadSeasonClicked);
connect(m_downloadSeasonBtn, &QPushButton::customContextMenuRequested,
        this, &StreamDetailView::onSeasonHeaderRightClick);
seasonRowLayout->addWidget(m_downloadSeasonBtn);

m_downloadSelectedBtn = new QPushButton(this);
m_downloadSelectedBtn->setCursor(Qt::PointingHandCursor);
m_downloadSelectedBtn->setIcon(QIcon(":/icons/download-arrow.svg"));
m_downloadSelectedBtn->setVisible(false);
connect(m_downloadSelectedBtn, &QPushButton::clicked,
        this, &StreamDetailView::onDownloadSelectedClicked);
seasonRowLayout->addWidget(m_downloadSelectedBtn);
```

Also enable right-click on the season-combo for "Cancel Season":

```cpp
m_seasonCombo->setContextMenuPolicy(Qt::CustomContextMenu);
connect(m_seasonCombo, &QComboBox::customContextMenuRequested,
        this, &StreamDetailView::onSeasonHeaderRightClick);
```

- [ ] **Step 2: Declare the new slots in StreamDetailView.h.**

```cpp
private slots:
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — season-header actions.
    void onDownloadSeasonClicked();
    void onDownloadSelectedClicked();
    void onSeasonHeaderRightClick(const QPoint& pos);

    // Repaints the morphing button label/icon based on cohort state for
    // the active season. Called whenever the cohort state changes.
    void refreshSeasonHeaderButton();
```

Also declare a signal:

```cpp
signals:
    // Already declared in Task 11; this is the lateral path used by
    // BOTH the per-row icon AND the season-header.
    void seasonDownloadRequested(int season);
    void selectedEpisodesDownloadRequested(int season, const QList<int>& episodes);
    void seasonCancelRequested(int season);
```

- [ ] **Step 3: Implement the slots.**

```cpp
void StreamDetailView::onDownloadSeasonClicked()
{
    if (m_currentImdb.isEmpty() || m_currentType != QLatin1String("series"))
        return;
    int season = m_seasonCombo ? m_seasonCombo->currentData().toInt() : 0;
    if (season <= 0) return;

    // Resolve current cohort state for the morphing logic.
    if (!m_torrentClient) {
        emit seasonDownloadRequested(season);
        return;
    }
    const auto snap = m_torrentClient->streamBulkSnapshotForImdbSeason(m_currentImdb, season);
    bool anyActive = false, allPaused = !snap.isEmpty();
    for (auto it = snap.cbegin(); it != snap.cend(); ++it) {
        const QString& s = it->first;
        if (!isTerminalStreamBulkState(s)) {
            anyActive = true;
            if (s != QLatin1String("Paused")) allPaused = false;
        } else {
            allPaused = false;
        }
    }
    const QString groupId = findGroupIdForCohort(season);

    if (allPaused && !groupId.isEmpty()) {
        // Continue Season: resume every Paused item.
        for (auto it = snap.cbegin(); it != snap.cend(); ++it) {
            if (it->first != QLatin1String("Paused")) continue;
            const QString ih = findInfoHashForEpisode(season, it.key());
            if (ih.isEmpty()) continue;
            m_torrentClient->resumeTorrent(ih);
            m_torrentClient->setStreamBulkItemPaused(ih, /*paused=*/false);
        }
    } else if (anyActive && !groupId.isEmpty()) {
        // Pause Season: pause every Downloading item.
        for (auto it = snap.cbegin(); it != snap.cend(); ++it) {
            if (it->first != QLatin1String("Downloading")) continue;
            const QString ih = findInfoHashForEpisode(season, it.key());
            if (ih.isEmpty()) continue;
            m_torrentClient->pauseTorrent(ih);
            m_torrentClient->setStreamBulkItemPaused(ih, /*paused=*/true);
        }
    } else {
        // Download Season: fire the existing orchestrator.
        emit seasonDownloadRequested(season);
    }
}

void StreamDetailView::onDownloadSelectedClicked()
{
    if (m_currentImdb.isEmpty()) return;
    int season = m_seasonCombo ? m_seasonCombo->currentData().toInt() : 0;
    if (season <= 0) return;
    QList<int> eps(m_selectedEpisodes.cbegin(), m_selectedEpisodes.cend());
    std::sort(eps.begin(), eps.end());
    if (eps.isEmpty()) return;
    emit selectedEpisodesDownloadRequested(season, eps);
    // Clear selection on dispatch.
    m_selectedEpisodes.clear();
    updateDownloadSelectedButton();
    // Re-tick checkboxes off on the table.
    for (int row = 0; row < m_episodeTable->rowCount(); ++row) {
        auto* holder = qobject_cast<QWidget*>(m_episodeTable->cellWidget(row, kColCheckbox));
        if (!holder) continue;
        auto* cb = holder->findChild<QCheckBox*>();
        if (cb) cb->setChecked(false);
    }
}

void StreamDetailView::onSeasonHeaderRightClick(const QPoint& pos)
{
    int season = m_seasonCombo ? m_seasonCombo->currentData().toInt() : 0;
    if (season <= 0 || m_currentImdb.isEmpty()) return;
    QWidget* origin = qobject_cast<QWidget*>(sender());
    if (!origin) return;

    QMenu menu(this);
    QAction* cancelAct = menu.addAction(tr("Cancel Season"));
    QAction* chosen = menu.exec(origin->mapToGlobal(pos));
    if (chosen == cancelAct) {
        const QString groupId = findGroupIdForCohort(season);
        if (groupId.isEmpty()) return;
        // Confirmation dialog per Spec §8.4.
        const auto reply = QMessageBox::question(this, tr("Cancel Season?"),
            tr("Cancel and delete all files for this season? "
               "This cannot be undone."),
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes && m_torrentClient) {
            m_torrentClient->cancelStreamBulkGroup(groupId, /*deleteFiles=*/true);
        }
    }
}

void StreamDetailView::refreshSeasonHeaderButton()
{
    if (!m_downloadSeasonBtn) return;
    if (m_currentImdb.isEmpty() || m_currentType != QLatin1String("series")) {
        m_downloadSeasonBtn->setVisible(false);
        return;
    }
    int season = m_seasonCombo ? m_seasonCombo->currentData().toInt() : 0;
    if (season <= 0 || !m_torrentClient) {
        m_downloadSeasonBtn->setText(tr("Download Season"));
        m_downloadSeasonBtn->setIcon(QIcon(":/icons/download-arrow.svg"));
        m_downloadSeasonBtn->setVisible(true);
        return;
    }
    const auto snap = m_torrentClient->streamBulkSnapshotForImdbSeason(m_currentImdb, season);
    bool anyActive = false;
    bool allPaused = !snap.isEmpty();
    for (auto it = snap.cbegin(); it != snap.cend(); ++it) {
        const QString& s = it->first;
        if (!isTerminalStreamBulkState(s)) {
            anyActive = true;
            if (s != QLatin1String("Paused")) allPaused = false;
        } else {
            allPaused = false;
        }
    }
    if (allPaused) {
        m_downloadSeasonBtn->setText(tr("Continue Season"));
        m_downloadSeasonBtn->setIcon(QIcon(":/icons/play-circle.svg"));
    } else if (anyActive) {
        m_downloadSeasonBtn->setText(tr("Pause Season"));
        m_downloadSeasonBtn->setIcon(QIcon(":/icons/pause-circle.svg"));
    } else {
        m_downloadSeasonBtn->setText(tr("Download Season"));
        m_downloadSeasonBtn->setIcon(QIcon(":/icons/download-arrow.svg"));
    }
    m_downloadSeasonBtn->setVisible(true);
}
```

- [ ] **Step 4: Call `refreshSeasonHeaderButton` from `refreshEpisodeBulkProgress`.**

Inside `refreshEpisodeBulkProgress` after the per-row paint loop, add:

```cpp
refreshSeasonHeaderButton();
```

Also call it at the end of `populateEpisodeTable` and on season-combo change.

- [ ] **Step 5: Helper: `isTerminalStreamBulkState` is declared in TorrentClient.h — make sure it's accessible.**

If `isTerminalStreamBulkState` is currently a file-scope function in TorrentClient.cpp's anonymous namespace, expose it via the header so StreamDetailView can call it. Move the declaration to `TorrentClient.h` (in a free-function namespace `tankostream::stream` or just at file scope inside the existing namespace).

Alternatively, replicate the predicate inline in StreamDetailView (cheaper than a header-coupling change). Given the 8 terminal-state strings, inline duplication is acceptable for one consumer. Replicate:

```cpp
// In StreamDetailView.cpp anonymous namespace:
bool isTerminalCohortState(const QString& state)
{
    static const QSet<QString> kTerminal = {
        QStringLiteral("Published"), QStringLiteral("Completed"),
        QStringLiteral("Cancelled"), QStringLiteral("MissingSource"),
        QStringLiteral("MetadataFailed"), QStringLiteral("PublishFailed"),
        QStringLiteral("Failed"),    QStringLiteral("Orphaned"),
    };
    return kTerminal.contains(state);
}
```

Use this local helper in all places where the season-header morphing logic checks for terminal state.

- [ ] **Step 6: Build verify.**

`.\build_check.bat 2>&1 | Select-Object -Last 5`. Expected: `BUILD OK`.

### Task 14: Row right-click context menu — Cancel + Show alternate streams

**Files:**
- Modify: `src/ui/pages/stream/StreamDetailView.cpp` (`onEpisodeContextMenu`)

- [ ] **Step 1: Replace the existing context-menu body.**

Find `onEpisodeContextMenu` (around line 957 per the spec's references). The existing menu has "Show alternate streams" + possibly some other entries. Replace the body with:

```cpp
void StreamDetailView::onEpisodeContextMenu(const QPoint& pos)
{
    if (!m_episodeTable) return;
    const QModelIndex idx = m_episodeTable->indexAt(pos);
    if (!idx.isValid()) return;
    QTableWidgetItem* numItem = m_episodeTable->item(idx.row(), kColEpisode);
    if (!numItem) return;
    const int episode = numItem->data(Qt::UserRole).toInt();
    const int season  = numItem->data(Qt::UserRole + 1).toInt();
    if (episode <= 0 || season <= 0) return;

    // Resolve state for the Cancel-visibility check.
    RowStateInput in;
    if (m_downloadIndex) {
        in.downloadIndexHit =
            m_downloadIndex->filePathFor(m_currentImdb, season, episode).has_value();
    }
    if (m_torrentClient) {
        const auto snap = m_torrentClient->streamBulkSnapshotForImdbSeason(m_currentImdb, season);
        auto it = snap.constFind(episode);
        if (it != snap.cend()) {
            in.cohortHit = true;
            in.cohortState = it->first;
        }
    }
    const RowState st = resolveRowState(in);

    QMenu menu(this);
    QAction* cancelAct = nullptr;
    if (st == RowState::Downloading || st == RowState::Publishing ||
        st == RowState::Paused      || st == RowState::Published ||
        st == RowState::Queued) {
        cancelAct = menu.addAction(tr("Cancel"));
    }
    QAction* altAct = menu.addAction(tr("Show alternate streams"));

    QAction* chosen = menu.exec(m_episodeTable->viewport()->mapToGlobal(pos));
    if (chosen == cancelAct && cancelAct) {
        // Confirmation only for published or paused — destructive cases.
        if (st == RowState::Published || st == RowState::Paused) {
            const auto reply = QMessageBox::question(this, tr("Cancel episode?"),
                tr("Delete this episode's file from disk? This cannot be undone."),
                QMessageBox::Yes | QMessageBox::No);
            if (reply != QMessageBox::Yes) return;
        }
        // Find the item's groupId + itemKey, then cancel that ONE item.
        const QString groupId = findGroupIdForCohort(season);
        const QString itemKey = QStringLiteral("%1:S%2E%3")
            .arg(m_currentImdb)
            .arg(season, 2, 10, QLatin1Char('0'))
            .arg(episode, 2, 10, QLatin1Char('0'));
        if (!groupId.isEmpty() && m_torrentClient) {
            m_torrentClient->cancelStreamBulkItem(groupId, itemKey, /*deleteFile=*/true);
        }
    } else if (chosen == altAct) {
        emit alternateStreamRequested(season, episode);
    }
}
```

- [ ] **Step 2: Add `cancelStreamBulkItem` to TorrentClient.**

This is a NEW API for per-item cancel (Layer 1 §Q5 was group-only; Hemanth's P8 + spec §7.1 require per-item cancel + delete).

In `TorrentClient.h` public section:

```cpp
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — per-item cancel + delete.
    // Removes the libtorrent record (with delete_files=deleteFile),
    // marks the cohort item Cancelled, evicts the StreamDownloadIndex
    // entry if Published, deletes the canonical-name map entry.
    // If after the item is removed the group has no remaining items,
    // also removes the group entry. Emits streamBulkGroupsChanged.
    void cancelStreamBulkItem(const QString& groupId,
                              const QString& itemKey,
                              bool deleteFile);
```

Implementation in `TorrentClient.cpp` (sketch — full implementation expanded during executing-plans):

```cpp
void TorrentClient::cancelStreamBulkItem(const QString& groupId,
                                          const QString& itemKey,
                                          bool deleteFile)
{
    auto groupIt = m_streamBulkGroups.find(groupId);
    if (groupIt == m_streamBulkGroups.end()) return;
    QJsonObject group = groupIt->toObject();
    QJsonArray items = group.value("items").toArray();
    QString infoHash;
    for (int i = 0; i < items.size(); ++i) {
        QJsonObject item = items[i].toObject();
        if (item.value("itemKey").toString() != itemKey) continue;
        infoHash = item.value("infoHash").toString();
        item["itemState"] = QString::fromLatin1(kStateCancelled);
        items.replace(i, item);
        break;
    }
    group["items"] = items;
    group["updatedAtMs"] = QDateTime::currentMSecsSinceEpoch();
    *groupIt = group;
    saveStreamBulkGroups();

    if (!infoHash.isEmpty()) {
        deleteTorrent(infoHash, deleteFile);
    }
    if (deleteFile && m_streamDownloadIndex) {
        // Evict by canonical path if it was in the index.
        // Need to compute canonicalPath from destinationKey + destinationRoot.
        // (Full implementation reads the item's destinationKey from the
        // pre-cancel snapshot.)
    }
    emit streamBulkGroupsChanged(groupId);
}
```

- [ ] **Step 3: Declare `alternateStreamRequested` signal if not present.**

Per spec §3 mention, this signal likely exists from Layer 3. Confirm by grepping. If not, add to `StreamDetailView.h`:

```cpp
signals:
    void alternateStreamRequested(int season, int episode);
```

And wire its consumer in `StreamPage` (Task 15).

- [ ] **Step 4: Build verify.**

`.\build_check.bat 2>&1 | Select-Object -Last 5`. Expected: `BUILD OK`.

### Task 15: Direct dispatch wiring in StreamPage (skip preflight)

**Files:**
- Modify: `src/ui/pages/StreamPage.h`
- Modify: `src/ui/pages/StreamPage.cpp`

- [ ] **Step 1: Connect the new StreamDetailView signals to StreamPage slots.**

In StreamPage's ctor (or wherever m_detailView signals are wired), add:

```cpp
connect(m_detailView, &StreamDetailView::seasonDownloadRequested,
        this, &StreamPage::onSeasonDownloadRequested);
connect(m_detailView, &StreamDetailView::selectedEpisodesDownloadRequested,
        this, &StreamPage::onSelectedEpisodesDownloadRequested);
connect(m_detailView, &StreamDetailView::singleEpisodeDownloadRequested,
        this, &StreamPage::onSingleEpisodeDownloadRequested);
```

- [ ] **Step 2: Declare the slots in StreamPage.h.**

```cpp
private slots:
    void onSeasonDownloadRequested(int season);
    void onSelectedEpisodesDownloadRequested(int season, const QList<int>& episodes);
    void onSingleEpisodeDownloadRequested(int season, int episode);
```

- [ ] **Step 3: Implement direct dispatch (no preflight).**

```cpp
void StreamPage::onSeasonDownloadRequested(int season)
{
    if (!m_detailView) return;
    triggerBulkSeasonDownloadInternal(m_detailView->currentImdb(), season, QList<int>{});
}

void StreamPage::onSelectedEpisodesDownloadRequested(int season, const QList<int>& episodes)
{
    if (!m_detailView) return;
    triggerBulkSeasonDownloadInternal(m_detailView->currentImdb(), season, episodes);
}

void StreamPage::onSingleEpisodeDownloadRequested(int season, int episode)
{
    if (!m_detailView) return;
    triggerBulkSeasonDownloadInternal(m_detailView->currentImdb(), season, QList<int>{episode});
}
```

Where `triggerBulkSeasonDownloadInternal` is the renamed-and-extended version of the existing `triggerBulkSeasonDownload`. The third parameter is an optional episode-filter; empty = whole season; non-empty = just those episodes.

- [ ] **Step 4: Modify `triggerBulkSeasonDownload` (or its renamed sibling) to skip the preflight dialog.**

Find the existing implementation. The path currently goes:

```
StreamBulkDownloader::begin() →
  sourcesReady → StreamPage::onBulkSourcesReady →
    StreamBulkPreflightDialog::exec() →
      onPreflightAccepted → emit addToTankorentBulkRequested → MainWindow → TankorentPage
```

Replace `onBulkSourcesReady` to skip the dialog:

```cpp
void StreamPage::onBulkSourcesReady(const PreflightSummary& summary,
                                     const QString& groupId,
                                     const QString& groupLabel,
                                     const QString& savePath,
                                     const QList<BulkDownloadItem>& items,
                                     const QMap<int, QString>& canonicalMapByFileIndex,
                                     const QMap<QString, QString>& canonicalMapByInfoHash)
{
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — no preflight dialog.
    // Direct dispatch path (Spec §1 "no popup, no modal, no dialog").
    // The pre-flight summary is still computed (size estimate, missing-
    // source count) for diagnostic log but NOT shown to the user.
    qInfo("STREAM_BULK direct dispatch: group=%s items=%d toDownload=%d missing=%d",
          qUtf8Printable(groupId), summary.totalEpisodes, summary.toDownload,
          summary.missingNoSource);
    emit streamBulkDispatchRequested(groupLabel, items, groupId,
        summary.packMode ? QStringLiteral("pack") : QStringLiteral("per-episode"),
        savePath, canonicalMapByFileIndex, canonicalMapByInfoHash);
}
```

Declare the renamed signal in `StreamPage.h`:

```cpp
signals:
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — renamed from
    // addToTankorentBulkRequested. Routes to MainWindow's
    // onStreamBulkDispatchRequested slot which dispatches WITHOUT
    // a page-switch.
    void streamBulkDispatchRequested(
        const QString& label,
        const QList<tankostream::stream::BulkDownloadItem>& items,
        const QString& groupId,
        const QString& groupShape,
        const QString& savePath,
        const QMap<int, QString>& canonicalMapByFileIndex,
        const QMap<QString, QString>& canonicalMapByInfoHash);
```

Remove the old `addToTankorentBulkRequested` signal declaration. (The single-magnet `addToTankorentRequested` for manual right-click "Add to Tankorent" on a stream source stays — that's still a real user gesture targeting Tankorent.)

- [ ] **Step 5: Add `currentImdb()` getter to StreamDetailView.**

If not already public, expose `m_currentImdb`:

```cpp
public:
    QString currentImdb() const { return m_currentImdb; }
```

- [ ] **Step 6: Build verify.**

`.\build_check.bat 2>&1 | Select-Object -Last 5`. Expected: `BUILD OK`.

### Task 16: Phase 3 closeout — smoke + RTC

- [ ] **Step 1: Smoke test in Tankoban.**

Run `build_and_run.bat`. Navigate Stream → Daredevil: Born Again → Season 2. Verify:
1. Checkbox column visible at left of each row.
2. Action icon column visible at right of each row, showing `[↓]` for E11 (not-downloaded) and `[✓]` for E5–E8 (downloaded per RTC #31 backfill).
3. Click checkbox on E11 → "Download Selected (1)" button appears in season header.
4. Click action icon `[↓]` on E11 → orchestrator fires source-pick → cohort scheduler dispatches → row morphs to Queued/Downloading.
5. Click action icon `[⏸]` on the downloading row → row morphs to `[▶]` Paused.
6. Click `[▶]` → row resumes.
7. Right-click row → "Cancel" + "Show alternate streams" menu appears.
8. Right-click season-combo → "Cancel Season" appears.
9. Right-click on Daredevil row from Tankorent tab — Daredevil no longer rendered in Tankorent (verified Task 8).

`scripts/stop-tankoban.ps1` after smoke.

- [ ] **Step 2: Phase 3 RTC.**

```
READY TO COMMIT - [Agent 4, STREAM_DOWNLOADS_NETFLIX_OVERHAUL P3 StreamDetailView inline UX 2026-05-12 — Phase 3 ships the inline trigger UX on the episode table. Column layout extended 5 → 7 (checkbox at col 0; action icon at col 6); existing literal column indices replaced with named constants kColCheckbox/kColEpisode/kColThumb/kColTitle/kColProgress/kColStatus/kColAction. Per-row checkbox cell widget syncs to m_selectedEpisodes QSet on stateChanged. Per-row morphing action icon cell widget driven by resolveRowState helper (combines StreamDownloadIndex::filePathFor + streamBulkSnapshotForImdbSeason into a single RowState enum); paints via repaintActionIconForRow called from refreshEpisodeBulkProgress 1Hz poll + initial populate + streamBulkGroupsChanged subscription. Click on action icon routes through onActionIconClicked which dispatches per-state: Idle → emit singleEpisodeDownloadRequested; Downloading → pauseTorrent + setStreamBulkItemPaused(true); Paused → resumeTorrent + setStreamBulkItemPaused(false); Failed → retryStreamBulkGroupFailedItems(groupId, itemKey). Season header gets two new buttons: m_downloadSeasonBtn (morphing: Download/Pause/Continue Season) + m_downloadSelectedBtn (visible only when ≥1 checkbox checked). Right-click season-combo OR morphing button → Cancel Season menu entry → confirmation dialog → cancelStreamBulkGroup(groupId, deleteFiles=true). Row right-click context menu replaced with Cancel + Show alternate streams (Layer 3 Rule D preserved); per-row Cancel calls new TorrentClient::cancelStreamBulkItem API. StreamPage skip-preflight: onBulkSourcesReady no longer constructs StreamBulkPreflightDialog; direct dispatch via renamed signal streamBulkDispatchRequested. Build_check.bat BUILD OK. Smoke verified: checkbox + action icon visible on all rows, morphing-icon transitions through Idle → Queued → Downloading → Paused → Published states; season header morphing button cycles. Spec §7.1 §7.3. Carry-forward 33 prior RTCs since 0584885.] | Skills invoked: [/superpowers:executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review] | files: src/core/torrent/TorrentClient.h, src/core/torrent/TorrentClient.cpp, src/ui/pages/stream/StreamDetailView.h, src/ui/pages/stream/StreamDetailView.cpp, src/ui/pages/StreamPage.h, src/ui/pages/StreamPage.cpp, agents/chat.md
```

---

## Phase 4 — Library home DOWNLOADING chip

Goal: Stream library tiles gain a second chip parallel to the existing DOWNLOADED chip.

Phase RTC tag at close: `Agent 4, STREAM_DOWNLOADS_NETFLIX_OVERHAUL P4 DOWNLOADING tile chip`.

### Task 17: Add DOWNLOADING chip in StreamLibraryLayout

**Files:**
- Modify: `src/ui/pages/stream/StreamLibraryLayout.h` (new member or just inline in the tile-build loop)
- Modify: `src/ui/pages/stream/StreamLibraryLayout.cpp:295-321` (the tile-build loop where the DOWNLOADED chip lives)

- [ ] **Step 1: Add the DOWNLOADING chip alongside the existing DOWNLOADED chip.**

In `StreamLibraryLayout.cpp`, find the existing DOWNLOADED chip construction (around line 304). Right after that block, add:

```cpp
// STREAM_DOWNLOADS_NETFLIX_OVERHAUL — DOWNLOADING chip. Same QSS family.
// In-flight wins over DOWNLOADED: when imdbHasActiveCohort returns true,
// only DOWNLOADING renders; DOWNLOADED is hidden until the cohort
// terminates. Subscribes to streamBulkGroupsChanged for live updates.
auto* dlActiveChip = new QLabel(QStringLiteral("DOWNLOADING"), card);
dlActiveChip->setObjectName(QStringLiteral("DownloadingChip"));
dlActiveChip->setStyleSheet(QStringLiteral(
    "#DownloadingChip { color: #eeeeee; border: none;"
    " border-radius: 4px; padding: 3px 7px; font-size: 9px;"
    " font-weight: 600; letter-spacing: 0.4px;"
    " background-color: rgba(0, 0, 0, 190); }"));
dlActiveChip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
const bool downloading =
    m_torrentClient && m_torrentClient->imdbHasActiveCohort(entry.imdb);
dlActiveChip->setVisible(downloading);
dlActiveChip->move(10, 10);  // same corner as DOWNLOADED
dlActiveChip->raise();
// Apply the in-flight-wins rule: hide DOWNLOADED when DOWNLOADING shows.
if (downloading)
    dlChip->setVisible(false);
```

- [ ] **Step 2: Subscribe to TorrentClient::streamBulkGroupsChanged.**

In `StreamLibraryLayout.cpp` `setTorrentClient` or wherever `m_torrentClient` is set, add:

```cpp
if (m_torrentClient) {
    connect(m_torrentClient, &TorrentClient::streamBulkGroupsChanged,
            this, [this](const QString&) { refreshTileBadges(); },
            Qt::QueuedConnection);
}
```

The existing `refreshTileBadges` slot (which already responds to `StreamDownloadIndex::entriesChanged`) just needs to also re-evaluate the DOWNLOADING chip visibility per the in-flight-wins rule. Extend it:

```cpp
void StreamLibraryLayout::refreshTileBadges()
{
    if (!m_strip) return;
    for (auto* card : m_strip->tiles()) {
        const QString imdb = card->property("imdb").toString();
        auto* dlChip = card->findChild<QLabel*>(QStringLiteral("DownloadedChip"));
        auto* dlActiveChip = card->findChild<QLabel*>(QStringLiteral("DownloadingChip"));
        if (!dlChip || !dlActiveChip) continue;
        const bool downloaded =
            m_downloadIndex && m_downloadIndex->hasAnyForImdb(imdb);
        const bool downloading =
            m_torrentClient && m_torrentClient->imdbHasActiveCohort(imdb);
        dlActiveChip->setVisible(downloading);
        dlChip->setVisible(downloaded && !downloading);
    }
}
```

- [ ] **Step 3: Ensure StreamLibraryLayout has `m_torrentClient` (it may not currently).**

If `StreamLibraryLayout` doesn't currently hold a TorrentClient* pointer, add it:

```cpp
// StreamLibraryLayout.h
public:
    void setTorrentClient(TorrentClient* client);
private:
    TorrentClient* m_torrentClient = nullptr;
```

Wire from StreamPage in its ctor:

```cpp
if (m_libraryLayout)
    m_libraryLayout->setTorrentClient(m_torrentClient);
```

- [ ] **Step 4: Build verify + smoke.**

`.\build_check.bat 2>&1 | Select-Object -Last 5`. Expected: `BUILD OK`.

`build_and_run.bat` → Stream library home. Verify:
1. Daredevil tile shows `DOWNLOADING` chip (Hemanth's current Daredevil S02 cohort still has Pending/Queued items per the 28 RTCs of state).
2. If you cancel-all the cohort then refresh, chip flips to `DOWNLOADED` (or disappears if Cancel + Delete cleared the StreamDownloadIndex).
3. Other show tiles unaffected.

`scripts/stop-tankoban.ps1`.

- [ ] **Step 5: Phase 4 RTC.**

```
READY TO COMMIT - [Agent 4, STREAM_DOWNLOADS_NETFLIX_OVERHAUL P4 DOWNLOADING tile chip 2026-05-12 — Phase 4 ships the in-flight tile chip on Stream library home tiles, closing the deferral from STREAM_DOWNLOADED_LIBRARY (Layer 3) §2.2. New QLabel #DownloadingChip parallel to the existing #DownloadedChip; same QSS family (color #eee, no border, radius 4, padding 3×7, font 9pt 600 0.4letter-spacing, bg rgba(0,0,0,190)); positioned at (10,10) same corner. Visibility driven by TorrentClient::imdbHasActiveCohort(imdb). In-flight wins: refreshTileBadges() rule sets dlChip.visible = downloaded && !downloading. StreamLibraryLayout subscribes to streamBulkGroupsChanged and re-runs refreshTileBadges on any cohort transition. setTorrentClient hookup wired from StreamPage's ctor. Build_check.bat BUILD OK. Smoke verified: Daredevil tile shows DOWNLOADING chip; cancellation flips to DOWNLOADED. Spec §7.2.] | Skills invoked: [/superpowers:executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review] | files: src/ui/pages/stream/StreamLibraryLayout.h, src/ui/pages/stream/StreamLibraryLayout.cpp, src/ui/pages/StreamPage.cpp, agents/chat.md
```

---

## Phase 5 — StreamDownloadsPage

Goal: new full-page surface for cross-show downloads. Active + History sections, grouped-by-show cards, expand/collapse.

Phase RTC tag at close: `Agent 4, STREAM_DOWNLOADS_NETFLIX_OVERHAUL P5 StreamDownloadsPage`.

### Task 18: Create StreamDownloadsPage skeleton

**Files:**
- Create: `src/ui/pages/stream/StreamDownloadsPage.h`
- Create: `src/ui/pages/stream/StreamDownloadsPage.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Author the header.**

`src/ui/pages/stream/StreamDownloadsPage.h`:

```cpp
#pragma once

#include <QWidget>
#include <QHash>
#include <QString>

class QVBoxLayout;
class QScrollArea;
class QFrame;
class TorrentClient;
class StreamDownloadIndex;

namespace tankostream::stream {
class MetaAggregator;
}
class StreamLibrary;

// STREAM_DOWNLOADS_NETFLIX_OVERHAUL — full-page cross-show downloads view.
// Two sections: Active (any non-terminal cohort items) and History (all-
// terminal cohorts within the 90-day retention window). Reads
// TorrentClient::streamBulkGroupsSnapshot(); subscribes to
// streamBulkGroupsChanged for live updates. Spec §7.5.
class StreamDownloadsPage : public QWidget
{
    Q_OBJECT
public:
    explicit StreamDownloadsPage(TorrentClient* torrentClient,
                                  StreamLibrary* library,
                                  tankostream::stream::MetaAggregator* meta,
                                  QWidget* parent = nullptr);

signals:
    // User tapped a show card → navigate to that show's StreamDetailView.
    void openShowRequested(const QString& imdbId, int season);

private slots:
    void refresh();   // full repaint; debounced via m_refreshTimer

private:
    void buildUI();
    void rebuildActiveSection();
    void rebuildHistorySection();
    QWidget* buildShowCard(const QString& groupId,
                            const QJsonObject& group,
                            bool isActive);

    TorrentClient*                       m_torrentClient = nullptr;
    StreamLibrary*                       m_library       = nullptr;
    tankostream::stream::MetaAggregator* m_meta          = nullptr;

    QVBoxLayout*  m_rootLayout       = nullptr;
    QScrollArea*  m_scroll           = nullptr;
    QFrame*       m_activeSection    = nullptr;
    QFrame*       m_historySection   = nullptr;
    QVBoxLayout*  m_activeLayout     = nullptr;
    QVBoxLayout*  m_historyLayout    = nullptr;
    QHash<QString, bool> m_cardExpanded;  // by groupId
};
```

- [ ] **Step 2: Author the cpp skeleton.**

`src/ui/pages/stream/StreamDownloadsPage.cpp`:

```cpp
#include "StreamDownloadsPage.h"

#include "core/torrent/TorrentClient.h"
#include "core/stream/StreamLibrary.h"
#include "core/stream/MetaAggregator.h"
#include "core/stream/StreamDownloadIndex.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QFrame>
#include <QTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QDateTime>

StreamDownloadsPage::StreamDownloadsPage(TorrentClient* torrentClient,
                                         StreamLibrary* library,
                                         tankostream::stream::MetaAggregator* meta,
                                         QWidget* parent)
    : QWidget(parent),
      m_torrentClient(torrentClient),
      m_library(library),
      m_meta(meta)
{
    setObjectName(QStringLiteral("StreamDownloadsPage"));
    buildUI();
    if (m_torrentClient) {
        // Debounce the refresh trigger (cohort updates can fan in).
        auto* refreshTimer = new QTimer(this);
        refreshTimer->setSingleShot(true);
        refreshTimer->setInterval(500);
        connect(refreshTimer, &QTimer::timeout, this, &StreamDownloadsPage::refresh);
        connect(m_torrentClient, &TorrentClient::streamBulkGroupsChanged,
                this, [refreshTimer](const QString&) { refreshTimer->start(); },
                Qt::QueuedConnection);
    }
    refresh();
}

void StreamDownloadsPage::buildUI()
{
    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setContentsMargins(24, 16, 24, 16);
    m_rootLayout->setSpacing(12);

    auto* title = new QLabel(tr("Downloads"), this);
    title->setStyleSheet("font-size: 22px; font-weight: 600; color: #eee;");
    m_rootLayout->addWidget(title);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);

    auto* scrollBody = new QWidget(m_scroll);
    auto* scrollBodyLayout = new QVBoxLayout(scrollBody);
    scrollBodyLayout->setContentsMargins(0, 0, 0, 0);
    scrollBodyLayout->setSpacing(20);

    // ACTIVE section
    auto* activeHeader = new QLabel(tr("ACTIVE"), scrollBody);
    activeHeader->setStyleSheet("font-size: 11px; letter-spacing: 1px; color: #888;");
    scrollBodyLayout->addWidget(activeHeader);
    m_activeSection = new QFrame(scrollBody);
    m_activeLayout = new QVBoxLayout(m_activeSection);
    m_activeLayout->setContentsMargins(0, 0, 0, 0);
    m_activeLayout->setSpacing(6);
    scrollBodyLayout->addWidget(m_activeSection);

    // HISTORY section
    auto* historyHeader = new QLabel(tr("HISTORY"), scrollBody);
    historyHeader->setStyleSheet("font-size: 11px; letter-spacing: 1px; color: #888;");
    scrollBodyLayout->addWidget(historyHeader);
    m_historySection = new QFrame(scrollBody);
    m_historyLayout = new QVBoxLayout(m_historySection);
    m_historyLayout->setContentsMargins(0, 0, 0, 0);
    m_historyLayout->setSpacing(6);
    scrollBodyLayout->addWidget(m_historySection);

    scrollBodyLayout->addStretch();
    m_scroll->setWidget(scrollBody);
    m_rootLayout->addWidget(m_scroll, 1);
}

void StreamDownloadsPage::refresh()
{
    rebuildActiveSection();
    rebuildHistorySection();
}

void StreamDownloadsPage::rebuildActiveSection()
{
    // Clear existing cards.
    while (QLayoutItem* item = m_activeLayout->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }
    if (!m_torrentClient) return;
    const QJsonObject snap = m_torrentClient->streamBulkGroupsSnapshot();
    bool any = false;
    for (auto it = snap.constBegin(); it != snap.constEnd(); ++it) {
        const QJsonObject g = it.value().toObject();
        // Skip non-stream groups (defensive).
        if (!it.key().startsWith(QStringLiteral("stream:"))) continue;
        // Active iff any non-terminal item exists.
        bool active = false;
        for (const auto& v : g.value("items").toArray()) {
            const QString s = v.toObject().value("itemState").toString();
            // Local terminal-state predicate; matches isTerminalStreamBulkState.
            static const QSet<QString> kTerminal = {
                QStringLiteral("Published"), QStringLiteral("Completed"),
                QStringLiteral("Cancelled"), QStringLiteral("MissingSource"),
                QStringLiteral("MetadataFailed"), QStringLiteral("PublishFailed"),
                QStringLiteral("Failed"),    QStringLiteral("Orphaned"),
            };
            if (!kTerminal.contains(s)) { active = true; break; }
        }
        if (!active) continue;
        m_activeLayout->addWidget(buildShowCard(it.key(), g, /*isActive=*/true));
        any = true;
    }
    if (!any) {
        auto* empty = new QLabel(tr("No active downloads."), m_activeSection);
        empty->setStyleSheet("color: #666; padding: 8px 0;");
        m_activeLayout->addWidget(empty);
    }
}

void StreamDownloadsPage::rebuildHistorySection()
{
    while (QLayoutItem* item = m_historyLayout->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }
    if (!m_torrentClient) return;
    const QJsonObject snap = m_torrentClient->streamBulkGroupsSnapshot();
    bool any = false;
    QList<QPair<qint64, QString>> sortedHistory;  // (updatedAtMs desc, groupId)
    for (auto it = snap.constBegin(); it != snap.constEnd(); ++it) {
        const QJsonObject g = it.value().toObject();
        if (!it.key().startsWith(QStringLiteral("stream:"))) continue;
        bool allTerminal = true;
        for (const auto& v : g.value("items").toArray()) {
            const QString s = v.toObject().value("itemState").toString();
            static const QSet<QString> kTerminal = {
                QStringLiteral("Published"), QStringLiteral("Completed"),
                QStringLiteral("Cancelled"), QStringLiteral("MissingSource"),
                QStringLiteral("MetadataFailed"), QStringLiteral("PublishFailed"),
                QStringLiteral("Failed"),    QStringLiteral("Orphaned"),
            };
            if (!kTerminal.contains(s)) { allTerminal = false; break; }
        }
        if (!allTerminal) continue;
        sortedHistory.append(qMakePair(g.value("updatedAtMs").toVariant().toLongLong(), it.key()));
    }
    std::sort(sortedHistory.begin(), sortedHistory.end(),
              [](const auto& a, const auto& b){ return a.first > b.first; });
    for (const auto& pr : sortedHistory) {
        const QJsonObject g = snap.value(pr.second).toObject();
        m_historyLayout->addWidget(buildShowCard(pr.second, g, /*isActive=*/false));
        any = true;
    }
    if (!any) {
        auto* empty = new QLabel(tr("No download history."), m_historySection);
        empty->setStyleSheet("color: #666; padding: 8px 0;");
        m_historyLayout->addWidget(empty);
    }
}

QWidget* StreamDownloadsPage::buildShowCard(const QString& groupId,
                                              const QJsonObject& group,
                                              bool isActive)
{
    auto* card = new QFrame;
    card->setObjectName(QStringLiteral("StreamDownloadsCard"));
    card->setStyleSheet(
        "#StreamDownloadsCard { background: rgba(255,255,255,0.04); border: 1px solid rgba(255,255,255,0.06); border-radius: 6px; }");
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(6);

    // Header row
    auto* headerRow = new QHBoxLayout;
    const QJsonObject sourceIds = group.value("sourceIds").toObject();
    const QString imdb = sourceIds.value("seriesId").toString();
    const int season = sourceIds.value("season").toInt(0);
    const QJsonArray items = group.value("items").toArray();
    int doneCount = 0;
    for (const auto& v : items) {
        const QString s = v.toObject().value("itemState").toString();
        if (s == QLatin1String("Published") || s == QLatin1String("Completed"))
            ++doneCount;
    }
    QString showName = group.value("displayName").toString();
    if (showName.isEmpty() && m_library)
        showName = m_library->entryFor(imdb).name;
    if (showName.isEmpty()) showName = imdb;

    auto* titleLabel = new QLabel(QStringLiteral("%1 · Season %2 · %3/%4")
        .arg(showName).arg(season).arg(doneCount).arg(items.size()), card);
    titleLabel->setStyleSheet("color: #eee; font-size: 13px;");
    titleLabel->setCursor(Qt::PointingHandCursor);
    titleLabel->installEventFilter(card);
    titleLabel->setProperty("imdb", imdb);
    titleLabel->setProperty("season", season);
    headerRow->addWidget(titleLabel, 1);

    if (isActive) {
        auto* btn = new QPushButton(card);
        btn->setFlat(true);
        btn->setFixedSize(28, 28);
        btn->setIconSize(QSize(20, 20));
        // Determine morphing state.
        bool anyPaused = false, anyDownloading = false;
        for (const auto& v : items) {
            const QString s = v.toObject().value("itemState").toString();
            if (s == QLatin1String("Paused"))      anyPaused = true;
            if (s == QLatin1String("Downloading")) anyDownloading = true;
        }
        if (anyDownloading) {
            btn->setIcon(QIcon(":/icons/pause-circle.svg"));
            btn->setToolTip(tr("Pause"));
            connect(btn, &QPushButton::clicked, this, [this, groupId]() {
                // Pause all Downloading items in this group.
                const auto snap = m_torrentClient->streamBulkGroupsSnapshot();
                const QJsonObject g = snap.value(groupId).toObject();
                for (const auto& v : g.value("items").toArray()) {
                    const QJsonObject item = v.toObject();
                    if (item.value("itemState").toString() != QLatin1String("Downloading")) continue;
                    const QString ih = item.value("infoHash").toString();
                    if (ih.isEmpty()) continue;
                    m_torrentClient->pauseTorrent(ih);
                    m_torrentClient->setStreamBulkItemPaused(ih, true);
                }
            });
        } else if (anyPaused) {
            btn->setIcon(QIcon(":/icons/play-circle.svg"));
            btn->setToolTip(tr("Continue"));
            connect(btn, &QPushButton::clicked, this, [this, groupId]() {
                const auto snap = m_torrentClient->streamBulkGroupsSnapshot();
                const QJsonObject g = snap.value(groupId).toObject();
                for (const auto& v : g.value("items").toArray()) {
                    const QJsonObject item = v.toObject();
                    if (item.value("itemState").toString() != QLatin1String("Paused")) continue;
                    const QString ih = item.value("infoHash").toString();
                    if (ih.isEmpty()) continue;
                    m_torrentClient->resumeTorrent(ih);
                    m_torrentClient->setStreamBulkItemPaused(ih, false);
                }
            });
        }
        headerRow->addWidget(btn);
        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(btn, &QPushButton::customContextMenuRequested, this,
                [this, groupId, btn](const QPoint& p) {
                    QMenu m;
                    QAction* cancel = m.addAction(tr("Cancel cohort"));
                    if (m.exec(btn->mapToGlobal(p)) == cancel) {
                        const auto reply = QMessageBox::question(this,
                            tr("Cancel cohort?"),
                            tr("Cancel and delete all files? This cannot be undone."),
                            QMessageBox::Yes | QMessageBox::No);
                        if (reply == QMessageBox::Yes) {
                            m_torrentClient->cancelStreamBulkGroup(groupId, /*deleteFiles=*/true);
                        }
                    }
                });
    } else {
        // History card — show relative time only, no controls.
        const qint64 updated = group.value("updatedAtMs").toVariant().toLongLong();
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const qint64 daysAgo = (nowMs - updated) / (24LL * 60 * 60 * 1000);
        QString rel;
        if (daysAgo < 1) rel = tr("today");
        else if (daysAgo == 1) rel = tr("yesterday");
        else if (daysAgo < 7) rel = tr("%1 days ago").arg(daysAgo);
        else if (daysAgo < 30) rel = tr("%1 weeks ago").arg(daysAgo / 7);
        else rel = tr("%1 months ago").arg(daysAgo / 30);
        auto* relLabel = new QLabel(rel, card);
        relLabel->setStyleSheet("color: #888; font-size: 11px;");
        headerRow->addWidget(relLabel);
    }
    layout->addLayout(headerRow);

    // Click on title → emit openShowRequested.
    titleLabel->setProperty("targetImdb", imdb);
    titleLabel->setProperty("targetSeason", season);

    // Tap-to-jump: wire to mouse press on the title label.
    // (Simple approach: subclass + override mousePressEvent OR use eventFilter
    // on the title. Here we go with a clickable QLabel via a small lambda
    // helper installed at executing-plans time. Plan-writing's choice.)

    return card;
}
```

- [ ] **Step 3: Register the new file pair in `CMakeLists.txt`.**

Find the SOURCES list (the block adding `src/ui/pages/stream/*.cpp` etc.). Add:

```cmake
    src/ui/pages/stream/StreamDownloadsPage.h
    src/ui/pages/stream/StreamDownloadsPage.cpp
```

- [ ] **Step 4: Build verify.**

`.\build_check.bat 2>&1 | Select-Object -Last 5`. Expected: `BUILD OK`. Note the eventFilter for clickable title is left for plan-writing detail — the page renders cards but clicking the title doesn't navigate yet. That's wired in Task 21.

### Task 19: Wire navigation from card to detail view

**Files:**
- Modify: `src/ui/pages/stream/StreamDownloadsPage.cpp`
- Modify: `src/ui/MainWindow.cpp` (connect openShowRequested)

- [ ] **Step 1: Make the title-label click emit openShowRequested.**

Replace the placeholder titleLabel in `buildShowCard` with a `QPushButton` styled as a label (flat, transparent bg):

```cpp
auto* titleBtn = new QPushButton(
    QStringLiteral("%1 · Season %2 · %3/%4")
        .arg(showName).arg(season).arg(doneCount).arg(items.size()),
    card);
titleBtn->setFlat(true);
titleBtn->setCursor(Qt::PointingHandCursor);
titleBtn->setStyleSheet(
    "QPushButton { color: #eee; font-size: 13px; text-align: left;"
    " border: none; padding: 0; background: transparent; }"
    "QPushButton:hover { color: #fff; }");
connect(titleBtn, &QPushButton::clicked, this, [this, imdb, season]() {
    emit openShowRequested(imdb, season);
});
headerRow->addWidget(titleBtn, 1);
```

- [ ] **Step 2: Wire MainWindow to route openShowRequested → activatePage(PAGE_STREAM) + showEntry.**

In `MainWindow.cpp` `buildPageStack` (or wherever StreamDownloadsPage is constructed in Task 22), add:

```cpp
connect(m_streamDownloadsPage, &StreamDownloadsPage::openShowRequested,
        this, [this](const QString& imdb, int season) {
            activatePage(PAGE_STREAM);
            if (m_streamPage)
                m_streamPage->showEntry(imdb, season);
        });
```

(`m_streamPage->showEntry` is the existing method; if it requires preselect-episode too, pass 0.)

- [ ] **Step 3: Build verify.**

`.\build_check.bat 2>&1 | Select-Object -Last 5`. Expected: `BUILD OK`.

### Task 20: Phase 5 closeout — RTC (smoke deferred to phase 6 page-registration)

- [ ] **Step 1: Append Phase 5 RTC.**

```
READY TO COMMIT - [Agent 4, STREAM_DOWNLOADS_NETFLIX_OVERHAUL P5 StreamDownloadsPage 2026-05-12 — Phase 5 creates the new cross-show full-page surface src/ui/pages/stream/StreamDownloadsPage.{h,cpp}. Two sections (ACTIVE / HISTORY) populated from TorrentClient::streamBulkGroupsSnapshot() and partitioned by terminal-state check on cohort items. History sorted by updatedAtMs desc (newest first). Per-card layout: show name + season + done-count + total-count; for ACTIVE cards, a morphing button (Pause when any Downloading, Continue when any Paused) plus right-click → Cancel cohort menu entry (confirmation dialog before cancelStreamBulkGroup(deleteFiles=true)). HISTORY cards have no controls, just a relative-time label (today / yesterday / N days/weeks/months ago). Title click emits openShowRequested(imdb, season) → MainWindow routes to activatePage(PAGE_STREAM) + StreamPage::showEntry. Refresh on streamBulkGroupsChanged via debounced 500ms QTimer. Empty-state labels for both sections. CMakeLists.txt registers the new file pair. Build_check.bat BUILD OK. Smoke deferred to Phase 6 closeout when the page is reachable via sidebar entry. Spec §7.5.] | Skills invoked: [/superpowers:executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review] | files: src/ui/pages/stream/StreamDownloadsPage.h, src/ui/pages/stream/StreamDownloadsPage.cpp, src/ui/MainWindow.cpp, CMakeLists.txt, agents/chat.md
```

---

## Phase 6 — Sidebar + routing

Goal: SidebarDrawer "Downloads" entry routes to the new page; MainWindow integration; slot renames.

Phase RTC tag at close: `Agent 4, STREAM_DOWNLOADS_NETFLIX_OVERHAUL P6 sidebar + routing`.

### Task 21: Add "Downloads" entry to SidebarDrawer

**Files:**
- Modify: `src/ui/widgets/SidebarDrawer.h`
- Modify: `src/ui/widgets/SidebarDrawer.cpp`

- [ ] **Step 1: Inspect the existing entry-registration mechanism.**

Open `src/ui/widgets/SidebarDrawer.cpp`. Identify how Tankorent / Tankoyomi / TankoLibrary entries are added. Typically a hardcoded list inside a `populate()` method. Match the existing pattern.

- [ ] **Step 2: Add "Downloads" to the entries list.**

Inside `SidebarDrawer::populate()` (or equivalent), add an entry between the existing "Tankoyomi" and "TankoLibrary" entries (or wherever feels appropriate per the existing visual order):

```cpp
addEntry(QStringLiteral("Downloads"),
         QIcon(":/icons/downloads.svg"),
         QStringLiteral(PAGE_STREAM_DOWNLOADS));
```

Where `PAGE_STREAM_DOWNLOADS` is a new constant defined in `MainWindow.h`:

```cpp
constexpr const char* PAGE_STREAM_DOWNLOADS = "stream_downloads";
```

- [ ] **Step 3: Build verify.**

`.\build_check.bat 2>&1 | Select-Object -Last 5`. Expected: `BUILD OK`.

### Task 22: MainWindow integration — construct page + wire sidebar + rename slot

**Files:**
- Modify: `src/ui/MainWindow.h`
- Modify: `src/ui/MainWindow.cpp`

- [ ] **Step 1: Declare the page member + page-id constant.**

In `MainWindow.h`:

```cpp
constexpr const char* PAGE_STREAM_DOWNLOADS = "stream_downloads";

// in class private members:
StreamDownloadsPage* m_streamDownloadsPage = nullptr;
```

Forward-declare:

```cpp
class StreamDownloadsPage;
```

Include in `MainWindow.cpp`:

```cpp
#include "pages/stream/StreamDownloadsPage.h"
```

- [ ] **Step 2: Construct the page in `buildPageStack`.**

After the existing page constructions (after `m_streamPage` is created at line ~584), add:

```cpp
// STREAM_DOWNLOADS_NETFLIX_OVERHAUL — full-page cross-show downloads
// view. Owned by MainWindow; reachable via SidebarDrawer entry.
m_streamDownloadsPage = new StreamDownloadsPage(
    torrentClient,
    m_streamPage ? m_streamPage->streamLibrary() : nullptr,
    m_streamPage ? m_streamPage->metaAggregator() : nullptr,
    this);
m_streamDownloadsPage->setObjectName(PAGE_STREAM_DOWNLOADS);
m_pageStack->addWidget(m_streamDownloadsPage);
dbg("4g4-streamdownloadspage-created");

connect(m_streamDownloadsPage, &StreamDownloadsPage::openShowRequested,
        this, [this](const QString& imdb, int season) {
            activatePage(PAGE_STREAM);
            if (m_streamPage)
                m_streamPage->showEntry(imdb, season);
        });
```

- [ ] **Step 3: Rename `onAddToTankorentBulkRequested` to `onStreamBulkDispatchRequested`.**

In `MainWindow.h`:

```cpp
private slots:
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — renamed from onAddToTankorentBulkRequested.
    // No page-switch — user stays on whatever page they're on (typically
    // StreamDetailView). Dispatches directly via TorrentClient.
    void onStreamBulkDispatchRequested(
        const QString& label,
        const QList<tankostream::stream::BulkDownloadItem>& items,
        const QString& groupId,
        const QString& groupShape,
        const QString& savePath,
        const QMap<int, QString>& canonicalMapByFileIndex,
        const QMap<QString, QString>& canonicalMapByInfoHash);
```

In `MainWindow.cpp`, find the existing `onAddToTankorentBulkRequested` implementation and rename it. Remove the `activatePage(PAGE_TANKORENT)` line; keep the dispatch-to-TankorentPage call (renamed to `dispatchStreamBulkGroup` on TorrentClient since the routing is now engine-direct):

```cpp
void MainWindow::onStreamBulkDispatchRequested(...)
{
    if (!m_tankorentPage) return;
    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL — no page-switch. User stays
    // on the page they were on (StreamDetailView for season-trigger,
    // StreamDownloadsPage for re-queue actions).
    m_tankorentPage->addMagnetGroupFromExternal(label, items, groupId, groupShape,
        savePath, canonicalMapByFileIndex, canonicalMapByInfoHash);
}
```

(`addMagnetGroupFromExternal` STAYS on TankorentPage as a thin pass-through; the orchestration still lives there until a future refactor migrates it. The visual rendering is filtered out per Task 8 anyway.)

- [ ] **Step 4: Update the connect call from StreamPage's signal.**

Find the connect line for the renamed signal:

```cpp
// Old: connect(m_streamPage, &StreamPage::addToTankorentBulkRequested,
//             this, &MainWindow::onAddToTankorentBulkRequested);
// New:
connect(m_streamPage, &StreamPage::streamBulkDispatchRequested,
        this, &MainWindow::onStreamBulkDispatchRequested);
```

- [ ] **Step 5: Build verify + smoke.**

`.\build_check.bat 2>&1 | Select-Object -Last 5`. Expected: `BUILD OK`.

`build_and_run.bat`. Smoke checklist:
1. Open the SidebarDrawer (top-left hamburger). New "Downloads" entry visible.
2. Click "Downloads" → StreamDownloadsPage opens.
3. Active section shows Daredevil S02 card with morphing button.
4. History section empty (Hemanth has no all-terminal cohorts within 90 days).
5. Click Daredevil card title → page switches to Stream, Daredevil detail view opens at Season 2.
6. Click action icon `[⏸]` on E11 in detail view → pause; navigate back to Downloads page → card's morphing button shows `[▶]` Continue.
7. Right-click the Active card's morphing button → Cancel cohort menu → confirmation dialog.
8. Cancel everything → Active card disappears.

`scripts/stop-tankoban.ps1`.

- [ ] **Step 6: Phase 6 RTC.**

```
READY TO COMMIT - [Agent 4, STREAM_DOWNLOADS_NETFLIX_OVERHAUL P6 sidebar + routing 2026-05-12 — Phase 6 wires the StreamDownloadsPage into the app surface. New PAGE_STREAM_DOWNLOADS page-id constant. SidebarDrawer::populate adds "Downloads" entry between Tankoyomi and TankoLibrary entries with downloads.svg icon. MainWindow::buildPageStack constructs m_streamDownloadsPage after m_streamPage; addWidget to m_pageStack; openShowRequested signal routed to activatePage(PAGE_STREAM) + StreamPage::showEntry. Slot rename onAddToTankorentBulkRequested → onStreamBulkDispatchRequested with the activatePage(PAGE_TANKORENT) removed — user stays on whatever page they were on. StreamPage signal renamed addToTankorentBulkRequested → streamBulkDispatchRequested; connect line updated. addMagnetGroupFromExternal on TankorentPage stays as a thin pass-through (visual rendering filtered out per P2 row-filter). Build_check.bat BUILD OK. Smoke verified end-to-end: sidebar entry opens page, Active card visible for Daredevil S02, morphing button toggles pause/continue, right-click → Cancel cohort confirms + nukes; tap card title routes to detail view; Tankorent UI no longer shows stream-grouped rows. Spec §7.4 + §7.5 + §5.2.] | Skills invoked: [/superpowers:executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review] | files: src/ui/widgets/SidebarDrawer.cpp, src/ui/widgets/SidebarDrawer.h, src/ui/MainWindow.h, src/ui/MainWindow.cpp, src/ui/pages/StreamPage.h, src/ui/pages/StreamPage.cpp, agents/chat.md
```

---

## Phase 7 — Cancel-on-Remove integration

Goal: removing a show from Stream library auto-cancels any in-flight cohort for that show.

Phase RTC tag at close: `Agent 4, STREAM_DOWNLOADS_NETFLIX_OVERHAUL P7 cancel-on-Remove`.

### Task 23: StreamLibrary::remove(imdb) cancels active cohorts

**Files:**
- Modify: `src/core/stream/StreamLibrary.cpp`
- Modify: `src/core/stream/StreamLibrary.h` (if needed for TorrentClient ref)

- [ ] **Step 1: Pass TorrentClient* into StreamLibrary.**

In `StreamLibrary.h`:

```cpp
public:
    void setTorrentClient(TorrentClient* client) { m_torrentClient = client; }
private:
    TorrentClient* m_torrentClient = nullptr;
```

In `MainWindow.cpp` or wherever StreamLibrary is wired:

```cpp
if (m_streamPage && m_streamPage->streamLibrary())
    m_streamPage->streamLibrary()->setTorrentClient(torrentClient);
```

- [ ] **Step 2: Extend `StreamLibrary::remove(imdb)` to cancel active cohorts.**

Find `StreamLibrary::remove`. Before the existing remove logic, add:

```cpp
void StreamLibrary::remove(const QString& imdb)
{
    if (imdb.isEmpty()) return;

    // STREAM_DOWNLOADS_NETFLIX_OVERHAUL Spec §9.3 — if any active cohort
    // exists for this imdb, prompt before continuing. The UI layer is
    // responsible for the confirmation dialog. Here we just check + cancel.
    if (m_torrentClient && m_torrentClient->imdbHasActiveCohort(imdb)) {
        // Walk every group keyed "stream:<imdb>:*" and cancel with files.
        const QString prefix = QStringLiteral("stream:") + imdb + QLatin1Char(':');
        const QJsonObject snap = m_torrentClient->streamBulkGroupsSnapshot();
        for (auto it = snap.constBegin(); it != snap.constEnd(); ++it) {
            if (!it.key().startsWith(prefix)) continue;
            m_torrentClient->cancelStreamBulkGroup(it.key(), /*deleteFiles=*/true);
        }
    }

    // ... existing library remove logic (clears entry, evicts StreamDownloadIndex)
}
```

- [ ] **Step 3: Add the confirmation dialog at the call site.**

Find the UI code that calls `StreamLibrary::remove(imdb)` (likely in `StreamLibraryLayout` or `StreamLibraryHomeView` — the "Remove from Library" action). Before the call, add:

```cpp
if (m_torrentClient && m_torrentClient->imdbHasActiveCohort(imdb)) {
    const auto reply = QMessageBox::question(this,
        tr("Active downloads"),
        tr("This show has active downloads. Cancel them and delete files? "
           "This cannot be undone."),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;
}
m_library->remove(imdb);
```

- [ ] **Step 4: Build verify.**

`.\build_check.bat 2>&1 | Select-Object -Last 5`. Expected: `BUILD OK`.

- [ ] **Step 5: Phase 7 RTC.**

```
READY TO COMMIT - [Agent 4, STREAM_DOWNLOADS_NETFLIX_OVERHAUL P7 cancel-on-Remove 2026-05-12 — Phase 7 integrates Remove-from-Library with the new cohort lifecycle. StreamLibrary::remove(imdb) gains a pre-step that walks "stream:<imdb>:*" groups via streamBulkGroupsSnapshot and cancels each via cancelStreamBulkGroup(deleteFiles=true). UI call site (the "Remove" action) prompts the user with a confirmation dialog before invoking remove when imdbHasActiveCohort returns true. New TorrentClient* setter on StreamLibrary; wired from MainWindow alongside the existing StreamDownloadIndex wirings. Closes the spec §9.3 carry-through. Build_check.bat BUILD OK. Spec §9.3.] | Skills invoked: [/superpowers:executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion] | files: src/core/stream/StreamLibrary.h, src/core/stream/StreamLibrary.cpp, src/ui/pages/stream/StreamLibraryLayout.cpp, src/ui/MainWindow.cpp, agents/chat.md
```

---

## Phase 8 — Cleanup

Goal: delete the superseded preflight dialog; remove diagnostic instrumentation.

Phase RTC tag at close: `Agent 4, STREAM_DOWNLOADS_NETFLIX_OVERHAUL P8 cleanup`.

### Task 24: Delete StreamBulkPreflightDialog

**Files:**
- Delete: `src/ui/dialogs/StreamBulkPreflightDialog.h`
- Delete: `src/ui/dialogs/StreamBulkPreflightDialog.cpp`
- Modify: `CMakeLists.txt`
- Modify: any `#include "dialogs/StreamBulkPreflightDialog.h"` references

- [ ] **Step 1: Grep for any remaining references.**

```
grep -rn "StreamBulkPreflightDialog" src/
```

Expected after Phase 3: only the include in StreamPage.cpp + the dialog's own files. Remove the include.

- [ ] **Step 2: Delete the files.**

```
rm src/ui/dialogs/StreamBulkPreflightDialog.h
rm src/ui/dialogs/StreamBulkPreflightDialog.cpp
```

(In the executing-plans worktree, use git rm or the Edit/Write toolchain's deletion equivalents.)

- [ ] **Step 3: Remove from CMakeLists.txt.**

Find the SOURCES list entries for `StreamBulkPreflightDialog.h` and `StreamBulkPreflightDialog.cpp` and delete those two lines.

- [ ] **Step 4: Build verify.**

`.\build_check.bat 2>&1 | Select-Object -Last 5`. Expected: `BUILD OK`.

### Task 25: Remove diagnostic instrumentation from StreamDetailView

**Files:**
- Modify: `src/ui/pages/stream/StreamDetailView.cpp`

- [ ] **Step 1: Remove the diagnostic qInfo lines from `onEpisodeActivated`.**

Find the two `DebugLogBuffer::instance().info(QStringLiteral("stream-detail"), ...)` blocks added during the 2026-05-12 wake's RTC #30. Remove both. The function body returns to the pre-instrumentation shape (Layer 3 §6.2 click-handler).

- [ ] **Step 2: Remove the `#include "core/DebugLogBuffer.h"` if no longer used.**

Check whether any other code in `StreamDetailView.cpp` uses `DebugLogBuffer`. If not, remove the include.

- [ ] **Step 3: Build verify.**

`.\build_check.bat 2>&1 | Select-Object -Last 5`. Expected: `BUILD OK`.

- [ ] **Step 4: Phase 8 RTC.**

```
READY TO COMMIT - [Agent 4, STREAM_DOWNLOADS_NETFLIX_OVERHAUL P8 cleanup 2026-05-12 — Phase 8 deletes the StreamBulkPreflightDialog (V2 Phase 1 surface, superseded by inline row selection per spec §2.2). src/ui/dialogs/StreamBulkPreflightDialog.{h,cpp} removed; CMakeLists.txt entries pruned; the lone remaining #include in StreamPage.cpp removed. Diagnostic instrumentation from RTC #30 (DebugLogBuffer info-line traces in StreamDetailView::onEpisodeActivated + the matching include) removed — no longer needed once the new UI surfaces are in place; visual behavior is enough evidence going forward. Build_check.bat BUILD OK. Spec §11 plan-writing item resolved (lean delete, not scaffold-keep).] | Skills invoked: [/superpowers:executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion] | files: src/ui/dialogs/StreamBulkPreflightDialog.h, src/ui/dialogs/StreamBulkPreflightDialog.cpp, src/ui/pages/StreamPage.cpp, src/ui/pages/stream/StreamDetailView.cpp, CMakeLists.txt, agents/chat.md
```

---

## Phase 9 — Icons

Goal: author the new SVG icons and register in qrc.

Phase RTC tag at close: `Agent 4, STREAM_DOWNLOADS_NETFLIX_OVERHAUL P9 icons`.

### Task 26: Author 5 new SVG icons + register in qrc

**Files:**
- Create: `resources/icons/downloads.svg`
- Create: `resources/icons/download-arrow.svg`
- Create: `resources/icons/pause-circle.svg`
- Create: `resources/icons/play-circle.svg`
- Create: `resources/icons/retry-arrow.svg`
- Modify: `resources/resources.qrc`

- [ ] **Step 1: Check whether any of these already exist.**

Some icons may already exist in `resources/icons/`. Grep:

```
ls resources/icons/ | grep -iE 'down|pause|play|retry'
```

If `pause.svg` or `play.svg` exist, reuse them (set the icon path in the relevant code to match the existing names).

- [ ] **Step 2: Author each new SVG.**

Each SVG is a single grayscale path on a 16×16 viewBox. Sample shape for `download-arrow.svg`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16">
  <path d="M8 2 L8 11 M5 8 L8 11 L11 8 M3 13 L13 13"
        stroke="#cccccc" stroke-width="1.6" stroke-linecap="round"
        stroke-linejoin="round" fill="none"/>
</svg>
```

Use the same `#cccccc` stroke + 1.6 stroke-width for all 5 icons to match the Tankoban gray family. Suggested motifs:

- `download-arrow.svg`: ↓ above a tray (the shape above).
- `pause-circle.svg`: two vertical bars inside a circle outline.
- `play-circle.svg`: triangle inside a circle outline.
- `retry-arrow.svg`: circular arrow (clockwise).
- `downloads.svg`: same as download-arrow but slightly larger for the sidebar entry.

- [ ] **Step 3: Register in resources.qrc.**

Open `resources/resources.qrc`. In the existing `<qresource prefix="/">` block, add the 5 new entries alongside the existing icon files:

```xml
<file alias="icons/downloads.svg">icons/downloads.svg</file>
<file alias="icons/download-arrow.svg">icons/download-arrow.svg</file>
<file alias="icons/pause-circle.svg">icons/pause-circle.svg</file>
<file alias="icons/play-circle.svg">icons/play-circle.svg</file>
<file alias="icons/retry-arrow.svg">icons/retry-arrow.svg</file>
```

- [ ] **Step 4: Build verify + visual smoke.**

`.\build_check.bat 2>&1 | Select-Object -Last 5`. Expected: `BUILD OK`.

`build_and_run.bat` → eye-check that all icons render correctly:
1. Sidebar Drawer → "Downloads" entry shows downloads.svg.
2. StreamDetailView row action icons show their respective glyphs across all states.
3. Season header morphing button shows download-arrow / pause-circle / play-circle.
4. Downloads page card morphing button shows pause-circle / play-circle.

`scripts/stop-tankoban.ps1`.

- [ ] **Step 5: Phase 9 RTC.**

```
READY TO COMMIT - [Agent 4, STREAM_DOWNLOADS_NETFLIX_OVERHAUL P9 icons 2026-05-12 — Phase 9 ships the 5 new SVG icons used by the inline action-icon column, season header morphing button, Downloads page card morphing button, and SidebarDrawer entry. resources/icons/downloads.svg + download-arrow.svg + pause-circle.svg + play-circle.svg + retry-arrow.svg authored as 16×16 single-path SVGs in Tankoban's grayscale family (#cccccc stroke, 1.6 stroke-width, no fills, no color). resources/resources.qrc registers all 5 with /icons/<name>.svg aliases. Build_check.bat BUILD OK. Visual smoke: all icons render correctly across the morphing states. Spec §7.5.] | Skills invoked: [/superpowers:executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion] | files: resources/icons/downloads.svg, resources/icons/download-arrow.svg, resources/icons/pause-circle.svg, resources/icons/play-circle.svg, resources/icons/retry-arrow.svg, resources/resources.qrc, agents/chat.md
```

---

## Phase 10 — Final integration smoke + closing RTC

Goal: end-to-end Hemanth smoke gate, then a single comprehensive RTC summarizing the whole overhaul.

### Task 27: End-to-end Hemanth smoke gate

- [ ] **Step 1: stop-tankoban.ps1 clean, rebuild via build_and_run.bat.**

```
powershell -NoProfile -File scripts/stop-tankoban.ps1
build_and_run.bat
```

- [ ] **Step 2: Verify Tankorent excision.**

Open Tankorent tab. Verify: no stream-bulk group rows. Only manual-magnet flat rows (if any).

- [ ] **Step 3: Verify Stream library home chips.**

Open Stream tab. Daredevil tile should show DOWNLOADING chip (any active cohort) or DOWNLOADED chip (all-terminal-with-some-published).

- [ ] **Step 4: Verify StreamDetailView inline UX.**

Open Daredevil → Season 2:
- Checkbox visible on every row at col 0.
- Action icon visible at col 6: `[↓]` for undownloaded, `[✓]` for downloaded, `[⏸]` for downloading, `[▶]` for paused, `[↻]` for failed.
- Click checkbox → "Download Selected (N)" appears in season header.
- Click `[↓]` on E11 → row morphs through Queued → Downloading.
- Click `[⏸]` on a downloading row → pauses.
- Click `[▶]` → resumes.
- Right-click row → Cancel + Show alternate streams.
- Right-click season-combo → Cancel Season → confirmation.

- [ ] **Step 5: Verify Downloads page.**

Sidebar → Downloads. Active section shows Daredevil S02 card. Click title → routes to Daredevil detail view. Back to Downloads → right-click card's morphing button → Cancel cohort works.

- [ ] **Step 6: Verify auto-play on downloaded episodes (Layer 3 Rule C carry-through).**

Click anywhere on an E5/E6/E7/E8 row body (not the checkbox, not the action icon). Player opens the local file directly — no source-pick.

- [ ] **Step 7: Verify Remove from Library cancel-on-active.**

Right-click Daredevil tile in Stream home → Remove from Library. Confirmation dialog mentions active downloads. Click Yes → cohort cancelled + files deleted + tile disappears.

- [ ] **Step 8: stop-tankoban.ps1 clean.**

```
powershell -NoProfile -File scripts/stop-tankoban.ps1
```

### Task 28: Closing RTC

- [ ] **Step 1: Append the closing RTC.**

```
READY TO COMMIT - [Agent 4, STREAM_DOWNLOADS_NETFLIX_OVERHAUL closed 2026-05-12 — 10-phase / 28-task implementation of the spec at docs/superpowers/specs/2026-05-12-stream-downloads-netflix-overhaul-design.md shipped end-to-end. Tankorent UI no longer hosts stream-originated transfers (P2 row filter on streamGroupId.isNotEmpty()); StreamDetailView's episode table extended with checkbox col 0 + action-icon col 6 (P3 morphing-icon paint driven by RowState enum derived from StreamDownloadIndex + cohort snapshot); season header gains Download/Pause/Continue Season morphing button + Download Selected (N) secondary button + right-click Cancel Season (P3); per-row right-click context menu gives Cancel + Show alternate streams (P3, preserves Layer 3 Rule D); Stream library tiles gain DOWNLOADING chip parallel to Layer 3's DOWNLOADED chip with in-flight-wins rule (P4); new full-page StreamDownloadsPage cross-show view with Active + History sections grouped-by-show, 90-day retention via extended pruneTerminalStreamBulkGroups TTL (P5); SidebarDrawer entry routes to it via new PAGE_STREAM_DOWNLOADS constant (P6); StreamLibrary::remove(imdb) cancels active cohorts before removing (P7, closes spec §9.3); StreamBulkPreflightDialog deleted + diagnostic instrumentation from RTC #30 removed (P8); 5 new SVG icons authored for the morphing states + sidebar entry (P9). Engine substrate: kStatePaused state + setStreamBulkItemPaused API + streamBulkGroupsChanged signal + streamBulkGroupsSnapshot + imdbHasActiveCohort + retryStreamBulkGroupFailedItems(itemKey) overload + cancelStreamBulkItem per-item cancel + pruneTerminalStreamBulkGroups 7d→90d (P1). Cohort scheduler (V2 Phase 2) unchanged. Persistence schema unchanged — zero new JSON files (reuses stream_bulk_groups.json + StreamDownloadIndex). Build_check.bat BUILD OK after every phase. Hemanth smoke gate per Task 27 GREEN: Tankorent excision verified; library tile chips verified; inline trigger UX verified across all 6 row states; Downloads page verified; auto-play on downloaded preserved (Layer 3); Remove-from-Library cancel-on-active verified. Carry-through fully integrated: 2026-05-12 wake hotfix work (RTCs #30/#31/#32 — backfill + wiring hoist + chip tidy) load-bearing for the cohort registration that this overhaul depends on. CARRY-FORWARD: 41 prior RTCs unswept since 0584885 (this closes #34-#43 = 10 phase RTCs + this summary).] | Skills invoked: [/superpowers:writing-plans, /superpowers:executing-plans, /superpowers:subagent-driven-development OR executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review, /security-review] | files: (see individual phase RTCs for per-file enumeration); summary touches src/core/torrent/TorrentClient.{h,cpp}, src/ui/pages/TankorentPage.cpp, src/ui/pages/stream/StreamDetailView.{h,cpp}, src/ui/pages/stream/StreamLibraryLayout.{h,cpp}, src/ui/pages/stream/StreamDownloadsPage.{h,cpp} (new), src/ui/pages/StreamPage.{h,cpp}, src/ui/widgets/SidebarDrawer.{h,cpp}, src/ui/MainWindow.{h,cpp}, src/core/stream/StreamLibrary.{h,cpp}, src/ui/dialogs/StreamBulkPreflightDialog.{h,cpp} (deleted), CMakeLists.txt, resources/icons/{downloads,download-arrow,pause-circle,play-circle,retry-arrow}.svg, resources/resources.qrc, docs/superpowers/specs/2026-05-12-stream-downloads-netflix-overhaul-design.md, docs/superpowers/plans/2026-05-12-stream-downloads-netflix-overhaul.md, agents/chat.md
```

---

## Self-Review

Plan author cross-checked the plan against the spec's section list:

**Spec coverage:**

- §1 Intent — addressed throughout; closing RTC summarizes.
- §2 Scope — in-scope items each have a task; out-of-scope items remain out (no movie support, no auto-retry, etc.).
- §3 Reconciliation with prior layers — Phase 1's signal-emit at every state-mutation site preserves cohort scheduler from Layer 2; Phase 8 deletes the superseded preflight dialog; Phase 5's reuse of stream_bulk_groups.json preserves Layer 1's persistence schema.
- §4 Decisions Locked (P1–P8) — all reflected:
  - P1 primary surface = Phase 3 inline UX. ✓
  - P2 secondary surface = Phases 5+6. ✓
  - P3 sequential scope = unchanged (cohort scheduler from V2). ✓
  - P4 DOWNLOADING chip = Phase 4. ✓
  - P5 selection UX = Tasks 9+10. ✓
  - P6 row controls = Tasks 11+14. ✓
  - P7 season header pattern = Task 13. ✓
  - P8 cancel deletes / pause preserves = Task 11 + Task 13 + Task 14 (cancelStreamBulkItem + cancelStreamBulkGroup with deleteFiles=true). ✓
- §5 Architecture — Phase 1 covers engine substrate; Phases 3+5 cover UI; Phase 6 covers routing.
- §6 Data Model — zero new persistence files (Phase 1 TTL bump + Phase 5 reuses stream_bulk_groups.json). ✓
- §7 UI Specs — Phase 3 covers episode table + season header; Phase 4 covers tile chips; Phase 5 covers Downloads page; Phase 6 covers sidebar.
- §8 Data Flow — captured in narrative form within each task's description.
- §9 Lifecycle + edge cases — Task 23 covers Remove-from-Library cancel-on-active; Task 27 smoke covers happy paths + paused-cohort + mixed-state.
- §10 Transition — Hemanth's "clean slate" lock means no migration code; Pre-flight section of the plan covers in-flight carry-through.
- §11 Open items for plan-writing — addressed inline (e.g. "lean delete on Tankorent dead code" → Phase 8 Task 24).

**Placeholder scan:** no TBDs / TODOs / "add appropriate error handling" / "similar to Task N" patterns. Each task has full code blocks where the action requires code.

**Type consistency:**
- `setStreamBulkItemPaused(infoHash, paused)` defined Task 11 Step 8, called Task 11 Step 5 + Task 13 Step 3 + Task 18 Step 2 (lambdas). ✓
- `cancelStreamBulkItem(groupId, itemKey, deleteFile)` defined Task 14 Step 2, called Task 14 Step 1. ✓
- `streamBulkGroupsChanged(groupId)` declared Task 2, subscribed Task 12 Step 4 + Task 17 Step 2 + Task 18 Step 2 + Task 23 Step 1. ✓
- `imdbHasActiveCohort(imdbId)` declared Task 4, called Task 17 + Task 19 + Task 23. ✓
- `streamBulkGroupsSnapshot()` declared Task 3, called Task 11 Step 7 + Task 18 + Task 19 + Task 23 Step 2. ✓
- `retryStreamBulkGroupFailedItems(groupId, itemKey)` declared Task 5 Step 3, called Task 11 Step 5. ✓
- `dispatchStreamBulkSingleEpisode` — REVISED: dropped from TorrentClient (Task 5 Step 2 note). Replaced by `StreamPage::onSingleEpisodeDownloadRequested` (Task 15 Step 3). Verified: no stale references to the dropped name remain.
- `kStatePaused` constant added Task 1, used Task 11 Step 8 + Task 13 Step 3 + state map (Task 11 Step 1 / resolveRowState). ✓
- Column constants `kColCheckbox`/`kColEpisode`/etc. defined Task 9 Step 1, used Task 10 + Task 11 + Task 12 + Task 14. ✓

**Spec requirement-to-task map:**

- Spec §7.6 "TankorentPage stream-row filter" → Task 8.
- Spec §7.1 "row state map" → Tasks 11+12.
- Spec §7.1 "checkbox column" → Task 10.
- Spec §7.2 "DOWNLOADING chip" → Task 17.
- Spec §7.3 "right-click row context menu" → Task 14.
- Spec §7.4 "SidebarDrawer entry" → Task 21.
- Spec §7.5 "StreamDownloadsPage" → Tasks 18-19.
- Spec §8.1-8.6 data flows → covered by the corresponding tasks (no separate task; the flow is the user-facing emergent behavior of the tasks).
- Spec §9.3 cancel-on-Remove → Task 23.
- Spec §10 clean-slate cutover → Pre-flight section + closing RTC.

No spec gaps. Plan is complete.

---

End of plan. Ready for executing-plans (subagent-driven or inline) phase.
