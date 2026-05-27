# Books mode → Stremio-style pivot — Design spec

- **Date:** 2026-05-20
- **Author:** Agent 2 (Book Reader + TankoLibrary; TankoLibrary ownership transferred from Agent 4B same day at 4B's brotherhood departure).
- **Arc tag:** `BOOKS_STREMIO_PIVOT`
- **Status:** Phase 1 brainstorm — pending Codex (Agent 7) review-AND-EXPAND in place per gov-v4 Rule 20 (revised 2026-05-14 ~5:35pm at `agents/chat.md:3640+`). After Codex's expansion lands inline (HTML-comment attribution markers per added/rewritten section), Hemanth fires `/superpowers:writing-plans` directly. No second Codex pass; no separate audit file.
- **Phase 2 (writing-plans, separate Hemanth fire):** `docs/superpowers/plans/2026-05-20-books-stremio-pivot.md`
- **Phase 3 (executing-plans, separate Hemanth fire):** multi-summon arc via `/superpowers:subagent-driven-development` per `feedback_plan_first_zero_errors.md` and `feedback_no_pause_between_subagent_tasks.md`.
- **Mockup:** `docs/superpowers/mockups/2026-05-20-books-stremio-pivot/series-detail-stormlight.html` (Hemanth-approved 2026-05-20 ~6:30pm; standalone HTML, opens in any browser via `file://`; ships with a Comics-side substitution map at the bottom for Agent 1).
- **Skills invoked (Phase 1):** `/brief`, `/superpowers:brainstorming`, `/superpowers:verification-before-completion` (before announcing this doc ready for Codex).
- **Coordination boundaries:** Stream (Agent 4), Comics (Agent 1), Video player (Agent 3), Theme (Agent 5) — all untouched. Tankorent + Comics cross-coordination at writing-plans time (see §9).

---

## §1 Goal — Hemanth's vision verbatim

The vision was locked 2026-05-20 ~3:00pm at the brainstorm kickoff:

> "changing book mode into the same as comic mode and stream mode. it will use tankolibrary as the source, it needs a metadata api like cinemta and anilist and we will have a catalaogue of books and then they will be connectedt to tankolibrary. the same way comic mode is to tankoyomi and stream mode is to to tankorrent."

Two additional load-bearing quotes from later in the same brainstorm session:

> "we will build a full-stremio-style discovery board but it will be like the theatre mode where it has the catalogue button which presents basically the same movies and TV shows in the actual stremio's catalogue. And we need to build something like this for comics too." (Catalogue-button framing — explicit Comics cross-arc directive included.)

> "no more manual, they dissapear. having manual books makes the whole stremio but for books system hard ... there is this one historical anecdote about a japanese commander ordering his troops to drown all the ships to thee bottom of the ocean to drive home the point that there is no going back. I deleted them all brother." (Burn-the-ships call — Hemanth deleted his own library mid-brainstorm to remove retreat.)

The arc tag `BOOKS_STREMIO_PIVOT` reflects the pattern, not the surface name. The user-facing mode label stays "Books".

---

## §2 Reference surfaces

### §2.1 Stream-side blueprint (the load-bearing reference)

Per Hemanth's "Stream as blueprint, not just reference" framing (carried over from the COMICS_TANKOYOMI_STREAM_MERGER arc), every Books-mode surface forks Stream's analogous surface with book-domain substitution:

| Stream concept | Books-mode equivalent |
|---|---|
| Episode | Book (in a series) |
| Season | "Books in this series" position |
| IMDb id | ISBN / OLID (Open Library identifier) |
| `MetaItemPreview` | `BookCatalogueResult` (see §6.3) |
| `MetaAggregator` | `BookCatalogueAggregator` (see §6.3) |
| `StreamDownloadIndex` | `BooksCatalogueLibraryStore` (see §6.2) |
| Movies / Series split | Books / Series split (reversed display order — Series first) |

Key Stream files this spec ports the patterns from:

- `src/ui/pages/stream/StreamSearchWidget.h:21` + `.cpp:48-68` — search input → `MetaAggregator::queryCatalogs(...)` → result tiles → `metaActivated(MetaItemPreview)` on click. Two sections, per-section `kInitialCap = 6` initial cap + "Show N more" overflow reveal.
- `src/ui/pages/stream/StreamLibraryLayout.h:16` + `.cpp:84-110, 332-373` — TileStrip + TileCard grid + DOWNLOADED chip; sort + density slider; `showEvent` triggers `StreamDownloadIndex::validateAll`.
- `src/ui/pages/stream/StreamContinueStrip.h:16` — Continue strip pattern.
- `src/ui/pages/stream/StreamDetailView.h:34` + `.cpp:53-110, 1213-1291` — hero + season combo + 7-column episode table + `RowState` enum {Idle, Queued, Downloading, Publishing, Published, Paused, Failed} + season-header morphing button + right-click context menu + signals (`playRequested`, `bulkDownloadRequested(season)`, `singleEpisodeDownloadRequested`, etc.) + `autoAddToLibrary()` at `.h:95`.
- `src/core/stream/StreamDownloadIndex.h:21` — canonical-key-keyed thread-safe JSON index over `stream_downloads.json`, bidirectional lookup, schema version 1, three derived maps under one mutex.
- `src/core/stream/MetaAggregator` — catalog query pipeline that fans out to Stream addon registry and merges results.

Hemanth's explicit framing during the brainstorm: *"think more in terms of stream/theatre mode. if it's a series, it gets the tv show treatment like in theatre/stream mode. if it's a single book, it gets the movie treatment once again like how we do it in stream mode."*

### §2.2 Comics-merger sibling (architectural cousin, not literal port)

The COMICS_TANKOYOMI_STREAM_MERGER arc (`docs/superpowers/specs/2026-05-14-comics-tankoyomi-merger-brainstorm.md`, currently Phase 7+ shipped under Agent 1) is the closest sibling. Books-mode picks deliberately diverge in two places:

1. **No "two worlds side by side" for Books.** Comics keeps folder-imported series alive because the on-disk reality differs structurally (folder of CBZs vs catalogue download). Books has no such structural difference — an EPUB is an EPUB regardless of provenance. So we burn the ships: Books library = catalogue-sourced only, no folder-imported tier. See §3.8.

2. **No bookmark-without-download state for Books.** Comics has `[+ Add to library]` as silent bookmark + per-chapter downloads inside the detail page (chapters are large + incrementally-released; bookmark-then-download-later is a real UX). Books has `[Search for downloads]` as the ONLY entry to the library — books are small (single EPUB, few MB), source availability is the uncertain part, and the decision cost of downloading is essentially zero once a source is found. Hemanth-coined framing: empty placeholders for unfindable books would be a "UX scar." See §3.3.

### §2.3 TankoLibrary inherited surface (Agent 4B → Agent 2, 2026-05-20)

Agent 4B departed the brotherhood 2026-05-20. TankoLibrary ownership transferred to Agent 2 same day. Inherited surface (all 4B's hand, audit-driven authoring + live-network-probe discipline):

- `src/ui/pages/TankoLibraryPage.{h,cpp}` (232 + 2268 LOC) — Sources sub-app with results grid + detail page + filters popover + inline Transfers tab + Books/Audiobooks media-tab pills. Audiobooks tab stays compiled (ABB shipped 2026-04-22 via `TANKOLIBRARY_ABB_FIX_TODO M1`) but the Books-mode catalogue layer does not surface audiobooks in v1 per §3.1.
- `src/core/book/BookResult.h` (45 lines) — 4B's POD: `source`, `sourceId`, `md5` (primary cross-source dedup key), `title`, `author`, `publisher`, `year`, `description`, `language`, `format`, `isbn`, `pages`, `fileSize`, `coverUrl`, `detailUrl`, `downloadUrl`. Already catalogue-ready; no redesign needed in this arc.
- `src/core/book/BookScraper.h` (55 lines) — virtual base contract: `search(query, filters)`, `fetchDetail(md5OrId)`, `resolveDownload(md5OrId)` + signals.
- `src/core/book/AnnaArchiveScraper.{h,cpp}` (95 + N LOC) — wired but disabled at construction since `TANKOLIBRARY_FIX_TODO M2.2` captcha-block finding. Re-enable in v1 (see §3.6).
- `src/core/book/LibGenScraper.{h,cpp}` (89 + N LOC) — primary book source today. Working. JSON API at libgen.li (URL contract probed live 2026-04-22 by 4B; preserved in memory `reference_libgen_url_params.md`).
- `src/core/book/AbbScraper.{h,cpp}` — AudioBookBay scraper, out of v1 scope.
- `src/core/book/AaSlowDownloadWaitHandler.{h,cpp}` — AA stage-(b) countdown + `no_cloudflare` warning handling (4B's hostile-site research distilled into code).
- `src/core/book/BookDownloader.{h,cpp}` (123 + N LOC) — HTTP streaming with resume, HEAD probe, checksum verify, write-to-library-path. v1 extends to a magnet-source variant for Tankorent integration (see §4.1).
- `TANKOLIBRARY_FIX_TODO.md` — 4B's authoring; all milestones shipped (M1 + M2.1 + M2.2 + M2.3 + M2.4 + Track B batch 1).
- `reference_libgen_url_params.md` memory — live-probed LibGen URL contract.

### §2.4 Books-side current surface (BOOKS_STREMIO_PIVOT rewrites / repurposes)

- `src/ui/pages/BooksPage.{h,cpp}` (~115 + N LOC) — Continue Reading strip + tile grid + search bar (local-library filter today) + sort combo + grid/list toggle + density slider + `BookSeriesView` per-series folder-tree view + `BooksScanner` thread. Most of the page rewires (see §4.1).
- `src/ui/readers/BookReader.{h,cpp}` — QWebEngineView-based EPUB reader. UNTOUCHED by this pivot. Catalogue-sourced files open in the same reader unchanged.
- `src/ui/readers/BookBridge.{h,cpp}` — JS↔C++ bridge. UNTOUCHED.
- `src/scan/BooksScanner.{h,cpp}` (assumed path) — file-discovery walker. SIMPLIFIES — see §4.1.
- `src/ui/pages/BookSeriesView.{h,cpp}` — folder-tree archive view. **DELETED** by this arc. Replaced by `BooksTankoLibrarySeriesDetailView` (see §4.1).

---

## §3 Hemanth's locked picks across the brainstorm

The brainstorm was paced one-question-at-a-time conversationally per `feedback_hemanth_terms_or_skip.md` after a course-correction from Hemanth on the first batch ("you didn't use superpower brainstorm brother. please do that and brother, try to ask me the questions with simpler, descriptive and understandable logic" — 2026-05-20). Each pick below is a direct Hemanth call, sometimes overriding the agent's initial recommendation.

### §3.1 Audiobooks — fully out of scope

Audiobooks stay siloed inside TankoLibrary's existing Audiobooks media-tab. The Books-mode catalogue layer does not surface audiobooks. The standalone audiobook player remains a separate future deliverable, not coupled to this arc.

Hemanth rationale 2026-05-20: *"Audiobooks will go on the back seat for this entire stremiofication my brother. simply because i don't think we'd be able to find catalog APIs or create our scrapping algorythms for audiobook metadata. It stays out of scope for the time being. our entire focus is in matching what comic mode is doing but with book mode equiavalent solutions."*

### §3.2 Unit shape — movie OR series, catalogue decides

Two unit shapes, mirroring Stream's Movies / Series split:

- **Standalone novel = movie-shape.** One tile in the library + search results. Click → single detail page with cover + meta + [Search for downloads]. No "inside" navigation.
- **Multi-book series = TV-show-shape.** One tile (the series). Click → series detail view with the books listed in series order, each with its own download arrow and read action. Mirror of Stream's show view with episode table.

Catalogue figures out the shape per result. Open Library's series field is the primary signal; ambiguous metadata defaults to movie-shape (a wrong-singleton is a much smaller bug than a wrong-grouping).

Hemanth-coined framing: *"think more in terms of stream/theatre mode. if it's a series, it gets the tv show treatment like in theatre/stream mode. if it's a single book, it gets the movie treatment once again like how we do it in stream mode."*

### §3.3 No `[+ Add to library]` — `[Search for downloads]` is the only entry

The Add-to-library button does not exist anywhere in Books mode. The library = files actually on disk + their catalogue records. No bookmark-without-download state. No empty placeholders.

For movie-shape (standalone novel): the detail page has one big `[Search for downloads]` button. Click → picker opens → user picks a source → file downloads → catalogue record gets created → tile appears in library. The button morphs into a progress bar during download, then into a `[Read]` button when the file is on disk.

For series-shape: the series detail page has:
- A bulk `[Search for downloads — entire series]` button at the top of the action row. Fires concurrent fan-out probes for every book in the series. Shows progress strip ("3 of 5 found, 1 downloading"). Mirror of Stream's "Download season" pattern.
- Per-book `[Search for downloads ↓]` arrows on each row in the books-in-series table. Single-book picker, independent of the bulk action. Mirror of Stream's per-episode download arrow.

Hemanth rationale 2026-05-20: *"piracy for books isn't as widespread as it is for comics or tv/movies. What we need is a search for downloads option. And since epub files are always just a few mb unlike the 100s of mbs of 10s of GBs of comic and theatre mode respectively, downloading a book is a very easy decision and should be the only way to add to library. because what if there's a book that we can't download from libgen or any other sources and even from tankorrent searches? Then adding that book to library equates to having an empty placeholder."*

### §3.4 Search bar — global catalogue takeover (no local-library search in v1)

The Books-mode search bar searches the world catalogue (Open Library primary + Google Books fallback), not the local library. Type + Enter → library hides → search-takeover view fills the page → results stream in → click a result → detail page → [Search for downloads].

Local-library-search is explicitly deferred to v2 (post-pivot, when library scales to where it's actually useful).

Hemanth rationale 2026-05-20: *"yeah it's A. I don't want to a search bar for what exists in my library. That's lazy as fuck. Someday in the future if I feel like me or my brother gets into habits of adding 100s of books into the library we will add a search within library but for now it's global search only."*

### §3.5 Search results page — Stream-blueprint port, two sections

Two sections, **Series first, then Books**. Per-section `kInitialCap = 6` + "Show N more" overflow reveal. Direct fork of `StreamSearchWidget`'s Movies/Series split pattern.

- **Series section:** multi-book hits — *Stormlight Archive*, *Lord of the Rings*, *Discworld*, *The Expanse*. Clicking a tile opens the series-shape detail page.
- **Books section:** standalone-novel hits — *Project Hail Mary*, *Klara and the Sun*, *Beach Read*. Clicking a tile opens the movie-shape detail page.

If a single search returns both (e.g., a query that hits a series AND standalone books by the same author), both sections render. If only one section has hits, the empty section is hidden.

No mockup-pass needed for the search-takeover view — Stream + Comics give the literal template. Hemanth 2026-05-20: *"do we need to mock-up and stress on B because the search results on comic mode and theatre mode already have given us a perfect template."*

### §3.6 [Search for downloads] sources — LibGen + Anna's Archive + Tankorent (parallel fan-out)

Three v1 sources, all fired in parallel on every [Search for downloads] click:

- **LibGen** — primary, already shipped under 4B's authoring. EPUB/PDF/MOBI corpus, JSON API at libgen.li, direct file URL → `BookDownloader` HTTP stream → file on disk. Cheapest hit path.
- **Anna's Archive** — wired but disabled at construction since `TANKOLIBRARY_FIX_TODO M2.2` captcha-block. Re-enable in v1. Captcha-solving approach is an Agent-2 Rule-14 implementation call at writing-plans time: either Playwright MCP for the JS interstitial, or AA's token-based API access if obtainable. Either path lives in its own sub-task; AA-going-flaky is not a v1 blocker.
- **Tankorent** — Agent 4's domain post-4B-departure. Three pieces of work, all cross-coordinated with Agent 4 at writing-plans time:
  1. Book-category query filter wired into the Tankorent search wrapper (so book queries don't return video/comic torrents).
  2. Magnet-source variant on `BookDownloader` (or thin shim from `TorrentClient::addTorrent` → file extraction → move to Books library path). API surface agreed with Agent 4 first.
  3. Agent 4 sign-off on the integration before Agent 2 writes against `TorrentClient`.

Hemanth's pushback 2026-05-20 on Agent 2's initial "LibGen-only v1" recommendation: *"Tankorent search (especially piratesbay) produces all kinds of book results. Do you really want to remove our tankorrent indexers from the equation? And there are so many torrenting sites we haven't added to our indexer which could have even more focus on books. What do you say?"* Agent 2 conceded; LibGen-only was a simplified-MVP recommendation that violated the standing `feedback_quality_standard.md` rule.

### §3.7 Picker — parallel fan-out with quality signals (Hemanth call: option B)

When [Search for downloads] fires, a picker opens with **three source sections** stacked vertically (LibGen / Anna's Archive / Tankorent), each loading independently with its own spinner. Results stream in as each source returns — user does not wait for the slowest source.

Each row in the picker shows enough quality signal that the user can dodge masquerades, watermarked rips, and low-quality scans:

- **Format** (EPUB / PDF / MOBI / AZW3 / DJVU)
- **File size** (display-only, as returned by source — e.g., "4.2 MB")
- **Source name** (already implied by section, but reinforced)
- **Filename hint or release-group tag** (when available — e.g., "retail edition", uploader handle, scan quality tag)
- **Seeders + leechers** (Tankorent rows only)

Click a row → that source's result downloads → picker closes → detail page action button morphs to progress bar.

Within-source multi-format (e.g., LibGen returns 3 EPUBs + 2 PDFs of the same book) — flat-list all 5 versions under the LibGen section in source-default order. No grouping or sub-dropdown. Each row is one downloadable file. *(Agent-2 Rule-14 implementation call.)*

Hemanth rationale 2026-05-20: *"B. But we have to seperate the sources in order to reduce the time the app takes to search for the books across libgen and through indexers. The reason I pick B is, there are a lot of innacurate or misleading books masqeurading as other books or books that are still what we searched for but low quality pages and watermarks. This is where having options would beneift us."* The "separate the sources" guidance is what locks the parallel-fan-out-with-progressive-population pattern.

### §3.8 Library — catalogue-records-only, no folder-imported, burn the ships

The library is the catalogue-records store (see §6.2). A book exists in your library if and only if you went through [Search for downloads] and got a hit.

- `BookSeriesView` (folder-tree archive view) is **deleted**.
- `BooksScanner`'s file-discovery walk is **deleted**. Its new job is to validate that the files behind catalogue records still exist on disk (`validateAll()` pattern from `StreamDownloadIndex`).
- Manually-dropped files in the Books root folder that don't correspond to a catalogue record are **ignored** by the library.
- No migration logic ships with the pivot. Library starts empty for every user on first launch.
- Files on disk left untouched (we never delete user data) but ignored.

Hemanth hard call 2026-05-20: *"no more manual, they dissapear. having manual books makes the whole stremio but for books system hard ... there is this one historical anecdote about a japanese commander ordering his troops to drown all the ships to thee bottom of the ocean to drive home the point that there is no going back. I deleted them all brother."*

This is the boldest scope decision of the brainstorm. It also serves as a Hemanth commitment-to-the-shape signal — he deleted his own books mid-brainstorm to remove the retreat option. The pivot ships with the same commitment.

### §3.9 Empty library state — quiet empty

Library empty (first launch, or after burn-the-ships migration ship). Books mode renders:

- Topbar (visible, normal).
- Search bar (visible, normal).
- Empty grid area with **single line of copy:** *"Search for books to add to library"*.
- Continue Reading strip hidden (nothing to continue).
- Library grid hidden (nothing to grid).
- No discovery-board fallback (Catalogue button is v2, doesn't exist yet in v1).
- No onboarding nudge / illustrative graphic / CTA-pointing-at-affordance.

Hemanth design rule captured (cross-applicable beyond Books): *"Just 'Search for books to add to library'. I did the same with comics lmao. why do we need to over explain everything when the UI already explains it for us."* This is filed as a feedback memory at brainstorm close: short empty-state copy + no enumeration of UI affordances + prefer quiet empty states over auto-fill discovery.

### §3.10 Continue Reading strip — series tile + book subscript, click auto-resumes

The Continue strip at the top of Books mode surfaces books mid-read. After the pivot it handles both unit shapes:

- **Movie-shape book** (standalone novel, mid-read): tile shows the book cover + subscript *"Project Hail Mary · 42%"*. Click → opens the EPUB at last-read page.
- **Series-shape book** (you're mid-read on Book 3 of Stormlight): tile shows the **series** cover (book 1 as canonical default) + subscript *"Stormlight Archive · Reading Oathbringer · 62%"*. Click → opens *Oathbringer* directly at last-read page. Does **not** detour through the series detail view.

This mirrors Stream's pattern: Continue surfaces the show, click auto-resumes the episode. The series identity goes in the subscript only — the click resumes the actual reading.

Hemanth 2026-05-20: *"I lean A. reading a series is always a binge thing for me. you got it right brother."*

### §3.11 No-source-found UX — polite empty (option A)

When [Search for downloads] returns zero hits across all three sources:

- Picker shows three source sections, each spinner stopped, each ending in *"No results"*.
- Detail page stays where it is. No library record created.
- User closes picker, moves on.

No "Notify me when available" affordance. No silent background re-probe. No retry/wishlist machinery. Hemanth 2026-05-20: *"polite empty easy."*

### §3.12 Catalogue button + discovery board — v2 deferred (waits on Comics)

The Catalogue button + full-Stremio-style discovery board is **out of v1 scope** for Books mode. v1 ships without it. v2 ships after Comics's analogous catalogue page lands first (Agent 1's domain), at which point Books mirrors that pattern.

Hemanth 2026-05-20: *"catalog is v2 brother. first me and our brother agent 1 must get the comics catalogue page right, which doesn't even exist as of now. so let's think about the catalogue page after we get all the fundemantal functioning of the books for stremio concept right."*

The Catalogue button's existence + Comics-mode parallel are both confirmed in chat.md directive relay #1 (Agent 2 → Agent 1, 2026-05-20 ~4:10pm), capturing Hemanth's verbatim ask: *"we will build a full-stremio-style discovery board but it will be like the theatre mode where it has the catalogue button which presents basically the same movies and TV shows in the actual stremio's catalogue. And we need to build something like this for comics too, so just let our brother 1 know Hemanth said this."* Agent 1 will scope the Comics version when they next surface for Comics polish; Books mirrors after.

---

## §4 Architecture

Three concentric layers + the existing reader (untouched):

- **Catalogue layer (new).** Open Library primary + Google Books fallback. Provides search results, series detection, hero metadata (synopsis, genres, cover URL, ISBN, author bio for the "Other books by" scroller). Mirror of `MetaAggregator` for Stream.
- **Source layer (TankoLibrary, mostly inherited from 4B).** LibGen + AA + Tankorent. Probed in parallel on [Search for downloads]. Returns `BookResult` rows for the picker.
- **Library layer (rewritten).** Catalogue-record store (`books_catalogue_library.json`). Each record wraps one downloaded file with display metadata + read progress + (for series) series-position. `BooksScanner` simplifies to validating these records.
- **Reader (existing, untouched).** `BookReader` opens the file path from a catalogue record exactly like today.

### §4.1 Reuse-vs-fork map

Hybrid pattern (Approach C from Comics merger §4) — reuse low-level primitives + fork higher-level composers.

**REUSE DIRECTLY (no changes):**

- `TileStrip` / `TileCard` primitives — shared across all modes.
- `ToastHud` — for source-failure toast ("Anna's Archive didn't respond" — same pattern Stream + Comics use).
- `BookReader` (`src/ui/readers/BookReader.{h,cpp}`) — EPUB rendering unchanged.
- `BookBridge` (`src/ui/readers/BookBridge.{h,cpp}`) — JS↔C++ bridge unchanged.
- `BookResult` POD (`src/core/book/BookResult.h`) — 4B's design, already catalogue-ready.
- `BookDownloader` HTTP path (`src/core/book/BookDownloader.{h,cpp}`) — unchanged. Magnet-source variant added in parallel (see below).
- `BookScraper` virtual base (`src/core/book/BookScraper.h`) — unchanged.
- `LibGenScraper` / `AnnaArchiveScraper` / `AaSlowDownloadWaitHandler` — wire AA back in but no rewrite of the scrapers themselves.
- `CloudflareCookieHarvester` (4B's pattern) — AA Cloudflare stage-(a).
- `reference_libgen_url_params.md` — 4B's live-probed URL contract (do not re-probe; preserved knowledge).

**FORK FROM STREAM WITH ATTRIBUTION (header comments name the Stream parent file + line):**

- `StreamSearchWidget` → `BooksTankoLibrarySearchWidget` (working name; final name decided at writing-plans). Same two-section layout, same `kInitialCap = 6`, same "Show N more" overflow.
- `StreamDetailView` → `BooksTankoLibrarySeriesDetailView` (series-shape) **and** `BooksTankoLibraryDetailView` (movie-shape). Two forks for the two unit shapes. Movie-shape is a lighter fork (no book list table). Series-shape ports the full pattern (hero + bulk-download button + per-book table + RowState enum + context menu).
- `StreamContinueStrip` → extend BooksPage's existing `m_continueStrip` (BooksPage.h:76) with series-aware subscript rendering + auto-resume click handler. Likely no full fork — just a behavior extension on the existing widget.
- `StreamDownloadIndex` → `BooksCatalogueLibraryStore` (see §6.2). Rewrite for catalogue-records, not just download-index. Schema version 1.

**NEW (no parallel in Stream):**

- `BookCatalogueAggregator` — Open Library + Google Books query fan-out. Parallel to Stream's `MetaAggregator`.
- `BookSearchAggregator` — the picker engine, fans out parallel queries to LibGen + AA + Tankorent.
- `BookDownloader` magnet-source variant — handles Tankorent magnet URLs. Either an extension of `BookDownloader` or a thin shim from `TorrentClient::addTorrent` + extraction + move-to-library-path. API surface agreed with Agent 4.

**DELETE:**

- `BookSeriesView` (`src/ui/pages/BookSeriesView.{h,cpp}`) — folder-tree archive view, no longer needed.
- `BooksPage::m_seriesView` field + its routing in `MainWindow::openBook(...)` connect chain.
- `BooksScanner`'s file-discovery walk (the `walkBooks(...)` family). Replaced by catalogue-record path validation only.

**REWRITE:**

- `BooksScanner` (`src/scan/BooksScanner.{h,cpp}` — verify path at writing-plans) — drop file-discovery walk, replace with catalogue-record path validation on `showEvent`. Mirror of `StreamDownloadIndex::validateAll`.
- `BooksPage` (`src/ui/pages/BooksPage.{h,cpp}`) — most of the page rewires: search bar → catalogue takeover signal, Continue strip → new semantic, library grid → catalogue-record-driven, `m_seriesView` field removed.
- `MainWindow` Books routing — replace `BookSeriesView`-targeted signals with new detail-view routing (movie-shape vs series-shape, decided at click time by the catalogue record's `seriesId` field).

---

## §5 User-facing flows

### §5.1 First launch (library empty)

User opens Books mode for the first time post-pivot. Library is empty. Topbar visible, search bar visible, no Catalogue button (that's v2). Library grid empty with single line: *"Search for books to add to library"*. Continue Reading strip hidden (nothing to continue).

### §5.2 Search → result → detail (movie-shape)

User types *"project hail mary"* in the search bar, hits Enter. Library hides. Search-takeover view fills the page. Two sections labeled (Series / Books); the Series section is empty for this query and hidden; the Books section streams in results. Tiles populate: cover + title + author. User clicks the *Project Hail Mary* tile.

Movie-shape detail page opens. Hero: cover on the left + "Project Hail Mary" + "Andy Weir" + meta strip ("Ballantine · 2021 · English · 480 pages") + big purple [Search for downloads ↓] button. Below the hero: synopsis paragraph (truncated to ~5 lines with "Read more") + genre tag chips ("hard sci-fi", "first contact", "thriller", "space opera"). At the bottom: "Other books by Andy Weir" scroller (*The Martian*, *Artemis*, etc.).

User clicks [Search for downloads ↓]. Picker opens with three source sections, each spinning. LibGen returns first (~500ms): three rows appear ("EPUB · 4.2 MB", "EPUB · 5.1 MB · retail edition", "PDF · 8.1 MB"). Anna's Archive returns next (~2s): two rows ("EPUB · 4.5 MB", "PDF · 6.8 MB"). Tankorent returns last (~5s): one row ("Pirate Bay · 38-book Andy Weir pack · 1.2 GB torrent · 142 seeders").

User clicks the first LibGen EPUB row. Picker closes. Detail page's [Search for downloads] button morphs into a progress bar. `BookDownloader` HTTP-streams the file. ~3 seconds. Progress bar morphs into [Read]. File is on disk at `<Books root>/Project Hail Mary.epub`. A `CatalogueRecord` was created during the download. User clicks [Read]. `BookReader` opens at page 1.

### §5.3 Search → result → detail (series-shape)

User types *"stormlight archive"*. Library hides. Search-takeover: "Series" section has the *Stormlight Archive* tile (5 books, Brandon Sanderson). "Books" section has individual book results — Open Library indexes both the series and each book separately, so both surface. User clicks the series tile.

Series-shape detail page opens. (See mockup at `docs/superpowers/mockups/2026-05-20-books-stremio-pivot/series-detail-stormlight.html` for the approved layout.) Hero: series cover (book 1 by default) + "Stormlight Archive" + "Brandon Sanderson" + meta strip ("5 books · sci-fi/fantasy · ongoing · English") + big purple [Search for downloads — entire series ↓] button. Below the hero: series synopsis + genre tag chips. Books-in-this-series table: 5 rows initially all in "Available not-started" state, each with an outlined [Search for downloads ↓] action.

User clicks [Search for downloads — entire series ↓]. Bulk probe fires: 5 concurrent fan-outs across LibGen + AA + Tankorent, one per book. Progress strip under the bulk button: *"1/5 found · 2/5 downloading · 3/5 not yet started · ..."*. Some books complete fast (LibGen has them), some take longer (fall through to Tankorent), one misses (no source found anywhere). Final state: 4/5 found and downloaded; *Wind and Truth* row shows italic *"no source yet"*.

Or: user clicks a per-book [Search for downloads ↓] arrow on just Book 3 *Oathbringer*. Single-book picker opens (same picker as the movie-shape flow). User picks a source, downloads, returns to the series detail page. Book 3's row morphs to Read action.

Right-click on the series header → context menu: *Cancel all downloads · Remove series from library · Show series on Open Library*.

### §5.4 Continue Reading mid-read (series)

User has read 62% of *Oathbringer* (Book 3 of Stormlight). Closes the reader. Comes back to Books mode the next day. The Continue Reading strip at the top of Books mode shows a tile with the series cover (book 1) + subscript *"Stormlight Archive · Reading Oathbringer · 62%"*. User clicks the tile → `BookReader` opens *Oathbringer* directly at 62%, no series detail page detour.

### §5.5 No source found

User searches for an obscure self-published work or an out-of-print academic title. Catalogue finds it via Open Library or Google Books. Detail page opens normally (cover, synopsis, ISBN all rendered). User clicks [Search for downloads]. Picker opens. LibGen spinner stops → *"No results"*. AA spinner stops → *"No results"*. Tankorent spinner stops → *"No results"*. User closes picker. Detail page stays. No library record created. User backs out via the back arrow or searches for something else.

---

## §6 Data model

### §6.1 CatalogueRecord (new — the v1 library entity)

```cpp
struct CatalogueRecord {
    // Identity
    QString catalogueId;        // e.g. "openlib:OL27448W" or "googlebooks:abc123"
    QString isbn;               // when known (multi-ISBN joined with ',')
    QString md5;                // BookResult.md5 for the downloaded file

    // Display metadata (from catalogue layer)
    QString title;
    QString author;
    QString publisher;
    QString year;
    QString language;
    QString description;        // synopsis
    QStringList genres;         // Open Library subjects
    QString coverUrl;           // remote (lazy-fetched)
    QString cachedCoverPath;    // local cached cover for offline render

    // Series (nullable — set only if catalogue identified series-shape)
    QString seriesId;           // shared across CatalogueRecords in same series
    QString seriesName;
    int     seriesPosition;     // 1-indexed; 0 if unknown
    int     seriesTotal;        // when known; 0 if unknown

    // File (from source layer + downloader)
    QString filePath;           // canonical relative path under Books root
    QString format;             // "epub" | "pdf" | "mobi" | ...
    QString fileSize;           // human-readable display

    // State (from app)
    qint64  addedAt;            // epoch seconds
    double  readProgress;       // 0.0..1.0
    qint64  lastReadAt;         // epoch seconds
    QString lastReadCfi;        // EPUB CFI position for resume
};
Q_DECLARE_METATYPE(CatalogueRecord)
```

A series of N books = N CatalogueRecords sharing the same `seriesId`. The series tile in the library is rendered from the most-recent / most-progressed record in the series group (lookup via the `seriesId → list<catalogueId>` derived map).

### §6.2 BooksCatalogueLibraryStore (rewrite of folder-scan-based library)

A new JSON-backed thread-safe store, mirroring `StreamDownloadIndex`'s shape:

- File path: `<userdata>/books_catalogue_library.json` (path resolved via `CoreBridge::dataDir` or equivalent).
- `catalogueId → CatalogueRecord` map (primary).
- `seriesId → list<catalogueId>` derived map (for series-tile aggregation in the library grid).
- `filePath → catalogueId` reverse-lookup (for on-disk validation).
- Schema version 1. Three derived maps under one mutex.
- `validateAll()` on `BooksPage::showEvent` walks the records, checks each file path exists on disk, marks orphan records (file deleted out-of-band) for cleanup.
- Save-on-mutation pattern (debounced ~500ms to coalesce burst writes during bulk downloads).

### §6.3 BookCatalogueAggregator (catalogue-side query pipeline)

Mirrors `MetaAggregator` for Stream. Fans out queries to:

- **Open Library** — `https://openlibrary.org/search.json?q=<query>&fields=key,title,author_name,first_publish_year,isbn,subject,cover_i` (primary).
- **Google Books** — `https://www.googleapis.com/books/v1/volumes?q=<query>&key=<KEY>` (fallback, requires API key from Hemanth at writing-plans time).

Merges results, dedupes by ISBN + (title + author) fuzzy match. Returns ordered list of `BookCatalogueResult`s for the search-takeover view. Per-source failure surfaces a toast ("Google Books didn't respond") via `ToastHud`; other source's results still render. Open Library remains primary because (a) no key required, (b) richer series-field metadata, (c) author-page endpoint powers the "Other books by author" scroller.

Series detection: Open Library Edition records carry a `series` field, but it's patchily populated. Fallback heuristic: author + title-suffix pattern matching ("Book 1", "#1", Roman numerals) + Author/works-list cross-reference. Ambiguous → movie-shape (a wrong-singleton is recoverable; a wrong-grouping is jarring). *(Agent-2 Rule-14 implementation call at writing-plans.)*

### §6.4 BookSearchAggregator (source-side picker pipeline)

The picker engine. Given a `BookCatalogueResult` (ISBN preferred for source-side query, fallback title + author), fans out parallel queries to:

- LibGen (working, primary source today).
- Anna's Archive (re-enabled in v1; captcha-solving sub-task).
- Tankorent (book-category query filter, magnet-source variant of `BookDownloader`).

Each source's results stream into the picker independently (per-section spinner stops + rows populate per source as that source's query returns). Cross-source md5 dedup where md5 is available (md5 is a strong dedup key — same file across sources merges into one row tagged with multiple source badges).

---

## §7 Out-of-scope confirmations

- **Audiobooks** — entirely out of this arc. TankoLibrary's Audiobooks media-tab stays compiled but the Books-mode catalogue never surfaces audiobooks.
- **BookReader** — untouched. EPUB rendering, page turn, dictionary lookup, font settings — all stay as today.
- **Theme system** (Agent 5's lane) — fully out. The new detail view + new picker use existing theme tokens only; no new tokens added.
- **Other modes** (Stream / Comics / Videos / Tankoyomi / Tankorent UI) — untouched, except:
  - Tankorent gets a book-category query filter wired in (Agent 4 owns; tiny additive change).
  - Comics will get its analogous Catalogue button v2 (Agent 1 owns; Books mirrors after).
- **Standalone audiobook player** — separate future deliverable, untouched here.
- **AA captcha-solving infrastructure choice** — Agent-2 Rule-14 implementation call at writing-plans (Playwright MCP vs token-based API).
- **Catalogue → TankoLibrary source-search reconciliation** — Agent-2 Rule-14 implementation call (probe by ISBN when available, fall back to title + author).

---

## §8 Deferred follow-ups (v2 / later)

| Feature | Reason for defer | Trigger to ship |
|---|---|---|
| **Catalogue button + Stremio-style discovery board** | Comics catalogue page (Agent 1) needs to ship first; Books mirrors after | After Comics catalogue page lands. Codified in chat.md directive relay #1 to Agent 1. |
| **"Notify me when available" on no-source-found** | New background-probe infrastructure + notification surface | If miss-rate is high enough in v1 production usage to warrant the build |
| **Search within library** | Library hasn't scaled to where it's useful yet (2-user app, libraries start empty per §3.8) | When library reaches ~100+ books per user OR when Hemanth/his brother explicitly asks |
| **Standalone audiobook player** | Separate arc, not Books-mode-related | Hemanth wake to scope |
| **Book-focused indexer expansion to Tankorent** | v1 ships with Tankorent's existing public indexer roster (TPB primary for books); RuTracker / MyAnonamouse-class / smaller scene trackers waiting | Agent 4 + Agent 2 joint commission when prioritized. Hemanth-flagged 2026-05-20 as a real expansion direction. |
| **Series-aware sub-arc grouping** (Wheel of Time trilogies, Discworld subseries) | Open Library data is patchy for sub-arcs | If Hemanth/his brother reads a series with notable sub-arc structure and notices the gap |
| **Folder-imported books retroactively pulled in** | Burn-the-ships call rules it out for v1; would re-introduce dual-world complexity | Not currently planned |
| **Source-failure toast variants** (specific error categorization) | v1 ships generic "No results" + ToastHud-based "X didn't respond" | If failure-mode diagnostics become a debugging need |

---

## §9 Cross-agent coordination

- **Agent 4 (Stream + Tankorent)** — Tankorent owner post-4B-departure. Coordination required at writing-plans time:
  - Tankorent book-category query filter wiring.
  - `BookDownloader` magnet-source variant API surface agreement (or thin shim from `TorrentClient::addTorrent` → file extraction → move to Books library path).
  - Cross-arc smoke test once both ends are wired (Agent 4 confirms Tankorent returns book results; Agent 2 confirms the picker renders them and downloads work end-to-end).
- **Agent 1 (Comics + Tankoyomi)** — already directive-relayed in chat.md 2026-05-20 ~7:05pm (relay #2). Catalogue button v2 alignment is Agent 1's call to scope after their current Phase 7+ Fandom catalog redesign work stabilizes; Books mirrors. Mockup at `docs/superpowers/mockups/2026-05-20-books-stremio-pivot/series-detail-stormlight.html` is also relayed for Agent 1's Comics series view overhaul direction (Hemanth specifically asked Agent 2 to share — 2026-05-20 ~6:30pm).
- **Agent 0 (Coordinator)** — phase-boundary commits + CLAUDE.md dashboard row updates + chat.md sweeps. Standard governance.
- **Agent 3 (Video Player) / Agent 5 (Library UX)** — untouched by this arc. No coordination needed.

---

## §10 UI Mockups

- **Series-shape detail page** (Stormlight Archive example, mixed row states): [`docs/superpowers/mockups/2026-05-20-books-stremio-pivot/series-detail-stormlight.html`](../mockups/2026-05-20-books-stremio-pivot/series-detail-stormlight.html). Standalone self-contained HTML, opens in any browser via `file://`. Hemanth-approved 2026-05-20 ~6:30pm. Five row states visible at once (Read 100% / Read mid-progress / Downloading-with-spinner / Available not-started / No-source-found italic). Bottom of the file contains the Comics-side substitution map relayed to Agent 1.

Other mockups deferred — movie-shape detail page + search-takeover layout + library landing all follow Stream-blueprint port exactly; no mockup-pass needed.

---

## §11 Process

This brainstorm followed `/superpowers:brainstorming`'s actual one-question-at-a-time cadence after a course-correction from Hemanth on the first batch (*"you didn't use superpower brainstorm brother. please do that and brother, try to ask me the questions with simpler, descriptive and understandable logic"* — 2026-05-20). Questions framed in Hemanth-terms per `feedback_hemanth_terms_or_skip.md`. Pacing was one-at-a-time conversational, NOT batches-of-4 — that pattern is Agent 1's per `feedback_brainstorm_batches_of_four.md`, not universal.

Visual companion engaged for the series-shape detail page mockup per `superpowers:brainstorming/visual-companion.md`; the standalone HTML mockup saved to a non-gitignored stable path (`docs/superpowers/mockups/2026-05-20-books-stremio-pivot/`) for cross-agent sharing.

### Next gates

1. **This doc → Codex Trigger C review-and-EXPAND-in-place** (gov-v4 Rule 20, revised 2026-05-14 at `agents/chat.md:3640+`). Codex adds inline HTML-comment attribution markers per added/rewritten section. One-shot pass; no separate audit file.
2. **Hemanth fires `/superpowers:writing-plans`** → produces `docs/superpowers/plans/2026-05-20-books-stremio-pivot.md`.
3. **Agent 2 authors `BOOKS_STREMIO_PIVOT_FIX_TODO.md`** (14-section template per `feedback_fix_todo_authoring_shape.md`) at writing-plans completion, for phase-cursor tracking + CLAUDE.md dashboard row registration.
4. **Hemanth fires `/superpowers:executing-plans`** → multi-summon arc per `feedback_no_pause_between_subagent_tasks.md` (rip through tasks back-to-back, no per-task pause).
5. **Cross-agent coordination** with Agent 4 happens during writing-plans (Tankorent integration design) and during execution (smoke tests).
