# Tankoyomi Series Page UI Fix Plan

> **⚠ SUPERSEDED 2026-05-14** by the COMICS_TANKOYOMI_STREAM_MERGER vision. Tankoyomi dissolves into Comics mode; the series page becomes a Stream-show-view-style surface inside the Comics library, not a stand-alone Tankoyomi screen. Tankoyomi ownership transferred from Agent 4B to Agent 1 the same day. Plan kept for historical context — the alignment + glyph + author-placeholder fixes shipped here may inform the merger arc's brainstorm-md. See `CLAUDE.md` dashboard stanza + `agents/GOVERNANCE.md` Rule 20 (gov-v4, revised same-day 2026-05-14: Codex reviews AND EXPANDS Agent 1's brainstorm-md in place; co-authorship, not audit; one pass total).

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring the `MangaDetailView` series page (shipped end-to-end in the just-closed TANKOYOMI_MIHON_OVERHAUL arc) up to visual parity with the polished `StreamDetailView` episode-list surface — fix the lazy back affordance, broken download caret glyph, misaligned chapter-table headers vs rows, dead `#` and `Date` columns, and the orphan em-dash author placeholder.

**Architecture:** All changes are scoped to `src/ui/pages/tankoyomi/MangaDetailView.{h,cpp}` (one self-contained widget, ~696 LOC pre-fix). The visual contract is **`StreamDetailView`** — the existing show-detail surface after STREAM_DOWNLOADS_NETFLIX_OVERHAUL P3 ships. Every visual decision in this plan cross-references a specific `StreamDetailView.cpp:line` for the sibling pattern. The engine layer (`MangaDownloader`) is untouched — the existing per-chapter state machine (queued / downloading / completed / error / cancelled) + `ChapterDownloadIndicator` widget (custom-painted, 5-state, animated progress arc from B.1–B.3) stay as-is. We are only restructuring the table's column model + headers + typography around them.

**Tech Stack:** C++ / Qt 6 Widgets + QSS. Build via `build_check.bat` (compile gate) + MCP smoke for visual verification per project convention. No new external deps. No new SVG files needed — all icons referenced (`download-arrow`, `pause-circle`, `check`, `play-circle`, `retry-arrow`) already exist at `resources/icons/`.

**Reference spec (informal):** This prompt is post-overhaul polish, not a brainstormed spec. The "spec" is Hemanth's verbatim §1 below + the visual anchor pointers in §2.

---

## §1 Goal — Hemanth verbatim

> "The Tankoyomi series page UI is unappealing. The chapter list has columns (#, Chapter, Date) where none of the data matches the headers — no index under #, the Chapter header is centered but the rows are left-aligned, and there are no dates beneath Date, just the download buttons. I don't need index and date columns. The `<back` text-affordance is lazy — use a proper back button. `Download v` has a broken glyph. Look at Stream mode's TV show detail page as the visual anchor for what a polished list view with download items should look like. Write a `/superpowers:writing-plans` plan to fix the Tankoyomi series page; I'll fire `executing-plans` separately."

---

## §2 Visual anchor — `StreamDetailView` references

Every visual choice in this plan mirrors a specific line in `src/ui/pages/stream/StreamDetailView.cpp`. Reading the cited line ranges first is mandatory before executing the corresponding task.

### Back button shape (Task 1)

- `StreamDetailView.cpp:310-318` — `QPushButton("← Back", this)` with `setObjectName("SidebarAction")`, `setFixedHeight(30)`, `setCursor(Qt::PointingHandCursor)`, and the explicit `setStyleSheet`:

  ```cpp
  "#SidebarAction { background: transparent; border: none; color: rgba(255,255,255,0.7);"
  "  font-size: 13px; padding: 0 8px; }"
  "#SidebarAction:hover { color: #fff; }"
  ```

  NOT an icon-only button using `chevron_left.svg` — the sibling shape is Unicode-arrow + "Back" text. Mirror exactly.

### Title typography (Task 2)

- `StreamDetailView.cpp:398-401` — `QLabel` with `setWordWrap(true)` + `setStyleSheet("color: #e0e0e0; font-size: 16px; font-weight: bold;")`.

### Inline metadata line (Task 2)

- `StreamDetailView.cpp:410-419` — `m_metaLine` with QSS:

  ```cpp
  "QLabel#StreamDetailMetaLine {"
  "  background: transparent;"
  "  border: none;"
  "  color: rgba(255,255,255,0.62);"
  "  font-size: 12px;"
  "  font-weight: 400;"
  ```

  Single inline metadata string (dot-separator joined) — NOT a multi-row layout.

### Chapter / episode table column model (Tasks 3 + 4)

- `StreamDetailView.cpp:53-60` — anonymous-namespace column constants:

  ```cpp
  constexpr int kColCheckbox  = 0;
  constexpr int kColEpisode   = 1;
  constexpr int kColThumb     = 2;
  constexpr int kColTitle     = 3;
  constexpr int kColProgress  = 4;
  constexpr int kColStatus    = 5;
  constexpr int kColAction    = 6;
  constexpr int kColumnCount  = 7;
  ```

- `StreamDetailView.cpp:548-585` — full table construction. Header labels with empty strings for non-title columns. `setSectionResizeMode(kColTitle, Stretch)` + `Fixed` everywhere else. Column widths: checkbox=32, episode=36, thumb=76, progress=80, status=60, action=36. `verticalHeader()->setDefaultSectionSize(64)`. `setShowGrid(false)` + `setAlternatingRowColors(true)` + `setSortingEnabled(false)` + `verticalHeader()->hide()`.

- `StreamDetailView.cpp:579-585` — table QSS:

  ```cpp
  "QTableWidget { background: transparent; border: none; color: #ccc;"
  "  alternate-background-color: rgba(255,255,255,0.03); }"
  "QTableWidget::item { padding: 4px; }"
  "QTableWidget::item:selected { background: rgba(255,255,255,0.08); }"
  "QHeaderView::section { background: rgba(255,255,255,0.05); color: rgba(255,255,255,0.5);"
  "  border: none; font-size: 11px; padding: 4px; }"
  ```

