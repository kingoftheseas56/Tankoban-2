# Sources UI Refinement — Design Spec

- **Date:** 2026-05-13
- **Author:** Agent 4B (Sources)
- **Status:** Brainstorm ratified by Hemanth same-session. Implementation plan to follow via `/superpowers:writing-plans`.
- **Surface:** `src/ui/pages/{TankorentPage,TankoyomiPage,TankoLibraryPage}.{h,cpp}` + supporting widgets under `src/ui/pages/tankolibrary/` and `src/ui/pages/tankoyomi/` + `resources/icons/` + `resources/resources.qrc`.

---

## 1. Purpose

Refine the UI of the three Sources pages to industry-standard fonts, sizes, spacing, alignment, and visual consistency. Polish + density tune. No structural redesign.

The three pages were authored at different times by different agents. Each picked its own search-row layout, tab affordance, status-row position, page margins, and filter density. On top of structural drift, several outright violations of Hemanth's stated UI rules accumulated (blue progress bar, green/red row tints, hardcoded gold bypassing the theme system). This spec corrects both.

---

## 2. Scope

### In-scope
- `src/ui/pages/TankorentPage.{h,cpp}` (~2600 LOC)
- `src/ui/pages/TankoyomiPage.{h,cpp}` (~1100 LOC)
- `src/ui/pages/TankoLibraryPage.{h,cpp}` (~1800 LOC)
- `src/ui/pages/tankolibrary/TransfersView.{h,cpp}` (downstream)
- `src/ui/pages/tankoyomi/MangaResultsGrid.{h,cpp}` (downstream)
- `src/ui/pages/tankolibrary/BookResultsGrid.{h,cpp}` (downstream)
- `resources/icons/magnet.svg` (NEW)
- `resources/icons/kebab-menu.svg` (NEW)
- `resources/resources.qrc` (register new SVGs)

### Out of scope (explicit)
1. Stream-mode pages (StreamPage / StreamDetailView / StreamLibraryLayout) — Agent 4 territory.
2. Library-consumer pages (VideosPage / BooksPage / ComicsPage / AudiobooksPage) — Agent 5 territory per `feedback_agent5_scope.md`.
3. `Theme.cpp` palette work — Agent 5. Coordinate via `agents/chat.md` if a new token is needed (see §6 Theme integration map).
4. Tankorent search row 2 layout — Hemanth explicit "no change to our torrent downloader".
5. Tab-affordance harmonization across pages — Hemanth picked "leave inconsistent".
6. Tankoyomi 5-column results / view-toggle / source ordering — Hemanth picked "keep as is".
7. Column removal beyond Tankorent's Files column.
8. TankoLibrary Books/Audiobooks pill row collapse.
9. New colors (strict gray/black/white + single gold accent per `feedback_no_color_no_emoji.md`).
10. New emoji glyphs.
11. QML migration (Qt Widgets + QSS only per `feedback_qt_vs_electron_aesthetic.md`).

---

## 3. Per-page audit verdict

### 3.1 TankorentPage

**Strong:** Search row 1 clearly anchors the primary action. Per-site Category vocabulary lookups are well-modeled. Click-to-sort headers work end-to-end with persisted state. Soft-cap pagination at 100 rows with "Show all" link. Group/child transfer rows handle stream-bulk inclusion cleanly. Healthy keyboard polish (Return submits, drag-drop accepts).

**Weak:** Green / red row tints (color rule violation). Category column shows raw IDs like `"1_2"` on some sources. Files column always shows `"-"`. Link column uses `M` and `↓` text glyphs, not SVG icons. Bulk-group Category cell hardcodes `"videos"` lowercase. Search row 2 carries 8 horizontal controls (busy at any width but Hemanth wants it preserved). No empty / loading / zero-results states.

**Refresh candidates:** TR.1 – TR.8 below.

### 3.2 TankoyomiPage

**Strong:** Empty + loading + zero-results state pages exist (the model the other two pages will copy). Stacked widget swaps list / grid / empty / loading cleanly. Per-scraper status with an error toast carrying a Retry action. Sort combo and view toggle persist across sessions. Paginated downloader controls with pause / resume.

