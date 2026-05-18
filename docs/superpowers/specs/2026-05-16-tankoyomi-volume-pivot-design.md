# Tankoyomi Volume Pivot — Design

**Date**: 2026-05-16
**Owner**: Agent 1 (Comic Reader + Tankoyomi)
**Status**: Brainstormed and approved (verbal). Awaiting spec-file review before plan-time.
**Predecessor arc**: TANKOYOMI_PREMIUM_MVP (11 phases shipped 2026-05-15; UI half pivots, plumbing survives).
**Predecessor brainstorm**: docs/superpowers/specs/2026-05-15-tankoyomi-premium-brainstorm.md
**Architecture path**: A (fork StreamDetailView; v2 generalizes).

---

## 1. Vision

Stremio for manga.

The user opens Comics, types a series name, clicks the result, sees a Theatre-shape series page: banner + volume list + Sources panel on the right. Clicks a volume row, the Sources panel populates with ranked options (trusted nyaa scan packs first, WeebCentral chapter-pack fallback last). Clicks a source row, the volume downloads. Opens the volume, reads page-by-page. Never sees a chapter.

Volume is the only first-class UI unit. Chapters are implementation detail buried beneath the volume layer — they exist in AniList metadata for vol-mapping and in WeebCentral fetches for the synthesized-volume fallback path, but the user never sees the word "chapter" anywhere in the app.

The internal naming joke (Tankoban = the Japanese word for the bound-volume manga format) becomes the entire product identity.

---

## 2. Locked Decisions

12 decisions ratified across 3 AskUserQuestion batches in the 2026-05-16 brainstorm session.

1. **Sources panel = Stremio-style ranked list** on the right side of the series view, populated on volume-row click. Matches the Daredevil Theatre detail view layout exactly.

2. **Volume X = un-bound chapters past the last AniList-bound volume.** Self-cleaning: when AniList confirms a new bound volume, those chapters move out of Vol X into the new bound vol. Vol X exists ONLY for "ongoing" status series.

3. **WeebCentral fallback = HTTP-fetch + zip-on-the-fly.** For a vol whose source is WeebCentral, AniList's vol-to-chapter mapping drives a sequential fetch of each chapter's images via the existing MangaDownloader HTTP path; results are zipped into a single cbz on disk. No torrent involved on this path. The cbz is finalized through the same Phase 4 .tankoban-part + validation + rename lifecycle as torrent downloads.

4. **Read-state = strictly volume-keyed.** Continue strip shows `<Series> — Vol N — page X/Y`. No chapter mentioned in the UI anywhere. Internal canonical-chapter-key (Phase 5) stays for cross-source bookkeeping but never surfaces.

5. **Catalog = hybrid (curated for precision, runtime nyaa for the rest).** Per-series JSON catalog ships only when we have exact piece-range precision (Phase 11 Death Note shape). For uncurated series the app queries nyaa at vol-click time with uploader-trust filter, ranks by seeders within the trust tier.

6. **Search results = single ranked list, AniList-primary.** No PREMIUM/MANGA/COMICS section split. One list, AniList metadata-driven.

7. **Search backbone = AniList only.** WeebCentral never appears as a user-facing search result. It remains a backend fetch source for synthesized volumes.

8. **Existing library = burn-it-down on first launch.** Pre-pivot chapter-folder records moved to `<appData>/comics_pre_pivot_backup/` (recoverable for 30 days), MangaDownloadIndex wiped, Tankoyomi library wiped. Manga posters thumb cache preserved so re-downloads do not re-fetch covers.

9. **Library model = downloads + optional bookmarks.** Library is your downloaded volumes by default (Stremio-flavor). "Add to library" stays as a save-for-later bookmark for users who want a series to appear on Comics landing before they download anything. Inside a series view: all volumes are listed (downloaded + un-downloaded), exactly like Theatre's episode list.

10. **Source ranking = trust tier first, then seeders.** Tier-1 uploaders (`1r0n`, `Hox`, `VIZ Digital`) rank above any other nyaa uploader. Within a tier, seeder count breaks ties. WeebCentral chapter-pack always sits below all torrent sources but above "no source available."

