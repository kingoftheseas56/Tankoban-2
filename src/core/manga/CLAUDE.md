# Manga Source Domain — Agent 1

This file auto-loads when any agent reads a file under `src/core/manga/` (Claude Code nested CLAUDE.md behavior — files in subtree → that subtree's CLAUDE.md is included in context). Sibling file at `src/ui/pages/comics/CLAUDE.md` carries the Comics UI side.

## Domain owner

**Agent 1** (Comic Reader + Tankoyomi as source-side ingestion into Comics mode). Tankoyomi ownership inherited from Agent 4B on 2026-05-14. Standing polish mode on `COMIC_READER_FIX_TODO.md` Phase 6 — **no new UI/UX without explicit Hemanth ratification**.

## Active arc — COMICS_TANKOYOMI_STREAM_MERGER

Vision locked 2026-05-14. This layer is where the arc's backend wiring lives: Tankoyomi dissolves as a standalone source; the scrapers and metadata clients here become the engine for the merged Comics-mode search + download flow. See `src/ui/pages/comics/CLAUDE.md` for the full operative shape, process gate, and Hemanth verbatim quote — not duplicated here.

**What changes in this tree when the arc executes:**
- `MangaSourceRegistry` gains a new Comics-mode routing path (WeebCentral chapter-fetch + torrent volume path unified)
- `NyaaRuntimeSource` / `TorrentVolumeProvider` are the torrent-path primitives feeding `ComicsSeriesView`'s Sources panel
- `WeebCentralVolumePacker` is the HTTP-fallback path — packs individual WC chapters into a cbz volume on the fly
- `ComicsPrePivotMigrator` handles the burn-it-down migration on first launch post-merger
- `FallbackChainResolver` orchestrates the three-backend fan-out: nyaa trusted-uploader → WeebCentral → ReadComics

## What lives here

This tree is the **manga source / scraper / metadata layer** — no Qt widgets, no QML. Think of it as the engine room; `src/ui/pages/comics/` is the cockpit.

**Scrapers:**
- `WeebCentralScraper` / `ReadComicsScraper` — HTTP scrapers for chapter page lists. WeebCentral is confirmed to downscale to ~71% linear / ~50% pixel count of master (see `project_weebcentral_71pct_downscale_confirmed.md`) — it is a fallback, not a primary source.
- `MangaDownloader` — downloads chapter page images and packs them into cbz. Contains the zero-page guard (Fix A) and 1 KB size sanity check (Fix B) from the EMPTY_CBZ_BUG_FIX (2026-05-15). If you see a 22-byte cbz with hex `50 4B 05 06`, that's the pre-fix signature.

**Metadata clients (subdirs):**
- `anilist/` — AniListClient (GraphQL, free, 90 req/min, no auth), AniListCache (file-backed), AniListVolumeMapper (chapters→volumes heuristic, TDD-tested), AniListParser, AniListTypes
- `bookwalker/` — BookWalkerClient, BookWalkerCache, BookWalkerSeriesPageParser, VolumeCoverResolver, VolumeCoverAlignment
- `mangaupdates/` — MangaUpdatesClient, MangaUpdatesDisambiguator, MangaUpdatesStatusParser, VolumeMetadataResolver, JapaneseTitlePicker
- `fandom/` — FandomClient, FandomVolumeResolver, FandomCatalogCache, WikiManifest + Registry, InfoboxExtractor, TableExtractor, FandomTypes
- `wikidata/` — WikidataClient, WikidataCache
- `wikipedia/` — WikipediaParser, WikipediaResolver

**Library / catalog:**
- `ComicsLibraryRecord` — the in-library record shape for a Comics-mode series
- `PremiumCatalog` / `PremiumCatalogSchema` — the curated trusted-uploader catalog (nyaa torrent index)
- `MangaDownloadIndex` — registers downloaded cbzs; feeds the continue-reading strip and library view
- `MangaPosterCache` — poster art cache

**Volume pipeline:**
- `NyaaRuntimeSource` — runtime nyaa search filtered by trusted-uploader tier
- `TorrentVolumeProvider` — resolves "give me Vol N of series X" → torrent + libtorrent file-priority → cbz on disk
- `TorrentRequestLedger` — tracks in-flight torrent volume requests
- `WeebCentralVolumePacker` — HTTP chapter-fetch + zip-on-the-fly fallback
- `PremiumArchiveValidator` — validates downloaded cbzs; enforces `.tankoban-part` + quarantine pattern
- `PremiumCoverExtractor` — extracts cover art from finished cbzs; fired post-finalize by both providers
- `MangaTransferCoordinator` — three-backend fan-out orchestrator

**Misc:**
- `MangaScraper.h` — base interface / signal contract all scrapers implement
- `MangaResult.h` / `MangaSeriesDetail.h` — POD types for search results + series detail
- `MangaSourceRegistry` — runtime registry of available sources
- `ComicsTankoyomiLibrary` — library record store for Tankoyomi-origin series
- `TrustedUploaders` — the trust-tier table (1r0n / Hox / VIZ Digital = tier 1; etc.)
- `CanonicalChapterKey` — stable key for a chapter across scraper + torrent paths
- `FallbackChainResolver` — orchestrates WeebCentral → ReadComics fallback
- `ComicsPrePivotMigrator` — one-shot burn-it-down migration, moves pre-pivot files to `<appData>/comics_pre_pivot_backup/`

## Reference apps

- **Mihon / Tachiyomi** — primary reference for scraper contract shape and chapter-download flow. On-disk reference: see `reference_reader_codebases.md`.
- **AniList** — primary metadata backbone. Not a scraper — a GraphQL metadata service.
- **Stremio (Stream mode)** — the UI BLUEPRINT; the backend architecture of this tree mirrors what Stremio's addon infrastructure does for video.

## Load-bearing memories (read when touching this domain)

- `project_anilist_api_facts.md` — AniList GraphQL: free, 90/min, no auth for read; exposes Media.chapters/volumes counts but NOT per-chapter binding — drives the chapters-per-vol heuristic in AniListVolumeMapper
- `project_tankoyomi_volume_pivot_arc_2026-05-16.md` — TANKOYOMI_VOLUME_PIVOT 13-phase arc spec + plan (awaiting subagent execution); the 12 locked decisions are the architectural contract for this tree
- `feedback_stremio_for_manga_vibe.md` — volume is the only first-class UI unit; chapters are buried implementation detail; WeebCentral is a fallback, never primary
- `project_weebcentral_71pct_downscale_confirmed.md` — WeebCentral confirmed to downscale to ~71% linear / ~50% pixel count of master; motivates the torrent-primary / WC-fallback architecture
- `project_tankoyomi_premium_mvp_brainstorm_prelock.md` — Stremio-for-manga MVP pre-lock decisions; 30-series proposed catalog; superseded in shape by the volume-pivot arc but still the ancestor reasoning
- `project_tankoyomi_continue_reading_shipped.md` — Tankoyomi-downloaded chapters surface in Comics CR strip on read-start; just-in-time map registration pattern in ComicsPage
- `project_empty_cbz_bug_fix.md` — MangaDownloader rejects zero-page scraper responses; 22-byte EOCD-only ZIP is the pre-fix bug signature; Fix A (zero-page guard) + Fix B (1 KB size check) both in MangaDownloader.cpp
- `feedback_brainstorm_batches_of_four.md` — Agent 1's brainstorm pacing preference for new arcs
- `feedback_libtorrent_windows_backslash_separator.md` — libtorrent file_path(i) uses backslash on Windows; split on `[\\/]` not just `/` when parsing torrent file paths. Agent 4 territory but directly relevant when TorrentVolumeProvider reads file paths from libtorrent
- `reference_libtorrent_source.md` — libtorrent RC_2_0 at `C:\tools\libtorrent-source\`; Agent 4 owns this engine but this tree calls into it via TorrentVolumeProvider

## Dev-bridge surface (Agent 1's commands)

Agent 1 owns the `comics-*` tankoctl prefix (v1.2 — 10 commands). These read the manga-source state from a running Tankoban instance without touching the UI:

- `comics-get-state` — snapshot of Comics-mode active state
- `comics-get-library` — all series currently in the Comics library
- `comics-get-series` — detail for a specific series (volumes, download state)
- `comics-get-sources` — active source list for the current series
- `comics-get-downloads` — in-flight and completed download records
- `comics-select-volume` — fires real `populateSourcesForRow` path (real code path, not a sim)
- `comics-dispatch-volume` — triggers a volume download
- `comics-open-series` / `comics-open-chapter` — navigation commands
- `comics-search-tankoyomi` — runs a search through the Tankoyomi/scraper path

Plus cross-mode `library-*` commands (17 cmds, owned by Agent 5) for comics-mode tile state. Full catalog enumerable via `out\tankoctl.exe ping`.

## Build / MCP lane discipline (gov-v7)

When building or MCP-smoking this domain, use the lease registry per Rules 19 + 22. Lane names: `mcp` for desktop interactions, `build` for `build_check.bat` against shared `out/`. Per-lane build dirs (`TANKOBAN_BUILD_LANE=<lane>`) bypass the shared lock when isolating from concurrent brothers.

This tree is pure C++ with no UI — changes here almost always require a main-app build_check to verify. Run `build_check.bat` after any non-trivial edit; the link step dominates (~915s) so batch your changes before verifying.

## When this file activates

Auto-loads when any agent reads a file under `src/core/manga/`. Treat the content as ambient context: who owns this code, what arc is in flight, which memories are load-bearing, which dev-bridge commands belong to this domain.

If you're Agent 1: this is your home turf, redundant reminder.

If you're another brother doing a cross-domain audit / read: this orients you on whose hand you're reading + what taste discipline applies (volume-first always, WeebCentral is a fallback not a feature). Cross-domain edits without Agent 1 sign-off are a Rule 14 violation in this domain.
