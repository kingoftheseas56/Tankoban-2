# BookWalker JP — Per-Volume Cover Source — Design

**Date:** 2026-05-18
**Owner:** Agent 1 (Comic Reader + Tankoyomi domain)
**Status:** Brainstorm complete; awaiting plan via `/superpowers:writing-plans`
**Originating context:** Codex Trigger D #7 abort on MangaUpdates per-volume covers (API surface insufficient). Manual re-probe confirmed MangaUpdates exposes only series-level art. BookWalker JP probed live 2026-05-18 — 30+ per-volume cover URLs extracted cleanly from raw HTML on the Berserk series page, no JS rendering required.

---

## 1. Problem statement

Tankoban's Comics mode renders volume rows in `ComicsSeriesView`. Today every row shares the same series-level cover (from AniList). The COMICS_TANKOYOMI_STREAM_MERGER series-view-port arc shipped 2026-05-18 brought parity with Stream mode's per-episode-thumb visual richness — but the underlying per-volume cover data was missing. We need a source that exposes one cover per tankōbon volume.

Prior sources eliminated:
- **MangaUpdates API/HTML** — verified 2026-05-18 (three-way probe: `/v1/series/{id}`, `/v1/series/{id}/covers`, raw HTML). Single series-level `image` only; no per-volume gallery.
- **MangaDex aggregate API** — eliminated 2026-05-16 by Agent 7 audit. Upload-driven catalog gaps fatal (Kingdom: 11 of 79 volumes; Death Note: 3 of 12). Same failure mode would apply to MangaDex's `/cover` endpoint.

## 2. Solution

Add BookWalker JP as the per-volume cover source for non-Premium series. BookWalker is the official Hakusensha/Kodansha/Shueisha retailer catalog; per-volume cover art is publisher-canonical and CDN-served at `https://rimg.bookwalker.jp/<id>/<token>.jpg`. Live probe found 30+ cover URLs on the Berserk series page in `data-original=` attributes of static HTML — no JS rendering required, no auth, no captcha, no region block.

The chain is gated by AniList (English→Japanese title shim) and anchored by MangaUpdates (canonical volume count for index alignment). Premium-curated series short-circuit the entire chain — `PremiumCoverExtractor` already handles those.

## 3. Locked decisions

Captured via `/superpowers:brainstorming` (2 batches via AskUserQuestion):

| # | Decision | Choice | Rationale |
|---|----------|--------|-----------|
| 1 | Scope | Covers only | Minimal v1; no scope creep into titles/dates/ISBNs |
| 2 | Title lookup | AniList alt-titles cascade | Free, already wired, covers mainstream manga automatically |
| 3 | Volume mapping | Cross-reference MangaUpdates canonical count | Robust against omnibus/deluxe interleavings; uses authoritative anchor |
| 4 | Fallback | Series-level cover (current behavior) | Visually consistent on miss; no new placeholder asset |
| 5 | Premium tier | Premium curated covers always win; skip BookWalker for Premium series | Preserves Premium exclusivity + saves network calls |
| 6 | Refresh policy | 7-day TTL auto-refetch + smart invalidation on MangaUpdates count growth | Steady freshness; freebie on count delta picks up new releases mid-cycle |
| 7 | Fetch UX | Loading overlay; series view appears only when all data ready | Hemanth directive: "I'm sick of this app looking incomplete every time I open the app and waiting to load" — block-render-until-ready, no progressive populate |

## 4. Architecture

### 4.1 New module — `src/core/manga/bookwalker/`

Parallel to existing `anilist/` and `mangaupdates/` subdirectories. Four new files:

- **`BookWalkerClient.{h,cpp}`** — async HTTP client built on `QNetworkAccessManager`. Same shape as `MangaUpdatesClient`. Two public operations:
  - `searchSeries(QString japaneseTitle)` → emits `seriesIdFound(QString seriesId)` or `seriesNotFound()`
  - `fetchSeriesCovers(QString bookwalkerSeriesId)` → emits `coversReady(QList<QPair<int, QString>>)` or `fetchFailed(QString reason)`
