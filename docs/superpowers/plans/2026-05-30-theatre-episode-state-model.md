# Theatre Episode State Model — Implementation Plan (Phase 1)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every Theatre episode row derive its state from ONE disk-first source of truth (file on disk = Downloaded, full stop), feeding both the status text and the action control, and delete the second (legacy cohort `RowState`) system that disagreed with it.

**Architecture:** A pure-logic `deriveEpisodeDisplayState()` (testable, in `core/stream`) maps {on-disk, transfer-state, %} → one of five display states. A thin `StreamDetailView` gatherer feeds it (disk via `StreamDownloadIndex::filePathFor`+`QFileInfo::exists`; in-progress via the existing engine snapshot, consulted ONLY for not-on-disk episodes). One unified per-row painter sets the status cell + the action control (Download / Pause / Resume / **Play** text button / Retry). The legacy `RowState`/`resolveRowState`/`actionIconForState` cluster and the overlapping painters are removed; the season header button migrates to the same disk-first derivation.

**Tech Stack:** C++17, Qt 6 (QTableWidget, QPushButton, QMenu, QDesktopServices), libtorrent via `TorrentClient`, GoogleTest (`tankoban_tests`). Build: `build_check.bat`; smoke: `out\tankoctl.exe` + disk inspection + relaunch.

**Source spec:** [docs/superpowers/specs/2026-05-30-theatre-episode-state-model-design.md](../specs/2026-05-30-theatre-episode-state-model-design.md)

---

## Discipline notes (read before starting)

- **TDD applies to pure logic only.** Task 1 (`deriveEpisodeDisplayState`) gets real GoogleTest coverage. The UI tasks (2–5) touch `QTableWidget`/`QMenu`/engine glue — verified by **code-walk + `build_check` BUILD OK + tankoctl/disk smoke + relaunch**, per CLAUDE.md (pure-logic-only TDD).
- **gov-v13: flat-on-master, Path A commits.** Do NOT self-commit to master. After each task's build/test passes, post a `READY TO COMMIT — [Agent 4, ...]` line; Agent 0 batches. (If executing inline under Agent 4's own Opus session, Agent 4 may commit its own arc per the once-only precedent — but default to RTC.) **No worktrees** (gov-v13).
- **Build discipline (hard-won):** kill the app FIRST (`Get-Process Tankoban | Stop-Process -Force`), build via the **PowerShell tool** (`& '.\build_check.bat'`), and **verify `out\Tankoban.exe` LastWriteTime advanced** before any smoke. "BUILD OK" alone is not proof — a running exe lock makes the relink silently no-op. See `feedback_verify_exe_mtime_after_build`.
- **Reads may be semantically-summarized.** Before editing any anchor, `grep`/Read the REAL lines in the live file — do not trust line numbers from this plan blindly; they orient, the grep confirms.
- **Shared tree has live work** from Agent 1 (ComicsSeriesView) + Agent 2 (BooksPage). Stage ONLY this arc's files; never `git add -A`.

## File Structure

**Create:**
- `src/core/stream/EpisodeDisplayState.h` — `EpisodeDisplayState` enum + `EpisodeStateInputs` struct + `deriveEpisodeDisplayState()` decl. Pure; no Qt-UI deps (QString only).
- `src/core/stream/EpisodeDisplayState.cpp` — the derivation (priority logic).
- `tests/core/stream/test_episode_display_state.cpp` — GoogleTest coverage.

**Modify:**
- `cmake/TankobanSources.cmake` — add `EpisodeDisplayState.cpp` (next to `AutoSourcePicker.cpp`).
- `cmake/TankobanTests.cmake` — add `test_episode_display_state.cpp` (next to `test_auto_source_picker.cpp`).
- `src/ui/pages/stream/StreamDetailView.cpp` — the gatherer, the unified row painter, the action-click router, the context menu, the deletion pass, and the season-button migration.
- `src/ui/pages/stream/StreamDetailView.h` — new member-helper declarations; remove dead ones in Task 5.

**Remove (Task 5, Theatre state path in StreamDetailView.cpp):**
- `RowState` enum, `RowStateInput`, `resolveRowState`, `actionIconForState`, `actionIconForSubstrateState`, `repaintActionIconForRow`, and the cohort-snapshot reads used only for per-row *state* (the live progress snapshot stays as the in-progress data source).