11. **Volume cover art = AniList per-volume when available, series cover fallback, Phase 10 extracted cover post-download.** Three-stage cover resolution: AniList exposes per-volume art for many series via the `volumes` field on `Media`. When that exists, use it. When not, use the series cover. Post-download, Phase 10's cover extractor replaces it with the actual first-page from the cbz.

12. **AniList = cache on first fetch, refresh on series open.** First search hits the live GraphQL endpoint. Series metadata + volume mapping cached locally at `<appData>/anilist_cache/<series_id>.json`. Background re-fetch on detail view open to pick up newly bound volumes. Cached data is the offline source of truth for bookmarked series.

---

## 3. Architecture (Path A)

Fork StreamDetailView into ComicsSeriesView. Maintain two parallel files in v1. Generalize into a shared `ShowStyleDetailView` widget in v2 once both views are mature and we know which sections truly need to be shared.

Rationale: Theatre is stable and Hemanth uses it daily. A refactor that pulls Theatre into scope has real regression risk. Forking gives us a known-good starting layout for Comics without touching Theatre's code.

Three-layer breakdown under the unified Comics surface:

- **Search/landing layer**: AniList-primary single-list search; library landing shows downloaded series + bookmarked series tiles.
- **Series-view layer**: `ComicsSeriesView` (forked from StreamDetailView). Banner + meta + synopsis on the left, volume list below, Sources panel on the right.
- **Source layer**: three providers feed the Sources panel — CatalogSource (precision), NyaaRuntimeSource (runtime), WeebCentralVolumePacker (fallback).

AniList sits above all three as the metadata backbone. It tells us how chapters group into volumes, which is what makes "click Vol 50" resolve to "file index 49 in 1r0n pack" or "chapters 47-52 from WeebCentral" depending on which source the user picks.

The Phase 1-5 + 9 (facade only) + 10 plumbing all stays. Phase 6 (detail view), Phase 7 (filter chips), Phase 8 (search Premium section), Phase 9 (adopt-folder, moot post-burn) UI work gets ripped out and rebuilt.

---

## 4. Components

### New code (~7 logical units)

1. **AniListClient** (`src/core/manga/anilist/AniListClient.{h,cpp}`)
   - GraphQL HTTP client via QNetworkAccessManager.
   - Two query shapes: `searchByTitle(query) -> List<MediaPreview>` and `seriesById(id) -> MediaDetail` (full metadata + volume + chapter list).
   - Public endpoint `graphql.anilist.co`, unauthenticated, 90 req/min rate limit, internally throttled.

2. **AniListCache** (`src/core/manga/anilist/AniListCache.{h,cpp}`)
   - JSON file-backed local cache at `<appData>/anilist_cache/`.
   - Refresh-on-open semantics: detail view fires a background re-fetch when opened; cached data renders immediately, fresh data replaces it on arrival.
   - Bookmarked series are flagged in the cache index so a cache-eviction pass never drops them.

3. **AniListVolumeMapper** (`src/core/manga/anilist/AniListVolumeMapper.{h,cpp}`)
   - Pure-function-style helper: given a series's AniList chapter list + volume bindings, produces an ordered list of `VolumeRow` structs (vol number, chapter range, cover art URL or null, status: bound/unbound).
   - Vol X synthesis: when series is ongoing AND there are chapters past the last AniList-bound vol, append a `VolumeRow{vol=X, chapterRange=[lastBound+1..latest]}` at the end.
   - Pure logic, easy to unit-test (no I/O, no Qt UI).

4. **WeebCentralVolumePacker** (`src/core/manga/WeebCentralVolumePacker.{h,cpp}`)
   - Sibling to TorrentVolumeProvider on the source layer.
   - `requestVolume(seriesId, volumeNumber, chapterList, destinationPath)`: HTTP-fetches each chapter's images via existing MangaDownloader path (or its image-fetch primitives), accumulates them in a staging dir, zips into one cbz at the destination, fires the same `volumeCompleted` signal shape so downstream code (MangaDownloadIndex.registerVolume, Phase 10 cover extractor) does not care which source completed.
   - Emits `volumeProgress` (fraction across all chapters in the vol) + `volumeFailed` (single chapter fail aborts the whole vol with retry option).