- **`BookWalkerTypes.h`** — small data structs (`BookWalkerSearchResult`, `BookWalkerVolumeRecord`)
- **`BookWalkerCache.{h,cpp}`** — disk cache for URL metadata only. Storage path: `<config-dir>/cache/bookwalker_covers/<anilistId>.json` (AniList ID is the identity backbone across `src/core/manga/`; confirmed via grep). Format:
  ```json
  {
    "schemaVersion": 1,
    "fetchedAt": "2026-05-18T22:00:00Z",
    "canonicalCount": 43,
    "bookwalkerSeriesId": "16664",
    "volumes": [{"vol": 1, "url": "https://rimg.bookwalker.jp/0038802/..."}, ...]
  }
  ```
  Read-side checks TTL + count drift; both trigger refetch.
- **`VolumeCoverResolver.{h,cpp}`** — orchestrator coordinating the full fetch chain. Sibling pattern to existing `mangaupdates/VolumeMetadataResolver`. Single public method: `resolveSeriesCovers(SeriesIdentity)` → emits `coversResolved(QMap<int, QString>)` or `resolutionFailed(QString reason)`.

### 4.2 Reused — no new code

- **`AniListClient`** — fetch the series' Japanese alternate title. Already cached via `AniListCache`.
- **`MangaUpdatesClient`** — fetch canonical volume count (already cached in mangaupdates layer).
- **`MangaPosterCache`** — URL → pixmap caching. BookWalker's URL strings hand off to this; image bytes never touch `BookWalkerCache`.
- **`PremiumCoverExtractor`** — short-circuits the entire chain for Premium-catalog series. BookWalker never runs for Premium series.
- **`ComicsSeriesView`** — col-2 cellWidget pattern (QLabel-with-pixmap) shipped 2026-05-18 in Bug 3 round-2. No structural UI changes needed; the pixmap source just points at the resolved per-volume URL instead of the series-level URL.

### 4.3 Loading-overlay decision (resolved in plan)

`LoadingOverlay` exists at `src/ui/player/LoadingOverlay.cpp` but is namespaced to player domain. The plan picks between two options:
- **(a) Reuse with namespace move** — relocate `LoadingOverlay` to a shared `src/ui/widgets/` location and consume from both player and comics domains. Cleaner long-term, touches more files.
- **(b) Lightweight comics-side overlay** — add a small `ComicsSeriesViewLoadingOverlay` purpose-built for this view. Tighter blast radius, mild duplication.

Decision deferred to plan-time after a quick review of how player-side `LoadingOverlay` is wired and whether its API is general enough to reuse cleanly.

## 5. Data flow on series-view-open

User clicks a series tile in `ComicsLibrary`. `ComicsSeriesView::openSeries(SeriesIdentity)` fires immediately:

1. **Loading overlay paints** — series view holds back any volume-row paint.
2. **`VolumeCoverResolver::resolveSeriesCovers(id)` invoked.** Resolver executes:
   1. **Premium short-circuit.** If `PremiumCatalog::hasEntry(id)` returns true, emit Premium-curated covers immediately. Resolver exits. (BookWalker never runs.)
   2. **AniList alt-title fetch.** `AniListClient::fetchAlternateTitles(id)` → `japaneseTitle`. Usually warm-cache from prior series-open elsewhere in app (~ms).
   3. **MangaUpdates count fetch.** `MangaUpdatesClient::fetchVolumeCount(id)` → `canonicalCount`. Usually warm-cache (~ms). Needed both for cache-drift check (next step) and for index alignment (step 7).
   4. **Cache check.** Read `BookWalkerCache::load(anilistId)`. If hit AND `now - fetchedAt < 7 days` AND `cached.canonicalCount == canonicalCount`, emit cached covers. Resolver exits.
   5. **BookWalker search.** `BookWalkerClient::searchSeries(japaneseTitle)` → `bookwalkerSeriesId`.
   6. **BookWalker series-page parse.** `BookWalkerClient::fetchSeriesCovers(bookwalkerSeriesId)` → ordered list of cover URLs (extracted from `data-original` attributes in static HTML).
   7. **Index alignment.** Take the first `canonicalCount` URLs, drop overflow (handles omnibus/deluxe/special editions appearing after regular tankōbon). Build `{volume: coverUrl}` map.
   8. **Cache write.** Persist to `BookWalkerCache` with `fetchedAt = now`, `canonicalCount`, `bookwalkerSeriesId`.
   9. **Emit `coversResolved(volumeToCoverUrl)`.**
