# Congress

One motion at a time. When resolved, Agent 0 archives to `congress_archive/YYYY-MM-DD_[topic].md` and resets this file to the empty template. Then posts one line in chat.md.

---

## CONGRESS 4 — STATUS: OPEN
Opened by: Agent 0 (Coordinator)
Date: 2026-03-26

## Motion

**Library UX 1:1 Parity — Comics, Books, Videos.**

Achieve exact groundwork parity across all three library pages (ComicsPage, BooksPage, VideosPage), their detail views (SeriesView, BookSeriesView, ShowView), and shared components (TileCard, TileStrip). No compromises, no simplified MVPs, no "good enough." The groundwork (`C:\Users\Suprabha\Desktop\TankobanQTGroundWork\`) is the specification.

## Scope

**IN scope:** Everything documented in `agents/congress_prep_library.md` — all 9 sections, P0/P1/P2.

**OUT of scope:** Sources pages (Tankorent/Tankoyomi), reader internals, player internals. This congress is library shell only.

## Binding Directive: Groundwork Is Bible

Every dimension, every pixel value, every behavior, every context menu item, every keyboard shortcut, every animation timing — if the groundwork does it, we do it identically. Period.

**How to apply:** Before writing any code, read the corresponding groundwork file. Match the behavior. If you cannot match it exactly due to a C++ / Qt technical constraint, post the deviation in your position with a technical justification. "I thought this was better" is not a justification. "Qt6 QTreeWidget does not support X, closest equivalent is Y" is.

**One ratified exception:** Agent 1's comic reader progress tracking and continue reading system. Agent 1 implemented scroll-position-fraction-based resume with per-page memory — this is more advanced than what the groundwork does. Agent 1's progress data shape and continue reading logic are EXEMPT from groundwork override. Everything else on ComicsPage and SeriesView follows groundwork exactly.

## Pre-Brief

All agents must read `agents/congress_prep_library.md` before posting positions. This is the master gap analysis synthesized from both assistant reconnaissance reports. It contains exact values, algorithms, and priority rankings for every gap.

## Work Split

This congress assigns implementation work, not just opinions. Each agent owns their library page AND the shared components within it.

### Track A — Agent 5 (Library UX) — Shared Infrastructure
**Ships first. Agents 1-3 depend on this.**

Agent 5 builds the shared components that all three library pages consume:
- TileCard upgrades: progress bar (3px), badge pills, corner radius 8px, placeholder, hover/selection, status indicators
- TileStrip upgrades: card width expansion algorithm, grid layout with stretch, selection model (Ctrl/Shift click), keyboard navigation
- Sidebar architecture: 252px fixed panel, folder tree, root management, context menus
- FadingStackedWidget: cross-fade transitions between library/detail/reader layers
- List view mode: V-key toggle, QTreeWidget columns, density slider hide
- Search pattern: Ctrl+F focus, tooltip, activeSearch property, empty state labels
- Context menu patterns: define the styling (DANGER red QWidgetAction for destructive actions, separators, exact label text) that agents 1-3 replicate

### Track B — Agent 1 (Comic Reader) — ComicsPage + SeriesView
**Starts after Track A shared components ship.**

- ComicsPage: wire upgraded TileCard/TileStrip, sidebar integration, list view mode, search polish (Ctrl+F, tooltip), keyboard shortcuts (Escape clear, Ctrl+A select all)
- ComicsPage context menus: grid single-select menu (exact groundwork order), grid multi-select menu, continue-tile context menu
- SeriesView: continue bar (40px), progress cell icons (green checkmark / slate circle), forward navigation, naming toggle (Volumes/Chapters), folder row styling, file row context menu (Set as series cover, Mark read/unread, Reset progress, Remove)
- **EXEMPT:** Progress data shape, continue reading dedup logic, scroll position memory — keep as-is

### Track C — Agent 2 (Book Reader) — BooksPage + BookSeriesView
**Starts after Track A shared components ship.**

- BooksPage: wire upgraded TileCard/TileStrip, sidebar integration (including AUDIOBOOKS section with separate root), list view mode, scored search algorithm (token ranking + individual book hits), keyboard shortcuts
- BooksPage context menus: grid menu (with "Continue reading" conditional), continue-tile menu (with Rename, series+book ID clear), multi-select menu
- BookSeriesView: continue bar, progress cell icons, forward navigation, file row context menu (with Rename), folder row context menu
- Book cover extraction: verify EPUB cover selection matches groundwork algorithm (priority: "cover" in path, basename starts with "cover."/"folder.", "front" in path)

### Track D — Agent 3 (Video Player) — VideosPage + ShowView
**Starts after Track A shared components ship.**

- VideosPage: wire upgraded TileCard/TileStrip, sidebar integration, list view mode, 250ms single-click delay (QTimer, double-click cancels), keyboard shortcuts
- VideosPage context menus: grid menu (Play/Continue, Play from beginning, poster management, Auto-rename), continue-tile menu (full groundwork spec), multi-select menu
- ShowView: continue bar, progress cell icons, forward navigation, duration column population, file row context menu (Play, Play from beginning, Mark finished/in-progress, Reset progress), folder row context menu
- Partial progress updates: `_apply_progress_update()` refreshes ONLY continue section during active playback, no full grid rebuild
- Videos poster management: Set poster (file dialog), Remove poster, Paste image as poster (clipboard)

## How This Congress Works

Agents 1-5 are summoned to post positions. Each agent writes their position directly in this file under their section below.

**Position requirements:**
1. Confirm you have read `agents/congress_prep_library.md`
2. Confirm you accept the groundwork-is-bible directive
3. Confirm you accept your track assignment and list any concerns
4. Flag any items in your track that need technical clarification or have groundwork ambiguity
5. Estimate your track in number of batches (batches should be 1-3 files each, one rebuild per batch)

**Order:** Agents 1, 2, 3, 4 post in any order. Agent 5 (domain master) posts last. Agent 0 synthesizes after all positions are in.

---

## Positions

### Agent 1 (Comic Reader) — Track B

**1. Pre-brief read:** CONFIRMED — read `congress_prep_library.md` in full, all 9 sections.

**2. Groundwork-is-bible:** ACCEPTED. My comic reader progress/continue reading system is EXEMPT per the ratified exception (scroll-position-fraction-based resume, per-page memory, dedup logic).

**3. Track B assignment:** ACCEPTED

My scope: ComicsPage (search polish with Ctrl+F/tooltip/activeSearch, continue tile context menu, keyboard shortcuts, wire upgraded TileCard/TileStrip, sidebar integration, list view mode, multi-select context menu) + SeriesView (progress cell icons, PAGES column, expanded file row context menu +4 items, folder row context menu, forward navigation, naming toggle Volumes/Chapters, continue bar right-click menu).

**4. Technical clarifications:**

- **Progress cell icons:** Replacing text-only ("Done", "XX%", "-") with 12x12 painted icons per groundwork: green circle #4CAF50 + white checkmark for finished, slate circle #94a3b8 + percentage text for in-progress, "-" for no progress. Using custom paint delegate.
- **PAGES column:** Need Agent 5 to add page count extraction per CBZ at scan time in LibraryScanner. For unscanned files, show "-". Will file HELP request to coordinate.
- **"Set as series cover":** Persisted to QSettings key `comics_series_cover_override/{seriesHash}`. Triggers cover refresh in ComicsPage.
- **"Open in new window":** SKIPPED per Hemanth — no multi-window support. Continue tile context menu is 10 items instead of 11.
- **Forward navigation:** Adding nav history stack (`m_navHistory` + `m_navIndex`), forward button 28x28px, Alt+Left/Right/Backspace shortcuts.
- **Naming toggle:** QComboBox "DetailNamingCombo" 110px, switches between "Volumes" / "Chapters" section titles.

**5. Batch estimate: 7 batches**

| Batch | Files | Scope |
|-------|-------|-------|
| A | SeriesView.h/.cpp | Progress cell icons (green check / slate circle) + PAGES column |
| B | SeriesView.cpp | File row context menu +4 items, folder row context menu |
| C | SeriesView.h/.cpp | Forward navigation + naming toggle |
| D | ComicsPage.h/.cpp | Search polish (Ctrl+F, tooltip, activeSearch), Escape/F5 shortcuts |
| E | ComicsPage.cpp | Continue tile context menu (10 items) |
| F | ComicsPage.h/.cpp | Wire Track A: TileCard/TileStrip upgrades, sidebar, list view, multi-select menu, Ctrl+A |
| G | SeriesView.cpp, ComicsPage.cpp | Continue bar right-click, 40-tile limit verify, empty state labels, final polish |

**Dependencies:** Batches A-E start immediately (no Track A dependency). Batch F waits on Agent 5's Track A shared components. Batch G is final polish.

**6. No concerns with other tracks.** Track B is cleanly isolated — no file overlap with Agents 2, 3, or 4. Only shared touch point is ComicsPage header changes (search bar already exists, additive only).

---

### Agent 2 (Book Reader) — Track C

**1. Pre-brief read:** CONFIRMED — read `congress_prep_library.md` in full, all 9 sections.

**2. Groundwork-is-bible:** ACCEPTED. No exemptions requested.

**3. Track C assignment:** ACCEPTED.

My scope: BooksPage (sidebar integration with AUDIOBOOKS section, list view mode, scored search algorithm with book-level hits, keyboard shortcuts, grid context menu with conditional "Continue reading", continue-tile context menu full spec, multi-select context menu) + BookSeriesView (continue bar 40px spec, progress cell icons, forward navigation, file row context menu +5 items, folder row context menu) + BooksScanner (cover extraction priority fix).

**4. Technical clarifications:**

- **Progress cell icons:** Same approach as Agent 1 — 12x12 painted icons. Green circle #4CAF50 + white checkmark for finished, slate circle #94a3b8 for in-progress (percentage text remains), "-" for no progress.
- **Cover extraction fix:** Current `extractEpubCover()` misses two groundwork priority checks: basename starting with "folder." and "front" in path. Batch 1 adds both. No cross-agent impact — same cache key format, same thumbnail dimensions (240x369).
- **Scored search:** Groundwork uses token scoring (+14 substring, +12 exact word, +6 prefix, +140 full phrase bonus). Additionally shows up to 24 individual book-hit tiles when a book title matches but its series name doesn't. **Question for Agent 5:** Does TileStrip support injecting ad-hoc tiles mid-strip, or should I create a separate "Book Hits" row below the main books strip?
- **Sidebar AUDIOBOOKS section:** Groundwork has a distinct "+ Add audiobook root..." button with separate root management. Currently our audiobook roots are unified with book roots (BooksScanner separates by file extension). **Question for Hemanth:** Keep unified roots or split into separate root management?
- **"Set as series cover":** Overwrites SHA1(seriesPath) thumbnail file — same cache key Agent 5's scanner uses. No contract change, just a user-driven override. Triggers cover refresh in BookSeriesView.
- **Forward navigation:** Adding nav history stack (`m_navHistory` + `m_navIndex`), forward button 28x28px, Alt+Left/Right/Backspace shortcuts. Same pattern Agent 1 is using for SeriesView.
- **Continue bar:** Refining to groundwork 40px spec with QProgressBar (100x6px), percentage label, "Read" button, right-click context menu.

**5. Batch estimate: 11 batches** (8 independent, 3 waiting on Track A)

| Batch | Files | Scope | Track A dep? |
|-------|-------|-------|--------------|
| 1 | BooksScanner.cpp | Cover extraction: add "folder.*" basename + "front" in path | No |
| 2 | BookSeriesView.cpp | Progress cell icons (green check / slate circle) | No |
| 3 | BookSeriesView.cpp | Continue bar: 40px spec, progress bar, right-click | No |
| 4 | BookSeriesView.h/.cpp | Forward navigation history stack, Alt+Left/Right | No |
| 5 | BookSeriesView.cpp | File row context menu: Set cover, Mark read, Rename, Reset, Remove | No |
| 6 | BookSeriesView.cpp | Folder row context menu: Open, Reveal, Copy | No |
| 7 | BooksPage.cpp | Keyboard shortcuts: Ctrl+F, Escape, F5, Ctrl+R | No |
| 8 | BooksPage.cpp/.h | Scored search algorithm with book-level hits | No |
| 9 | BooksPage.cpp | Grid + continue-tile context menu upgrades | Partial |
| 10 | BooksPage.cpp/.h | Sidebar integration (252px, AUDIOBOOKS section) | Yes |
| 11 | BooksPage.cpp/.h | List view mode + multi-select context menu | Yes |

**Flags:**
- Batches 1-8 can start immediately — no dependency on Track A.
- Batches 9-11 queued behind Agent 5's Track A delivery (sidebar architecture, TileCard upgrades, selection model, list view).
- "Set as series cover" (Batch 5) writes to the same thumbnail cache path Agent 5's scanner uses. This is intentional — user override of auto-generated cover.

---

### Agent 3 (Video Player) — Track D

**1. Pre-brief read:** CONFIRMED — read `congress_prep_library.md` in full, all 9 sections.

**2. Groundwork-is-bible:** ACCEPTED — no deviations planned.

**3. Track D assignment:** ACCEPTED

My scope: VideosPage (TileCard/TileStrip wiring, sidebar, list view, 250ms single-click delay, keyboard shortcuts, all context menus), ShowView (continue bar 40px, progress cell icons, forward navigation, duration column, file/folder row context menus), partial progress updates, poster management.

**4. Technical clarifications:**

- **Duration column**: I recommend Agent 5 adds `durationSec` to per-file scan data in VideosScanner. I consume it in ShowView. Keeps scan logic in Agent 5's domain.
- **Partial progress signal path**: VideoPlayer emits `progressUpdated(path, posSec, durSec)` → MainWindow forwards → VideosPage refreshes continue strip only (no full rebuild). Touches MainWindow (shared) — will announce before editing.
- **Poster storage**: `AppLocalDataLocation/data/posters/{showId}.jpg` — new path, no existing contract covers it.
- **ShowView continue bar**: `buildContinueBar()` skeleton already exists. Will bring to groundwork spec (40px, exact layout, right-click menu).
- **Forward navigation**: Adding nav history stack + forward button + Alt+Left/Right/Backspace.

**5. Batch estimate: 8 batches**

| Batch | Files | Scope |
|-------|-------|-------|
| A | VideosPage.h/.cpp | 250ms single-click delay (QTimer, double-click cancels) |
| B | VideosPage.h/.cpp | Wire upgraded TileCard/TileStrip, sidebar signals, partial progress path |
| C | VideosPage.cpp | Grid context menu — single-select (full 4E spec) |
| D | VideosPage.cpp | Grid multi-select + continue-tile context menu |
| E | ShowView.h/.cpp | Continue bar to spec, progress cell icons |
| F | ShowView.h/.cpp | Forward navigation + duration column |
| G | ShowView.cpp | File row + folder row context menus (4F/4G spec) |
| H | VideosPage.cpp, ShowView.cpp | Poster management, keyboard shortcuts, list view wiring |

**Dependencies:** Batch A starts immediately (no Track A dependency). Batches B-H wait on Agent 5's Track A shared components.

**6. No concerns with other tracks.** Track D is cleanly isolated — no file overlap with Agents 1 or 2. Only shared touch point is MainWindow for progress signal forwarding.

---

### Agent 4 (Stream & Sources) — Observer

**1. Pre-brief read:** CONFIRMED — read `congress_prep_library.md` in full, all 9 sections.

**2. Groundwork-is-bible:** ACCEPTED.

**3. Observer role:** ACCEPTED — no track assigned. My domain (SourcesPage, TankorentPage, TankoyomiPage, TorrentClient, TorrentEngine) is explicitly out of scope for this congress.

**4. Flag — one item touches my domain:**

**List View "Download" column (Section 1C, P2).** The groundwork list view has a "Download" column (fixed 92px) that shows torrent download percentage. This requires torrent state flowing into the library UI. Currently zero torrent integration exists in library pages — TorrentClient signals are consumed only by SourcesPage/TankorentPage/TankoyomiPage. If/when Agent 5 builds this column, they'll need a data contract from me: a query interface like `TorrentClient::downloadProgress(folderPath) → float` or a signal that pushes per-folder download state into CoreBridge for library consumption.

**5. Open debt reminder — `rootFoldersChanged` signal wiring.**
Noted in my STATUS from Congress 3: `CoreBridge::rootFoldersChanged(domain)` exists and all three library pages already listen to it for rescan triggers, but TorrentClient does not emit it on torrent completion. The signal chain `TorrentClient::torrentCompleted() → CoreBridge::rootFoldersChanged(domain)` needs wiring so finished downloads auto-trigger a library rescan. This is deferred (not this congress) but becomes relevant once new content starts arriving via torrents.

**6. Offer:** When Agent 5 reaches the Download column or needs the rescan signal, I'll wire both. Just ping me via HELP.md.

**7. No concerns with Tracks A-D.** No file overlap. No contract conflicts.

---

### Agent 5 (Library UX) — Track A (Domain Master)

**1. Pre-brief read:** CONFIRMED — read `congress_prep_library.md` in full, all 9 sections. I wrote most of the existing infrastructure being upgraded, so I know exactly where the gaps are.

**2. Groundwork-is-bible:** ACCEPTED. This is my territory — I'll match it down to the pixel.

**3. Track A assignment:** ACCEPTED.

My scope is the foundation everything else sits on. Nothing in Tracks B-D can ship until my shared components are ready. I take that responsibility seriously.

**4. Technical clarifications and answers to team questions:**

**TileCard upgrades:**
- **Progress bar (3px):** Painted directly onto the cover pixmap in `applyBadges()`. Track: `QColor(0, 0, 0, 77)`, fill: palette Highlight fallback `#94a3b8` (in-progress) or `#4CAF50` (finished). No new widgets — pure QPainter on the cached pixmap.
- **Badge pill (bottom-right):** Already partially implemented (`m_percentBadge`). Need to match exact groundwork spec: pixelSize 11, DemiBold, `QColor(0, 0, 0, 115)` bg, `#f0f0f0` text, 6px padding, capsule shape. Hidden when `status == "finished"`.
- **Page badge (bottom-left):** New pill, same visual as badge pill but positioned bottom-left. Content: `"{page}/{totalPages}"`.
- **Corner radius 8px:** `_round_pixmap()` equivalent — QPainterPath clip on the cover pixmap before painting. Applied once during pixmap generation, cached.
- **Placeholder (no cover):** `QColor(0, 0, 0, 89)` background, first alpha char uppercased, point size 28 Bold, color `#9ca3af`, centered.
- **Hover/selection:** 2px accent border on `QFrame#TileImageWrap`. 120ms click flash via QTimer(400ms). Keyboard focus ring: 1px dotted `rgba(255,255,255,0.5)`.
- **"New" dot:** 10x10 ellipse, palette Highlight color, top-left. Shown when file mtime < 7 days ago. Requires `newestMtimeMs` (already in scanner structs).
- **Folder icon overlay:** Two rounded rects, `QColor(255, 255, 255, 140)`, top-left. Only on folder/series tiles, not individual file tiles.

**TileStrip upgrades:**
- **Card width expansion:** Porting groundwork algorithm exactly: `avail = width() - 4; cols = max(1, (avail + gap) / (card_w + gap)); actual_card_w = max(card_w, (avail - (cols-1)*gap) / cols)`. Cards stretch to fill — no orphan whitespace.
- **Grid layout:** Replacing current manual positioning with proper column math in `reflowTiles()`. First empty column gets stretch.
- **Selection model:** New `GridSelectionModel` — left-click selects one, Ctrl+click toggles, Shift+click range, Escape clears. `QSet<TileCard*> m_selected`. Emits `selectionChanged(QList<TileCard*>)`.
- **Keyboard navigation:** Arrow keys (Left/Right +/-1, Up/Down +/-cols), Enter = `tileDoubleClicked`, Escape = clear selection. Focus ring on focused tile.
- **Signals:** Adding `tileDoubleClicked`, `tileRightClicked`, `selectionChanged`. `tilePosterDropped` deferred (P2).

**Sidebar (252px):**
- `QFrame` objectName `"LibrarySidebar"`, fixed width 252px, left of content area.
- "LIBRARIES" label + root folder list (clickable, filters grid to selected root).
- "+ Add root..." button (objectName `"SidebarAction"`).
- "FOLDERS" label + QTreeWidget folder hierarchy.
- Context menus on tree items: Open, Reveal, Copy, Remove (DANGER).
- "All" item context menu: "Restore all hidden series/shows".
- This is the biggest single piece — new widget class, integration into all 3 pages, signal wiring for folder filtering.

**FadingStackedWidget:**
- Custom QStackedWidget subclass with cross-fade opacity animation (QPropertyAnimation on widget opacity). Replaces plain QStackedWidget in all 3 library pages. Straightforward port from groundwork's `fading_stacked_widget.py`.

**List view mode:**
- QTreeWidget with columns: Name (stretch), Items (80px), Last Modified (140px), Download (92px — P2, data contract from Agent 4).
- V-key toggle. Persisted per-library to QSettings.
- View toggle button: 28x28px, objectName `"ViewToggle"`.
- Density slider hidden in list mode.

**Search pattern polish:**
- Ctrl+F focuses search bar and selects all text.
- Tooltip: `"Separate words to match all\n(e.g. 'one piece' matches series or volumes containing both words)"`.
- `activeSearch` Qt property for CSS styling.
- Empty state labels: objectName `"LibraryEmptyLabel"`, centered. No-tiles-no-search: `"No comics found\nAdd a root folder via the + button or browse Sources for content"`. No-tiles-search: `'No results for "{query}"'`.

**Context menu patterns:**
- DANGER styling (red text QWidgetAction) is already in `ContextMenuHelper::addDangerAction()` — done.
- Exact label text, separator positions per groundwork — already mostly correct from Phase 2, will verify each label verbatim.

**@Agent 1 — PAGES column:** I'll add page count extraction per CBZ at scan time in `LibraryScanner.cpp`. For CBZ files: open archive, count image entries, store as `pageCount` in `SeriesInfo::FileEntry`. No HELP request needed — this is squarely in my scanner code. I'll ship it in Batch 2 alongside other TileCard data enrichment.

**@Agent 2 — TileStrip ad-hoc tiles:** No, TileStrip does NOT currently support injecting ad-hoc tiles mid-strip. Best approach: create a separate "Book Hits" TileStrip row below the main books strip, populated with individual book tiles that matched the query. I'll add a `setStripLabel(QString)` method to TileStrip so you can title it "MATCHING BOOKS" or similar. This keeps the architecture clean — one strip per semantic group.

**@Agent 2 — Audiobook sidebar:** I'll implement the groundwork's separate AUDIOBOOKS section in the sidebar with its own "+ Add audiobook root..." button. This is the correct groundwork behavior. BooksScanner already separates by extension — the sidebar just needs separate root management UI.

**@Agent 3 — Duration column data:** I'll add `durationSec` extraction to `VideosScanner`. This requires probing video files at scan time (lightweight ffprobe-style metadata read via QProcess calling ffprobe, or reading container headers). I'll provide it as `qint64 durationSec` in the `ShowInfo::FileEntry` struct. Agent 3 consumes it in ShowView.

**@Agent 4 — Download column:** Acknowledged. When I reach list view (Batch 10), I'll file a HELP request for the `TorrentClient::downloadProgress(folderPath)` data contract.

**5. Batch estimate: 12 batches**

| Batch | Files | Scope | Dependency |
|-------|-------|-------|------------|
| 1 | TileCard.h/.cpp | Corner radius 8px, placeholder (no-cover), progress bar 3px, badge pills exact spec, page badge bottom-left | None |
| 2 | TileCard.h/.cpp, LibraryScanner.cpp | Status indicators (finished hides badge, green bar), "New" dot, folder icon overlay, page count extraction for CBZ | None |
| 3 | TileCard.h/.cpp | Hover 2px accent border, 120ms click flash, keyboard focus ring, selection visual state | None |
| 4 | TileStrip.h/.cpp | Card width expansion algorithm, grid layout with stretch, `reflowTiles()` rewrite | None |
| 5 | TileStrip.h/.cpp | Selection model (Ctrl/Shift/click), `selectionChanged` signal, Escape clear, keyboard nav (arrow keys, Enter) | Batch 4 |
| 6 | TileStrip.h/.cpp | `tileDoubleClicked`, `tileRightClicked` signals, `setStripLabel()` for Agent 2, continue mode exact heights | Batch 5 |
| 7 | ComicsPage, BooksPage, VideosPage | Search polish: Ctrl+F, tooltip, activeSearch property, empty state labels, Escape-clear-search | None |
| 8 | NEW FadingStackedWidget.h/.cpp | Cross-fade QStackedWidget subclass, wire into all 3 pages replacing QStackedWidget | Batch 7 |
| 9 | NEW LibrarySidebar.h/.cpp | 252px sidebar: root list, folder tree, context menus, "Add root" button, filter signals | Batch 4 |
| 10 | ComicsPage, BooksPage, VideosPage | Sidebar integration: embed LibrarySidebar left of content, wire folder filter to TileStrip, books AUDIOBOOKS section | Batch 9 |
| 11 | NEW LibraryListView.h/.cpp | QTreeWidget list view: columns, row population, V-key toggle, persisted mode, hide density slider | Batch 4 |
| 12 | ComicsPage, BooksPage, VideosPage | List view integration, view toggle button 28x28px, multi-select context menu pattern, final polish | Batch 11 |

**Execution order:** Batches 1-3 (TileCard) and 4-6 (TileStrip) and 7 (search polish) are independent — I'll interleave them. Batch 8 (FadingStackedWidget) is quick. Batches 9-10 (sidebar) and 11-12 (list view) are the biggest lifts.

**When Tracks B-D can start:**
- After Batch 6: Agents 1-3 can wire TileCard/TileStrip upgrades and selection model.
- After Batch 10: Agents 1-3 can integrate sidebar.
- After Batch 12: Agents 1-3 can wire list view.

I'll post milestone announcements in chat.md at Batches 6, 10, and 12 so agents know when to pull.

**6. Concerns:**

- **Sidebar is the riskiest piece.** It's a new widget class touching all 3 pages with bidirectional signals (folder selection filters grid, grid navigation updates tree selection). I'll prototype on ComicsPage first, then replicate to Books/Videos.
- **Card width expansion may interact with density slider.** The expansion algorithm uses the base card width from density as input — they should compose cleanly, but I'll test at all 3 density levels.
- **Duration extraction in VideosScanner** requires either shipping ffprobe alongside the app or parsing container headers natively. I'll start with QProcess+ffprobe (same as groundwork) and we can optimize later if needed.

**No concerns with other tracks.** Tracks B-D are cleanly scoped. My only touch points with other agents are the data contracts (page count for Agent 1, TileStrip label API for Agent 2, duration data for Agent 3, download progress for Agent 4) — all additive.

---

## Agent 0 Synthesis

All five positions read. Zero rejections on groundwork-is-bible. Zero rejections on track assignments. Zero file ownership conflicts. This is the cleanest congress we've run.

### Decisions Ratified

1. **Groundwork is bible.** No deviations unless Qt/C++ technical impossibility, justified in writing. Unanimous acceptance.
2. **Agent 1 progress/continue reading EXEMPT.** Scroll-position-fraction-based resume stays. Everything else on ComicsPage/SeriesView follows groundwork.
3. **Audiobook roots split.** Separate "+ Add audiobook root..." button in sidebar per groundwork. Agent 5 implements in Track A Batch 9-10.
4. **Book-level search hits = separate row.** Agent 2 creates a "MATCHING BOOKS" TileStrip below the main books strip. Agent 5 provides `setStripLabel()` in Batch 6.
5. **"Open in new window" SKIPPED.** No multi-window support in Tankoban 2. All continue-tile menus drop this item.
6. **Duration extraction via ffprobe.** Agent 5 adds QProcess+ffprobe to VideosScanner. Agent 3 consumes `durationSec` in ShowView.
7. **Download column deferred to Agent 4.** List view "Download" column (P2) requires data contract from Agent 4. Agent 5 files HELP when ready.
8. **rootFoldersChanged wiring deferred.** Not this congress. Agent 4 wires when needed.

### Execution Order — The Master Schedule

**Phase 1: Parallel Start (NOW)**

These have zero Track A dependency — agents begin immediately:

| Agent | Batches | Scope |
|-------|---------|-------|
| Agent 5 | 1-3 | TileCard: corner radius, placeholder, progress bar, badges, status, hover, new dot, folder icon |
| Agent 5 | 4-6 | TileStrip: card expansion, grid layout, selection model, keyboard nav, signals |
| Agent 5 | 7 | Search polish: Ctrl+F, tooltip, activeSearch, empty states |
| Agent 1 | A-E | SeriesView: progress icons, PAGES col, context menus, forward nav, naming toggle. ComicsPage: search polish, continue-tile menu |
| Agent 2 | 1-8 | BooksScanner cover fix, BookSeriesView: progress icons, continue bar, forward nav, context menus. BooksPage: shortcuts, scored search |
| Agent 3 | A | VideosPage: 250ms single-click delay |

**Phase 2: After Agent 5 Batch 6 (TileCard + TileStrip ready)**

| Agent | Batches | Scope |
|-------|---------|-------|
| Agent 1 | F | Wire TileCard/TileStrip upgrades into ComicsPage |
| Agent 2 | 9 | Grid + continue-tile context menu upgrades |
| Agent 3 | B-D | Wire TileCard/TileStrip, sidebar signals, context menus |

**Phase 3: After Agent 5 Batch 10 (Sidebar ready)**

| Agent | Batches | Scope |
|-------|---------|-------|
| Agent 5 | 8 | FadingStackedWidget |
| Agent 1 | F (cont) | Sidebar integration into ComicsPage |
| Agent 2 | 10 | Sidebar integration into BooksPage (with AUDIOBOOKS section) |
| Agent 3 | E-G | ShowView: continue bar, forward nav, duration, context menus |

**Phase 4: After Agent 5 Batch 12 (List view ready)**

| Agent | Batches | Scope |
|-------|---------|-------|
| Agent 1 | F (cont), G | List view, multi-select, final polish |
| Agent 2 | 11 | List view + multi-select context menu |
| Agent 3 | H | Poster management, keyboard shortcuts, list view wiring |

### Total Batch Count

| Agent | Batches | Track A dependency |
|-------|---------|-------------------|
| Agent 5 | 12 | None (IS Track A) |
| Agent 1 | 7 | Batch F-G wait on milestones |
| Agent 2 | 11 | Batches 9-11 wait on milestones |
| Agent 3 | 8 | Batches B-H wait on milestones |
| **Total** | **38 batches** | |

### Cross-Agent Data Contracts (New)

| Provider | Consumer | Data | Delivery |
|----------|----------|------|----------|
| Agent 5 | Agent 1 | `SeriesInfo::FileEntry::pageCount` (int, CBZ image count) | Track A Batch 2 |
| Agent 5 | Agent 2 | `TileStrip::setStripLabel(QString)` | Track A Batch 6 |
| Agent 5 | Agent 3 | `ShowInfo::FileEntry::durationSec` (qint64, ffprobe) | Track A Batch 2 |
| Agent 4 | Agent 5 | `TorrentClient::downloadProgress(folderPath) -> float` | On HELP request |

### Shared File Touches

Only Agent 3 touches MainWindow (for `progressUpdated` signal forwarding). Agent 3: announce in chat.md before editing, per build rules.

Agent 5 touches all three library pages in Batches 7, 10, 12 — but these are additive (search polish, sidebar embed, list view embed). No conflicts with agents 1-3 because agents 1-3 wait for Agent 5's milestone before touching those files for the same features.

### Risk Assessment

- **Highest risk:** Sidebar (Agent 5 Batch 9-10). New widget class with bidirectional signals across 3 pages. Agent 5 correctly plans to prototype on ComicsPage first.
- **Medium risk:** Card width expansion interacting with density slider. Agent 5 flagged it. Test at all 3 density levels.
- **Low risk:** Everything else. The patterns are well-understood, the groundwork code is readable, the positions show all agents know exactly what they're building.

### No Overrides

No domain master override needed. Agent 5's position is comprehensive and correctly addresses every cross-agent question. Agents 1-3 scoped their work cleanly within their domains. Agent 4's observer position is responsible — flagged relevant items without overstepping.

**This congress is ready for Hemanth's final word.**

---

## Hemanth's Final Word

I, THE LAZY LEADER OF THE BROTHERHOOD, APPROVES.

Congress 4 is ratified. Execute.
