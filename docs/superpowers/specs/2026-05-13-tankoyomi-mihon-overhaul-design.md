# Tankoyomi Mihon Overhaul — Design Spec

- **Date:** 2026-05-13
- **Author:** Agent 4B (Sources)
- **Status:** Brainstorm ratified by Hemanth same-session. Implementation plan to follow via `/superpowers:writing-plans`.
- **Surface:** `src/ui/pages/TankoyomiPage.{h,cpp}` + `src/ui/pages/tankoyomi/MangaResultsGrid.{h,cpp}` (existing) + NEW widgets under `src/ui/pages/tankoyomi/` + `src/core/manga/MangaDownloader.{h,cpp}` (engine extensions) + `resources/icons/` (new SVGs as needed)
- **Reference:** `C:\Users\Suprabha\Downloads\Comic References\mihon-main\` (Mihon, Kotlin + Jetpack Compose)
- **Coordination boundary:** READER (Agent 1's ComicReader) explicitly out of scope. THEME (Agent 5) untouched. Tankorent / TankoLibrary / Stream / library-consumer pages untouched.

---

## §1 Goal

Hemanth verbatim from the Phase 1 prompt:

> "I want Tankoyomi to have a complete overhaul by referring to the download UI/UX in `C:\Users\Suprabha\Downloads\Comic References\mihon-main`. Agent 4B goes through Mihon's source, identifies the UI/UX architecture, brainstorms (superpowers) how to implement it in Tankoyomi, then superpowers writing-plans, then superpowers executing-plans."

This is a **Sources-side** architectural redesign — chapter discovery, chapter download state visibility, queue management, batch operations. The downstream comic reader stays Agent 1's domain; Tankoyomi delivers files to disk and `ComicsPage` picks them up via the existing scanner.

Hemanth's follow-up clarification mid-brainstorm:

> "I want clarity on how many chapters are downloaded, whether the download is happening, cancelling downloads, pausing them and unpausing them, selecting only a particular range of chapters for download and all these important matters."

> "Basically all the context menu options in Tankorent."

These promoted four items from "Phase 2 deferred" to "v1 essential" — per-series pause + resume, "Custom range..." modal, chapter-count visibility everywhere, and Tankorent-parity context menus on every surface.

---

## §2 Reference codebase summary

Mihon is Kotlin + Jetpack Compose + Android XML legacy. Cannot transliterate to C++/Qt directly. Architecture + UX patterns ported; idiomatic implementation rewritten.

Modules studied (paths relative to `C:\Users\Suprabha\Downloads\Comic References\mihon-main\`):

- **Download engine** — `app/src/main/java/eu/kanade/tachiyomi/data/download/`
  - `model/Download.kt` — per-chapter Download data class + 5-state State enum
  - `DownloadManager.kt` — public API: enqueue, cancel, delete, query, observe
  - `Downloader.kt` — worker with parallel-source + parallel-page concurrency
  - `DownloadStore.kt` — queue persistence across restarts
  - `DownloadCache.kt` — filesystem index for "is chapter X downloaded?" badge counts
  - `DownloadProvider.kt` — file path scheme + directory naming
  - `DownloadPendingDeleter.kt` — delayed deletion (read-then-clean)
  - `DownloadJob.kt` / `DownloadNotifier.kt` — Android WorkManager + system notifications (not portable)
- **Manga detail UI** — `app/src/main/java/eu/kanade/presentation/manga/`
  - `MangaScreen.kt` — top-level Composable
  - `components/MangaInfoBox.kt` — cover + meta + synopsis hero
  - `components/MangaChapterListItem.kt` — single chapter row composable
  - `components/ChapterDownloadIndicator.kt` — the per-chapter download circle (the key reference)
  - `components/MangaBottomActionMenu.kt` — bulk-action drawer for selected chapters
  - `components/MangaToolbar.kt` — top bar with overflow + download dropdown
- **Library UI** — `app/src/main/java/eu/kanade/presentation/library/`
  - `components/LibraryBadges.kt` — downloaded-count + unread + language badges
  - `components/CommonMangaItem.kt` — grid / list tile + badge slots
- **Browse / source UI** — `app/src/main/java/eu/kanade/presentation/browse/`
  - `BrowseSourceScreen.kt` — search-results within a source
  - `components/BrowseBadges.kt` — "In library" CollectionsBookmark badge
- **Settings / preferences** — `domain/src/main/java/tachiyomi/domain/download/service/DownloadPreferences.kt`

---

## §3 Per-slice architecture findings

### §3.1 Download manager / queue / store / provider

**State machine** (`Download.kt:66-72`): five states — `NOT_DOWNLOADED(0)` / `QUEUE(1)` / `DOWNLOADING(2)` / `DOWNLOADED(3)` / `ERROR(4)`. Per-`Download` object carries `pages: List<Page>?`, `progress: Int` (average across pages, 0–100), reactive `statusFlow` + `progressFlow` (debounced 50 ms).

**DownloadManager public API** (`DownloadManager.kt:36-446`):

- `downloadChapters(manga, chapters, autoStart)` — enqueue list
- `startDownloads()` / `pauseDownloads()` / `clearQueue()` — global control
- `startDownloadNow(chapterId)` — priority insert at queue front (Force Start analog)
- `cancelQueuedDownloads(downloads)` — remove from queue
- `deleteChapters(chapters, manga, source)` — delete from disk + cache, with read/bookmark filtering
- `deleteManga(manga, source, removeQueued)` — purge entire manga dir
- `renameSource` / `renameManga` / `renameChapter` — sync on title/source rename
- `getDownloadCount(manga)` — for library badge
- `isChapterDownloaded(...)` — cache lookup
- `statusFlow()` / `progressFlow()` — observable streams over the queue

**Downloader internals** (`Downloader.kt:70-732`):

- Parallel sources (`parallelSourceLimit`, default 5)
- Parallel pages within a chapter (`parallelPageLimit`, default 5)
- Retry exponential backoff: 2 s / 4 s / 8 s, 3 attempts per image (`Downloader.kt:503-512`)
- `splitTallImages` preference for webtoon-style long images
- `saveChaptersAsCBZ` preference: ZIP-archive each chapter folder
- ComicInfo.xml metadata written alongside each chapter
- Pending-delete tombstone via `DownloadPendingDeleter`

**Storage scheme** (`DownloadProvider.kt`):

- Root: `<downloads_dir>/<source_name>/<manga_title>/<chapter_dir>` (optional `.cbz`)
- Chapter dir name: `[scanlator_]<sanitized_chapter_name>_<6-char-md5(url)>` — hash suffix disambiguates duplicate-named chapters
- Filename sanitization via `DiskUtil.buildValidFilename`, optional non-ASCII disallow

**Cache** (`DownloadCache.kt`):

- ProtoBuf-serialized disk cache `dl_index_cache_v3` for boot speed
- Walks downloads tree at startup, caches `Map<sourceId, Map<mangaDirName, Set<chapterDirName>>>`
- 1-hour `renewInterval` for re-scan
- Mutex-guarded reads/writes
- `_changes` channel — observable invalidations for UI

### §3.2 Manga detail screen + per-chapter download indicator

**ChapterDownloadIndicator** (`ChapterDownloadIndicator.kt:42-282`) — the central widget. Five visual treatments mapped to `Download.State`:

- **NOT_DOWNLOADED** — outlined down-arrow inside a 26 dp icon area, 40 dp click target. Tap = `START`. Long-press = `START_NOW` (priority).
- **QUEUE** — indeterminate spinner with the arrow centered. Tap = open small menu (Start Now / Cancel). Long-press = `CANCEL`.
- **DOWNLOADING (progress > 0)** — determinate progress arc with `progress = animatedProgress / 100f`, stroke width = `IndicatorSize / 2`. Arrow color flips at 50%. Same tap semantics as QUEUE.
- **DOWNLOADED** — `Icons.Filled.CheckCircle`. Tap = open menu (Delete). Long-press = same menu.
- **ERROR** — `Icons.Outlined.ErrorOutline`, error-color tint. Tap + long-press = `START` (retry).

**MangaChapterListItem** (`MangaChapterListItem.kt:48-183`) — single chapter row:

- Padded row (start = 16 dp, top = 12 dp, end = 8 dp, bottom = 12 dp), Column (weight 1f) for title + subtitle | `ChapterDownloadIndicator` on right
- Title row: filled-circle unread dot (if not read), bookmark icon (if bookmarked), chapter title (ellipsised)
- Subtitle row: date • read-progress • scanlator (dot-separated)
- `SwipeableActionsBox` wrapper for configurable left + right swipe actions
- `combinedClickable`: tap = open chapter, long-press = enter selection mode

**MangaBottomActionMenu** (`MangaBottomActionMenu.kt:69-179`) — bulk-action drawer:

- Animated `expandVertically` from bottom; rounded-top surface
- Up to seven conditional buttons: Bookmark / Remove Bookmark / Mark Read / Mark Unread / Mark Previous Read / Download / Delete
- Each button has long-press-to-confirm with `delay(1.seconds)` auto-dismiss

### §3.3 Library screen — badges + queue indicators

**LibraryBadges** (`LibraryBadges.kt:13-48`):

- `DownloadsBadge(count: Long)` — numeric pill, tertiary color, shown when count > 0
- `UnreadBadge(count: Long)` — numeric pill, primary color
- `LanguageBadge(isLocal, sourceLanguage)` — folder icon or uppercase language code

**CommonMangaItem** (`CommonMangaItem.kt`):

- `MangaCompactGridItem` — cover with title overlaid on bottom gradient
- `MangaComfortableGridItem` — cover above, title below
- `MangaListItem` — 56 dp row: cover + title + `BadgeGroup`
- All three accept `coverBadgeStart` (top-left corner) and `coverBadgeEnd` (top-right corner) slots
- "Continue reading" `Icons.Filled.PlayArrow` button at bottom-right of cover

### §3.4 Source / browse / search screen

**BrowseSourceScreen** (`BrowseSourceScreen.kt:39-118`):

- `LazyPagingItems<StateFlow<Manga>>` driving pagination
- Three states based on `LoadState`: Loading (no items + refresh=Loading) → `LoadingScreen`; Empty → `EmptyScreen` with actions Retry / WebView / Help; Error after items loaded → snackbar with retry
- Three display modes via `LibraryDisplayMode`: CompactGrid / ComfortableGrid / List

**BrowseBadges** (`BrowseBadges.kt`) — single `InLibraryBadge(enabled: Boolean)` rendering `Icons.Outlined.CollectionsBookmark` when manga is in library.

### §3.5 Settings around download paths + naming + retention

**DownloadPreferences** (`DownloadPreferences.kt`):

- `downloadOnlyOverWifi: Boolean` (default true)
- `saveChaptersAsCBZ: Boolean` (default true)
- `splitTallImages: Boolean` (default true)
- `autoDownloadWhileReading: Int` (default 0)
- `removeAfterReadSlots: Int` (default −1)
- `removeAfterMarkedAsRead: Boolean`
- `removeBookmarkedChapters: Boolean`
- `removeExcludeCategories: Set<String>`
- `downloadNewChapters: Boolean`
- `downloadNewChapterCategories` / `…Exclude: Set<String>`
- `downloadNewUnreadChaptersOnly: Boolean`
- `parallelSourceLimit: Int` (default 5)
- `parallelPageLimit: Int` (default 5)

---

## §4 Tankoyomi current state — per-slice gaps

### §4.1 Download engine — substantial but flat

`MangaDownloader` (`src/core/manga/MangaDownloader.h:50-122`) has the right shape:

- Five chapter states using string-typed `status`: `"queued"` / `"downloading"` / `"completed"` / `"error"` / `"cancelled"`
- `MangaDownloadRecord` (series-level) holds `chapters: QList<ChapterDownload>`
- Public API: `startDownload`, `cancelDownload(id)`, `cancelAll()`, `removeDownload(id)`, `removeWithData(id)`, `pauseAll()`, `resumeAll()`, `isPaused()`, `moveSeriesToTop(id)`, `moveSeriesToBottom(id)`, `reorderChapters(id, orderKey, ascending)`
- Persisted to JSON via `JsonStore` (`manga_downloads.json` + `manga_history.json`)
- Signals: `downloadUpdated(id)`, `downloadCompleted(id)`, `pausedChanged(paused)`
- `MAX_CONCURRENT_CHAPTERS = 2`, `MAX_IMAGE_RETRIES = 3` (2 s / 4 s / 8 s — matches Mihon)

**Gaps vs Mihon:**

- No per-series pause (only global)
- No "start chapter now" / priority insert at chapter granularity
- No `restartSeries` (clear error states + re-engage queue)
- No `retryFailedChapters` at series granularity
- No `countDownloadedForSeries` / `countByState` queries
- Per-chapter state observability is "walk the record's chapter list on each `downloadUpdated(id)`" — workable, but consumer has to find the changed row itself

### §4.2 Manga detail screen — does not exist

Tankoyomi today has no embedded detail surface. Double-click on a search result opens `AddMangaDialog` (`src/ui/dialogs/AddMangaDialog.{h,cpp}`) — a modal pop-up with chapter checkboxes, destination path picker, format combobox, OK / Cancel.

`TankoyomiPage::onResultDoubleClicked` (`src/ui/pages/TankoyomiPage.cpp:911-974`) wires the dialog: fetches chapters from the scraper, populates the dialog, and on Accept dispatches `m_downloader->startDownload(...)`. The dialog dismisses; the user is never shown that manga's chapter state again until they re-search.

There is no way to:

- See per-chapter download state after the dialog closes
- Cancel individual chapters mid-download (only series-level via Transfers tab)
- Re-trigger a download on a specific failed chapter
- Read a synopsis or any manga metadata after closing the dialog

### §4.3 Library screen — N/A (separation of concerns)

Tankoyomi has no library screen. Downloaded comics land in `~/Comics/` (or wherever the rootFolder is set) and `ComicsPage` (Agent 5's or Agent 1's territory) shows them. Mihon collapses both surfaces; Tankoban-2 keeps them split. Library-side affordances (downloads-count badge, in-library marker, continue-reading button) are downstream of this overhaul.

The one library-adjacent affordance Tankoyomi DOES have: `MangaResultsGrid::setInLibraryKeys()` (`src/ui/pages/tankoyomi/MangaResultsGrid.h:34`) overlays a tick badge on search-result tiles for manga with files on disk. Binary marker only.

### §4.4 Source / browse / search screen — exists, recently polished

`TankoyomiPage` (`src/ui/pages/TankoyomiPage.h`) is the Results-tab surface:

- Search controls: `m_queryEdit` + `m_sourceCombo` + `m_searchBtn` + `m_cancelBtn` + `m_refreshBtn` + `m_pauseBtn` + `m_moreBtn` + `m_viewToggleBtn` + `m_sortCombo`
- Status row: `m_searchStatus` + `m_downloadStatus`
- Two-tab `m_tabWidget`: Results + Transfers
- Results tab holds inner `m_resultsStack` (QStackedWidget): list table / grid / empty / loading
- `MangaResultsGrid` wraps Agent 5's `TileStrip` / `TileCard`
- Async cover cache (`ensureCover()` + `coverReady` signal)
- T1–T31 cosmetic polish (column alignment, status casing, color literal patches, density lift) landed earlier today

### §4.5 Settings around download paths + naming + retention

- Destination: `m_bridge->rootFolders("comics").first()` is the default; user overrides per-download in `AddMangaDialog` today
- Format: per-download "cbz" or "folder" via dialog combobox today
- No global download-preferences surface; no auto-download-new, no remove-after-read, no Wi-Fi gate, no concurrency-pref UI

---

## §5 Hemanth's picks (the brainstorm verdicts)

Locked same-session 2026-05-13 across seven AskUserQuestion exchanges plus two follow-up clarifications:

1. **Detail surface** — "Full screen, like Mihon." `AddMangaDialog` retired. Tankoyomi's Results tab gains an inner stack that swaps between search-results and an embedded manga detail view.
2. **Tap model on chapter download circle** — "Simpler: tap-cycles, no long-press." Tap on Not-downloaded = Start. Tap on Queued/Downloading = Cancel immediately. Tap on Downloaded = small confirm popover then Delete. Tap on Errored = Retry. No long-press affordances anywhere on the icon. Right-click on the row opens the broader context menu (see §6.4).
3. **Read state vocabulary in chapter rows** — "Download state only." Chapter row shows chapter number, name, date, scanlator (if any), download circle. No unread dot, no bookmark icon, no "page 12/24" progress text. Read state stays Agent 1's domain.
4. **Multi-select model** — "Shift-click multi-select (desktop-native)." Click selects, Shift-click extends range, Ctrl-click toggles. A thin action bar appears at the top of the chapter list while any rows are selected: "Chapters X–Y (N chapters) selected · [Download] [Delete] [Clear]" — range readout when contiguous, count-only when discontiguous.
5. **Transfers tab redesign** — "Card list grouped by manga." Each card holds one manga's queue subset with cover thumb, title, "N of M chapters" line, per-series controls (Pause / Resume / Cancel), and expandable chapter rows.
6. **Detail screen header** — "Cover + title + meta strip." Cover (left), title + author + status + source + chapter count (right), action row underneath (Download dropdown + ellipsis). No synopsis. No genres. No reading stats.
7. **Reader bridge** — "Pure download, no reader bridge in v1." Detail screen has no "Read latest" button. Clicking a downloaded chapter row does not open the reader. Reading happens in the Comics tab. Deferred — a future arc adds the reader bridge as a cross-agent coordination item.

Mid-brainstorm Hemanth promoted four items from "Phase 2 deferred" to "v1 essential":

8. **Chapter-count clarity at every level** — visible on search-result tile, detail screen meta strip, Transfers card, Transfers tab global status line.
9. **Per-series pause + resume** — first-class. Each Transfers card has a Pause / Resume toggle button. Global Pause / Resume stays on the tab top-bar. Engine extension required.
10. **"Custom range..." modal** — first-class. Detail screen Download dropdown gets a "Custom range..." action that opens a numeric From/To dialog. Range honors actual chapter list, skips already-downloaded, warns on empty range.
11. **Tankorent-parity right-click context menus** — across four surfaces: search-result tile, chapter row (state-aware), Transfers card, detail screen header. Full vocabulary in §6.4.

---

## §6 Proposed architecture for Tankoyomi

### §6.1 Page-level shape

`TankoyomiPage` keeps its 2-tab structure (Results + Transfers). The Results tab grows an inner stack:

- **Page A** — `m_resultsStack` (existing) holding list / grid / empty / loading / no-results sub-pages. Owns the search controls + status row above it.
- **Page B** — NEW embedded `MangaDetailView` widget.

Navigation: result activation → flip inner stack to Page B → back button on Page B → flip back to Page A, preserving search state. The outer `m_tabWidget` is untouched by this navigation; switching to Transfers and back preserves whichever inner page (A or B) was active.

### §6.2 New widget surface

All under `src/ui/pages/tankoyomi/`:

- **`MangaDetailView.{h,cpp}`** — embedded detail screen. Header block (cover + meta + action row), optional multi-select bar, chapter list `QTableWidget` (4 columns: chapter number, name, date, download indicator). Subscribes to `MangaDownloader::chapterUpdated` for live row updates. Holds the scraper.fetchChapters lifecycle internally.
- **`ChapterDownloadIndicator.{h,cpp}`** — custom-painted widget; subclasses `QWidget`, overrides `paintEvent`. Five visual states drawn programmatically. 28 px outer, 22 px content. Emits `clicked()`; exposes `setState(State)` + `setProgress(int 0-100)`. Reused on chapter rows in both `MangaDetailView` AND inside `TransferGroupCard`.
- **`TransferGroupCard.{h,cpp}`** — single-series card. Header: cover thumb + title + state label + progress bar + Pause-toggle + Cancel-series X. Body: expandable list of chapter rows, each with name + state label + `ChapterDownloadIndicator`. Move-to-top / Move-to-bottom buttons on the left edge (v1; full drag-to-arbitrary-position deferred).
- **`ChapterRangeDialog.{h,cpp}`** — small modal for "Custom range...". Two `QSpinBox` inputs (From / To), live preview "N chapters will be downloaded", Cancel + Download buttons. Validates against actual chapter list.

### §6.3 Engine extensions (`MangaDownloader`)

Promoted from Phase 2 deferred to v1:

- `pauseSeries(QString id)` / `resumeSeries(QString id)` / `isSeriesPaused(QString id) const` — per-series pause control. Adds `paused` flag to `MangaDownloadRecord`; `processQueue` skips records with `paused == true`. Persisted in JSON.
- `restartSeries(QString id)` — walks the record's chapter list, resets all `"error"` / `"cancelled"` chapters to `"queued"`, clears `paused`, re-engages queue.
- `retryFailedChapters(QString id)` — restarts only `"error"` chapters of a series (does not touch `"cancelled"`).
- `startChapterNow(QString seriesId, QString chapterId)` — moves a specific chapter to the front of its series's per-chapter queue.
- `countDownloadedForSeries(QString seriesTitle, QString source) const → int` — counts `"completed"` chapters across active records AND history.
- `countByState() const → struct { int downloading; int queued; int doneToday; }` — for the Transfers tab status line. `doneToday` derived from chapters whose `completedAt` (history) is within the current day.
- New signal `chapterUpdated(QString seriesId, QString chapterId)` — emits when a single chapter's state changes. Consumers re-render only the changed row.
- Existing `cancelDownload(id)` / `removeWithData(id)` / `cancelAll()` / `moveSeriesToTop` / `moveSeriesToBottom` / `reorderChapters` reused as-is.

### §6.4 Context menu vocabulary (all four surfaces)

Lifted from Tankorent's vocabulary at `src/ui/pages/TankorentPage.cpp:1694+` (results table), `2392+` (transfers flat row), `2574+` (transfers bulk-group). Domain-mismatched items excluded per §6.6.

**Transfers card right-click menu:**

- Pause series / Resume series (toggle based on `isSeriesPaused`)
- Restart series
- Show in folder
- Expand details / Collapse details (toggle)
- Retry failed chapters (only when any chapter is errored)
- Move to top / Move to bottom
- Sort chapters by ▸ submenu: Chapter number ascending / Chapter number descending / Date ascending / Date descending
- Copy series title
- (separator)
- Cancel series (danger; keeps downloaded chapter files)
- Cancel + Delete files (danger; full purge)

**Chapter-row right-click menu — Not-downloaded state:**

- Start download
- Add to top of queue (priority — via `startChapterNow` after enqueue)
- (separator)
- Copy chapter URL
- Copy chapter name

**Chapter-row right-click menu — Queued / Downloading state:**

- Start now (jump queue) — calls `startChapterNow`
- Cancel chapter
- (separator)
- Copy chapter URL
- Copy chapter name

**Chapter-row right-click menu — Downloaded state:**

- Open folder (parent directory)
- Show file in folder (Windows: `explorer.exe /select,<path>`)
- (separator)
- Copy chapter URL
- Copy chapter name
- (separator)
- Delete from disk (danger; confirm popover)

**Chapter-row right-click menu — Errored state:**

- Retry download
- Cancel
- (separator)
- Copy error message
- Copy chapter URL

**Search-result tile right-click menu:**

- Open detail screen (primary; also double-click)
- Quick add all chapters — bypasses detail screen, fetches chapters via scraper, enqueues all
- (separator)
- Open source page in browser
- Show in library folder (only when any chapter of this series is on disk)
- (separator)
- Copy title
- Copy source URL

**Detail screen header overflow (`...`) menu:**

- Refresh chapter list (re-fetch from scraper)
- Open source page in browser
- Show series folder (only when any chapter downloaded)
- (separator)
- Copy series title
- Copy source URL

**Transfers tab top-bar — context-free buttons (not a menu):**

- [Pause all] / [Resume all] (toggle based on global `isPaused`)
- [Cancel all] (danger; confirmation popover)

### §6.5 Visibility of chapter counts

Three locations, all sourced from the new `countDownloadedForSeries` engine API:

- **Search-result tile** — when count > 0, render numeric badge "N ch saved" at top-right corner of cover (replaces binary tick). When count == 0, no badge.
- **Detail screen header** — meta strip's second line: "{total} chapters · {downloaded} downloaded" (e.g. "181 chapters · 47 downloaded").
- **Transfers card** — header line: "{state label} · {completedInGroup} of {totalInGroup} chapters" — totals are within this queue group, not the whole series.

Global status line on Transfers tab top: "{downloading} downloading · {queued} queued · {doneToday} done today" — sourced from `countByState`.

### §6.6 Items NOT lifted from Tankorent (explicit domain-mismatch list)

- **Speed Limits** — manga uses simple HTTP fetches; no peer-protocol throttle.
- **Seeding Rules** — no peer-protocol.
- **Force Recheck** — HTTP-image downloads either succeed or fail per image; the existing 3-attempt exponential backoff is the analog.
- **Force Reannounce** — no tracker.
- **Sequential Download** — replaced by "Sort chapters by ▸" submenu (chapter_number ascending = sequential).
- **Force Start / Cancel Force Start** — replaced by "Add to top of queue" / "Start now (jump queue)" — same priority semantics but state isn't sticky.
- **Copy Info Hash** — replaced by Copy chapter URL / Copy source URL.
- **Properties... / View Files...** — no separate dialog; detail screen already shows everything we need.
- **Set Location...** — destination locked to comics root folder per locked design.
- **Visible columns header toggle** — overkill for a 4-column chapter table.

### §6.7 AddMangaDialog retirement

`src/ui/dialogs/AddMangaDialog.{h,cpp}` are removed from the build. Source files `git mv`'d to `agents/_archive/dialogs/` for one-revert rollback per project convention. All `m_downloader->startDownload(...)` call sites move into `MangaDetailView`. The retirement is complete — no thin shim, no compatibility path.

---

## §7 State machines + flows in detail

### §7.1 Per-chapter download state machine

Five states mirroring Mihon's `Download.State` enum at `Download.kt:66-72`:

- **NotDownloaded** — chapter has no record (or record exists with status `"cancelled"` AND no file on disk). Visual: hollow outlined down-arrow.
- **Queued** — `ChapterDownload::status == "queued"`. Visual: indeterminate spinner with centered arrow.
- **Downloading** — `status == "downloading"`. Visual: determinate progress arc derived from `downloadedImages / totalImages`, centered arrow recolors at 50%.
- **Downloaded** — `status == "completed"` AND file exists on disk. Visual: filled grayscale check-circle.
- **Errored** — `status == "error"`. Visual: outlined error icon, error-color stroke.

Transitions:

- `NotDownloaded → Queued` — user taps icon, OR invokes context-menu "Start download", OR selects + bulk-Download: `MangaDownloader::startDownload(series, source, [chapter], dest, "cbz")`
- `Queued → Downloading` — engine picks up: `processQueue` advances; `chapterUpdated` emitted
- `Downloading → Downloaded` — last image written + CBZ archive complete: `downloadChapter` returns success; `chapterUpdated` emitted
- `Downloading | Queued → NotDownloaded` — user taps, cancel: `MangaDownloader` cancels in-flight HTTP, marks chapter `"cancelled"`, removes record; `chapterUpdated` emitted; row re-renders as NotDownloaded
- `Downloading → Errored` — 3 retries exhausted on an image, or HTTP non-recoverable: chapter marked `"error"`; `chapterUpdated` emitted
- `Errored → Queued` — user retries via tap or context menu: `restartSeries` / `retryFailedChapters` / per-chapter retry resets chapter to `"queued"`
- `Downloaded → NotDownloaded` — user invokes Delete from menu: confirm popover → `removeWithData` filtered to one chapter; file unlinked; `chapterUpdated` emitted

### §7.2 Per-series state for Transfers card display

Series-level state derives from the chapter aggregate. Card header label resolves in this priority order:

- All chapters cancelled → card auto-removes after 2 seconds
- Cancelled (partial) → state set; card stays visible until user dismisses
- Paused → if `isSeriesPaused(id)` returns true
- Errored → if any chapter in the card is errored AND no chapter is currently downloading
- Downloading → if any chapter is `"downloading"`
- Queued → if any chapter is `"queued"` (no chapter downloading yet)
- Completed → if all chapters in the card are `"completed"`

### §7.3 Detail view state machine

- **Idle** — initial state; chapter list blank or stale-cached
- **Loading** — `scraper.fetchChapters(seriesId)` in flight; loading spinner over chapter-list area
- **Ready** — chapter list populated; rows render with current download state
- **Error** — scraper emitted `errorOccurred`; error banner above chapter list with Retry button
- **Selection** — orthogonal sub-state; non-empty `m_selectedChapterIds` triggers multi-select bar

### §7.4 Flows

**Flow A — search → detail → single chapter:**

1. User types query, presses Enter
2. `m_scrapers` fan out; results populate `m_displayedResults`
3. User clicks result tile (or double-clicks list row)
4. `TankoyomiPage::showMangaDetail(result)` called
5. Inner stack flips to Page B (`MangaDetailView`)
6. Detail view enters Loading; `scraper.fetchChapters(result.id)` runs
7. On `chaptersReady`: chapter list populates, Detail view enters Ready
8. `MangaDownloader::countDownloadedForSeries(...)` queried for header "X downloaded" text
9. For each chapter, check active `MangaDownloadRecord` for matching `chapterId` → set initial state on `ChapterDownloadIndicator`
10. User taps a Not-Downloaded circle → `MangaDownloader::startDownload(seriesTitle, source, [chapter], dest, "cbz")` → indicator morphs Queued → Downloading-with-arc → Downloaded as `chapterUpdated` fires

**Flow B — search → detail → shift-click bulk:**

1. User in Detail view, clicks chapter 5 row → row highlights, multi-select bar appears: "Chapter 5 (1 chapter) selected"
2. User Shift-clicks chapter 15 row → rows 5–15 highlight; bar updates: "Chapters 5–15 (11 chapters) selected"
3. User clicks [Download] → `MangaDownloader::startDownload(...)` with all 11 chapters
4. Multi-select bar dismisses (selection cleared on action commit); 11 indicators morph to Queued in sequence

**Flow C — search → detail → "Custom range..." modal:**

1. User clicks Download-dropdown in detail header → menu: Download all / Next 5 / Next 10 / Next 25 / Custom range...
2. User picks "Custom range..." → `ChapterRangeDialog` opens, pre-filled with sensible defaults
3. User adjusts From/To; live preview updates: "47 chapters will be downloaded" (filtered to not-already-downloaded chapters in range)
4. User clicks [Download] → dialog closes → `MangaDownloader::startDownload(...)` with the filtered list

**Flow D — Transfers tab → per-series cancel:**

1. User clicks Transfers tab → cards stack vertically by `m_recordOrder`
2. Each card shows live progress as `chapterUpdated` and `downloadUpdated` fire
3. User right-clicks a card → context menu (§6.4)
4. User picks "Cancel + Delete files" → confirm popover → `removeWithData(id)` → card disappears, files removed

**Flow E — Transfers tab → per-series pause:**

1. User clicks Pause-toggle on a card → `pauseSeries(id)`
2. Engine sets `MangaDownloadRecord::paused = true`, persists to JSON
3. `processQueue` skips this record; in-flight chapter completes its current image then stops
4. Card state label updates to "Paused"; card recolors muted
5. User clicks Resume → `resumeSeries(id)` → flag cleared → `processQueue` picks up

**Flow F — Detail view → priority insert via context menu:**

1. User right-clicks a Queued chapter row → context menu (Queued/Downloading variant)
2. User picks "Start now (jump queue)" → `startChapterNow(seriesId, chapterId)`
3. Engine reorders that chapter to the front of its series's per-chapter queue
4. Next `processQueue` cycle picks it up before others

---

## §8 Information-architecture mockups (ASCII)

### §8.1 Search results — Page A (unchanged structurally, badge enriched)

```
+--------------------------------------------------+
|  Tankoyomi                                       |
|                                                  |
|  [Search...                  ] [Source v] [Go]   |
|  [Cancel] [Refresh] [Pause] [More] [List|Grid]   |
|                                                  |
|  Done: 24 from WeebCentral, 11 from RC           |
+--------------------------------------------------+
|  +------+  +------+  +------+  +------+          |
|  |      |  |      |  |      |  |      |          |
|  |cover |  |cover |  |cover |  |cover |          |
|  | 47ch |  |      |  | 12ch |  |      |          |
|  +------+  +------+  +------+  +------+          |
|  Promised  Berserk   Vinland   Solo              |
|  Neverland           Saga      Leveling          |
|  WeebCent  WeebCent  RC        WeebCent          |
+--------------------------------------------------+
```

`47ch` / `12ch` corners indicate downloaded-chapter count; missing badge = not yet downloaded.

### §8.2 Manga detail — Page B (Ready state)

```
+--------------------------------------------------+
| <- back  The Promised Neverland         ...      |
+--------------------------------------------------+
|                                                  |
|  [          ]   The Promised Neverland           |
|  [  cover   ]                                    |
|  [          ]   Kaiu Shirai                      |
|  [          ]   Ongoing · WeebCentral            |
|                 181 chapters · 47 downloaded     |
|                                                  |
|              [ Download v ]   [ ... ]            |
|                                                  |
+--------------------------------------------------+
| Chapters (181)                                   |
|                                                  |
| Ch 181  Final chapter                       (o)  |
| Ch 180  Reunion                             (v)  |
| Ch 179  The escape                     ((60%))   |
| Ch 178  Goodbye                             (Q)  |
| Ch 177  The truth                           (v)  |
| Ch 176  Threat                              (!)  |
| Ch 175  Hope                                (v)  |
| Ch 174  Departure                           (v)  |
+--------------------------------------------------+

