# THEATRE_ANIME_CATALOG — Design Spec

**Status:** APPROVED design (Hemanth, 2026-05-28). Ready for implementation plan.
**Owner:** Agent 4 (Stream + Tankorent).
**Supersedes:** the ground-truth seed at `docs/superpowers/specs/2026-05-28-theatre-anime-catalog-handoff.md`.
**Process:** authored via `/superpowers:brainstorming` with a full ground-truth-first pass (every keystone verified on live data before design — per [[feedback_ground_truth_before_brainstorm]]).

---

## 1. Problem

In Theatre, One Piece shows as **23 TMDB-style seasons**. In Hemanth's Stremio it's **one continuous absolute-numbered list** (1100+ episodes). This is **not a bug** — it's a missing catalog layer.

- Cinemeta (our metadata source) genuinely returns One Piece as 23 seasons. Verified: `GET https://v3-cinemeta.strem.io/meta/series/tt0388629.json` → 23 seasons / ~1230 videos. Tankoban faithfully reflects that.
- Hemanth's Stremio differs because he runs the **Anime Kitsu** addon, which provides a separate anime catalog keyed on Kitsu ids. We don't have that layer.

This arc adds an anime metadata catalog to Theatre so anime read as one flat episode list.

## 2. Goals / Non-goals

**Goals**
- Any anime (`genre = Animation` + `country = Japan`) automatically reads from the Kitsu anime catalog instead of Cinemeta — flat absolute episode numbering, correct anime metadata — at **every** entry point (search, library, browse).
- A small **"Anime" badge** signals which catalog is in use.
- Watching anime works (already reachable via Torrentio + Kitsu ids).
- Downloading anime works against how anime is actually packaged: big batch torrents with in-torrent episode-range selection.

