# WeebCentral Identity Pivot — Design

**Date:** 2026-05-19
**Owner:** Agent 1 (Comic Reader + Tankoyomi domain)
**Status:** Brainstorm complete; awaiting plan via `/superpowers:writing-plans`
**Originating context:** AniList 403 diagnosis (2026-05-19 morning, systematic-debugging) revealed AniList's 90-req/min unauthenticated cap had tripped overnight after the BookWalker smoke session. Hemanth's response: *"AniList is here for metadata, the entries still have to exist from WeebCentral. WeebCentral is the baseline for search, and everything else is built on that... we will find other solutions than an API that burns out after 5 tries."* This spec turns that directive into a concrete identity-backbone pivot.

---

## 1. Problem statement

Today's Comics-mode search bar routes through `AniListClient::searchByTitle` against `https://graphql.anilist.co`. Two compounding problems with this:

1. **AniList is brittle infrastructure on the critical path.** Free 90-req/min unauthenticated cap. A normal afternoon of smoke testing exceeded the limit yesterday; the in-app search remained broken for ~12 hours until Cloudflare's WAF cooldown cleared. The user-visible error was `Search failed: Error transferring https://graphql.anilist.co - server replied with status code 403`. Nothing in the app could be searched.

2. **Search results don't match what's readable.** AniList's catalog includes light novels, art books, omnibus editions, and obscure spin-offs that Tankoban has no way to actually open. Even when AniList works, a search for "One Piece" returned 25 results including "Wan Piece", "One Piece Party", "One Piece: Ace's Story" (a novel), and assorted one-shots. Tankoban is a manga reader — the user wants series whose chapters they can read in the app.

WeebCentral hosts the actual chapter scans. WeebCentral is the canonical source of what's READABLE in Tankoban. AniList is best understood as a decoration layer (English title formatting, banner art, descriptions, year/status) — useful when available, fatal if treated as load-bearing.

The deeper claim: **the identity backbone of Tankoyomi should be WeebCentral, not AniList.** Today it is the inverse, and that inversion creates the brittleness and the catalog-mismatch we observed.

## 2. Solution

Pivot the identity backbone from AniList to WeebCentral.

- **Search bar** routes through the existing `WeebCentralScraper::search` (already wired into `MangaSourceRegistry`, currently unused for the in-app search). Results display with WeebCentral title + WeebCentral cover. No AniList query fires from search.
- **Library entries** key on `weebcentralId` (the ULID-shaped identifier WeebCentral assigns, e.g. `01J76XYAVE3FZ3YMHMTKEZGXM4` for Berserk). `anilistId` is stored alongside when available, but the library is no longer AniList-required.
- **MangaUpdates becomes the load-bearing metadata source.** Its `associated` field carries Japanese alt-titles (verified 2026-05-16 audit: Berserk → `ベルセルク`, plus 9 other-language variants). MangaUpdates is the silent hero — robust API, no aggressive rate-limit issues experienced.
- **BookWalker resolver re-keyed.** Its Japanese-title input (currently `AniListCache::japaneseTitleFor`) is swapped for a MangaUpdates `associated`-field accessor. BookWalker arc continues to work; AniList is no longer on its critical path.
- **AniList demoted to pure decoration.** Optional banner / description / English-formatted-title lookup on the series-detail page only. If 403'd, the page still renders with WeebCentral data. Search bar never touches AniList.

The net effect: WeebCentral is the only hard dependency for "can the user search and read." MangaUpdates is the only soft dependency for "does the user get rich per-volume covers." AniList is purely cosmetic — its outages don't affect functionality.

## 3. Locked decisions

Captured via `/superpowers:brainstorming` (1 AskUserQuestion batch, 4 questions, Hemanth answers + 1 philosophical push):

