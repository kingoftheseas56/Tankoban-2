# Stream Async Race Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix two async/race bugs surfaced by Hemanth's 2026-05-18 smoke of yesterday's THREE_SMALL_FIXES arc. (Bug A) Movie Download button is silent-no-op when clicked before async stream load completes — yesterday's Task 1 made the button visible but didn't guard against pre-load clicks. (Bug B) Yesterday's Task 3 `QTimer::singleShot(0)` kick fires before the cross-thread torrent client registers the new bulk group, causing `refreshEpisodeBulkProgress` to read an empty snapshot and STOP the poll timer — actively worse than no fix at all.

**Architecture:** Both bugs are timing/race issues. Task A disables the movie Download button until `setStreamSources()` populates `m_lastChoices`. Task B reverts the Task 3 singleShot kicks + replaces them with `m_bulkPollTimer->start()` + a new `m_lastBulkDispatchTime` member + a grace-period check in `refreshEpisodeBulkProgress` so the timer isn't stopped during the snapshot-population race window. Both tasks single-file scope (StreamDetailView.cpp + .h for Task B's new member).

**Tech Stack:** Qt6 / C++20 / CMake-Ninja-MSVC. No new dependencies, no new tests (Tankoban smoke-first policy).

**Reference background:** Phase 1 root-cause investigation traced at chat.md ~2026-05-18 1:36pm. Click handler silent-fail at StreamDetailView.cpp:503-507. Timer-stop-on-empty at StreamDetailView.cpp:1303-1306. Both confirmed via static analysis + Hemanth's smoke evidence.

**Brotherhood contract notes:**
- No `git commit` — agents post READY TO COMMIT to chat.md per Rule 11
- Build verify = `build_check.bat`. Kill any running `Tankoban.exe` first to avoid LNK1168
- ASCII-only sweep on the diff
- One fix per RTC per `feedback_one_fix_per_rebuild.md`

---

## File Structure

**Files modified:**
- `src/ui/pages/stream/StreamDetailView.cpp` — both tasks
- `src/ui/pages/stream/StreamDetailView.h` — Task B only (new `m_lastBulkDispatchTime` member declaration)

No new files. No new tests. Each task is its own commit boundary.

---

## Task A: Movie Download Button Disabled Until Streams Load

**Files:**
- Modify: `src/ui/pages/stream/StreamDetailView.cpp` (button-init + setEnabled toggles)

**Background:** The click handler at line 503-507 silent-no-ops when `m_lastChoices.isEmpty()`:
```cpp
if (m_lastChoices.isEmpty()) {
    qWarning() << "...no loaded choices for" << m_currentImdb << "- ignoring click";
    return;
}
```
No UI feedback. Hemanth's smoke surfaced this: he clicked Download on a non-library movie before streams arrived, got nothing. Yesterday's Task 1 made the button visible too early — it should be visually disabled until `m_lastChoices` populates with at least one magnet.

**Goal:** Button is `setEnabled(false)` initially + when streams are loading + on stream-load error/placeholder. `setEnabled(true)` only when `setStreamSources` populates `m_lastChoices` with at least one magnet source.

- [ ] **Step 1: Initialize button disabled at construction**

Open `src/ui/pages/stream/StreamDetailView.cpp`. Find the `m_movieDownloadBtn` construction block (around line 490-500). After the existing setup lines (`setFixedHeight`, `setCursor`, `setIcon`, `setStyleSheet`) and BEFORE the `connect(...)` line at 501, insert:

```cpp
    m_movieDownloadBtn->setEnabled(false);
```

This makes the button greyed out the moment it's created. Visually, the user sees "this is here but not ready yet."

- [ ] **Step 2: Disable on loading + placeholder + error**

Find `setStreamSourcesLoading()` at line 759. Current body:
```cpp
void StreamDetailView::setStreamSourcesLoading()
{
    if (m_sourcesList) m_sourcesList->setLoading();
}
```

Replace with:
```cpp
void StreamDetailView::setStreamSourcesLoading()
{
    if (m_sourcesList) m_sourcesList->setLoading();
    if (m_movieDownloadBtn) m_movieDownloadBtn->setEnabled(false);
}
```

Find `setStreamSourcesError()` at line 776. Current body:
```cpp
void StreamDetailView::setStreamSourcesError(const QString& message)
{
    if (m_sourcesList) m_sourcesList->setError(message);
}
```