5. **NyaaRuntimeSource** (`src/core/manga/NyaaRuntimeSource.{h,cpp}`)
   - Queries nyaa.si at vol-click time when no catalog entry exists.
   - Uploader-trust filter (1r0n, Hox, VIZ Digital). Configurable trust-tier JSON at `resources/manga_uploader_trust.json`.
   - Returns ranked candidates. Trust tier is the primary sort key, seeder count breaks ties.
   - Surfaces candidates that point at the same torrent as a catalog entry (deduplication safety).

6. **ComicsSeriesView** (`src/ui/pages/comics/ComicsSeriesView.{h,cpp}`)
   - Forked from `src/ui/pages/stream/StreamDetailView.{h,cpp}`.
   - Banner at top (AniList banner art or series cover full-width).
   - Title + meta line + synopsis + genres below the banner (same as Theatre).
   - Volume list (replaces episode list): #, cover, title (`Volume N` or `Volume X`), chapter-range subtitle, progress, status, download icon.
   - Right-side Sources panel: empty state "Select a volume to see sources" pre-click; populated post-click.
   - Add-to-library / Remove-from-library button in the top-right (preserves bookmark model).

7. **ComicsSourcesPanel** (`src/ui/pages/comics/ComicsSourcesPanel.{h,cpp}`)
   - Right-side widget inside ComicsSeriesView.
   - Header line: `Sources for Volume N`.
   - List of source rows: each row shows tier-badge + seeders (or "HTTP" for WeebCentral) + size + uploader + Download button.
   - Click Download → fires the appropriate provider's `requestVolume` call.

### Surviving from 11-phase (no change or minimal change)

- **PremiumCatalog + PremiumCatalogSchema** (Phase 1) — still drives precision sources. Schema may extend with `anilistId` field for AniList cross-reference (already present in schema, fill-in now).
- **premium_catalog_helper** Python tool (Phase 2) — still useful for curating tier-1 nyaa packs.
- **TorrentVolumeProvider** (Phase 3) — core download engine for catalog + nyaa-runtime sources. No signature changes expected.
- **PremiumArchiveValidator + .tankoban-part + quarantine** (Phase 4) — finalization lifecycle, applies to both torrent + WeebCentral-packed cbzs.
- **MangaDownloadIndex.registerVolume + canonical chapter keys** (Phase 5) — still tracks which vols are downloaded; canonical chapter keys still resolve cross-source progress.
- **PremiumCoverExtractor** (Phase 10) — post-download cover replacement still fires; works the same for both source paths.
- **MangaTransferCoordinator** (Phase 9 facade) — extended to fan out pause/resume to WeebCentralVolumePacker as a third backend (alongside MangaDownloader + TorrentVolumeProvider).

### Deleted

- `ComicsTankoyomiDetailView.{h,cpp}` (Phase 6 + 7 work) — replaced by ComicsSeriesView.
- `ComicsTankoyomiSearchWidget` Premium-section + chip logic (Phase 8) — replaced by single AniList-primary list.
- ComicsPage adopt-existing-folder helper + adopt callback wiring (Phase 9 UI portion) — moot post-burn-it-down.
- Filter chip row (All/Downloaded/Unread/Premium/Loose) — Theatre's series view doesn't have one; for parity we drop it. (Status per row + a search-within-the-volume-list affordance covers the same need at much lower complexity.)

---

## 5. Data Flow

A single happy-path scenario: user opens app, searches "one piece", downloads vol 50.

1. **App launch**: first-time post-pivot launch detects existing chapter-folder records, executes burn-it-down sweep (move to `<appData>/comics_pre_pivot_backup/`, wipe MangaDownloadIndex, wipe Tankoyomi library, preserve manga_posters thumb cache).

2. **User types "one piece"**: ComicsSearchWidget fires AniListClient.searchByTitle("one piece") → results land as a single ranked list of MediaPreview tiles (series cover + title + format + year).