**Weak:** Loading bar uses `#60a5fa` (color rule violation). Lowercase status enum leaks into the Transfers table (`"queued"`). Table column / header alignment drifts (status cells left-aligned, header centered). `⋮` Unicode glyph for the More button instead of an SVG icon.

**Refresh candidates:** TY.1 – TY.7 below.

### 3.3 TankoLibraryPage

**Strong:** Per-tab placeholder customization. Books / Audiobooks media-tab pills above the search row. Inline Transfers tab as a sibling pill toggle. Async cover cache with disk + memory tiers. Two-stage detail flow (snapshot-paint, then merged detail update).

**Weak:** 9-control search row (Query / Search / Cancel / 3 format checkboxes / English-only / Sort / Audio format) — won't fit at industry-standard density. Hardcoded gold `#c7a76b` in four places, bypassing the theme system Agent 5 shipped. Page margins `12/12/12/12` don't match the other two pages' `8/8/8/0`.

**Refresh candidates:** TL.1 – TL.5 below.

---

## 4. Cross-page rules (apply to all three pages)

### CR.1 — Density lift to industry standard

- Controls: 30px → **36px** height (QLineEdit, QComboBox, QPushButton, QToolButton in search/filter rows).
- Body text: 11px / 12px → **13px** (status labels, table cell text).
- Inter-row spacing (QVBoxLayout/QHBoxLayout::setSpacing): 6 → **10**.
- Table row height (`verticalHeader()->setDefaultSectionSize`): 26px → **32px**.
- Header font-weight: 400 → **600** for clearer hierarchy.

Reference targets: Stremio search bar, Windows File Explorer, Steam library, VS Code search panel.

### CR.2 — Page margins harmonized

- All three pages adopt **`setContentsMargins(12, 12, 12, 12)`** on the outer `QVBoxLayout`.
- Inter-row spacing on the outer layout: **10**.

### CR.3 — Theme integration

- Every page gets a page-level `setObjectName()` so Theme.cpp QSS can target it:
  - `TankorentPage::setObjectName("TankorentPage")`
  - `TankoyomiPage::setObjectName("TankoyomiPage")`
  - `TankoLibraryPage::setObjectName("TankoLibraryPage")`
- Inline hardcoded colors route through Theme.cpp tokens (see §6 mapping table).
- If a needed token is absent from Theme.cpp (e.g. `__FG_MUTED__`), flag in `agents/chat.md` for Agent 5 before adding — do not modify `Theme.cpp` unilaterally.

### CR.4 — Empty + Loading + Zero-results states

Three-state stack ported from Tankoyomi (proven pattern at `TankoyomiPage.cpp:381-470`).

- **Pre-search empty:** centered label "Type a query and hit Enter" + page-specific hint (see per-page sections for exact copy).
- **Loading:** centered "Searching..." label + indeterminate progress bar (4px high, 220px wide, theme-accent chunk, transparent track).
- **Zero-results:** centered "No results for '<query>'" + Retry button (re-runs `m_lastQuery`) + Clear button (clears input, focuses input, returns to pre-search state).

`QStackedWidget` index assignment:
- Index 0: existing data view (table or grid)
- Index 1: empty / pre-search page
- Index 2: loading page
- Index 3: zero-results page

### CR.5 — Hover row state

All `QTableView` and `QListView`-derived result + transfer surfaces get a subtle hover-row tint.

QSS scoped per table (always under `#ObjectName` per `feedback_css_scoping.md`):

```
#SearchResultsTable::item:hover { background: rgba(255,255,255,0.04); }
#TransfersTable::item:hover     { background: rgba(255,255,255,0.04); }
#MangaResultsTable::item:hover  { background: rgba(255,255,255,0.04); }
#MangaTransfersTable::item:hover{ background: rgba(255,255,255,0.04); }
```

(Selection background `__ACCENT_SOFT__` is brighter, so the hover state falls back cleanly when a row is selected.)

### CR.6 — Tab styles stay inconsistent

Hemanth explicit. Tankorent + Tankoyomi keep `QTabWidget`. TankoLibrary keeps custom pill toggle.

### CR.7 — Table header weight 600

```
#<TableName> QHeaderView::section { font-weight: 600; }
```

