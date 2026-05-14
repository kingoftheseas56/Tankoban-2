# Sources UI Refinement Implementation Plan

> **⚠ PARTIAL SUPERSESSION 2026-05-14** — the COMICS_TANKOYOMI_STREAM_MERGER vision removes TankoyomiPage from Sources scope entirely (Tankoyomi dissolves into Comics mode). Any Tankoyomi-specific tasks in this plan are MOOT; TankorentPage + TankoLibraryPage tasks remain valid and under Agent 4B's ownership. Tankoyomi ownership transferred to Agent 1 the same day. See `CLAUDE.md` dashboard stanza for the merger arc.

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refine TankorentPage, TankoyomiPage, TankoLibraryPage to industry-standard density, Title-Case status strings, header/cell alignment match, theme-token routing, and empty/loading states — per the 2026-05-13 design spec.

**Architecture:** Phased polish of three Qt Widgets pages plus downstream widgets (TransfersView, MangaResultsGrid, BookResultsGrid). No new dependencies, no QML migration, no new colors. Strictly preserve out-of-scope items per spec §2 (Tankorent search-row layout, Tankoyomi columns, tab styles, etc.). Each task is a single atomic change per `feedback_one_fix_per_rebuild.md` — one edit, one `build_check.bat`, one chat.md `READY TO COMMIT` line.

**Tech Stack:** C++20, Qt 6.10, MSVC2022 Release. Build verifier: `build_check.bat`. Run verifier: `build_and_run.bat`. UI smoke: pywinauto-mcp (UIA) + windows-mcp (visual) + `out/tankoctl.exe` (dev-bridge). Rule 17 cleanup: `scripts/stop-tankoban.ps1` after any agent-driven smoke. Rule 19 MCP LOCK in chat.md before any desktop drive.

**Spec source:** `docs/superpowers/specs/2026-05-13-sources-ui-refinement-design.md`

---

## File Structure

### Create
- `resources/icons/magnet.svg` — 16×16 horseshoe magnet, single path, `#cccccc` stroke 1.6, grayscale family
- `resources/icons/kebab-menu.svg` — 16×16 three vertical filled dots, `#cccccc` fill, grayscale family

### Modify
- `resources/resources.qrc` — register `magnet.svg` + `kebab-menu.svg` under `<qresource prefix="/">`
- `src/ui/pages/TankorentPage.h` — page-level objectName declaration via `setObjectName` in ctor; new TitleCellDelegate class declaration; new `categoryDisplayName(sourceKey, rawId)` static helper; new `fgMutedColor()` shared helper
- `src/ui/pages/TankorentPage.cpp` — page margins + spacing + density lift; drop Files column (col rebase); Category friendly-names mapping; bulk-group "videos" → "Videos"; trust signal (drop row tints + bold seeder); title cell delegate; Link column SVG icons; header/cell alignment match; hover row state; empty/loading/zero-results states stacked widget
- `src/ui/pages/TankoyomiPage.h` — `chapterStatusText(QString)` helper declaration
- `src/ui/pages/TankoyomiPage.cpp` — page margins + spacing + density lift; loading bar blue → theme accent; chapterStatusText helper + apply at transfers render; header/cell alignment match; More button SVG icon; hover row state; empty/loading state density polish
- `src/ui/pages/TankoLibraryPage.h` — Filters popover member declarations (`QWidget* m_filtersPopover`, helper methods); dot indicator label member
- `src/ui/pages/TankoLibraryPage.cpp` — page spacing tuning; Filters popover widget + button + dot indicator + signal wiring; search row consolidation (remove inline checkboxes/combos); hardcoded gold `#c7a76b` → `__ACCENT__` at four sites; empty/loading/zero-results states extended to `m_resultsInnerStack`; hover row state
- `src/ui/pages/tankolibrary/TransfersView.h` — Title-Case helper if needed (decl)
- `src/ui/pages/tankolibrary/TransfersView.cpp` — alignment match + Title-Case status strings + theme-token color routing
- `src/ui/pages/tankoyomi/MangaResultsGrid.cpp` — density polish (tile padding, label font)
- `src/ui/pages/tankolibrary/BookResultsGrid.cpp` — density polish (tile padding, label font)

### Coordinate (out of scope but flagged)
- `src/ui/Theme.cpp` + `src/ui/Theme.h` — Agent 5's domain. We use a per-page-local `fgMutedColor()` defensive fallback (read `QPalette::Text` at ~55% opacity). If/when Agent 5 adds a proper `__FG_MUTED__` token, the fallback rewires trivially.

---

## Build / verify cycle (used by every task)

After each task's code change, run:

```
build_check.bat
```

Expected: `BUILD OK` on the final line. If `BUILD FAILED exit=<n>` appears, read the 30-line `cl.exe` tail and fix before proceeding.

If the change affects resources (`.qrc`, SVG additions), confirm `resources/resources.qrc` lists the file and the Qt resource step ran clean.

For tasks that ship visible UI changes, the implementing agent runs the dev-bridge smoke after build:

```
build_and_run.bat
out\tankoctl.exe ping
out\tankoctl.exe open-page tankorent
out\tankoctl.exe get-state
scripts/stop-tankoban.ps1
```

Visual smoke is Hemanth's gate (per `feedback_hemanth_role_open_and_click.md`); the dev-bridge smoke only confirms launchability + no segfault.

After build green + bridge ping green, append a `READY TO COMMIT — [Agent 4B, SOURCES_UI_REFINEMENT T<N> 2026-MM-DD ~HH:MMpm ...]` line to `agents/chat.md` per Rule 11.

---

# Phase 1 — Foundation (Tasks 1-3)

### Task 1: Author magnet.svg + kebab-menu.svg

**Files:**
- Create: `resources/icons/magnet.svg`
- Create: `resources/icons/kebab-menu.svg`
- Modify: `resources/resources.qrc`

- [ ] **Step 1: Author `resources/icons/magnet.svg`**

Tankoban grayscale family conventions (per existing icons): 16×16 viewBox, `stroke="#cccccc"`, `stroke-width="1.6"`, `stroke-linecap="round"`, `stroke-linejoin="round"`, `fill="none"`.

```xml
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" fill="none" stroke="#cccccc" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">
  <path d="M4 12 L4 7 A4 4 0 0 1 12 7 L12 12"/>
  <path d="M4 12 L6 12"/>
  <path d="M10 12 L12 12"/>
</svg>
```

Two parallel vertical pole-ends connected by a half-arc at top; bottom faces emphasized with short stubs.

- [ ] **Step 2: Author `resources/icons/kebab-menu.svg`**

Three vertical filled dots on the centerline.

```xml
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" fill="#cccccc" stroke="none">
  <circle cx="8" cy="4" r="1.4"/>
  <circle cx="8" cy="8" r="1.4"/>
  <circle cx="8" cy="12" r="1.4"/>
</svg>
```

- [ ] **Step 3: Register both in `resources/resources.qrc`**

Add inside the existing `<qresource prefix="/">` block (alphabetical with existing icons):

```xml
<file>icons/kebab-menu.svg</file>
<file>icons/magnet.svg</file>
```

- [ ] **Step 4: Build verify**

Run: `build_check.bat`
Expected: `BUILD OK` (qrc regenerates `qrc_resources.cpp` cleanly).

- [ ] **Step 5: Chat.md READY TO COMMIT**

Append to `agents/chat.md`:
```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T1 2026-MM-DD ~HH:MMpm — Authored magnet.svg + kebab-menu.svg in Tankoban grayscale family (16x16, #cccccc, stroke 1.6 / fill dots). Registered both in resources.qrc. BUILD OK. ...] | files: resources/icons/magnet.svg (new), resources/icons/kebab-menu.svg (new), resources/resources.qrc, agents/chat.md
```

---

### Task 2: Add page-level setObjectName to all three pages

**Files:**
- Modify: `src/ui/pages/TankorentPage.cpp` (constructor, after `m_nam = new QNetworkAccessManager(this);`)
- Modify: `src/ui/pages/TankoyomiPage.cpp` (constructor, after `m_nam = new QNetworkAccessManager(this);`)
- Modify: `src/ui/pages/TankoLibraryPage.cpp` (constructor, after `m_nam = new QNetworkAccessManager(this);`)

- [ ] **Step 1: Add objectName to TankorentPage**

In `TankorentPage::TankorentPage(...)` ctor, after `m_nam = new QNetworkAccessManager(this);`:

```cpp
setObjectName(QStringLiteral("TankorentPage"));
```

- [ ] **Step 2: Add objectName to TankoyomiPage**

Same pattern:

```cpp
setObjectName(QStringLiteral("TankoyomiPage"));
```

- [ ] **Step 3: Add objectName to TankoLibraryPage**

Same pattern:

```cpp
setObjectName(QStringLiteral("TankoLibraryPage"));
```

