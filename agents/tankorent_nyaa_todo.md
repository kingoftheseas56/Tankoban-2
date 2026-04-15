# Tankorent — Nyaa-inspired Search UI Rework

Scope: Tankorent's **Search Results** surface only. Nyaa is reference for layout/interaction patterns, not a literal port — we keep our multi-source aggregation, scraper architecture, and Qt6 widget toolkit. Reference doc: [`nyaa_search_reference.md`](nyaa_search_reference.md). Owner: Agent 4. One fix per rebuild.

Out of scope (this track): Transfers tab, Add Torrent dialog, indexer plumbing, anything in Tankoyomi or Stream. Same column shape may later inform those, but they're separate plans.

---

## Track A — Sortable column headers

Goal: Replace the explicit Sort combo with clickable column headers, the universal table interaction Nyaa relies on.

### A1. Click-to-sort on column headers
- **Files:** `src/ui/pages/TankorentPage.cpp`
- **Scope:** Wire `QHeaderView::sectionClicked` → re-sort `m_displayedResults` by that column's key. Stable-sort, ascending on first click of an inactive column, descending on second click of the active one. No QSettings persistence yet.
- **Nyaa parallel:** `<th class="sorting">` headers with `s` / `o` URL params.

### A2. Active-sort visual indicator
- **Files:** `src/ui/pages/TankorentPage.cpp`
- **Scope:** Use `QHeaderView::setSortIndicator(col, order)` so Qt paints the up/down arrow on the active header. Keep our own `m_sortCol` / `m_sortOrder` in sync with what the user clicked.
- **Nyaa parallel:** `sorting_desc` / `sorting_asc` CSS class swap on the active header.

### A3. Default sort = Date (newest first)
- **Files:** `src/ui/pages/TankorentPage.cpp`
- **Scope:** On first render of a fresh search, set `m_sortCol = (date column index)`, `m_sortOrder = DescendingOrder`, apply once before populating rows. Matches Nyaa's `id desc` default.

### A4. Remove the explicit Sort combo
- **Files:** `src/ui/pages/TankorentPage.h/.cpp`
- **Scope:** Delete `m_sortCombo` member + its construction in `buildSearchControls`. Header-click is now the sole sort surface — combo is redundant.
- **Depends on:** A1, A2 (must work before removing the combo).

### A5. Persist sort key + order across sessions
- **Files:** `src/ui/pages/TankorentPage.cpp`
- **Scope:** `QSettings` keys `tankorent/sortCol` + `tankorent/sortOrder`, restored in ctor, written on each header click.

---

## Track B — Row trust signal (row tint)

Goal: Replace the per-cell health dot with a full-row background tint, exactly like Nyaa's `success` / `default` / `danger` row classes. Faster to scan, frees up the Seeders column.

### B1. Compute trust class per row
- **Files:** `src/ui/pages/TankorentPage.cpp` (private helper)
- **Scope:** Add `static QString trustClass(const TorrentResult& r)` returning `"healthy"` / `"normal"` / `"poor"`. Heuristic: `seeders >= 50` → healthy, `seeders >= 5` → normal, else poor. Tunable constants.
- **No UI change yet** — pure helper, drives B2.

### B2. Apply row tint via stylesheet + dynamic property
- **Files:** `src/ui/pages/TankorentPage.cpp`
- **Scope:** On each row, set `item->setData(Qt::BackgroundRole, color)` per the trust class — tints `rgba(76,175,80,0.10)` (healthy), nothing (normal), `rgba(239,68,68,0.10)` (poor). Apply on every cell of the row to avoid stripe artifacts.
- **Depends on:** B1.

### B3. Drop the per-cell health dot
- **Files:** `src/ui/pages/TankorentPage.h/.cpp`
- **Scope:** Remove `healthDot()` / `healthColor()` static helpers and their use in the Seeders cell. Seeders column is now plain integers; the row tint carries the signal.
- **Depends on:** B2.

---

## Track C — Per-row affordances (Nyaa's Link column)

Goal: Surface the two most common actions inline on each row instead of always going through a context menu / dialog.

### C1. Quick-action column (download + magnet)
- **Files:** `src/ui/pages/TankorentPage.h/.cpp`
- **Scope:** New 70 px wide column "Link" between Title and Source. Two clickable icons per cell:
  - 📥 download — invokes the existing `onAddTorrentClicked(row)` flow
  - 🧲 magnet — copies the magnet URI to the clipboard (no dialog)
- Cells use a small `QWidget` with a `QHBoxLayout` of two `QToolButton`s, set via `setCellWidget`.
- **Nyaa parallel:** the dual-icon `Link` column.