---

## Phase 1 — disk-first episode state model

### Task 1: `deriveEpisodeDisplayState` pure-logic core (TDD)

**Files:**
- Create: `src/core/stream/EpisodeDisplayState.h`
- Create: `src/core/stream/EpisodeDisplayState.cpp`
- Test: `tests/core/stream/test_episode_display_state.cpp`
- Modify: `cmake/TankobanSources.cmake`, `cmake/TankobanTests.cmake`

- [ ] **Step 1: Write the header**

```cpp
// src/core/stream/EpisodeDisplayState.h
#pragma once

namespace tankostream::stream {

// The single display state for one episode row. Derived disk-first:
// on-disk ALWAYS wins (a downloaded file is Downloaded even if a stale
// transfer record says otherwise). Only not-on-disk episodes consult the
// engine for in-progress detail.
enum class EpisodeDisplayState {
    NotDownloaded,  // no file, no active transfer  -> Download affordance
    Downloading,    // not on disk, active transfer -> Pause affordance + N%
    Paused,         // not on disk, transfer paused -> Resume affordance + N%
    Failed,         // not on disk, transfer errored-> Retry affordance
    Downloaded,     // file on disk                 -> Play affordance
};

// Inputs gathered by the caller (disk check + engine transfer lookup).
struct EpisodeStateInputs {
    bool onDisk      = false;  // StreamDownloadIndex::filePathFor + QFileInfo::exists
    bool hasTransfer = false;  // engine has a transfer covering this episode
    bool paused      = false;  // that transfer is paused
    bool failed      = false;  // that transfer is errored
    int  progressPct = 0;      // 0..100 (only meaningful when hasTransfer)
};

// Priority: onDisk > failed > paused > downloading > none.
EpisodeDisplayState deriveEpisodeDisplayState(const EpisodeStateInputs& in);

}  // namespace tankostream::stream
```

- [ ] **Step 2: Write the failing test**

```cpp
// tests/core/stream/test_episode_display_state.cpp
#include <gtest/gtest.h>
#include "core/stream/EpisodeDisplayState.h"

using tankostream::stream::deriveEpisodeDisplayState;
using tankostream::stream::EpisodeDisplayState;
using tankostream::stream::EpisodeStateInputs;

static EpisodeStateInputs in(bool onDisk, bool hasTransfer, bool paused, bool failed, int pct) {
    EpisodeStateInputs i;
    i.onDisk = onDisk; i.hasTransfer = hasTransfer; i.paused = paused;
    i.failed = failed; i.progressPct = pct;
    return i;
}

TEST(EpisodeDisplayState, OnDiskIsAlwaysDownloaded) {
    // The exact bug: a stale "downloading"/"pending" transfer must NOT override
    // a file that is actually on disk.
    EXPECT_EQ(deriveEpisodeDisplayState(in(true,  true,  false, false, 40)),
              EpisodeDisplayState::Downloaded);
    EXPECT_EQ(deriveEpisodeDisplayState(in(true,  false, false, false, 0)),
              EpisodeDisplayState::Downloaded);
    EXPECT_EQ(deriveEpisodeDisplayState(in(true,  true,  true,  true,  0)),
              EpisodeDisplayState::Downloaded);
}

TEST(EpisodeDisplayState, NoDiskNoTransferIsNotDownloaded) {
    EXPECT_EQ(deriveEpisodeDisplayState(in(false, false, false, false, 0)),
              EpisodeDisplayState::NotDownloaded);
}

TEST(EpisodeDisplayState, ActiveTransferIsDownloading) {
    EXPECT_EQ(deriveEpisodeDisplayState(in(false, true, false, false, 30)),
              EpisodeDisplayState::Downloading);
}

TEST(EpisodeDisplayState, PausedTransferIsPaused) {
    EXPECT_EQ(deriveEpisodeDisplayState(in(false, true, true, false, 30)),
              EpisodeDisplayState::Paused);
}

TEST(EpisodeDisplayState, FailedBeatsPausedAndDownloading) {
    EXPECT_EQ(deriveEpisodeDisplayState(in(false, true, true, true, 10)),
              EpisodeDisplayState::Failed);
    EXPECT_EQ(deriveEpisodeDisplayState(in(false, true, false, true, 10)),
              EpisodeDisplayState::Failed);
}
```