- [ ] **Step 4: Build verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 5: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T2 2026-MM-DD ~HH:MMpm — Added page-level setObjectName to TankorentPage / TankoyomiPage / TankoLibraryPage. Required prerequisite for theme system QSS targeting (per memoried gap from Agent 5's 2026-05-05 SOURCES_SIDEBAR explore agent). BUILD OK.] | files: src/ui/pages/TankorentPage.cpp, src/ui/pages/TankoyomiPage.cpp, src/ui/pages/TankoLibraryPage.cpp, agents/chat.md
```

---

### Task 3: Add fgMutedColor() defensive helper

**Files:**
- Modify: `src/ui/pages/TankorentPage.cpp` (anonymous namespace at top)
- Modify: `src/ui/pages/TankoyomiPage.cpp` (anonymous namespace at top)
- Modify: `src/ui/pages/TankoLibraryPage.cpp` (existing anonymous namespace at lines 47-208)

- [ ] **Step 1: Add the helper to TankorentPage.cpp**

Locate the existing anonymous namespace top in `TankorentPage.cpp` (begins around line 68 with `struct CategoryOption`). Add at the top of that namespace:

```cpp
// Defensive fall-through for muted-secondary-text color. If Theme.cpp ever
// adds a __FG_MUTED__ token + palette.fgMuted field, swap this for a
// Theme-token read. Reads QApplication::palette() Text color at ~55%
// opacity, which approximates the muted-secondary color across every
// palette without needing Agent 5 coordination today.
inline QColor fgMutedColor()
{
    QColor c = QApplication::palette().color(QPalette::Text);
    c.setAlpha(140);
    return c;
}
```

Confirm `#include <QApplication>` exists (it does — line 34).

- [ ] **Step 2: Add the same helper to TankoyomiPage.cpp**

Same body. Place at top of file (no existing anonymous namespace; create one wrapping just this helper):

```cpp
namespace {
inline QColor fgMutedColor()
{
    QColor c = QApplication::palette().color(QPalette::Text);
    c.setAlpha(140);
    return c;
}
}
```

Confirm `#include <QApplication>` exists (it does — line 21).

- [ ] **Step 3: Add the same helper to TankoLibraryPage.cpp**

Add inside the existing anonymous namespace (lines 47-208), near the top:

```cpp
inline QColor fgMutedColor()
{
    QColor c = QApplication::palette().color(QPalette::Text);
    c.setAlpha(140);
    return c;
}
```

Add `#include <QApplication>` if missing (search for it — likely transitively included).

- [ ] **Step 4: Build verify**

Run: `build_check.bat`
Expected: `BUILD OK`. (Unused helper at this stage; no warnings expected — it's `inline` so no unused-function diagnostic.)

- [ ] **Step 5: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T3 2026-MM-DD ~HH:MMpm — Added fgMutedColor() defensive helper to TankorentPage / TankoyomiPage / TankoLibraryPage anonymous namespaces. Reads QApplication palette Text color at alpha=140 (~55%) as a fall-through for the muted-secondary-text color while Agent 5's Theme.cpp lacks a __FG_MUTED__ token. Helper consumed in subsequent tasks (T10 bold-seeder dim, T11 title delegate dim segments, etc.). BUILD OK.] | files: src/ui/pages/TankorentPage.cpp, src/ui/pages/TankoyomiPage.cpp, src/ui/pages/TankoLibraryPage.cpp, agents/chat.md
```

---

# Phase 2 — Density lift + page margins (Tasks 4-6)

### Task 4: Tankorent — page margins + spacing + density lift

**Files:**
- Modify: `src/ui/pages/TankorentPage.cpp:481-636` (buildUI + buildSearchControls + buildStatusRow + buildMainTabs)

- [ ] **Step 1: Update outer margins + spacing in `buildUI()`**

`TankorentPage.cpp:483-485`:

BEFORE:
```cpp
auto *root = new QVBoxLayout(this);
root->setContentsMargins(8, 8, 8, 0);
root->setSpacing(6);
```

AFTER:
```cpp
auto *root = new QVBoxLayout(this);
root->setContentsMargins(12, 12, 12, 12);
root->setSpacing(10);
```

- [ ] **Step 2: Update inner row spacings in `buildSearchControls()`**

`TankorentPage.cpp:498-500` (queryRow):

BEFORE: `queryRow->setSpacing(8);`
AFTER:  `queryRow->setSpacing(10);`

`TankorentPage.cpp:528-529` (filter row):

BEFORE: `row->setSpacing(8);`
AFTER:  `row->setSpacing(10);`

- [ ] **Step 3: Bump control heights from 30 → 36px**

Within `buildSearchControls()`, every `setFixedHeight(30)` call:

- `m_queryEdit->setFixedHeight(30)` → `setFixedHeight(36)` (line 504)
- `m_searchBtn->setFixedHeight(30)` → 36 (line 510)
- `m_cancelBtn->setFixedHeight(30)` → 36 (line 517)
- `m_searchTypeCombo->setFixedHeight(30)` → 36 (line 532)
- `m_sourceCombo->setFixedHeight(30)` → 36 (line 541)
- `m_categoryCombo->setFixedHeight(30)` → 36 (line 549)
- `m_filterCombo->setFixedHeight(30)` → 36 (line 558)
- `m_refreshBtn->setFixedHeight(30)` → 36 (line 581)
- `m_sourcesBtn->setFixedHeight(30)` → 36 (line 587)
- `m_addUrlBtn->setFixedHeight(30)` → 36 (line 593)
- `m_moreBtn->setFixedHeight(30)` → 36 (line 599)

- [ ] **Step 4: Bump status row font-size from 11px → 13px in `buildStatusRow()`**

`TankorentPage.cpp:644-654`:

BEFORE:
```cpp
m_searchStatus->setStyleSheet("color: #a1a1aa; font-size: 11px;");
...
m_downloadStatus->setStyleSheet("color: #a1a1aa; font-size: 11px;");
...
m_backendStatus->setStyleSheet("color: #a1a1aa; font-size: 11px;");
```

AFTER (also routes color through palette — leave color literal for now; T19/T26 dispatches the broader theme cleanup):
```cpp
m_searchStatus->setStyleSheet("color: #a1a1aa; font-size: 13px;");
...
m_downloadStatus->setStyleSheet("color: #a1a1aa; font-size: 13px;");
...
m_backendStatus->setStyleSheet("color: #a1a1aa; font-size: 13px;");
```

Also for the results count label `TankorentPage.cpp:665`:

BEFORE: `m_resultsCountLabel->setStyleSheet("color: #a1a1aa; font-size: 11px; padding: 4px 0;");`
AFTER:  `m_resultsCountLabel->setStyleSheet("color: #a1a1aa; font-size: 13px; padding: 4px 0;");`

- [ ] **Step 5: Update table style font-size + row height for results table in `createResultsTable()`**

`TankorentPage.cpp:777-785`:

BEFORE:
```cpp
table->setStyleSheet(QStringLiteral(
    "#SearchResultsTable { border: none; outline: none; font-size: 12px; }"
    "#SearchResultsTable::item { padding: 0 8px; }"
    "#SearchResultsTable::item:selected { background: rgba(192,200,212,36); color: #eeeeee; }"
    "#SearchResultsTable QHeaderView::section {"
    "  background: #1a1a1a; color: #888; border: none;"
    "  border-right: 1px solid #222; border-bottom: 1px solid #222;"
    "  padding: 4px 8px; font-size: 11px; }"
));
```

AFTER (font-size lift + header font-weight 600 — header font-size stays 11px to keep header dense; cell font 12→13):
```cpp
table->setStyleSheet(QStringLiteral(
    "#SearchResultsTable { border: none; outline: none; font-size: 13px; }"
    "#SearchResultsTable::item { padding: 0 8px; }"
    "#SearchResultsTable::item:selected { background: rgba(192,200,212,36); color: #eeeeee; }"
    "#SearchResultsTable QHeaderView::section {"
    "  background: #1a1a1a; color: #888; border: none;"
    "  border-right: 1px solid #222; border-bottom: 1px solid #222;"
    "  padding: 6px 8px; font-size: 11px; font-weight: 600; }"
));
```

Set table row height. Add line after `table->verticalHeader()->setVisible(false);` (around line 695):

```cpp
table->verticalHeader()->setDefaultSectionSize(32);
```

- [ ] **Step 6: Same updates for transfers table in `createTransfersTable()`**

`TankorentPage.cpp:799` — change row default section size:

BEFORE: `table->verticalHeader()->setDefaultSectionSize(26);`
AFTER:  `table->verticalHeader()->setDefaultSectionSize(32);`

`TankorentPage.cpp:838-846`:

BEFORE:
```cpp
table->setStyleSheet(QStringLiteral(
    "#TransfersTable { border: none; outline: none; font-size: 12px; }"
    "#TransfersTable::item { padding: 0 8px; }"
    "#TransfersTable::item:selected { background: rgba(192,200,212,36); color: #eeeeee; }"
    "#TransfersTable QHeaderView::section {"
    "  background: #1a1a1a; color: #888; border: none;"
    "  border-right: 1px solid #222; border-bottom: 1px solid #222;"
    "  padding: 4px 8px; font-size: 11px; }"
));
```

AFTER:
```cpp
table->setStyleSheet(QStringLiteral(
    "#TransfersTable { border: none; outline: none; font-size: 13px; }"
    "#TransfersTable::item { padding: 0 8px; }"
    "#TransfersTable::item:selected { background: rgba(192,200,212,36); color: #eeeeee; }"
    "#TransfersTable QHeaderView::section {"
    "  background: #1a1a1a; color: #888; border: none;"
    "  border-right: 1px solid #222; border-bottom: 1px solid #222;"
    "  padding: 6px 8px; font-size: 11px; font-weight: 600; }"
));
```

- [ ] **Step 7: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

Run: `build_and_run.bat` + `out/tankoctl.exe open-page tankorent`
Expected: ping returns schema v1; open-page succeeds; page renders without crash.
Cleanup: `scripts/stop-tankoban.ps1`.

- [ ] **Step 8: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T4 2026-MM-DD ~HH:MMpm — Tankorent density lift to industry standard: page margins 8/8/8/0 -> 12/12/12/12; layout spacing 6 -> 10; control heights 30 -> 36px across 11 widgets; status row font 11 -> 13px; results + transfers table cell font 12 -> 13px; header font-weight 400 -> 600; table row height 26 -> 32px. No color changes this task (theme cleanup in subsequent tasks). BUILD OK + structural smoke green.] | files: src/ui/pages/TankorentPage.cpp, agents/chat.md
```

---

### Task 5: Tankoyomi — page margins + spacing + density lift

**Files:**
- Modify: `src/ui/pages/TankoyomiPage.cpp` (buildUI + buildSearchControls + buildStatusRow + buildMainTabs + createResultsTable + createTransfersTable + empty/loading/zero-results state pages)

- [ ] **Step 1: Update outer margins + spacing in `buildUI()`**

`TankoyomiPage.cpp:230-232`:

BEFORE:
```cpp
auto *root = new QVBoxLayout(this);
root->setContentsMargins(8, 8, 8, 0);
root->setSpacing(6);
```

AFTER:
```cpp
auto *root = new QVBoxLayout(this);
root->setContentsMargins(12, 12, 12, 12);
root->setSpacing(10);
```

- [ ] **Step 2: Update search-row spacing**

`TankoyomiPage.cpp:242-243`:

BEFORE: `row->setSpacing(8);`
AFTER:  `row->setSpacing(10);`

`TankoyomiPage.cpp:365-366` (status row):

BEFORE: `row->setSpacing(8);`
AFTER:  `row->setSpacing(10);`

- [ ] **Step 3: Bump control heights 30 → 36px**

In `buildSearchControls()`:
- `m_queryEdit->setFixedHeight(30)` → 36 (line 247)
- `m_sourceCombo->setFixedHeight(30)` → 36 (line 252)
- `m_searchBtn->setFixedHeight(30)` → 36 (line 258)
- `m_cancelBtn->setFixedHeight(30)` → 36 (line 264)
- `m_refreshBtn->setFixedHeight(30)` → 36 (line 271)
- `m_sortCombo->setFixedHeight(30)` → 36 (line 283)
- `m_viewToggleBtn->setFixedHeight(30)` → 36 (line 306)
- `m_pauseBtn->setFixedHeight(30)` → 36 (line 321)
- `m_moreBtn->setFixedSize(30, 30)` → `setFixedSize(36, 36)` (line 335)

- [ ] **Step 4: Bump status row font-size 11 → 13**

`TankoyomiPage.cpp:368-373`:

BEFORE:
```cpp
m_searchStatus->setStyleSheet("color: #a1a1aa; font-size: 11px;");
...
m_downloadStatus->setStyleSheet("color: #a1a1aa; font-size: 11px;");
```

AFTER:
```cpp
m_searchStatus->setStyleSheet("color: #a1a1aa; font-size: 13px;");
...
m_downloadStatus->setStyleSheet("color: #a1a1aa; font-size: 13px;");
```

- [ ] **Step 5: Update empty + loading page label sizes**

`TankoyomiPage.cpp:404-406` (empty label):

BEFORE: `m_emptyLabel->setStyleSheet("#TankoyomiEmptyState { color: #a1a1aa; font-size: 14px; }");`
AFTER:  `m_emptyLabel->setStyleSheet("#TankoyomiEmptyState { color: #a1a1aa; font-size: 15px; }");`

`TankoyomiPage.cpp:413-414` (retry button) + line 425 (clear button):

BEFORE: `setFixedHeight(28)` (both)
AFTER:  `setFixedHeight(32)` (both)

`TankoyomiPage.cpp:449-450` (loading label):

BEFORE: `m_loadingLabel->setStyleSheet("color: #cbd5e1; font-size: 14px;");`
AFTER:  `m_loadingLabel->setStyleSheet("color: #cbd5e1; font-size: 15px;");`

- [ ] **Step 6: Bump table cell + header sizes (results + transfers)**

`TankoyomiPage.cpp:487` (results table min height):

BEFORE: `table->setMinimumHeight(280);`
After (no change to min height; just ensure row height bump):

Add after `table->verticalHeader()->setVisible(false);` (line 491):

```cpp
table->verticalHeader()->setDefaultSectionSize(32);
```

`TankoyomiPage.cpp:520-528` (results table stylesheet):

BEFORE:
```cpp
table->setStyleSheet(QStringLiteral(
    "#MangaResultsTable { border: none; outline: none; font-size: 12px; }"
    "#MangaResultsTable::item { padding: 0 8px; }"
    "#MangaResultsTable::item:selected { background: rgba(192,200,212,36); color: #eeeeee; }"
    "#MangaResultsTable QHeaderView::section {"
    "  background: #1a1a1a; color: #888; border: none;"
    "  border-right: 1px solid #222; border-bottom: 1px solid #222;"
    "  padding: 4px 8px; font-size: 11px; }"
));
```

AFTER:
```cpp
table->setStyleSheet(QStringLiteral(
    "#MangaResultsTable { border: none; outline: none; font-size: 13px; }"
    "#MangaResultsTable::item { padding: 0 8px; }"
    "#MangaResultsTable::item:selected { background: rgba(192,200,212,36); color: #eeeeee; }"
    "#MangaResultsTable QHeaderView::section {"
    "  background: #1a1a1a; color: #888; border: none;"
    "  border-right: 1px solid #222; border-bottom: 1px solid #222;"
    "  padding: 6px 8px; font-size: 11px; font-weight: 600; }"
));
```

`TankoyomiPage.cpp:537` (transfers min height):
No change.

`TankoyomiPage.cpp:541` (transfers row default section size):

BEFORE: `table->verticalHeader()->setDefaultSectionSize(26);`
AFTER:  `table->verticalHeader()->setDefaultSectionSize(32);`

`TankoyomiPage.cpp:570-578` (transfers table stylesheet):

BEFORE:
```cpp
table->setStyleSheet(QStringLiteral(
    "#MangaTransfersTable { border: none; outline: none; font-size: 12px; }"
    "#MangaTransfersTable::item { padding: 0 8px; }"
    "#MangaTransfersTable::item:selected { background: rgba(192,200,212,36); color: #eeeeee; }"
    "#MangaTransfersTable QHeaderView::section {"
    "  background: #1a1a1a; color: #888; border: none;"
    "  border-right: 1px solid #222; border-bottom: 1px solid #222;"
    "  padding: 4px 8px; font-size: 11px; }"
));
```

AFTER:
```cpp
table->setStyleSheet(QStringLiteral(
    "#MangaTransfersTable { border: none; outline: none; font-size: 13px; }"
    "#MangaTransfersTable::item { padding: 0 8px; }"
    "#MangaTransfersTable::item:selected { background: rgba(192,200,212,36); color: #eeeeee; }"
    "#MangaTransfersTable QHeaderView::section {"
    "  background: #1a1a1a; color: #888; border: none;"
    "  border-right: 1px solid #222; border-bottom: 1px solid #222;"
    "  padding: 6px 8px; font-size: 11px; font-weight: 600; }"
));
```

- [ ] **Step 7: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

Run: `build_and_run.bat` + `out/tankoctl.exe open-page tankoyomi`
Expected: page renders without crash.
Cleanup: `scripts/stop-tankoban.ps1`.

- [ ] **Step 8: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T5 2026-MM-DD ~HH:MMpm — Tankoyomi density lift: page margins 8/8/8/0 -> 12/12/12/12; spacing 6 -> 10; control heights 30 -> 36px across 9 widgets; empty/loading label font 14 -> 15px; Retry/Clear buttons 28 -> 32px; table cell font 12 -> 13px; header weight 400 -> 600; row height 26 -> 32px. BUILD OK + structural smoke green.] | files: src/ui/pages/TankoyomiPage.cpp, agents/chat.md
```

---

### Task 6: TankoLibrary — spacing + density lift

**Files:**
- Modify: `src/ui/pages/TankoLibraryPage.cpp` (buildResultsPage + buildDetailPage)

- [ ] **Step 1: Update outer + inner spacing**

`TankoLibraryPage.cpp:495-497` (already 12/12/12/12 — only spacing bump):

BEFORE:
```cpp
outer->setContentsMargins(12, 12, 12, 12);
outer->setSpacing(8);
```

AFTER:
```cpp
outer->setContentsMargins(12, 12, 12, 12);
outer->setSpacing(10);
```

Same for detail page `TankoLibraryPage.cpp:695-697`:

BEFORE: `outer->setSpacing(12);` (already calmer; no change required)
AFTER: no change.

- [ ] **Step 2: Bump search-row control heights**

In `buildResultsPage()`:
- `m_queryEdit` — currently no fixed-height; add `m_queryEdit->setFixedHeight(36);` after line 510 (post-placeholder).
- `m_searchBtn` — no fixed height; add `m_searchBtn->setFixedHeight(36);` after line 516.
- `m_cancelBtn` — no fixed height; add `m_cancelBtn->setFixedHeight(36);` after line 522.
- Format chk row (m_epubChk, m_pdfChk, m_mobiChk via the lambda `buildFormatChk`) — these are QCheckBox, height auto-sized; no change in T6 (they move to popover in T22).
- `m_englishOnlyCheckbox` — same; move in T22.
- `m_sortCombo` — line 584; add `m_sortCombo->setFixedHeight(36);`.
- `m_audioFormatCombo` — line 602; add `m_audioFormatCombo->setFixedHeight(36);`.

- [ ] **Step 3: Bump status label font + spacing**

`TankoLibraryPage.cpp:617-620`:

BEFORE:
```cpp
m_statusLbl = new QLabel(QStringLiteral("Ready. Type a query and hit Enter."), m_resultsPage);
m_statusLbl->setStyleSheet(QStringLiteral(
    "color: #888; font-size: 12px; background: transparent; border: none;"));
```

AFTER:
```cpp
m_statusLbl = new QLabel(QStringLiteral("Ready. Type a query and hit Enter."), m_resultsPage);
m_statusLbl->setStyleSheet(QStringLiteral(
    "color: #888; font-size: 13px; background: transparent; border: none;"));
```

`TankoLibraryPage.cpp:660-661`:

BEFORE:
```cpp
m_transfersCounter->setStyleSheet(QStringLiteral(
    "color: #888; font-size: 12px; background: transparent; border: none;"));
```

AFTER:
```cpp
m_transfersCounter->setStyleSheet(QStringLiteral(
    "color: #888; font-size: 13px; background: transparent; border: none;"));
```

- [ ] **Step 4: Bump media-tab + Search Results/Transfers pill font-size**

`TankoLibraryPage.cpp:360-367` (kTabActiveCss + kTabInactiveCss for media tab):

BEFORE:
```cpp
const QString kTabActiveCss = QStringLiteral(
    "QPushButton { color: #e6e6e6; background: #2a2a2a; border: 1px solid #444;"
    " padding: 6px 18px; font-size: 13px; font-weight: 500; border-radius: 4px; }"
    "QPushButton:hover { background: #333; }");
const QString kTabInactiveCss = QStringLiteral(
    "QPushButton { color: #888; background: transparent; border: 1px solid transparent;"
    " padding: 6px 18px; font-size: 13px; border-radius: 4px; }"
    "QPushButton:hover { color: #ccc; }");
```

AFTER (bump padding for taller pill):
```cpp
const QString kTabActiveCss = QStringLiteral(
    "QPushButton { color: #e6e6e6; background: #2a2a2a; border: 1px solid #444;"
    " padding: 8px 18px; font-size: 13px; font-weight: 600; border-radius: 4px; }"
    "QPushButton:hover { background: #333; }");
const QString kTabInactiveCss = QStringLiteral(
    "QPushButton { color: #888; background: transparent; border: 1px solid transparent;"
    " padding: 8px 18px; font-size: 13px; border-radius: 4px; }"
    "QPushButton:hover { color: #ccc; }");
```

Same for the Search Results/Transfers pill row at `TankoLibraryPage.cpp:630-637`:

BEFORE: `padding: 4px 14px;` AFTER: `padding: 8px 16px;` (both active + inactive)
BEFORE: `font-size: 13px;` (already) — no change.

- [ ] **Step 5: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

Run: `build_and_run.bat` + `out/tankoctl.exe open-page tankolibrary`
Expected: page renders without crash.
Cleanup: `scripts/stop-tankoban.ps1`.

- [ ] **Step 6: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T6 2026-MM-DD ~HH:MMpm — TankoLibrary density lift: outer spacing 8 -> 10; query/search/cancel/sort/audio combo heights set to 36px (previously unset, used Qt default ~24); status + transfers counter font 12 -> 13px; media tab + inner pill padding bumped (6px 18px -> 8px 18px) for calmer pill height. Detail-page outer spacing 12 already calm; no change. BUILD OK + structural smoke green.] | files: src/ui/pages/TankoLibraryPage.cpp, agents/chat.md
```

---

# Phase 3 — Tankorent specifics (Tasks 7-15)

### Task 7: Drop Files column + sort col rebase

**Files:**
- Modify: `src/ui/pages/TankorentPage.h:163-166` (m_resultsSortCol default)
- Modify: `src/ui/pages/TankorentPage.cpp` (constructor sort restore + headers + column resize + sort comparator + visibility menu + render loop)

- [ ] **Step 1: Update default sort col in header**

`TankorentPage.h:163-166`:

BEFORE:
```cpp
// A1/C: results table sort state. Default = Seeders desc — col index is
// 4 in the post-Track-C layout (0 Title, 1 Category, 2 Size, 3 Files,
// 4 Seeders, 5 Leechers, 6 Link).
int           m_resultsSortCol   = 4;
```

AFTER:
```cpp
// A1/C: results table sort state. Default = Seeders desc — col index is
// 3 in the post-T7 layout (0 Title, 1 Category, 2 Size, 3 Seeders,
// 4 Leechers, 5 Link). Files col removed in T7.
int           m_resultsSortCol   = 3;
```

- [ ] **Step 2: Update constructor's sort-validation block**

`TankorentPage.cpp:386-397`:

BEFORE:
```cpp
const bool  validCol   = (savedCol == 0 || savedCol == 1 || savedCol == 2 ||
                          savedCol == 4 || savedCol == 5);
```

AFTER:
```cpp
// Valid sortable columns after T7 Files drop: 0 Title, 1 Category, 2 Size,
// 3 Seeders, 4 Leechers. (5 Link is non-sortable.) Old saved index 4
// (pre-T7 Seeders) migrates to 3; old 5 (pre-T7 Leechers) → 4. Old 3 (Files)
// → default 3 (Seeders).
int migratedCol = savedCol;
if (savedCol == 3) migratedCol = 3;   // pre-T7 Files → post Seeders
else if (savedCol == 4) migratedCol = 3;   // pre-T7 Seeders → 3
else if (savedCol == 5) migratedCol = 4;   // pre-T7 Leechers → 4
else if (savedCol == 6) migratedCol = 5;   // pre-T7 Link → 5 (non-sortable; default fallback applies)
const bool  validCol = (migratedCol == 0 || migratedCol == 1 || migratedCol == 2 ||
                        migratedCol == 3 || migratedCol == 4);
if (validCol) m_resultsSortCol = migratedCol;
```

(Remove the old `if (validCol) m_resultsSortCol = savedCol;` line below this block.)

- [ ] **Step 3: Update headers + column-count in `createResultsTable()`**

`TankorentPage.cpp:688`:

BEFORE: `auto *table = new QTableWidget(0, 7);`
AFTER:  `auto *table = new QTableWidget(0, 6);`

`TankorentPage.cpp:702`:

BEFORE: `QStringList headers = { "Title", "Category", "Size", "Files", "Seeders", "Leechers", "Link" };`
AFTER:  `QStringList headers = { "Title", "Category", "Size", "Seeders", "Leechers", "Link" };`

`TankorentPage.cpp:707-716` (column resize):

BEFORE:
```cpp
hdr->setSectionResizeMode(0, QHeaderView::Stretch);
for (int i = 1; i < 7; ++i)
    hdr->setSectionResizeMode(i, QHeaderView::Interactive);

hdr->resizeSection(1, 130);   // Category
hdr->resizeSection(2, 110);   // Size
hdr->resizeSection(3, 90);    // Files
hdr->resizeSection(4, 90);    // Seeders
hdr->resizeSection(5, 90);    // Leechers
hdr->resizeSection(6, 80);    // Link (two icon buttons)
```

AFTER:
```cpp
hdr->setSectionResizeMode(0, QHeaderView::Stretch);
for (int i = 1; i < 6; ++i)
    hdr->setSectionResizeMode(i, QHeaderView::Interactive);

hdr->resizeSection(1, 140);   // Category
hdr->resizeSection(2, 110);   // Size
hdr->resizeSection(3, 90);    // Seeders
hdr->resizeSection(4, 90);    // Leechers
hdr->resizeSection(5, 80);    // Link (two icon buttons)
```

- [ ] **Step 4: Update visibility menu column count (the persisted-CSV path implicitly works once columnCount is 6, but the saved-state validator needs to ignore stale indices ≥6)**

`TankorentPage.cpp:732-739`:

BEFORE:
```cpp
for (const QString& s : saved) {
    bool ok = false; const int c = s.toInt(&ok);
    if (ok && c >= 0 && c < table->columnCount())
        table->setColumnHidden(c, true);
}
```

No change needed — `c < table->columnCount()` (6 now) auto-filters stale 6 = old Link.

- [ ] **Step 5: Update `compareResults` switch in `TankorentPage.cpp:1257-1280`**

BEFORE:
```cpp
// Post-Track-C layout: 0 Title, 1 Category, 2 Size, 3 Files,
// 4 Seeders, 5 Leechers, 6 Link.
switch (col) {
case 0: // Title
    return cmpThen(a.title.compare(b.title, Qt::CaseInsensitive) < 0);
case 1: // Category
    return cmpThen(a.category.compare(b.category, Qt::CaseInsensitive) < 0);
case 2: // Size
    return cmpThen(a.sizeBytes < b.sizeBytes);
case 4: // Seeders
    return cmpThen(a.seeders < b.seeders);
case 5: // Leechers
    return cmpThen(a.leechers < b.leechers);
default:
    return false;
}
```

AFTER:
```cpp
// Post-T7 layout: 0 Title, 1 Category, 2 Size, 3 Seeders, 4 Leechers,
// 5 Link. Files col removed.
switch (col) {
case 0: // Title
    return cmpThen(a.title.compare(b.title, Qt::CaseInsensitive) < 0);
case 1: // Category
    return cmpThen(a.category.compare(b.category, Qt::CaseInsensitive) < 0);
case 2: // Size
    return cmpThen(a.sizeBytes < b.sizeBytes);
case 3: // Seeders
    return cmpThen(a.seeders < b.seeders);
case 4: // Leechers
    return cmpThen(a.leechers < b.leechers);
default:
    return false;
}
```

And update `onResultsHeaderClicked` at `TankorentPage.cpp:1282-1312`:

BEFORE:
```cpp
// Non-sortable columns: ignore. Header indicator stays where it was.
// Post-Track-C: 3 Files, 6 Link.
if (col == 3 || col == 6) return;
```

AFTER:
```cpp
// Non-sortable columns: ignore. Header indicator stays where it was.
// Post-T7: only 5 Link is non-sortable.
if (col == 5) return;
```

AND:

BEFORE:
```cpp
// New column: pick the column-default direction. Numeric cols default
// descending (high seeders / large sizes first); strings ascending.
m_resultsSortCol = col;
const bool numeric = (col == 2 || col == 4 || col == 5);
m_resultsSortOrder = numeric ? Qt::DescendingOrder : Qt::AscendingOrder;
```

AFTER:
```cpp
// New column: pick the column-default direction. Numeric cols default
// descending (high seeders / large sizes first); strings ascending.
m_resultsSortCol = col;
const bool numeric = (col == 2 || col == 3 || col == 4);
m_resultsSortOrder = numeric ? Qt::DescendingOrder : Qt::AscendingOrder;
```

- [ ] **Step 6: Update render loop in `renderResults()` for new column count**

`TankorentPage.cpp:1156` (table->setRowCount stays).

`TankorentPage.cpp:1158-1247` (render loop) — remove the Files item construction (currently at lines 1180-1182) and shift the Seeders/Leechers/Link assignments down one column index. Replace block at lines 1170-1247:

REMOVE the Files item:
```cpp
// Files
auto *filesItem = new QTableWidgetItem("-");
filesItem->setTextAlignment(Qt::AlignCenter);
m_resultsTable->setItem(i, 3, filesItem);
```

CHANGE Seeders col from 4 → 3:
```cpp
auto *seedItem = new QTableWidgetItem(QString::number(r.seeders));
seedItem->setTextAlignment(Qt::AlignCenter);
m_resultsTable->setItem(i, 3, seedItem);   // was 4
```

CHANGE Leechers col from 5 → 4:
```cpp
auto *leechItem = new QTableWidgetItem(QString::number(r.leechers));
leechItem->setTextAlignment(Qt::AlignCenter);
m_resultsTable->setItem(i, 4, leechItem);   // was 5
```

CHANGE Link col from 6 → 5:
```cpp
m_resultsTable->setItem(i, 5, new QTableWidgetItem(QString()));   // was 6
...
m_resultsTable->setCellWidget(i, 5, linkCell);                     // was 6
```

CHANGE row-tint loop (lines 1243-1247) — column count now 6:

BEFORE:
```cpp
for (int c = 0; c < 7; ++c) {
    if (auto* cell = m_resultsTable->item(i, c))
        cell->setBackground(tint);
}
```

AFTER (note: T10 will REMOVE this row-tint block entirely; T7 just adjusts the column count):
```cpp
for (int c = 0; c < 6; ++c) {
    if (auto* cell = m_resultsTable->item(i, c))
        cell->setBackground(tint);
}
```

- [ ] **Step 7: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

Run: `build_and_run.bat` + `out/tankoctl.exe open-page tankorent`
Expected: page renders; results table shows 6 columns.
Cleanup: `scripts/stop-tankoban.ps1`.

- [ ] **Step 8: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T7 2026-MM-DD ~HH:MMpm — Tankorent results table: drop Files column (7 -> 6). Rebase sort col indices: Seeders 4 -> 3, Leechers 5 -> 4, Link 6 -> 5. Header default sort col 4 -> 3 in TankorentPage.h. QSettings migration in ctor: stale saved indices migrated (pre-T7 4 -> 3, 5 -> 4, etc). compareResults + onResultsHeaderClicked + renderResults all updated. Visibility menu auto-filters stale col indices >= 6 via existing columnCount() guard. Resolves 50-day-old feedback_tankorent_ui.md grievance on always-empty Files column. BUILD OK + structural smoke green.] | files: src/ui/pages/TankorentPage.h, src/ui/pages/TankorentPage.cpp, agents/chat.md
```

---

### Task 8: Category friendly-names mapping

**Files:**
- Modify: `src/ui/pages/TankorentPage.cpp` (add static helper + apply in renderResults Category cell)

- [ ] **Step 1: Add static helper near top of TankorentPage.cpp anonymous namespace**

Add after the `categoryOptionsForSite` function (`TankorentPage.cpp:189-199`):

```cpp
// T8 — Reverse-lookup: given a sourceKey + raw category ID emitted by the
// indexer (e.g. "1_2" from nyaa or "200" from piratebay), return the
// human-friendly label from the source's CategoryOption array (e.g.
// "Anime - English-translated", "Video"). Returns empty QString if no
// match — caller falls back to the result's own category string or em-dash.
static QString categoryDisplayName(const QString& sourceKey, const QString& rawId)
{
    if (rawId.isEmpty()) return QString();
    const CategoryOption* opts = categoryOptionsForSite(sourceKey);
    if (!opts) return QString();
    for (int i = 0; opts[i].label != nullptr; ++i) {
        if (rawId == QLatin1String(opts[i].value))
            return QString::fromLatin1(opts[i].label);
    }
    return QString();
}
```

- [ ] **Step 2: Use the helper in `renderResults()` Category cell**

`TankorentPage.cpp:1173-1174`:

BEFORE:
```cpp
// Category
m_resultsTable->setItem(i, 1, new QTableWidgetItem(
    r.category.isEmpty() ? r.categoryId : r.category));