Applied per page. Header font-size stays 11–12px (small enough to fit; weight 600 carries the hierarchy).

### CR.8 — Status string Title Case rule

**Every visible status / state / category display string renders in Title Case.** Source enums stay lowercase; mapping happens at the render boundary via a per-page helper.

- **Tankorent:** `torrentStatusText()` already maps to Title Case ✓. One stray lowercase to fix: bulk-group Category cell at `TankorentPage.cpp:1977` (`"videos"` → `"Videos"`).
- **Tankoyomi:** add `chapterStatusText(QString state)` helper in the anonymous namespace mapping `"queued"` → `"Queued"`, `"downloading"` → `"Downloading"`, `"paused"` → `"Paused"`, `"complete"` → `"Complete"`, `"failed"` → `"Failed"`. Audit MangaDownloader's chapter-state vocabulary during implementation; cover every state it emits.
- **TankoLibrary:** audit `src/ui/pages/tankolibrary/TransfersView.{h,cpp}` for raw enum leaks; apply equivalent helper.

### CR.9 — Header / cell alignment match rule

**Every table column's header alignment matches its cell alignment.**

Apply explicit alignment per column on both:
- Header: `table->horizontalHeader()->model()->setHeaderData(col, Qt::Horizontal, Qt::Alignment, Qt::TextAlignmentRole)` (or simpler: set a per-column header alignment via a small helper after `setHorizontalHeaderLabels`).
- Cell: `item->setTextAlignment(Qt::Alignment)` on every cell creation.

Conventions:

1. **Name / Title / Series / Category / Status (text)** columns → Left + VCenter on both.
2. **Numeric (Size, Speed, Seeds, Peers, Progress %, Chapters, ETA)** columns → Center + VCenter on both.
3. **Icon-only (Status icon, Link, Info, Queue)** columns → Center + VCenter on both.

---

## 5. Per-page changes

### 5.1 TankorentPage

#### TR.1 — Results columns: 7 → 6

- Remove the **Files** column entirely. Always `"-"` until download; no useful pre-add information.
- Final column set: `Title | Category | Size | Seeders | Leechers | Link`.
- `TorrentResult::files` field kept in the backing struct for backend use.
- `m_resultsSortCol` rebase: Seeders col index 4 → **3**; Leechers 5 → **4**; Link 6 → **5**. Update sort restore + persistence logic in the constructor (`TankorentPage.cpp:386-397`).
- Update column-visibility right-click menu (`TankorentPage.cpp:741-762`) — drop the Files entry; rebuild persistence key (the `tankorent/hiddenColumns` CSV becomes 0..5 not 0..6).
- Update `compareResults` switch (`TankorentPage.cpp:1257-1280`) — re-route col indices.

#### TR.2 — Category column shows human-friendly names

Translate raw IDs to friendly names per source. Build a static reverse-mapping function `categoryDisplayName(sourceKey, rawId)` reading the existing `CategoryOption` arrays already at the top of `TankorentPage.cpp`:

- `"1_2"` (nyaa) → `"Anime — English"`
- `"200"` (piratebay) → `"Video"`
- `"100"` (piratebay) → `"Audio"`
- `"41"` (1337x) → `"TV / HD"`
- (continues for every `CategoryOption` entry across NYAA / PIRATEBAY / EXTTORRENTS / YTS / EZTV / X1337X / TORRENTSCSV)

If a result's `categoryId` doesn't match any registered option, fall back to the existing `r.category` value if non-empty; else display `"—"` (em-dash).

Replace the current render site at `TankorentPage.cpp:1173-1174` with the resolver call.

#### TR.3 — Trust signal: bold seeder count, no row tint

- **Remove** the four brush constants `kHealthyOdd / kHealthyEven / kPoorOdd / kPoorEven` and their application loop at `TankorentPage.cpp:1233-1247`.
- Replace with per-cell weight + foreground:
  - When `r.seeders >= 50`: set the Seeders cell font weight bold (`QFont::Bold`).
  - When `r.seeders < 5`: set the Seeders cell foreground to a dim gray (`palette.fgMuted` or `__FG_MUTED__` resolved at render time; fall back to `#888` if Theme token unavailable).
  - Mid-range (5–49): normal weight, normal foreground.
