# Books mode → FictionDB catalogue swap — Design spec

- **Date:** 2026-05-27
- **Author:** Agent 2 (Book Reader + TankoLibrary)
- **Arc tag:** `BOOKS_FICTIONDB_CATALOGUE`
- **Status:** Phase 1 brainstorm complete — design approved by Hemanth 2026-05-27. Pending spec review → `/superpowers:writing-plans`.
- **Predecessor arc:** `BOOKS_STREMIO_PIVOT` (vision locked 2026-05-20; §3.8 burn-the-ships + §5.2 download flow shipped 2026-05-27). This arc swaps the catalogue *source* underneath that pivot.
- **Skills invoked (Phase 1):** `/superpowers:brainstorming`, `/hemanth-language`.

---

## §1 Goal — why this arc exists

The BOOKS_STREMIO_PIVOT used **Open Library** (+ Google Books fallback) as the catalogue source. Open Library is community-edited like Wikipedia: every contributor can create separate "Work" records for what is logically the same book. A search for "Dune" returns ~50 distinct results — the 1965 novel, the 50th Anniversary Edition, the Spanish translation, the Audible edition, all 5 sequels, ~11 prequels, companion material. There is no enforced "one logical book = one record" rule and no reliable series grouping.

Hemanth's framing (2026-05-27): *"we created a cinemta for manga by scrapping mangafire. it proved to me that we don't need an official indexer API, just any website on the internet that maintains a clean database of books will do the job for us. we just need to find it."*

Two AI research passes (ChatGPT + Gemini) plus live `curl` verification settled the source:

- **LibraryThing** (Gemini's #1) — Cloudflare JS-challenge wall ("Just a moment…"). Blocked, same as Anna's Archive. **Rejected.**
- **StoryGraph** (ChatGPT's #1) — HTTP 200 but a JavaScript shell; zero book metadata in the served HTML. **Rejected.**
- **FictionDB** — HTTP 200 with full server-rendered content + structured `og:` meta tags + direct ISBN-derived cover URLs + clean series pages + no Cloudflare challenge even at burst. **Selected.**

FictionDB is the MangaFire-equivalent for books — arguably cleaner, because its `og:` meta tags give structured metadata instead of fragile CSS-selector parsing.

**The tradeoff Hemanth accepted:** FictionDB is fiction-only (no *Sapiens*, no technical books, no general nonfiction). Hemanth's call (2026-05-27): **drop Open Library + Google Books entirely; Books mode becomes a fiction reader.** Burn-the-ships, consistent with the §3.8 decision.

---

## §2 Locked decisions (Hemanth brainstorm 2026-05-27)

| # | Decision | Hemanth's pick |
|---|----------|----------------|
| D1 | What happens to Open Library + Google Books? | **Drop from the catalogue pipeline.** FictionDB is the sole catalogue; Books mode is fiction-first and proud of it; no nonfiction discovery via catalogue in this arc. Default disposition of the now-unused client files: keep-compiled-dormant (re-enableable), per §3.3 — Hemanth may elect full deletion at plan time. |
| D2 | Arc scope — how much of FictionDB's series data do we build? | **Full series treatment.** Series render as one tile → series-shape detail view with books in order. Standalone novels keep movie-shape. |
| D3 | Series detail page availability behaviour | **Lazy per-book.** Each book row has its own `[Search for downloads]`; clicking searches that book's sources. No auto-search-all on series open (would fire 12 concurrent searches per 6-book series). |
| D4 | Bulk "download entire series" button | **Per-book only for v1.** No bulk button this arc. Deferred to v1.x (the downloader already queues, so it's a small later add). |
| D5 | Cover source | **FictionDB ISBN-derived covers** (`fictiondb.com/covers/<isbn>.jpg`). Implementation detail — mirrors existing cover-cache pattern. |
| D6 | Architecture approach | **Hybrid (Approach 3).** Reuse the source-agnostic data model + storefront + §5.2 download flow; swap the client; retire SeriesDetector's heuristics in favour of FictionDB's explicit series data; build the series-shape detail view fresh. |

---

## §3 Reference surfaces

### §3.1 The MangaFire blueprint (architectural cousin)

This arc mirrors how Comics mode scrapes MangaFire for its catalogue. FictionDB plays the MangaFire role for books:

- One canonical page per logical book at a stable URL (`/title/<slug>~<author>~<id>.htm`)
- Series pages (`/series/<slug>~<author>~<id>.htm`) with member books in reading order
- Author pages (`/author/<slug>~<id>.htm`) listing the author's works
- Server-rendered HTML, parseable with a Chrome-UA `QNetworkRequest`
- Direct cover image URLs (`/covers/<isbn>.jpg`)
- `og:` structured meta tags (`og:title`, `og:isbn`, `og:type=books.book`, `og:image`, `og:url`)

### §3.2 Live-verified FictionDB facts (2026-05-27 curl probes)

- Book page `https://www.fictiondb.com/title/dune~frank-herbert~99723.htm` → HTTP 200, 94KB, server-rendered. Contains `og:title="Dune by Frank Herbert"`, `og:isbn="9780441172719"`, `og:image="…/covers/0441172717.jpg"`, synopsis, genre, publication year.
- Series page `https://www.fictiondb.com/series/dune-chronicles-frank-herbert~3735.htm` → HTTP 200, 81KB, lists the 6 Dune Chronicles books with years.
- Cloudflare-proxied but NOT challenging — 3 rapid burst requests all returned HTTP 200, zero "just a moment" interstitials. (Cloudflare's `challenge-platform` beacon is present as a passive script but does not gate access at moderate rates.)
- Cover URL derivation: `/covers/<ISBN-10>.jpg`. ISBN-10 derivable from the og:isbn (ISBN-13) or scraped directly.

### §3.3 Existing Tankoban surfaces reused / reworked

- `src/core/book/BookCatalogueResult.h` — POD for a catalogue hit. **Reused** (source-agnostic; already carries title/author/isbn/coverUrl/seriesId/seriesName/seriesPosition/seriesTotal).
- `src/core/book/CatalogueRecord.{h,cpp}` — library entity. **Reused** unchanged.
- `src/core/book/BooksCatalogueLibraryStore.{h,cpp}` — JSON-backed library store. **Reused** unchanged.
- `src/core/book/CatalogueDeduper.{h,cpp}` — **Simplified** (single source now; cross-source merge logic mostly retired).
- `src/core/book/SeriesDetector.{h,cpp}` — **Retired from the pipeline.** Its heuristic series-detection (title-suffix patterns, Roman numerals) is replaced by FictionDB's explicit series pages. The file may stay compiled for its tests but is removed from the aggregator's path.
- `src/core/book/OpenLibraryClient.{h,cpp}` + `GoogleBooksClient.{h,cpp}` — **Dropped from the aggregator fan-out.** Files stay compiled-but-unwired (dormant, like `AnnaArchiveScraper`) so a future nonfiction revisit can re-enable them. (Hemanth may elect full deletion at plan time.)
- `src/ui/pages/books/BookCatalogueSearchWidget.{h,cpp}` — search-takeover storefront. **Reused** (two-section Series/Books layout already matches FictionDB's output shape).
- `src/ui/pages/books/BookCatalogueDetailView.{h,cpp}` — movie-shape detail view. **Reused** for standalone novels (already carries §5.2 download flow + §3.3 backout).
- `src/core/book/BookSearchAggregator.{h,cpp}` + `LibGenScraper` + `TankorentBookScraper` + `BookDownloader` — the source + download layer. **Reused unchanged** (§5.2 already wired the catalogue→source→download→record→reader chain).

### §3.4 Stream-side blueprint for the series detail view

The new `BookSeriesDetailView` ports patterns from:
- `src/ui/pages/stream/StreamDetailView.h` — hero + season/series header + per-row table + RowState enum + per-row action buttons + right-click context menu.
- The approved Stormlight mockup at `docs/superpowers/mockups/2026-05-20-books-stremio-pivot/series-detail-stormlight.html` (Hemanth-approved 2026-05-20 ~6:30pm).

---

## §4 Architecture (Approach 3 — Hybrid)

Three layers, as established by BOOKS_STREMIO_PIVOT. This arc only changes the catalogue layer's *source* + adds the series-shape detail surface:

```
   CATALOGUE LAYER          SOURCE LAYER               READER
   (discovery/metadata)     (file acquisition)
   ─────────────────        ──────────────────         ──────
   FictionDB            →    TankoLibrary (LibGen)  →   BookReader
   (was OpenLib+GBooks)      + Tankorent
```

### §4.1 New — `FictionDbClient` (`src/core/book/FictionDbClient.{h,cpp}`)

Scraper mirroring `OpenLibraryClient`'s QObject + QNetworkAccessManager + signal shape. Operations:

- `void search(const QString& query)` → GET FictionDB search endpoint → parse results → emit `searchResults(QString query, QList<BookCatalogueResult> series, QList<BookCatalogueResult> standalone)`. (Series hits carry `seriesId` + `seriesName`; standalone hits don't.)
- `void fetchSeries(const QString& seriesId)` → GET `/series/...` → parse ordered book list → emit `seriesReady(QString seriesId, QString seriesName, QString author, QList<BookCatalogueResult> books)` (each book carries its `seriesPosition`).
- `void fetchBook(const QString& bookId)` → GET `/title/...` → parse `og:` tags + inline metadata → emit `bookReady(BookCatalogueResult)`.
- `void fetchAuthorWorks(const QString& authorId)` → GET `/author/...` → emit `authorWorksReady(QString authorId, QList<BookCatalogueResult>)`.
- Per-source failure → `fetchFailed(QString context, QString error)` surfaced as a `ToastHud` toast.
- All requests use a Chrome-UA `QNetworkRequest` helper (UA + Accept + 10s transferTimeout + NoLessSafeRedirectPolicy — same shape as the OpenLibraryClient hardening + LibGenScraper).

Parsing strategy: prefer `og:` meta tags (stable) for title/isbn/cover; fall back to documented CSS selectors for synopsis/genre/year/series-position. Cover URL taken from `og:image` directly.

### §4.2 Reworked — `BookCatalogueAggregator`

- Drops the OL + GB fan-out. FictionDB is the sole upstream.
- `search(query)` → `FictionDbClient::search` → forwards the series/standalone split to the storefront.
- Series detection: NO heuristic. Series membership comes from FictionDB's `searchResults` series list + `fetchSeries`. `SeriesDetector` is removed from this path.
- `CatalogueDeduper` simplifies — within a single source, dedupe is only needed for the rare case of FictionDB returning near-duplicate rows; cross-source merge logic is retired.

### §4.3 Reused — storefront (`BookCatalogueSearchWidget`)

Two sections, Series first then Books (§3.5 of the predecessor spec). FictionDB's `searchResults(series, standalone)` maps directly: series list → Series section, standalone list → Books section. `kInitialCap = 6` + "Show N more" overflow (the §3.5 polish item) folds into this arc since the storefront is in active scope. Clicking a series tile → series detail view; clicking a standalone tile → movie detail view.

### §4.4 New — `BookSeriesDetailView` (`src/ui/pages/books/BookSeriesDetailView.{h,cpp}`)

Series-shape detail surface. Forked from `StreamDetailView` + the Stormlight mockup:
- Hero: series cover (book 1 default) + series name + author + meta strip ("6 books · science fiction · 1965–1985").
- Series synopsis + genre chips below the hero.
- Books-in-series table: one row per book in reading order. Each row shows cover thumbnail + title + position + a per-book action button.
- **Per-book action button states (lazy per D3):**
  - `[Search for downloads]` — default; click fires `BookSearchAggregator::searchFor(book)` for THAT book → picker → §5.2 download flow.
  - `Downloading XX%` — during that book's download.
  - `[Read]` — when that book's `CatalogueRecord` exists (file on disk).
- No bulk "download entire series" button (D4 — deferred to v1.x).
- `backRequested()` → returns to storefront/library.
- Each per-book download reuses the exact §5.2 chain already built: `downloadRequested` → BooksPage → BookDownloader (HTTP mirror-failover or magnet) → `downloadComplete` → `BooksCatalogueLibraryStore::upsertRecord` → grid refresh + reader.

### §4.5 Reused — movie-shape detail view (`BookCatalogueDetailView`)

Standalone novels keep using the detail view shipped today (§5.2 + §3.3 backout). Unchanged.

### §4.6 Routing (`BooksPage`)

- Storefront tile click: series result → `BookSeriesDetailView`; standalone result → `BookCatalogueDetailView`. Decided by the result's `seriesId` (non-empty = series).
- Both detail views feed the same `BooksPage` download slots (`onCatalogueDownloadRequested` etc.) so the §5.2 record-creation + reader-open path is shared.

---

## §5 User-facing flows

### §5.1 Search → series → per-book download

User types "dune" → storefront shows Series section [Dune Chronicles] + Books section [standalone Dune titles]. Click Dune Chronicles → series detail view: hero + 6 books in order, each with `[Search for downloads]`. Click Book 1 (Dune) → LibGen/Tankorent search → picker → click a source → mirror-failover download → Book 1's row morphs to `[Read]` + the file opens in the reader + the library grid now has Dune. Books 2-6 remain `[Search for downloads]` until the user grabs each.

### §5.2 Search → standalone → download

User types "the godfather" → storefront Books section shows the Mario Puzo novel (standalone, no series). Click it → movie-shape detail view (the one tested today) → `[Search for downloads]` → §5.2 flow → downloads + opens.

### §5.3 Continue reading

Unchanged from BOOKS_STREMIO_PIVOT — the Continue strip walks catalogue records with `readProgress` in (0,1). FictionDB-sourced records flow through identically.

---

## §6 Out of scope / deferred

- **Nonfiction discovery** — dropped with OL+GB (D1). Re-enableable later if Hemanth revisits; OL/GB clients stay compiled-dormant.
- **Bulk "download entire series"** (D4) — v1.x.
- **Auto-search availability on series open** (D3) — v1.x.
- **Smarter sort/ranking within search results** (popularity, first-publish-year) — v1.x. FictionDB's native ordering is the v1 default.
- **`og:`-tag-less edge cases** — if a FictionDB page lacks structured tags, fall back to CSS selectors; truly unparseable pages are skipped (logged, not crashed).
- **AnnaArchiveScraper re-enable** — still dormant per Path C (2026-05-21).
- **Cover multi-resolution** — FictionDB serves one cover size; we cache + scale client-side as today.

---

## §7 Cross-agent coordination

- **Agent 4 (Tankorent)** — no new coordination. The §5.2 catalogue→Tankorent bridge already exists; FictionDB just feeds cleaner ISBN/title queries into the existing `TankorentBookScraper` path.
- **Agent 1 (Comics)** — none. (MangaFire scraping is the architectural cousin, not a shared surface.)
- **Agent 0 (Coordinator)** — spec commit + plan-phase tracking + sweep. This arc's RTCs join the same sweep cadence.
- **Agents 3, 5** — untouched.

---

## §8 Process

Brainstorm followed `/superpowers:brainstorming` one-question-at-a-time (Agent 2's pacing per the 2026-05-20 BOOKS_STREMIO_PIVOT precedent, NOT Agent 1's batches-of-4). Four forks locked: D1 (drop OL+GB), D2 (full series treatment), D3 (lazy per-book), D4 (per-book only). Architecture approach D6 (hybrid) recommended + accepted.

### Next gates

1. **Hemanth reviews this spec** → approves or requests changes.
2. **`/superpowers:writing-plans`** → produces `docs/superpowers/plans/2026-05-27-books-fictiondb-catalogue.md`.
3. **Agent 2 authors `BOOKS_FICTIONDB_CATALOGUE_FIX_TODO.md`** (optional, for phase-cursor tracking) at writing-plans completion.
4. **Execution** — inline or subagent-driven per plan size. Phase order likely: FictionDbClient (with parser tests) → aggregator rework → storefront routing → BookSeriesDetailView → BooksPage routing → drop OL+GB from fan-out → build + smoke.
5. **Live smoke** — search "dune" → series tile → series detail → per-book download → reader, end to end via tankoctl + Hemanth visual confirm.

### Open implementation calls (Agent-2 Rule-14, decided at plan time)

- FictionDB search-results HTML parsing shape (search endpoint URL + result-row selectors).
- ISBN-10 ⇄ ISBN-13 conversion for cover URL derivation.
- Whether to fully delete vs keep-dormant OpenLibraryClient + GoogleBooksClient (default: keep-dormant).
- Cover-cache keying for FictionDB covers (reuse existing `m_catalogueCoverDir` pattern).