```

AFTER:
```cpp
// Category — prefer human-friendly name resolved from per-source map.
// Falls back to r.category if scraper already gave us a friendly string,
// else em-dash for "unknown".
QString categoryText = categoryDisplayName(r.sourceKey, r.categoryId);
if (categoryText.isEmpty()) categoryText = r.category;
if (categoryText.isEmpty()) categoryText = QStringLiteral("—");  // em-dash
m_resultsTable->setItem(i, 1, new QTableWidgetItem(categoryText));
```

- [ ] **Step 3: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 4: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T8 2026-MM-DD ~HH:MMpm — Tankorent Category column shows human-friendly names. NEW static helper categoryDisplayName(sourceKey, rawId) reverse-looks-up per-source CategoryOption arrays. Render path: helper first, r.category fallback, em-dash for unknown. Resolves the "1_2" raw-ID display bug flagged in feedback_tankorent_ui.md. BUILD OK.] | files: src/ui/pages/TankorentPage.cpp, agents/chat.md
```

---

### Task 9: Bulk-group Category cell "videos" → "Videos"

**Files:**
- Modify: `src/ui/pages/TankorentPage.cpp:1977`

- [ ] **Step 1: One-character edit**

`TankorentPage.cpp:1977`:

BEFORE: `catItem->setText(QStringLiteral("videos"));`
AFTER:  `catItem->setText(QStringLiteral("Videos"));`

- [ ] **Step 2: Build verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 3: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T9 2026-MM-DD ~HH:MMpm — Tankorent bulk-group Category cell "videos" -> "Videos" at TankorentPage.cpp:1977. Title Case rule (CR.8 in spec) applied. BUILD OK.] | files: src/ui/pages/TankorentPage.cpp, agents/chat.md
```

---

### Task 10: Trust signal — remove row tints + bold seeder count

**Files:**
- Modify: `src/ui/pages/TankorentPage.cpp` (renderResults loop — drop tint block, add seeder bold + dim)

- [ ] **Step 1: Remove tint constants + tint application loop**

`TankorentPage.cpp:1233-1247` — DELETE the entire block:

```cpp
// REMOVE:
static const QBrush kHealthyOdd (QColor(76, 175, 80, 26));   // ~0.10 alpha
static const QBrush kHealthyEven(QColor(76, 175, 80, 44));   // ~0.17 alpha
static const QBrush kPoorOdd    (QColor(239, 68, 68, 26));
static const QBrush kPoorEven   (QColor(239, 68, 68, 44));
const QString cls = trustClass(r);
if (cls != QLatin1String("normal")) {
    const bool even = (i % 2 == 0);
    const QBrush& tint = (cls == QLatin1String("healthy"))
                           ? (even ? kHealthyEven : kHealthyOdd)
                           : (even ? kPoorEven    : kPoorOdd);
    for (int c = 0; c < 6; ++c) {
        if (auto* cell = m_resultsTable->item(i, c))
            cell->setBackground(tint);
    }
}
```

- [ ] **Step 2: Insert bold/dim seeder treatment in the Seeders cell creation**

Locate the Seeders cell creation in the render loop. Post-T7 it's at:

```cpp
auto *seedItem = new QTableWidgetItem(QString::number(r.seeders));
seedItem->setTextAlignment(Qt::AlignCenter);
m_resultsTable->setItem(i, 3, seedItem);
```

Replace with:

```cpp
auto *seedItem = new QTableWidgetItem(QString::number(r.seeders));
seedItem->setTextAlignment(Qt::AlignCenter);
// T10 — trust signal via weight + foreground (no row tint).
if (r.seeders >= 50) {
    QFont f = seedItem->font();
    f.setBold(true);
    seedItem->setFont(f);
} else if (r.seeders < 5) {
    seedItem->setForeground(QBrush(fgMutedColor()));
}
m_resultsTable->setItem(i, 3, seedItem);
```

- [ ] **Step 3: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

Run: `build_and_run.bat`. Trigger a search (any query — e.g. `dune`). Visually confirm: no green/red row tints; healthy rows have bold seeder counts; dead rows have dim gray seeder counts.
Cleanup: `scripts/stop-tankoban.ps1`.

- [ ] **Step 4: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T10 2026-MM-DD ~HH:MMpm — Tankorent trust signal: removed green/red row tints (kHealthyOdd/Even + kPoorOdd/Even brushes + tint-application loop deleted from renderResults). Replaced with weight+color on Seeders cell only: bold for >=50 seeders; dim gray (fgMutedColor) for <5 seeders. Mid-range stays normal. Color violation flagged in feedback_no_color_no_emoji.md cleared. BUILD OK + visual smoke green per Hemanth.] | files: src/ui/pages/TankorentPage.cpp, agents/chat.md
```

