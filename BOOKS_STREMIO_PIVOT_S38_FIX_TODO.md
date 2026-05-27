# BOOKS_STREMIO_PIVOT §3.8 burn-the-ships backout — Fix TODO

## §1 Status

- **Phase cursor:** PENDING — F1 (full plan execution against `docs/superpowers/plans/2026-05-27-books-stremio-pivot-s38-burn-the-ships-backout.md`).
- **Owner:** Agent 2 (Book Reader + TankoLibrary).
- **Authored:** 2026-05-27 ~1:45pm IST.
- **Trigger:** Hemanth ratification 2026-05-27 ~1:02pm IST — *"We uphold the original specs always"* — restoring §3.8 burn-the-ships call from the 2026-05-20 brainstorm.

## §2 Scope

Rip `BooksScanner` (folder-walk discovery) and `BookSeriesView` (folder-tree archive view) out of Books mode per design spec §3.8 + §4.1 + §6.2. Rewire the library grid, Continue Reading strip, devSnapshot tree, and dispatcher commands to consume `BooksCatalogueLibraryStore` records exclusively.

Design contract: `docs/superpowers/specs/2026-05-20-books-stremio-pivot-design.md` §3.8 + §4.1 + §6.2.

Implementation plan: `docs/superpowers/plans/2026-05-27-books-stremio-pivot-s38-burn-the-ships-backout.md` (5 phases, ~25 tasks, scope-amended 2026-05-27 ~1:45pm IST after full BooksPage.cpp read).

## §3 Why this is its own arc, not part of the quota-bridge sweep

The bridge work (Agent 9 + Agent 7) kept the legacy folder-scan world alive alongside the new catalogue layer. The 2026-05-20 brainstorm explicitly axed that pattern. Honest scope after a full BooksPage.cpp read is ~600-800 LOC of churn across the page — too large to bundle with the bridge ratification sweep without compromising smoke quality.

Agent-2 ratified pacing (2026-05-27 ~1:45pm IST, Path 2 of 3 surfaced to Hemanth): ship §3.3 backout this wake; queue §3.8 backout as a focused next-wake arc with proper smoke discipline.

## §4 Surface in scope

**Modified:**
- `src/ui/pages/BooksPage.cpp` (1671 LOC; estimated ~600-800 LOC churn — delete scanner machinery + rewire grid/continue-strip/devSnapshot tree to catalogue-records)
- `src/ui/pages/BooksPage.h` (~20 LOC delete: forward-decls + scanner/series-view members + scanner-coupled slots)
- `src/devtools/SystemIntrospection.cpp` (1 comment removal)
- `src/ui/readers/BookBridge.h` (1 comment removal)
- `src/core/book/BookDownloader.cpp` (3 comment updates)
- `src/core/book/BooksCatalogueLibraryStore.h` (1 comment update)
- `CMakeLists.txt` (4 SOURCES/HEADERS entries removed)

**Deleted:**
- `src/ui/pages/BookSeriesView.cpp` + `BookSeriesView.h`
- `src/core/BooksScanner.cpp` + `BooksScanner.h`

**Cross-file verification (no edits expected):**
- `src/ui/MainWindow.cpp:267` — `connect(books, &BooksPage::openBook, this, &MainWindow::openBookReader);` — signal source changes to catalogue tile click; connect target unchanged.

## §5 Dispatcher command semantics (load-bearing decisions)

Five commands reference scanner state and need replacement or deprecation:

1. **`books_refresh_library`** — currently calls `triggerScan()`. Replace with `m_catalogueStore->validateAll()` + return validation summary (records-validated count + orphan count).
2. **`books_open_series`** — references `m_seriesFiles` + `m_seriesView->showSeries()`. Deprecate as v1.x; series-shape detail view is §5.3 deferred. Return `BAD_REQUEST` with code `DEFERRED_TO_V1X` until series-shape detail view ships.
3. **`books_get_series_state`** — references `m_seriesView->devSnapshot()`. Deprecate same as `books_open_series` — return `DEFERRED_TO_V1X`.
4. **`library_trigger_scan`** — currently calls `triggerScan()`. Replace with `m_catalogueStore->validateAll()` per v1.6 cross-mode contract.
5. **`library_reset_mode`** — currently calls `showGrid()`. Preserve as-is; no scanner coupling.

## §6 devSnapshot rewrites

- **`devSnapshot()`** — replace `hasScanned/scanning/rescanPending/seriesCount/progressEntries/seriesViewActive` fields with `catalogueRecordCount` + `validating` (transient from `validateAll` state flag) + `searchActive`.
- **`devLibrarySection()`** — replace `scan_state` block with `catalogue_state` block (records count + last-validate-at timestamp).
- **`devLibrarySnapshot()`** — replace `m_seriesFiles` walk with `m_catalogueStore->all()` walk. Each entry: catalogueId, title, author, year, filePath, readProgress, lastReadAt, addedAt.

## §7 buildUI surface to prune

