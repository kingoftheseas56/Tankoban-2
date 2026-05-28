# Books library context menu — Design spec

- **Date:** 2026-05-28
- **Author:** Agent 2 (Book Reader + TankoLibrary)
- **Arc tag:** `BOOKS_LIBRARY_CONTEXT_MENU`
- **Status:** Brainstorm complete — design approved by Hemanth 2026-05-28. Pending spec review → `/superpowers:writing-plans`.
- **Predecessor:** `BOOKS_FICTIONDB_CATALOGUE` (catalogue + series detail + metadata enrichment shipped 2026-05-28). Library grid renders owned-book tiles + series-group tiles (`BooksPage::rebuildBookGrid` → `addCatalogueRecordTile` / `addLibrarySeriesTile`).
- **Skills invoked:** `/superpowers:brainstorming`, `/hemanth-language`.
- **Companion follow-up:** Comics mode lacks the same library-grid context menu; a sibling arc applies the equivalent treatment to `ComicsPage` after this lands. Out of scope here (Books first, Hemanth 2026-05-28).

---

## 1. Problem

Books mode has a right-click context menu on the **Continue Reading strip** only (`BooksPage.cpp:709` — Continue reading / Mark read·unread / Clear / Rename / Reveal / Copy path). The **main library grid tiles** — owned-book tiles and series-group tiles — have **no** context menu. Users can only click (which opens the detail/series view); there's no quick path to read, rename, remove, or reveal a library item. Theatre/Stream has no richer per-tile menu to mirror, so this defines the Books pattern.

## 2. Decisions locked (brainstorm 2026-05-28)

- **D1 — Scope: library grid only.** Right-click acts on `m_bookStrip` tiles (owned books + series groups). Search results (`BookCatalogueSearchWidget`) and series detail rows (`BookSeriesDetailView`) stay click-only — they act through their existing Get/Read buttons. Not-yet-owned items get no menu.
- **D2 — Owned-book menu = full set, mirroring the Continue Reading menu** (consistency): Read · Mark as read/unread · Rename… · Remove from library · Reveal in File Explorer · Copy path.
- **D3 — Remove always asks first** — a 3-way dialog: *Remove from library only* / *Delete the file too* (danger) / *Cancel*.
- **D4 — Series tile menu** = Open series · Remove series (bulk, same remove semantics across all owned books of the series).
- **D5 — Reuse, don't invent.** Build on `ContextMenuHelper` (styled menu + reveal + copy + danger action) and the `BooksCatalogueLibraryStore` mutators. No new files, no new architecture; mirror the proven Continue Reading handler.

## 3. What the user experiences

**Right-click an owned book tile** (Books home grid) → styled dark menu:
- **Read** → opens the book in the reader (`emit openBook(filePath)`).
- **Mark as read / Mark as unread** → toggles the reader's `finished` flag (progress JsonStore, `"books"` domain, keyed by the path-hash). Label reflects current state. Marking read removes it from Continue Reading; marking unread brings it back.
- **Rename…** → input dialog; renames the file on disk **and** updates the library record's `filePath` so the record stays valid (the Continue Reading rename does not update the record — this fixes that gap for the grid path).
- **Remove from library** → the remove dialog (§3, D3).
- **Reveal in File Explorer** / **Copy path** → `ContextMenuHelper::revealInExplorer` / `copyToClipboard` (disabled if no file path).

**Right-click a series tile** (e.g. "Stormlight Archive") →
- **Open series** → `m_seriesDetailView->loadSeries(seriesId)` + switch to it (same as a click).
- **Remove series** → remove dialog worded for all N owned books of the series; applies the chosen remove semantics to every `catalogueIdsForSeries(seriesId)` member.

**The remove dialog** (`QMessageBox`, dark-styled):
- **Remove from library only** → `store->evictByCatalogueId(id)` (record dropped; file untouched on disk).
- **Delete the file too** → danger button; `QFile::remove(filePath)` then `evictByCatalogueId(id)`. Irreversible.
- **Cancel** → no-op.
- After any removal, the grid refreshes via the store's `recordsChanged` → `rebuildBookGrid`, and `refreshContinueStrip()` is called so a removed in-progress book also leaves Continue Reading.

## 4. Components & data flow

All changes live in **`BooksPage`** (`src/ui/pages/BooksPage.{cpp,h}`).

- **`m_bookStrip` gains `Qt::CustomContextMenu`** + a `customContextMenuRequested` handler (mirrors the `m_continueStrip` handler at `BooksPage.cpp:709`). The handler:
  1. `card = m_bookStrip->tileAt(pos)` — bail if null.
  2. Branch on the card's existing properties: `catalogueSeries` (series tile, set in `addLibrarySeriesTile`) vs `catalogueRecord` (owned-book tile, set in `addCatalogueRecordTile`).
  3. Build the appropriate menu via `ContextMenuHelper::createMenu(this)`. The destructive emphasis lives on the remove **dialog's** danger button, not on a menu item, so the menu items stay plain.
  4. `menu->exec(m_bookStrip->mapToGlobal(pos))`; dispatch on the chosen action.
- **Book-tile data:** the tile carries `catalogueId`; the handler looks up `store->recordFor(catalogueId)` for `filePath`, `title`, series membership. `finished` state read from `m_bridge->progress("books", progressKeyFor(filePath))`.
- **Series-tile data:** the tile carries `seriesId`; removal iterates `store->catalogueIdsForSeries(seriesId)`.
- **A shared private helper** `removeFromLibrary(QStringList catalogueIds, QString promptNoun)` shows the 3-way dialog once and applies the result to all ids — used by both the single-book and series paths so the dialog logic isn't duplicated.
- **Reused as-is:** `ContextMenuHelper`, `BooksCatalogueLibraryStore::{recordFor,evictByCatalogueId,catalogueIdsForSeries}`, `m_bridge->progress/saveProgress`, `progressKeyFor` (now backslash-normalized), `m_seriesDetailView->loadSeries`, `emit openBook`.

## 5. Error handling / edge cases

- **Missing file** (record exists, file deleted out-of-band): Read / Reveal / Copy disabled or no-op when `filePath` empty or `!QFile::exists`. Remove-library-only still works (drops the stale record).
- **Rename collision / file in use:** `QFile::rename` returns false → `QMessageBox::warning` (mirrors the existing Continue Reading rename), record left unchanged.
- **Series with one owned book:** "Remove series" dialog still applies; wording uses the actual owned count.
- **Empty/era tiles:** handler bails if `tileAt` returns null or the card lacks both type properties.

## 6. Testing

Primarily a UI surface — verified by live smoke, not unit tests (consistent with the Continue Reading menu, which has none):
- Right-click an owned book → each action: Read opens reader; Mark read removes from Continue Reading + flips label; Rename renames file + record (reopen still works); Remove (library-only) drops tile but file persists; Remove (delete file) drops tile + file gone; Reveal opens Explorer; Copy path pastes the path.
- Right-click a series tile → Open series loads the page; Remove series clears all owned members (per chosen semantics).
- `build_check.bat` BUILD OK; full live smoke via `build_and_run.bat`.
- If a pure-logic seam emerges (e.g. the remove-id collection for a series), a small GoogleTest is optional but not required for v1.

## 7. Out of scope

- Comics library-grid context menu (sibling follow-up arc).
- Context menus on search results / series detail rows (D1).
- Multi-select / batch actions across arbitrary tiles (only series-group bulk remove is in scope).
- Drag-and-drop, keyboard context-menu key handling beyond Qt defaults.