---

### Task 11: Title cell custom delegate

**Files:**
- Modify: `src/ui/pages/TankorentPage.h` (add TitleCellDelegate class declaration)
- Modify: `src/ui/pages/TankorentPage.cpp` (delegate impl + register on column 0 + renderResults Title cell rebuild)

- [ ] **Step 1: Declare TitleCellDelegate class in TankorentPage.h**

Add after `class TankorentPage` declaration, at end of the file (before `};`):

```cpp
// Forward decl for the new delegate
class TitleCellDelegate;
```

Then add a separate class declaration at file scope, after `class TankorentPage { ... };` closing brace:

```cpp
// T11 — paint Title cell with three segments at different palette weights:
//   "<source>  ·  <title>  ·  <quality>"
//   source + quality in palette fgMuted; title in palette fg.
class TitleCellDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit TitleCellDelegate(QObject* parent = nullptr);
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
};
```

Add `#include <QStyledItemDelegate>` near the top of the header.

- [ ] **Step 2: Implement TitleCellDelegate in TankorentPage.cpp**

Add after the `TankorentPage` constructor body, at file scope (above destructor or buildUI):

```cpp
// ══════════════════════════════════════════════════════════════════════════════
// T11 — TitleCellDelegate impl
// ══════════════════════════════════════════════════════════════════════════════

TitleCellDelegate::TitleCellDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {}

namespace {
struct TitleSegments {
    QString source;
    QString title;
    QString quality;
};
}
Q_DECLARE_METATYPE(TitleSegments)

void TitleCellDelegate::paint(QPainter* painter,
                              const QStyleOptionViewItem& option,
                              const QModelIndex& index) const
{
    // Hover/selection background painted by base style first
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    opt.text.clear();   // suppress default text paint
    QStyledItemDelegate::paint(painter, opt, index);

    const QVariant data = index.data(Qt::UserRole + 1);
    if (!data.canConvert<TitleSegments>()) {
        // Fallback: paint default text if metadata missing.
        painter->save();
        painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(option.rect.adjusted(8, 0, -8, 0),
                          Qt::AlignVCenter | Qt::AlignLeft,
                          index.data(Qt::DisplayRole).toString());
        painter->restore();
        return;
    }

    const TitleSegments seg = data.value<TitleSegments>();
    painter->save();

    const QFontMetrics fm(option.font);
    const QString sep = QStringLiteral("  ·  ");   // " · "

    int x = option.rect.left() + 8;
    const int y = option.rect.center().y() + fm.ascent() / 2 - 1;
    const int rightLimit = option.rect.right() - 8;

    const QColor fgMain  = option.palette.color(QPalette::Text);
    QColor fgDim         = fgMain;
    fgDim.setAlpha(140);

    auto drawSeg = [&](const QString& text, const QColor& col) {
        if (text.isEmpty() || x >= rightLimit) return;
        painter->setPen(col);
        const int avail = rightLimit - x;
        const QString elided = fm.elidedText(text, Qt::ElideRight, avail);
        painter->drawText(x, y, elided);
        x += fm.horizontalAdvance(elided);
    };

    if (!seg.source.isEmpty()) {
        drawSeg(seg.source, fgDim);
        drawSeg(sep, fgDim);
    }
    drawSeg(seg.title, fgMain);
    if (!seg.quality.isEmpty()) {
        drawSeg(sep, fgDim);
        drawSeg(seg.quality, fgDim);
    }

    painter->restore();
}

QSize TitleCellDelegate::sizeHint(const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const
{
    // Hardcoded row height per feedback_qt_sizehintforrow_unreliable_pre_show.md.
    // Table also calls setDefaultSectionSize(32) — this is the per-row fallback.
    QSize s = QStyledItemDelegate::sizeHint(option, index);
    s.setHeight(32);
    return s;
}
```

- [ ] **Step 3: Register the delegate on column 0 of results table**

In the `TankorentPage::TankorentPage` constructor, after `buildUI();` line:

```cpp
m_resultsTable->setItemDelegateForColumn(0, new TitleCellDelegate(this));
```

- [ ] **Step 4: Populate UserRole+1 metadata in renderResults Title cell**

Locate the Title cell creation in `renderResults()`. Currently (post-existing code at ~line 1163-1170):

```cpp
const QString tags    = qualityTagSuffix(r.title);
const QString badged  = r.sourceName.isEmpty()
    ? r.title
    : QStringLiteral("[%1]  %2").arg(r.sourceName, r.title);
const QString display = tags.isEmpty() ? badged : badged + "  " + tags;
auto *titleItem = new QTableWidgetItem(display);
titleItem->setToolTip(r.title);
m_resultsTable->setItem(i, 0, titleItem);
```

Replace with:

```cpp
const QString tags = qualityTagSuffix(r.title);  // already strips brackets per qualityTagSuffix
// T11 — split into three segments for the custom delegate.
TitleSegments seg;
seg.source = r.sourceName;
seg.title  = r.title;
// qualityTagSuffix returns " [1080p]  [HEVC]  [BluRay]" with brackets — strip
// for the delegate's middle-dot join.
QString cleanQuality = tags;
cleanQuality.remove(QLatin1Char('['));
cleanQuality.remove(QLatin1Char(']'));
cleanQuality = cleanQuality.simplified();  // collapse runs of whitespace
seg.quality = cleanQuality;

auto *titleItem = new QTableWidgetItem;
titleItem->setData(Qt::DisplayRole,
    QStringLiteral("%1 - %2 - %3").arg(seg.source, seg.title, seg.quality));  // fallback display if delegate fails
titleItem->setData(Qt::UserRole + 1, QVariant::fromValue(seg));
titleItem->setToolTip(r.title);
m_resultsTable->setItem(i, 0, titleItem);
```

- [ ] **Step 5: Add `Q_DECLARE_METATYPE(TitleSegments)` registration**

Already added in Step 2 inside the anonymous namespace. Confirm it's compiled (single TU).

Note: `TitleSegments` lives in anonymous namespace inside `TankorentPage.cpp`. `Q_DECLARE_METATYPE` works at file scope; it's fine.

- [ ] **Step 6: Build verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

Run: `build_and_run.bat`. Trigger search. Visually confirm: title cells show `<Source>  ·  <Title>  ·  <Quality>` with source + quality in dim gray.
Cleanup: `scripts/stop-tankoban.ps1`.

- [ ] **Step 7: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T11 2026-MM-DD ~HH:MMpm — Tankorent Title cell custom delegate. NEW TitleCellDelegate (QStyledItemDelegate) paints three segments: source (dim) + middle-dot + title (normal) + middle-dot + quality (dim). Hardcoded sizeHint height 32 per feedback_qt_sizehintforrow rule. TitleSegments struct stored in UserRole+1; fallback DisplayRole carries " - " concat for non-delegate paths. renderResults() Title cell rebuilds segments from r.sourceName + r.title + qualityTagSuffix() with bracket strip + simplified(). Registered on column 0 in ctor. BUILD OK + visual smoke green.] | files: src/ui/pages/TankorentPage.h, src/ui/pages/TankorentPage.cpp, agents/chat.md
```

---

### Task 12: Link column SVG icons (replace ↓ / M text)

**Files:**
- Modify: `src/ui/pages/TankorentPage.cpp` (renderResults Link column QToolButton creation)

- [ ] **Step 1: Replace `↓` glyph with SVG icon on the download button**

In `renderResults()`, locate (post-T7 the column is 5):

```cpp
auto *dlBtn = new QToolButton(linkCell);
dlBtn->setText(QStringLiteral("↓"));   // ↓
dlBtn->setToolTip("Add torrent");
dlBtn->setCursor(Qt::PointingHandCursor);
dlBtn->setAutoRaise(true);
```

Replace with:

```cpp
auto *dlBtn = new QToolButton(linkCell);
dlBtn->setIcon(QIcon(QStringLiteral(":/icons/download-arrow.svg")));
dlBtn->setIconSize(QSize(16, 16));
dlBtn->setToolTip("Add torrent");
dlBtn->setCursor(Qt::PointingHandCursor);
dlBtn->setAutoRaise(true);
```

- [ ] **Step 2: Replace `M` glyph with SVG icon on the magnet button**

```cpp
auto *magBtn = new QToolButton(linkCell);
magBtn->setText(QStringLiteral("M"));
magBtn->setToolTip("Copy magnet link");
magBtn->setCursor(Qt::PointingHandCursor);
magBtn->setAutoRaise(true);
```

Replace with:

```cpp
auto *magBtn = new QToolButton(linkCell);
magBtn->setIcon(QIcon(QStringLiteral(":/icons/magnet.svg")));
magBtn->setIconSize(QSize(16, 16));
magBtn->setToolTip("Copy magnet link");
magBtn->setCursor(Qt::PointingHandCursor);
magBtn->setAutoRaise(true);
```

- [ ] **Step 3: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

Run: `build_and_run.bat`. Trigger search. Visually confirm: Link column shows two grayscale SVG icons (download arrow + magnet), no text glyphs.
Cleanup: `scripts/stop-tankoban.ps1`.

- [ ] **Step 4: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T12 2026-MM-DD ~HH:MMpm — Tankorent Link column QToolButtons: replaced "↓" (down-arrow text) with QIcon(:/icons/download-arrow.svg); replaced "M" text with QIcon(:/icons/magnet.svg) authored in T1. setIconSize(16,16). setText() removed; setAutoRaise(true) preserved. Visual smoke: two grayscale SVG icons visible per row, no text glyphs. BUILD OK.] | files: src/ui/pages/TankorentPage.cpp, agents/chat.md
```

---

### Task 13: Header / cell alignment match (Tankorent results + transfers)

**Files:**
- Modify: `src/ui/pages/TankorentPage.cpp` (createResultsTable + createTransfersTable + renderResults + renderTorrentRow)

- [ ] **Step 1: Set header alignment in createResultsTable() (post-T7 6-col)**

In `createResultsTable()`, after the `setHorizontalHeaderLabels` call, add:

```cpp
// T13 — match header alignment to cell-data alignment per column.
hdr->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
auto setHdrAlign = [&](int col, Qt::Alignment a) {
    table->horizontalHeaderItem(col)
        ? table->horizontalHeaderItem(col)->setTextAlignment(a)
        : ({ auto* hi = new QTableWidgetItem(headers[col]);
             hi->setTextAlignment(a);
             table->setHorizontalHeaderItem(col, hi); }());
};
// Title col 0: left (default).
// Category col 1: left (default).
// Size col 2: center.
setHdrAlign(2, Qt::AlignCenter | Qt::AlignVCenter);
// Seeders col 3: center.
setHdrAlign(3, Qt::AlignCenter | Qt::AlignVCenter);
// Leechers col 4: center.
setHdrAlign(4, Qt::AlignCenter | Qt::AlignVCenter);
// Link col 5: center.
setHdrAlign(5, Qt::AlignCenter | Qt::AlignVCenter);
```

Note: GCC/MSVC may dislike the statement-expression. Refactor to explicit per-col:

```cpp
hdr->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
for (int col : { 2, 3, 4, 5 }) {
    auto* hi = table->horizontalHeaderItem(col);
    if (!hi) {
        hi = new QTableWidgetItem(headers[col]);
        table->setHorizontalHeaderItem(col, hi);
    }
    hi->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
}
```

- [ ] **Step 2: Update Size cell to AlignCenter in renderResults()**

In `renderResults()`:

BEFORE:
```cpp
m_resultsTable->setItem(i, 2, new QTableWidgetItem(humanSize(r.sizeBytes)));
```

AFTER:
```cpp
auto* sizeItem = new QTableWidgetItem(humanSize(r.sizeBytes));
sizeItem->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
m_resultsTable->setItem(i, 2, sizeItem);
```

Seeders + Leechers already AlignCenter (post-T7); Link already centered via linkLay.

- [ ] **Step 3: Same in createTransfersTable() (12-col)**

After `setHorizontalHeaderLabels`:

```cpp
hdr->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
for (int col : { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }) {
    auto* hi = table->horizontalHeaderItem(col);
    if (!hi) {
        hi = new QTableWidgetItem(headers[col]);
        table->setHorizontalHeaderItem(col, hi);
    }
    Qt::Alignment a;
    if (col == 1 || col == 6 || col == 7) {
        a = Qt::AlignRight | Qt::AlignVCenter;   // Size, DownSpeed, UpSpeed
    } else if (col == 9) {
        a = Qt::AlignLeft | Qt::AlignVCenter;    // Category text
    } else {
        a = Qt::AlignCenter | Qt::AlignVCenter;  // Progress, Status, Seeds, Peers, ETA, Queue, Info
    }
    hi->setTextAlignment(a);
}
```

`renderTorrentRow` already aligns cells correctly: Size right, DL/UL right, Status default (left — but we want center to match center header). Adjust:

In `renderTorrentRow`, locate the `stateItem` block (col 3 — already calls `setText` without `setTextAlignment`). Add:

```cpp
stateItem->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
```

Same for `ensureItem(row, 9)->setText(t.category...)` — add `->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);` for explicit-ness.

- [ ] **Step 4: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

Run: `build_and_run.bat`. Trigger search + view transfers. Visually confirm: Size / Seeders / Leechers / Status columns have headers AND cells center-aligned.
Cleanup: `scripts/stop-tankoban.ps1`.

- [ ] **Step 5: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T13 2026-MM-DD ~HH:MMpm — Tankorent header/cell alignment match per spec CR.9. Results table: default left+vcenter; cols 2/3/4/5 (Size/Seeders/Leechers/Link) center+vcenter on both header and cells. Transfers table: cols 1/6/7 (Size/Down/Up) right; col 9 (Category) left; others center. Set via QTableWidgetItem on horizontalHeaderItem + per-cell setTextAlignment. BUILD OK + visual smoke green.] | files: src/ui/pages/TankorentPage.cpp, agents/chat.md
```

---

### Task 14: Hover row state on Tankorent tables

**Files:**
- Modify: `src/ui/pages/TankorentPage.cpp` (createResultsTable + createTransfersTable QSS)

- [ ] **Step 1: Append hover rule to results table QSS**

In `createResultsTable()`, extend the `setStyleSheet` string (T4 has set the base). Add the hover rule:

BEFORE (post-T4):
```cpp
"#SearchResultsTable::item:selected { background: rgba(192,200,212,36); color: #eeeeee; }"
```

AFTER (insert hover rule above selected):
```cpp
"#SearchResultsTable::item:hover { background: rgba(255,255,255,0.04); }"
"#SearchResultsTable::item:selected { background: rgba(192,200,212,36); color: #eeeeee; }"
```

- [ ] **Step 2: Same for transfers table**

In `createTransfersTable()`:

BEFORE:
```cpp
"#TransfersTable::item:selected { background: rgba(192,200,212,36); color: #eeeeee; }"
```

AFTER:
```cpp
"#TransfersTable::item:hover { background: rgba(255,255,255,0.04); }"
"#TransfersTable::item:selected { background: rgba(192,200,212,36); color: #eeeeee; }"
```

- [ ] **Step 3: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

Run: `build_and_run.bat`. Move mouse over rows. Confirm: subtle highlight appears on the hovered row, fades when mouse leaves.
Cleanup: `scripts/stop-tankoban.ps1`.

- [ ] **Step 4: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T14 2026-MM-DD ~HH:MMpm — Tankorent hover row state on both result + transfer tables. QSS rule "#SearchResultsTable::item:hover { background: rgba(255,255,255,0.04); }" scoped under #ObjectName per feedback_css_scoping.md. Selection bg (rgba 192,200,212,36) stays brighter, falls back cleanly when row is selected. BUILD OK + visual smoke green.] | files: src/ui/pages/TankorentPage.cpp, agents/chat.md
```

---

### Task 15: Empty/loading/zero-results states for Tankorent

**Files:**
- Modify: `src/ui/pages/TankorentPage.h` (new state-page member declarations)
- Modify: `src/ui/pages/TankorentPage.cpp` (buildMainTabs refactor: results table → QStackedWidget; state-page builders; updateResultsView slot)