- [ ] **Step 3: Add both files to CMake.**

`grep -n "AutoSourcePicker.cpp" cmake/TankobanSources.cmake` → add `src/core/stream/EpisodeDisplayState.cpp` on the next line, same indentation.
`grep -n "test_auto_source_picker.cpp" cmake/TankobanTests.cmake` → add `tests/core/stream/test_episode_display_state.cpp` on the next line, same indentation.

- [ ] **Step 4: Run the test to verify it FAILS (red).**

Run (PowerShell): `& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64; & "C:\tools\cmake-3.31.6-windows-x86_64\bin\cmake.exe" --build out --target tankoban_tests`
Expected: link error — `deriveEpisodeDisplayState` unresolved (no .cpp yet).

- [ ] **Step 5: Write the implementation.**

```cpp
// src/core/stream/EpisodeDisplayState.cpp
#include "core/stream/EpisodeDisplayState.h"

namespace tankostream::stream {

EpisodeDisplayState deriveEpisodeDisplayState(const EpisodeStateInputs& in)
{
    // Disk is the single source of truth — a file on disk is Downloaded,
    // full stop, regardless of any stale transfer/index record.
    if (in.onDisk)       return EpisodeDisplayState::Downloaded;
    if (!in.hasTransfer) return EpisodeDisplayState::NotDownloaded;
    if (in.failed)       return EpisodeDisplayState::Failed;
    if (in.paused)       return EpisodeDisplayState::Paused;
    return EpisodeDisplayState::Downloading;
}

}  // namespace tankostream::stream
```

- [ ] **Step 6: Build + run tests (green).**

Run (PowerShell, vcvarsall + cmake as Step 4), then: `cd out; & .\tankoban_tests.exe --gtest_filter=EpisodeDisplayState.*` (ensure `C:\tools\qt6sdk\6.10.2\msvc2022_64\bin` on PATH).
Expected: 5 tests PASS.

- [ ] **Step 7: RTC (Path A).**

Post to chat.md: `READY TO COMMIT — [Agent 4, THEATRE_EPISODE_STATE_MODEL P1.T1]: deriveEpisodeDisplayState pure-logic core + 5 GoogleTests (disk-first priority). Files: src/core/stream/EpisodeDisplayState.{h,cpp}, tests/core/stream/test_episode_display_state.cpp, cmake/TankobanSources.cmake, cmake/TankobanTests.cmake.`

### Task 2: Disk-first gatherer + unified per-row painter

**Files:**
- Modify: `src/ui/pages/stream/StreamDetailView.cpp`, `src/ui/pages/stream/StreamDetailView.h`

Verification = code-walk + build + smoke (UI glue).

- [ ] **Step 1: Add the include + member-helper decls (StreamDetailView.h).**

In the private section of `StreamDetailView`, add:
```cpp
    // THEATRE_EPISODE_STATE_MODEL (2026-05-30) — disk-first per-episode state.
    tankostream::stream::EpisodeDisplayState episodeDisplayState(int season, int episode) const;
    // Repaints ONE row's status cell + action control from episodeDisplayState.
    void refreshEpisodeRow(int row, int season, int episode);
    // Repaints every visible row of the active season via refreshEpisodeRow.
    void refreshAllEpisodeRows();
```
And in StreamDetailView.cpp near the other includes: `#include "core/stream/EpisodeDisplayState.h"`.

- [ ] **Step 2: Implement the gatherer `episodeDisplayState`.**