Legend:
(o) NotDownloaded   ((arc)) Downloading
(Q) Queued          (v) Downloaded     (!) Errored
```

### §8.3 Manga detail — selection state

```
+--------------------------------------------------+
| <- back  The Promised Neverland         ...      |
+--------------------------------------------------+
|  [cover]   The Promised Neverland                |
|            181 chapters · 47 downloaded          |
|              [ Download v ]   [ ... ]            |
+--------------------------------------------------+
| Chapters 5-15 (11 chapters) selected             |
|       [Download]  [Delete]  [Clear]              |
+--------------------------------------------------+
| Chapters (181)                                   |
|                                                  |
|  Ch 181  Final chapter                      (o)  |
|  Ch 180  Reunion                            (v)  |
|*Ch 15  In the city                          (o)  |   <- shift-click end
|*Ch 14  Out the door                         (o)  |
|*Ch 13  Crisis                               (o)  |
|*Ch 12  The hunt                             (o)  |
|*Ch 11  Discovery                            (v)  |
|*Ch 10  Lullaby                              (o)  |
|*Ch  9  Family                               (o)  |
|*Ch  8  The wall                             (o)  |
|*Ch  7  Trust                                (o)  |
|*Ch  6  Departure                            (o)  |
|*Ch  5  First day                            (o)  |   <- click start
|  Ch  4  Birthday                            (v)  |
+--------------------------------------------------+

