# Books Data Domain — Agent 2

This file auto-loads when any agent reads a file under `src/core/book/` (Claude Code nested CLAUDE.md behavior — files in subtree → that subtree's CLAUDE.md is included in context). Part of the path-scoped guidance migration (Phase 2 Item 4 of the CLAUDE_CODE_PRACTICES arc, 2026-05-21). Sibling for the UI side lives at `src/ui/pages/tankolibrary/CLAUDE.md`.

## Domain owner

**Agent 2** (Book Reader + TankoLibrary). TankoLibrary ownership inherited from Agent 4B on 2026-05-20 at 4B's brotherhood departure. This layer is the catalogue + scraper + metadata + downloader plumbing — everything below the UI chrome.

## Active arc — BOOKS_STREMIO_PIVOT

Vision locked 2026-05-20, Agent 2 owns. Hemanth verbatim: *"changing book mode into the same as comic mode and stream mode... metadata api like cinemta and anilist... catalogue of books then connected to tankolibrary."*

**Operative shape:** Open Library + Google Books catalogue layer → TankoLibrary source-side ingestion → existing BookReader. Mirrors the comics arc exactly: Comics has AniList/Fandom catalogue → Tankoyomi source → ComicReader; Stream has Cinemeta → Tankorent → ffmpeg sidecar. Books is the third domain to get this treatment.

**Pattern table:**
- Stream: Cinemeta catalogue → Tankorent source → ffmpeg sidecar player. Owner: Agent 4.
- Comics: Fandom + AniList catalogue → Tankoyomi source → ComicReader. Owner: Agent 1.
- Books: Open Library + Google Books catalogue → TankoLibrary source → BookReader. Owner: Agent 2.

**Phase cursor (as of 2026-05-21):**
- Phases 1–3 SHIPPED: data model + Open Library + Google Books clients + SeriesDetector + CatalogueDeduper + BookCatalogueAggregator orchestrator. ~50 GoogleTest cases. BUILD OK.
- Phase 4 SHIPPED: AA captcha investigation → Path C decision (defer AA to v1.1; v1 = LibGen + Tankorent only), TankorentBookScraper + BookSearchAggregator skeleton + BookDownloader::startMagnetDownload. BUILD OK.
- Phases 5–8 (search widget + picker + detail view + bookshelf integration) queued. Likely Trigger E (Agent 2 Jrs in parallel tabs).
- Phase 9 (BooksPage rewire + integration + smoke) inline at arc close.

**Path C (locked 2026-05-21):** AA's `/ads.php` + `/slow_download/` are Cloudflare Turnstile-gated (architecturally distinct from `cf_clearance`-class cookies that `CloudflareCookieHarvester` handles). No public API. `AnnaArchiveScraper` stays compiled but disabled at construction (`TankoLibraryPage.cpp:254` — one-line re-enable when v1.1 revisit window opens). v1.1 options: 2Captcha/CapMonster integration OR visible-webview modal flow. Full audit: `agents/audits/aa_captcha_investigation_2026-05-21.md`.

**Process gate (Rule 20, gov-v4):** Codex Trigger C review-and-expand gate was Hemanth-explicit-skipped at brainstorm-close ("you have my go ahead on all the specs"). No Codex pass. Documented in chat.md for the eventual arc-close audit.

**Brainstorm pacing:** batches of 4 questions per `feedback_brainstorm_batches_of_four.md`. Already complete for this arc.

## File map — what lives here

**Catalogue layer (Phases 1–3, all shipped):**
- `BookCatalogueResult.h` — POD for a single catalogue hit (title/author/isbn/openLibKey/coverUrl/seriesId etc.)
- `CatalogueRecord.{h,cpp}` — JSON-round-trippable record; persisted by `BooksCatalogueLibraryStore`
- `BooksCatalogueLibraryStore.{h,cpp}` — in-memory + on-disk catalogue cache (mirrors the way Tankoyomi's series cache works)
- `OpenLibraryClient.{h,cpp}` — Open Library REST client (primary metadata API; free, no key, ISBN/OLID/cover/subjects)
- `GoogleBooksClient.{h,cpp}` — Google Books fallback (broader catalog; needs API key + quota'd)
- `SeriesDetector.{h,cpp}` — heuristic series-detection (title patterns + series-field signal); 11 tests
- `CatalogueDeduper.{h,cpp}` — ISBN + fuzzy-title merge; 8 tests
- `BookCatalogueAggregator.{h,cpp}` — fan-out orchestrator (drives OpenLibrary + GoogleBooks → dedup → series-detect → seriesId assignment)

**Scraper / downloader layer (inherited from Agent 4B + Phase 4 additions):**
- `BookScraper.h` — abstract base for all book sources (LibGen, AnnaArchive, TankorentBook)
- `LibGenScraper.{h,cpp}` — LibGen.li HTML scraper (no captcha; `topics[]=l&topics[]=f` filter; format-narrowing client-side only — see `reference_libgen_url_params.md`)
- `AnnaArchiveScraper.{h,cpp}` — AA scraper (compiled, disabled at construction; v1.1 revisit)
- `AaSlowDownloadWaitHandler.{h,cpp}` — AA stage-(b) countdown + `no_cloudflare` interstitial handler (dormant at v1 per Path C)
- `TankorentBookScraper.{h,cpp}` — Tankorent bridge (consumes `TankorentSearchService` 3-signal contract: `resultsReady` / `indexerError` / `searchFinished`; `TorrentResult → BookResult` mapping with format inference from filename patterns; `downloadUrl` carries magnetUri so `BookDownloader::startMagnetDownload` pipes straight through)
- `BookSearchAggregator.{h,cpp}` — source-agnostic fan-out (takes `QList<BookScraper*>` at ctor; picker-widget owns source-list construction in Phase 8)
- `BookDownloader.{h,cpp}` — HTTP download path (`startDownload`) + magnet/torrent path (`startMagnetDownload`; consumes `TorrentClient::addMagnetHeadless` + `torrentCompleted` signal); stale-key detection for LibGen's ephemeral `get.php?key=` param
- `AbbScraper.{h,cpp}` — additional scraper (legacy, in-tree)
- `BookResult.h` — search-result POD (format/language/publisher/year/pages/ISBN/MD5/size/downloadUrl/magnetUri)

## Reference apps

- **Cinemeta** — the "metadata API like cinemeta" Hemanth explicitly named. Open Library is the books equivalent: free, no key, rich ISBN/cover/subject data.
- **Stream mode (Stremio architecture)** — the BLUEPRINT for this arc. Cards → series view → in-library downloads. Same as how Comics took Stream as a blueprint.
- **LibGen** — primary scraper source for v1 (zero captcha, direct HTML, Chrome-UA plain `QNetworkRequest` works).

## Load-bearing memories (read when touching this domain)

- `project_books_stremio_pivot_2026-05-20.md` — vision lock + arc shape + pattern parity table
- `reference_libgen_url_params.md` — live-probed LibGen endpoint facts (topics[], format-narrowing client-side only, ~60s key rotation on get.php, cover URL derivation via /ads.php parse)
- `project_audiobook_paired_reading.md` — AUDIOBOOK_PAIRED_READING_FIX_TODO (Phase 1+2 shipped, Phase 3+4 queued); audiobooks stay inside Books tab — no standalone mode
- `project_tts_kokoro.md` — **Kokoro TTS REMOVED 2026-04-15. Do NOT reintroduce.** Edge TTS only (`EdgeTtsClient` + `EdgeTtsWorker`, Qt direct WSS, EDGE_TTS_FIX_TODO closed 2026-04-16).
- `feedback_stream_server_firewall_gotcha.md` — Cloudflare-adjacent caution: silent-fail pattern (peers>0 + dlSpeed=0 for 30s) applies to torrent/magnet downloads too, not just Stremio stream-server. Check firewall state before diagnosing BookDownloader magnet path.
- `feedback_brainstorm_batches_of_four.md` — Agent 2's brainstorm pacing (batches of 4 AskUserQuestion, not one-at-a-time)
- `feedback_plan_first_zero_errors.md` — plan-mode before ≥50 LOC or multi-file or new behavior
- `project_agent4b_departure_2026-05-20.md` — 4B's inheritance ledger; honor the scraper hand you're reading

## Dev-bridge surface (Agent 2's commands)

Agent 2 owns two tankoctl prefix families:

**`books-*` (v1.3 — 24 commands):**
- `books-get-state` / `books-get-library` / `books-get-progress` / `books-get-chapters`
- `books-open-book` / `books-open-series` / `books-open-chapter`
- `books-seek-page` / `books-set-layout` / `books-sort` / `books-density`
- `books-tts-state` / `books-tts-play` / `books-tts-pause` / `books-tts-resume` / `books-tts-stop` / `books-tts-set-voice` / `books-tts-set-speed` / `books-tts-cancel-stream`
- `books-refresh-library` / `books-search-library` / `books-clear-search` / `books-get-listen-state` / `books-get-series-state`
- `books-dump-ui`

**`sources-*` (v1.5 — 20 commands, inherited from Agent 4B):**
- `sources-search-tankorent` / `sources-search-tankolibrary`
- `sources-get-indexer-health` / `sources-get-pending-downloads` / `sources-get-tankorent-state` / `sources-get-tankolibrary-state` / `sources-get-tankolibrary-results`
- `sources-add-magnet` / `sources-add-url`
- `sources-pause-torrent` / `sources-resume-torrent` / `sources-remove-torrent`
- `sources-set-speed-limits` / `sources-set-queue-limits`
- `sources-cancel-download` / `sources-cancel-search`
- `sources-force-indexer-refresh`
- `sources-open-tankolibrary-detail` / `sources-download-tankolibrary-selected` / `sources-set-tankolibrary-filters`

Full catalog enumerable via `out\tankoctl.exe ping`.

## Build / MCP lane discipline (gov-v7)

When building or MCP-smoking this domain, use the lease registry per Rules 19 + 22 (gov-v7 landed 2026-05-21 — chat.md announcement at ~12:25pm IST). `out\tankoctl.exe lease-get <lane>` is the machine-truth source; chat.md `## BUILD LANE` / `## MCP LANE` headings remain for human narrative. Lane names: `mcp` for desktop interactions, `build` for `build_check.bat` against shared `out/`. `TANKOBAN_BUILD_LANE=<lane>` bypasses the shared lock for isolated parallel builds.

## When this file activates

Auto-loads when any agent reads a file under `src/core/book/`. Treat it as ambient context: who owns this code, what arc is mid-flight, which memories are load-bearing, which dev-bridge commands belong here.

If you're Agent 2: home turf, redundant reminder.

If you're another brother doing a cross-domain read or audit: this orients you on whose hand you're reading + what taste discipline applies. Cross-domain edits without Agent 2 sign-off are a Rule 14 violation in this domain.