3. **User clicks One Piece tile**: ComicsSeriesView opens. AniListCache.get("one_piece_anilistId") returns cached MediaDetail if present, else AniListClient.seriesById fires fresh. Background re-fetch always fires on open to catch newly bound vols. Banner + meta + synopsis paint immediately from cache. AniListVolumeMapper builds the volume list: vols 1..111 (bound) + Vol X (chapters 1112-1146 say). Volume rows render with their AniList per-vol cover art (or series cover fallback) + MangaDownloadIndex.filePathForVolume lookup to mark each row Downloaded / In progress / Not downloaded.

4. **User clicks Vol 50 row**: ComicsSourcesPanel.populate(seriesId, volNumber=50) fires. Three queries run in parallel:
   - PremiumCatalog.entryById(seriesId) → if catalog hit, get volume entry with file-index + piece-range. Insert at top of source list with tier-1 badge.
   - NyaaRuntimeSource.search(seriesTitle, volNumber=50) → nyaa query with uploader-trust filter. Returns ranked candidates. Append after catalog hits.
   - WeebCentralVolumePacker.canSynthesize(seriesId, volNumber=50, chapterList=[47, 48, 49, 50, 51, 52]) → returns true if WeebCentral has all required chapters. Append at bottom of source list.
   - Each source row renders: tier badge, uploader, size (catalog: exact; nyaa: from torrent; WeebCentral: estimated from chapter count × avg chapter size), seeders (catalog: live nyaa query; nyaa: same; WeebCentral: "HTTP"), Download button.

5. **User clicks Download on the 1r0n source row**: TorrentVolumeProvider.requestVolume(catalogEntry, volumeEntry, destinationPath) fires. Phase 3 + 4 lifecycle: addMagnet paused=true → metadataReady → expectedInfoHash check → setFilePriorities → startTorrent → pieceFinished events accumulate covered-byte tally → file complete → finalize lifecycle (.tankoban-part copy → archive validation → atomic rename to `<rootFolder>/<series>/Vol 50.cbz` → registerVolume in MangaDownloadIndex → emit volumeCompleted → kick off PremiumCoverExtractor). Row's Status flips to Downloaded. Cover thumb replaces the AniList pre-download art.

6. **User clicks the now-Downloaded Vol 50 row**: ComicsSeriesView emits openVolume(cbzPath). ComicReader opens, page-keyed. User reads. Progress saved as `<series>:vol50 -> page N`. Continue strip on Comics landing surfaces `One Piece — Vol 50 — page N/M`.

Alt scenario (WeebCentral path, e.g., Vol 50 has no catalog entry and no nyaa seeders):
- Step 5 alt: User clicks Download on the WeebCentral source row → WeebCentralVolumePacker.requestVolume(seriesId, 50, [47..52], destinationPath) fires → sequential HTTP image fetch per chapter → accumulate in staging dir → zip into one cbz → run through Phase 4 finalize lifecycle (.tankoban-part rename → archive validation → atomic rename) → registerVolume → volumeCompleted → cover extractor → row flips Downloaded.

---

## 6. Error Handling

- **AniList offline**: cache provides metadata for bookmarked series + recently-opened series; search returns "Offline — only cached series available" with a list of currently-cached series tiles.
- **nyaa returns zero seeders for a catalog hit**: source row shows "0 seeders, fallback to WeebCentral?" prompt OR auto-falls-through to WeebCentral with a toast `Premium scan unavailable, falling back to WeebCentral synthesis`.
- **WeebCentral chapter fetch fails mid-volume**: partial chapters in staging dir cleaned up, vol marked Failed, row shows Retry button. Retry re-fetches all chapters (no partial-resume in v1; chapter HTTP fetches are cheap enough to retry from scratch).
- **AniList re-fetch surfaces a newly bound volume**: Vol X chapters that now belong to Vol N+1 automatically migrate on series-view re-open (re-fetch runs in background; UI re-renders volume list when fresh data arrives). Any in-progress read state on a chapter that moved volumes resolves via canonical-chapter-key lookup so the user does not lose progress.
- **Stale catalog infohash vs nyaa reupload**: PremiumCatalog.entryById still returns the entry, but nyaa.si query for that infohash returns zero seeders. Source-rank logic drops the catalog hit to the bottom (below live-nyaa hits) automatically.
- **User deletes a downloaded vol externally** (Explorer, command line, etc.): MangaDownloadIndex.validateAll catches missing files on next scan or app launch. Row Status flips back to Not downloaded.
- **Existing chapter-folder library on first post-pivot launch**: burn-it-down sweep is destructive at the index layer (MangaDownloadIndex wiped, Tankoyomi library JSON wiped) but non-destructive on disk (folders move to `<appData>/comics_pre_pivot_backup/`, 30-day recoverable; manga_posters/ cache preserved). User sees a one-time banner: `Welcome to the new Comics layout. Your old library has been backed up.` (link → opens the backup folder in Explorer).
- **AniList GraphQL rate-limit hit**: client implements exponential-backoff retry. 90 req/min internal throttle prevents this in practice (one search burst + a few series opens is well under the limit).