* = selected row (highlighted background)
```

### §8.4 Download dropdown + Custom range modal

```
[ Download v ]
  +----------------------------+
  | Download all                |
  | Download next 5             |
  | Download next 10            |
  | Download next 25            |
  | Custom range...             |
  +----------------------------+

[Custom range... clicked]
+----------------------------------+
|  Download range of chapters      |
+----------------------------------+
|                                  |
|  From chapter:  [    5  ]        |
|  To chapter:    [   25  ]        |
|                                  |
|  17 chapters will be downloaded  |
|  (4 already downloaded, skipped) |
|                                  |
|       [Cancel]   [Download]      |
+----------------------------------+
```

### §8.5 Transfers tab — card list

```
+--------------------------------------------------+
| Transfers                                        |
| 5 downloading · 12 queued · 8 done today         |
| [Pause all]  [Resume all]  [Cancel all]          |
+--------------------------------------------------+
|                                                  |
| +----------------------------------------------+ |
| | [cov]  The Promised Neverland         X      | |
| |        Downloading · 3 of 12 chapters        | |
| |        ==============-----------             | |
| |        [ Pause ]                             | |
| |   Ch 12  Downloading                ((34%))  | |
| |   Ch 11  Queued                        (Q)   | |
| |   Ch 10  Queued                        (Q)   | |
| +----------------------------------------------+ |
|                                                  |
| +----------------------------------------------+ |
| | [cov]  Berserk                        X      | |
| |        Paused · 7 of 50 chapters             | |
| |        ============-----------    (muted)    | |
| |        [ Resume ]                            | |
| |   Ch 50  Paused                        (Q)   | |
| +----------------------------------------------+ |
|                                                  |
| +----------------------------------------------+ |
| | [cov]  Vinland Saga                   X      | |
| |        Errored · 4 of 12 chapters            | |
| |        =========-------------                | |
| |        [ Retry failed ]                      | |
| |   Ch 8   Errored                       (!)   | |
| |   Ch 7   Queued                        (Q)   | |
| +----------------------------------------------+ |
+--------------------------------------------------+
```

### §8.6 Right-click context menus — examples

Right-click on a downloaded chapter row:

```
+----------------------------+
| Open folder                |
| Show file in folder        |
+----------------------------+
| Copy chapter URL           |
| Copy chapter name          |
+----------------------------+
| Delete from disk           |
+----------------------------+
```

Right-click on a Transfers card:

```
+----------------------------------+
| Pause series                     |
| Restart series                   |
+----------------------------------+
| Show in folder                   |
| Collapse details                 |
+----------------------------------+
| Retry failed chapters            |
+----------------------------------+
| Move to top                      |
| Move to bottom                   |
| Sort chapters by         ▸       |
+----------------------------------+
| Copy series title                |
+----------------------------------+
| Cancel series                    |
| Cancel + Delete files            |
+----------------------------------+
```

Right-click on a search-result tile:

```
+----------------------------------+
| Open detail screen               |
| Quick add all chapters           |
+----------------------------------+
| Open source page in browser      |
| Show in library folder           |   <- only when any chapter downloaded
+----------------------------------+
| Copy title                       |
| Copy source URL                  |
+----------------------------------+
```

---

## §9 Persistence + storage + naming proposals

### §9.1 Schema addition to `manga_downloads.json`

One new field on `MangaDownloadRecord`:

- `"paused": bool` — defaults to false; written when `pauseSeries(id)` called; read when card constructs to set initial pause state.

No other schema changes. Migration is non-destructive: legacy records without `"paused"` field default to false on load.

### §9.2 Storage path scheme — preserved as-is

Current Tankoyomi path: `{destinationPath}/{seriesTitle}/{chapterFolder_or_cbz}` where `destinationPath` defaults to `rootFolders("comics").first()`.

Mihon path: `{downloadsDir}/{sourceName}/{mangaTitle}/{chapterDir_or_cbz}` with chapter dir name = `[scanlator_]<sanitized_chapter_name>_<6-char-md5-hash>`.

**Tankoyomi keeps the existing scheme** — no source-name path segment, no md5 hash suffix on chapter names. Reasons:

- Existing downloaded series on disk would break if layout changed
- `ComicsPage` scanner has expectations about the directory tree shape
- Hash suffix is only needed when manga have duplicate chapter names — rare for WeebCentral / ReadComicsOnline

If a future scraper surfaces duplicate-named chapters, a `provider.getChapterDirName` extension can be added then.

### §9.3 Format default

CBZ by default. The existing `format` parameter on `startDownload(...)` stays in the API signature (back-compat for unknown callers), defaults to `"cbz"` if unspecified. Per-download format override is no longer exposed in the UI — when `AddMangaDialog` retires, no caller passes anything other than `"cbz"`.

A future arc can add a global preference (Mihon's `saveChaptersAsCBZ`) if Hemanth wants to flip it; for v1 it's hardcoded.

### §9.4 History file — unchanged

`manga_history.json` keeps its current shape. `countDownloadedForSeries` walks both active records AND history to assemble the count, so completed-then-archived series still contribute.

---

## §10 Scope boundaries — explicit list of what is NOT in this overhaul

### Cross-agent boundaries (do not touch)

1. **ComicReader (Agent 1's COMIC_READER_FIX_TODO domain)** — read state, bookmark, page-progress, reading session, mark-as-read.
2. **ComicsPage** (Agent 1 / Agent 5 — library surface).
3. **Theme.cpp palette work** (Agent 5) — Tankoyomi uses existing theme tokens.
4. **Tankorent / TankoLibrary / Stream pages** — sibling pages, untouched.
5. **Library-consumer pages** (Videos / Books / Audiobooks / Comics) — Agent 5.

### Mihon features explicitly NOT ported

6. **Reader integration** — no "Read latest" button, no click-downloaded-row-to-read.
7. **Read state in chapter rows** — no unread dot, no bookmark icon, no "page 12/24" progress text.
8. **Auto-download new chapters on schedule** (`downloadNewChapters`).
9. **Remove after read** (`removeAfterReadSlots`, `removeAfterMarkedAsRead`).
10. **Split tall images** (`splitTallImages`) — webtoon-specific preprocessing.
11. **Download only over Wi-Fi** — not applicable on desktop.
12. **Long-press affordances on download circle** — explicit tap-only model per §5.2.
13. **Swipe gestures on chapter rows** (Mihon's `SwipeableActionsBox`) — mobile pattern.
14. **Source-extension plugin architecture** — Tankoyomi has its own scraper layer.
15. **Bottom-drawer bulk action bar with long-press confirm** — replaced by top-bar shift-click multi-select.
16. **System notifications** (Android `DownloadNotifier`) — not applicable on desktop.
17. **ComicInfo.xml metadata file** — defer; existing scanner doesn't read it.
18. **Tracker integration** (MAL / AniList / etc.) — Tankoban has no tracker.
19. **Per-source download path override** — destination is global.
20. **Per-download format override** — CBZ-only per §9.3.

### Tankorent context menu items NOT lifted

21. Speed Limits, Seeding Rules, Force Recheck, Force Reannounce, Sequential Download, Force Start, Copy Info Hash, Properties..., View Files..., Set Location..., Visible columns header toggle. Full rationale in §6.6.

### Cosmetic polish completed previously (do not redo)

22. T1–T31 from `2026-05-13-sources-ui-refinement-design.md` already shipped — control heights, body text size, inter-row spacing, status string Title Case, color literal patches, MangaResultsGrid density. The Mihon overhaul builds on top of, does not duplicate, that polish.

---

## §11 Open questions deferred to Phase 2 (writing-plans)

Not user-visible decisions — engineering details to work through during plan authoring:

1. **`ChapterDownloadIndicator` paint implementation** — `QPainter` operations for each of the 5 states. Animation strategy for the progress arc (`QPropertyAnimation` on a `qreal` progress value?). Re-paint trigger source (subscribe to `chapterUpdated`?). 28 px outer / 22 px content. SVG-fallback considered if `paintEvent` gets unwieldy.
2. **`MangaDetailView` chapter list widget choice** — `QTableWidget` with custom delegate vs `QListView` with `QStyledItemDelegate` vs `QScrollArea` of stacked custom rows. Trade-offs: density, scroll perf for 200+ chapters, selection model integration, per-row context menu plumbing.
3. **`MangaDownloader` signal granularity** — current `downloadUpdated(id)` is series-level. Add separate `chapterUpdated(seriesId, chapterId)` and emit both at the right moments without doubling work. OR consolidate to one signal with nullable args. Pick during plan.
4. **Filesystem-verify pass** — if user manually deletes a chapter file outside the app, `MangaDownloader` still reports it as "completed". Decision: lazy-verify on detail-view enter (walk on-disk files vs records) OR explicit "Refresh" button only.
5. **Cover cache lifecycle** — current `ensureCover` is page-scoped. New detail view needs to: invalidate in-flight covers when user navigates back; share cache state between Results tile and Detail header; handle the `coverReady` signal landing after navigation.
6. **`startChapterNow` reorder semantics** — moves chapter to front of its series queue, but does it ALSO move the series to top of global queue? Mihon's `startDownloadNow` does both. Tankoyomi's per-series model needs explicit handling.
7. **Drag-to-arbitrary-position card reorder** — full drag-and-drop on `QListView` is non-trivial; v1 ships button-based (Move to Top / Move to Bottom in context menu). Full drag = future v1.1 arc.
8. **"Show file in folder"** Windows-specific implementation — `explorer.exe /select,<absPath>` via `QProcess`. macOS / Linux equivalents skipped (project is Windows-only).
9. **Animation polish** — multi-select bar slide-in/out; Transfers card collapse/expand; per-chapter row re-render flicker when state changes. `QPropertyAnimation` choices.
10. **`AddMangaDialog` archive path** — confirm `git mv` target during execution. Either `agents/_archive/dialogs/` or simple deletion with revert-as-recovery.

---

## §12 Risk + rollback notes

### §12.1 Risks

- **`ChapterDownloadIndicator` is the first state-machine custom widget in Tankoban-2** — expect 2–3 visual iterations before it lands clean. Mitigation: ship simplest `paintEvent` first (5 distinct icons), polish animations second. Reference SVGs at `resources/icons/{pause-circle,play-circle,checkbox-checked,checkbox-empty,download-arrow,retry-arrow}.svg` exist already (added during T26–T30 polish).
- **200-chapter list re-render perf** — naive "rebuild table on every `downloadUpdated`" hits visible jank. Mitigation: index rows by chapter ID, update only the changed row's indicator widget via `setState/setProgress`.
- **Cover-cache races across rapid detail navigation** — open detail, back, open another detail; in-flight cover requests for the first may land while showing the second. Mitigation: cancel cover requests on navigate-back; key cover assignments by `(source, id)` and ignore mismatched arrivals.
- **`MangaDownloader` JSON store schema migration** — adding `paused` needs to handle legacy records gracefully. Mitigation: default to false on missing field; next save normalizes.
- **Tests don't exist for `MangaDownloader`** — visual smoke is the only safety net. Mitigation: Phase 3 execution plan includes explicit smoke matrix (start, pause-series, resume-series, cancel-mid-flight, errored-retry, custom-range, multi-select bulk-cancel).
- **Engine extensions widen `MangaDownloader` API** — six new public methods + one new signal. Risk of regressions on existing call sites. Mitigation: extensions are additive; existing API preserved.
- **`AddMangaDialog` retirement might leave orphan callers** — if anything outside `TankoyomiPage::onResultDoubleClicked` references it, build will break. Mitigation: grep before deletion; archive path keeps source available if a missed caller surfaces post-merge.

### §12.2 Rollback strategy

- **Single-commit revertibility per phase.** Each phase (engine extensions / detail view / transfers redesign / context menus / dialog retirement) ships as one commit. Reverting any one returns Tankoyomi to the pre-phase state without cascading damage.
- **AddMangaDialog source preserved in `agents/_archive/dialogs/`** — if the new detail view falls flat in smoke, `git mv` brings the dialog back and the `onResultDoubleClicked` patch reverts.
- **JSON store backward compat** — adding `paused` is additive; older app versions reading the new JSON ignore unknown keys.
- **No file-on-disk layout change** — existing downloaded series stay readable by ComicsPage scanner; no migration script needed.

---

## §13 Suggested implementation phases (high-level — for Phase 2 plan)

Suggested phase boundaries for `/superpowers:writing-plans` to elaborate:

1. **Phase A — Engine extensions** (`MangaDownloader` API surface). Pure engine work, no UI. `pauseSeries` / `resumeSeries` / `restartSeries` / `retryFailedChapters` / `startChapterNow` / `countDownloadedForSeries` / `countByState` / `chapterUpdated` signal. JSON store migration for `paused` field.
2. **Phase B — `ChapterDownloadIndicator` widget**. Standalone, no integration. Unit-style smoke via a test page that flips through 5 states with a button.
3. **Phase C — `MangaDetailView` widget**. Wires up against existing search-result activation. AddMangaDialog still in tree but no longer triggered.
4. **Phase D — Multi-select + range modal + Download dropdown**. Shift-click on detail view; thin action bar; `ChapterRangeDialog` + dropdown wiring.
5. **Phase E — `TransferGroupCard` widget + Transfers tab redesign**. Replaces existing `m_transfersTable`.
6. **Phase F — Context menus** (all four surfaces). Tankorent-parity vocabulary.
7. **Phase G — `AddMangaDialog` retirement + cleanup**. `git mv` dialog source to archive, prune dead code.

Phase ordering picked so the user-visible surface evolves incrementally — Phase B–C alone are shippable as an MVP if Phase 2 plan budget gets tight.

---

## Brainstorm log

Seven AskUserQuestion exchanges (plus two follow-up clarifications) ratified the design same-session on 2026-05-13:

1. Sequencing of this overhaul vs the cosmetic Sources polish — answer: T1–T31 already done, clean canvas.
2. Detail surface shape — answer: full screen, retire AddMangaDialog.
3. Tap model on download circle — answer: simple tap-only.
4. Read state vocabulary in chapter rows — answer: download-state only.
5. Multi-select model — answer: shift-click desktop-native.
6. Transfers tab redesign — answer: card list grouped by manga.
7. Detail screen header — answer: cover + title + meta strip.
8. Reader bridge — answer: pure download, no reader bridge in v1.

Follow-up clarifications:

9. "I want clarity on how many chapters are downloaded, whether the download is happening, cancelling downloads, pausing them and unpausing them, selecting only a particular range of chapters for download" → promoted chapter-count visibility, per-series pause, "Custom range..." modal, and per-series cancel from "Phase 2 deferred" to "v1 essential".
10. "Basically all the context menu options in Tankorent" → §6.4 added with full menu vocabulary lifted from Tankorent (`TankorentPage.cpp:1694+`, `2392+`, `2574+`); domain-mismatched items excluded per §6.6.