**Non-goals (explicitly out of scope)**
- Arc/saga grouping or arc→episode-range auto-selection (too much metadata machinery for too little — Hemanth's call).
- Reworking short/seasonal-anime behavior beyond the catalog swap.
- Rebuilding anything Stremio doesn't (we go *one step beyond* Stremio with the auto-reroute — see §4).
- Reusing the Comics `AniList*` GraphQL client — it serves manga volume data, not video episode meta; the addon route is less work (judged this wake).

## 3. Ground-truth (verified live this wake — do not re-derive)

| Fact | Evidence |
|---|---|
| Cinemeta returns One Piece as 23 seasons | live curl `meta/series/tt0388629.json` |
| Anime detection signal exists in Cinemeta meta | One Piece meta carries `genres: ["Animation",…]` + `country: "Japan"` |
| Anime Kitsu serves the flat shape we want | `meta/series/kitsu:12.json` → all videos `season=1`, `episode` 1..1100+, ids `kitsu:12:N`; per-video `imdb_id="tt0388629"` |
| Anime Kitsu manifest | `community.anime.kitsu` v0.0.10; resources catalog+meta+subtitles; types anime/movie/series; idPrefixes `kitsu, mal, anilist, anidb` |
| Stream side already anime-capable | `torrentio…/stream/series/kitsu:12:1.json` → 60 streams w/ infoHashes (some live-action contamination) |
| Torrentio accepts kitsu | seeded in `AddonRegistry.cpp:684-705` with stream types `{movie,series,anime}`, idPrefixes `{tt,kitsu}` |
| Kitsu has **no** IMDb mapping | Kitsu `anime/12/mappings` → tvdb 81797 / anidb 69 / mal 21 / anilist 21 / trakt 37696. No `imdb`. The tt→kitsu bridge cannot go through Kitsu directly. |
| A deterministic IMDb↔Kitsu bridge exists | Fribb/anime-lists `anime-list-full.json` carries `imdb_id` + `kitsu_id` per entry; raw at `raw.githubusercontent.com/Fribb/anime-lists/master/<file>` |
| Stremio routes by id-prefix, never merges/auto-reroutes | addon protocol: `tt…`→Cinemeta, `kitsu:…`→Anime Kitsu; separate namespaces; sequential fallback only. Our auto-reroute is intentionally beyond Stremio. |
| Our episode grouping already groups by `season` | `MetaAggregator::parseSeriesEpisodes` (cpp:167-205) — all-`season=1` payload renders as one flat list with no extra work |
| Our meta fetch is tt-only gated | `MetaAggregator::fetchSeriesMeta` (cpp:263) rejects non-`tt` ids — this gate must be opened for the anime path |

## 4. Product decisions (Hemanth, 2026-05-28)

1. **Numbering** — absolute, one continuous list (match Stremio's Kitsu shape).
2. **Scope** — **all** anime (`Animation` + `Japan`), regardless of season count. (Overrides the initial "only broken megaseries" — Hemanth widened it: Kitsu is locked in for everything Animation+Japan.)
3. **Detection** — automatic + small "Anime" badge.
4. **Downloads** — **no arcs.** Anime torrents are big batches ("One Piece (Dual Audio) Episode 1-1076 1080p x264"). Model = find the batch torrent, expose its episode file list, user picks a range from inside it (BitTorrent-client style), download just that range via file-priority.

## 5. Architecture

### 5.1 Detection + reroute
After Cinemeta series meta is fetched, evaluate: `genres` contains `"Animation"` AND `country == "Japan"` → this is anime → reroute to the Kitsu catalog. No season-count condition. Live-action, western cartoons untouched.

Hook point: the meta-resolution flow around `MetaAggregator::fetchSeriesMeta`. Today it is hard-gated to `tt` ids (cpp:263); the anime path runs *after* the Cinemeta fetch (we need Cinemeta's genres/country to detect), so detection wraps the Cinemeta result rather than replacing the entry id.

### 5.2 The ID bridge (the real engineering work)
We hold a `tt…` id; Kitsu/Torrentio want `kitsu:N`. Kitsu publishes no IMDb mapping, so:

- **Primary — mapping table.** Fetch + cache Fribb's `anime-list-full.json` (file-backed cache, mirror the `AniListCache` pattern). Lookup `imdb_id → kitsu_id`. Deterministic, offline after first fetch.
- **Fallback — search-confirm (load-bearing, not rare).** For shows missing from the table (notably **fresh seasonal anime** — the list lags new airings, and many newer/niche anime lack IMDb entries): query Anime Kitsu's search catalog by the Cinemeta title, then **confirm** the match by the `imdb_id` Anime Kitsu embeds in its meta (round-trip equality with our `tt` id). Because all-anime scope routes current-season shows too, this path runs often and must be built first-class.
- **Last resort — graceful degrade.** If neither resolves a Kitsu id, stay on the Cinemeta view. Never worse than today.

New component (working name): `AnimeCatalogResolver` in `src/core/stream/` — owns detection, the table cache, the search-confirm fallback, and returns `{kitsuId, isAnime}`.

### 5.3 Metadata swap
- Seed **Anime Kitsu** as a meta+catalog addon in `AddonRegistry` (alongside Cinemeta/Torrentio). idPrefixes `{kitsu, mal, anilist, anidb}`, resources `meta` + `catalog` (subtitles optional, defer).
- For a detected anime, fetch `meta/series/kitsu:<id>.json` from Anime Kitsu and feed it to `parseSeriesEpisodes`. All videos are `season=1` → the existing grouping yields one flat list with no change to that function.
- Surface an `isAnime` flag through the series-meta result so the UI can badge it.

### 5.4 Series page (UI)
- One continuous episode list (absolute 1,2,3…), no season tabs/chips for anime (hide them).
- Small **"Anime"** badge near the title/source area.
- Touchpoints: the Theatre series/detail view + `TheatreDownloadPanel`. Season/multi-season chips already exist; gate them off when `isAnime`.

### 5.5 Watching (streams)
- Episode play sends `kitsu:<id>:<episode>` to Torrentio (existing stream source, already kitsu-capable). Thread the resolved Kitsu id through the play path so the episode id is `kitsu:…` not `tt:season:ep`.
- **Contamination guard:** Torrentio sometimes returns the live-action One Piece. Prefer anime/fansub-tagged results (existing `QualityScorer`/`PackClassifier`/`TitleMetadataEstimator` are the place to bias ranking). This is ranking, not exclusion.

### 5.6 Downloading
- Anime query strategy in `StreamAggregator::searchPacks`: instead of `"<title> S<NN>"` / `"<title> Season N"` (cpp:704-712), build **broad batch queries** for anime: `"<title>"`, `"<title> 1080p"`, `"<title> Complete"`, `"<title> Batch"`. Surfaces the big multi-episode torrents.
- **In-torrent range selection:** present the chosen batch's episode file list; user selects a range or individual episodes; download just those via libtorrent file-priority. Reuse the file-priority mechanism already used on the manga side (`TorrentVolumeProvider` "give me Vol N") — same primitive, episode-range instead of volume.
- **Finding 3:** lift the `kPackSearchPerIndexerLimit = 25` cap (`StreamAggregator.cpp:683`) for the batch path — anime batch search needs the wider net. (This is the real Nyaa-parity surface, distinct from the standalone-tab 80→300 already shipped in Phase 2.)

### 5.7 Scope & safety
- Only `Animation + Japan` shows are touched; everything else is byte-for-byte unchanged.
- Reroute covers every entry point uniformly (search/library/browse) so One Piece reads flat however reached — fixes the search-bar-vs-Kitsu-shelf split Hemanth confirmed.
- Every failure path (table miss → search-confirm; search-confirm miss → Cinemeta; Kitsu unreachable → Cinemeta) degrades to today's behavior. No regression for non-anime.

## 6. Key code touchpoints

| File | Change |
|---|---|
| `src/core/stream/AnimeCatalogResolver.{h,cpp}` (new) | detection + tt→kitsu bridge (table + search-confirm) + cache |
| `src/core/stream/MetaAggregator.cpp` | wrap series-meta resolution: detect anime post-Cinemeta, reroute to Anime Kitsu, carry `isAnime` flag. Entry id stays `tt` (so Cinemeta fetch + detection still run); the anime meta fetch uses the resolved `kitsu` id via a path that bypasses the tt-only gate at cpp:263 — that gate is not loosened for general callers |
| `src/core/stream/addon/AddonRegistry.cpp` | seed Anime Kitsu addon (meta+catalog) |
| `src/core/stream/StreamAggregator.cpp` | anime batch query strategy (cpp:704-712); lift 25-cap (cpp:683) for batch path |
| Theatre series/detail view + `TheatreDownloadPanel.cpp` | flat list rendering, hide season chips when anime, "Anime" badge, in-torrent episode-range picker |
| stream play path | thread `kitsu:<id>:<ep>` episode ids to Torrentio |
| `QualityScorer` / `PackClassifier` / `TitleMetadataEstimator` | bias against live-action contamination on anime stream/pack results |

## 7. Data dependency

- **Fribb/anime-lists** `anime-list-full.json` (or `anime-list-mini.json`) — IMDb↔Kitsu map. Fetched + cached file-backed; refresh on a TTL. Update cadence is community-driven and can lag (drives the §5.2 fallback). Validate One Piece's specific row at build time (cheap smoke).

## 8. Risks / edge cases

- **Mapping-table coverage gaps** for new/niche anime → mitigated by the search-confirm fallback (built first-class).
- **`country` missing on some Cinemeta meta** → detection could miss. If observed, widen the signal (e.g. Animation + Japanese language/cast) — implementation refinement, not a design change.
- **Anime films** (`Animation + Japan`, non-episodic) route to Kitsu too — Kitsu serves movies, so this is fine.
- **Live-action contamination** in Torrentio results — ranking bias, not a blocker.
- **Anime Kitsu addon availability** — third-party endpoint; degrade to Cinemeta if unreachable.

## 9. Testing strategy

- **Pure-logic unit tests** (`tankoban_tests`, TDD): detection predicate (Animation+Japan), `parseSeriesEpisodes` on an all-`season=1` payload yields one flat ordered list, mapping-table lookup, search-confirm id round-trip equality.
- **Live-fetch fixtures** captured this wake reused as test inputs (One Piece Cinemeta meta, Anime Kitsu meta, Fribb row).
- **Hemanth smoke:** open One Piece from the search bar → flat absolute list + Anime badge; play an episode; download a batch + pick a range; confirm a single-season modern anime also routes to Kitsu; confirm a live-action show + a western cartoon are untouched.

## 10. Suggested build order (seed for the plan)

1. `AnimeCatalogResolver` — detection + mapping-table bridge (+ cache) + search-confirm fallback. (TDD pure logic.)
2. Seed Anime Kitsu addon in registry; wire MetaAggregator reroute + `isAnime` flag.
3. Series-page: flat list + hide chips + Anime badge.
4. Watching: thread kitsu episode ids to Torrentio + contamination ranking bias.
5. Downloading: anime batch query strategy + lift 25-cap + in-torrent episode-range picker.
6. Smoke + Hemanth visual verification.