### C2. Inline source badge in the Title cell
- **Files:** `src/ui/pages/TankorentPage.cpp`
- **Scope:** Prepend a small grey pill `[nyaa]` / `[1337x]` / `[piratebay]` to the Title text. Reuses `r.source`. Remove the dedicated Source column to reclaim width.
- **Nyaa parallel:** Nyaa's category icon plays this role; for us source-of-result is the equivalent identifying glyph.

### C3. Title cell tooltip
- **Files:** `src/ui/pages/TankorentPage.cpp`
- **Scope:** Set `item->setToolTip(0, r.title)` — long titles already elide, the tooltip exposes the full string on hover. Cheap.

---

## Track D — Result count & pagination feel

Goal: Tankorent currently shows whatever the scrapers returned in one big dump. Nyaa caps at 75 per page with a count line. We don't truly paginate (results are one-shot), but the count line + a soft-cap are a quality-of-life win.

### D1. Result-count line above the table
- **Files:** `src/ui/pages/TankorentPage.cpp`
- **Scope:** New `QLabel` between status row and tab widget reading `"Showing N results from M sources"` (M = how many scrapers returned ≥1 hit). Empty state and error state hide it.
- **Nyaa parallel:** `Displaying results 1-75 out of 1000 results.`

### D2. Soft cap + "show more" affordance
- **Files:** `src/ui/pages/TankorentPage.cpp`
- **Scope:** Render at most 100 rows by default; add a single `Show all N results` button at the bottom of the table that flips an `m_showAll` flag and re-renders. Rare to need >100 for a torrent search; the cap keeps the table snappy.
- **Depends on:** D1.

---

## Track E — Filter dropdown (Nyaa's `f` param)

Goal: One small filter widget that narrows results without re-searching the network.

### E1. Filter combo: All / High seed / No dead
- **Files:** `src/ui/pages/TankorentPage.h/.cpp`
- **Scope:** New `QComboBox m_filterCombo` in the search-controls row with three entries:
  - All (default)
  - High seed only (≥ 20 seeders)
  - Hide dead (≥ 1 seeder)
- Applied client-side in `renderResults()` after dedup, before sort. Persisted to `QSettings tankorent/filter`.
- **Nyaa parallel:** Filter dropdown (`No filter` / `No remakes` / `Trusted only`).

---

## Track F — Polish

### F1. Hover row highlight + alternating-row striping
- **Files:** `src/ui/pages/TankorentPage.cpp`
- **Scope:** The QPalette already has alternating colours for the table. Verify they survive the row-tint background from B2; if not, blend the tint into both alternates so striping stays visible underneath. Hover already fires via `QTableWidget`'s default `selectionBehavior` styling — confirm visible.

### F2. Date column hover tooltip = relative time
- **Files:** `src/ui/pages/TankorentPage.cpp`
- **Scope:** When populating the Date cell, set `item->setToolTip(col, humanRelative(timestamp))` (e.g. `"4 hours ago"`). Cell text stays the absolute date.
- **Nyaa parallel:** date `<td title="...">` shows relative on hover.

### F3. Right-click the column header → toggle column visibility
- **Files:** `src/ui/pages/TankorentPage.cpp`
- **Scope:** `QHeaderView::customContextMenuRequested` → menu with each column as a checkable entry. Persist visible-set to `QSettings tankorent/columns`.
- Lower priority than the rest; tucks away as power-user polish.

---

## Execution order

Independent within each track. Suggested ordering for the smallest user-visible deltas first:

| Batch | Track-item | Rebuild |
|-------|------------|---------|
| 1 | A1 — click-to-sort | yes |
| 2 | A2 — sort indicator arrow | yes |
| 3 | A3 — default sort newest first | yes |
| 4 | B1 + B2 — row tint (helper + apply) | yes |
| 5 | B3 — drop per-cell health dot | yes |
| 6 | A4 — remove sort combo | yes |
| 7 | A5 — persist sort to QSettings | yes |
| 8 | C1 — Link column with download + magnet | yes |
| 9 | C2 — inline source badge, drop Source column | yes |
| 10 | C3 — title tooltip | yes |
| 11 | D1 — result count line | yes |
| 12 | D2 — soft cap + show-all | yes |
| 13 | E1 — filter combo | yes |
| 14 | F1 — striping vs tint reconciliation | yes |
| 15 | F2 — date relative tooltip | yes |
| 16 | F3 — column visibility menu | yes |

Sixteen small batches. Tracks A and B are the structural rework; C through F layer affordances on top.

**No CMakeLists changes** anticipated for any batch — all work is inside `TankorentPage.h/.cpp`. Will flag in the per-batch chat.md entry if that turns out wrong.

**No shared-file touches** anticipated.

**No contract changes** anticipated.
