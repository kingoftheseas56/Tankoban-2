# Books mode → FictionDB catalogue (local series index + two-track search) — Design spec

- **Date:** 2026-05-28
- **Author:** Agent 2 (Book Reader + TankoLibrary)
- **Arc tag:** `BOOKS_FICTIONDB_CATALOGUE`
- **Status:** SHIPPED with a mid-build architecture pivot. ⚠️ **D7–D9 (local series index, two-track-with-index, instant-from-index) are SUPERSEDED.** The A–Z series directory the index is built from proved to be FictionDB's *indie long tail* (7,248 series, Dune/Stormlight absent), so the index can't find the series users actually search. As-built: the series track is **Top-N resolution** (free-text search → peek top ~8 book pages → group by self-declared series link). D1–D6 + D10 (no catalog board) stand. The `BookSeriesIndex`/builder are dormant. Series-detail metadata (cover/synopsis/year) is fetched eagerly per-book on open + cached (`BookSeriesDetailView`, #2 2026-05-28).
- **Supersedes:** `docs/superpowers/specs/2026-05-27-books-fictiondb-catalogue-design.md`. That spec assumed FictionDB's free-text search returns a clean series/standalone split (its §4.1/§4.2). Phase-1 build + 2026-05-28 live probing **disproved** that: FictionDB free-text search returns a *flat book list with no series records*. This spec replaces the search mechanism with a **local series index** built from FictionDB's A–Z series directory + a **two-track** search. Decisions D1–D6 carry forward unchanged; D7–D10 are new.
- **Predecessor arc:** `BOOKS_STREMIO_PIVOT` (vision locked 2026-05-20; §3.8 burn-the-ships + §5.2 per-book download flow shipped 2026-05-27).
- **Phase-1 already shipped (last wake):** `FictionDbClient` book/series/search parsers + network methods + 4 GoogleTests green (commits pending Agent 0 sweep).
- **Skills invoked:** `/superpowers:brainstorming`, `/hemanth-language`, live `curl` probing.

---

## §1 Goal — why this arc exists

BOOKS_STREMIO_PIVOT used **Open Library** (+ Google Books fallback) as the catalogue source. Open Library is community-edited: a search for "Dune" returns ~50 distinct results (editions, translations, sequels, prequels, companion material) with no enforced "one logical book = one record" rule and no reliable series grouping.

Hemanth's framing (2026-05-27): *"we created a cinemta for manga by scrapping mangafire … just any website on the internet that maintains a clean database of books will do the job for us."* Two AI research passes + live `curl` settled the source on **FictionDB** (LibraryThing = Cloudflare wall; StoryGraph = JS shell; FictionDB = server-rendered HTML, `og:` tags, no challenge at burst). Fiction-only; Hemanth accepted dropping OL+GB entirely.

**The Phase-1 discovery that reshaped this spec (2026-05-28):** FictionDB's *free-text search* (`/search/searchresults.htm?srchtxt=…&styp=5`) returns a **flat list of individual books — no series records**. Every `styp` variant was probed: `styp=1`=authors, `styp=3/7`=near-empty, `styp=5`=flat books. So a typed query alone cannot produce the clean "one series tile" result.

**The unlock (2026-05-28 live-verified):** FictionDB has a complete, server-rendered, **A–Z series directory** — `/series/series-lists.htm` → 26 alphabetical pages `/series/author-series~<a..z>.htm` (paginated, e.g. letter "A" runs 12 pages, 339 series), each row a clean `{series name, author, /series/<slug>~<id>.htm}`. FictionDB claims ~80K series total. This is a scrapable, comprehensive series catalogue. So: **build a local series index from this directory; run series search against it (instant, comprehensive, series-first); run standalone-book search live against FictionDB's flat search.**

---

## §2 Locked decisions

Carried from the 2026-05-27 brainstorm (unchanged):

| # | Decision | Pick |
|---|----------|------|
| D1 | Open Library + Google Books | **Dropped from the pipeline.** FictionDB sole source; Books mode is fiction-only. |
| D2 | Series treatment | **Full** — series render as one tile → series-shape detail view with books in order. Standalones keep movie-shape. |
| D3 | Series detail availability | **Lazy per-book** — each book row searches its own sources only when its button is clicked. No auto-search-all on series open. |
| D4 | Bulk "download entire series" | **Per-book only for v1.** Bulk deferred to v1.x. |
| D5 | Cover source | **FictionDB `og:image`** (`/covers/<isbn>.jpg`), reusing the existing cover-cache. |
| D6 | Architecture approach | **Hybrid (Approach 3)** — reuse data model + storefront + §5.2 download flow; new client + new series-detail view; retire OL/GB + SeriesDetector heuristics. |

New this wake (2026-05-28 brainstorm):

| # | Decision | Pick |
|---|----------|------|
| D7 | How does series search work? | **Local series index** (brainstorm "Option 2"), built from FictionDB's A–Z series directory. Search queries the local index — instant, comprehensive, series-first. *Not* live free-text search (which is flat/noisy). |
| D8 | Standalone-book search | **Two-track** (brainstorm "Option A", mirrors Theatre's Movies/TV split): a **Series** track (instant, from the index) + a **Books** track (live FictionDB flat search) → the storefront's existing two-section Series/Books layout. |
| D9 | Result quality on the Books track | **No Open-Library-style duplicates** (live-verified: FictionDB is one-record-per-book — Godfather = 59 results / 59 unique slugs; Brothers Karamazov = 2 entries, not 50 translations). The real issue is **ranking** — FictionDB's flat search is not relevance-sorted (Puzo's *The Godfather* ranks #13). So the Books track is **re-ranked locally**: exact/prefix title matches + author matches float to the top; capped at 6 + "Show more". |
| D10 | Catalog / browse board (genre rows) | **NOT in this arc.** Search is the v1 discovery surface. The genre-row catalog board is deferred to a later joint **Books + Comics** catalog arc (Theatre is exempt — it rides Cinemeta's ready-made catalog, not a scraped one). Library grid + Continue Reading strip stay exactly as they are (mirroring Comics/Theatre). |

---

## §3 Reference surfaces

### §3.1 Live-verified FictionDB facts (2026-05-27 + 2026-05-28 curl probes)

- **Book page** `/title/<slug>~<author>~<id>.htm` → HTTP 200, server-rendered. `og:title="Dune by Frank Herbert"`, `og:isbn`, `og:image` (cover), `og:description`; `datePublished` (year); a positioned series link ("Dune Chronicles - 1") that self-declares series membership + position.
- **Series page** `/series/<slug>~<id>.htm` → HTTP 200, books in reading order (document order = position).
- **A–Z series directory** → `/series/series-lists.htm` links to `/series/author-series~<a..z>.htm`; each is paginated (`author-series~<letter>~<page>.htm`), each row a clean `{series name, author-in-slug, /series/<slug>~<id> link}` (+ genre / age-level per row, per research). ~80K series total.
- **Free-text search** `/search/searchresults.htm?srchtxt=<q>&styp=5` → HTTP 200, flat `<table>` of distinct books (`itemprop` url/name/author), **no series records, not relevance-ranked**.
- **No Cloudflare challenge** at burst (passive beacon only). Chrome-UA `QNetworkRequest` passes.
- **No edition-spam** — every search result is a distinct book (unique slug). Translations/editions are NOT multiplied (Karamazov = 2 entries).

### §3.2 Existing Tankoban surfaces

- `src/core/book/BookCatalogueResult.h` — catalogue-hit POD. **Reused** (carries title/author/isbn/coverUrl/seriesId/seriesName/seriesPosition/seriesTotal).
- `src/core/book/FictionDbClient.{h,cpp}` — **built last wake (Phase 1).** Has `search`/`fetchBook`/`fetchSeries` + `parseSearchPage`/`parseBookPage`/`parseSeriesPage` + `slugFromHref`. **Extended this arc** with series-index-page fetch + parse (§4.1).
- `src/core/book/BooksCatalogueLibraryStore.{h,cpp}` + `CatalogueRecord.{h,cpp}` — library store/entity. **Reused unchanged.**
- `src/core/book/BookCatalogueAggregator.{h,cpp}` — **reworked** to the two-track model (§4.3).
- `src/core/book/OpenLibraryClient`, `GoogleBooksClient`, `SeriesDetector`, `CatalogueDeduper` — **retired from the pipeline** (§4.6). The `SeriesDetector::SeriesGroup` *data struct* is preserved as the storefront wire-type (relocated to a neutral header if SeriesDetector is deleted — decided at plan time).
- `src/ui/pages/books/BookCatalogueSearchWidget.{h,cpp}` — two-section Series/Books storefront. **Reused + extended** (wire two-track, add 6-tile cap, route series clicks).
- `src/ui/pages/books/BookCatalogueDetailView.{h,cpp}` — movie-shape detail (§5.2 download + §3.3 backout). **Reused unchanged** for standalones.
- `BookSearchAggregator` + `LibGenScraper` + `TankorentBookScraper` + `BookDownloader` — source/download layer. **Reused unchanged** (§5.2 chain).
- `src/ui/pages/BooksPage.{cpp,h}` — **reworked** (construct index + series view; routing).
- `src/ui/pages/stream/StreamDetailView.h` + the approved 2026-05-20 Stormlight mockup — **blueprint** for the new series-detail view.

---

## §4 Architecture

Three layers, unchanged in shape; this arc rebuilds the catalogue layer's *discovery mechanism* (local index + two-track) and adds the series-detail surface:

```
   CATALOGUE LAYER                       SOURCE LAYER            READER
   ─────────────────                     ──────────────          ──────
   FictionDB series index (local)  ┐
   + FictionDB live book search    ┘ →   TankoLibrary (LibGen)   BookReader
                                         + Tankorent
```

### §4.1 Extended — `FictionDbClient` (series-index reading)

Add to the Phase-1 client:

- `static QList<SeriesIndexEntry> parseSeriesIndexPage(const QString& html, bool* hasNextPage)` — parse an `author-series~<letter>[~<page>].htm` page into `SeriesIndexEntry{ QString seriesId /*slug*/, QString seriesName, QString author, QString genre /*optional*/ }`; set `hasNextPage` from the "»"/next-page link. Pure, unit-tested against a frozen fixture.
- `void fetchSeriesIndexPage(const QString& letter, int page)` → GET the page → emit `seriesIndexPageReady(QString letter, int page, QList<SeriesIndexEntry> entries, bool hasNextPage)` / `seriesIndexPageFailed(letter, page, error)`. Chrome-UA helper (already in the client).

### §4.2 New — `BookSeriesIndex` (`src/core/book/BookSeriesIndex.{h,cpp}`)

The local series catalogue — the heart of instant series search.

- **Storage:** JSON (matches `BooksCatalogueLibraryStore`). Each entry `{ seriesId, seriesName, author, genre }`. ~80K entries ≈ a few MB; loaded into memory on startup.
- **Two-tier source, no first-launch wait:** a **pre-built index ships bundled** as an app resource (generated once during development via the build path below). On launch, the index loads instantly from the bundle (or from a newer refreshed copy in the data dir if present). The user never waits on first run.
- **Build / refresh path:** walk `author-series~<a..z>.htm` + pagination via `FictionDbClient::fetchSeriesIndexPage`, accumulate entries, write JSON. ~300 polite (~1/sec) page fetches, run **in the background** (a) once during dev to produce the bundled resource, and (b) periodically + on a manual "refresh catalogue" action to keep the data-dir copy current. Per-page failures are non-fatal (skip + continue → partial index still usable).
- **Query:** `QList<SeriesIndexEntry> query(const QString& text, int limit) const` — case-insensitive substring match on `seriesName` (and `author`), ranked exact → prefix → contains. Synchronous, in-memory, instant.
- **Freshness:** index carries a `builtAt` timestamp + `schemaVersion`; a background refresh fires if older than a threshold (e.g. 30 days). Manual refresh available. (Cadence is an implementation detail, decided at plan time.)

### §4.3 Reworked — `BookCatalogueAggregator` (two-track)

Drops OL+GB+`SeriesDetector`+`CatalogueDeduper` from the pipeline. Holds a `FictionDbClient*` + a `BookSeriesIndex*`. `query(q)`:

1. **Series track (instant):** `m_index->query(q)` → series matches → convert each to the storefront wire-type (`SeriesGroup{ seriesName, author, books=[1 stub carrying catalogueId=`fictiondb:<series-slug>` + seriesId + empty coverUrl] }`). Emit `aggregateReady(q, seriesGroups, /*standalones*/ {})` **immediately** so the Series section paints with zero wait.
2. **Books track (live):** `m_fictiondb->search(q)` → on `searchResults(q, books)`: **re-rank** locally (score by exact-title > prefix-title > contains, + author-match bonus; stable sort) and keep all distinct results — no hard drop, since the 6-tile cap (§4.4) hides the long tail behind "Show more". Emit `aggregateReady(q, seriesGroups, standalones)` **again** with the now-populated Books section. The storefront re-renders both sections; Series stays, Books fills in.
3. **Generation guard:** a `query()` bumps a generation counter; stale `searchResults` from a superseded query are dropped (preserves the existing race-guard intent).

**Series-member folding** (a Dune-book in the Books track collapsing into the Dune Chronicles tile) requires per-book series resolution FictionDB's flat search doesn't provide cheaply — **deferred to v1.x** (§6). v1 shows the clean Series tile (from the index) prominently + the re-ranked Books list; mild redundancy (a series member may also appear as a book) is accepted, never a wrong-grouping.

Series-tile **covers** load lazily: when a series tile first renders, fetch its series page → first book `og:image` → cache (the storefront's existing `m_catalogueCoverDir` cover-cache). The A–Z index rows carry no cover.

### §4.4 Reused + extended — storefront (`BookCatalogueSearchWidget`)

Two sections (Series first, then Books) already exist. Wire to the reworked `aggregateReady`. Add `kInitialCap = 6` per section + "Show N more". Series-tile click → `seriesPicked` carrying the FictionDB series slug (read from the stub's `seriesId`); standalone-tile click → existing `bookPicked` → movie detail view.

### §4.5 New — `BookSeriesDetailView` (`src/ui/pages/books/BookSeriesDetailView.{h,cpp}`)

Series-shape detail, forked from `StreamDetailView` + the Stormlight mockup:

- Hero: series cover (book-1 default) + series name + author + meta strip ("6 books · science fiction").
- Books-in-series table: one row per book in reading order (from `FictionDbClient::fetchSeries`). Each row: cover thumb + title + position + a per-book action button.
- **Per-book button states (lazy, D3):** `[Search for downloads]` (default; click → that book's source search → §5.2 picker/download) → `Downloading XX%` → `[Read]` (when the book's `CatalogueRecord` exists / file on disk). State derives per-row from `BooksCatalogueLibraryStore::hasRecord(catalogueId)`, refreshed on `recordsChanged`.
- No bulk button (D4). `backRequested()` returns to storefront/library. Each per-book download reuses the §5.2 chain verbatim.

### §4.6 Routing (`BooksPage`) + retirement

- Construct `BookSeriesIndex` + `BookSeriesDetailView`; `setCatalogueStore` on the series view; add it to the page stack.
- Storefront `seriesPicked` → `m_fictiondb->fetchSeries(slug)` → on `seriesReady` → `m_seriesDetailView->showSeries(...)` + show it. Standalone `bookPicked` → existing movie detail path (unchanged).
- Series-view `bookDownloadRequested` / `bookReadRequested` → the existing §5.2 `BooksPage` slots (record-creation + reader-open shared by both detail views).
- Remove OL+GB+SeriesDetector+CatalogueDeduper from the aggregator path. **Disposition of the now-unused files:** unwire now; full deletion is a clean-up the plan may include (lean delete, since fiction-only is locked and there's no nonfiction-fallback plan — but the `SeriesGroup` struct must find a neutral home first).

---

## §5 User-facing flows

### §5.1 Search → series → per-book download
Type "stormlight" → **Series** section paints instantly: one *Stormlight Archive* tile (from the local index). **Books** section fills a beat later (live, re-ranked). Click the series tile → series detail: hero + 5 books in order, each `[Search for downloads]`. Click Book 1 → §5.2 (resolve → mirror-failover → download → record → reader); its row → `[Read]`, file opens, library now has it. Books already on disk show `[Read]` immediately.

### §5.2 Search → standalone → download
Type "the godfather" → **Books** section, re-ranked so Mario Puzo's novel is on top (not buried at FictionDB's native rank #13). Click → movie-shape detail (tested 2026-05-27) → `[Search for downloads]` → §5.2.

### §5.3 Library + Continue Reading
Unchanged. Library grid shows catalogue records (mirrors Comics/Theatre). Continue strip walks records with `readProgress` in (0,1). FictionDB-sourced records flow identically.

---

## §6 Out of scope / deferred

- **Catalog / browse board (genre rows)** — D10; later joint Books+Comics arc.
- **Series-member folding** (Books-track members collapsing into their series tile) — needs per-book series resolution; v1.x.
- **Nonfiction discovery** — dropped with OL+GB (D1).
- **Bulk "download entire series"** (D4) and **auto-search-all on series open** (D3) — v1.x.
- **"Other books by author" scroller** — v1.x.
- **Cover multi-resolution** — FictionDB serves one size; cache + scale client-side as today.
- **AnnaArchiveScraper re-enable** — dormant per Path C (2026-05-21).

---

## §7 Cross-agent coordination

- **Agent 4 (Tankorent)** — no new coordination; §5.2 catalogue→Tankorent bridge already exists. NB: Agent 4's TANKORENT_QUALITY_AND_QUEUE touches `MainWindow`/`TorrentClient`; my §5.2 used `MainWindow::torrentClient()` (already committed). Re-check before editing shared files.
- **Agent 1 (Comics)** — MangaFire scraping is the architectural cousin; the deferred catalog board (D10) is a shared future arc. No current shared surface.
- **Agent 0** — spec commit + plan-phase tracking + sweep.
- **Agents 3, 5** — untouched.

---

## §8 Process & next gates

Brainstorm ran `/superpowers:brainstorming` one-question-at-a-time (Agent 2 pacing). Visual companion offered + declined (library layout is locked, mirrors Comics/Theatre). Forks locked: D7 (local index), D8 (two-track), D9 (no-dupes + local re-rank), D10 (no catalog board this arc). D1–D6 carried from 2026-05-27.

**Next gates:**
1. Hemanth reviews this spec → approve / request changes.
2. `/superpowers:writing-plans` → `docs/superpowers/plans/2026-05-28-books-fictiondb-catalogue.md` (supersedes the 2026-05-27 plan; Phase 1 is already done).
3. Execution (inline or subagent-driven). Phase order: **(1)** `FictionDbClient` series-index parse/fetch + `BookSeriesIndex` build/query/store + bundled-index generation → **(2)** aggregator two-track rework → **(3)** storefront wiring + 6-tile cap → **(4)** `BookSeriesDetailView` → **(5)** `BooksPage` routing + OL/GB retirement → **(6)** build + live smoke + RTC.
4. Live smoke: search "stormlight"/"dune" → instant series tile → series detail → per-book download → reader, via tankoctl + Hemanth visual confirm.

**Open implementation calls (Agent-2 Rule-14, decided at plan time):**
- Series-index JSON schema + on-disk location (bundled resource path + data-dir refresh copy) + refresh cadence threshold.
- Index query ranking + Books-track re-rank scoring specifics.
- `SeriesGroup` struct's neutral home if `SeriesDetector` is deleted.
- Twice-emit vs new-signal for instant-series-then-books in the aggregator (default: re-emit `aggregateReady`).
- Full-delete vs keep-dormant for OpenLibraryClient + GoogleBooksClient (lean delete).