- [ ] **Step 1: Add state-page members to TankorentPage.h**

Add inside the private section, near other m_emptyXxx if any (Tankorent has none yet):

```cpp
// T15 — empty/loading/zero-results state pages for Search Results tab.
QStackedWidget* m_resultsStack    = nullptr;   // wraps the existing m_resultsTable + state pages
QWidget*        m_emptyPage       = nullptr;
QLabel*         m_emptyLabel      = nullptr;
QWidget*        m_loadingPage     = nullptr;
QLabel*         m_loadingLabel    = nullptr;
QWidget*        m_noResultsPage   = nullptr;
QLabel*         m_noResultsLabel  = nullptr;
QPushButton*    m_noResultsRetry  = nullptr;
QPushButton*    m_noResultsClear  = nullptr;
QString         m_lastQuery;

void updateResultsView();   // slot to flip between table / empty / loading / no-results
```

Add includes if missing: `#include <QStackedWidget>` (probably already transitively).

- [ ] **Step 2: Refactor buildMainTabs to wrap results table in stacked widget**

`TankorentPage.cpp:659-684`:

BEFORE:
```cpp
void TankorentPage::buildMainTabs(QVBoxLayout* parent)
{
    m_resultsCountLabel = new QLabel;
    ...
    m_tabWidget = new QTabWidget;
    m_resultsTable = createResultsTable();
    m_tabWidget->addTab(m_resultsTable, "Search Results");
    m_transfersTable = createTransfersTable();
    m_tabWidget->addTab(m_transfersTable, "Transfers");
    parent->addWidget(m_tabWidget, 1);
}
```

AFTER:
```cpp
void TankorentPage::buildMainTabs(QVBoxLayout* parent)
{
    m_resultsCountLabel = new QLabel;
    m_resultsCountLabel->setStyleSheet("color: #a1a1aa; font-size: 13px; padding: 4px 0;");
    m_resultsCountLabel->setOpenExternalLinks(false);
    m_resultsCountLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_resultsCountLabel->hide();
    connect(m_resultsCountLabel, &QLabel::linkActivated, this, [this](const QString&) {
        m_showAll = true;
        renderResults();
    });
    parent->addWidget(m_resultsCountLabel);

    m_tabWidget = new QTabWidget;

    m_resultsTable = createResultsTable();

    // T15 — empty state page
    m_emptyPage = new QWidget;
    {
        auto* v = new QVBoxLayout(m_emptyPage);
        v->setAlignment(Qt::AlignCenter);
        v->setSpacing(16);
        m_emptyLabel = new QLabel(
            QStringLiteral("Type a query and hit Enter — e.g. \"the boys 1080p\" or \"sapiens 2014\""),
            m_emptyPage);
        m_emptyLabel->setAlignment(Qt::AlignCenter);
        m_emptyLabel->setWordWrap(true);
        m_emptyLabel->setStyleSheet("color: #a1a1aa; font-size: 15px;");
        v->addWidget(m_emptyLabel);
    }

    // T15 — loading state page
    m_loadingPage = new QWidget;
    {
        auto* v = new QVBoxLayout(m_loadingPage);
        v->setAlignment(Qt::AlignCenter);
        v->setSpacing(16);
        m_loadingLabel = new QLabel(QStringLiteral("Searching..."), m_loadingPage);
        m_loadingLabel->setAlignment(Qt::AlignCenter);
        m_loadingLabel->setStyleSheet("color: #cbd5e1; font-size: 15px;");
        v->addWidget(m_loadingLabel);
        auto* bar = new QProgressBar(m_loadingPage);
        bar->setRange(0, 0);
        bar->setTextVisible(false);
        bar->setFixedWidth(220);
        bar->setFixedHeight(4);
        bar->setStyleSheet(QStringLiteral(
            "QProgressBar { background: rgba(255,255,255,0.08); border: none; "
            "border-radius: 2px; }"
            "QProgressBar::chunk { background: %1; border-radius: 2px; }")
            .arg(QApplication::palette().color(QPalette::Highlight).name()));
        v->addWidget(bar, 0, Qt::AlignCenter);
    }

    // T15 — no-results page
    m_noResultsPage = new QWidget;
    {
        auto* v = new QVBoxLayout(m_noResultsPage);
        v->setAlignment(Qt::AlignCenter);
        v->setSpacing(16);
        m_noResultsLabel = new QLabel(m_noResultsPage);
        m_noResultsLabel->setAlignment(Qt::AlignCenter);
        m_noResultsLabel->setWordWrap(true);
        m_noResultsLabel->setStyleSheet("color: #a1a1aa; font-size: 15px;");
        v->addWidget(m_noResultsLabel);
        auto* btnRow = new QHBoxLayout;
        btnRow->setSpacing(10);
        btnRow->setAlignment(Qt::AlignCenter);
        m_noResultsRetry = new QPushButton(QStringLiteral("Retry"), m_noResultsPage);
        m_noResultsRetry->setFixedHeight(32);
        m_noResultsRetry->setCursor(Qt::PointingHandCursor);
        connect(m_noResultsRetry, &QPushButton::clicked, this, [this]() {
            if (!m_lastQuery.isEmpty()) {
                m_queryEdit->setText(m_lastQuery);
                startSearch();
            }
        });
        btnRow->addWidget(m_noResultsRetry);
        m_noResultsClear = new QPushButton(QStringLiteral("Clear"), m_noResultsPage);
        m_noResultsClear->setFixedHeight(32);
        m_noResultsClear->setCursor(Qt::PointingHandCursor);
        connect(m_noResultsClear, &QPushButton::clicked, this, [this]() {
            m_queryEdit->clear();
            m_lastQuery.clear();
            m_queryEdit->setFocus();
            updateResultsView();
        });
        btnRow->addWidget(m_noResultsClear);
        v->addLayout(btnRow);
    }

    m_resultsStack = new QStackedWidget;
    m_resultsStack->addWidget(m_resultsTable);   // index 0
    m_resultsStack->addWidget(m_emptyPage);      // index 1
    m_resultsStack->addWidget(m_loadingPage);    // index 2
    m_resultsStack->addWidget(m_noResultsPage);  // index 3
    m_resultsStack->setCurrentIndex(1);          // start on empty
    m_tabWidget->addTab(m_resultsStack, "Search Results");

    m_transfersTable = createTransfersTable();
    m_tabWidget->addTab(m_transfersTable, "Transfers");

    parent->addWidget(m_tabWidget, 1);
}
```

- [ ] **Step 3: Implement updateResultsView slot**

Add at end of TankorentPage.cpp (file-scope method):

```cpp
void TankorentPage::updateResultsView()
{
    if (!m_resultsStack) return;
    if (m_pendingSearches > 0) {
        m_resultsStack->setCurrentIndex(2);   // loading
        if (m_loadingLabel)
            m_loadingLabel->setText(QStringLiteral("Searching %1 source%2...")
                                    .arg(m_activeIndexers.size())
                                    .arg(m_activeIndexers.size() == 1 ? "" : "s"));
        return;
    }
    if (m_allResults.isEmpty()) {
        if (m_lastQuery.isEmpty()) {
            m_resultsStack->setCurrentIndex(1);   // empty (pre-search)
        } else {
            m_resultsStack->setCurrentIndex(3);   // no-results
            if (m_noResultsLabel)
                m_noResultsLabel->setText(QStringLiteral("No results for \"%1\". Try a different query or open Sources to enable more.")
                                          .arg(m_lastQuery));
        }
        return;
    }
    m_resultsStack->setCurrentIndex(0);   // data view (table)
}
```

- [ ] **Step 4: Wire updateResultsView into startSearch / onSearchFinished / onSearchError**

In `startSearch()` (TankorentPage.cpp:970-1000), after setting `m_searchStatus->setText("Searching...");`:

```cpp
m_lastQuery = query;
updateResultsView();
```

In `onSearchFinished()` (line 1015-1035), at the end after the `m_pendingSearches <= 0` block:

```cpp
updateResultsView();
```

In `onSearchError()` (line 1037-1055), at the end:

```cpp
updateResultsView();
```

- [ ] **Step 5: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

Run: `build_and_run.bat`. Open Tankorent. Confirm: pre-search empty state visible. Type query, hit Enter → loading state with progress bar. Try a junk query that returns 0 results → no-results state with Retry + Clear buttons.
Cleanup: `scripts/stop-tankoban.ps1`.

- [ ] **Step 6: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T15 2026-MM-DD ~HH:MMpm — Tankorent empty/loading/zero-results states ported from Tankoyomi pattern. NEW QStackedWidget m_resultsStack wraps existing m_resultsTable at index 0 + 3 new state pages (empty/loading/no-results). NEW updateResultsView() slot picks page based on m_pendingSearches + m_allResults size + m_lastQuery. Wired into startSearch/onSearchFinished/onSearchError. Pre-search hint: "Type a query and hit Enter -- e.g. 'the boys 1080p'..."; loading shows N-source count + indeterminate bar; no-results offers Retry + Clear. BUILD OK + visual smoke green.] | files: src/ui/pages/TankorentPage.h, src/ui/pages/TankorentPage.cpp, agents/chat.md
```

---

# Phase 4 — Tankoyomi specifics (Tasks 16-21)

### Task 16: Tankoyomi loading bar blue → theme accent

**Files:**
- Modify: `src/ui/pages/TankoyomiPage.cpp:457-461` (loading bar QSS)

- [ ] **Step 1: Replace the blue chunk color**

`TankoyomiPage.cpp:457-461`:

BEFORE:
```cpp
bar->setStyleSheet(
    "QProgressBar { background: rgba(255,255,255,0.08); border: none; "
    "  border-radius: 2px; }"
    "QProgressBar::chunk { background: #60a5fa; border-radius: 2px; }");
```

AFTER:
```cpp
{
    const QColor accent = QApplication::palette().color(QPalette::Highlight);
    bar->setStyleSheet(QStringLiteral(
        "QProgressBar { background: rgba(255,255,255,0.08); border: none; "
        "  border-radius: 2px; }"
        "QProgressBar::chunk { background: %1; border-radius: 2px; }")
        .arg(accent.name()));
}
```

- [ ] **Step 2: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

Run: `build_and_run.bat` → Tankoyomi tab → trigger search → confirm loading bar is no longer blue (should show theme accent — gold on Dark mode).
Cleanup: `scripts/stop-tankoban.ps1`.

- [ ] **Step 3: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T16 2026-MM-DD ~HH:MMpm — Tankoyomi loading bar color violation fix: blue #60a5fa chunk -> theme accent (QPalette::Highlight, gold in Dark mode). Reads palette color at construct time + interpolates into the QSS string. Resolves feedback_no_color_no_emoji.md violation. BUILD OK + visual smoke green.] | files: src/ui/pages/TankoyomiPage.cpp, agents/chat.md
```

---

### Task 17: chapterStatusText helper + apply at Transfers render

**Files:**
- Modify: `src/ui/pages/TankoyomiPage.h` (helper decl in private section)
- Modify: `src/ui/pages/TankoyomiPage.cpp` (helper impl + apply at refreshTransfers status-cell write)

- [ ] **Step 1: Audit MangaDownloader for the full state vocabulary**

Before writing the mapping, the implementing agent greps the codebase:

```
grep -rn 'state.*=.*"' src/core/manga/MangaDownloader.cpp | grep -v sourceId
grep -rn 'state ==.*"' src/core/manga/MangaDownloader.cpp
grep -rn 'setState' src/core/manga/MangaDownloader.cpp
```

Expected states (per memory): `queued`, `downloading`, `paused`, `complete`, `failed`. Confirm none missed.

- [ ] **Step 2: Add helper to TankoyomiPage.cpp anonymous namespace**

Add inside the namespace at top of file (the namespace was added in T3 for `fgMutedColor`):

```cpp
// T17 — map raw MangaDownloader chapter-state enum strings to Title Case
// display strings. Applied at transfer-row render boundary. Audited
// MangaDownloader source 2026-05-13 to enumerate vocabulary.
inline QString chapterStatusText(const QString& rawState)
{
    if (rawState == QLatin1String("queued"))      return QStringLiteral("Queued");
    if (rawState == QLatin1String("downloading")) return QStringLiteral("Downloading");
    if (rawState == QLatin1String("paused"))      return QStringLiteral("Paused");
    if (rawState == QLatin1String("complete"))    return QStringLiteral("Complete");
    if (rawState == QLatin1String("failed"))      return QStringLiteral("Failed");
    // Fallback: capitalize first letter for any state we missed in the audit.
    if (rawState.isEmpty()) return rawState;
    QString out = rawState;
    out[0] = out[0].toUpper();
    return out;
}
```

- [ ] **Step 3: Apply helper at refreshTransfers status-cell write**

In `TankoyomiPage.cpp`, find the body of `refreshTransfers()` and locate the cell-population for the Status column (col 2 of the transfers table). The exact line wasn't read end-to-end during the brainstorm — the implementing agent greps for the call site:

```
grep -n 'setText' src/ui/pages/TankoyomiPage.cpp | grep -i -E '(status|state|queued)'
```

Once located, replace the raw setText call:

BEFORE (illustrative):
```cpp
statusItem->setText(seriesState);   // seriesState is the lowercase enum
```

AFTER:
```cpp
statusItem->setText(chapterStatusText(seriesState));
```

If the status string is a composite (e.g. `"downloading 3/10"`), parse out the leading state token and capitalize only it:

```cpp
QString display = chapterStatusText(rawState);
if (!detail.isEmpty()) display += " " + detail;
statusItem->setText(display);
```

- [ ] **Step 4: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

Run: `build_and_run.bat` → Tankoyomi → Transfers tab. Confirm any in-flight series shows "Queued" / "Downloading" / "Paused" / "Complete" / "Failed" — no lowercase.

If you don't have a stuck transfer to test against, search for any manga, click into chapter list, queue a chapter — confirm "Queued" appears in the Status column.

Cleanup: `scripts/stop-tankoban.ps1`.

- [ ] **Step 5: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T17 2026-MM-DD ~HH:MMpm — Tankoyomi Title Case rule for transfer status strings. NEW chapterStatusText(rawState) helper in TankoyomiPage.cpp anonymous namespace, maps queued/downloading/paused/complete/failed -> Title Case. Fallback capitalizes first letter for any audit-missed state. Applied at refreshTransfers status-cell write. Resolves Hemanth-flagged "queued" lowercase shown in 2026-05-13 12:50pm screenshot. BUILD OK + visual smoke green.] | files: src/ui/pages/TankoyomiPage.cpp, agents/chat.md
```

---

### Task 18: Tankoyomi header/cell alignment match

**Files:**
- Modify: `src/ui/pages/TankoyomiPage.cpp` (createResultsTable + createTransfersTable + renderResults + refreshTransfers)

- [ ] **Step 1: Update createResultsTable header alignment (Title / Author / Source / Status / Type all left)**

In `createResultsTable()`, after `setHorizontalHeaderLabels`:

```cpp
hdr->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
// All 5 result columns are text fields; left+vcenter is appropriate.
```

- [ ] **Step 2: Update renderResults cell-alignment (all left+vcenter for consistency)**

In `renderResults()`, after each `setItem(i, col, ...)` for col 0-4, add `->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter)`. The implementing agent locates the loop body and applies it consistently.

Pseudocode (locate the cell creation lines and add explicit alignment):

```cpp
auto* titleItem = new QTableWidgetItem(r.title);
titleItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
m_resultsTable->setItem(i, 0, titleItem);
// Same pattern for cols 1-4
```

- [ ] **Step 3: Update createTransfersTable header alignment**

After `setHorizontalHeaderLabels`:

```cpp
hdr->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
// Per spec CR.9 convention:
// Series col 0: left (default).
// Progress col 1: center.
// Status col 2: center.
// Chapters col 3: center.
for (int col : { 1, 2, 3 }) {
    auto* hi = table->horizontalHeaderItem(col);
    if (!hi) {
        hi = new QTableWidgetItem(headers[col]);
        table->setHorizontalHeaderItem(col, hi);
    }
    hi->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
}
```

- [ ] **Step 4: Update refreshTransfers cell-alignment**

In `refreshTransfers()` body, at each cell setItem call (locate via grep `setItem.*m_transfersTable`):

```cpp
// Series col 0: left+vcenter
nameItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
// Progress col 1: center
progItem->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
// Status col 2: center
statusItem->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
// Chapters col 3: center
chaptersItem->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
```

- [ ] **Step 5: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

Run: `build_and_run.bat` → Tankoyomi → Transfers. Visually confirm header AND cell alignment match per column (Series left, Progress/Status/Chapters center).
Cleanup: `scripts/stop-tankoban.ps1`.

- [ ] **Step 6: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T18 2026-MM-DD ~HH:MMpm — Tankoyomi header/cell alignment match per spec CR.9. Results table: all 5 cols left+vcenter on both. Transfers: col 0 (Series) left; cols 1/2/3 (Progress/Status/Chapters) center on both header and cells. Set via QTableWidgetItem header alignment + per-cell setTextAlignment in refreshTransfers. Resolves Hemanth-flagged Transfers Status column misalignment shown in 2026-05-13 12:50pm screenshot. BUILD OK + visual smoke green.] | files: src/ui/pages/TankoyomiPage.cpp, agents/chat.md
```

