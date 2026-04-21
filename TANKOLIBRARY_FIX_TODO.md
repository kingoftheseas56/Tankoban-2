# TankoLibrary Fix TODO

**Owner:** Agent 4B (Sources — Tankorent + Tankoyomi + TankoLibrary).
**Authored:** 2026-04-21 by Agent 0, on Agent 4B's summon after domain-master validation of `agents/audits/tankolibrary_2026-04-21.md` (Agent 7 Codex audit).
**Status:** AUTHORED; M1 queued pending Agent 4B summon.
**Reference audit:** [agents/audits/tankolibrary_2026-04-21.md](agents/audits/tankolibrary_2026-04-21.md). Agent 4B validation post at `agents/chat.md` 2026-04-21 14:?? block.

---

## 1. Context

Tankoban has no book-source sub-app today. SourcesPage exposes only Tankorent + Tankoyomi (stack indices 0=launcher, 1=tankorent, 2=tankoyomi). BooksPage is local-library-first — it scans configured directories for `*.epub/pdf/mobi/fb2/azw3/djvu/txt` and builds UI from what's on disk. There is currently no in-app path to discover and download books from shadow-library sources.

Agent 7's 2026-04-21 audit evaluated three candidate source surfaces (Anna's Archive, LibGen, Z-Library) plus two implementation references (Openlib Flutter AA client, zshelf Z-Library client) and produced a greenfield pre-planning audit with 4 P0 / 3 P1 / 2 P2 findings and 5 hypotheses for Agent 4B.

Agent 4B validated all 5 hypotheses 2026-04-21 and committed to a two-track plan:
- **Track A — Main** (phases M1/M2/M3): scaffold + Anna's Archive search → download end-to-end + LibGen dual-source fan-out.
- **Track B — Polish** (after M3 lands): filters surface, cover fetch+cache, detail cards, loading/error states, per-source IndexerHealth.

Hemanth has ratified: source choice (AA + LibGen, Z-Library deferred), two-track structure, M1 as starting batch. Phase-entry scope for M1 is frozen below; M2 and M3 scope finalizes at batch entry per Agent 4B's Rule-14 calls.

---

## 2. Objective

Ship a new `TankoLibrary` sub-app sibling to Tankoyomi that lets users discover and download books from Anna's Archive + LibGen shadow-library sources. Downloaded files land in the existing BooksPage library path so `LibraryScanner` picks them up on next scan. Z-Library is explicitly out of v1 scope and stays parked behind a later stateful-source phase.

On completion of Track A (M1 + M2 + M3):
1. SourcesPage gains a third launcher tile (TankoLibrary) alongside Tankorent and Tankoyomi.
2. TankoLibraryPage renders search results from Anna's Archive and LibGen as a unified grid.
3. Downloads end-to-end: user picks a result → AA/LibGen download-resolution flow runs → file lands in BooksPage library path → BooksPage picks it up on next scan.
4. "All Sources" fan-out aggregates AA + LibGen results like Tankorent aggregates its indexers.

On completion of Track B: filter UI (language/format/sort/year), first-class covers + detail cards, honest error + loading states, per-source IndexerHealth persistence.

---

## 3. Non-goals

