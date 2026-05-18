# Comics + Tankoyomi + Stream-as-blueprint merger — Brainstorm

- **Date:** 2026-05-14
- **Author:** Agent 1 (Comic Reader + Tankoyomi, scope-expanded 2026-05-14 per the BROTHERHOOD_RESTRUCTURE bundle commit `1466a79`)
- **Arc tag:** `COMICS_TANKOYOMI_STREAM_MERGER`
- **Status:** Phase 1 brainstorm — pending Codex (Agent 7) review-AND-EXPAND per gov-v4 Rule 20 (revised 2026-05-14 ~5:35pm at chat.md:3640+). After Codex's expansion lands inline (HTML-comment attribution markers), Hemanth fires `/superpowers:writing-plans` directly; no second Codex pass.
- **Phase 2 (writing-plans, separate Hemanth fire):** `docs/superpowers/plans/2026-05-14-comics-tankoyomi-merger.md`
- **Phase 3 (executing-plans, separate Hemanth fire):** multi-summon arc via `/superpowers:subagent-driven-development`, recommended cadence one-go-per-phase
- **Reference brief:** `C:\Users\Suprabha\.claude\plans\alright-now-give-me-abundant-beacon.md` (Agent 8 prompt-architect output, 379 lines)
- **Coordination boundaries:** Comic Reader (Agent 1's existing surface) untouched; theme system (Agent 5) untouched; player (Agent 3) untouched; Books / Videos / Stream untouched; Tankorent + TankoLibrary (Agent 4B's narrowed lane) untouched
- **Skills invoked (Phase 1):** `/brief`, `/superpowers:brainstorming`, `/superpowers:verification-before-completion` (before announcing this doc ready for Codex)

---

## §1 Goal — Hemanth's vision verbatim

> "This is for an entirely new vision. I want to remove Tankoyomi and merge it with the Comics mode. The Comics mode will then take a lot of elements from Stream mode.
>
> In the search bar, instead of 'Search for volumes' or whatever, there will be a simple 'Search Tankoyomi,' which, upon entering a series name, will show me the Tankoyomi results. Just like Stream mode's search results, I can click on the poster of the search result and enter a series view very similar to Stream mode. I will also have the same 'Add to Library' option.
>
> As for the downloads, everything happens inside the library itself, Netflix/Stream mode style. The downloading happens in the series page itself, which itself will be a replica of Stream mode's show view, and downloading a chapter automatically adds the series to the library.
>
> Manga or comics added through Tankoyomi will have a Tankoyomi badge because the UI inside is completely different from other series folders that were not downloaded from Tankoyomi. The UI inside is basically the series view I was talking about.
>
> And Stream mode isn't just a reference; it's a blueprint."

Additional emphasis Hemanth added the same day, captured in the brief and re-quoted because it is the load-bearing clause that drives §6 of this doc:

> "If possible, the code from Stream mode's search and home view page can be reused for Comics mode's Tankoyomi-infused counterpart."

This brainstorm-md must produce an EXPLICIT REUSE-VS-FORK-VS-REPLACE MAP for every Stream-side widget / class / persistence type. §6 below is that artifact.

---

## §2 Reference surfaces

### §2.1 Stream-side blueprint inventory (file:line cites — `feedback_reference_during_implementation`)

These are the surfaces Hemanth named as the blueprint. Each is annotated in §6 with its reuse disposition (REUSE DIRECTLY / TEMPLATE-PARAMETERIZE / FORK WITH ATTRIBUTION / REPLACE WITH NOTHING).