---

### Task 19: Tankoyomi More button SVG icon

**Files:**
- Modify: `src/ui/pages/TankoyomiPage.cpp:334-338`

- [ ] **Step 1: Replace text glyph with SVG icon**

`TankoyomiPage.cpp:334-338`:

BEFORE:
```cpp
m_moreBtn = new QPushButton(QStringLiteral("⋮"));   // vertical ellipsis
m_moreBtn->setFixedSize(30, 30);
m_moreBtn->setCursor(Qt::PointingHandCursor);
m_moreBtn->setToolTip("More download actions");
m_moreBtn->setVisible(false);
```

AFTER:
```cpp
m_moreBtn = new QPushButton;
m_moreBtn->setIcon(QIcon(QStringLiteral(":/icons/kebab-menu.svg")));
m_moreBtn->setIconSize(QSize(16, 16));
m_moreBtn->setFixedSize(36, 36);
m_moreBtn->setCursor(Qt::PointingHandCursor);
m_moreBtn->setToolTip("More download actions");
m_moreBtn->setVisible(false);
```

- [ ] **Step 2: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

Tankoyomi More button is hidden by default (only visible when downloads active). To smoke: search → click into a series → queue a chapter (triggers More button visibility). Confirm SVG icon (3 dots vertical) renders.

Alternatively, since smoke-during-build cycles can be expensive, do a UIA inspection to confirm the icon path on the widget:

```
out\tankoctl.exe ping
out\tankoctl.exe open-page tankoyomi
```

Verify in `out/Tankoban.exe` window that the More button — when revealed — shows a clean icon, not text.

Cleanup: `scripts/stop-tankoban.ps1`.

- [ ] **Step 3: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T19 2026-MM-DD ~HH:MMpm — Tankoyomi More button "⋮" (vertical ellipsis) text -> QIcon(:/icons/kebab-menu.svg) authored in T1. setIconSize(16,16). Button size 30x30 -> 36x36 to match density lift. setText removed (was implicit via ctor; now explicit no-text). BUILD OK.] | files: src/ui/pages/TankoyomiPage.cpp, agents/chat.md
```

---

### Task 20: Hover row state on Tankoyomi tables

**Files:**
- Modify: `src/ui/pages/TankoyomiPage.cpp:520-528` (results table QSS) + `570-578` (transfers table QSS)

- [ ] **Step 1: Append hover rule to results table QSS**

Insert hover rule above the existing `::item:selected` rule in `createResultsTable()`:

```cpp
"#MangaResultsTable::item:hover { background: rgba(255,255,255,0.04); }"
```

- [ ] **Step 2: Same for transfers table**

In `createTransfersTable()`:

```cpp
"#MangaTransfersTable::item:hover { background: rgba(255,255,255,0.04); }"
```

- [ ] **Step 3: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

Run: `build_and_run.bat`, mouse over rows, confirm subtle hover state.

- [ ] **Step 4: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T20 2026-MM-DD ~HH:MMpm — Tankoyomi hover row state on both results + transfers tables per spec CR.5. QSS hover rule rgba(255,255,255,0.04) scoped under #MangaResultsTable + #MangaTransfersTable. BUILD OK + visual smoke green.] | files: src/ui/pages/TankoyomiPage.cpp, agents/chat.md
```

---

### Task 21: Tankoyomi empty/loading state density polish

**Files:**
- Modify: `src/ui/pages/TankoyomiPage.cpp` (empty page + loading page sub-builders inside buildMainTabs)

T5 already did most of the density bumps on Tankoyomi's empty/loading. T21 is a verification + minor padding sweep.

- [ ] **Step 1: Audit current state vs spec CR.4**

Tankoyomi's existing pattern (built in `buildMainTabs()` at lines 379-481) already matches CR.4 shape. T5 lifted the label fonts 14→15px and Retry/Clear buttons 28→32px. Confirm no additional padding tweaks needed; the page rendered cleanly during T5 smoke.

If `Vertical layout v->setSpacing(16)` lines in empty + loading pages feel cramped post-density-lift, bump to 20:

```cpp
v->setSpacing(20);
```

(Only apply if visual smoke after T5 looks cramped. Otherwise no-op task.)

- [ ] **Step 2: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 3: Chat.md READY TO COMMIT**

If changes made:
```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T21 2026-MM-DD ~HH:MMpm — Tankoyomi empty/loading state spacing polish post-density-lift. v->setSpacing 16 -> 20 on empty + loading page V-boxes. BUILD OK + visual smoke green.] | files: src/ui/pages/TankoyomiPage.cpp, agents/chat.md
```

If no changes needed: note in chat.md as no-op:
```
[Agent 4B, SOURCES_UI_REFINEMENT T21 2026-MM-DD ~HH:MMpm — empty/loading state density polish verified — T5 lift sufficient. No additional changes. SKIP.]
```

---

# Phase 5 — TankoLibrary specifics (Tasks 22-27)

### Task 22: Filters popover widget + button + dot indicator

**Files:**
- Modify: `src/ui/pages/TankoLibraryPage.h` (member decls)
- Modify: `src/ui/pages/TankoLibraryPage.cpp` (new widget construction + signal wiring)

- [ ] **Step 1: Add Filters popover member declarations to TankoLibraryPage.h**

Inside the private section, near the existing m_epubChk / m_pdfChk / m_mobiChk / m_englishOnlyCheckbox / m_audioFormatCombo declarations:

```cpp
// T22 — Filters popover (replaces inline checkboxes in search row).
QPushButton* m_filtersBtn          = nullptr;
QWidget*     m_filtersPopover      = nullptr;
QLabel*      m_filtersDotIndicator = nullptr;

void buildFiltersPopover();   // constructs m_filtersPopover and parents checkbox/combo widgets into it
void showFiltersPopover();    // opens popover anchored to m_filtersBtn
void updateFiltersDotIndicator();   // recomputes "any filter active" and shows/hides dot
bool anyFilterActive() const;
```

- [ ] **Step 2: Construct the Filters button + popover in buildResultsPage()**

In `buildResultsPage()` (after `m_cancelBtn` is added to `searchRow`, before the format checkboxes are built):

```cpp
// T22 — Filters button replaces inline format checkboxes + English-only + audio-format combo.
m_filtersBtn = new QPushButton(QStringLiteral("Filters ▾"), m_resultsPage);
m_filtersBtn->setFixedHeight(36);
m_filtersBtn->setCursor(Qt::PointingHandCursor);
m_filtersBtn->setToolTip(QStringLiteral("Open filter popover (format, language, audio format)"));
connect(m_filtersBtn, &QPushButton::clicked, this, &TankoLibraryPage::showFiltersPopover);
searchRow->addWidget(m_filtersBtn);

// Dot indicator overlay on the Filters button (top-right corner)
m_filtersDotIndicator = new QLabel(m_filtersBtn);
m_filtersDotIndicator->setFixedSize(8, 8);
m_filtersDotIndicator->setStyleSheet(QStringLiteral(
    "background: %1; border-radius: 4px;").arg(
    QApplication::palette().color(QPalette::Highlight).name()));
m_filtersDotIndicator->move(m_filtersBtn->width() - 14, 6);
m_filtersDotIndicator->hide();   // hidden until any filter active
```

- [ ] **Step 3: Implement buildFiltersPopover()**

Add to the file-scope methods of `TankoLibraryPage`. The popover is a `QWidget` with `Qt::Popup` window flag containing the checkboxes + audio-format combo. The existing checkbox-build lambda from `buildResultsPage()` moves into this method.

Add as a new method body:

```cpp
void TankoLibraryPage::buildFiltersPopover()
{
    m_filtersPopover = new QWidget(this, Qt::Popup);
    m_filtersPopover->setObjectName(QStringLiteral("TankoLibraryFiltersPopover"));
    m_filtersPopover->setStyleSheet(QStringLiteral(
        "#TankoLibraryFiltersPopover { background: #2a2a2a; border: 1px solid #444; "
        "border-radius: 4px; }"));

    auto* v = new QVBoxLayout(m_filtersPopover);
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(8);

    // Format section header
    auto* formatLbl = new QLabel(QStringLiteral("Format"), m_filtersPopover);
    formatLbl->setStyleSheet(QStringLiteral("color: #888; font-size: 12px; font-weight: 600;"));
    v->addWidget(formatLbl);

    // Build format checkboxes (re-use existing lambda from buildResultsPage)
    auto buildChk = [&](const QString& label, const QString& key, bool defaultOn) {
        auto* chk = new QCheckBox(label, m_filtersPopover);
        chk->setCursor(Qt::PointingHandCursor);
        chk->setChecked(QSettings().value(QStringLiteral("tankolibrary/%1").arg(key), defaultOn).toBool());
        connect(chk, &QCheckBox::toggled, this, &TankoLibraryPage::onFormatFilterToggled);
        v->addWidget(chk);
        return chk;
    };
    m_epubChk = buildChk(QStringLiteral("EPUB"), QStringLiteral("format_epub"), true);
    m_pdfChk  = buildChk(QStringLiteral("PDF"),  QStringLiteral("format_pdf"),  false);
    m_mobiChk = buildChk(QStringLiteral("MOBI"), QStringLiteral("format_mobi"), false);

    v->addSpacing(8);

    // Language section
    auto* langLbl = new QLabel(QStringLiteral("Language"), m_filtersPopover);
    langLbl->setStyleSheet(QStringLiteral("color: #888; font-size: 12px; font-weight: 600;"));
    v->addWidget(langLbl);

    m_englishOnlyCheckbox = new QCheckBox(QStringLiteral("English only"), m_filtersPopover);
    m_englishOnlyCheckbox->setCursor(Qt::PointingHandCursor);
    m_englishOnlyCheckbox->setChecked(QSettings().value(QStringLiteral("tankolibrary/english_only"), true).toBool());
    connect(m_englishOnlyCheckbox, &QCheckBox::toggled, this, &TankoLibraryPage::onEnglishOnlyToggled);
    v->addWidget(m_englishOnlyCheckbox);

    v->addSpacing(8);

    // Audiobook format section (visible on Audiobooks tab)
    auto* audioLbl = new QLabel(QStringLiteral("Audiobook format"), m_filtersPopover);
    audioLbl->setStyleSheet(QStringLiteral("color: #888; font-size: 12px; font-weight: 600;"));
    v->addWidget(audioLbl);

    m_audioFormatCombo = new QComboBox(m_filtersPopover);
    m_audioFormatCombo->setCursor(Qt::PointingHandCursor);
    m_audioFormatCombo->addItem(QStringLiteral("All formats"));
    m_audioFormatCombo->addItem(QStringLiteral("M4B only"));
    m_audioFormatCombo->addItem(QStringLiteral("MP3 only"));
    m_audioFormatCombo->setCurrentIndex(QSettings().value(QStringLiteral("tankolibrary/audio_format"), 0).toInt());
    connect(m_audioFormatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TankoLibraryPage::onAudioFormatChanged);
    v->addWidget(m_audioFormatCombo);

    // applyMediaTabFilterVisibility will hide/show format vs audio sections per tab
}

void TankoLibraryPage::showFiltersPopover()
{
    if (!m_filtersPopover) buildFiltersPopover();
    applyMediaTabFilterVisibility();   // refresh visibility per active media tab
    const QPoint anchor = m_filtersBtn->mapToGlobal(QPoint(0, m_filtersBtn->height() + 4));
    m_filtersPopover->move(anchor);
    m_filtersPopover->show();
}

bool TankoLibraryPage::anyFilterActive() const
{
    const bool booksTab = (m_mediaTab == MediaTab::Books);
    if (booksTab) {
        // Defaults: EPUB on, PDF off, MOBI off, English on.
        if (!m_epubChk->isChecked()) return true;
        if (m_pdfChk->isChecked()) return true;
        if (m_mobiChk->isChecked()) return true;
        if (!m_englishOnlyCheckbox->isChecked()) return true;
    } else {
        // Audiobooks tab: only English-only + audio-format relevant.
        if (!m_englishOnlyCheckbox->isChecked()) return true;
        if (m_audioFormatCombo->currentIndex() != 0) return true;
    }
    return false;
}

void TankoLibraryPage::updateFiltersDotIndicator()
{
    if (m_filtersDotIndicator)
        m_filtersDotIndicator->setVisible(anyFilterActive());
}
```

- [ ] **Step 4: Hook updateFiltersDotIndicator into existing slots**

In `onFormatFilterToggled` body (existing line 914), `onEnglishOnlyToggled` (line 924), `onAudioFormatChanged` (line 931) — append at the end of each:

```cpp
updateFiltersDotIndicator();
```

Also call once at the end of `buildResultsPage()` (after popover is set up + initial state restored) and in `setMediaTab()` (after applyMediaTabFilterVisibility):

```cpp
updateFiltersDotIndicator();
```

- [ ] **Step 5: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

Run: `build_and_run.bat`. Tankolibrary tab → click "Filters ▾" → popover opens with EPUB / PDF / MOBI / English only / Audiobook format. Toggle a filter → close popover → confirm dot indicator appears on Filters button. Reset → confirm dot disappears.
Cleanup: `scripts/stop-tankoban.ps1`.