Replace with:
```cpp
void StreamDetailView::setStreamSourcesError(const QString& message)
{
    if (m_sourcesList) m_sourcesList->setError(message);
    if (m_movieDownloadBtn) m_movieDownloadBtn->setEnabled(false);
}
```

Find `setStreamSourcesPlaceholder()` at line 781. Current body:
```cpp
void StreamDetailView::setStreamSourcesPlaceholder(const QString& message)
{
    if (m_sourcesList) m_sourcesList->setPlaceholder(message);
}
```

Replace with:
```cpp
void StreamDetailView::setStreamSourcesPlaceholder(const QString& message)
{
    if (m_sourcesList) m_sourcesList->setPlaceholder(message);
    if (m_movieDownloadBtn) m_movieDownloadBtn->setEnabled(false);
}
```

- [ ] **Step 3: Enable on stream-arrival when at least one magnet present**

Find `setStreamSources()` at line 764-774. Current body:
```cpp
void StreamDetailView::setStreamSources(
    const QList<tankostream::stream::StreamPickerChoice>& choices,
    const QString&                                        savedChoiceKey)
{
    // THEATRE_DOWNLOAD_OVERHAUL E1 UX refinement 2026-05-17 — cache the
    // sorted choice list so the movie-row Download button can pick the
    // top-seeded magnet without re-running the aggregator. buildPickerChoices
    // already sorts magnets-with-seeders first (StreamSourceChoice.h:62-65).
    m_lastChoices = choices;
    if (m_sourcesList) m_sourcesList->setSources(choices, savedChoiceKey);
}
```

Replace with:
```cpp
void StreamDetailView::setStreamSources(
    const QList<tankostream::stream::StreamPickerChoice>& choices,
    const QString&                                        savedChoiceKey)
{
    // THEATRE_DOWNLOAD_OVERHAUL E1 UX refinement 2026-05-17 — cache the
    // sorted choice list so the movie-row Download button can pick the
    // top-seeded magnet without re-running the aggregator. buildPickerChoices
    // already sorts magnets-with-seeders first (StreamSourceChoice.h:62-65).
    m_lastChoices = choices;
    if (m_sourcesList) m_sourcesList->setSources(choices, savedChoiceKey);
    // STREAM_ASYNC_RACE_FIXES 2026-05-18 Task A — enable the movie Download
    // button only when at least one magnet source is present. Pre-fix the
    // button was clickable from the moment the movie-action-row showed,
    // silently no-op-ing when m_lastChoices was empty (Hemanth's 2026-05-18
    // smoke). Walking the choices for a magnet matches the click handler's
    // own filter at line 513-519.
    if (m_movieDownloadBtn) {
        bool hasMagnet = false;
        for (const auto& choice : m_lastChoices) {
            if (choice.sourceKind == QLatin1String("magnet")) {
                hasMagnet = true;
                break;
            }
        }
        m_movieDownloadBtn->setEnabled(hasMagnet);
    }
}
```

- [ ] **Step 4: Build verify**

Kill any running Tankoban first:
```powershell
Get-Process Tankoban -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process stremio-runtime -ErrorAction SilentlyContinue | Stop-Process -Force
```

Then build:
```
build_check.bat
```

Expected: `BUILD OK` near the end + exit 0.

- [ ] **Step 5: ASCII sweep on diff**

```powershell
$diff = git diff src/ui/pages/stream/StreamDetailView.cpp 2>$null
$nonAscii = $diff | Select-String -Pattern '[^\x00-\x7F]'
if ($nonAscii) { Write-Host "FOUND NON-ASCII:"; $nonAscii | Select-Object -First 5 } else { Write-Host "ASCII CLEAN" }
```

Expected: `ASCII CLEAN`.

- [ ] **Step 6: Post RTC for Task A**

Append to end of `agents/chat.md`:

```
## Agent 4 - STREAM_ASYNC_RACE_FIXES Task A: Movie Download button-disable - 2026-05-18

READY TO COMMIT - [Agent 4, STREAM_ASYNC_RACE_FIXES Task A: Movie Download button-disable fix per docs/superpowers/plans/2026-05-18-stream-async-race-fixes.md. Bug surfaced by Hemanth's 2026-05-18 smoke of yesterday's THREE_SMALL_FIXES Task 1: button became visible (Task 1 backfill worked) but was clickable before async stream load completed, click handler silent-no-op'd at line 503-507 (qWarning + return when m_lastChoices empty). Fix: button setEnabled(false) at construction (line ~501) + on setStreamSourcesLoading + on setStreamSourcesError + on setStreamSourcesPlaceholder; setEnabled(true) in setStreamSources only when at least one magnet source is present in m_lastChoices (mirrors click handler's filter at line 513-519). ~10 LOC modified across 4 methods + 1 line at construction. build_check.bat BUILD OK. ASCII sweep clean on diff. Smoke matrix for Hemanth: (a) open Theatre + search any movie NOT in your library + click into detail view -> Download button visible but GREYED OUT immediately, no silent-click footgun; (b) wait ~2-10s for streams to arrive in the Sources panel -> Download button transitions to ENABLED (full color); (c) click Download -> torrent dispatch fires + auto-add to library + episode/movie status updates; (d) movies already in library: same behavior, but streams may arrive faster from cache so button enables almost immediately.] | Skills invoked: [/superpowers:writing-plans, /superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion] | files: src/ui/pages/stream/StreamDetailView.cpp, agents/chat.md
```

- [ ] **Step 7: Report DONE to controller**

Include `git diff --stat src/ui/pages/stream/StreamDetailView.cpp` evidence.

---

## Task B: Revert Task 3 Kicks + Grace-Period Poll Timer

**Files:**
- Modify: `src/ui/pages/stream/StreamDetailView.h` (new `m_lastBulkDispatchTime` member declaration)
- Modify: `src/ui/pages/stream/StreamDetailView.cpp` (revert kicks + add force-start + grace check)

**Background:** Yesterday's Task 3 kicks at lines 529-534 + 645-650 fire `refreshEpisodeBulkProgress()` via `QTimer::singleShot(0, ...)` immediately after `emit theatreDownloadRequested(...)`. The torrent dispatch is cross-thread (libtorrent worker) and takes >1 event-loop tick to register the new bulk group. So when the refresh runs, the snapshot is empty, and the early-return at line 1303-1306 STOPS the poll timer entirely. Status stuck at "-" until navigation re-triggers `populateEpisodeTable`.

**Goal:** Replace the kicks with: (a) force-start `m_bulkPollTimer` after each emit, (b) record `m_lastBulkDispatchTime`, (c) modify `refreshEpisodeBulkProgress` to NOT stop the timer if a dispatch was recent (10s grace window).

- [ ] **Step 1: Add `m_lastBulkDispatchTime` member to StreamDetailView.h**

Open `src/ui/pages/stream/StreamDetailView.h`. Find the existing private members section that holds `m_bulkPollTimer`. Add adjacent to it:

```cpp
    // STREAM_ASYNC_RACE_FIXES 2026-05-18 Task B — stamped when a download
    // dispatch fires (movie or season-header pack). refreshEpisodeBulkProgress
    // checks this to avoid stopping the poll timer during the cross-thread
    // window where the torrent client hasn't yet registered the new bulk
    // group in its snapshot. Default-constructed (invalid) until first
    // dispatch.
    QDateTime m_lastBulkDispatchTime;
```

If `<QDateTime>` isn't already included in StreamDetailView.h, add `#include <QDateTime>` to the includes block.

- [ ] **Step 2: Revert Task 3 kicks at line 529-534 (movie download click handler)**

Open `src/ui/pages/stream/StreamDetailView.cpp`. Find the block at line 529-534 (movie download click handler — inside the `m_movieDownloadBtn` connect lambda, after the `emit theatreTopSeededDownloadRequested(...)` call):

```cpp
        // THREE_SMALL_FIXES 2026-05-18 Task 3 - kick episode-bulk-progress refresh
        // immediately so the per-row Status column updates without waiting for the
        // 1Hz poll tick (Hemanth's smoke 2026-05-17 showed row stuck at "-" until
        // view re-render). singleShot(0) delays by one event-loop tick so torrent
        // client snapshot has registered the new group before the refresh reads it.
        QTimer::singleShot(0, this, &StreamDetailView::refreshEpisodeBulkProgress);
```

Replace with:

```cpp
        // STREAM_ASYNC_RACE_FIXES 2026-05-18 Task B — record dispatch timestamp +
        // force-start the bulk-progress poll timer. The 1Hz poll will catch the
        // snapshot update naturally once the cross-thread torrent client
        // registers the new group. refreshEpisodeBulkProgress honors a 10s
        // grace window so it does NOT stop the timer on empty-snapshot reads
        // while a dispatch is still being processed. Replaces the
        // THREE_SMALL_FIXES 2026-05-18 Task 3 singleShot(0) kick which fired
        // too early + actively stopped the timer (Hemanth's 2026-05-18 smoke).
        m_lastBulkDispatchTime = QDateTime::currentDateTime();
        if (m_bulkPollTimer && !m_bulkPollTimer->isActive())
            m_bulkPollTimer->start();
```

- [ ] **Step 3: Revert Task 3 kicks at line 645-650 (season-header pack-options click handler)**

Find the identical block at line 645-650 (inside the `m_packOptionsBtn` connect lambda, after the `emit theatreDownloadRequested(...)` call). Same 6-line block:

```cpp
        // THREE_SMALL_FIXES 2026-05-18 Task 3 - kick episode-bulk-progress refresh
        // immediately so the per-row Status column updates without waiting for the
        // 1Hz poll tick (Hemanth's smoke 2026-05-17 showed row stuck at "-" until
        // view re-render). singleShot(0) delays by one event-loop tick so torrent
        // client snapshot has registered the new group before the refresh reads it.
        QTimer::singleShot(0, this, &StreamDetailView::refreshEpisodeBulkProgress);
```

Replace with the SAME block as Step 2 (the comment + the two LOC):

```cpp
        // STREAM_ASYNC_RACE_FIXES 2026-05-18 Task B — record dispatch timestamp +
        // force-start the bulk-progress poll timer. The 1Hz poll will catch the
        // snapshot update naturally once the cross-thread torrent client
        // registers the new group. refreshEpisodeBulkProgress honors a 10s
        // grace window so it does NOT stop the timer on empty-snapshot reads
        // while a dispatch is still being processed. Replaces the
        // THREE_SMALL_FIXES 2026-05-18 Task 3 singleShot(0) kick which fired
        // too early + actively stopped the timer (Hemanth's 2026-05-18 smoke).
        m_lastBulkDispatchTime = QDateTime::currentDateTime();
        if (m_bulkPollTimer && !m_bulkPollTimer->isActive())
            m_bulkPollTimer->start();
```

- [ ] **Step 4: Modify `refreshEpisodeBulkProgress` snapshot-empty branch (grace window)**

Find the snapshot-empty early-return at line 1303-1306:

```cpp
    if (snapshot.isEmpty()) {
        if (m_bulkPollTimer && m_bulkPollTimer->isActive())
            m_bulkPollTimer->stop();
        return;
    }
```

Replace with:

```cpp
    if (snapshot.isEmpty()) {
        // STREAM_ASYNC_RACE_FIXES 2026-05-18 Task B — don't stop the poll
        // timer if a dispatch happened recently. The cross-thread torrent
        // client may take up to ~few seconds to register the new bulk group;
        // stopping here would mean the row never updates without nav-away+back
        // (Hemanth's 2026-05-18 smoke). 10s grace window — long enough for
        // realistic worker-thread lag, short enough that idle drains still
        // terminate polling in reasonable time.
        constexpr qint64 kDispatchGraceMs = 10000;
        const bool recentDispatch = m_lastBulkDispatchTime.isValid()
            && m_lastBulkDispatchTime.msecsTo(QDateTime::currentDateTime())
                 < kDispatchGraceMs;
        if (!recentDispatch && m_bulkPollTimer && m_bulkPollTimer->isActive())
            m_bulkPollTimer->stop();
        return;
    }
```

- [ ] **Step 5: Build verify**

Kill any running Tankoban first:
```powershell
Get-Process Tankoban -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process stremio-runtime -ErrorAction SilentlyContinue | Stop-Process -Force
```

Then build:
```
build_check.bat
```

Expected: `BUILD OK`. Failure modes:
- Missing `<QDateTime>` include in .h — Step 1 should have added it; verify
- `QDateTime` not in scope in .cpp — usually already included transitively, but add `#include <QDateTime>` to the .cpp Qt-includes block if needed

- [ ] **Step 6: ASCII sweep on diff**

```powershell
$diff = git diff src/ui/pages/stream/StreamDetailView.h src/ui/pages/stream/StreamDetailView.cpp 2>$null
$nonAscii = $diff | Select-String -Pattern '[^\x00-\x7F]'
if ($nonAscii) { Write-Host "FOUND NON-ASCII:"; $nonAscii | Select-Object -First 5 } else { Write-Host "ASCII CLEAN" }
```