### Title cell stacked layout (Task 4)

- `StreamDetailView.cpp:920-950` — `QVBoxLayout` inside cell widget with primary title label (`color: #e0e0e0; font-size: 12px; font-weight: 500;`) and optional secondary overview label (`color: rgba(255,255,255,0.45); font-size: 10px; font-style: italic;`) with 2-line clamp via `QFontMetrics::lineSpacing() * 2`. For Tankoyomi we adapt this to title + optional release-date inline subtitle.

### Action icon button (Task 4)

- `StreamDetailView.cpp:870-885` — `QPushButton flat, setFixedSize(24, 24), setIconSize(QSize(16, 16)), setCursor(PointingHandCursor)`, click-routed via `mapToGlobal(rect().center())`. For Tankoyomi we keep `ChapterDownloadIndicator` (from B.1–B.3, 28×28 custom-painted) as the action widget — Mihon's design intent has the animated progress arc, which we already shipped. We're only mirroring the **column geometry**, not replacing the widget.

### Action icon state map (informational — already shipped)

- `StreamDetailView.cpp:98-107` — `actionIconForState` SVG-per-state mapping. Tankoyomi's equivalent is `ChapterDownloadIndicator::setState` per state — already wired in `MangaDetailView::deriveChapterState` (cpp:432–466 post-overhaul). No change needed.

### Row context menu (informational — already shipped)

- `StreamDetailView.cpp` — `onEpisodeContextMenu`. Tankoyomi's equivalent is `MangaDetailView::showChapterContextMenu` (shipped in F.2). No change needed.

---

## §3 Per-fix tasks (overview)

Six tasks. All single-file edits on `MangaDetailView.{h,cpp}`. Each ends with `build_check.bat` → `BUILD OK` + commit per the Tankoban convention. Final task includes MCP smoke screenshot evidence.