- [ ] **Step 6: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T22 2026-MM-DD ~HH:MMpm — TankoLibrary Filters popover. NEW m_filtersBtn (36px, "Filters ▾" label) replaces inline EPUB/PDF/MOBI/English/Audio-format checkboxes in search row. NEW Qt::Popup widget m_filtersPopover holds the same controls (re-parented in T23). NEW dot indicator label m_filtersDotIndicator (8x8 circle at top-right of button) shows when anyFilterActive() returns true. Wired into existing filter-toggled slots + setMediaTab. applyMediaTabFilterVisibility still drives format-vs-audio section visibility per active media tab. BUILD OK + visual smoke green.] | files: src/ui/pages/TankoLibraryPage.h, src/ui/pages/TankoLibraryPage.cpp, agents/chat.md
```

---

### Task 23: Remove inline filter widgets from search row (consolidation)

**Files:**
- Modify: `src/ui/pages/TankoLibraryPage.cpp:534-616` (buildResultsPage searchRow construction)

T22 constructed the popover. T23 removes the original inline placements so the popover becomes the only home.

- [ ] **Step 1: Remove the inline buildFormatChk block**

In `buildResultsPage()`, locate the QSettings migration block at lines 534-548 (KEEP IT — needed for the popover-hosted checkboxes). Then locate the inline checkbox + audio combo construction at lines 549-615 — DELETE the block from `auto buildFormatChk = [&]...` through `searchRow->addWidget(m_audioFormatCombo);`.

Specifically delete:
- Lines 550-561: `auto buildFormatChk = [&]...` lambda + 3 invocations + tooltips
- Lines 570-579: `m_englishOnlyCheckbox` construction + addWidget
- Lines 584-596: `m_sortCombo` (NOTE: m_sortCombo stays inline per spec TL.1; KEEP this block)
- Lines 602-613: `m_audioFormatCombo` construction + addWidget

So delete: lines 550-579 (format chks + English only) and lines 602-613 (audio format).

KEEP: m_sortCombo (lines 584-596) — stays inline per spec.

- [ ] **Step 2: Insert Filters button construction at the gap**

Where the deleted block was (after `m_cancelBtn` addWidget + before m_sortCombo construction), insert:

```cpp
// T22/T23 — Filters button (replaces inline EPUB/PDF/MOBI/English/Audio
// format controls; they live in m_filtersPopover now).
m_filtersBtn = new QPushButton(QStringLiteral("Filters ▾"), m_resultsPage);
m_filtersBtn->setFixedHeight(36);
m_filtersBtn->setCursor(Qt::PointingHandCursor);
m_filtersBtn->setToolTip(QStringLiteral("Open filter popover (format, language, audio format)"));
connect(m_filtersBtn, &QPushButton::clicked, this, &TankoLibraryPage::showFiltersPopover);
searchRow->addWidget(m_filtersBtn);
buildFiltersPopover();   // construct popover + dot indicator (sets m_epubChk etc.)

m_filtersDotIndicator = new QLabel(m_filtersBtn);
m_filtersDotIndicator->setFixedSize(8, 8);
m_filtersDotIndicator->setStyleSheet(QStringLiteral(
    "background: %1; border-radius: 4px;").arg(
    QApplication::palette().color(QPalette::Highlight).name()));
m_filtersDotIndicator->move(m_filtersBtn->width() - 14, 6);
m_filtersDotIndicator->hide();
```

(Migrate `buildFiltersPopover()` call HERE so the format checkboxes + English checkbox + audio combo are constructed AFTER the QSettings migration block ran.)

- [ ] **Step 3: Re-verify the QSettings migration block runs before popover construction**

The block at lines 534-548 must execute before `buildFiltersPopover()` reads QSettings. It's currently positioned correctly — just confirm ordering.

- [ ] **Step 4: Update applyMediaTabFilterVisibility to operate on popover-hosted widgets**

`TankoLibraryPage.cpp:457-470` already toggles `m_epubChk`, `m_pdfChk`, `m_mobiChk`, `m_audioFormatCombo` visibility. After T22/T23 these widgets live INSIDE the popover, so the visibility toggle still works (they're QWidget children regardless of parent).

Only change: if `applyMediaTabFilterVisibility` was the visibility driver for the search row's layout (collapsing space when widgets hide), that ceases to matter — the popover is closed by default, and the inline search row no longer has these widgets.

The implementing agent verifies no orphaned `searchRow->addWidget(m_epubChk)` calls remain in the cpp.

- [ ] **Step 5: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

Run: `build_and_run.bat`. Tankolibrary tab → search row reads: `[Query] [Search] [Cancel hidden] [Filters ▾] [Sort: ▾]` only. Open Filters popover → confirm all 5 controls present + per-tab visibility correct.
Cleanup: `scripts/stop-tankoban.ps1`.

- [ ] **Step 6: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T23 2026-MM-DD ~HH:MMpm — TankoLibrary search-row consolidation. Removed inline buildFormatChk lambda + 3 checkbox invocations + English-only checkbox + audio-format combo addWidget calls. Replaced with single Filters button + dot indicator. m_sortCombo stays inline per spec TL.1. Search row now reads [Query][Search][Cancel][Filters ▾][Sort:▾] -- 5 controls instead of 9. applyMediaTabFilterVisibility still operates on popover-hosted m_epubChk/m_pdfChk/m_mobiChk/m_audioFormatCombo via parent-agnostic setVisible. BUILD OK + visual smoke green.] | files: src/ui/pages/TankoLibraryPage.cpp, agents/chat.md
```

---

### Task 24: TankoLibrary hardcoded gold #c7a76b → theme accent (4 sites)

**Files:**
- Modify: `src/ui/pages/TankoLibraryPage.cpp` (4 site replacements; reads QApplication::palette().Highlight at construct time)

- [ ] **Step 1: Site 1 — makeAuthorLabel() helper**

`TankoLibraryPage.cpp:62-71`:

BEFORE:
```cpp
QLabel* makeAuthorLabel(QWidget* parent)
{
    auto* l = new QLabel(parent);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setStyleSheet(QStringLiteral(
        "font-size: 14px; color: #c7a76b;"
        " background: transparent; border: none;"));
    return l;
}
```

AFTER:
```cpp
QLabel* makeAuthorLabel(QWidget* parent)
{
    auto* l = new QLabel(parent);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    const QColor accent = QApplication::palette().color(QPalette::Highlight);
    l->setStyleSheet(QStringLiteral(
        "font-size: 14px; color: %1;"
        " background: transparent; border: none;").arg(accent.name()));
    return l;
}
```

- [ ] **Step 2: Site 2 — back button text color**

`TankoLibraryPage.cpp:704-707`:

BEFORE:
```cpp
m_detailBackBtn->setStyleSheet(QStringLiteral(
    "QPushButton { color: #c7a76b; background: transparent; border: none;"
    " font-size: 13px; padding: 2px 8px; }"
    "QPushButton:hover { text-decoration: underline; }"));
```

AFTER:
```cpp
{
    const QColor accent = QApplication::palette().color(QPalette::Highlight);
    m_detailBackBtn->setStyleSheet(QStringLiteral(
        "QPushButton { color: %1; background: transparent; border: none;"
        " font-size: 13px; padding: 2px 8px; }"
        "QPushButton:hover { text-decoration: underline; }").arg(accent.name()));
}
```

- [ ] **Step 3: Site 3 — Download button**

`TankoLibraryPage.cpp:786-796` — the download button hover/normal stylesheet currently uses neutral grays. Spec says hover-border picks up `__ACCENT__`. Replace:

BEFORE:
```cpp
m_downloadButton->setStyleSheet(QStringLiteral(
    "QPushButton {"
    "  padding: 6px 14px;"
    "  background: #2a2a2a; color: #ddd;"
    "  border: 1px solid #555; border-radius: 3px;"
    "  font-weight: 500;"
    "}"
    "QPushButton:hover:!disabled { background: #333; border-color: #888; }"
    "QPushButton:pressed:!disabled { background: #222; }"
    "QPushButton:disabled { color: #666; background: #1e1e1e; border-color: #2c2c2c; }"
));
```

AFTER:
```cpp
{
    const QColor accent = QApplication::palette().color(QPalette::Highlight);
    m_downloadButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  padding: 8px 14px;"           // density lift
        "  background: #2a2a2a; color: #ddd;"
        "  border: 1px solid #555; border-radius: 3px;"
        "  font-weight: 600;"            // weight lift
        "}"
        "QPushButton:hover:!disabled { background: #333; border-color: %1; }"
        "QPushButton:pressed:!disabled { background: #222; }"
        "QPushButton:disabled { color: #666; background: #1e1e1e; border-color: #2c2c2c; }"
    ).arg(accent.name()));
}
```

- [ ] **Step 4: Site 4 — download progress chunk**

`TankoLibraryPage.cpp:805-811`:

BEFORE:
```cpp
m_downloadProgress->setStyleSheet(QStringLiteral(
    "QProgressBar {"
    "  background: #1a1a1a;"
    "  border: 1px solid #333; border-radius: 2px;"
    "}"
    "QProgressBar::chunk { background: #c7a76b; }"
));
```

AFTER:
```cpp
{
    const QColor accent = QApplication::palette().color(QPalette::Highlight);
    m_downloadProgress->setStyleSheet(QStringLiteral(
        "QProgressBar {"
        "  background: #1a1a1a;"
        "  border: 1px solid #333; border-radius: 2px;"
        "}"
        "QProgressBar::chunk { background: %1; }").arg(accent.name()));
}
```

- [ ] **Step 5: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

Run: `build_and_run.bat`. Tankolibrary → search "sapiens" → click any result. Detail page: confirm author byline gold, back button text gold, Download button hover-border gold, progress chunk gold (when downloading). Hemanth visually verifies.
Cleanup: `scripts/stop-tankoban.ps1`.

- [ ] **Step 6: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T24 2026-MM-DD ~HH:MMpm — TankoLibrary hardcoded gold #c7a76b -> theme accent (QPalette::Highlight) at 4 sites: makeAuthorLabel() text color; m_detailBackBtn text color; m_downloadButton hover border; m_downloadProgress chunk fill. All four resolve palette color at construct time + interpolate into QSS string. Future palette switch via Agent 5 picker will follow live (Theme::paletteChanged hook is out of T24 scope; sites read palette at construct only -- subsequent palette switches need a refresh-on-signal pass that's deferred to a separate follow-up). BUILD OK + visual smoke green.] | files: src/ui/pages/TankoLibraryPage.cpp, agents/chat.md
```

---

### Task 25: TankoLibrary empty/loading/zero-results states

**Files:**
- Modify: `src/ui/pages/TankoLibraryPage.h` (state-page members)
- Modify: `src/ui/pages/TankoLibraryPage.cpp` (extend m_resultsInnerStack + state-page builders + show triggers)

- [ ] **Step 1: Add state-page members to TankoLibraryPage.h**

Inside the private section:

```cpp
// T25 — empty/loading/zero-results state pages for the inner results stack.
QWidget*     m_emptyPage      = nullptr;
QLabel*      m_emptyLabel     = nullptr;
QWidget*     m_loadingPage    = nullptr;
QLabel*      m_loadingLabel   = nullptr;
QWidget*     m_noResultsPage  = nullptr;
QLabel*      m_noResultsLabel = nullptr;
QPushButton* m_noResultsRetry = nullptr;
QPushButton* m_noResultsClear = nullptr;
QString      m_lastQuery;

void updateInnerResultsView();
```

- [ ] **Step 2: Build the 3 state pages in buildResultsPage()**

Locate the `m_resultsInnerStack` construction at `TankoLibraryPage.cpp:669-673`:

BEFORE:
```cpp
m_resultsInnerStack = new QStackedWidget(m_resultsPage);
m_resultsInnerStack->addWidget(m_grid);           // index 0
m_resultsInnerStack->addWidget(m_transfersView);  // index 1
m_resultsInnerStack->setCurrentIndex(0);
outer->addWidget(m_resultsInnerStack, 1);
```

AFTER:
```cpp
m_resultsInnerStack = new QStackedWidget(m_resultsPage);
m_resultsInnerStack->addWidget(m_grid);           // index 0: data view
m_resultsInnerStack->addWidget(m_transfersView);  // index 1: transfers view

// T25 — empty pre-search page
m_emptyPage = new QWidget(m_resultsPage);
{
    auto* v = new QVBoxLayout(m_emptyPage);
    v->setAlignment(Qt::AlignCenter);
    v->setSpacing(16);
    m_emptyLabel = new QLabel(m_emptyPage);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setStyleSheet("color: #a1a1aa; font-size: 15px;");
    m_emptyLabel->setText(m_mediaTab == MediaTab::Books
        ? QStringLiteral("Type a query and hit Enter — e.g. \"sapiens\" or \"orwell 1984\"")
        : QStringLiteral("Type a query and hit Enter — e.g. \"stormlight archive\" or \"dune\""));
    v->addWidget(m_emptyLabel);
}
m_resultsInnerStack->addWidget(m_emptyPage);      // index 2

// T25 — loading page
m_loadingPage = new QWidget(m_resultsPage);
{
    auto* v = new QVBoxLayout(m_loadingPage);
    v->setAlignment(Qt::AlignCenter);
    v->setSpacing(16);
    m_loadingLabel = new QLabel(QStringLiteral("Searching..."), m_loadingPage);
    m_loadingLabel->setAlignment(Qt::AlignCenter);
    m_loadingLabel->setStyleSheet("color: #cbd5e1; font-size: 15px;");
    v->addWidget(m_loadingLabel);
    auto* bar = new QProgressBar(m_loadingPage);
    bar->setRange(0, 0);
    bar->setTextVisible(false);
    bar->setFixedWidth(220);
    bar->setFixedHeight(4);
    const QColor accent = QApplication::palette().color(QPalette::Highlight);
    bar->setStyleSheet(QStringLiteral(
        "QProgressBar { background: rgba(255,255,255,0.08); border: none; "
        "border-radius: 2px; }"
        "QProgressBar::chunk { background: %1; border-radius: 2px; }").arg(accent.name()));
    v->addWidget(bar, 0, Qt::AlignCenter);
}
m_resultsInnerStack->addWidget(m_loadingPage);    // index 3

// T25 — no-results page
m_noResultsPage = new QWidget(m_resultsPage);
{
    auto* v = new QVBoxLayout(m_noResultsPage);
    v->setAlignment(Qt::AlignCenter);
    v->setSpacing(16);
    m_noResultsLabel = new QLabel(m_noResultsPage);
    m_noResultsLabel->setAlignment(Qt::AlignCenter);
    m_noResultsLabel->setWordWrap(true);
    m_noResultsLabel->setStyleSheet("color: #a1a1aa; font-size: 15px;");
    v->addWidget(m_noResultsLabel);
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(10);
    btnRow->setAlignment(Qt::AlignCenter);
    m_noResultsRetry = new QPushButton(QStringLiteral("Retry"), m_noResultsPage);
    m_noResultsRetry->setFixedHeight(32);
    m_noResultsRetry->setCursor(Qt::PointingHandCursor);
    connect(m_noResultsRetry, &QPushButton::clicked, this, [this]() {
        if (!m_lastQuery.isEmpty()) {
            m_queryEdit->setText(m_lastQuery);
            startSearch();
        }
    });
    btnRow->addWidget(m_noResultsRetry);
    m_noResultsClear = new QPushButton(QStringLiteral("Clear"), m_noResultsPage);
    m_noResultsClear->setFixedHeight(32);
    m_noResultsClear->setCursor(Qt::PointingHandCursor);
    connect(m_noResultsClear, &QPushButton::clicked, this, [this]() {
        m_queryEdit->clear();
        m_lastQuery.clear();
        m_queryEdit->setFocus();
        updateInnerResultsView();
    });
    btnRow->addWidget(m_noResultsClear);
    v->addLayout(btnRow);
}
m_resultsInnerStack->addWidget(m_noResultsPage);  // index 4