- **No Z-Library in v1.** Agent 4B validated H4 (Z-Library adds less incremental v1 coverage than its architecture cost suggests — AA already federates z-lib's corpus). Z-Library would make Tankoban's first stateful indexer (per-user domain + cookie + account-status + rate-limit UI) and is deferred behind a future stateful-source phase if AA+LibGen proves insufficient.
- **No direct port of Openlib selectors.** Openlib hardcodes `div.flex.pt-3.pb-3.border-b` + `div.font-semibold.text-2xl` which are pre-drift relative to current Anna templates. Openlib stays a flow reference only (search → detail → slow-download → webview DOM scrape → HEAD probe → checksum → write). M1's first task is a fresh HTML-snapshot pass on current AA domains + selector authoring against actual templates.
- **No reuse of MangaResult.** Book metadata needs format, language, publisher, year, pages, ISBN/identifier, file size, MD5 — richer than chapter/page traversal. New `BookResult` + `BookScraper` contracts under `src/core/book/`.
- **No trending books surface.** Openlib's is not real AA trending (it aggregates Goodreads/Penguin/BookDigits and back-converts to title search). Skip v1.
- **No account-tier / saved-books / search history persistence.** All z-lib-era features; either moot (z-lib deferred) or out of scope (search history stays session-scoped like Tankorent).

---

## 4. Agent ownership

**Primary:** Agent 4B.
**Coordinator:** Agent 0 (authoring this TODO, CLAUDE.md dashboard row, commit sweep).

**Cross-agent touches expected:**
- **Agent 5 (Library UX)** — may need a ping if TankoLibraryPage's grid/tile rendering shares code with existing Tankoyomi/Tankorent surfaces (Agent 5 owns cross-mode library-side UX per `feedback_agent5_scope`). Coordinate before reinventing.
- **Agent 2 (Book Reader)** — no code touches expected. BooksPage library-path integration uses existing LibraryScanner infrastructure owned by Agent 5. Agent 2's domain (reading EPUB/MOBI/etc.) stays untouched; new files just arrive in library path + Agent 2's reader opens them unchanged.
- **Agent 0** — CLAUDE.md dashboard row at authoring. After each phase exit, dashboard row update + commit sweep.

---

## 5. Architecture — `src/core/book/` tree

New directory parallel to `src/core/manga/`:

```
src/core/book/
    BookResult.h            — result-row data model (title, author, publisher, year, pages, language, format, size, ISBN, md5/source-id, cover URL, access state, description)
    BookScraper.h           — interface: search(query, filters), fetchDetail(md5OrId), resolveDownload(md5OrId)
    AnnaArchiveScraper.cpp/h
    LibGenScraper.cpp/h
    AaSlowDownloadWaitHandler.cpp/h  — Anna's slow-download countdown + no_cloudflare wait handler (new capability, orthogonal to CloudflareCookieHarvester)
    BookDownloader.cpp/h    — HTTP streaming with resume, HEAD probe, checksum verify, write-to-library-path
```

UI additions:

```
src/ui/pages/
    TankoLibraryPage.cpp/h
    tankolibrary/
        BookResultsGrid.cpp/h
        [additional components land per Track B phases]
```

`src/ui/pages/SourcesPage.cpp/h` — stack index grows: 0=launcher, 1=tankorent, 2=tankoyomi, **3=tankolibrary**. Add launcher tile.

**Reuse:**
- `src/core/indexers/CloudflareCookieHarvester` — reusable AS-IS for AA's Cloudflare `cf_clearance` stage. Does NOT subsume AA's distinct slow-download countdown + `no_cloudflare` warning page (that's `AaSlowDownloadWaitHandler`'s job).
- `src/core/TorrentIndexer::IndexerHealth` pattern — mirror for `BookSourceHealth` persistence in Track B.
- `PosterFetcher` pattern — reuse where applicable for cover fetching in Track B.
- BooksPage library-path config — canonical download destination from day one (no divergent library structure).

---

## 6. Track A — Main (phases M1, M2, M3)

### Phase M1 — Scaffold + AA search-only

**Scope (frozen at wake entry by Agent 4B):**
1. **AA HTML snapshot pass** — reachability test from this network on `annas-archive.li`, `.se`, `.org`. Live template fetch against current search + detail pages from the reachable domain. Record snapshot timestamp + domain selected + template hash for drift-detection.
2. **`src/core/book/` scaffold:**
   - `BookResult.h` with the full data model per §5.
   - `BookScraper.h` interface.
   - `AnnaArchiveScraper.{cpp,h}` with `search(query, filters)` implemented against the actual snapshot selectors (NOT Openlib's). `fetchDetail` + `resolveDownload` scaffolded as stubs for M2.
3. **`TankoLibraryPage.{cpp,h}` scaffold** wired into SourcesPage as third button (index 3). Empty BookResultsGrid component renders search results. No filter UI, no detail view, no download button (M2/M3 territory).

**Smoke verification (M1 exit):**
- Launch Tankoban via `build_and_run.bat`.
- Navigate: Sources tab → TankoLibrary launcher tile → search "orwell 1984" → results grid renders AA hits with title + author + format + year.
- Zero downloads this batch.
- Zero regressions to Tankorent or Tankoyomi surfaces.
- Rule 17 cleanup green via `scripts/stop-tankoban.ps1`.

**Rule-14 design calls at wake entry:**
- Which AA domain to target primary vs fallback (`.li` / `.se` / `.org`) based on reachability test.
- Selector granularity — CSS selectors vs XPath vs targeted regex — per what current AA templates actually expose.
- BookResult optional-field handling (empty string vs null-like sentinel) for fields AA doesn't surface in search results (many book pages lack ISBN or MD5 until detail page).

### Phase M2 — AA detail + download end-to-end

**Scope (finalizes at M2 wake entry):**
1. `AnnaArchiveScraper::fetchDetail(md5OrId)` — detail page parser for title, description, full metadata, slow-download link list, browser-verification flags.
2. Cloudflare harness — reuse `CloudflareCookieHarvester` for stage (a) `cf_clearance` harvest.
3. `AaSlowDownloadWaitHandler.{cpp,h}` — new capability for stage (b) AA slow-download countdown + `no_cloudflare` warning page handling.
4. `AnnaArchiveScraper::resolveDownload(md5OrId)` — end-to-end: detail → slow-download link → CF+wait handling → HEAD probe of mirror candidates → direct download URL.
5. `BookDownloader.{cpp,h}` — HTTP streaming with resume support, progress signal, checksum verify on completion, write-to-library-path.
6. Download destination = BooksPage library path (read from existing QSettings config).
7. TankoLibraryPage wires: result row click → detail view → download button → BookDownloader with progress.

**Smoke (M2 exit):**
- Full flow: search → pick result → detail → download → file lands in library path → BooksPage next scan picks it up.
- One book downloads cleanly end-to-end.
- Honest error messages if CF challenge fails / slow-download wait timeout / mirror HEAD all fail.

### Phase M3 — LibGen + dual-source fan-out

**Scope (finalizes at M3 wake entry):**
1. `LibGenScraper.{cpp,h}` against `libgen.rs/json.php` (or reachable mirror). Fields per current libgen-api-modern contract: title, author, publisher, year, pages, language, ISBN, MD5, extension, file size.
2. `resolveDownload` — `book/index.php?md5=<md5>` mirror page → direct download URL.
3. "All Sources" fan-out filter at TankoLibraryPage level — query dispatches to both AnnaArchiveScraper + LibGenScraper in parallel, results bucketed by source + aggregated into unified grid (like Tankorent's All-Sources pattern).
4. Per-source badge on result rows so user knows whether a hit is AA or LibGen.

**Smoke (M3 exit):**
- Query returns AA + LibGen rows aggregated.
- Download works from both sources.
- Source-specific failures surface honestly (one source failing doesn't take down the grid).

### Track A exit criteria

- All three phases shipped.
- Smoke matrix: 3+ successful downloads across AA and LibGen, 2+ distinct book formats (EPUB + PDF at minimum), BooksPage picks up all downloaded files on next scan.
- Zero regressions to Tankorent or Tankoyomi.
- READY TO COMMIT posted per phase; Agent 0 sweeps.

---

## 7. Track B — Polish (after M3 lands)

Scope finalizes when Agent 4B picks it up. Target scope per audit P1 + Agent 4B's §8 "skipped in v1" list:

1. **Filter surface** (language, format, sort, year) — UI + state + query-param wiring for both AA and LibGen. Filter vocabulary harmonized across sources (a single "format=EPUB" applies to both).
2. **Cover fetch + cache** — reuse PosterFetcher pattern. Fetch at search-result-render time, cache to disk, hydrate grid async.
3. **Detail cards** — full metadata panel (publisher, year, pages, language, ISBN, description, file size) + download button with progress bar.
4. **Loading + error states** — honest "Searching…" / "Source unreachable" / "Download failed — mirror N of M failed" / empty-results. Match Tankorent's error-taxonomy maturity.
5. **Per-source `BookSourceHealth` persistence** — mirror `TorrentIndexer::IndexerHealth`. Track consecutive failures, last-success timestamp, user-disable toggle in TankoLibrary's own Sources config popover.

---

## 8. Files expected to touch

**New (in-repo):**
- `src/core/book/BookResult.h`
- `src/core/book/BookScraper.h`
- `src/core/book/AnnaArchiveScraper.{cpp,h}`
- `src/core/book/LibGenScraper.{cpp,h}` (M3)
- `src/core/book/BookDownloader.{cpp,h}` (M2)
- `src/core/book/AaSlowDownloadWaitHandler.{cpp,h}` (M2)
- `src/ui/pages/TankoLibraryPage.{cpp,h}`
- `src/ui/pages/tankolibrary/BookResultsGrid.{cpp,h}`
- Track B adds more UI components as phases define them.

**Modified:**
- `src/ui/pages/SourcesPage.{cpp,h}` — stack index 3 + launcher tile.
- `CMakeLists.txt` — new compilation units for the core/book/ and tankolibrary/ directories.

**Reused (no changes):**
- `src/core/indexers/CloudflareCookieHarvester.{cpp,h}` — stage (a) CF harvest only.
- `src/ui/pages/BooksPage.cpp` + `LibraryScanner` (Agent 5 domain) — library-path read + post-download scan trigger.
- `PosterFetcher` — Track B cover fetch.

---

## 9. Rule-14 design calls at wake entry

Finalizes per-phase (not here):
1. **M1 — AA domain primary vs fallback** — based on live reachability test from this network.
2. **M1 — selector strategy** — CSS selectors vs XPath vs regex per actual template.
3. **M1 — BookResult empty-field handling** — empty string, null-like sentinel, or `std::optional<>` wrapper.
4. **M2 — download-resolution failure policy** — how many mirror HEAD probes, what timeout, how to classify errors.
5. **M2 — BookDownloader resume vs restart** — on interrupted download, resume from Range or start fresh.
6. **M3 — All Sources fan-out concurrency** — parallel dispatch vs sequential, timeout per source, aggregate-ranking when both succeed.
7. **M3 — duplicate-result dedup** — same ISBN/MD5 from AA and LibGen, prefer which source, or surface both with badges.

---

## 10. Verification procedure (per-phase)

**M1 exit:**
1. `build_check.bat` → BUILD OK.
2. `build_and_run.bat` launches cleanly.
3. MCP-driven or manual smoke on "orwell 1984" query via TankoLibrary tab → results grid renders with title/author/format/year on each row.
4. Tankorent + Tankoyomi tabs still function unchanged.
5. Rule 17: `scripts/stop-tankoban.ps1` kills Tankoban + sidecar cleanly.
6. READY TO COMMIT line in chat.md; Agent 0 sweeps.

**M2 exit:**
1. End-to-end download of one book — EPUB or PDF.
2. File lands in configured BooksPage library path.
3. BooksPage rescans (manual or automatic) and the new book appears.
4. Reader opens the book successfully (Agent 2's domain; just a smoke).
5. Rule 17 cleanup green.

**M3 exit:**
1. Query returns AA + LibGen rows aggregated.
2. Download works from both sources.
3. One source failing doesn't crash the grid or block the other source's results.
4. Rule 17 cleanup green.

---

## 11. Rollback strategy

Per-phase `git revert HEAD` per phase commit is safe:
- M1 revert — SourcesPage stack index 3 goes away, launcher tile disappears, core/book/ files reverted. Tankorent + Tankoyomi unchanged.
- M2 revert — M1 stays; download path comes out. Scaffold still works for search.
- M3 revert — M1 + M2 stay; LibGen + fan-out come out. AA-only survives.

No protocol or IPC changes (sidecar untouched). No cross-process state. No schema migration. Additive code only.

---

## 12. What NOT to include (explicit deferrals)

- **Z-Library support.** Deferred to a later stateful-source phase if AA+LibGen proves insufficient. Requires: per-user domain selection, cookie + remix_userkey/userid storage, account-status UI, rate-limit messaging, rotating-domain handling. Not scoped in any Track.
- **Trending books surface.** Openlib's is not real AA trending; no clean v1 implementation path.
- **Account-tier UI.** Z-Library-specific.
- **Saved-books list.** Z-Library-specific.
- **Search history persistence.** Keep session-scoped like Tankorent.
- **First-party Anna account flow.** AA has no official account for clients; ignore.
- **Multi-user or sync.** Tankoban is single-user desktop-first; no cloud state.
- **Mobi/azw3/djvu conversion pipelines.** Download-as-is; BooksPage reader opens whatever formats it supports.

---

## 13. Reference material

- **Audit:** [agents/audits/tankolibrary_2026-04-21.md](agents/audits/tankolibrary_2026-04-21.md) — Agent 7's greenfield pre-planning audit.
- **Agent 4B validation + domain position:** `agents/chat.md` 2026-04-21 14:?? block (H1-H5 all validated + architecture decisions + Track A/B split).
- **Openlib (flow reference only):** `C:\Users\Suprabha\Downloads\tankolibrary references\Openlib\` — Flutter AA client. Flow decomposition useful; selectors pre-drift.
- **zshelf (Z-Library reference, NOT for v1):** `C:\Users\Suprabha\Downloads\tankolibrary references\zshelf\` — Z-Library client with cookie + domain auth. Cost surface reference if Z-Library ever enters scope.
- **Current AA templates:** https://github.com/LilyLoops/annas-archive (public mirror of Anna's Archive codebase). Authoritative for current selector + flow contract.
- **LibGen client reference:** `libgen-api-modern` on PyPI (current Python wrapper). JSON schema + field vocabulary for LibGenScraper.
- **Cross-references:**
  - `src/core/manga/MangaScraper.h` — Tankoyomi-shaped interface pattern. TankoLibrary is structurally similar, semantically different.
  - `src/core/indexers/TorrentIndexer.h` — `IndexerHealth` pattern for Track B `BookSourceHealth`.
  - `src/core/indexers/CloudflareCookieHarvester.{cpp,h}` — reusable for AA stage (a).
  - `src/ui/pages/BooksPage.cpp:366-425` — library scan surface; TankoLibrary downloads land where this scans.
- **Feedback memory:**
  - `feedback_audit_framing_standard_not_better_worse` — MATCHES STANDARD / DEVIATES framing for any source comparison.
  - `feedback_audit_validation_same_turn` — audit → validate → TODO authoring one-turn discipline (proven here).
  - `feedback_reference_during_implementation` — open actual reference source during implementation, don't rely on audit summary alone.

---

## 14. Cross-agent coordination hooks

- **Agent 0** — authors this TODO + CLAUDE.md dashboard row + commit sweep. Post-M1/M2/M3 phase-boundary commits.
- **Agent 5** — ping if BookResultsGrid needs to share TileCard/TileStrip infrastructure with library surfaces. Agent 5's domain owns cross-mode library UX.
- **Agent 2** — no coordination needed for Track A. Agent 2's reader opens BooksPage-landed files unchanged. If Track B adds book-detail card styling that mirrors reader UX, coordinate then.
- **Agent 4B** — primary owner throughout.

---

**End of TODO.**