| # | Decision | Choice | Rationale |
|---|----------|--------|-----------|
| 1 | AniList in search results | NEVER — search uses WeebCentral fields only | Zero rate-limit risk; fastest search; matches "WeebCentral is baseline" philosophy |
| 2 | WeebCentral-only series (no AniList match) | Show with WeebCentral title + cover, no badge | Treat them as first-class results; this is a reader app, what's readable should appear |
| 3 | Library identity | Hybrid — weebcentralId primary, anilistId alongside if exists | Preserves all existing AniList-keyed features (cover resolver, sidecar metadata) for series with AniList match; gracefully handles WeebCentral-only |
| 4 | BookWalker resolver Japanese-title source | MangaUpdates `associated` field (NOT AniList) | Hemanth philosophical push: "AniList is a fancy database that burns out after 5 tries." Empirical verification (probe 2026-05-19): WeebCentral series page has zero Japanese characters; MangaUpdates `associated` field has the katakana we need |
| 5 | AniList client retention | Kept but demoted to optional decoration | Lazy lookup on series-detail-open for banner + description + English title; never on critical path; failures are silent |
| 6 | Library migration for existing anilistId-keyed entries | Lazy backfill on first series-view-open | New entries get weebcentralId at search-time; existing entries (Death Note today) get a one-time WeebCentral lookup-by-title on next open and weebcentralId is stored thereafter |

## 4. Architecture

### 4.1 Reused (no new code)

- **`WeebCentralScraper`** at `src/core/manga/WeebCentralScraper.{h,cpp}` — already has `search(query, limit=60)`, `fetchChapters(seriesId)`, `fetchPages(chapterId)`, `fetchDetail(MangaResult)`. Wired into `MangaSourceRegistry.cpp:9`. Verified working (2026-05-19 probe: 200 OK, 17 hits, real cover URLs for "one piece").
- **`MangaSourceRegistry`** — already manages the list of scrapers; `WeebCentralScraper` is the only entry today.
- **`MangaUpdatesClient` + `VolumeMetadataResolver`** at `src/core/manga/mangaupdates/` — already fetches MangaUpdates series-by-id and emits volume count. Needs minor extension (see §4.2).
- **`BookWalkerClient` / `BookWalkerCache` / `BookWalkerSeriesPageParser` / `VolumeCoverAlignment`** — entire BookWalker stack shipped yesterday. Resolver gets re-pointed at a new Japanese-title source; everything downstream of that is identical.
- **`PremiumCatalog` + `PremiumCoverExtractor`** — Premium short-circuit logic continues to work; the catalog entry must carry both `weebcentralId` and `anilistId` for Premium series (curated update if needed).
- **`AniListClient` + `AniListCache`** — retained for lazy decoration use only (banner / description / English title on series-detail page). Existing `japaneseTitleFor` accessor stays in the codebase but is NOT called by the resolver chain anymore (removing it entirely is future cleanup; leaving it for now in case a future feature needs it).

### 4.2 New / extended

**`MangaUpdatesClient` — extension (not new file)**
- Add `QStringList japaneseAlternateTitlesFor(int requestId)` or surface via `seriesSucceeded` payload. The `associated` field is already in the response (per audit `manga_volume_metadata_sources_2026-05-16.md`); it just isn't parsed today.
- Filter alt-titles to those containing CJK / Hiragana / Katakana characters (same Unicode-range check the BookWalker arc already uses in `AniListCache::japaneseTitleFor`).

**`VolumeMetadataResolver` — extension**
- Add `resolveByTitle(QString englishTitle, QString weebcentralId)` alongside the existing `resolveForAnilist`. The new method searches MangaUpdates by title and caches by `weebcentralId` instead of `anilistId`.
- The existing `resolveForAnilist` stays (backward compatibility for entries that already have anilistId).

**`VolumeCoverResolver` — re-keyed**
- Replace `AniListCache* anilistCache` dependency with a Japanese-title lookup that goes through `VolumeMetadataResolver`'s new `weebcentralId`-keyed sidecar.
- `resolveForAnilist(int anilistId)` becomes `resolveForSeries(QString weebcentralId, int anilistIdOptional)`. AniList ID is now optional metadata, not the identity.
- Cache filename pattern changes from `<anilistId>.json` to `<weebcentralId>.json`. Old cache files orphan harmlessly (next cold-open writes new entry).