---

## 7. Testing

7-case smoke matrix for v1 close-out:

1. **Search renders single list**: type "death note" → AniList returns Death Note tile in a single ranked list (no section split).
2. **Series view opens with vol list**: click Death Note tile → ComicsSeriesView opens → 12 volume rows render with per-vol art from AniList.
3. **Vol click populates Sources panel**: click Vol 1 row → Sources panel populates with catalog hit (1r0n KG Manga from Phase 11 curated entry) + WeebCentral fallback.
4. **Catalog source download completes**: click the 1r0n source row → real torrent download → Phase 4 finalize → row flips Downloaded → cover thumb replaces pre-download art.
5. **WeebCentral fallback download completes**: pick a non-catalog series (or a vol whose catalog seeders are zero) → click WeebCentral source row → HTTP chapter fetch + zip → vol cbz lands at canonical path → row flips Downloaded.
6. **Bookmark + AniList offline survives**: bookmark a series via the Add-to-library button → close app → disconnect network → reopen app → bookmarked series tile still openable from AniList cache; series view renders without crashing.
7. **Vol X auto-shrink on AniList refresh**: open One Piece series view (Vol X exists) → manually edit the cached AniList JSON to add a new bound vol covering some Vol X chapters → re-open series view → Vol X smaller, new bound vol appended.

Deferred to a v1.x integration smoke arc: visual fidelity A/B (catalog vs WeebCentral cbz on same vol), crash-resume during WeebCentral packer mid-fetch, sources-panel ranking ordering with multiple nyaa uploaders, mass-bookmark performance with 100+ bookmarked series.

---

## 8. Burn-it-down spec

Pre-pivot artifacts to handle on first post-pivot app launch:

- `comics_library.json` (Tankoyomi library records) → move to `<appData>/comics_pre_pivot_backup/comics_library.json`.
- `manga_downloads_index.json` (Phase 5 index) → move to `<appData>/comics_pre_pivot_backup/manga_downloads_index.json`.
- Per-series Comics folders containing chapter-cbzs → leave on disk; LibraryScanner just won't see them anymore (no library record points at them post-wipe). Manga posters at `manga_posters/` preserved.
- Tankoyomi sidecar files (`.tankoyomi-meta.json` per series folder) → leave on disk; harmless after the wipe.

A startup migration class (`ComicsPrePivotMigrator`) executes the move on first detection of the pre-pivot files. Idempotent: if the backup dir already exists, no-op. One-time banner UI rendered next launch: `Welcome to the new Comics. Your previous library has been backed up.` with a "Show backup folder" button.

30-day cleanup is a manual-only operation in v1. The backup folder is the user's escape hatch — they can move files back into the active Comics root if they want them back. v1.x adds a "Restore backup" affordance to the Settings page.

---

## 9. Forward-compatibility hooks

- **v2: generalize StreamDetailView + ComicsSeriesView into shared `ShowStyleDetailView`** — refactor extract once both views are mature and the truly-shared sections are obvious.
- **v2: more uploader trust tiers** — community-curated uploader allowlist with signing.
- **v2: Restore backup affordance** in Settings page.
- **v2: chapter-level resume within a volume** — if users complain that page-keyed resume in a 300-page volume is too coarse, the canonical chapter key (Phase 5) is already there to resolve to a chapter boundary if a "Resume at last chapter" toggle is added.
- **v2: WeebCentral packer chapter-level partial resume** — for failed mid-volume fetches.
- **v2: Hot-reload catalog** — current catalog requires app restart.
- **v3: AniList write-back tracking** — if user wants Anilist tracking integration, OAuth + write endpoints get wired.