Add to StreamDetailView.cpp. Disk check via the existing index; in-progress via the engine snapshot (the SAME `streamBulkSnapshotForImdbSeason` already used in `populateEpisodeTable` — `QHash<int episode, QPair<QString state,int pct>>`). Confirm the snapshot's exact type with `grep -n "streamBulkSnapshotForImdbSeason" src/core/torrent/TorrentClient.h` before writing.
```cpp
tankostream::stream::EpisodeDisplayState
StreamDetailView::episodeDisplayState(int season, int episode) const
{
    using tankostream::stream::EpisodeStateInputs;
    EpisodeStateInputs in;

    // Disk = source of truth.
    if (m_downloadIndex && !m_currentImdb.isEmpty()) {
        const auto p = m_downloadIndex->filePathFor(m_currentImdb, season, episode);
        if (p.has_value() && QFileInfo::exists(*p))
            in.onDisk = true;
    }
    // Only consult the engine for NOT-on-disk episodes.
    if (!in.onDisk && m_torrentClient && !m_currentImdb.isEmpty()) {
        const auto snap = m_torrentClient->streamBulkSnapshotForImdbSeason(m_currentImdb, season);
        const auto it = snap.constFind(episode);
        if (it != snap.constEnd()) {
            in.hasTransfer = true;
            const QString st = it.value().first;       // cohort state string
            in.progressPct  = it.value().second;
            in.paused = (st == QLatin1String("Paused"));
            in.failed = (st == QLatin1String("Failed")
                      || st == QLatin1String("MetadataFailed")
                      || st == QLatin1String("PublishFailed")
                      || st == QLatin1String("MissingSource"));
        }
    }
    return tankostream::stream::deriveEpisodeDisplayState(in);
}
```
(Note: the snapshot is the live in-progress data source — what Task 5 removes is the broken `RowState` *resolution* layer, not this live read.)

- [ ] **Step 3: Implement `refreshEpisodeRow` — the ONE painter (status text + action control).**