- `trustClass()` static helper stays (used elsewhere or in case future signals reattach); the rendering loop is what changes.

#### TR.4 — Title cell tighter inline format

Replace `[Source]  Title  [Q1]  [Q2]  [Q3]` with **`Source · Title · Q1 Q2 Q3`** where:
- Source name renders in dim gray, separated from title by ` · ` (space + middle dot + space).
- Title renders in normal foreground.
- Quality tags join with spaces (no brackets), prefixed by ` · `, rendered in dim gray.

**Implementation:** custom `QStyledItemDelegate` painting three segments in the same cell with different palette colors. Delegate registered only on column 0 of `m_resultsTable`. Pseudocode:

```
class TitleCellDelegate : public QStyledItemDelegate {
    void paint(painter, option, index) override {
        const auto data = index.data(Qt::UserRole).value<TitleSegments>();
        // paint data.source in fgMuted at (x=margin, y=center)
        // paint " · " in fgMuted
        // paint data.title in fg at next x
        // if data.quality non-empty: paint " · " + data.quality in fgMuted
        // each segment measured via QFontMetrics
    }
    QSize sizeHint(option, index) override {
        return { computedWidth, ROW_HEIGHT };  // hardcode ROW_HEIGHT per feedback_qt_sizehintforrow
    }
};
```

`renderResults()` populates the cell's `Qt::UserRole` with a `TitleSegments { source, title, quality }` struct.

Tooltip still carries the full title (existing line `titleItem->setToolTip(r.title)` is preserved).

#### TR.5 — Link column SVG icons

- Replace `↓` text on download QToolButton with `QIcon(":/icons/download-arrow.svg")`. File already exists in `resources/icons/` from STREAM_DOWNLOADS_NETFLIX_OVERHAUL Phase 9.
- Replace `M` text on magnet QToolButton with `QIcon(":/icons/magnet.svg")`. **NEW SVG**, author per Tankoban grayscale family:
  - 16×16 viewBox, single path
  - `stroke="#cccccc"`, `stroke-width="1.6"`, `stroke-linecap="round"`, `stroke-linejoin="round"`
  - No fill (or `fill="none"`)
  - Classic horseshoe-magnet glyph (two parallel vertical poles connected at the top by a half-arc)
- Register `magnet.svg` in `resources/resources.qrc` under the `icons/` prefix.
- QToolButton `setText("")` after icon assignment; `setIconSize(QSize(16,16))`.

#### TR.6 — Search row 2: no structural change

Per Hemanth ("no change to our torrent downloader"). All 8 controls (Videos / Sources / Cat / Filter combos + Refresh + Sources btn + Add URL + More buttons) stay in their current positions. Density lift (CR.1) applies — controls grow from 30px → 36px tall.

#### TR.7 — Empty / loading / zero-results states

Apply CR.4 pattern to Tankorent. Page-specific copy:
- **Pre-search hint:** `"Type a query and hit Enter — e.g. 'the boys 1080p' or 'sapiens 2014'"`
- **Loading:** `"Searching <N> sources..."` where N is `m_activeIndexers.size()`.
- **Zero-results:** `"No results from <comma-list of source names tried>. Try a different query or open Sources to enable more."` + Retry + Clear buttons (Retry re-runs last query; Clear empties input + focuses it + returns to pre-search).

Refactor `m_tabWidget`'s first tab from a bare `m_resultsTable` to a `QStackedWidget` holding the table + the three new state pages.

#### TR.8 — Bulk-group Category text Title Case

`TankorentPage.cpp:1977`: replace `catItem->setText(QStringLiteral("videos"))` with `catItem->setText(QStringLiteral("Videos"))`.

### 5.2 TankoyomiPage

#### TY.1 — Loading bar grayscale via theme accent

`TankoyomiPage.cpp:457-460`: replace the chunk-background literal `#60a5fa` (blue) with `__ACCENT__` theme token resolution. Surrounding QSS preserved (`border-radius: 2px`, track `rgba(255,255,255,0.08)`).

Implementation: resolve `palette.accent` at construct time via `Theme::currentPalette()`; subscribe to `Theme::paletteChanged` signal to re-apply on palette switch.

#### TY.2 — Transfer Status string Title Case