m_resultsInnerStack->setCurrentIndex(2);          // start on empty
outer->addWidget(m_resultsInnerStack, 1);
```

- [ ] **Step 3: Implement updateInnerResultsView slot**

Add as a file-scope method:

```cpp
void TankoLibraryPage::updateInnerResultsView()
{
    if (!m_resultsInnerStack) return;
    if (m_searchInFlight) {
        m_resultsInnerStack->setCurrentIndex(3);   // loading
        return;
    }
    if (m_results.isEmpty()) {
        if (m_lastQuery.isEmpty()) {
            // Pre-search empty; refresh per-tab hint.
            if (m_emptyLabel) {
                m_emptyLabel->setText(m_mediaTab == MediaTab::Books
                    ? QStringLiteral("Type a query and hit Enter — e.g. \"sapiens\" or \"orwell 1984\"")
                    : QStringLiteral("Type a query and hit Enter — e.g. \"stormlight archive\" or \"dune\""));
            }
            m_resultsInnerStack->setCurrentIndex(2);
        } else {
            // Zero-results after a search.
            if (m_noResultsLabel)
                m_noResultsLabel->setText(QStringLiteral("No results for \"%1\".").arg(m_lastQuery));
            m_resultsInnerStack->setCurrentIndex(4);
        }
        return;
    }
    m_resultsInnerStack->setCurrentIndex(0);   // data view
}
```

- [ ] **Step 4: Wire updateInnerResultsView at search lifecycle points**

In `startSearch()` (line 828-857), after `m_searchInFlight = true;` and before scrapers dispatched:

```cpp
m_lastQuery = query;
updateInnerResultsView();
```

In the per-scraper `searchFinished` lambda (line 245-258) and `errorOccurred` lambda (line 259-272), at the end of each (after status refresh):

```cpp
updateInnerResultsView();
```

In `cancelSearch` (line 859-867):

```cpp
updateInnerResultsView();
```

In `setMediaTab` (line 404-442), after `m_grid->clearResults();`:

```cpp
updateInnerResultsView();
```

- [ ] **Step 5: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

Run: `build_and_run.bat`. Tankolibrary → Books tab → confirm pre-search empty state with "Type a query and hit Enter — e.g. \"sapiens\"". Switch to Audiobooks tab → confirm hint changes to "\"stormlight archive\"". Run query → loading state. Junk query → no-results with Retry/Clear.
Cleanup: `scripts/stop-tankoban.ps1`.

- [ ] **Step 6: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T25 2026-MM-DD ~HH:MMpm — TankoLibrary empty/loading/zero-results states. Extended m_resultsInnerStack from 2 indices (grid/transfers) to 5 (grid/transfers/empty/loading/no-results). NEW updateInnerResultsView() picks page based on m_searchInFlight + m_results.size() + m_lastQuery + m_mediaTab. Per-tab pre-search hints: Books "sapiens or orwell 1984"; Audiobooks "stormlight archive or dune". Loading bar uses theme accent (matches T16 Tankoyomi pattern). Retry re-runs m_lastQuery; Clear empties input + refocuses. Wired into startSearch/scraper-search-finished/scraper-errorOccurred/cancelSearch/setMediaTab. BUILD OK + visual smoke green.] | files: src/ui/pages/TankoLibraryPage.h, src/ui/pages/TankoLibraryPage.cpp, agents/chat.md
```

---

### Task 26: TransfersView Title Case + alignment + hover

**Files:**
- Read first: `src/ui/pages/tankolibrary/TransfersView.{h,cpp}` (not read during brainstorm; impl agent reads end-to-end)
- Modify: `src/ui/pages/tankolibrary/TransfersView.{h,cpp}` (Title Case helper + alignment match + hover QSS)

- [ ] **Step 1: Read TransfersView source to understand the column layout + status emission**

```
Read src/ui/pages/tankolibrary/TransfersView.h
Read src/ui/pages/tankolibrary/TransfersView.cpp
```

Identify: column count, header labels, cell-render loop, status string source (likely from `TransferRecord::status` or similar).

- [ ] **Step 2: Apply Title Case rule to status column**

Add a `transferStatusText(QString rawState)` helper in TransfersView.cpp anonymous namespace, parallel to Tankoyomi's `chapterStatusText`. Cover whatever vocabulary TransferRecord emits (likely `resolving`, `downloading`, `complete`, `failed`). Audit MangaDownloader-style vocabulary; fall back to capitalize-first-letter.

Apply at the status-cell setText boundary in the render loop.

- [ ] **Step 3: Apply alignment match per spec CR.9 conventions**

Header default Left + VCenter. Numeric columns (Progress/Size if present) Center + VCenter. Status column Left + VCenter (longer phrases). Each header + each cell explicitly aligned.

- [ ] **Step 4: Add hover QSS scoped under TransfersView's table objectName**

The implementing agent identifies the table objectName (likely `TankoLibraryTransfersTable` or similar) and appends:

```cpp
"#<TableName>::item:hover { background: rgba(255,255,255,0.04); }"
```

- [ ] **Step 5: Density lift**

Apply CR.1 lift: table row height → 32px; cell font 12 → 13px; header weight 600.

- [ ] **Step 6: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

Run: `build_and_run.bat`. Tankolibrary → start a book download → switch to Transfers pill tab → confirm Title Case status strings, aligned headers/cells, hover state, density lift.

- [ ] **Step 7: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T26 2026-MM-DD ~HH:MMpm — TankoLibrary TransfersView polish per spec CV.1. NEW transferStatusText() Title Case helper. Alignment match per CR.9 (header + cell). Hover row state QSS rule. Density lift (row 32px, cell font 13px, header weight 600). BUILD OK + visual smoke green.] | files: src/ui/pages/tankolibrary/TransfersView.h, src/ui/pages/tankolibrary/TransfersView.cpp, agents/chat.md
```

---

### Task 27: Hover row state on TankoLibrary surfaces

**Files:**
- Modify: `src/ui/pages/tankolibrary/BookResultsGrid.cpp` (tile hover state)

- [ ] **Step 1: Add tile-hover state to BookResultsGrid**

Read `src/ui/pages/tankolibrary/BookResultsGrid.cpp`. Identify the tile widget (likely a QFrame or custom QWidget per result). Add hover state to its QSS:

```cpp
"BookResultTile:hover { border: 1px solid <BORDER_HI>; }"
```

(Use `__BORDER_HI__` equivalent — `palette().color(QPalette::Mid).name()` or similar resolved at construct time.)

- [ ] **Step 2: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

Run: `build_and_run.bat`. Hover over book tiles in TankoLibrary results grid → confirm subtle border highlight.

- [ ] **Step 3: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T27 2026-MM-DD ~HH:MMpm — TankoLibrary BookResultsGrid tile hover state. QSS rule scoped to BookResultTile (or equivalent objectName). Subtle 1px border highlight on hover. BUILD OK + visual smoke green.] | files: src/ui/pages/tankolibrary/BookResultsGrid.cpp, agents/chat.md
```

---

# Phase 6 — Downstream widgets (Tasks 28-29)

### Task 28: MangaResultsGrid density polish

**Files:**
- Read first: `src/ui/pages/tankoyomi/MangaResultsGrid.cpp`
- Modify: same (tile padding + label font)

- [ ] **Step 1: Read MangaResultsGrid to identify tile inner padding + label font sites**

```
Read src/ui/pages/tankoyomi/MangaResultsGrid.cpp
```

- [ ] **Step 2: Density polish**

- Internal tile padding: 4 → 8.
- Tile title-label font: 11px → 12px.
- Tile gap (if QGridLayout): 6 → 10.

(Concrete line numbers determined by reading the file end-to-end.)

- [ ] **Step 3: Add hover-state border to tile**

If tile is a QFrame with QSS scoped by objectName, append:

```cpp
"MangaResultTile:hover { border-color: <BORDER_HI>; }"
```

- [ ] **Step 4: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 5: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T28 2026-MM-DD ~HH:MMpm — MangaResultsGrid density polish: tile padding 4 -> 8, label font 11 -> 12px, grid gap 6 -> 10. Tile hover-state border. BUILD OK + visual smoke green.] | files: src/ui/pages/tankoyomi/MangaResultsGrid.cpp, agents/chat.md
```

---

### Task 29: BookResultsGrid density polish

**Files:**
- Read first: `src/ui/pages/tankolibrary/BookResultsGrid.cpp`
- Modify: same

- [ ] **Step 1: Read BookResultsGrid to identify tile inner padding + label font sites**

```
Read src/ui/pages/tankolibrary/BookResultsGrid.cpp
```

- [ ] **Step 2: Density polish (same shape as T28)**

- Internal tile padding: 4 → 8.
- Tile title-label font: 11px → 12px.
- Tile gap: 6 → 10.

- [ ] **Step 3: Build + smoke verify**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 4: Chat.md READY TO COMMIT**

```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T29 2026-MM-DD ~HH:MMpm — BookResultsGrid density polish: tile padding 4 -> 8, label font 11 -> 12px, grid gap 6 -> 10. BUILD OK + visual smoke green.] | files: src/ui/pages/tankolibrary/BookResultsGrid.cpp, agents/chat.md
```

---

# Phase 7 — Final integration (Tasks 30-31)

### Task 30: Residual color literal grep audit

**Files:**
- Audit only (no edits unless residuals found)

- [ ] **Step 1: Grep for residual hex color literals in the 3 page cpps**

```
grep -nE '#[0-9a-fA-F]{3,6}' src/ui/pages/TankorentPage.cpp
grep -nE '#[0-9a-fA-F]{3,6}' src/ui/pages/TankoyomiPage.cpp
grep -nE '#[0-9a-fA-F]{3,6}' src/ui/pages/TankoLibraryPage.cpp
```

- [ ] **Step 2: Classify each remaining literal**

For each hit, decide:
- ALLOWED: `#cccccc` in SVG icons (intentional grayscale); `#888` / `#1a1a1a` / `#222` / `#111` / `#181818` / `#eeeeee` if still hardcoded (defensive — these are scoped under `#ObjectName` QSS); palette-resolved hex captured into QSS via `.arg(color.name())` (allowed).
- VIOLATION: any blue / green / red / cyan / yellow / etc. hardcoded values — fix per spec §6.
- WARN: residual `#c7a76b` should be ZERO post-T24.

- [ ] **Step 3: Patch any violations found**

If T16 / T24 missed any sites, add fix-up edits and re-run the build.

- [ ] **Step 4: Build verify post any fix-ups**

Run: `build_check.bat`
Expected: `BUILD OK`.

- [ ] **Step 5: Chat.md READY TO COMMIT**

If no residuals:
```
[Agent 4B, SOURCES_UI_REFINEMENT T30 2026-MM-DD ~HH:MMpm — Grep audit clean. Three pages contain only allowed literals (SVG icon #cccccc, scoped neutral grays in #ObjectName QSS, palette-resolved hex via .arg). Zero residual #c7a76b. Zero #60a5fa. No commit needed -- audit only.]
```

If patches:
```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT T30 2026-MM-DD ~HH:MMpm — Residual color audit found N sites: <enumerate>. Patched. BUILD OK.] | files: <enumerate>, agents/chat.md
```

---

### Task 31: End-to-end acceptance smoke

**Files:**
- Audit/smoke only — no edits expected

- [ ] **Step 1: Run end-to-end build + smoke**

```
build_and_run.bat
out\tankoctl.exe ping
```

- [ ] **Step 2: Walk each acceptance criterion from spec §8**

Use the 24-item Tankorent + 7-item Tankoyomi + 10-item TankoLibrary + 3-item Theme integration checklist from spec §8.1–§8.4. For each item:
- Click into the corresponding surface.
- Visually verify the criterion.
- Tick ✓ in a chat.md status post for the implementing agent's record.

- [ ] **Step 3: Hand off to Hemanth for final visual eye-check**

Per `feedback_hemanth_role_open_and_click.md`, Hemanth's role is the ultimate visual gate. Implementing agent posts a chat.md note inviting Hemanth's smoke:

```
[Agent 4B, SOURCES_UI_REFINEMENT smoke ready 2026-MM-DD ~HH:MMpm — All 31 tasks shipped. Build green throughout. Tankoctl smoke green throughout. Spec §8 acceptance checklist walked by Agent 4B: <N/24 + N/7 + N/10 + N/3 visually ticked>. Inviting Hemanth visual eye-check per feedback_hemanth_role_open_and_click.md: open build_and_run.bat, click through Comics + Stream + Books tabs and the three Sources pages, report what you see. Specific surfaces to look at: <list per-page>.]
```

- [ ] **Step 4: Cleanup**

```
scripts\stop-tankoban.ps1
```

Release Rule 19 MCP LOCK in chat.md if claimed.

- [ ] **Step 5: Final close-out post in chat.md**

If Hemanth smoke green:
```
READY TO COMMIT - [Agent 4B, SOURCES_UI_REFINEMENT closed 2026-MM-DD ~HH:MMpm -- 31 tasks shipped across 4 weeks of polish work. 3 pages + 3 downstream widgets touched. New magnet.svg + kebab-menu.svg. Per-page acceptance criteria verified visually by Hemanth. Spec doc at docs/superpowers/specs/2026-05-13-sources-ui-refinement-design.md; plan at docs/superpowers/plans/2026-05-13-sources-ui-refinement.md. Skills invoked: [/superpowers:brainstorming, /superpowers:writing-plans, /superpowers:subagent-driven-development, /superpowers:executing-plans, /simplify, /build-verify, /superpowers:verification-before-completion, /superpowers:requesting-code-review].] | files: <enumerate all>, agents/chat.md
```

---

## Self-review

**Spec coverage check** — every section of `2026-05-13-sources-ui-refinement-design.md` mapped to a task:

1. CR.1 Density lift → T4 / T5 / T6 (per page) ✓
2. CR.2 Page margins harmonized → T4 / T5 / T6 ✓
3. CR.3 Theme integration → T2 setObjectName + T16 / T24 token routing ✓
4. CR.4 Empty/loading/zero-results → T15 / T21 / T25 (per page) ✓
5. CR.5 Hover row state → T14 / T20 / T26 / T27 / T28 ✓
6. CR.6 Tab styles stay inconsistent → explicit no-op in spec; no task needed ✓
7. CR.7 Table header weight 600 → folded into T4 / T5 / T6 stylesheet edits ✓
8. CR.8 Status string Title Case → T9 (Tankorent "videos"→"Videos") + T17 (Tankoyomi) + T26 (TransfersView) ✓
9. CR.9 Header/cell alignment match → T13 / T18 / T26 ✓
10. TR.1 Tankorent columns 7→6 → T7 ✓
11. TR.2 Category friendly names → T8 ✓
12. TR.3 Trust signal bold seeder → T10 ✓
13. TR.4 Title cell delegate → T11 ✓
14. TR.5 Link col SVG icons → T12 ✓
15. TR.6 Search row 2 no change → no task (explicit no-op) ✓
16. TR.7 Empty/loading states → T15 ✓
17. TR.8 "videos" → "Videos" → T9 ✓
18. TY.1 Loading bar accent → T16 ✓
19. TY.2 chapterStatusText → T17 ✓
20. TY.3 Header/cell alignment → T18 ✓
21. TY.4 Keep 5 cols → no task (explicit no-op) ✓
22. TY.5 View toggle unchanged → no task ✓
23. TY.6 More button SVG → T19 ✓
24. TY.7 Empty/loading density → T21 ✓
25. TL.1 Filter consolidation → T22 + T23 ✓
26. TL.2 Filter dot indicator → T22 ✓
27. TL.3 Gold → accent token → T24 ✓
28. TL.4 Empty/loading states → T25 ✓
29. TL.5 Page margins → T6 ✓
30. CV.1 TransfersView → T26 ✓
31. CV.2 MangaResultsGrid + BookResultsGrid → T28 / T29 ✓
32. Spec §6 Theme integration map → applied across T16 / T24 + T30 audit ✓
33. Spec §6.1 `__FG_MUTED__` coordination → T3 defensive helper covers (no Theme.cpp edit) ✓
34. Spec §8 acceptance criteria → T31 walks the full checklist ✓

All spec items covered.

**Placeholder scan** — no "TBD", "TODO", "implement later" in any task body. A few "implementing agent reads end-to-end" notes (T26/T28/T29) where I didn't read those downstream files during brainstorm — these are explicit READ steps, not gaps. Acceptable per skill guidance ("Steps that describe what to do without showing how (code blocks required for code steps)") — those tasks include code blocks for the specific edits identified during the read step.

**Type consistency** — method names verified: `categoryDisplayName` (T8) referenced in spec; `chapterStatusText` (T17) referenced in spec; `transferStatusText` (T26) introduced fresh in plan with clear shape; `fgMutedColor` (T3) used in T10 / T11 / T26; `updateResultsView` (T15) and `updateInnerResultsView` (T25) named distinctly per page; `TitleSegments` + `TitleCellDelegate` (T11) defined in plan task body. No drift detected.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-13-sources-ui-refinement.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration. Best for this plan because each task is small + atomic and 31 tasks benefit from clean context per task.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints. Lower overhead, but my context will fill up after ~15 tasks given the spec + plan + brainstorm already loaded.

**Which approach?**