- `src/ui/pages/stream/StreamSearchWidget.h:21` + `.cpp:48-68` — search input → `MetaAggregator::queryCatalogs(...)` → result tiles → emits `metaActivated(MetaItemPreview)` on click. Two sections (Movies / Series) with per-section `kInitialCap = 6` initial cap + "Show N more" overflow reveal pattern.
- `src/ui/pages/stream/StreamLibraryLayout.h:16` + `.cpp:84-110, 332-373` — TileStrip + TileCard grid + DOWNLOADED chip (Layer 3) + DOWNLOADING chip (Netflix overhaul Phase 4); sort combo + density slider; `showEvent` triggers `StreamDownloadIndex::validateAll`.
- `src/ui/pages/stream/StreamHomeBoard.h:26` — thin container around `StreamContinueStrip` after the popular-rows removal 2026-04-15.
- `src/ui/pages/stream/StreamContinueStrip.h:16` — landing-page Continue strip.
- `src/ui/pages/stream/StreamDetailView.h:34` + `.cpp:53-110, 1213-1291` — season combo + 7-column episode table (col 0 checkbox, col 6 action icon) + `RowState` enum {Idle, Queued, Downloading, Publishing, Published, Paused, Failed} + season-header morphing button + right-click context menu (Cancel / Remove / Show alternate streams). Signals: `playRequested`, `sourceActivated`, `bulkDownloadRequested(season)`, `selectedEpisodesDownloadRequested(season, episodes)`, `singleEpisodeDownloadRequested(season, episode)`, `playLocalFileFromStreamRequested(...)`. Auto-add-to-library fires on first progress save via `autoAddToLibrary()` at `.h:95`.
- `src/core/stream/StreamDownloadIndex.h:21` — canonical-key-keyed thread-safe JSON index over `stream_downloads.json`, bidirectional lookup (path → entry, imdb:season:episode → path), `isStreamOwned(canonicalKey)` for VideosScanner filter. Schema version 1, three derived maps under one mutex.
- `src/ui/pages/stream/StreamDownloadsPage.{h,cpp}` (created Netflix overhaul Phase 5) — Active + History sections grouped by show, 500ms debounced refresh, 90-day retention, `openShowRequested` signal.
- `src/core/stream/MetaAggregator` (referenced by StreamSearchWidget ctor) — catalog query pipeline that fans out to Stream addon registry and merges results.
- `src/ui/widgets/SidebarDrawer.{h,cpp}` — left-drawer container holding Tankorent / Tankoyomi / TankoLibrary / Stream Downloads. The "Tankoyomi" entry is in-scope for removal (§8); Tankorent + TankoLibrary entries stay (Agent 4B's lane).

The just-shipped Netflix overhaul spec at `docs/superpowers/specs/2026-05-12-stream-downloads-netflix-overhaul-design.md` documents the latest Stream surface state; this brainstorm aligns with that surface.

### §2.2 Current Comics-side inventory (file:line cites)

- `src/ui/pages/ComicsPage.h:23` + `.cpp` — TileStrip + TileCard grid + Continue strip + LibraryScanner thread + search bar (`m_searchBar` line 72) + grid/list view-toggle (`m_viewToggle` line 76) + density slider + sort combo + `INavStateProvider` (GLOBAL_NAV_HISTORY Task 8). The existing `m_searchBar` is what Hemanth wants repurposed for "Search Tankoyomi" per Batch A locked answer.
- `src/ui/pages/SeriesView.h` + `.cpp` — existing per-series folder-tree archive table. Stays unchanged for folder-imported series per Batch E Q1.
- `src/ui/readers/ComicReader.{h,cpp}` — CBZ page renderer. Out of scope per Batch H Q1.
- `src/core/scan/LibraryScanner.h` — walks `m_bridge->rootFolders("comics")` for .cbz/.cbr/.rar; cover thumbnail cache. EXTEND in §6 to recognize Tankoyomi-origin folders via their hidden `.meta.json` sidecar (or via cross-check against `ComicsLibraryRecord` / `MangaDownloadIndex` — see §7 for the source-of-truth invariant Codex should validate).
- `src/ui/MainWindow.{h,cpp}` — owns the page stack, the SidebarDrawer wiring, `openComic(...)` connect chain, and routing for which page-id the active button maps to. Will need EXTEND for the new comics-side detail view routing (badged → ComicsTankoyomiDetailView, unbadged → SeriesView).

### §2.3 Current Tankoyomi-side inventory (file:line cites)

Per the Agent 4B → Agent 1 ownership transfer at commit `1466a79`, Agent 1 now owns these.

- `src/ui/pages/TankoyomiPage.h:29` — Results tab (search bar + 4-page stack `m_searchResultsStack` {list / grid / empty / loading} + `m_detailView` inner stack at C.5 `4ddae68`) + Transfers tab (`m_transfersCardList` vertical card list of `TransferGroupCard`); async cover cache pipeline (`ensureCover` line 39 + `m_coversInFlight` set). DELETE per §6.
- `src/ui/pages/tankoyomi/MangaDetailView.{h,cpp}` (yesterday's C.5 ship at `4ddae68` + `bff2274`) — Mihon-style detail page wired into TankoyomiPage's Results tab inner stack. DELETE per §6 (replaced by ComicsTankoyomiDetailView).
- `src/ui/pages/tankoyomi/MangaResultsGrid.{h,cpp}` — grid view used in TankoyomiPage Results tab. DELETE (the new ComicsTankoyomiSearchWidget grid replaces).
- `src/ui/pages/tankoyomi/TransferGroupCard.{h,cpp}` — per-series Transfers card. DELETE (no Transfers tab in merged Comics; no cross-series Downloads page per Batch C Q2).
- `src/ui/dialogs/AddMangaDialog.{h,cpp}` — modal pop-up that was Tankoyomi's pre-C.5 detail+add path. DELETE (Add happens on the new detail page as a silent-bookmark button per Batch B Q2).
- `src/core/manga/MangaScraper.h:9` — virtual base, contract `sourceId() / sourceName() / search(query, limit) / fetchChapters(seriesId) / fetchPages(chapterId)` + signals `searchFinished / chaptersReady / pagesReady / errorOccurred`. ABSORB UNCHANGED. **Open: Codex to confirm whether a new `fetchDetail(seriesId) → SeriesDetail` method is needed to power the §5.3 hero (cover + synopsis + genres), or whether existing search-time `MangaResult` carries enough metadata for the WeebCentral + ReadComicsOnline pair.**
- `src/core/manga/WeebCentralScraper.{h,cpp}` — concrete manga scraper. ABSORB UNCHANGED.
- `src/core/manga/ReadComicsScraper.{h,cpp}` — concrete comics scraper. ABSORB UNCHANGED.
- `src/core/manga/MangaDownloader.h:56` — Mihon-parity download engine, 5-state per-chapter status, per-series + global pause/resume, restart/retry/start-chapter-now, JsonStore-backed persistence (`manga_downloads.json` + `manga_history.json`), `chapterUpdated(seriesId, chapterId)` signal. ABSORB; minor EXTEND if new signal needed for the per-series Tankoyomi-origin status flow. **Open: Codex to check whether the emit-while-locked pattern flagged in observation 2551 needs hardening alongside the new auto-add toast flow (Batch C Q3).**
- `src/core/manga/MangaResult.h` — data shape for search hits + ChapterInfo + PageInfo. ABSORB UNCHANGED.

### §2.4 Off-tree reference (architecture-only, do not transliterate)

- `C:\Users\Suprabha\Downloads\Comic References\mihon-main\` — Mihon (Kotlin + Jetpack Compose). Architecture-only reference per the brief. Mihon's per-chapter download indicator semantics (5-state tap-only circle, no long-press per Hemanth's 2026-05-13 lock), pause/resume/restart/retry verbs, multi-select pattern carry forward to the new detail view; Mihon's plugin/extension architecture is NOT ported (Batch F Q4 locked v1 as sealed, with door open to v2).

---

## §3 Hemanth's locked picks across the 8 batches

The brainstorming was paced as batches of four per `feedback_brainstorm_batches_of_four.md`, with ASCII previews in `AskUserQuestion` previews for layout choices, per Hemanth's verbatim preference codified 2026-05-14.

### §3.1 Batch A — Search flow

- **Search bar location:** always-on top bar — the existing `ComicsPage::m_searchBar` (line 72) is REPURPOSED in place; placeholder text changes from "Search series and volumes" to "Search Tankoyomi". Same widget instance, new meaning. Local-library search as a separate feature is NOT preserved in v1 (only Tankoyomi-search lives on the bar; library scrolls below).
- **Empty state:** no special pre-search content. Empty bar → today's library tiles + Continue strip remain visible. No recent-searches dropdown, no Trending row.
- **Result layout when Go is hit:** full takeover — library hides, results fill the page, Back returns to library. Stream-mode parity with `StreamSearchWidget`.
- **Result grouping:** two sections — Manga (WeebCentral) and Comics (ReadComicsOnline) — mirroring Stream's Movies / Series split. Future scrapers will be tagged with a `mediaType ∈ {manga, comics}` field on the scraper interface (Codex to confirm whether to add as a virtual method or a `sourceMediaType()` accessor; default falls under §6 implementation notes).

### §3.2 Batch B — Series detail flow

- **Detail header:** full Stream-style hero — cover + title + small meta strip (author . year . status . source name) + a short synopsis paragraph + genre tags. Hemanth's rationale: both WeebCentral and ReadComicsOnline expose this metadata; the new view should render whatever's available.
- **Add to Library button:** silent bookmark Stream parity — clicking [+ Add to library] immediately puts a tile in the Comics library, even with zero chapters on disk. Clicking again ("Remove from library") removes the record. No separate Saved-vs-Library lists.
- **Tile timing on the Comics page:** appears immediately on Add click. The tile's cover image is the same hero image the source already provided to the detail view, so there is NO placeholder/shimmer state — Hemanth's specific call-out: "the series view would already have a hero poster from weebcentral or readcomicsonline."
- **Chapter list layout:** flat scrolling list of all chapters with a Sort dropdown (newest first / oldest first). Volume / story-arc grouping (as a Stream-season-style organising layer for series like One Piece) is **deferred to v2** per Hemanth's directive: "if you think season-system but with volumes or story arcs is possible, then we will save it for a future version, so make a note of it." See §9 Deferred follow-ups.

### §3.3 Batch C — Library / Downloads relationship (the "Netflix-style" call)

- **Download triggers on the detail page:** per-chapter download arrow on each row PLUS a "Download Range..." From/To modal PLUS shift-click multi-select with a "Download N selected" button when checkboxes are filled. This carries forward Hemanth's locked Mihon picks from 2026-05-13.
- **Cross-series Downloads page:** **none.** Significant divergence from Stream's blueprint (Stream just shipped `StreamDownloadsPage` as a sidebar entry). For Comics, downloads are visible only inside each series's detail page. The DOWNLOADING chip on each library tile is the only cross-board signal. See §6 — `StreamDownloadsPage` is REPLACED WITH NOTHING for the comics side. Tankoyomi's existing Transfers tab is deleted, not migrated.
- **Auto-add behaviour:** when the user clicks a chapter's download arrow on a series that's not yet in the library, the series is silently added AND a toast notification briefly says "Added Berserk to your library" at the bottom of the screen. Reuses Stream's existing `ToastHud` widget.
- **Per-series queue controls (Pause / Resume / Restart / Retry-failed / Cancel-all):** live on the series detail page only — right-click the series header or click an overflow [...] menu. No equivalent surface anywhere else. Consistent with the no-cross-series-Downloads-page decision.

### §3.4 Batch D — Tankoyomi-origin badge

- **Position:** top-left corner chip on the tile, matching Stream's DOWNLOADED chip position. Both chips can coexist on the same tile (Tankoyomi-origin + DOWNLOADED-all-chapters).
- **Content:** generic "Tankoyomi" label. Not source-specific. The source name (WeebCentral / ReadComicsOnline) lives in the detail-page meta strip per §3.2.
- **Stickiness:** sticky with the library record. Manually deleting downloaded files in File Explorer does NOT remove the badge; the tile and badge stay. What changes is the per-chapter on-disk state inside the detail view — chapters that previously had a downloaded-tick reveal as undownloaded (live disk validation, mirroring `StreamDownloadIndex::validateAll` on `showEvent`). The only way to remove the badge is "Remove from library" in the app.
- **Continue Reading strip:** yes, same badge in the same position on the continue-strip tiles too.

### §3.5 Batch E — Internal UI distinction for badged vs non-badged

- **Folder-imported series (no badge):** keep today's `SeriesView` (folder-tree archive table) unchanged. Two completely different inner UIs side by side, exactly as Hemanth's vision named.
- **Offline source behaviour:** when the Tankoyomi source is unreachable, the detail page still loads cover + title + meta from cache; chapters on disk remain fully readable; a small banner says "Couldn't refresh chapter list from Tankoyomi — showing cached state." Not-yet-downloaded chapters appear greyed with a tooltip; their download arrows are disabled while the source is offline.
- **Folder-drop name collision:** if a user manually drops a folder named "Berserk" into their library root that happens to match a known Tankoyomi series, the app treats it as folder-imported. No auto-match, no suggestion banner, no link prompt. Provenance is determined strictly by HOW the series got into the library, not by name.
- **Manual conversion (folder → Tankoyomi-origin):** not supported in v1. To convert, the user removes the folder series and re-adds via Tankoyomi search. Files on disk can remain (will be merged on subsequent downloads).

### §3.6 Batch F — Sources / extensibility

- **v1 source list:** WeebCentral + ReadComicsOnline (today's two). No new sources shipped in v1.
- **Source enable/disable toggle:** none. All sources always on. Search always fans out to both.
- **Source-failure UX:** brief toast at the bottom when a source fails to return results during a search ("ReadComicsOnline didn't respond"). Reuses `ToastHud`. Search results still show whatever the working source returned.
- **Third-party / user-installed sources:** sealed for v1, but the scraper layer is to be designed so a future v2 extensions arc does NOT require rewriting consumers. Specifically (Rule-14 implementation pick): the hardcoded `m_scrapers` list in `TankoyomiPage` ctor moves into a small registry-style factory; source IDs stay string-keyed (no compile-time enums); the `MangaScraper` virtual interface stays clean (no implementation-detail leakage); the UI iterates scrapers without baking "exactly two" anywhere. See §9 Deferred follow-ups.

### §3.7 Batch G — Persistence + migration

- **Where Tankoyomi-downloaded chapters land on disk:** into the user's existing Comics root folder (first entry of `m_bridge->rootFolders("comics")`). One folder per series, named normally. Looks like any other folder-imported series in File Explorer.
- **Migration from existing TankoyomiPage state:** **clean slate.** When the merger ships, existing `manga_downloads.json` records and `manga_history.json` history are not adopted. Completed Tankoyomi-downloaded folders on disk get rescanned by `LibraryScanner` as folder-imported (NO Tankoyomi badge). In-flight queues are cancelled, partial CBZ files stay on disk as orphans (no library record). To re-claim a previously-Tankoyomi-downloaded series as Tankoyomi-origin, the user re-adds via search.
- **Comics root-folder change in settings:** library records persist (tiles stay, badges stay). Per-chapter on-disk state revalidates live — previously-downloaded ticks revert to undownloaded if the new root doesn't contain the files. **Hemanth's ideal preference (Rule-14 implementation pick for Agent 1): auto-copy Tankoyomi-origin series folders to the new root on the root-change transition**, with fallback to "tiles stay, chapters missing, re-download per chapter" if the auto-copy is non-trivial to implement. To be verified in Phase 2 (writing-plans).
- **On-disk file layout:** normal-looking folders + filenames. One folder per series (`D:\Comics\Berserk\`), chapters named `Chapter 358.cbz` etc. A small hidden metadata file per series folder (proposed: `.tankoyomi-meta.json`) tracks source ID, series ID, scraper version, last-seen chapter list cache (for offline-source path). Same-title collisions across sources are disambiguated by source suffix on collision only (`Berserk` vs `Berserk (ReadComicsOnline)`). Illegal Windows filename characters (`:`, `?`, `*`, `<`, `>`, `|`) are replaced with `_`.

### §3.8 Batch H — Out-of-scope confirmations

- **Comic Reader (CBZ page renderer):** fully out of scope. Tankoyomi-origin chapters open in today's reader as-is; existing `openComic(cbzPath, seriesCbzList, seriesName)` signal chain is reused.
- **Theme system (Agent 5's lane):** fully out. The new chip + new detail view use existing theme tokens only. No new tokens added in this arc.
- **Other modes (Books / Videos / Stream / Tankorent / TankoLibrary):** untouched, except the SidebarDrawer's "Tankoyomi" entry is removed (one-line mechanical change to `SidebarDrawer.cpp` + corresponding `PAGE_TANKOYOMI` routing in `MainWindow`). Tankorent + TankoLibrary entries stay.
- **Catch-all (anything else Hemanth wants in scope):** nothing additional pulled in.

---

## §4 Architecture — hybrid reuse + fork (Rule-14 implementation pick)

Three approaches were considered for how to structure the code reuse from Stream into the merged Comics surface:

- **Approach A — Template-parameterize Stream's surfaces.** `StreamSearchWidget` becomes `class template<ResultPreview, Aggregator>`; `StreamLibraryLayout` parameterized on `<LibraryEntry, DownloadIndex>`; `StreamDetailView` parameterized on `<Episode, MediaItem>`. Instantiated twice: once for Stream (`<MetaItemPreview, MetaAggregator>`), once for Comics (`<MangaResult, MangaAggregator>`).
- **Approach B — Fork Stream's surfaces with attribution.** Copy each Stream-side widget into a new `src/ui/pages/comics/Comics*` namespace, renaming types and adjusting semantics; each fork carries a header comment naming the Stream-side parent file and line.
- **Approach C — Hybrid: reuse low-level primitives, fork higher-level composers.** TileStrip + TileCard primitives stay shared (already shared in the codebase). ChipBadge rendering stays shared. ToastHud stays shared. But the composer widgets that compose those primitives into the search-takeover view + the library-grid view + the detail view get forked with attribution. The state-machine and signals in Stream's detail view (RowState, action-icon dispatch, season-header morphing button) port as design patterns, not literal templated code.

**Picked: Approach C (Hybrid).** Rationale:

- Qt's `Q_OBJECT` macro and template parameters don't compose cleanly. Templating `StreamDetailView` (~600 LOC with rich signal/slot wiring + embedded `StreamSourceList`) is a heavy refactor that risks regressing Stream's just-shipped Netflix overhaul (`docs/superpowers/specs/2026-05-12-stream-downloads-netflix-overhaul-design.md`, ratified 2026-05-13 commit `4651236`).
- The leaf types (Episode vs Chapter, Season vs Volume, IMDb-id vs manga-id, MetaItemPreview vs MangaResult) diverge enough that templated reuse would devolve into specialisation forests.
- Each surface will likely evolve independently — Hemanth already named "volume / story-arc grouping" as a v2 follow-up for Comics; Stream stays season-grouped. Independent evolution is easier with forks than with template specialisations.
- Primitive sharing (TileStrip / TileCard / chip rendering / ToastHud) is real and is the cheapest high-value reuse path.

The provenance model (§3.4 / §3.7) drives the architecture:

- Every series in the Comics library has a **provenance flag**: `folder` or `tankoyomi`.
- `folder`-origin series are discovered by `LibraryScanner` walking the root folders; their persistence is purely the on-disk layout + cover thumbnail cache.
- `tankoyomi`-origin series have a **library record** in a new JSON file (proposed: `comics_library.json`) holding {seriesId, title, origin, sourceId, addedAt, coverPath, rootFolder}. Their on-disk presence is managed by the (existing) `MangaDownloader` which writes chapters into the configured root.
- The provenance flag drives the inner-UI routing: opening a `folder`-origin tile → today's `SeriesView` (folder tree); opening a `tankoyomi`-origin tile → the new `ComicsTankoyomiDetailView`. ComicsPage's tile-click handler dispatches by record lookup.

Per-chapter on-disk state is **live-validated**, mirroring the `StreamDownloadIndex::validateAll` on-showEvent pattern (`StreamLibraryLayout.h:42-43` `showEvent` override). On every detail-page open, the new `MangaDownloadIndex` re-checks whether the indexed file paths still exist on disk and updates the chapter rows' tick / untick marks accordingly.

---

## §5 The merged Comics surface — user-facing flows

### §5.1 The library page

The Comics page's existing layout is preserved structurally: Continue Reading strip at the top, library grid below. Two changes:

1. The search bar's placeholder changes from "Search series and volumes" to "Search Tankoyomi". The bar's signal wiring routes to the new search-takeover view (forked from `StreamSearchWidget`) instead of the existing client-side library filter.
2. Library tiles gain a top-left corner chip showing "Tankoyomi" for `tankoyomi`-origin series. The existing tile chip slot (used by the Continue strip's progress indicator) is preserved; a new chip slot is added for provenance. Both chip slots can render simultaneously (a Tankoyomi-origin series that's been read partway → Tankoyomi badge top-left + Continue progress on the cover).
3. Tiles for series that are actively downloading chapters gain a DOWNLOADING chip (parallel to Stream's DOWNLOADING chip ship from Netflix overhaul Phase 4 / Task 17, memory 2166). Driven by `MangaDownloader::countByState()` or equivalent (already exists at `MangaDownloader.h:117-121`).

### §5.2 The search flow

User types a query, hits Go (or Enter). The library hides; the search-takeover view (forked from `StreamSearchWidget`) fills the page. Two parallel scraper searches kick off (WeebCentral + ReadComicsOnline). Results stream in. The page shows two sections, "Manga" first then "Comics", each with its own grid of result tiles, each tile showing cover + title. The per-section `kInitialCap = 6` initial cap + "Show N more" overflow reveal pattern is preserved as-is from Stream.

If a source fails, the working source's results still render; a brief toast at the bottom says "ReadComicsOnline didn't respond" (or similar). Clicking a result tile opens the new series detail view (§5.3). Clicking Back at the top returns to the library.

The search bar stays visible during takeover so the user can refine without going Back first. (This matches Stream's takeover behaviour and is implicit in the "always-on top bar" pick from Batch A.)

### §5.3 The series detail view (Tankoyomi-origin)

A new view, `ComicsTankoyomiDetailView`, forked from `StreamDetailView`. Layout from top to bottom:

- A back-arrow row at the very top.
- The hero: cover image on the left (using the source's poster URL, cached in an existing-or-new poster cache directory), title large to the right of the cover, then a small meta strip (`author . year . status . sourceName`), then a short synopsis paragraph, then a row of genre tags. A primary "Add to Library" button sits at the top-right of the row; it morphs to "Remove from library" when the series is in the library.
- A chapter list section below the hero. Flat scrolling list. Header: `CHAPTERS [Download Range...] [Download N selected]` with the "Download N selected" button hidden until at least one row is checked. Sort dropdown to the right of the header (newest first / oldest first).
- Each chapter row: `[checkbox] [download-arrow] Chapter NN  date  source` columns. The download arrow's icon morphs by per-chapter state (idle = down-arrow, queued = spinner, downloading = progress arc, done = check, failed = error icon). This is the `RowState` enum from `StreamDetailView.h` ported with literal verbatim semantics. Click an arrow on a not-downloaded chapter → download starts (with auto-add silent-bookmark if not yet in library + toast at the bottom). Click on a done chapter → opens the chapter in ComicReader.
- Right-click on the series header (the hero row) → context menu: Pause series / Resume series / Restart all chapters / Retry failed chapters / Cancel all / Remove from library.
- Right-click on a chapter row → context menu: Cancel / Remove (for downloaded chapters) / Open in Reader / Show alternate source (v2 — flagged, not v1).

If the source is offline, a small banner above the chapter list says "Couldn't refresh chapter list from Tankoyomi — showing cached state." Already-downloaded chapters stay readable; not-downloaded ones are greyed with disabled arrows.

### §5.4 The auto-add flow

User clicks the download arrow on a not-yet-added series's chapter row:

1. `MangaDownloader::startDownload(...)` is called.
2. A new entry is appended to `comics_library.json` with `origin: "tankoyomi", sourceId: <weebcentral|readcomicsonline>, addedAt: <epoch ms>, coverPath: <cached poster path>, rootFolder: <first comics root>`.
3. `ComicsPage::refreshTiles()` (or equivalent) is invoked → the new tile appears immediately.
4. `ToastHud::show("Added Berserk to your library")` fires; auto-fades.

### §5.5 The remove-from-library flow

User clicks "Remove from library" on the detail page's primary button (or right-click → Remove from library on the tile or header):

1. Confirmation dialog (Stream parity at `StreamLibrary::remove` → Netflix overhaul Phase 7): "Remove Berserk from your library?" with options "Remove (keep files)" and "Remove and delete files" + Cancel.
2. On Remove (keep files): the library record is dropped from `comics_library.json`; the tile disappears; downloaded files remain on disk (and will be re-scanned next time as folder-imported, with no Tankoyomi badge). To re-claim provenance, re-add via search.
3. On Remove and delete files: same as above plus the series folder is rm-rf'd from disk.
4. If active downloads exist for the series, both options first cancel the active downloads (mirrors Netflix overhaul P7 cancel-with-delete pattern).

### §5.6 The root-folder-change flow (Hemanth's auto-copy ideal)

User changes the Comics root folder in Settings. Behaviour (Rule-14 implementation pick, to verify in Phase 2):

1. **Ideal path (auto-copy):** the app iterates `comics_library.json` entries where `origin == "tankoyomi"` and moves their series folders from the old root to the new root. Progress dialog during the move. Records' `rootFolder` field is updated post-move. `MangaDownloadIndex::validateAll` re-runs against the new paths.
2. **Fallback path (if auto-copy is hard to implement reliably):** library records persist (tiles + badges stay). Chapters revalidate as not-on-disk. User can re-download per chapter, OR manually copy the files over and trigger a rescan.

Folder-imported series follow today's `LibraryScanner` behaviour — they'd be picked up automatically if their files happen to be in the new root, or disappear from the library if they aren't.

---

## §6 Code reuse map — the load-bearing artifact

For every Stream-side surface in §2.1 + every Comics-side and Tankoyomi-side surface in §2.2 + §2.3, this section names the disposition. Per `feedback_no_tables_simple_lists.md`, no markdown tables — list form.

### §6.1 Stream-side dispositions

- **`StreamSearchWidget.{h,cpp}` (h:21 + cpp:48-68)** — **FORK WITH ATTRIBUTION** to `src/ui/pages/comics/ComicsTankoyomiSearchWidget.{h,cpp}`. The new fork carries an `// Forked from src/ui/pages/stream/StreamSearchWidget.{h,cpp} (sha at fork time)` header comment. Same structural shape (search input → aggregator query → two-section grid with `kInitialCap=6` cap + Show-more overflow → click emits `seriesActivated(MangaResult)` analogue). Diverges at the type level (manga result types instead of `MetaItemPreview`) and at the aggregator wiring (fan out to `QList<MangaScraper*>` instead of `MetaAggregator`). Rationale: Q_OBJECT + templates don't compose; types diverge enough that templating would be specialization forest.
- **`StreamLibraryLayout.{h,cpp}` (h:16 + cpp:84-110, 332-373)** — **REUSE PATTERN, EXTEND `ComicsPage` in place**. Do NOT fork the whole layout class. The chip system (DOWNLOADED chip + DOWNLOADING chip) is extracted as a small `ChipBadge` widget if not already, then integrated into `ComicsPage`'s existing TileStrip + TileCard. ComicsPage gains: a Tankoyomi-origin chip rendered top-left, a DOWNLOADING chip rendered top-right (or below; final position is Codex+plan-mode call), and a `setTorrentClient`-equivalent (the manga side calls `setMangaDownloader`) for the DOWNLOADING chip's data source. The `showEvent` validateAll pattern at `StreamLibraryLayout.h:42-43` is mirrored on the comics side via a new `MangaDownloadIndex::validateAll` on `ComicsPage::showEvent`. Rationale: ComicsPage already has a working tile layout; duplicating it as a separate class buys nothing.
- **`StreamHomeBoard.{h,cpp}` (h:26)** — **REPLACE WITH NOTHING**. Comics already has its own Continue strip rendering in `ComicsPage::m_continueStrip`; no HomeBoard equivalent is needed. The existing Continue strip gains the Tankoyomi chip on its tiles (same chip slot as the library grid).
- **`StreamContinueStrip.{h,cpp}` (h:16)** — **PATTERN REUSE, no class fork**. `ComicsPage::m_continueStrip` already exists; just teach it to render the Tankoyomi chip when the underlying record's provenance is `tankoyomi`.
- **`StreamDetailView.{h,cpp}` (h:34 + cpp:53-110, 1213-1291)** — **FORK WITH ATTRIBUTION** to `src/ui/pages/comics/ComicsTankoyomiDetailView.{h,cpp}`. The new fork carries an `// Forked from src/ui/pages/stream/StreamDetailView.{h,cpp}` header comment. The RowState enum, action-icon-by-state mapping, right-click context menu builder, and signal-and-slot shape (player vs reader handoff, auto-add-on-first-progress) port literally. Diverges at: chapter list instead of season-combo + episode table (single flat list per Batch B Q4), addition of multi-select checkboxes + Range modal (Hemanth Mihon pick), removal of embedded StreamSourceList (manga has no equivalent stream-source pick stage), addition of offline-source banner. The auto-add-to-library trigger (Stream's `autoAddToLibrary()` at `StreamDetailView.h:95`, called from `StreamPage`'s `progressUpdated` lambda) is reused as a pattern but moved earlier in the flow — fires when the user clicks the first download arrow, not when first chapter completes, since clicking the arrow IS the user intent per Batch C Q3. Rationale: same as StreamSearchWidget — Q_OBJECT + templates + ~600 LOC of domain semantics = clean fork.
- **`StreamDownloadIndex.{h,cpp}` (h:21)** — **FORK WITH ATTRIBUTION** to `src/core/manga/MangaDownloadIndex.{h,cpp}`. Same threadsafe canonical-key-keyed JSON-backed shape (`m_byPath` + `m_byEpisode`/`m_byChapter` + `m_imdbHasAny`/`m_seriesHasAny` derived maps, `validateAll` for disk reconciliation, `entriesChanged` signal). Different keying — `(sourceId, seriesId, chapterId)` instead of `(imdbId, season, episode)`. JSON file path: `<dataDir>/manga_downloads_index.json`. Rationale: shape is proven; types differ enough that template-parameterizing the index would be more work than copying it. Schema version 1, same convention.
- **`StreamDownloadsPage.{h,cpp}` (created NETFLIX_OVERHAUL P5)** — **REPLACE WITH NOTHING** per Batch C Q2. No cross-series Downloads page on the comics side. Significant divergence from Stream's blueprint — flagged here explicitly so the reuse-vs-divergence map is honest.
- **`MetaAggregator` (StreamSearchWidget ctor input)** — **NO REUSE**. Stream's catalog pipeline fans out to addon-registered Stremio catalogs. Manga side already has its own pattern: a hardcoded `QList<MangaScraper*> m_scrapers` populated in ctor (today's `TankoyomiPage`). The new ComicsTankoyomiSearchWidget keeps this pattern with the Rule-14 §3.6 implementation note about extracting into a small registry-style factory.
- **`TileStrip + TileCard` primitives (src/ui/widgets/)** — **REUSE DIRECTLY**. Already shared across ComicsPage and Stream surfaces. No changes needed to the primitives themselves.
- **`ToastHud` (src/ui/widgets/)** — **REUSE DIRECTLY**. Stream's `ToastHud` is the toast notification widget that handles the auto-launch toast on Stream-side and similar transient messages. Comics merger uses it verbatim for (a) the auto-add toast (§5.4) and (b) the source-failure toast on search (§3.6).
- **`SidebarDrawer` (src/ui/widgets/)** — **MINIMAL EDIT**. One mechanical change: remove the existing `PAGE_TANKOYOMI` drawer entry. Tankorent + TankoLibrary + Stream Downloads entries stay. The `MainWindow` routing for `PAGE_TANKOYOMI` is removed alongside.

### §6.2 Current Comics + Tankoyomi inventory dispositions

- **`ComicsPage.{h,cpp}` (`src/ui/pages/`)** — **EXTEND**. Existing structure preserved (Continue strip + library grid + search bar + view-toggle + density slider + sort combo). Search bar's placeholder + signal target are repurposed; new chip slots added to tiles for Tankoyomi-origin + DOWNLOADING; new branch on tile-click handler dispatching by provenance flag (Tankoyomi → new detail view; folder → today's SeriesView).
- **`SeriesView.{h,cpp}`** — **ABSORB UNCHANGED**. Used for folder-imported series.
- **`ComicReader.{h,cpp}`** — **ABSORB UNCHANGED**. Out of scope per Batch H Q1.
- **`LibraryScanner.{h,cpp}`** — **EXTEND**. Continues to walk root folders as today. New behaviour: when it encounters a series folder that has a `.tankoyomi-meta.json` sidecar (proposed name; Codex to confirm), it skips creating a folder-origin tile and lets the `comics_library.json` reader own that record. Without the sidecar, behaviour is unchanged.
- **`MainWindow.{h,cpp}`** — **EXTEND**. Remove `PAGE_TANKOYOMI` routing + corresponding sidebar wiring. Add the new ComicsPage tile-click dispatcher for badged-vs-unbadged routing. Wire `openComic(...)` from the new detail view (it already works for the existing flow).
- **`TankoyomiPage.{h,cpp}`** — **DELETE**.
- **`src/ui/pages/tankoyomi/MangaDetailView.{h,cpp}` (yesterday's C.5 ship)** — **DELETE**. Replaced by `ComicsTankoyomiDetailView`.
- **`src/ui/pages/tankoyomi/MangaResultsGrid.{h,cpp}`** — **DELETE**. Replaced by the new search-takeover grid.
- **`src/ui/pages/tankoyomi/TransferGroupCard.{h,cpp}`** — **DELETE**. No Transfers tab in merged Comics.
- **`src/ui/dialogs/AddMangaDialog.{h,cpp}`** — **DELETE**. Add is now silent-bookmark via detail-view button.
- **`MangaScraper.h` + concrete scrapers (`WeebCentralScraper`, `ReadComicsScraper`)** — **ABSORB UNCHANGED** as base; Codex to confirm whether `fetchDetail(seriesId) → SeriesDetail` is needed for the hero metadata (cover + synopsis + genres + author + year + status). If yes: add a virtual method + propagate to concretes. If no: enrich `MangaResult` at search time.
- **`MangaDownloader.{h,cpp}`** — **ABSORB**; possibly minor EXTEND. Mihon-parity API already shipped per memory 2264 / 2374. The new auto-add flow (§5.4) needs to fire on `startDownload(...)` rather than on completion. Codex to confirm whether the emit-while-locked pattern flagged in observation 2551 needs hardening as part of this arc or is non-blocking.
- **`MangaResult.h` + ChapterInfo + PageInfo** — **ABSORB UNCHANGED** for v1; possibly EXTEND for hero metadata depending on the §6.2 scraper-interface decision.

### §6.3 New files to create

- `src/ui/pages/comics/ComicsTankoyomiSearchWidget.{h,cpp}`
- `src/ui/pages/comics/ComicsTankoyomiDetailView.{h,cpp}`
- `src/core/manga/MangaDownloadIndex.{h,cpp}`
- `src/core/manga/ComicsLibraryRecord.{h,cpp}` (or merge into a small singleton — Codex to recommend shape)
- Possibly `src/ui/widgets/ChipBadge.{h,cpp}` if not already extracted from Stream's chip rendering

### §6.4 Icons (resources/icons/)

Stream's Netflix overhaul shipped `downloads.svg`, `download-arrow.svg`, `pause-circle.svg`, `play-circle.svg`, `retry-arrow.svg`. The Comics merger should REUSE these verbatim where applicable. No new icons should be required unless the Tankoyomi-origin chip wants a custom glyph beyond a text chip — per Batch D Q2 (generic "Tankoyomi" text label), no glyph needed.

---

## §7 Persistence + provenance

### §7.1 Library records

- **`comics_library.json`** (new file under `<dataDir>`) — holds Tankoyomi-origin library records: `[{seriesId, title, origin: "tankoyomi", sourceId, addedAt, coverPath, rootFolder, lastSeenChapterListCacheRef, ...}, ...]`. JsonStore-backed, same pattern as Stream's `StreamLibrary`.
- **Folder-origin records** stay represented purely by their on-disk folder + LibraryScanner cache (no new JSON file). The cleanest source-of-truth invariant (Codex to validate): a tile in the ComicsPage grid comes from one of two sources — either `comics_library.json` (Tankoyomi-origin) or `LibraryScanner` (folder-origin). A series in `comics_library.json` is NEVER also in the LibraryScanner walk (because the LibraryScanner skips folders that have a `.tankoyomi-meta.json` sidecar). This avoids double-tiles. Codex to confirm this invariant works for the case where the user deletes the sidecar manually.

### §7.2 Chapter index

- **`manga_downloads_index.json`** (new file under `<dataDir>`) — canonical-key-keyed map of downloaded chapters; threadsafe; `validateAll` for disk reconciliation; `entriesChanged` signal. Forked from `StreamDownloadIndex` shape (§6.1).

### §7.3 Hidden per-series sidecar

- **`<seriesFolder>/.tankoyomi-meta.json`** (proposed filename; Codex to confirm preferred name) — stored inside each Tankoyomi-origin series folder. Holds: source ID, manga ID, scraper version (so future scraper upgrades can know what they're updating), last-seen chapter list cache (for offline-source path § 3.5), and a creation-timestamp tying back to `comics_library.json`'s `addedAt`. Hidden file convention (`.` prefix) so it doesn't clutter File Explorer.

### §7.4 Existing files (preserved or dropped)

- **`manga_downloads.json` (existing TankoyomiPage downloader records)** — clean-slate dropped on merger ship per Batch G Q2. Codex to confirm whether a one-time archival copy should be preserved at `<dataDir>/manga_downloads.json.pre-merger-backup` for forensic recovery before deletion. Recommend yes.
- **`manga_history.json` (existing TankoyomiPage download history)** — same clean-slate drop. Same archival recommendation.

---

## §8 Out of scope (Phase 1 boundaries — Batch H locked)

- **Comic Reader** — `ComicReader.{h,cpp}`, `PageCache.{h,cpp}`, `SmoothScrollArea.{h,cpp}` and related — untouched.
- **Theme system** — `Theme.{h,cpp}`, `ThemePicker.{h,cpp}` and all per-theme resources — untouched. Use existing tokens (the same `Theme::kChipBg` shade Stream's DOWNLOADED chip uses, existing surface/text tokens for the detail view).
- **Books / Videos / Stream as code paths** — `BooksPage`, `VideosPage`, `StreamPage`, `StreamDetailView`, `StreamLibraryLayout` etc. — untouched (they are read for reference but not modified).
- **Tankorent / TankoLibrary** — Agent 4B's narrowed lane. Untouched.
- **Player** — irrelevant to this arc.
- **SidebarDrawer touch — IN SCOPE but mechanical** — the existing `PAGE_TANKOYOMI` entry is removed; corresponding `MainWindow` routing is removed. Tankorent + TankoLibrary + Stream Downloads entries stay.
- **Phase 2 (writing-plans) and Phase 3 (executing-plans)** — separate Hemanth fires after Codex's review-and-expand of THIS doc lands.

---

## §9 Deferred follow-ups (not blocking v1; flagged for future arcs)

- **Volume / story-arc grouping** as a Stream-season-style organising layer for the chapter list. Hemanth's directive: "if you think season-system but with volumes or story arcs is possible, then we will save it for a future version, so make a note of it." Implementation cost when picked up: scraper interface extension to expose `volume` / `arc` per chapter, UI surface change in the detail view's chapter section (collapsible headers vs combo selector — open product question for v2).
- **Third-party / user-installed sources (extensions)** — sealed in v1; the v1 scraper registry is designed not to block a v2 plugin architecture (§3.6). v2 would design either drop-in config files or compiled plugin modules; security surface is the load-bearing concern.
- **Per-source enable/disable toggle in settings** — not in v1 per Batch F Q2. Trivial to add as a checkbox row when desired.
- **Coordination ask to Agent 5** — a Tankoyomi-origin-specific chip accent token (distinct from `Theme::kChipBg`) would visually separate provenance badges from state badges. Out of this arc; Agent 5 picks up on next theme arc.
- **Right-click chapter row → "Show alternate source"** — Stream has this for episodes (`alternateStreamRequested` signal). Manga equivalent would let the user, on a downloaded chapter, switch to a different source's version of the same chapter. v2 feature; not in v1's right-click menu (Hemanth's Mihon picks named the v1 menu items explicitly).
- **One-time archival of `manga_downloads.json` + `manga_history.json` before drop** — recommended belt-and-suspenders for forensic recovery; trivial to implement at migration time.

---

## §10 Risks + open items for Codex review-and-expand (gov-v4 Rule 20)

Per the brief and the chat.md:3640+ revision: Codex (Agent 7) reviews AND expands this brainstorm-md in place. Codex edits this file directly with HTML-comment attribution markers (`<!-- Codex 2026-05-14: ... -->`) per added or rewritten section. One Codex pass total — after expansion lands, Agent 1 reads the merged result, then Hemanth fires `/superpowers:writing-plans` directly.

The brief asks Codex to: (a) verify the doc matches Hemanth's vision (Comics absorbs Tankoyomi with Stream-as-blueprint), and (b) APPEND architectural / scope / flow / persistence / coordination gaps Agent 1 missed.

Agent 1's specific asks for Codex's review-and-expand pass (Codex may also raise anything else):

1. **Scraper interface extension for hero metadata.** §3.2 names cover + synopsis + genres + author + year + status as the hero content. Today's `MangaResult` carries title + thumbnail + sourceId but probably not all of those. Codex to check both `WeebCentralScraper` and `ReadComicsScraper` for what's exposed today and recommend: either (a) add a virtual `fetchDetail(seriesId) → SeriesDetail` method to `MangaScraper` (introduces a new async signal pattern), or (b) enrich `MangaResult` at search-time to carry all hero fields, or (c) some hybrid (search-time minimal preview + on-demand fetchDetail for hero population). Whichever choice Codex recommends, the doc should append the rationale + file:line cites for the scraper sites that need extending.
2. **Auto-copy on root-folder-change implementability.** §5.6 names auto-copy as Hemanth's ideal with a fallback. Codex to spec the implementability — file move semantics on Windows (cross-volume copy + delete vs same-volume rename), progress UX, what happens if a copy fails mid-way (rollback or partial-state recovery), interaction with in-flight downloads, atomicity guarantees. If non-trivial, document the fallback path as the v1 default with auto-copy deferred.
3. **MangaDownloader emit-while-locked pattern (memory 2551).** Codex to evaluate whether this existing pattern in `MangaDownloader.{cpp}` needs hardening as part of this arc — specifically, the new auto-add toast flow (§5.4) calls `startDownload` → triggers signal emit → ToastHud rendering, all potentially crossing thread boundaries. If the existing pattern is unsafe under the new call pattern, scope-up this arc with the hardening; otherwise note as a non-blocking follow-up.
4. **Provenance source-of-truth invariant.** §7.1 proposes "every tile is sourced from either `comics_library.json` OR `LibraryScanner`, never both." Codex to validate this invariant under all edge cases: user deletes the `.tankoyomi-meta.json` sidecar manually; user adds two roots and the same series folder exists in both; user renames the series folder; user moves files between roots while the app is running. Append failure-mode handling for each.
5. **Phase sequencing.** §4 sketches 6-7 phases. Codex to validate the granularity — should some phases split (e.g., search bar repurpose splitting from result-takeover view fork), or some merge? The plan-mode skill in Phase 2 will firm this up, but pre-flagging risk reduces plan-mode churn.
6. **Anything else Codex spots.** Particularly: gaps in the scope fence, missing flow descriptions, unstated invariants, theme-system coordination items that need a heads-up to Agent 5, additional v2 follow-ups worth noting.

---

<!-- Codex review-and-expand pass appends inline below this marker. Each addition or rewrite must carry an HTML-comment attribution marker (e.g. `<!-- Codex 2026-05-14: <one-line summary> -->`) per gov-v4 Rule 20 (2026-05-14 ~5:35pm revision). One pass total. After expansion, Agent 1 reads the merged result and Hemanth fires /superpowers:writing-plans directly. -->

<!-- Codex 2026-05-14: Confirms the draft matches Hemanth's vision and lists superseding corrections for plan-mode. -->
## §11 Codex pass verdict + corrections to feed writing-plans

The draft matches Hemanth's locked vision. The core shape is correct: Comics absorbs Tankoyomi, the standalone Tankoyomi page disappears, Stream is treated as the blueprint for search/detail/library-download flow, Tankoyomi-origin series get a provenance badge, and the reuse-vs-fork-vs-replace map is treated as the load-bearing artifact.

Writing-plans should treat these corrections as superseding earlier wording where they conflict:

1. **`LibraryScanner` path correction.** The current scanner is `src/core/LibraryScanner.{h,cpp}`, not `src/core/scan/LibraryScanner.h`. It groups comic archives through `ScannerUtils::groupByFirstLevelSubdir(...)` at `src/core/LibraryScanner.cpp:48-53`.
2. **`ToastHud` path correction.** The reusable toast widget is `src/ui/player/ToastHud.{h,cpp}`, with the widget declared at `src/ui/player/ToastHud.h:9`. The existing Tankoyomi page already uses `Toast::show(...)` for partial source failures at `src/ui/pages/TankoyomiPage.cpp:155-168`.
3. **`StreamDownloadsPage` correction.** No `StreamDownloadsPage.{h,cpp}` exists under `src/` in the current tree. The comics-side disposition is still **REPLACE WITH NOTHING**, but plan-mode should not create a "delete StreamDownloadsPage" task. The concrete work is: do not create a comics cross-series downloads page, and remove the old Tankoyomi Transfers tab with `TankoyomiPage`.
4. **Scraper disposition correction.** `MangaScraper` should be **EXTEND**, not "ABSORB UNCHANGED", because the hero view needs metadata the current interface cannot fetch. Current contract is only `search`, `fetchChapters`, and `fetchPages` with result/chapter/page signals (`src/core/manga/MangaScraper.h:20-28`).
5. **`MangaResult.h` file disposition correction.** The existing `MangaResult` struct already carries `id`, `url`, `title`, `author`, `thumbnailUrl`, `source`, `status`, and `type` (`src/core/manga/MangaResult.h:6-15`). Do not overload it into a full hero cache. Add a separate detail struct in the same file or a sibling header.
6. **`TileCard` badge correction.** The existing badge surface is painter-based inside `TileCard::applyBadges()` (`src/ui/pages/TileCard.cpp:243-333`) and exposed through `TileCard::setBadges(...)` (`src/ui/pages/TileCard.h:17-19`). Plan-mode should prefer extending `TileCard` with explicit provenance/state badge slots over inventing `ChipBadge.{h,cpp}`. `ChipBadge` is only justified if Agent 5 asks for extraction during coordination.
7. **Agent 5 coordination correction.** `TileCard`, `TileStrip`, `ScannerUtils`, and `LibraryScanner` are Agent 5 primary-owned surfaces per `agents/GOVERNANCE.md:32`. Agent 1 can still own the arc, but writing-plans should include a heads-up/coordination step before editing those shared library UX primitives.

<!-- Codex 2026-05-14: Resolves the scraper metadata open item with a hybrid fetchDetail design. -->
## §12 Scraper metadata decision for the Stream-style hero

Recommendation: use a **hybrid** design.

1. Keep `MangaResult` as the search preview shape. It is enough for result cards because WeebCentral search already fills thumbnail, title, author, status, source, and type (`src/core/manga/WeebCentralScraper.cpp:128-183`), while ReadComicsOnline search fills title, slug/id, URL, source, type, and a predictable cover URL (`src/core/manga/ReadComicsScraper.cpp:49-64`).
2. Add a new detail shape, proposed `MangaSeriesDetail`, with:
   - `MangaResult preview`
   - `QString synopsis`
   - `QStringList genres`
   - `QString year`
   - `QString status`
   - `QString author`
   - `QString heroCoverUrl`
   - `QString sourceUrl`
   - `QList<ChapterInfo> cachedChapters` only if the source page fetch naturally returns the chapter list too
3. Extend `MangaScraper` with:
   - `virtual void fetchDetail(const MangaResult& preview) = 0;`
   - signal `detailReady(const MangaSeriesDetail& detail);`
4. Detail view flow:
   - open immediately from the search result using preview title/cover/source;
   - show cached detail from `comics_library.json` or the sidecar when present;
   - call `fetchDetail(preview)` on entry;
   - update the hero when `detailReady` arrives;
   - call `fetchChapters(...)` only if `detailReady` did not already include a chapter list.

Why not search-time enrichment:

1. ReadComicsOnline search currently uses a JSON suggestions API and only exposes title/slug plus the derived cover URL (`src/core/manga/ReadComicsScraper.cpp:45-64`). Fetching every result page just to populate synopsis/genres/year would make search slow and fragile.
2. WeebCentral search exposes more than ReadComicsOnline, but still not the full hero payload. The parser extracts title, author, thumbnail, and status from the result card (`src/core/manga/WeebCentralScraper.cpp:128-183`), not synopsis/genres/year.
3. The Stream blueprint already separates preview from detail: `StreamSearchWidget` emits a `MetaItemPreview` on activation (`src/ui/pages/stream/StreamSearchWidget.h:35-39`), then `StreamDetailView::showEntry(...)` paints from a preview hint while resolving richer metadata (`src/ui/pages/stream/StreamDetailView.h:49-57`). The manga side should mirror that shape.

The `mediaType` question does not need a new virtual method for v1. `MangaResult::type` already exists (`src/core/manga/MangaResult.h:14`), and both scrapers set it today (`src/core/manga/WeebCentralScraper.cpp:129-130`, `src/core/manga/ReadComicsScraper.cpp:56-58`). If a future source can return mixed media, per-result `type` is still the right model.

<!-- Codex 2026-05-14: Adds the missing detail-cache/offline invariant that falls out of fetchDetail. -->
## §13 Detail cache and offline detail flow

The offline-source behaviour in §3.5 needs one explicit persistence rule: the detail page must render from cache before it attempts a network refresh.

Recommended source order on detail open:

1. `comics_library.json` record by `(sourceId, seriesId)`.
2. `<seriesFolder>/.tankoyomi-meta.json`, if the folder is available.
3. In-memory search preview from the click path.
4. Network `fetchDetail(preview)`.

The app should store the last successful detail payload in the library record and write the same source/series identity into the sidecar. The sidecar can hold a small chapter-list cache for the offline path, but `comics_library.json` remains the app-level source of truth for whether the tile is Tankoyomi-origin.

This prevents a bad cold-start state where a library tile exists but the source is offline and the detail page has only a title. It also makes "Add to Library with zero chapters on disk" real: the tile can survive with cover/title/meta before any chapter file exists.

<!-- Codex 2026-05-14: Specifies auto-copy/root-change semantics and recommends fallback as v1 default unless split into its own phase. -->
## §14 Root-folder-change and auto-copy decision

Recommendation: make **fallback validation the v1 default**, and treat auto-copy as a separate optional phase only if writing-plans has enough room to isolate it.

Reason: current root-folder plumbing emits after mutation. `CoreBridge::addRootFolder(...)` and `removeRootFolder(...)` write `library_state.json` and emit `rootFoldersChanged(domain)` after the fact (`src/core/CoreBridge.cpp:80-121`). `RootFoldersOverlay` calls those directly from add/remove UI actions (`src/ui/RootFoldersOverlay.cpp:148-169`). There is no current "replace old comics root with new comics root" transaction that hands consumers both old root and new root before the setting changes.

Windows file semantics make auto-copy non-trivial:

1. Same-volume folder move can be a rename-like operation.
2. Cross-volume move is copy-then-delete. It is not atomic.
3. A partial copy can leave old files, new files, or both.
4. Antivirus/file locks can fail a file copy after several files already moved.
5. If downloads are active, the downloader may write into the old root while the migration is copying.

If auto-copy ships in v1, it should be a separate phase with this shape:

1. Add a root-change coordinator before mutating the root list. It captures old roots and new roots, then runs the migration.
2. Pause or cancel active downloads for affected Tankoyomi-origin series before moving files.
3. For each `comics_library.json` record, use the record's own `rootFolder` and `seriesFolderName`; do not infer by title.
4. Copy into a staging directory under the new root, for example `<newRoot>/.tankoyomi-migration/<sourceId>_<seriesId>.tmp`.
5. Verify copied file count and byte sizes. Hashing every archive is stronger but may be slow for large libraries; count+size is the minimum.
6. Rename the staged folder into its final name inside the new root. That final rename is same-volume and can be treated as the atomic publish step.
7. Only after publish succeeds, update the record's `rootFolder`, `seriesFolderName`, sidecar path, and `MangaDownloadIndex` paths.
8. Delete the old folder after record update. If delete fails, keep the new record and surface a plain "old copy could not be removed" message.
9. Persist `migrationState` in the record while the operation is in progress so app restart can resume or mark the record as needing repair.

If any step fails, do not roll the library record back to a guessed state. Keep the record, mark chapters as missing after `validateAll`, and keep the tile/badge visible. This matches Hemanth's "tiles stay, chapters missing, re-download per chapter" fallback and avoids destructive recovery.

<!-- Codex 2026-05-14: Resolves the MangaDownloader emit-while-locked ask for the new toast flow. -->
## §15 MangaDownloader signal hardening decision for auto-add toast

The new auto-add toast does **not** require a downloader hardening phase if the flow is wired correctly.

Current observations:

1. `MangaDownloader::startDownload(...)` writes the record under `m_mutex`, exits the lock, then calls `saveRecords()`, emits `downloadUpdated`, emits per-chapter `chapterUpdated`, and enters `processQueue()` (`src/core/manga/MangaDownloader.cpp:241-252`).
2. `processQueue()` explicitly unlocks before emitting `chapterUpdated(...)` and starting the chapter fetch (`src/core/manga/MangaDownloader.cpp:275-284`).
3. Pause/resume/retry/start-now paths collect affected IDs under lock, then save/emit after the lock (`src/core/manga/MangaDownloader.cpp:659-803`, `src/core/manga/MangaDownloader.cpp:807-859`).
4. The main remaining store-under-lock wart is completion history append inside the completion block (`src/core/manga/MangaDownloader.cpp:507-521`). That is not on the new auto-add toast path.

Required wiring rule: do not make `MangaDownloader` responsible for adding the series to the Comics library or showing the toast. The detail view should do this on the GUI thread before calling `startDownload(...)`:

1. `ComicsTankoyomiDetailView` receives a chapter/range/multi-select download request.
2. It calls `ComicsTankoyomiLibrary::ensureAdded(detail)` if the record is absent.
3. If `ensureAdded` created a new record, it shows `Toast::show(window(), "Added <title> to your library")`.
4. Then it calls `MangaDownloader::startDownload(...)`.

This keeps the UI side effect out of downloader signals, avoids thread-affinity surprises, and makes the "clicking a chapter automatically adds the series" product rule true even if the download fails immediately after enqueue.

Non-blocking follow-up: after this arc, Agent 1 can still do a downloader cleanup pass to standardize every mutation method around "collect under lock, save/emit off lock" and to move completion history writes out of the mutex. It is not a blocker for §5.4.

<!-- Codex 2026-05-14: Tightens the provenance source-of-truth invariant and covers requested edge cases. -->
## §16 Provenance source-of-truth invariant and edge cases

Corrected invariant: **`comics_library.json` is the source of truth for Tankoyomi-origin records. The sidecar is a disk recovery hint and scanner-skip hint, not the source of truth.**

Records should be keyed by `(sourceId, seriesId)`, not by title. Each record stores:

1. `sourceId`
2. `seriesId`
3. `title`
4. `origin: "tankoyomi"`
5. `rootFolder`
6. `seriesFolderName`
7. `canonicalSeriesPath`
8. `coverPath`
9. `detailCache`
10. `addedAt`
11. `lastValidatedAt`

Edge handling:

1. **User deletes `.tankoyomi-meta.json` manually.** The tile remains Tankoyomi-origin because `comics_library.json` says so. `LibraryScanner` must skip any canonical folder path claimed by a Tankoyomi record even if the sidecar is missing. On next detail open or validate pass, rewrite the sidecar.
2. **User adds two Comics roots and the same title exists in both.** Do not merge by title. A Tankoyomi record points to its own `(rootFolder, seriesFolderName, sourceId, seriesId)`. A folder-imported `Berserk` in another root can remain a separate folder-origin tile. If the exact same canonical path is seen twice through duplicate roots, dedupe by canonical path.
3. **User renames the series folder.** If the old `canonicalSeriesPath` is missing, scan all Comics roots for `.tankoyomi-meta.json` with the same `(sourceId, seriesId)`. If found, update `rootFolder`, `seriesFolderName`, and `canonicalSeriesPath`. If not found, keep the library record and badge, mark chapters missing, and allow re-download. Do not infer identity from folder name alone.
4. **User moves files between roots while the app is running.** `MangaDownloadIndex::validateAll` should reconcile paths on show/detail open. Scanner merge should run as a single generation: collect folder-origin results, load Tankoyomi records, suppress claimed paths, then publish the merged tile list. If a root-change signal arrives mid-scan, cancel or discard the old generation and restart.
5. **User removes from library but keeps files.** Delete the library record and sidecar. On next scan, the folder becomes folder-origin with no badge, as §5.5 already says.
6. **User removes from library and deletes files.** Cancel active downloads first, remove the library record, evict index entries for `(sourceId, seriesId)`, then delete the folder. If folder deletion fails, keep the record removed but show a plain failure message telling the user the files remain on disk.

This means `LibraryScanner` must accept either a skip-set or a resolver callback built from `comics_library.json`. A sidecar-only skip is not enough. Current scanner has no provenance hook; it simply groups first-level folders and emits `SeriesInfo` (`src/core/LibraryScanner.cpp:48-77`, `src/core/LibraryScanner.cpp:132-136`).

<!-- Codex 2026-05-14: Expands the reuse map with Stream surfaces that were missing from the first inventory. -->
## §17 Additional Stream-side reuse dispositions missing from §6

These dispositions keep the "Stream is blueprint" promise honest without importing video-specific machinery into Comics.

1. **`StreamLibrary.{h,cpp}` (`src/core/stream/StreamLibrary.h:13-51`, `src/core/stream/StreamLibrary.cpp:16-45`)** - **FORK/PATTERN REUSE** to `ComicsTankoyomiLibrary.{h,cpp}`. Reuse the add/remove/libraryChanged shape and the remove-evicts-index idea. Do not reuse the imdb-keyed struct directly.
2. **`StreamSourceChoice.{h,cpp}` (`src/ui/pages/stream/StreamSourceChoice.h:23-77`)** - **REPLACE WITH NOTHING** for v1. Manga/comics downloads do not have a per-episode stream-source picker. Source identity is the scraper source selected by the search result.
3. **`StreamSourceList.{h,cpp}` + `StreamSourceCard.{h,cpp}` (`src/ui/pages/stream/StreamSourceList.h:25-53`, `src/ui/pages/stream/StreamSourceCard.h:19-37`)** - **REPLACE WITH NOTHING**. The right-pane source list has no comics equivalent in v1. Do not create an alternate-source pane.
4. **`StreamAggregator` (`src/core/stream/StreamAggregator.h:26-28`)** - **REPLACE WITH NOTHING**. Manga page fetching is already owned by `MangaScraper::fetchPages(...)`; no stream fan-out is needed.
5. **`AddonRegistry`, `AddonTransport`, `Descriptor`, `Manifest`, `ResourcePath`, `MetaItem`, `StreamInfo`, `StreamSource`, `SubtitleInfo` (`src/core/stream/addon/*`)** - **REPLACE WITH NOTHING for v1**, **PATTERN REUSE only for future extensions**. The v1 scraper registry should stay small and hardcoded, with string source IDs, so future plugin work does not rewrite consumers.
6. **`CatalogAggregator` + `CatalogBrowseScreen` (`src/core/stream/CatalogAggregator.h:27-29`, `src/ui/pages/stream/CatalogBrowseScreen.h:30-32`)** - **REPLACE WITH NOTHING**. Hemanth explicitly picked no trending/home catalog rows for v1 search. Do not build a manga catalog browser.
7. **`CalendarEngine` + `CalendarScreen` (`src/core/stream/CalendarEngine.h:56-58`, `src/ui/pages/stream/CalendarScreen.h:24-26`)** - **REPLACE WITH NOTHING**. No manga release calendar in v1.
8. **`AddonManagerScreen` + `AddonDetailPanel` (`src/ui/pages/stream/AddonManagerScreen.h:23-25`, `src/ui/pages/stream/AddonDetailPanel.h:24-26`)** - **REPLACE WITH NOTHING**. No source manager in v1 per §3.6.
9. **`StreamPlayerController`, `StreamServerEngine`, `StreamServerClient`, `StreamServerProcess`, `SubtitlesAggregator` (`src/ui/pages/stream/StreamPlayerController.h:18-20`, `src/core/stream/stremio/StreamServerEngine.h:48-49`, `src/core/stream/stremio/StreamServerClient.h:29-30`, `src/core/stream/SubtitlesAggregator.h:27-29`)** - **REPLACE WITH NOTHING**. These are video playback substrate.
10. **`StreamBulkPlan`, `BulkSourceCollector`, `BulkPackVerifier` (`src/core/stream/StreamBulkPlan.h:43-149`, `src/core/stream/BulkSourceCollector.h:25-49`, `src/core/stream/BulkPackVerifier.h:27`)** - **PATTERN REUSE ONLY**. The useful pattern is "precompute a range/selection plan before enqueue, report skipped/already-downloaded items, then hand a stable list to the downloader." Do not port stream source-collection or torrent-pack verification.
11. **`StreamRescueScanner` (`src/core/stream/StreamRescueScanner.h:22-24`)** - **PATTERN REUSE LATER, NOT v1**. Sidecar-based recovery can cover renamed/moved Tankoyomi folders, but a dedicated rescue scanner is not required for first ship unless root migration is promoted.
12. **`StreamProgress` (`src/core/stream/StreamProgress.h:17-65`)** - **REPLACE WITH EXISTING COMICS PROGRESS**. Comics already uses `CoreBridge::progress("comics", ...)` through the reader and pages (`src/core/CoreBridge.h:33-37`). Do not create parallel manga reading progress.
13. **`StreamTelemetryWriter` (`src/core/stream/StreamTelemetryWriter.h:32`)** - **REPLACE WITH NOTHING**. No telemetry-specific surface for this arc.
14. **`StreamContinueStrip` + `StreamHomeBoard`** - keep the existing §6 disposition: no class fork. The comics continue strip already exists at `src/ui/pages/ComicsPage.h:67-69` and is populated in `src/ui/pages/ComicsPage.cpp:639-731`.

<!-- Codex 2026-05-14: Adds Tankoyomi-side dispositions missing or too broad in the first map. -->
## §18 Additional Tankoyomi-side dispositions

1. **`ChapterDownloadIndicator.{h,cpp}` (`src/ui/pages/tankoyomi/ChapterDownloadIndicator.h:10-21`, `src/ui/pages/tankoyomi/ChapterDownloadIndicator.cpp:54-81`)** - **ABSORB/REUSE DIRECTLY** unless the new detail view needs a namespace move. It already implements the five-state tap-only chapter control Hemanth locked. Keep it monochrome via palette colors; do not introduce colored status dots.
2. **`ChapterRangeDialog.{h,cpp}` (`src/ui/pages/tankoyomi/ChapterRangeDialog.h:13-22`)** - **ABSORB/REUSE DIRECTLY** for the "Download Range..." flow if the current dialog is clean. It already matches the range-download decision in §3.3.
3. **`MangaDetailView` current code** - **REPLACE AS A PAGE, ABSORB SELECT PARTS**. The current widget is too tied to the old Tankoyomi tab, but its chapter table, multi-select bar, range/dropdown wiring, and `ChapterDownloadIndicator` integration are useful reference (`src/ui/pages/tankoyomi/MangaDetailView.cpp:151-227`, `src/ui/pages/tankoyomi/MangaDetailView.cpp:573-599`, `src/ui/pages/tankoyomi/MangaDetailView.cpp:614-677`).
4. **`TankoyomiPage::ensureCover(...)` and poster cache** - **ABSORB PATTERN**. The new Comics search/detail path still needs the cache directory and cover-ready signal shape. Current cache dir is `<GenericDataLocation>/Tankoban/data/manga_posters` at `src/ui/pages/TankoyomiPage.cpp:95-98`; `ensureCover` is declared at `src/ui/pages/TankoyomiPage.h:36-39`.
5. **`TankoyomiPage` search fan-out** - **ABSORB PATTERN, NOT CLASS**. Current ctor creates both scrapers, stores them in `m_scrapers`, and connects success/error fan-in (`src/ui/pages/TankoyomiPage.cpp:100-108`, `src/ui/pages/TankoyomiPage.cpp:134-168`). Move this into a small `MangaSourceRegistry` plus the new search widget.
6. **`TransferGroupCard`** - keep §6 disposition: **DELETE**. Per-series controls move into the new detail page; cross-series transfers do not survive.

<!-- Codex 2026-05-14: Refines implementation phase sequencing so writing-plans does not delete Tankoyomi too early. -->
## §19 Phase sequencing recommendation

The 6-7 phase sketch should split a little more. The risky mistake would be deleting `TankoyomiPage` before the replacement Comics path can search, open detail, and enqueue a chapter.

Recommended plan granularity:

1. **Phase 1 - data contracts and registry.** Add `MangaSeriesDetail`, `fetchDetail`, `detailReady`, and a small scraper registry. Keep old Tankoyomi UI compiling.
2. **Phase 2 - library store and provenance merge.** Add `ComicsTankoyomiLibrary`, `comics_library.json`, sidecar writer/reader, scanner skip-set/resolver, and the two-source tile merge. Keep UI mostly unchanged.
3. **Phase 3 - search takeover shell.** Repurpose the Comics search bar to enter Tankoyomi search mode, add Back-to-library behavior, and fork `StreamSearchWidget` into `ComicsTankoyomiSearchWidget`. Do not delete local folder tiles.
4. **Phase 4 - Stream-style detail hero and Add/Remove Library.** Add `ComicsTankoyomiDetailView` with preview-first hero, `fetchDetail` refresh, cache fallback, and silent Add/Remove. No downloads yet except existing reader open for already-downloaded files.
5. **Phase 5 - chapter rows and downloader integration.** Wire `ChapterDownloadIndicator`, range/multi-select download, auto-add-before-download toast, per-series queue controls, and `MangaDownloadIndex::validateAll`.
6. **Phase 6 - provenance edge handling.** Implement sidecar rewrite, missing-sidecar recovery, renamed-folder recovery, duplicate-root/canonical-path dedupe, and folder-origin/Tankoyomi-origin merge rules.
7. **Phase 7 - root-change fallback.** Ship the safe fallback first: records persist, badges stay, validate marks chapters missing. If auto-copy is promoted, it should be a separate subphase with migration state, not bundled into generic persistence work.
8. **Phase 8 - remove old Tankoyomi surface.** Remove `PAGE_TANKOYOMI`, sidebar entry, `TankoyomiPage`, old `MangaDetailView` page, `MangaResultsGrid`, `TransferGroupCard`, and `AddMangaDialog` only after Phases 3-5 are compile-green.
9. **Phase 9 - polish, nav, and smoke.** Update Comics `INavStateProvider` to capture library/search/detail modes, verify Back/Forward semantics, offline-source banner, root-change fallback, remove/keep-files flow, and no cross-series Downloads page.

This sequencing keeps Hemanth's visible path usable throughout: first the data model, then search, then detail, then downloads, then deletion of the old standalone source page.

<!-- Codex 2026-05-14: Adds navigation and state-flow gaps for the merged Comics page. -->
## §20 Navigation/state gaps to cover in plan-mode

The existing Comics nav provider only captures local library search text and grid scroll (`src/ui/pages/ComicsPage.cpp:879-918`). After the merger, Comics has three modes:

1. library grid/continue strip;
2. Tankoyomi search results;
3. Tankoyomi-origin detail.

Plan-mode should add an explicit mode discriminator to Comics nav state. Restoring a Tankoyomi search result should not accidentally treat the query as local folder filtering, because v1 removes local-library search from that bar. Restoring a Tankoyomi detail should use `(sourceId, seriesId)` plus cached detail; if the cache is missing, fall back to the search results or library tile rather than blocking on a network fetch.

Back behaviour should be locked before implementation:

1. From search results, Back returns to library.
2. From Tankoyomi detail opened from search, Back returns to those search results.
3. From Tankoyomi detail opened from a library tile, Back returns to library at the prior scroll position.
4. Global Back/Forward should record the transition before swapping stacked pages, matching the recent `TankoyomiPage::navigationRequested` pattern (`src/ui/pages/TankoyomiPage.h:51-53`).

<!-- Codex 2026-05-14: Adds final scope fences and UI compliance notes for writing-plans. -->
## §21 Scope and UI compliance notes

1. Do not propose QML for this arc. The implementation stays in the current Qt Widgets stack.
2. Do not introduce color-coded manga states. Use palette-derived gray/black/white styling and existing SVG icons. `ChapterDownloadIndicator` already paints from the widget palette (`src/ui/pages/tankoyomi/ChapterDownloadIndicator.cpp:60-80`), which is the right direction.
3. Do not add a separate "Saved" list. The Add button writes the library record directly.
4. Do not add a cross-series downloads page. Tile chips and per-series detail controls are the only v1 download surfaces.
5. Do not migrate old `manga_downloads.json` into Tankoyomi-origin records. Keep Agent 1's clean-slate decision. The only addition I recommend is a one-time backup copy before deletion, as already noted in §7.4.
6. Do not let sidecar presence alone create a Tankoyomi badge. The badge comes from `comics_library.json`; sidecar only helps recovery and scanner suppression.
7. Do not delete old Tankoyomi files/classes until the replacement path has compile-green search, detail, add/remove, and at least one chapter enqueue path.