---

## 10. Plan handoff hooks

The implementation plan (written by /superpowers:writing-plans next) should produce ~12-15 phases in this order:

1. **AniListClient** + **AniListCache** — pure backend, no UI; testable in isolation.
2. **AniListVolumeMapper** — pure function helper; unit-testable; Vol X synthesis logic lives here.
3. **NyaaRuntimeSource** — runtime nyaa query + uploader-trust JSON config.
4. **WeebCentralVolumePacker** — HTTP fetch + zip + finalize integration.
5. **ComicsPrePivotMigrator** — burn-it-down logic; runs once on first post-pivot launch.
6. **ComicsSeriesView fork** — fork StreamDetailView; render banner + meta + volume list; Sources panel empty-state.
7. **ComicsSourcesPanel** — populate-on-click; ranked rows; Download wire-up to the three providers.
8. **Comics search refactor** — single AniList-primary list, kill the three-section split, route clicks to ComicsSeriesView.
9. **Comics landing refactor** — library tile rendering (downloaded vols + bookmarked series); bookmark store.
10. **MangaTransferCoordinator extension** — add WeebCentralVolumePacker to the pause/resume fan-out.
11. **Cover extractor integration** — post-download replacement; pre-download AniList per-vol art usage in ComicsSeriesView.
12. **Reader integration** — confirm ComicReader handles the new vol cbzs cleanly; page-keyed Continue strip update.
13. **Smoke matrix** — 7 cases per §7. Arc-close RTC.

Each phase ends with a smoke + RTC. Phase ordering is mostly bottom-up (backend first, UI last) so the UI phase finds working plumbing underneath.

---

## 11. Spec self-review

Inline self-review per the brainstorming skill checklist:

- **Placeholder scan**: no "TBD" or "TODO" tokens in actionable sections. The forward-compat section has "v2: ..." items that are intentionally deferred, not placeholders.
- **Internal consistency**: §1 vision aligns with §2 decisions aligns with §3 architecture aligns with §4 components. No contradictions found.
- **Scope check**: this design is one coherent pivot of one subsystem. The plan-time decomposition into ~13 phases is the right granularity. Each phase is independently buildable; no phase requires content from a later phase.
- **Ambiguity check**:
  - "Volume X" cutoff resolves to "chapters strictly after the last AniList-bound volume's last chapter" — made explicit in §2 decision 2 and §4 component 3.
  - "Tier-1 uploaders" enumerated explicitly in §2 decision 10: 1r0n, Hox, VIZ Digital. Resources file at `resources/manga_uploader_trust.json` is the canonical source.
  - "Burn-it-down" semantics distinguished: destructive at index/library JSON layer, non-destructive on disk (30-day backup). Section 8 spells this out.
  - Bookmarks vs downloads relationship: §2 decision 9 + §4 ComicsPage refactor define the model — library = (downloaded vols) UNION (bookmarked series), tile click always opens ComicsSeriesView regardless of which list contributed it.

Spec is ready for user review.

---

## 12. Lineage

- Brainstorm-md: `docs/superpowers/specs/2026-05-15-tankoyomi-premium-brainstorm.md` (predecessor MVP; Codex sections 17-27 co-authored per gov-v4 Rule 20).
- Predecessor plan: `docs/superpowers/plans/2026-05-15-tankoyomi-premium.md`.
- Predecessor execution: 11 phases shipped 2026-05-15 across one session; post-arc MCP smoke 2026-05-15 ~11:18pm surfaced the wrong-UI-shape feedback that prompts this pivot.
- Hemanth's vision message captured verbatim in agents/chat.md (post-MCP-lock-release thread).
- This design supersedes Phase 6 / Phase 7 / Phase 8 / Phase 9-adopt-folder of the predecessor arc. All other phases are preserved and consumed by the new architecture.