3. **On resolver success** — `ComicsSeriesView` hides loading overlay, paints volume rows with per-volume covers. Any unmapped row (partial coverage scenario) uses series-level fallback.
4. **On resolver failure** — `ComicsSeriesView` hides loading overlay, paints all rows with series-level fallback (current behavior).

Loading overlay hard-capped at 10 seconds (matches HTTP timeout). After 10s without resolver response, force-emit `resolutionFailed("timeout")`. User never sees a stuck overlay.

Cold-cache cost: 1-3 seconds typical. Warm-cache cost: instant (single disk read).

## 6. Error handling & fallback

Every link in the fetch chain has a defined failure path. No failure hangs the UI.

| Failure | Resolver behavior | UI result |
|---------|-------------------|-----------|
| AniList unreachable BUT fresh BookWalker cache exists | Skip drift check, serve cached covers, emit success | Covers paint from cache. Stale-tolerant degradation. |
| AniList unreachable AND no cache | `resolutionFailed("no japanese title")` | Series-level fallback |
| MangaUpdates count unreachable BUT fresh BookWalker cache exists | Skip drift check, serve cached covers, emit success | Same stale-tolerant degradation. |
| MangaUpdates count unreachable AND no cache | Proceed with BookWalker raw count as anchor (no overflow drop) | Covers render; may include omnibus rows mixed in |
| BookWalker: search returns 0 results | `resolutionFailed("series not on bookwalker")` | Series-level fallback. Common for niche/pre-release manga. |
| BookWalker: page parse returns 0 `data-original` | `resolutionFailed("parse failed")` + warning log | Series-level fallback. Warning log signals possible BookWalker layout change. |
| Network timeout (any step) | `resolutionFailed("timeout")`; cache NOT written | Series-level fallback. Next open retries. |
| Partial cover set (e.g. 30 of 43) | Emit partial map | Mapped rows use BookWalker cover; unmapped rows use series-level. Honest mixed-state render. |
| Disk write fail on cache | Log warning, emit success anyway | User gets covers this session; cache just won't speed up next open. |

## 7. Caching semantics

`BookWalkerCache` stores URL metadata only (no image bytes). One JSON file per series at `<config-dir>/cache/bookwalker_covers/<seriesIdentity>.json`.

Two invalidation triggers, both checked on cache-read:
1. **TTL.** `now - fetchedAt > 7 days` → treat as miss.
2. **Count drift.** `cached.canonicalCount != currentMangaUpdatesCount` → treat as miss. (Since the MangaUpdates count fetch happens anyway for index alignment, this check costs zero extra network.)

Pixmap caching is owned by existing `MangaPosterCache`. Its TTL and storage are unchanged; BookWalker hands it URLs, it handles the rest.

## 8. Verification

### 8.1 Smoke matrix

