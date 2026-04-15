# Tankoyomi — Mihon-inspired UI/UX Improvements

Scope: Tankoyomi's **search** and **downloads** UI only. Mihon is reference for patterns, not code — their stack is Kotlin/Compose, ours is Qt6/C++ Widgets. Nothing about the comics library, reader, or read-tracking. No color. No emoji. One fix per rebuild.

Owner: Agent 4 (Stream & Sources).
References in `C:\Users\Suprabha\Downloads\mihon-main\mihon-main\`:
- `app/src/main/java/eu/kanade/tachiyomi/ui/download/DownloadQueueScreen.kt`
- `app/src/main/java/eu/kanade/presentation/browse/BrowseSourceScreen.kt`
- `app/src/main/java/eu/kanade/presentation/browse/components/BrowseSourceComfortableGrid.kt` (and siblings)
- `app/src/main/java/eu/kanade/presentation/manga/MangaScreen.kt`

---

## A. Downloads UI (queue control)

### A1. Downloader pause/resume core
- **Files:** `src/core/manga/MangaDownloader.h`, `src/core/manga/MangaDownloader.cpp`
- **Scope:** Add `bool m_paused`, `pauseAll()`, `resumeAll()`, `isPaused()`. In `processQueue()`, short-circuit when `m_paused` is true. In-flight requests complete normally; no new chapters start until resumed. Emit a signal on state change.
- **Mihon parallel:** `DownloadQueueScreenModel.pauseDownloads()` / `startDownloads()`, backed by `DownloadManager.isDownloaderRunning`.
- **Done when:** unit behaviour — toggling pause mid-download stops the next chapter from starting; resume continues where it left off. No UI yet.

### A2. Pause/resume UI button
- **Files:** `src/ui/pages/TankoyomiPage.h`, `src/ui/pages/TankoyomiPage.cpp`
- **Scope:** Add a single `QPushButton` in the search-controls row (label toggles "Pause Downloads" / "Resume Downloads"). Wire to `MangaDownloader::pauseAll/resumeAll`. Hide when there are no active downloads.
- **Mihon parallel:** `SmallExtendedFloatingActionButton` with Play/Pause icon and expanded state.
- **Depends on:** A1.

### A3. Cancel all active downloads
- **Files:** `src/core/manga/MangaDownloader.h/.cpp`, `src/ui/pages/TankoyomiPage.cpp`
- **Scope:** `MangaDownloader::cancelAll()` — iterate `m_records` and cancel each. Add a "Cancel All" action to an overflow menu button next to the Pause/Resume button. Confirmation dialog before firing.
- **Mihon parallel:** `AppBar.OverflowAction` with `action_cancel_all` + `screenModel.clearQueue()`.
- **Depends on:** none (A1/A2 independent).

### A4. Per-chapter status icons in MangaTransferDialog
- **Files:** `src/ui/dialogs/MangaTransferDialog.cpp`
- **Scope:** Replace the text status column with 12x12 painted icons via a `QStyledItemDelegate`: green check (completed), slate circle (queued), blue ring (downloading), red X (error), gray bar (cancelled). Keep the text as the accessible name / tooltip.
- **Mihon parallel:** `DownloadItem` icon column in the queue list.

### A5. Count pill in Transfers tab
- **Files:** `src/ui/pages/TankoyomiPage.cpp`
- **Scope:** Extend the existing `"Transfers (%1)"` label to `"Transfers · N series · M chapters pending"` — pull chapter-pending count from `MangaDownloadRecord::chapters` across all active records. No new widgets; just richer tab text.
- **Mihon parallel:** `Pill` next to the title showing `downloadCount`.

---

## B. Search UI (browse experience)

### B1. Cover fetching + caching for search results
- **Files:** `src/ui/pages/TankoyomiPage.h/.cpp`
- **Scope:** Add a `QNetworkAccessManager`-driven poster downloader (reuse the pattern from `StreamLibraryLayout` / `StreamSearchWidget`). Cache to `AppLocalDataLocation/data/manga_posters/{source}_{id}.jpg`. Populate on each search result, cache-hit skips network. No UI yet — just the plumbing. Add a signal `coverReady(source, id, path)`.
- **Mihon parallel:** `MangaCoverFetcher` with disk cache.

### B2. Search results grid widget
- **Files:** new `src/ui/pages/tankoyomi/MangaResultsGrid.h/.cpp`, CMakeLists.txt
- **Scope:** New `QWidget` that takes a `QList<MangaResult>` and renders them as `TileCard`s in a `TileStrip`-style flow layout. Shows cover, title below, source chip. Double-click emits `resultActivated(row)`. Listens to `coverReady` to update tiles as covers arrive. No wiring into TankoyomiPage yet.
- **Mihon parallel:** `BrowseSourceComfortableGrid` — cover on top, title below.
- **Depends on:** B1.

### B3. Grid/list view toggle
- **Files:** `src/ui/pages/TankoyomiPage.h/.cpp`
- **Scope:** Stack the existing `m_resultsTable` and the new `MangaResultsGrid` inside a `QStackedWidget`. Add a toggle button (two states: "Grid" / "List") next to the Search button. Persist choice to `QSettings` key `tankoyomi/resultsView`.
- **Mihon parallel:** `LibraryDisplayMode` (Compact Grid / Comfortable Grid / List) with persistence.
- **Depends on:** B2.

### B4. Empty state panel
- **Files:** `src/ui/pages/TankoyomiPage.cpp`
- **Scope:** When `m_displayedResults.isEmpty()` after a search, show a centered label: `"No results for \"{query}\""` in the grid pane and table pane. When the page is first opened (no search yet), show `"Search manga & comics above"`. Hide when results are present.
- **Mihon parallel:** `EmptyScreen` with message.

### B5. Search-in-flight loading state
- **Files:** `src/ui/pages/TankoyomiPage.cpp`
- **Scope:** While `m_pendingSearches > 0`, show an indeterminate spinner (or at minimum a "Searching {source}..." line per scraper) in place of the results area. Clear on completion. Keep the existing status label as a secondary info line.
- **Mihon parallel:** `LoadingScreen` during `LoadState.Loading`.

---

## C. Polish

### C1. Toast/snackbar for transient errors
- **Files:** new `src/ui/widgets/Toast.h/.cpp`, CMakeLists.txt, `src/ui/pages/TankoyomiPage.cpp`
- **Scope:** Small bottom-anchored floating label that appears for 3–4 seconds with an optional "Retry" button. Used for scraper errors ("Fetch failed — retry?") instead of a `QMessageBox` or the red inline text. Reusable widget — Tankorent/Stream could adopt later.
- **Mihon parallel:** `SnackbarHostState.showSnackbar(message, actionLabel, Indefinite)` with retry action.

### C2. Sort menu on search results
- **Files:** `src/ui/pages/TankoyomiPage.cpp`
- **Scope:** Small sort combo — "Relevance" (current, default), "Title A–Z", "Title Z–A", "Source". Applied client-side to `m_displayedResults` before rendering.
- **Mihon parallel:** `NestedMenuItem` sort dropdown with asc/desc variants.

### C3. Pre-download detail panel in AddMangaDialog
- **Files:** `src/ui/dialogs/AddMangaDialog.h/.cpp`
- **Scope:** Split the dialog vertically. Left column (260px): cover, title, author, source, status. Right column: existing chapter selection UI. Cover path comes from B1's cache. Author/status already in `MangaResult`.
- **Mihon parallel:** `MangaScreen` header with cover + metadata, chapter list below.
- **Depends on:** B1.

---

## Execution order

Independent tracks, pick any order within a track:

| Batch | Todos | Rebuild |
|-------|-------|---------|
| 1 | A1 — pause/resume core | yes |
| 2 | A2 — pause/resume button | yes |
| 3 | A3 — cancel all | yes |
| 4 | A5 — count pill | yes |
| 5 | B1 — cover fetcher | yes (no visible change, log verification) |
| 6 | B2 — grid widget (hidden) | yes |
| 7 | B3 — grid/list toggle | yes |
| 8 | B4 — empty state | yes |
| 9 | B5 — loading state | yes |
| 10 | A4 — chapter status icons | yes |
| 11 | C1 — toast | yes |
| 12 | C2 — sort | yes |
| 13 | C3 — detail panel | yes |

Thirteen small batches, each a self-contained change with one rebuild. Batches 1–4 are pure downloads-UX; 5–9 are pure search-UX; 10–13 are polish.