1. **Task 1 — Back button mirror.** Replace `m_backBtn` text `< back` + bare construction with `← Back` SidebarAction QSS pattern from `StreamDetailView.cpp:310-318`.
2. **Task 2 — Hero card typography parity.** Author em-dash placeholder dropped (hide row when empty). Title gets explicit QSS matching `StreamDetailView.cpp:400`. Meta line collapses to single inline string (status · source · chapter-count) per `StreamDetailView.cpp:413-419` instead of three stacked rows.
3. **Task 3 — Download-dropdown glyph fix.** Replace the literal `"Download v"` text label (the source of the broken-caret rendering — `v` is being rendered as a literal lowercase v, not a chevron) with a proper Unicode down-arrow `▾` ("▾") OR remove the indicator entirely and rely on `QToolButton::InstantPopup` indicator semantics. Add QSS parity with sibling buttons.
4. **Task 4 — Chapter table column model.** Drop the `#` column. Drop the `Date` column. New model: 2 columns — `kColTitle = 0` (stretch, left-align) + `kColAction = 1` (fixed 36px, right-aligned). Apply anonymous-namespace constants pattern from `StreamDetailView.cpp:53-60`. Apply table-level QSS from `StreamDetailView.cpp:579-585`. Row height 56px fixed (vs Stream's 64px — Tankoyomi has no thumbnail column so shorter rows fit).
5. **Task 5 — `renderChapters` refactor.** Title cell becomes a `QVBoxLayout` stacked widget: primary chapter name (`color: #e0e0e0; font-size: 12px; font-weight: 500;`) + optional inline date subtitle when `dateUpload > 0` (`color: rgba(255,255,255,0.45); font-size: 10px;`). The redundant `"Ch 2015.0  Descender (2015-) #32"` row prefix becomes just the chapter name (e.g. `"Descender (2015-) #32"` or whatever the scraper already populates in `ChapterInfo::name`).
6. **Task 6 — Build verify + MCP smoke.** Single end-to-end smoke pass: launch via `build_and_run.bat`, navigate to Tankoyomi → search "Descender" → open detail → screenshot. Verify back button reads `← Back`, title is bold 16px, no em-dash row, no `#` / `Date` columns, chapter rows show clean chapter names + per-row indicator. Save evidence to `agents/audits/`.

---

## §4 Files-expected list

### Modified (one file across all 6 tasks)

- `src/ui/pages/tankoyomi/MangaDetailView.cpp` — buildUI + show() + renderChapters bodies.
- `src/ui/pages/tankoyomi/MangaDetailView.h` — IF Task 4 introduces new private const members (the `kCol*` constants live in anonymous namespace in cpp, so likely no header change).

### Read-only references (do not modify)

- `src/ui/pages/stream/StreamDetailView.cpp` — visual anchor. Read the cited line ranges in §2.
- `src/ui/pages/stream/StreamDetailView.h` — only if header-level patterns need lifting.
- `src/core/manga/MangaResult.h` — `ChapterInfo` field schema (`id`, `name`, `chapterNumber`, `dateUpload`, `url`, `source`). Already in scope from prior overhaul work.
- `resources/icons/` — confirm existence of `chevron_left.svg` (NOT used in this plan — anchor uses Unicode arrow instead).

### NOT touched

- `src/core/manga/MangaDownloader.{h,cpp}` — engine layer untouched. The chapter state machine is fine; this fix is presentation-only.
- `src/ui/pages/tankoyomi/ChapterDownloadIndicator.{h,cpp}` — keep B.1–B.3's animated indicator widget. We're only changing which column it lives in.
- `src/ui/pages/tankoyomi/ChapterRangeDialog.{h,cpp}` — irrelevant to series-page visual fix.
- `src/ui/pages/tankoyomi/TransferGroupCard.{h,cpp}` — Transfers tab surface, not the series-page surface.
- `src/ui/pages/TankoyomiPage.{h,cpp}` — outer page chrome, not the series-page surface.
- Theme.cpp / palette work — out of scope per the prompt.

---

## §5 Sub-agent dispatch ladder

Each task is one subagent dispatch (implementer) followed by spec-compliance review + code-quality review per the project's subagent-driven cadence. Six tasks, ~18 subagent invocations total at full rigor; sibling-task review cycles can be bundled per `feedback_decision_authority.md` Rule-14 coordination calls.

Tasks are mostly independent within `MangaDetailView` — Task 1 (back button) and Task 2 (hero) don't share LOC. Task 3 (download dropdown) is independent from 4 + 5 (chapter table). Tasks 4 and 5 are sequential — 4 sets up the column model, 5 fills it.

Suggested order: 1 → 2 → 3 → 4 → 5 → 6. Task 6 is verification-only.

---

## §6 Per-task implementation

### Task 1 — Back button mirror

**Files:**
- Modify: `src/ui/pages/tankoyomi/MangaDetailView.cpp:47-50` — `m_backBtn` construction

- [ ] **Step 1: Replace `m_backBtn` construction block**

Find the existing 4-line block:

```cpp
m_backBtn = new QPushButton(tr("< back"), this);
m_backBtn->setObjectName("MangaDetailBackBtn");
connect(m_backBtn, &QPushButton::clicked,
        this, &MangaDetailView::backRequested);
```

Replace with the `StreamDetailView.cpp:310-318` mirror:

```cpp
m_backBtn = new QPushButton(QString::fromUtf8("\xE2\x86\x90 Back"), this);
m_backBtn->setObjectName("SidebarAction");
m_backBtn->setFixedHeight(30);
m_backBtn->setCursor(Qt::PointingHandCursor);
m_backBtn->setStyleSheet(
    "#SidebarAction { background: transparent; border: none; color: rgba(255,255,255,0.7);"
    "  font-size: 13px; padding: 0 8px; }"
    "#SidebarAction:hover { color: #fff; }");
connect(m_backBtn, &QPushButton::clicked,
        this, &MangaDetailView::backRequested);
```

Note the object name change `"MangaDetailBackBtn"` → `"SidebarAction"`. This is intentional — `SidebarAction` is the established sibling style used by `StreamDetailView`; reusing the QSS rule keeps theming consistent. If `MangaDetailBackBtn` is referenced anywhere else (e.g. `Theme.cpp`), grep first and remove that rule.

Search for any remaining QSS rule scoped to `#MangaDetailBackBtn`:

```bash
git grep -n "MangaDetailBackBtn"
```

Expected: zero hits after this task (it was only set in `buildUI`).

- [ ] **Step 2: Run `build_check.bat`**

Expected: `BUILD OK`. If `LNK1168`, kill any running Tankoban.exe per Rule 1.

- [ ] **Step 3: Commit**

```bash
git add src/ui/pages/tankoyomi/MangaDetailView.cpp
git commit -m "fix(tankoyomi): MangaDetailView back button mirrors StreamDetailView shape"
```

- [ ] **Step 4: Append RTC to chat.md (unstaged)**

```
READY TO COMMIT - [Agent 4B, TANKOYOMI_SERIES_PAGE_FIX Task 1 2026-05-14 ~HH:MMam — back button reshaped to mirror StreamDetailView.cpp:310-318. Text "< back" → Unicode "← Back" (U+2190 leftward arrow); objectName "MangaDetailBackBtn" → "SidebarAction" (sibling QSS); setFixedHeight(30) + setCursor(PointingHand) + transparent-bg hover-flips-to-white setStyleSheet block applied. backRequested signal connection preserved. BUILD OK. Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion]] | files: src/ui/pages/tankoyomi/MangaDetailView.cpp, agents/chat.md
```

---

### Task 2 — Hero card typography parity

**Files:**
- Modify: `src/ui/pages/tankoyomi/MangaDetailView.cpp:107-159` — heroRow / metaCol construction
- Modify: `src/ui/pages/tankoyomi/MangaDetailView.cpp:254-260` — `show()` populates author/status

- [ ] **Step 1: Apply title QSS in buildUI**

Locate the `m_titleLabel` construction (currently in the topRow at cpp:51-52):

```cpp
m_titleLabel = new QLabel(this);
m_titleLabel->setObjectName("MangaDetailTitle");
```

Add the typography QSS matching `StreamDetailView.cpp:400`:

```cpp
m_titleLabel = new QLabel(this);
m_titleLabel->setObjectName("MangaDetailTitle");
m_titleLabel->setWordWrap(true);
m_titleLabel->setStyleSheet("color: #e0e0e0; font-size: 16px; font-weight: bold;");
```

- [ ] **Step 2: Collapse the meta column from 3 rows to 1 inline line**

Locate `metaCol` construction (currently at cpp:115-126):

```cpp
auto* metaCol = new QVBoxLayout();
metaCol->setSpacing(4);
m_authorLabel = new QLabel(this);
m_authorLabel->setObjectName("MangaDetailAuthor");
m_statusLabel = new QLabel(this);
m_statusLabel->setObjectName("MangaDetailStatus");
m_chapterCount = new QLabel(this);
m_chapterCount->setObjectName("MangaDetailChapterCount");
metaCol->addWidget(m_authorLabel);
metaCol->addWidget(m_statusLabel);
metaCol->addWidget(m_chapterCount);
metaCol->addStretch();
```

Replace with the single inline metaLine matching `StreamDetailView.cpp:410-419`:

```cpp
auto* metaCol = new QVBoxLayout();
metaCol->setSpacing(6);

m_metaLine = new QLabel(this);
m_metaLine->setObjectName("MangaDetailMetaLine");
m_metaLine->setWordWrap(true);
m_metaLine->setStyleSheet(
    "QLabel#MangaDetailMetaLine {"
    "  background: transparent;"
    "  border: none;"
    "  color: rgba(255,255,255,0.62);"
    "  font-size: 12px;"
    "  font-weight: 400;"
    "}");
metaCol->addWidget(m_metaLine);
metaCol->addStretch();
```

Remove the `m_authorLabel`, `m_statusLabel`, `m_chapterCount` member declarations from `MangaDetailView.h` and replace with a single `QLabel* m_metaLine = nullptr;`. Search for any other references in the cpp:

```bash
git grep -n "m_authorLabel\|m_statusLabel\|m_chapterCount" src/ui/pages/tankoyomi/MangaDetailView.cpp
```

Three call sites are expected: `show()` (sets author/status/chapterCount), `onChaptersReady` (updates chapterCount). Both need rewriting in Step 3.

- [ ] **Step 3: Rewrite `show()` to populate the single metaLine**

Locate the existing block in `MangaDetailView::show()` (cpp:254-270):

```cpp
m_titleLabel->setText(result.title);
m_authorLabel->setText(result.author.isEmpty() ? tr("—") : result.author);

const QString status = result.status.isEmpty() ? tr("Unknown") : result.status;
const QString srcLabel = mangaSourceDisplayName(result.source);
m_statusLabel->setText(QStringLiteral("%1 · %2").arg(status, srcLabel));

// ... cover loading ...

// Chapter count placeholder until scraper returns; C.3 fills in
m_chapterCount->setText(tr("Loading chapters..."));
```

Replace with:

```cpp
m_titleLabel->setText(result.title);

// Build inline meta string: [author · ]status · source[ · N chapters][ · M downloaded]
// Author + status fall through (em-dash / "Unknown") only when no real value.
QStringList parts;
if (!result.author.isEmpty()) {
    parts.append(result.author);
}
const QString status = result.status.isEmpty() ? tr("Unknown") : result.status;
parts.append(status);
parts.append(mangaSourceDisplayName(result.source));
parts.append(tr("Loading chapters..."));   // placeholder; onChaptersReady rewrites
m_metaLine->setText(parts.join(QStringLiteral(" · ")));

// ... cover loading unchanged ...
```

(Note: the dot-separator `·` is U+00B7 MIDDLE DOT, matching what `StreamDetailView` already uses.)

- [ ] **Step 4: Rewrite `onChaptersReady()` to update the metaLine**

Locate the existing block in `onChaptersReady` (cpp:320-335):

```cpp
m_chapterCount->setText(tr("%1 chapters · %2 downloaded")
    .arg(chapters.size()).arg(downloaded));
```

Replace with a rebuild of the full metaLine:

```cpp
QStringList parts;
if (!m_result.author.isEmpty()) {
    parts.append(m_result.author);
}
const QString status = m_result.status.isEmpty() ? tr("Unknown") : m_result.status;
parts.append(status);
parts.append(mangaSourceDisplayName(m_result.source));
parts.append(tr("%1 chapters").arg(chapters.size()));
if (downloaded > 0) {
    parts.append(tr("%1 downloaded").arg(downloaded));
}
m_metaLine->setText(parts.join(QStringLiteral(" · ")));
```

The em-dash placeholder is gone — empty author simply omits the segment. Empty downloaded count (i.e. 0 chapters on disk) also omits its segment.

- [ ] **Step 5: Run `build_check.bat`**

Expected: `BUILD OK`. If compile errors complain about removed `m_authorLabel` / `m_statusLabel` / `m_chapterCount` references anywhere else in the cpp, grep for them and remove. The overflow menu's `aboutToShow` lambda at cpp:95-100 references `m_downloader` only — not the removed members — so it should be fine.

- [ ] **Step 6: Commit**

```bash
git add src/ui/pages/tankoyomi/MangaDetailView.h src/ui/pages/tankoyomi/MangaDetailView.cpp
git commit -m "fix(tankoyomi): MangaDetailView hero typography mirrors StreamDetailView"
```

- [ ] **Step 7: Append RTC to chat.md (unstaged)**

```
READY TO COMMIT - [Agent 4B, TANKOYOMI_SERIES_PAGE_FIX Task 2 2026-05-14 ~HH:MMam — MangaDetailView hero typography parity with StreamDetailView. m_titleLabel gets explicit QSS "color: #e0e0e0; font-size: 16px; font-weight: bold;" matching StreamDetailView.cpp:400. Three stacked rows (author/status/chapterCount) collapsed into one inline m_metaLine (StreamDetailView.cpp:410-419 mirror — "QLabel#MangaDetailMetaLine { background: transparent; border: none; color: rgba(255,255,255,0.62); font-size: 12px; font-weight: 400; }"). show() builds dot-separated parts list: [author if non-empty] · status · source · "Loading chapters..." placeholder; onChaptersReady rebuilds with "N chapters" + optional "M downloaded". Em-dash author fallback removed — empty author omits its segment entirely. m_authorLabel / m_statusLabel / m_chapterCount members deleted. BUILD OK. Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion]] | files: src/ui/pages/tankoyomi/MangaDetailView.h, src/ui/pages/tankoyomi/MangaDetailView.cpp, agents/chat.md
```

---

### Task 3 — Download-dropdown glyph fix

**Files:**
- Modify: `src/ui/pages/tankoyomi/MangaDetailView.cpp:130-132` — `m_downloadDropdown` text + style

- [ ] **Step 1: Diagnose the broken glyph**

Locate cpp:131:

```cpp
m_downloadDropdown->setText(tr("Download v"));
```

The literal `"Download v"` is being rendered as text `Download v` — lowercase v, not a chevron. This is the root cause of the "broken glyph" in Hemanth's screenshot.

- [ ] **Step 2: Replace with Unicode down-triangle + QSS-flat button**

Replace the text + add explicit styling matching sibling button shape:

```cpp
m_downloadDropdown->setText(QString::fromUtf8("Download \xE2\x96\xBE"));   // U+25BE ▾
m_downloadDropdown->setObjectName("MangaDetailDownloadDropdown");
m_downloadDropdown->setPopupMode(QToolButton::InstantPopup);
m_downloadDropdown->setCursor(Qt::PointingHandCursor);
m_downloadDropdown->setStyleSheet(
    "#MangaDetailDownloadDropdown {"
    "  background: rgba(255,255,255,0.08); border: 1px solid rgba(255,255,255,0.18);"
    "  border-radius: 6px; color: #e0e0e0; font-size: 12px; padding: 6px 14px; }"
    "#MangaDetailDownloadDropdown:hover { background: rgba(255,255,255,0.14);"
    "  border-color: rgba(255,255,255,0.28); }"
    "#MangaDetailDownloadDropdown::menu-indicator { image: none; width: 0px; }");
```

The `menu-indicator { image: none; width: 0px; }` rule kills Qt's default arrow indicator (which is the secondary source of the "broken glyph" — Qt was rendering both the literal `v` AND its own InstantPopup indicator, doubling up).

QSS borrowed from `StreamDetailView.cpp:330-336` (the `DetailLibraryBtn` / `DetailTrailerBtn` shape).

- [ ] **Step 3: Run `build_check.bat`**

Expected: `BUILD OK`.

- [ ] **Step 4: Commit**

```bash
git add src/ui/pages/tankoyomi/MangaDetailView.cpp
git commit -m "fix(tankoyomi): Download-dropdown glyph + QSS parity with sibling buttons"
```

- [ ] **Step 5: Append RTC to chat.md (unstaged)**

```
READY TO COMMIT - [Agent 4B, TANKOYOMI_SERIES_PAGE_FIX Task 3 2026-05-14 ~HH:MMam — Download-dropdown glyph fix. Root cause: literal "Download v" string was rendering as lowercase v (not a chevron) AND Qt's default InstantPopup menu-indicator was painting alongside — double-glyph artifact. Fixed: text → "Download ▾" (U+25BE ▾ black-down-pointing-small-triangle); QSS menu-indicator suppressed via "image: none; width: 0px;". Added sibling QSS (rgba bg + border + radius + hover) matching StreamDetailView.cpp:330-336 (DetailLibraryBtn shape). PointingHand cursor added. BUILD OK. Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion]] | files: src/ui/pages/tankoyomi/MangaDetailView.cpp, agents/chat.md
```

---

### Task 4 — Chapter table column model

**Files:**
- Modify: `src/ui/pages/tankoyomi/MangaDetailView.cpp` — top of file (add anonymous namespace constants); cpp:214-231 (table construction)

- [ ] **Step 1: Add anonymous-namespace column constants**

At the top of `MangaDetailView.cpp`, after the includes (around line 30, after the last `#include`), add:

```cpp
namespace {
// Mirror of StreamDetailView.cpp:53-60 column-constants pattern. Tankoyomi's
// 2-column model: title (stretch) + action (fixed). No checkbox / no thumb /
// no progress / no status column — Hemanth flagged the # and Date columns
// as dead in the post-overhaul smoke; the per-chapter download indicator is
// already the only meaningful action surface.
constexpr int kColTitle    = 0;
constexpr int kColAction   = 1;
constexpr int kColumnCount = 2;
}  // namespace
```

- [ ] **Step 2: Rewrite chapter table construction in buildUI**

Locate cpp:214-231:

```cpp
m_chapterTable = new QTableWidget(0, 4, this);
m_chapterTable->setObjectName("MangaDetailChapterTable");
m_chapterTable->setHorizontalHeaderLabels(
    {tr("#"), tr("Chapter"), tr("Date"), tr("")});
m_chapterTable->horizontalHeader()->setStretchLastSection(false);
m_chapterTable->horizontalHeader()->setSectionResizeMode(
    1, QHeaderView::Stretch);
m_chapterTable->verticalHeader()->hide();
m_chapterTable->setSelectionBehavior(QAbstractItemView::SelectRows);
m_chapterTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
m_chapterTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
m_chapterTable->setShowGrid(false);
m_chapterTable->setContextMenuPolicy(Qt::CustomContextMenu);  // F.2 wires
```

Replace with the StreamDetailView-shaped 2-column model:

```cpp
m_chapterTable = new QTableWidget(0, kColumnCount, this);
m_chapterTable->setObjectName("MangaDetailChapterTable");
m_chapterTable->setHorizontalHeaderLabels({
    tr("Chapter"),
    QString(),                       // action — no header text
});
m_chapterTable->horizontalHeader()->setSectionResizeMode(kColTitle,  QHeaderView::Stretch);
m_chapterTable->horizontalHeader()->setSectionResizeMode(kColAction, QHeaderView::Fixed);
m_chapterTable->setColumnWidth(kColAction, 36);
m_chapterTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
m_chapterTable->verticalHeader()->setDefaultSectionSize(56);
m_chapterTable->verticalHeader()->hide();
m_chapterTable->setSelectionBehavior(QAbstractItemView::SelectRows);
m_chapterTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
m_chapterTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
m_chapterTable->setShowGrid(false);
m_chapterTable->setAlternatingRowColors(true);
m_chapterTable->setSortingEnabled(false);
m_chapterTable->setContextMenuPolicy(Qt::CustomContextMenu);
m_chapterTable->setStyleSheet(
    "QTableWidget { background: transparent; border: none; color: #ccc;"
    "  alternate-background-color: rgba(255,255,255,0.03); }"
    "QTableWidget::item { padding: 4px; }"
    "QTableWidget::item:selected { background: rgba(255,255,255,0.08); }"
    "QHeaderView::section { background: rgba(255,255,255,0.05); color: rgba(255,255,255,0.5);"
    "  border: none; font-size: 11px; padding: 4px; }");
```

Key changes from prior model:
- Column count: 4 → 2 (`kColumnCount`)
- Headers: `{"#", "Chapter", "Date", ""}` → `{"Chapter", ""}`
- Resize modes: title stretch + action fixed
- Action column width: 36px (matches StreamDetailView's action column)
- Header default alignment: `AlignLeft | AlignVCenter` — eliminates the "centered header / left-aligned rows" mismatch Hemanth flagged
- Row height: `verticalHeader()->setDefaultSectionSize(56)` — hardcoded per `feedback_qt_sizehintforrow_unreliable_pre_show.md`
- AlternatingRowColors enabled (was off)
- SortingEnabled explicitly false (was implicit)
- Full table QSS borrowed from `StreamDetailView.cpp:579-585`

- [ ] **Step 3: Run `build_check.bat`**

Expected: `BUILD OK`. `renderChapters` at cpp:468-500 still references `setItem(i, 0..3, …)` with old column indices — those will compile fine but produce wrong cell placement at runtime. Task 5 fixes that. If you smoke-test between Task 4 and Task 5 you'll see empty chapter rows; that's expected.

- [ ] **Step 4: Commit**

```bash
git add src/ui/pages/tankoyomi/MangaDetailView.cpp
git commit -m "fix(tankoyomi): chapter table 2-col model (drop # and Date columns)"
```

- [ ] **Step 5: Append RTC to chat.md (unstaged)**

```
READY TO COMMIT - [Agent 4B, TANKOYOMI_SERIES_PAGE_FIX Task 4 2026-05-14 ~HH:MMam — chapter table 2-col model per Hemanth ("I don't need index and date columns"). NEW anonymous-namespace constants kColTitle=0, kColAction=1, kColumnCount=2 (mirror of StreamDetailView.cpp:53-60). Headers reduced to {"Chapter", ""}. kColTitle Stretch + kColAction Fixed 36px (matches StreamDetailView's action column width). Header default alignment AlignLeft|AlignVCenter — eliminates the "centered header / left-aligned rows" mismatch. Row height hardcoded 56px via verticalHeader->setDefaultSectionSize per feedback_qt_sizehintforrow_unreliable_pre_show.md. AlternatingRowColors enabled + sortingEnabled false. Full table QSS from StreamDetailView.cpp:579-585 (transparent bg, ::item padding, ::item:selected highlight, ::section header style). renderChapters still references old column indices — Task 5 refactors. BUILD OK (runtime smoke between Task 4 and Task 5 will show empty rows; expected). Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion]] | files: src/ui/pages/tankoyomi/MangaDetailView.cpp, agents/chat.md
```

---

### Task 5 — `renderChapters` refactor for the new column model

**Files:**
- Modify: `src/ui/pages/tankoyomi/MangaDetailView.cpp:468-500` — `renderChapters` body
- Modify: `src/ui/pages/tankoyomi/MangaDetailView.cpp:345-376` — `onChapterUpdated` body (cellWidget lookup uses old column index)
- Modify: `src/ui/pages/tankoyomi/MangaDetailView.cpp:502-530` — `onChapterIconClicked` body (cellWidget lookup uses old column index)

- [ ] **Step 1: Rewrite `renderChapters`**

Locate cpp:468-500. Replace the entire body with:

```cpp
void MangaDetailView::renderChapters()
{
    m_chapterTable->setRowCount(m_chapters.size());
    for (int i = 0; i < m_chapters.size(); ++i) {
        const ChapterInfo& ch = m_chapters[i];

        // kColTitle: stacked title + optional inline date subtitle.
        // Mirror of StreamDetailView.cpp:920-950 title-cell shape.
        auto* titleCell = new QWidget(m_chapterTable);
        auto* titleLayout = new QVBoxLayout(titleCell);
        titleLayout->setContentsMargins(8, 6, 8, 6);
        titleLayout->setSpacing(2);

        auto* nameLabel = new QLabel(ch.name, titleCell);
        nameLabel->setWordWrap(false);
        nameLabel->setStyleSheet(
            "color: #e0e0e0; font-size: 12px; font-weight: 500; background: transparent;");
        nameLabel->setTextFormat(Qt::PlainText);
        titleLayout->addWidget(nameLabel);

        if (ch.dateUpload > 0) {
            const QString dateStr =
                QDateTime::fromMSecsSinceEpoch(ch.dateUpload).toString("yyyy-MM-dd");
            auto* dateLabel = new QLabel(dateStr, titleCell);
            dateLabel->setStyleSheet(
                "color: rgba(255,255,255,0.45); font-size: 10px; background: transparent;");
            dateLabel->setTextFormat(Qt::PlainText);
            titleLayout->addWidget(dateLabel);
        } else {
            titleLayout->addStretch();
        }
        m_chapterTable->setCellWidget(i, kColTitle, titleCell);

        // Hidden QTableWidgetItem in kColTitle so selection / sort / row-data
        // mechanics still work — cellWidget() doesn't satisfy the selection
        // model on its own. UserRole carries the chapter ID for context-menu
        // lookup mirror of StreamDetailView.cpp:891.
        auto* titleItem = new QTableWidgetItem();
        titleItem->setData(Qt::UserRole, ch.id);
        m_chapterTable->setItem(i, kColTitle, titleItem);

        // kColAction: ChapterDownloadIndicator (B.1–B.3, 5-state animated).
        auto* indicator = new ChapterDownloadIndicator(m_chapterTable);
        m_chapterTable->setCellWidget(i, kColAction, indicator);

        // Initial state from downloader records.
        deriveChapterState(ch.id, *indicator);

        connect(indicator, &ChapterDownloadIndicator::clicked, this,
                [this, i]() { onChapterIconClicked(i); });
    }
}
```

Removed:
- `QTableWidgetItem` setItem calls for cols 0/1/2 (now we use a single cellWidget for kColTitle)
- `resizeColumnToContents(0/2/3)` calls — replaced by the Fixed/Stretch resize-modes set in Task 4

Added:
- Stacked QVBoxLayout title cell mirroring `StreamDetailView.cpp:920-950`
- Inline date subtitle when `dateUpload > 0` (replaces the dead Date column)
- Hidden `QTableWidgetItem` in kColTitle to satisfy selection model (cellWidget alone doesn't carry data — same pattern as `StreamDetailView.cpp:893` where the action-column QPushButton lives alongside a `QTableWidgetItem` carrying UserRole data)

- [ ] **Step 2: Fix `onChapterUpdated` cellWidget lookup**

Locate cpp:371-373:

```cpp
auto* indicator = qobject_cast<ChapterDownloadIndicator*>(
    m_chapterTable->cellWidget(row, 3));
```

Replace the literal `3` with the constant:

```cpp
auto* indicator = qobject_cast<ChapterDownloadIndicator*>(
    m_chapterTable->cellWidget(row, kColAction));
```

- [ ] **Step 3: Fix `onChapterIconClicked` cellWidget lookup**

Locate cpp:508-509:

```cpp
auto* indicator = qobject_cast<ChapterDownloadIndicator*>(
    m_chapterTable->cellWidget(row, 3));
```

Same fix:

```cpp
auto* indicator = qobject_cast<ChapterDownloadIndicator*>(
    m_chapterTable->cellWidget(row, kColAction));
```

- [ ] **Step 4: Check for any other hardcoded column indices**

```bash
git grep -nE "cellWidget\(.*,\s*[0-3]\)" src/ui/pages/tankoyomi/MangaDetailView.cpp
git grep -nE "setItem\(.*,\s*[0-3]\)" src/ui/pages/tankoyomi/MangaDetailView.cpp
git grep -nE "item\(.*,\s*[0-3]\)" src/ui/pages/tankoyomi/MangaDetailView.cpp
```

Expected: zero hits after Steps 1–3. If any literal column index remains, replace with `kColTitle` or `kColAction`.

Also check `showChapterContextMenu` at cpp:613+ (F.2-shipped) — it uses `m_chapterTable->cellWidget(row, 3)` for state lookup. Replace with `kColAction`.

- [ ] **Step 5: Run `build_check.bat`**

Expected: `BUILD OK`.

- [ ] **Step 6: Commit**

```bash
git add src/ui/pages/tankoyomi/MangaDetailView.cpp
git commit -m "fix(tankoyomi): renderChapters uses kCol constants + stacked title cell"
```

- [ ] **Step 7: Append RTC to chat.md (unstaged)**

```
READY TO COMMIT - [Agent 4B, TANKOYOMI_SERIES_PAGE_FIX Task 5 2026-05-14 ~HH:MMam — renderChapters refactor for the 2-col model. Title cell now a QVBoxLayout cellWidget with primary name label (color: #e0e0e0; font-size: 12px; font-weight: 500;) + optional inline date subtitle when ch.dateUpload>0 (color: rgba(255,255,255,0.45); font-size: 10px;) — mirror of StreamDetailView.cpp:920-950 stacked title-cell shape. Hidden QTableWidgetItem in kColTitle carries Qt::UserRole=ch.id for selection-model + context-menu lookup. ChapterDownloadIndicator (B.1-B.3) preserved in kColAction. All literal column indices replaced with kColTitle/kColAction constants — fixed cellWidget lookups in onChapterUpdated, onChapterIconClicked, showChapterContextMenu. Date column collapse drops the parallel-date-column from prior shape. BUILD OK. Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion]] | files: src/ui/pages/tankoyomi/MangaDetailView.cpp, agents/chat.md
```

---

### Task 6 — Build verify + MCP smoke

**Files:**
- Read-only: all of MangaDetailView post-Task-5
- Create: `agents/audits/evidence_tankoyomi_series_page_fix_<HHMMSS>.png`

- [ ] **Step 1: Kill any running Tankoban + clean rebuild**

```bash
taskkill /F /IM Tankoban.exe
```

(Expected: process killed or "process not found" — either is fine.)

- [ ] **Step 2: Launch via `build_and_run.bat`**

```bash
build_and_run.bat
```

Wait for Tankoban window. Should compile clean from the prior `build_check.bat` runs.

- [ ] **Step 3: Drive smoke via MCP**

Claim MCP LOCK by appending to chat.md (unstaged):

```
[Agent 4B, MCP LOCK CLAIM 2026-05-14 ~HH:MMam — TANKOYOMI_SERIES_PAGE_FIX Task 6 smoke]
```

Then drive:

```bash
out/tankoctl.exe ping
out/tankoctl.exe open-page tankoyomi
out/tankoctl.exe get-state    # confirm activePageId = tankoyomi
```

Via pywinauto-mcp: click search input (AutomationId derived from `QLineEdit` objectName; run `scripts/uia-dump.ps1 -TargetClass TankoyomiPage` if needed). Type "Descender". Press Enter. Wait for "Done: N Results". Double-click the first result tile.

- [ ] **Step 4: Verify the 6 visual outcomes**

In the Tankoban window, check:

1. **Back button reads `← Back`** (Unicode leftward arrow + capital-B Back), not `< back` or `<-back`.
2. **Title is bold 16px in white**, NOT the previous default-styled label.
3. **Meta line is a single dot-separated string** — `Unknown · ReadComicsOnline · 32 chapters` (no em-dash row, no separate author row when author is empty).
4. **Download dropdown reads `Download ▾`** with the proper down-triangle glyph + a subtle border/background like the Stream library buttons. Click it to confirm the menu still opens and has the 5 actions from D.3 (Download all / Next 5 / 10 / 25 / Custom range...).
5. **Chapter table has 2 columns** — left-aligned "Chapter" header above left-aligned chapter rows; right-edge action column with `ChapterDownloadIndicator` (the per-chapter download circle from B.1–B.3). No # column. No Date column.
6. **Rows are 56px tall** — chapter name on the primary line, date string on a smaller-italic-gray secondary line if the scraper returned `dateUpload > 0` (likely empty for ReadComicsOnline source; verify by trying a WeebCentral query like "Sapiens" if needed).

- [ ] **Step 5: Capture screenshot**

Via windows-mcp Screenshot tool. Save as `agents/audits/evidence_tankoyomi_series_page_fix_<HHMMSS>.png`.

- [ ] **Step 6: Clean up**

```bash
scripts/stop-tankoban.ps1
```

Append MCP LOCK release to chat.md (unstaged):

```
[Agent 4B, MCP LOCK RELEASED 2026-05-14 ~HH:MMam]
```

- [ ] **Step 7: Commit evidence**

```bash
git add agents/audits/evidence_tankoyomi_series_page_fix_*.png
git commit -m "evidence(tankoyomi): series page fix smoke — 6 visual outcomes verified"
```

- [ ] **Step 8: Append final RTC to chat.md (unstaged)**

```
READY TO COMMIT - [Agent 4B, TANKOYOMI_SERIES_PAGE_FIX Task 6 2026-05-14 ~HH:MMam — series-page fix smoke GREEN end-to-end. Tankoyomi search "Descender" → double-click first result → MangaDetailView post-fix renders: (1) "← Back" Unicode-arrow text button with SidebarAction QSS, (2) bold 16px title "Descender (2015-)", (3) single inline meta line "Unknown · ReadComicsOnline · 32 chapters" (no em-dash row), (4) "Download ▾" dropdown with proper down-triangle + sibling QSS, (5) 2-column chapter table left-aligned Chapter header + 36px-fixed action column with ChapterDownloadIndicator per row, (6) 56px row height with stacked chapter-name + (when present) date subtitle. Evidence at agents/audits/evidence_tankoyomi_series_page_fix_HHMMSS.png. All 6 Task-target visual outcomes verified. Skills invoked: [/superpowers:executing-plans, /build-verify, /superpowers:verification-before-completion]] | files: agents/audits/evidence_tankoyomi_series_page_fix_HHMMSS.png, agents/chat.md
```

---

## §7 Risk + rollback notes

### Risks

- **Task 2's removal of `m_authorLabel` / `m_statusLabel` / `m_chapterCount` members will surface compile errors anywhere they're referenced.** Three known sites: `show()` (cpp:255-260), `onChaptersReady` (cpp:333-334). The grep step in Task 2 catches any others. Low risk; build_check is the safety net.

- **Task 4 changes the table column count from 4 to 2.** Any leftover hardcoded literal column index will runtime-misroute setItem / cellWidget calls. Task 5's grep step is the safety net; the build_check gate compiles fine even when indices are wrong (Qt doesn't validate column count at compile time).

- **The hidden `QTableWidgetItem` in `kColTitle`** (Task 5) is a Qt-specific necessity — without it, `selectionModel()->selectedRows()` returns empty even when the user clicks a row, breaking shift-click multi-select. Same pattern as `StreamDetailView.cpp:893`. Low risk; the pattern is proven in the sibling surface.

- **`showChapterContextMenu`** (F.2-shipped) uses `cellWidget(row, 3)` for state lookup. Task 5 Step 4's grep catches this; if missed, right-click on a chapter row would show the wrong state-variant menu.

- **No engine-side risk.** `MangaDownloader` is untouched. The per-chapter state machine + `ChapterDownloadIndicator` widget keep working unchanged.

### Rollback

- **Single-commit revertibility per task.** Each task ships as one commit. Reverting any single task returns `MangaDetailView` to the prior task's state.
- **Full series-page rollback:** `git revert` the 6 commits (Tasks 1–6) in reverse order. The pre-fix MangaDetailView state is whatever was at HEAD before Task 1's commit.
- **No JSON store schema changes.** No persistence layer concerns. No migration script needed.

---

## §8 Open questions for Hemanth

Per the prompt's Rule-14 directive ("Most calls in this plan are Rule-14 yours-to-make — fonts, sizes, exact column widths, icon sizes — pick those yourself"), the plan picks defaults for all coder-level questions. Only one product-level question remains worth surfacing:

**Q1: Inline date subtitle visibility.** Task 5 shows `dateUpload` as a small-italic-gray inline subtitle under each chapter name when the scraper populated it (`ch.dateUpload > 0`). For ReadComicsOnline this field is almost always 0 (scraper doesn't extract release dates). For WeebCentral it's sometimes populated. Three options:

- **(a) Show when present, hide when absent** (current plan default). Some rows have a date subtitle, others don't. Slight visual inconsistency but always honest.
- **(b) Never show dates inline.** Cleaner uniform row height (always single-line chapter name). Throws away available data on WeebCentral.
- **(c) Show "(no date)" placeholder when absent.** Worst of both worlds — never recommended; flagged for completeness only.

Plan default: (a). Hemanth can flip to (b) during execution if the visual inconsistency reads worse than expected.

**Q2 (informational, no answer required): Reuse `chevron_left.svg`?** The prompt suggested `chevron_left.svg` exists and should be used for the back button. The plan rejects this — `StreamDetailView`'s actual back button is the Unicode-arrow text variant. If Hemanth wants the chevron-SVG version instead, that's a Task-1 follow-up: swap the text with `setIcon(QIcon(":/icons/chevron_left.svg"))` + `setText("")`. No structural change.

---

## Self-review

**Spec coverage check** (against Hemanth's §1 verbatim):

- "lazy `<back` text-affordance — use a proper back button" → Task 1.
- "`Download v` has a broken glyph" → Task 3.
- "# column with no index under it" → Task 4 (drop the # column).
- "Chapter header centered but rows left-aligned" → Task 4 (`AlignLeft | AlignVCenter` header default).
- "no dates beneath Date, just the download buttons" → Task 4 (drop Date column) + Task 5 (inline-date subtitle when populated).
- "I don't need index and date columns" → Task 4 (both columns dropped).
- "Look at Stream mode's TV show detail page as the visual anchor" → §2 (file:line cites for every borrowed pattern).

Also covered (Hemanth's screenshot showed but didn't verbally call out):
- Em-dash author placeholder → Task 2 (hide instead of fall back to em-dash).
- Title typography (default-styled label looked thin) → Task 2 (bold 16px white).
- Three-row stacked meta column reads heavy → Task 2 (single inline line).

**Placeholder scan:** No "TBD" / "implement later". The two `~HH:MMam` placeholders in RTC lines are intentional — the executor fills the actual time. All other content is concrete.

**Type consistency check:** `kColTitle` / `kColAction` / `kColumnCount` named consistently across Tasks 4 and 5. `m_metaLine` named consistently across Task 2 + onChaptersReady update. `ChapterDownloadIndicator` references unchanged from B.1–B.3.

**Scope check:** Single coherent user-outcome (polish the Mihon-overhaul series page to StreamDetailView parity). 6 tasks, all single-file edits on `MangaDetailView.{h,cpp}`. Tightly scoped.

---

## Execution handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-14-tankoyomi-series-page-fix.md`. Two execution options:

1. **Subagent-Driven (recommended)** — Agent 4B dispatches a fresh subagent per task with two-stage review between tasks (matches the cadence used for STREAM_DOWNLOADS_NETFLIX_OVERHAUL P3 and the just-shipped TANKOYOMI_MIHON_OVERHAUL).
2. **Inline Execution** — Execute tasks in this session using `superpowers:executing-plans` with batched checkpoints.

Which approach?