Add `chapterStatusText(const QString& rawState)` helper in `TankoyomiPage.cpp` anonymous namespace. Maps every state emitted by `MangaDownloader`:
- `"queued"` → `"Queued"`
- `"downloading"` → `"Downloading"`
- `"paused"` → `"Paused"`
- `"complete"` → `"Complete"`
- `"failed"` → `"Failed"`
- (audit MangaDownloader during impl; add coverage for any state name that exists but isn't in this list — `"resuming"`, `"awaiting"`, etc.)

Apply at the render boundary in the transfers-table population loop (locate via the existing `refreshTransfers()` body; not in the chunk I read end-to-end but follows the same shape as Tankorent's `torrentStatusText()` usage).

#### TY.3 — Header / cell alignment match

Per CR.9. Tankoyomi transfers table column conventions:
- Series (col 0): **Left + VCenter** on both header and cells.
- Progress (col 1): **Center + VCenter** on both.
- Status (col 2): **Center + VCenter** on both. (Status here is a short word like "Queued" — center reads cleaner.)
- Chapters (col 3): **Center + VCenter** on both.

Tankoyomi results table column conventions:
- Title (col 0): **Left + VCenter**.
- Author (col 1): **Left + VCenter**.
- Source (col 2): **Left + VCenter**.
- Status (col 3): **Left + VCenter** (longer phrases like "Ongoing" / "Completed").
- Type (col 4): **Left + VCenter**.

#### TY.4 — Results columns: keep all 5

Per Hemanth. No column changes. Apply CR.8 + CR.9 only.

#### TY.5 — View toggle: keep text-flip pattern

Per Hemanth. Button continues to show the OTHER view's name as its label.

#### TY.6 — More button SVG icon

Replace `⋮` Unicode glyph (`QStringLiteral("⋮")`) with `QIcon(":/icons/kebab-menu.svg")`. **NEW SVG**, author per Tankoban grayscale family:
- 16×16 viewBox
- Three filled dots stacked vertically, each ~2.5px diameter, on the vertical centerline
- `fill="#cccccc"`, no stroke
- Dot positions: y=4, y=8, y=12

Register `kebab-menu.svg` in `resources/resources.qrc`. `setText("")` after icon assignment; `setIconSize(QSize(16,16))`.

#### TY.7 — Empty / loading / zero-results state polish

Existing states stay in place. Apply CR.1 density lift to:
- `m_emptyLabel` font: 14px → 15px.
- `m_loadingLabel` font: 14px → 15px.
- Empty / loading page top padding bumps to accommodate the new outer 12/12/12/12 margins.
- Retry + Clear buttons grow from 28px → 32px height (mid-tier; they're slightly smaller than the primary controls by design).
- Loading bar uses `__ACCENT__` per TY.1.

### 5.3 TankoLibraryPage

#### TL.1 — Search row consolidation: 9 → 4

Replace the current search row with:

```
[ Query (stretch=1) ]  [ Search ]  [ Cancel (hidden) ]  [ Filters ▾ ]  [ Sort: <current> ▾ ]
```

The five filter controls (`m_epubChk`, `m_pdfChk`, `m_mobiChk`, `m_englishOnlyCheckbox`, `m_audioFormatCombo`) move into a popover opened by the Filters button.

**Filters popover layout** (a `QFrame` shown as a popup via `setWindowFlags(Qt::Popup)` or a custom `QMenu` subclass with widget actions):

```
+-----------------------------+
|  Format                     |    <- shown when Books tab active
|   [x] EPUB                  |
|   [ ] PDF                   |
|   [ ] MOBI                  |
|                             |
|  Language                   |
|   [x] English only          |
|                             |
|  Audiobook format           |    <- shown when Audiobooks tab active
|   [ All formats         v ] |    <-   (mutex with Format section)
+-----------------------------+
```

`applyMediaTabFilterVisibility()` toggles which sections are visible inside the popover based on the active media tab. Same logic as today — just relocated.

**QSettings keys preserved** (`tankolibrary/format_epub`, `tankolibrary/format_pdf`, `tankolibrary/format_mobi`, `tankolibrary/english_only`, `tankolibrary/audio_format`) — no migration needed; only the parent widget moves.

**Sort combo stays inline** (consulted often during result scan).

#### TL.2 — Filters button dot indicator

The Filters button shows a small monochrome dot in its top-right corner when any filter is in non-default state.

Computation:
```
bool anyFilterActive =
       !m_epubChk->isChecked()                  // default ON: off = active
    || m_pdfChk->isChecked()                    // default OFF: on = active
    || m_mobiChk->isChecked()                   // default OFF: on = active
    || !m_englishOnlyCheckbox->isChecked()      // default ON: off = active
    || (m_audioFormatCombo->currentIndex() != 0); // default 0: non-zero = active
```

Dot rendering: a 5px circle in `__ACCENT__` painted as an overlay child widget at top-right, or via QSS using `qproperty-iconSize` with a small composited indicator icon. Simpler approach: a `QLabel` child of the Filters button positioned via `move()` at `(width - 8, 4)` and shown / hidden on filter change.

Connect filter-state slots (`onFormatFilterToggled`, `onEnglishOnlyToggled`, `onAudioFormatChanged`) to recompute the dot.

#### TL.3 — Hardcoded gold `#c7a76b` → `__ACCENT__` token

Four sites in `TankoLibraryPage.cpp`:

1. `makeAuthorLabel()` line 67-70 — `color: #c7a76b`.
2. `m_downloadButton` hover border line 793 — `border-color: #888` (currently). Hover variant should pick up `__ACCENT__`.
3. `m_downloadProgress` chunk line 810 — `background: #c7a76b`.
4. `m_detailBackBtn` text color line 705 — `color: #c7a76b`.

Implementation pattern (consistent across all four):
- At construction time, read `Theme::currentPalette().accent` and apply via `setStyleSheet()` with the accent value templated in.
- Connect to `Theme::paletteChanged` signal; on fire, rebuild the stylesheet with the new accent.
- Defensive fallback: if Theme module is not yet initialized at construct time (unlikely but possible during static init order issues), retain `#c7a76b` literal as the fallback color.

This ensures TankoLibrary's accent elements follow whichever palette is active (Dark gold, Nord frost, Solarized accent, etc.).

#### TL.4 — Empty / loading / zero-results states

Apply CR.4 pattern. Tankoyomi's pattern adapts cleanly because TankoLibrary already has an inner `QStackedWidget` (`m_resultsInnerStack`) — extend it with three more pages at indices 2/3/4 (currently 0 = grid, 1 = transfers).

Page-specific copy:
- **Books tab pre-search hint:** `"Type a query and hit Enter — e.g. 'sapiens' or 'orwell 1984'"` (current placeholder text — reuse).
- **Audiobooks tab pre-search hint:** `"Type a query and hit Enter — e.g. 'stormlight archive' or 'dune'"`.
- **Loading:** preserve the existing per-source status format `"Searching... (LibGen: searching..., AudioBookBay: 12)"` already in `refreshSearchStatus()` — render it inside the loading page instead of (or in addition to) the small status label.
- **Zero-results:** `"No results for '<query>' from <comma-list of active source names>."` + Retry + Clear.

Books vs Audiobooks media-tab switch resets to pre-search state (already happens in `setMediaTab()` via `m_grid->clearResults()`; extend to flip the inner stack to the empty page).

#### TL.5 — Page margins

Already `12/12/12/12` — matches CR.2. No change required.

### 5.4 Cross-pollination — downstream widgets

#### CV.1 — TransfersView (TankoLibrary)
- Audit `src/ui/pages/tankolibrary/TransfersView.{h,cpp}` for lowercase status string leaks. Apply Title Case rule.
- Apply alignment match rule (CR.9) — likely a 4-column structure similar to MangaTransfersTable.
- Apply hover row rule (CR.5).
- Apply theme-token routing (CR.3).

#### CV.2 — MangaResultsGrid + BookResultsGrid
- Density lift: cover tile internal padding 4 → 8; label font 11px → 12px on the tile.
- Theme token routing for tile background / border.
- Hover state: subtle border highlight on tile hover (1px solid `__BORDER_HI__`).

---

## 6. Theme integration map

Inline literals replaced by Theme.cpp tokens:

| Inline (current) | Theme token (target) | Palette field |
|---|---|---|
| `rgba(192,200,212,36)` (selection bg) | `__ACCENT_SOFT__` | `palette.accentSoft` |
| `#11` / `#111` (table base) | `__BG__` | `palette.bg0` |
| `#18` / `#181818` (table alt) | `__BG1__` | `palette.bg1` |
| `#1a1a1a` (header bg) | `__BG1__` | `palette.bg1` |
| `#888` (muted text) | `__FG_MUTED__` (NEW — see below) | `palette.fgMuted` (NEW field) |
| `#222` (border) | `__BORDER__` | `palette.border` |
| `#c7a76b` (hardcoded gold) | `__ACCENT__` | `palette.accent` |
| `#60a5fa` (blue loading bar) | `__ACCENT__` | `palette.accent` |
| `#eeeeee` (high-contrast text) | `__FG__` | `palette.fg` |

### 6.1 Theme.cpp coordination point

`__FG_MUTED__` token may not exist in Theme.cpp's current token set. **Before adding the token**, Agent 4B posts a HELP request / chat.md note to Agent 5 (theme owner) describing the need:

> Sources UI refresh needs `__FG_MUTED__` (palette `fgMuted` field) for: muted secondary text, table header text, dim-gray seeder count on dead torrents, dim-gray source / quality segments in Tankorent title delegate. Sensible values: Dark `#888` ; Light `#666` ; per other palettes Agent 5 picks. OK to add?

If Agent 5 declines or wants a different token name, this spec adapts — the names are mechanical; the requirement (one mid-tone muted-text token) is what matters.

---

## 7. Decisions ratified

The brainstorm produced ten decision points, all ratified by Hemanth same-session 2026-05-13 ~12:50–13:30 GMT+5:30.

1. **Scope:** Polish + tune density (B of three options).
2. **Tankorent columns:** Remove Files; keep Category but with human-friendly names ("Anime EN" not "1_2").
3. **Density:** Industry-standard lift (controls 36px, body 13px, row spacing 10, table rows 32px).
4. **Trust signal:** Bold seeder count on healthy rows; dim gray on dead rows. No row tint.
5. **TankoLibrary filters:** Sort inline; EPUB / PDF / MOBI / English / Audio format collapse into Filters popover. Filter button shows dot when any filter active.
6. **Tankorent search row 2:** No restructure. All 8 buttons stay (Refresh + Sources + Add URL + More + 4 combos).
7. **Empty / loading states:** Port Tankoyomi's pattern to all three pages.
8. **Tankorent title cell:** Tighter inline format `Nyaa · Title · Quality1 Quality2` with dim gray segments (custom delegate).
9. **Tab styles:** Leave inconsistent (Tankorent + Tankoyomi keep QTabWidget; TankoLibrary keeps pills).
10. **Polish micro-decisions:**
   - Tankoyomi columns: keep all 5.
   - View toggle: leave as-is (text-flip pattern).
   - Source picker order in Tankorent: keep current.
   - Hover row state: add subtle hover everywhere.
   - **Status string Title Case rule** (Hemanth-flagged via screenshot of Tankoyomi `queued` lowercase).
   - **Header / cell alignment match rule** (same screenshot).

---

## 8. Acceptance criteria

Hemanth-driven visual smoke per `feedback_hemanth_role_open_and_click.md` — launch via `build_and_run.bat`, click each surface, report what he sees.

### 8.1 TankorentPage

1. Results table shows 6 columns (no Files).
2. Category cells show friendly names ("Anime — English" not "1_2", etc.).
3. No green or red row tints visible anywhere.
4. Rows with ≥50 seeders: the seeder count appears bold.
5. Rows with <5 seeders: the seeder count appears in dim gray.
6. Title cell formats as `Source · Title · Quality` with dim gray prefix + suffix.
7. Link column shows SVG icons (download arrow + magnet) — no "M" or "↓" glyphs visible.
8. Search row 2 still shows Refresh, Sources, Add URL, More — all visible.
9. All search-row controls feel calmer (36px tall instead of 30px).
10. Body text reads larger (13px instead of 11–12px).
11. Empty pre-search state shows centered hint "Type a query and hit Enter…".
12. During search, centered loading bar visible.
13. Zero-results state shows the no-results message + Retry + Clear buttons.
14. Bulk-group Category cell reads "Videos" not "videos".
15. Every column's header alignment matches its cell alignment.
16. Hovering a row gives a subtle highlight (lighter than the selection background).
17. Switching the palette via topbar picker updates all theme-driven elements live.

### 8.2 TankoyomiPage

1. Loading bar is no longer blue — uses gold (or whichever palette accent is active).
2. Transfers Status column shows "Queued", "Downloading", "Paused", "Complete", "Failed" — no lowercase.
3. Transfers Status and Chapters columns: cells center-aligned, matching headers.
4. Transfers Series column: cells left-aligned, matching header.
5. More button shows SVG kebab icon — no `⋮` glyph.
6. Empty + zero-results state polish at new density (larger text, more breathing room).
7. Switching the palette updates the loading bar accent + selection + table backgrounds.

### 8.3 TankoLibraryPage

1. Search row shows `[Query] [Search] [Filters ▾] [Sort: ▾]` — 4 controls, no scattered checkboxes.
2. Click Filters → popover opens with EPUB / PDF / MOBI + English only + Audio format (Books or Audiobooks variant per active media tab).
3. Filters button shows a dot indicator when any filter is in non-default state.
4. Detail page author byline gold (theme accent).
5. Detail page Download button hover-border gold (theme accent).
6. Detail page download progress chunk gold (theme accent).
7. Back button text gold (theme accent).
8. Switching palette → all four accent surfaces update.
9. Books/Audiobooks pill row + Search Results/Transfers pill row both still visible.
10. Empty pre-search state shows centered hint with per-tab example ("sapiens" for Books, "stormlight archive" for Audiobooks).

### 8.4 Theme integration validation

1. Grep `#[0-9a-fA-F]{3,6}` across the three page `.cpp` files returns only allowed literals (icon hex `#cccccc`, defensive fallback gold, no other inline colors).
2. Each page's outermost widget has `setObjectName(...)`.
3. Every inline `setStyleSheet` call uses a `#ObjectName` selector (per `feedback_css_scoping.md`).

---

## 9. References

### 9.1 Memories consulted

- `feedback_tankorent_ui.md` — Category + Files column grievance (50 days old, finally landing here).
- `feedback_no_color_no_emoji.md` — strict gray / black / white + single gold accent rule.
- `feedback_simple_language.md` — communication style with Hemanth.
- `feedback_ui_grouping_by_intent.md` — group controls by intent class.
- `feedback_css_scoping.md` — always `#ObjectName`-scoped QSS.
- `feedback_qt_vs_electron_aesthetic.md` — Qt aesthetic limits, no QML migration.
- `feedback_qt_sizehintforrow_unreliable_pre_show.md` — hardcode row heights.
- `feedback_no_tables_simple_lists.md` — tables OK in spec docs (rule applies to chat responses only).

### 9.2 Theme infrastructure

- `src/ui/Theme.cpp` — palette tokens, `applyTheme()`, QSS template substitution.
- `src/ui/Theme.h` — palette struct, mode enum.
- Theme P1 + P2 shipped by Agent 5 (2026-04-25). P3 light-mode in progress.

### 9.3 Code files in scope

- `src/ui/pages/TankorentPage.{h,cpp}`
- `src/ui/pages/TankoyomiPage.{h,cpp}`
- `src/ui/pages/TankoLibraryPage.{h,cpp}`
- `src/ui/pages/tankolibrary/TransfersView.{h,cpp}`
- `src/ui/pages/tankoyomi/MangaResultsGrid.{h,cpp}`
- `src/ui/pages/tankolibrary/BookResultsGrid.{h,cpp}`
- `resources/resources.qrc`
- `resources/icons/` (target additions: `magnet.svg`, `kebab-menu.svg`)

### 9.4 Brainstorm transcript

Nine questions plus a four-question batch posed to Hemanth on 2026-05-13 12:50–13:30 GMT+5:30. Plus one Hemanth-initiated follow-up (screenshot of Tankoyomi Transfers showing `queued` lowercase + status / chapters column misalignment), folded back as rules CR.8 and CR.9. All decisions locked same-session.

---