Canonical quartet (matches Agent 7 2026-05-16 audit's standard probe set):

| Series | Status | Vols | Probe purpose |
|--------|--------|------|---------------|
| Death Note | Complete | 12 | Baseline; small completed series |
| One Piece | Ongoing | 114+ | Large catalog stress test; count alignment |
| Berserk | Ongoing | 43 | Standard test case; known-good BookWalker entry (verified 2026-05-18) |
| Kingdom | Ongoing | 79 | Historically gap-prone (MangaDex failed here); BookWalker JP coverage probe |

Per-series checks:
1. Open series → loading overlay paints, dismisses within 5s
2. `out/tankoctl.exe comics-get-series` confirms `volumeRows[i].coverUrl` matches BookWalker CDN pattern (`https://rimg.bookwalker.jp/...`)
3. Re-open within 7 days → instant render, no network call (verified via ring-buffer logs)
4. Force cache stale (delete the cache JSON file) → re-open → resolver refetches + recaches
5. Niche-series fallback: open a manga AniList knows but BookWalker doesn't index. Series-level covers paint without hang.
6. Premium-series probe: open a Premium-catalog series. Verify via logs that no BookWalker HTTP call fired.

### 8.2 Unit-level primitives

Opt-in via `-DTANKOBAN_BUILD_TESTS=ON` per Codex Stage 3a precedent:

- **`BookWalkerSeriesPageParser`** — pure function extracting ordered `data-original` URL list from raw HTML. Test fixtures: frozen Berserk + One Piece HTML samples committed to `tests/fixtures/bookwalker/`.
- **`VolumeCoverAlignment`** — pure function aligning N BookWalker URLs to M MangaUpdates canonical count, returning `QMap<int, QString>` with overflow dropped. Test cases: exact match (N == M), overflow (N > M, e.g. 60 raw vs 43 canonical), shortfall (N < M, e.g. 30 raw vs 43 canonical).

## 9. Out of scope (explicitly)

These were considered and deferred:
- **Volume titles, release dates, ISBNs, publisher** — covers-only by Decision #1.
- **Fandom MediaWiki source** — strictly stronger ceiling (covers + descriptions + arc/season binding) but high per-wiki maintenance. Deferred to v1.x or later for the eventual season-system in Comics mode. Captured as an Open Question.
- **Manual cover override UI** — no per-volume cover picker for users in v1.
- **High-resolution variant fetching** — current `rimg.bookwalker.jp` token returns moderate resolution. Higher-res URL patterns (if any) deferred to v1.x quality polish.

## 10. Open questions (post-v1)

- **Fandom MediaWiki integration** — when do we layer this on for per-volume descriptions and arc-affiliation metadata? This is the path to the eventual season-system in Comics mode mirroring Theatre's per-episode richness.
- **Premium catalog growth strategy** — as the Premium catalog expands, when does BookWalker effectively become the long-tail catalog vs the primary?
- **BookWalker resolution probe** — verify whether higher-resolution variants are accessible via URL-token manipulation. Premium-quality scans would matter for the eventual Tankoyomi-Premium pitch.

## 11. References

- **Live probe evidence** — chat.md 2026-05-18 21:30+ (BookWalker series page parse session; Hemanth + Agent 1).
- **Source audit** — `agents/audits/manga_volume_metadata_sources_2026-05-16.md` (Agent 7 Trigger C, 2026-05-16). Confirms BookWalker JP as "Reachable scrape target; official retailer catalog. Full official Japanese volumes visible via search/series pages."
- **Codex abort RTC** — chat.md 2026-05-18, Codex Trigger D #7 abort finding (MangaUpdates per-volume cover endpoint absent). Validated by Agent 1 re-probe same wake.
- **MangaUpdates failure verification** — Agent 1 live probe 2026-05-18 (three surfaces: `/v1/series/{id}`, `/v1/series/{id}/covers`, raw HTML grep). Confirms series-level `image` only.
- **MangaDex elimination** — `project_tankoyomi_premium_mvp_brainstorm_prelock.md` memory + Agent 7 audit Coverage Matrix row 4 (upload-driven gaps fatal).
- **ComicsSeriesView cellWidget precedent** — `src/ui/pages/comics/ComicsSeriesView.cpp` lines ~660 (`populateVolumeRows()`) — col-2 QLabel pixmap pattern shipped in Bug 3 round-2 (2026-05-18). No structural change needed for BookWalker consumption.
- **tankoctl v1.2 dev-bridge** — `out/tankoctl.exe comics-get-series` ships verification surface for the smoke matrix (Codex Trigger D #8, 2026-05-18).