- **Continue-strip context menu** (lines 696-777): "Open series" action references `m_seriesView->showSeries(...)` — delete the action.
- **Book-tile context menu** (lines 916-1028): entire menu is scanner-coupled (Open series, Continue reading from in-progress file walk, Mark as read all, Rename series, Hide series, Reveal in File Explorer, Copy path, Remove series folder). Most actions are folder-tier semantics that don't translate to catalogue records. Reframe per catalogue model: keep "Read" (if on disk), "Remove from library" (`m_catalogueStore->evictByCatalogueId`), "Open Library Page" (browse OpenLibrary URL), "Reveal file in Explorer" (if filePath populated). Net delete ~80 LOC + keep ~30 LOC.
- **`m_listView` (LibraryListView)** at lines 877-895 + 1217-1226: scanner-coupled. Delete entirely; grid view is the catalogue surface. List view is v1.x polish.
- **F5 shortcut** (line 1088): `triggerScan()`. Rewire to `m_catalogueStore->validateAll()` + emit `recordsChanged` to refresh grid.
- **`m_bookStrip->filterTiles(m_searchBar->text())`** (line 1015): local-library-filter leftover. Delete (§3.4 compliance bonus).

## §8 Continue Reading strip rewrite

Current `refreshContinueStrip()` walks `m_progressKeyMap` (populated by `addBookSeriesTile` from scanner). Rewrite to walk `m_catalogueStore->all()` filtered by `readProgress > 0 && readProgress < 1`, sorted by `lastReadAt` desc. Subscribe to both `recordsChanged` AND `recordReadStateChanged(catalogueId)`.

§3.10 series-aware subscript (*"Stormlight Archive · Reading Oathbringer · 62%"*) is deferred to v1.x — v1 ships file/progress only (`<author> · <progress%>`).

## §9 Verification gates

- **Build:** `build_check.bat` BUILD OK in shared `out/` lane (preferred) or `TANKOBAN_BUILD_LANE=agent2` isolated lane (if shared lane busy).
- **Smoke:** `build_and_run.bat` → Tankoban opens → `tankoctl open-page books` → `tankoctl books-get-library` returns empty library on first launch (no scanner output) → empty-state copy renders per §3.9 fallback.
- **Smoke evidence:** PNG capture to `agents/audits/smoke_evidence/books_stremio_pivot_s38_backout_<YYYY-MM-DD_HHMMSS>.png`.

## §10 Deferred items (folded into a separate v1.x consolidated fix-TODO post-§3.8-ship)

These slice-4 review items don't block §3.8 and queue as a separate Agent-2 v1.x backlog:

1. **§3.5 polish** — `kInitialCap = 6` + "Show N more" per-section overflow reveal in `BookCatalogueSearchWidget`.
2. **§5.3 series-shape detail view** — `BookCatalogueDetailView` currently movie-shape only. Series-shape needs hero + bulk download button + per-book table + RowState enum.
3. **§3.10 Continue Reading series-aware subscript** — *"Stormlight Archive · Reading Oathbringer · 62%"*.
4. **§3.9 empty-library quiet copy** — *"Search for books to add to library"* per spec wording.
5. **§5.2 full download path wiring** — clickable source rows → `BookDownloader::startDownload`/`startMagnetDownload` → `BooksCatalogueLibraryStore::upsertRecord` on completion → button morphs `[Search for downloads]` → progress bar → `[Read]`. Sources panel currently display-only (Agent 7 bridge intent).
6. **§5.3 series-shape catalogue detail enhancements** — bulk download fan-out + per-book RowState rendering.

## §11 Hemanth interaction surface

- Hemanth has already ratified §3.8 in this wake (2026-05-27 ~1:02pm IST: *"We uphold the original specs always"*). No new product/UX question expected during execution.
- Open potential question: whether to delete `m_listView` entirely or rewire to catalogue records. Agent-2 Rule-14 call (technical) — defaulting to delete; will only surface if execution reveals a UX concern.
- Final smoke evidence + RTC line published in chat.md for Hemanth visual confirmation.

## §12 Pacing + execution mode

- **Single agent (Agent 2), inline execution** per `feedback_plan_first_zero_errors.md` follow-on with plan already authored at the linked path.
- Estimated wall-clock: 1-2 hours for execution + build verify + smoke + RTC.
- Trigger-E parallel-tab not warranted (sequential delete-then-replace work where one task feeds the next).

## §13 Cross-agent coordination

- **Agent 0 (Coordinator):** sweep gating discussed in chat.md Path 2 coordination message. Agent 0 sweeps slice 1-3 files + `BookCatalogueDetailView.{cpp,h}` (with §3.3 backout applied) this wake. Holds `BooksPage.{cpp,h}` until §3.8 ships next wake.
- **Agent 4 (Stream + Tankorent):** indirectly relevant — the agent2-lane `TorrentEngine.cpp` libtorrent strong-typedef errors observed during §3.3 build verify (`tankoban_out_agent2/x64-windows/include/libtorrent/file_storage.hpp(524)`) are unrelated to this work but may affect §3.8 build verify if I'm forced into agent2 lane again. Flagged for awareness.
- **Agents 1, 3, 5:** untouched by this arc.

## §14 Skills + discipline expected

- `/superpowers:executing-plans` (already invoked at plan-execution start)
- `/superpowers:verification-before-completion` (per phase + final)
- `/build-verify` (after each phase + final shared-lane build)
- `/simplify` (final pass before RTC)
- `/superpowers:receiving-code-review` (Hemanth flag, in-flight)
- `/hemanth-language` (all status updates + RTC + smoke report)
- `/superpowers:systematic-debugging` if smoke surfaces unexpected behavior