**`ComicsLibraryRecord` — schema bump**
- Add `QString weebcentralId` field (primary identity).
- `int anilistId` becomes optional (0 = absent).
- JSON serializer writes both; reader is backward-compatible (legacy entries without `weebcentralId` get lazy backfill on first open).

**`ComicsTankoyomiSearchWidget` — re-wiring**
- Currently calls into AniListClient. Re-wire to `MangaSourceRegistry::activeScraper()->search(query)` (today that's WeebCentralScraper).
- Replace AniList `MediaPreview` result-handling with WeebCentral `MangaResult` result-handling.

**`ComicsSeriesView` — minor adjustments**
- Existing `showSeries(MediaPreview)` becomes `showSeries(MangaResult)` OR keep both signatures (overloads) for the duration of the migration.
- Lazy backfill path: on first open of a library entry with no `weebcentralId`, fire a WeebCentral search-by-title; if a confident match comes back, store the weebcentralId on the library entry.
- Per-volume cover resolution: now invoked with `weebcentralId` instead of `anilistId`.

**`MangaResult` ↔ `MediaPreview` field mapping**
- Verify `MangaResult` has the fields ComicsSeriesView needs (title, coverUrl, seriesId, year?, status?). If gaps, extend `MangaResult` or add a `MangaResultExt` carrying the augmented fields.

### 4.3 Removed / deprecated

- **`AniListClient::searchByTitle` from critical path** — the method remains in the codebase (used for lazy decoration on series-detail-open and as a backstop for `japaneseTitleFor` when MangaUpdates fails), but no UI path invokes it for search-bar queries. The throttle pattern stays.

## 5. Data flow

### 5.1 Search bar typed query

1. User types in `ComicsTankoyomiSearchWidget`. Existing debounce (if any) preserved.
2. Widget calls `m_sourceRegistry->activeScraper()->search(query, limit=60)`.
3. `WeebCentralScraper` POSTs `/search/data?text=<query>&sort=Best+Match&...` with `HX-Target: search-results` header. (Already implemented at `WeebCentralScraper.cpp:34`.)
4. `parseSearchHtml` returns `QList<MangaResult>` with title, weebcentralId (ULID), cover URL, series URL, year/status if available.
5. Widget paints search-result tiles using MangaResult fields directly. No AniList query fires.

### 5.2 User clicks search result → series-view-open

1. `ComicsSeriesView::showSeries(MangaResult)` invoked.
2. `m_currentWeebcentralId = result.id`; `m_currentAnilistId = 0` (will be backfilled if applicable later).
3. Loading overlay paints. Safety timer (10s) starts.
4. Three async chains fire in parallel:
   - **(a) WeebCentralScraper::fetchDetail(result)** → returns full chapter list. Populates volume rows.
   - **(b) VolumeMetadataResolver::resolveByTitle(result.title, result.id)** → searches MangaUpdates by English title, caches result keyed on weebcentralId. Returns canonical volume count + `associated` Japanese alt-titles list.
   - **(c) (Optional, fire-and-forget) AniListClient::searchByTitle(result.title)** → if successful, store anilistId on the series view + augment banner + description. If 403, silently skip.
5. `VolumeCoverResolver::resolveForSeries(weebcentralId, anilistIdOptional)` fires after (b) lands:
   - Premium short-circuit (PremiumCatalog::hasPremiumEntry by weebcentralId)
   - Japanese title: take first CJK/Hiragana/Katakana entry from MangaUpdates `associated` field
   - Cache check: `BookWalkerCache::load(weebcentralId, currentCanonicalCount)` (TTL + drift)
   - BookWalker search → page parse → align → cache write → emit
6. Volume rows update with BookWalker per-volume covers as they arrive.
7. If chain (b) returns no Japanese alt-titles (rare), BookWalker resolver emits `unresolved("no-japanese-title")` and series-level fallback paints. AniList is NOT used as a rescue here — that would put AniList back on the critical path which contradicts Decision #4. Niche series without a MangaUpdates Japanese alt-title gracefully get series-level covers; acceptable per the BookWalker arc's existing fallback contract.

### 5.3 Add to library

1. User clicks "Add to Library" on the series-detail page.
2. `ComicsLibraryRecord` is created/updated with:
   - `weebcentralId = m_currentWeebcentralId` (required)
   - `anilistId = m_currentAnilistId` (optional, may be 0)
   - Title + cover URL + bookmarked metadata as today
3. Existing AniList-keyed sidecar (if anilistId is known) continues to function for that entry. Pure WeebCentral entries skip the AniList sidecar lookup chain.

### 5.4 Existing-library entry first-open (legacy migration)

1. User opens Death Note (today: stored with `anilistId=30021` only, no weebcentralId yet).
2. `ComicsSeriesView::showSeries(libraryEntry)` detects empty weebcentralId.
3. Async: `WeebCentralScraper::search(libraryEntry.title)` fires in background.
4. If top result has confident-match similarity (case-insensitive exact match, or starts-with), store its weebcentralId on the library entry and persist.
5. If no confident match, log a warning; entry continues to function in legacy AniList-only mode (cover resolver chain takes the original AniList-keyed path).

## 6. Error handling

| Failure | App behavior |
|---------|--------------|
| WeebCentral search returns 0 results | Search bar shows "No results" — same as today |
| WeebCentral search returns 4xx/5xx | Search bar shows "Search failed, try again" with retry button. This is a hard dependency for the reader — acceptable to surface as an error. |
| WeebCentral `/search/data` page-format changes | Parser returns empty list; same as zero-result case. Logged. |
| MangaUpdates lookup-by-title fails | Skip Japanese-title fetch; BookWalker resolver runs in degraded mode (no per-volume covers, series-level fallback). Logged. |
| MangaUpdates `associated` field empty | Same as above. |
| AniList lookup-by-title fails (any reason, including 403) | Silent — series page renders with WeebCentral title and no banner/description. No user-visible error. |
| BookWalker chain fails | Series-level fallback (already-shipped behavior from yesterday). |
| Legacy library entry — WeebCentral title-match returns no confident match | Entry continues to function with anilistId-only path; logged for diagnostic follow-up. |

## 7. Caching

- **WeebCentral search results** — short TTL (5 min). Repeated identical queries hit cache.
- **WeebCentral series-detail** — longer TTL (24h). Chapter list cached by weebcentralId.
- **BookWalker per-volume covers** — existing `BookWalkerCache` reused; filename pattern changes from `<anilistId>.json` to `<weebcentralId>.json`. 7-day TTL unchanged.
- **MangaUpdates by-weebcentralId** — new cache layer (or extend `AniListCache` pattern to take a weebcentralId key alongside anilistId). 7-day TTL; drift check via vol-count delta.
- **AniListCache** — kept; used for lazy decoration lookups and as `japaneseTitleFor` fallback only.

## 8. Verification

### 8.1 Smoke matrix

| Test | Expectation |
|------|-------------|
| Search "One Piece" | Returns ~15-30 WeebCentral hits including manga, no AniList query fires (log-confirmed) |
| Search "Berserk" | First hit is Berserk-proper with weebcentralId `01J76XYAVE3FZ3YMHMTKEZGXM4` |
| Click Berserk result → series view | Loading overlay shows; within 5s, 43 volume rows populated with BookWalker tankobon covers per the BookWalker arc smoke |
| AniList blocked (e.g. firewall rule on `graphql.anilist.co`) | Search bar and series view both work end-to-end; no 403 surfaces; banner may be missing on series detail |
| Niche WeebCentral-only series | Search shows it; opens fine; per-volume covers show series-level fallback (no MangaUpdates match for niche titles is acceptable) |
| Existing Death Note library entry (anilistId=30021, no weebcentralId) | First re-open fires WeebCentral search-by-title; weebcentralId backfilled; subsequent opens go through new path |
| Premium-catalog series | PremiumCoverExtractor still wins via Premium short-circuit (Premium entries must be migrated to carry weebcentralId — see §10) |

### 8.2 Unit tests

- `WeebCentralScraper::parseSearchHtml` — extend existing test suite with 2026-05-19 frozen Berserk + One Piece search HTML fixtures.
- New: `VolumeMetadataResolver::extractJapaneseTitlesFromAssociated` (pure function) — frozen MangaUpdates response fixture for Berserk (we have it from the 2026-05-16 audit), assert `ベルセルク` is the first CJK/Hiragana/Katakana entry returned.
- New: legacy library backfill match logic (pure function) — given a library entry title and a list of WeebCentral search results, decide whether to backfill.

## 9. Out of scope (explicitly)

- **AniList augmentation on search-result tiles** (Decision #1 — never)
- **Bulk library migration at app startup** — lazy backfill only (Decision #6); a separate v1.x arc may add a "migrate everything now" button
- **Removing AniListClient entirely** — retained as decoration layer; full removal is future cleanup
- **Adding new metadata sources beyond WeebCentral/MangaUpdates/BookWalker/AniList** — separate arcs (Fandom MediaWiki for arc/season descriptions was deferred during the BookWalker arc)
- **Tankoyomi-Premium curated catalog expansion** — separate arc per `project_tankoyomi_premium_mvp_brainstorm_prelock.md`
- **Multi-source search** (combining results from multiple scrapers) — single-source-WeebCentral for v1; multi-source is a future feature

## 10. Open questions / v1.x deferrals

- **MangaUpdates lookup-by-title disambiguation** — if a title search returns multiple matches (e.g. "Berserk" matches the manga and a tangentially-named one-shot), what's the picking heuristic? Plan-time decision; likely "first result with non-trivial associated-titles list, prefer matches with publisher Hakusensha/Shueisha/Kodansha."
- **Premium-catalog migration** — Premium entries today key on anilistId. They need weebcentralId added. Hemanth-curated update or one-time migration script.
- **WeebCentralCache TTL specifics** — 5min for search, 24h for series-detail proposed; revisit if WeebCentral changes frequency.
- **`MangaResult` schema completeness** — verify it has all fields ComicsSeriesView needs (year, status, format). Resolved in plan-time after a brief grep.
- **AniList lazy decoration trigger point** — fire on series-view-open? On user-hover-of-banner-area? On Add-to-Library? Plan-time call.

## 11. References

- **AniList 403 diagnosis session** (chat.md 2026-05-19 morning) — systematic-debugging skill, environmental root cause, Hemanth pivot directive
- **BookWalker arc** (2026-05-18, 22 commits) — the resolver chain we're now re-keying. Spec at `docs/superpowers/specs/2026-05-18-bookwalker-volume-covers-design.md`
- **2026-05-16 audit** `agents/audits/manga_volume_metadata_sources_2026-05-16.md` — MangaUpdates `associated` field documented (Berserk → `ベルセルク` plus 9 variants)
- **Hemanth philosophy directive 2026-05-19** — "WeebCentral is the baseline... we will find other solutions than an API that burns out after 5 tries"
- **WeebCentralScraper.cpp:34** (existing `search` method, already wired into `MangaSourceRegistry.cpp:9`)
- **MangaSourceRegistry.cpp:9** — `m_scrapers.append(new WeebCentralScraper(nam, this));`
- **Live probe 2026-05-19** — WeebCentral `/search/data` returns 200 OK with real series + cover URLs; Berserk series page has zero Japanese characters (proves we cannot use WeebCentral as the Japanese-title source, need MangaUpdates)
- **Memory `feedback_stremio_for_manga_vibe.md`** — the broader vision that informs this arc