This is the unified replacement for the scattered painters. It sets the status cell text and rebuilds the action control per state. The action control: for `Downloaded` a **text** "Play" `QPushButton`; for all others an icon button. Reuse the existing action-cell holder at `kColAction` (built in `populateEpisodeTable`). Find the existing action button via the cell widget.
```cpp
void StreamDetailView::refreshEpisodeRow(int row, int season, int episode)
{
    if (!m_episodeTable || row < 0 || row >= m_episodeTable->rowCount())
        return;
    using S = tankostream::stream::EpisodeDisplayState;
    const S state = episodeDisplayState(season, episode);

    // Progress % for in-progress states (engine snapshot already read in
    // episodeDisplayState; re-read cheaply here for the % text).
    int pct = 0;
    if (m_torrentClient && !m_currentImdb.isEmpty()) {
        const auto snap = m_torrentClient->streamBulkSnapshotForImdbSeason(m_currentImdb, season);
        const auto it = snap.constFind(episode);
        if (it != snap.constEnd()) pct = it.value().second;
    }

    // --- Status text cell (kColStatus) ---
    if (auto* statusItem = m_episodeTable->item(row, kColStatus)) {
        QString text;
        switch (state) {
        case S::Downloading: text = tr("Downloading %1%").arg(pct); break;
        case S::Paused:      text = tr("Paused %1%").arg(pct);      break;
        case S::Failed:      text = tr("Failed");                   break;
        case S::Downloaded:  text = QString();                      break;  // Play button carries it
        case S::NotDownloaded: text = QString();                    break;
        }
        statusItem->setText(text);
    }

    // --- Action control cell (kColAction) ---
    auto* holder = m_episodeTable->cellWidget(row, kColAction);
    if (!holder) return;
    auto* btn = holder->findChild<QPushButton*>();
    if (!btn) return;

    btn->setProperty("episodeNum", episode);
    if (state == S::Downloaded) {
        btn->setIcon(QIcon());
        btn->setText(tr("Play"));
        btn->setToolTip(tr("Play downloaded episode"));
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { background: rgba(255,255,255,0.10);"
            "  border: 1px solid rgba(255,255,255,0.18); border-radius: 4px;"
            "  color: #e0e0e0; font-size: 11px; padding: 2px 10px; }"
            "QPushButton:hover { background: rgba(255,255,255,0.16); }"));
        btn->setMinimumWidth(48);
    } else {
        btn->setText(QString());
        btn->setStyleSheet(QStringLiteral("QPushButton { border: none; background: transparent; }"));
        btn->setMinimumWidth(0);
        QString icon, tip;
        switch (state) {
        case S::NotDownloaded: icon = ":/icons/download-arrow.svg"; tip = tr("Download episode"); break;
        case S::Downloading:   icon = ":/icons/pause-circle.svg";   tip = tr("Pause download");   break;
        case S::Paused:        icon = ":/icons/play-circle.svg";    tip = tr("Resume download");  break;
        case S::Failed:        icon = ":/icons/retry-arrow.svg";    tip = tr("Retry download");   break;
        case S::Downloaded:    break;  // handled above
        }
        btn->setIcon(QIcon(icon));
        btn->setToolTip(tip);
    }
}
```
NOTE on the icon resource names: verify each exists with `grep -rn "download-arrow.svg\|pause-circle.svg\|play-circle.svg\|retry-arrow.svg" src/` (they're all referenced today by `actionIconForState`). If `retry-arrow.svg` is absent, reuse `download-arrow.svg` and note it.

- [ ] **Step 4: Implement `refreshAllEpisodeRows` + route populate/timer through it.**

```cpp
void StreamDetailView::refreshAllEpisodeRows()
{
    if (!m_episodeTable) return;
    const int season = currentSeason();
    if (season <= 0) return;
    for (int row = 0; row < m_episodeTable->rowCount(); ++row) {
        auto* numItem = m_episodeTable->item(row, kColEpisode);
        if (!numItem) continue;
        const int episode = numItem->data(Qt::UserRole).toInt();
        if (episode > 0) refreshEpisodeRow(row, season, episode);
    }
}
```
At the END of `populateEpisodeTable`, after the rows are built, replace the trailing cluster of paint calls (`refreshEpisodeMarkers();` / `repaintActionIconForRow` loop / `refreshSubstrateStatesForActiveSeason();`) with a single `refreshAllEpisodeRows();`. (Leave `refreshEpisodeBulkProgress()` for now — Task 5 removes its cohort body; but repoint the 1Hz progress timer's lambda to call `refreshAllEpisodeRows()` instead of `refreshSubstrateStatesForActiveSeason()`.) Grep first: `grep -n "refreshEpisodeMarkers\|repaintActionIconForRow\|refreshSubstrateStatesForActiveSeason\|refreshEpisodeBulkProgress" src/ui/pages/stream/StreamDetailView.cpp` to find every call site.

- [ ] **Step 5: Build + verify exe mtime.**

PowerShell: `Get-Process Tankoban -ErrorAction SilentlyContinue | Stop-Process -Force; & '.\build_check.bat'; (Get-Item out\Tankoban.exe).LastWriteTime`
Expected: `BUILD OK` + mtime advanced past now-minus-build.

- [ ] **Step 6: Smoke.** Relaunch (`Start-Process out\Tankoban.exe --dev-control` with Qt bin on PATH + telemetry env). Open Daredevil S2 (files on disk) → every episode shows the **Play** text button, NO "Queued", NO retry icon. Leave the view + return → still Play. Confirm via `out\tankoctl.exe get-downloads` the entries match what's painted.

- [ ] **Step 7: RTC (Path A).** `READY TO COMMIT — [Agent 4, THEATRE_EPISODE_STATE_MODEL P1.T2]: disk-first gatherer (episodeDisplayState) + unified per-row painter (refreshEpisodeRow/refreshAllEpisodeRows) feeding status text + action control incl. Play text button; populate + 1Hz timer routed through it. Downloaded episodes read Downloaded + persist on re-entry.`

### Task 3: Action-control click router

**Files:** Modify `src/ui/pages/stream/StreamDetailView.cpp`

- [ ] **Step 1: Read the current click handler.** `grep -n "onActionIconClicked" src/ui/pages/stream/StreamDetailView.{h,cpp}` and read its body — it currently routes by cohort/substrate state. We rewrite it to route by `episodeDisplayState`.

- [ ] **Step 2: Rewrite `onActionIconClicked` to route by derived state.**

```cpp
void StreamDetailView::onActionIconClicked(int episode, const QPoint& /*globalPos*/)
{
    const int season = currentSeason();
    if (season <= 0 || m_currentImdb.isEmpty()) return;
    using S = tankostream::stream::EpisodeDisplayState;
    switch (episodeDisplayState(season, episode)) {
    case S::Downloaded:
        // Play from disk — same path as a row click.
        emit singleEpisodePlayRequested(season, episode);  // see Step 3 for the emit target
        break;
    case S::NotDownloaded:
        emit singleEpisodeDownloadRequested(season, episode);  // existing signal → StreamPage::startAutoDownload
        break;
    case S::Downloading: {
        const QString hash = infoHashForEpisode(season, episode);  // helper below
        if (!hash.isEmpty() && m_torrentClient) m_torrentClient->pauseTorrent(hash);
        break;
    }
    case S::Paused: {
        const QString hash = infoHashForEpisode(season, episode);
        if (!hash.isEmpty() && m_torrentClient) m_torrentClient->resumeTorrent(hash);
        break;
    }
    case S::Failed:
        emit singleEpisodeDownloadRequested(season, episode);  // retry = re-dispatch
        break;
    }
    refreshEpisodeRow(rowForEpisode(season, episode), season, episode);
}
```

- [ ] **Step 3: Resolve the play emit + the two helpers.**
  - **Play:** confirm how a downloaded episode plays today — `grep -n "onEpisodeActivated\|playLocalFileFromStreamRequested\|singleEpisodePlayRequested" src/ui/pages/stream/StreamDetailView.{h,cpp}`. `onEpisodeActivated(row,col)` already does the disk-check play. Simplest: for the Downloaded case, call `onEpisodeActivated(rowForEpisode(season,episode), 0)` directly instead of inventing a new signal. Use that; delete the `singleEpisodePlayRequested` line.
  - **`infoHashForEpisode(season,episode)`** and **`rowForEpisode(season,episode)`**: check if equivalents exist (`grep -n "findInfoHashForEpisode\|infoHashForEpisode\|rowForEpisode" src/ui/pages/stream/StreamDetailView.cpp`). `findInfoHashForEpisode` already exists (used by `onDownloadSeasonClicked`) — reuse it. For row lookup, add a tiny helper that scans `kColEpisode` items for the matching episode number, or inline the loop.

- [ ] **Step 4: Build + verify mtime (as Task 2 Step 5).**

- [ ] **Step 5: Smoke.** Start a NOT-downloaded episode → click download icon → row goes `Downloading N%` + pause icon → click pause → `Paused N%` + resume icon → click resume → downloading again. When it completes → Play button → click → plays. (Pause acts on the owning transfer in Phase 1 — per-file is Phase 2; note if siblings pause.)

- [ ] **Step 6: RTC (Path A).** `READY TO COMMIT — [Agent 4, THEATRE_EPISODE_STATE_MODEL P1.T3]: action-control click router by derived state (download/pause/resume/play/retry) via TorrentClient pause/resume + existing download+play paths.`

### Task 4: Context menu (cancel / delete-from-disk / show-in-folder)

**Files:** Modify `src/ui/pages/stream/StreamDetailView.cpp`

- [ ] **Step 1: Read the current handler.** `grep -n "onEpisodeContextMenu" src/ui/pages/stream/StreamDetailView.{h,cpp}` and read its body (currently cohort-driven). It's already connected to `m_episodeTable`'s `customContextMenuRequested`. We rewrite the body.

- [ ] **Step 2: Rewrite `onEpisodeContextMenu` disk-first.**

```cpp
void StreamDetailView::onEpisodeContextMenu(const QPoint& pos)
{
    if (!m_episodeTable) return;
    const int row = m_episodeTable->rowAt(pos.y());
    if (row < 0) return;
    auto* numItem = m_episodeTable->item(row, kColEpisode);
    if (!numItem) return;
    const int episode = numItem->data(Qt::UserRole).toInt();
    const int season  = numItem->data(Qt::UserRole + 1).toInt();
    if (episode <= 0 || season <= 0 || m_currentImdb.isEmpty()) return;

    using S = tankostream::stream::EpisodeDisplayState;
    const S state = episodeDisplayState(season, episode);

    QMenu menu(this);
    QAction* cancelAct = nullptr;
    QAction* deleteAct = nullptr;
    QAction* showAct   = nullptr;

    if (state == S::Downloading || state == S::Paused || state == S::Failed)
        cancelAct = menu.addAction(tr("Cancel download"));
    if (state == S::Downloaded) {
        deleteAct = menu.addAction(tr("Delete from disk"));
        showAct   = menu.addAction(tr("Show in folder"));
    }
    if (menu.isEmpty()) return;

    QAction* chosen = menu.exec(m_episodeTable->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == cancelAct) {
        const QString hash = findInfoHashForEpisode(season, episode);
        if (!hash.isEmpty() && m_torrentClient)
            m_torrentClient->deleteTorrent(hash, /*deleteFiles=*/false);
    } else if (chosen == deleteAct) {
        const auto p = m_downloadIndex
            ? m_downloadIndex->filePathFor(m_currentImdb, season, episode)
            : std::nullopt;
        if (!p.has_value()) return;
        const auto reply = QMessageBox::warning(
            this, tr("Delete from disk"),
            tr("Permanently delete this episode's file from disk?\n\n%1").arg(*p),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes) return;
        QFile::remove(*p);
        if (m_downloadIndex)
            m_downloadIndex->evictByPath(StreamDownloadIndex::computeCanonicalKey(*p));
    } else if (chosen == showAct) {
        const auto p = m_downloadIndex
            ? m_downloadIndex->filePathFor(m_currentImdb, season, episode)
            : std::nullopt;
        if (p.has_value())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(*p).absolutePath()));
    }
    refreshEpisodeRow(row, season, episode);
}
```
Confirm includes: `QMessageBox` (`grep -n "#include <QMessageBox>" src/ui/pages/stream/StreamDetailView.cpp`; add if missing), `QDesktopServices`/`QUrl` (QDesktopServices already included per line 17; add `<QUrl>` if absent). Confirm `evictByPath` + `computeCanonicalKey` signatures with `grep -n "evictByPath\|computeCanonicalKey" src/core/stream/StreamDownloadIndex.h`.

- [ ] **Step 3: Build + verify mtime.**

- [ ] **Step 4: Smoke.** Right-click a downloaded episode → menu shows Delete from disk + Show in folder → Delete → confirm dialog → Yes → file gone (verify on disk), row returns to download icon. Show in folder → opens the directory. Right-click a downloading episode → Cancel download → transfer stops, row returns to download icon. Right-click a not-downloaded episode → no menu (or empty).

- [ ] **Step 5: RTC (Path A).** `READY TO COMMIT — [Agent 4, THEATRE_EPISODE_STATE_MODEL P1.T4]: disk-first episode context menu — Cancel download / Delete from disk (confirm dialog, QFile::remove + index evict) / Show in folder.`

### Task 5: Deletion pass — remove the legacy cohort state system + migrate the season button

**Files:** Modify `src/ui/pages/stream/StreamDetailView.cpp`, `src/ui/pages/stream/StreamDetailView.h`

- [ ] **Step 1: Grep every caller of the legacy symbols before deleting.**

```
grep -n "resolveRowState\|actionIconForState\|actionIconForSubstrateState\|RowState\|RowStateInput\|repaintActionIconForRow\|renderEpisodeStateChip\|refreshSubstrateStatesForActiveSeason\|substrateEpisodeEntry" src/ui/pages/stream/StreamDetailView.cpp
```
Record every hit. Each must be removed or rerouted to `refreshEpisodeRow`/`episodeDisplayState`. Expected dead-after-Task-2/3/4: `resolveRowState`, `actionIconForState`, `actionIconForSubstrateState`, `RowState`, `RowStateInput`, `repaintActionIconForRow`, `renderEpisodeStateChip`, `refreshSubstrateStatesForActiveSeason`, `substrateEpisodeEntry`.

- [ ] **Step 2: Delete the dead functions/types** (the file-scope `namespace { ... }` cluster: `RowState`, `RowStateInput`, `resolveRowState`, `ActionIconSpec`-only-if-unused, `actionIconForState`, `actionIconForSubstrateState`, `substrateEpisodeEntry` if unused elsewhere) and the member functions `repaintActionIconForRow`, `renderEpisodeStateChip`, `refreshSubstrateStatesForActiveSeason` (+ their decls in the .h). Keep `findGroupIdForCohort`/`findInfoHashForEpisode`/`streamBulkSnapshotForImdbSeason` (still used by the gatherer + season button + cancel). Keep `isTerminalCohortState` only if the season button still needs it after Step 3.

- [ ] **Step 3: Migrate the season header button to disk-first.**

`onDownloadSeasonClicked` + `refreshSeasonHeaderButton` currently read cohort snapshots. Read them (`grep -n "onDownloadSeasonClicked\|refreshSeasonHeaderButton" src/ui/pages/stream/StreamDetailView.cpp`). Re-derive the season-level state from the episode rows: count episodes whose `episodeDisplayState` is Downloaded vs Downloading/Paused vs NotDownloaded. Button label/action:
  - all/most episodes Downloaded → button hidden or "Re-download" (keep simple: hide when every episode is Downloaded).
  - any Downloading → "Pause Season" (pauses all active episode transfers via `findInfoHashForEpisode` + `pauseTorrent`).
  - any Paused (none downloading) → "Continue Season" (resume all).
  - else → "Download Season" (emit `seasonDownloadRequested(season)` — unchanged).
Add a small `seasonAggregateState()` helper that loops episodes via `episodeDisplayState`. Replace the cohort-snapshot reads in both functions with it. If this balloons beyond ~1 task, STOP and post a chat.md note proposing it as P1.T6 split rather than half-doing it (spec §6 risk).

- [ ] **Step 4: Clean-from-scratch build + verify mtime.**

PowerShell: `Get-Process Tankoban | Stop-Process -Force; & '.\build_check.bat'; (Get-Item out\Tankoban.exe).LastWriteTime`. Expected `BUILD OK`, zero references to deleted symbols, mtime advanced.

- [ ] **Step 5: Full smoke matrix.** Re-run Task 2/3/4 smokes + the season button: open a fully-downloaded season → season button hidden/Re-download, every row Play. Start a fresh season download → "Pause Season" appears → pause → "Continue Season" → resume. No "Queued", no phantom retry icon anywhere.

- [ ] **Step 6: RTC (Path A).** `READY TO COMMIT — [Agent 4, THEATRE_EPISODE_STATE_MODEL P1.T5]: delete legacy cohort RowState system (resolveRowState/actionIconForState/repaintActionIconForRow/renderEpisodeStateChip/refreshSubstrateStatesForActiveSeason) + migrate season header button to disk-first seasonAggregateState; clean-from-scratch BUILD OK, one state system remains.`

---

## Self-Review (against the spec)

**Spec coverage:**
- §2.1 one source of truth = disk → Task 1 (pure rule) + Task 2 (gatherer checks disk first).
- §2.2 one derivation feeds status + action → Task 2 (`refreshEpisodeRow` sets both).
- §2.3 visual language (download/pause/resume/Play-text/retry) → Task 2 (painter) + Task 3 (clicks).
- §2.4 context menu (cancel/delete/show-folder) → Task 4.
- §2.5 downloaded persists across re-entry → Task 2 (re-derive from disk each populate; smoke Step 6).
- §3 state table (5 states, priority) → Task 1 enum + derivation; Task 2 maps to text+control.
- §5 delete-from-disk destructive + confirm → Task 4 (QMessageBox + QFile::remove + evict).
- §6 remove cohort RowState; migrate season button → Task 5.
- §9 Phase 2 (source-tiering, per-file pause) → explicitly NOT in any task (correct; deferred).

**Placeholder scan:** Task 1 ships full code + tests. UI tasks cite exact functions + give the new code; the few "grep to confirm signature/anchor" steps are verification-first (the reads were semantically-summarized, so confirm-then-edit is correct), not placeholders.

**Type consistency:** `EpisodeDisplayState` enum (5 values) + `EpisodeStateInputs` defined Task 1, used identically in Tasks 2–5. `episodeDisplayState(season,episode)` / `refreshEpisodeRow(row,season,episode)` / `refreshAllEpisodeRows()` consistent across tasks. `findInfoHashForEpisode` reused (not reinvented). `deleteTorrent(hash, deleteFiles)`, `pauseTorrent(hash)`, `resumeTorrent(hash)`, `evictByPath`, `computeCanonicalKey`, `filePathFor` all reused as confirmed in the read pass.

**Open verification carried into execution (not gaps):** exact line numbers (reads were summarized — every edit is grep-confirmed first); `streamBulkSnapshotForImdbSeason` value type (Step T2.2 greps it); icon resource existence (T2.3 greps); whether `onActionIconClicked`/`onEpisodeContextMenu` signatures match the rewrite (T3.1/T4.1 read first). All guarded with grep-first steps.