Expected: `ASCII CLEAN`.

- [ ] **Step 7: Post RTC for Task B**

Append to end of `agents/chat.md`:

```
## Agent 4 - STREAM_ASYNC_RACE_FIXES Task B: Revert Task 3 kicks + grace-period poll timer - 2026-05-18

READY TO COMMIT - [Agent 4, STREAM_ASYNC_RACE_FIXES Task B: revert yesterday's THREE_SMALL_FIXES Task 3 singleShot(0) kicks + replace with force-start poll timer + grace-period check per docs/superpowers/plans/2026-05-18-stream-async-race-fixes.md. Bug surfaced by Hemanth's 2026-05-18 smoke: Task 3's QTimer::singleShot(0, this, &StreamDetailView::refreshEpisodeBulkProgress) fires before the cross-thread torrent client registers the new bulk group in its snapshot; refreshEpisodeBulkProgress's snapshot-empty early-return at line 1303-1306 then STOPS the poll timer; status stuck at "-" until navigation re-triggers populateEpisodeTable. Task 3 was actively WORSE than no fix at all. Fix: (1) revert 6-LOC kick blocks at line 529-534 + line 645-650 (movie download click + season-header pack-options click); (2) replace with m_lastBulkDispatchTime = QDateTime::currentDateTime() stamp + force-start m_bulkPollTimer; (3) add QDateTime m_lastBulkDispatchTime member to StreamDetailView.h (default-constructed invalid until first dispatch); (4) modify refreshEpisodeBulkProgress snapshot-empty branch to honor a 10s grace window — don't stop the timer if a dispatch was recent. ~25 LOC modified across .cpp (-12 +20) and .h (+8). build_check.bat BUILD OK. ASCII sweep clean on diff. Smoke matrix for Hemanth: (a) open any series in Theatre + click season-header Download -> episode rows show Status column update within ~1s (1Hz poll tick), no longer requires nav-away-and-back; (b) movie Download click: same kick path fires, harmless (movie path early-returns inside refreshEpisodeBulkProgress on m_currentType != "series"); (c) idle drain — when downloads complete + snapshot empties + 10s pass with no new dispatch, the poll timer correctly stops (no CPU burn on idle detail view); (d) double-click stress: two rapid dispatches re-stamp m_lastBulkDispatchTime + timer keeps polling correctly.] | Skills invoked: [/superpowers:writing-plans, /superpowers:executing-plans, /superpowers:systematic-debugging, /build-verify, /superpowers:verification-before-completion] | files: src/ui/pages/stream/StreamDetailView.h, src/ui/pages/stream/StreamDetailView.cpp, agents/chat.md
```

- [ ] **Step 8: Report DONE to controller**

Include `git diff --stat src/ui/pages/stream/StreamDetailView.h src/ui/pages/stream/StreamDetailView.cpp` evidence.

---

## Self-review pass (done by plan-writer)

**Spec coverage:** Task A → Bug A (movie Download silent-fail). Task B → Bug B (episode rows stuck at "-" until navigation). Both bugs addressed. ✓

**Placeholder scan:** No "TBD" / "TODO" / "implement later" / "similar to Task N" in any step. Every step has concrete code blocks. ✓

**Type consistency:** `m_movieDownloadBtn` (existing), `m_lastChoices` (existing), `m_bulkPollTimer` (existing), `m_lastBulkDispatchTime` (new, declared in Task B Step 1, used consistently throughout Task B). `QDateTime::currentDateTime()` + `msecsTo` are standard Qt6 API. `m_movieDownloadBtn->setEnabled(bool)` is standard QWidget API. ✓

**Ambiguity check:** Task A's `setEnabled(true)` in `setStreamSources` is gated on "at least one magnet" — code mirrors the click handler's own filter at line 513-519 so the enable signal matches the click handler's success path exactly. Task B's grace window is concretely 10s (`constexpr qint64 kDispatchGraceMs = 10000`); no magic-number ambiguity. ✓

**Scope sanity:** Task A = ~10 LOC, Task B = ~25 LOC. Both single-file (Task B adds 1 member to .h). Each task is its own RTC + commit boundary per `feedback_one_fix_per_rebuild.md`. Total ~35 LOC. ✓
