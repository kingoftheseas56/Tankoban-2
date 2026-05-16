# Audit - Manga Volume Metadata Sources - 2026-05-16

By Agent 7 (Codex). For Agent 1 (Comic Reader / Tankoyomi) and Hemanth.

Reference comparison: MangaUpdates API/site, AniList GraphQL, Jikan/MAL, MangaDex API, Anime News Network encyclopedia API, BookWalker, Shonen Jump Plus, Comixology/Amazon, AniDB, Baka-Updates/MangaUpdates, MangaPark, Bato, Fandom wikis, and discovered official Shueisha/S-Manga catalog pages.

Scope: live-web research for manga volume metadata sources that can replace or supplement AniList when ongoing manga return null `chapters` and `volumes`. The probe matrix used Death Note, One Piece, Berserk, and Kingdom. Kaggle/static dataset imports were explicitly out of scope. No app code was changed.

## Observed behavior in our codebase

Tankoban's current volume pivot uses AniList as the top metadata backbone. `AniListClient` queries `chapters` and `volumes` scalars from `https://graphql.anilist.co` and stores them as `MediaDetail.totalChapters` and `MediaDetail.totalVolumes` in `src/core/manga/anilist/AniListClient.cpp`.

`AniListTypes.h` documents both totals as `0 when unknown / ongoing`, and `AniListVolumeMapper.cpp` now has a placeholder fallback for ongoing series with missing totals. That placeholder lets the UI show a single `Vol X` row, but it does not solve the actual backbone problem: Tankoyomi still lacks the real volume count for ongoing manga and lacks reliable chapter-to-volume binding unless another source supplies it.

Direct AniList probe on 2026-05-16:

| Series | AniList id | chapters | volumes | status |
|---|---:|---:|---:|---|
| Death Note | 30021 | 108 | 12 | FINISHED |
| One Piece | 30013 | null | null | RELEASING |
| Berserk | 30002 | null | null | RELEASING |
| Kingdom | 46765 | null | null | RELEASING |

Sample request shape:

```graphql
query ($search:String){
  Media(search:$search,type:MANGA){
    id
    title{romaji english}
    chapters
    volumes
    status
  }
}
```

AniList is therefore still useful for search, cover, banner, description, status, and broad identity, but it is not a sufficient volume-count source for ongoing series.

## Coverage Matrix

Legend:

- Full count: source exposes a usable total volume count matching the current publication shape.
- Partial count: source has some volumes but misses enough to fail as a catalog backbone.
- Binding: source exposes chapter-to-volume grouping.
- UX bonus: per-volume cover, release date, or volume description available.
- Freshness is based on live response metadata, visible page update state, or current catalog behavior observed during this audit.

| Source | Reachable / integration shape | Death Note | One Piece | Berserk | Kingdom | Binding | UX bonus | Freshness | Maintenance |
|---|---|---|---|---|---|---|---|---|---|
| MangaUpdates API | Reachable, unauth JSON for search/detail; POST search, GET detail. | Full: latest_chapter 108, status `12 Volumes + 1 Extra Volume (Complete)`. | Full: latest_chapter 1182, status `114 Volumes (Ongoing)`. | Full: latest_chapter 383, status `43 Volumes (Ongoing)`. | Full: latest_chapter 832, status `79 Volumes (Ongoing)`. | No structured per-chapter binding; anime adaptation fields may mention ranges only. | Series description, cover, publisher links, official links, last_updated. | Excellent: sample updates May 6-13, 2026. | Low-medium. API is clean, but count is embedded in status text, so parse conservatively. |
| AniList GraphQL | Reachable, unauth GraphQL; already integrated. | Full: 108/12. | Missing: null/null. | Missing: null/null. | Missing: null/null. | No public per-chapter binding. | Series cover, banner, description, genres. | Good for identity; poor for ongoing counts. | Low, already shipped, but insufficient alone. |
| MAL via Jikan | Reachable, unauth JSON wrapper. | Full: 108/12. | Missing: null/null. | Missing: null/null. | Missing: null/null. | No chapter binding in Jikan manga payload. | MAL synopsis, images, dates, status. | Good wrapper uptime; MAL ongoing totals are same null class. | Low, but not useful for this bug. |
| MangaDex aggregate API | Reachable, unauth JSON; `/manga/{id}/aggregate`. | Partial: all languages 3 volume keys, English 1. | Partial: all languages 62 volume keys including 1-7 and 61-114, English 2. | Strong for this case: all languages 44 volume keys including 1-43 and none; English 44. | Sparse: all languages 11 volume keys; English 4 volume keys. | Yes where uploads are tagged by volume/chapter. | Chapter IDs/titles via chapter API; covers via cover_art relationship. | Upload-driven; not publisher-canonical. | Low for API, high for correctness cleanup. |
| Anime News Network API | Reachable XML; reports search plus encyclopedia API. | Full count: `Number of tankoubon` 12; 28 release rows. | No `Number of tankoubon`; 230 release rows. | No `Number of tankoubon`; 58 release rows. | No `Number of tankoubon`; 6 release rows. | No generalized chapter binding; release rows are English/product records. | Picture, plot, releases, news/reviews. | Stable, generated live 2026-05-16; ongoing counts sparse. | Low-medium XML parser, but coverage incomplete. |
| BookWalker JP | Reachable scrape target; official retailer catalog. | Full official Japanese volumes visible via search/series pages. | Full official Japanese volumes visible, current around 114. | Full official Japanese volumes visible, current around 43. | Full official Japanese volumes visible, current around 79. | Usually no per-chapter binding in list view; individual ebook pages may list table-of-contents inconsistently. | Cover art, price, publication date, publisher, sample pages. | Official catalog, very fresh. | Medium-high. Scraper, JS/layout changes, region/language friction. |
| Shueisha / S-Manga / Shonen Jump Plus | Reachable scrape target; official publisher catalogs for Shueisha titles. | Full for Death Note via Shueisha/S-Manga/Jump Plus links exposed by MangaUpdates. | Full for One Piece via Shueisha/S-Manga. | Not applicable: Hakusensha, not Shueisha. | Full for Kingdom via Shueisha/S-Manga, not generally Shonen Jump Plus. | Publisher pages sometimes expose volume table-of-contents; not uniform. | Covers, ISBN, dates, official descriptions. | Official and fresh for Shueisha works. | Medium-high; publisher-specific parsers. |
| Comixology / Amazon Kindle | Reachable as retail pages, but Comixology brand is folded into Amazon. | Full English release count likely visible. | English release coverage available but lags Japanese releases and region/catalog filters apply. | English release coverage available, lags Japanese. | Poor historically because no VIZ English release before 2025; not canonical for Japanese count. | No reliable chapter binding for Japanese volumes. | Covers, release dates, descriptions, customer-facing product metadata. | Fresh for English releases only. | High for scraping and ToS risk; low value for Japanese ongoing counts. |
| AniDB | Reachable but wrong domain. | Not a manga-volume source. | Not a manga-volume source. | Not a manga-volume source. | Not a manga-volume source. | No. | Anime metadata only or adaptation linkage. | Stable but irrelevant. | Do not integrate for this problem. |
| Baka-Updates Manga | Same lineage/site as MangaUpdates; current canonical host is MangaUpdates. | Same as MangaUpdates. | Same as MangaUpdates. | Same as MangaUpdates. | Same as MangaUpdates. | Same as MangaUpdates. | Same as MangaUpdates. | Same as MangaUpdates. | Treat as alias/history, not separate integration. |
| MangaPark | Probe unstable: root timed out; title pages returned Cloudflare 521 for sampled Kingdom URLs. | Unknown. | Unknown. | Unknown. | Unknown. | Likely yes if page reachable, but not verified. | Reader UX may expose covers/chapters. | Unclear. | High; brittle and not a good backbone. |
| Bato.to | Probe unstable from this environment; search/title pages timed out or failed. | Unknown/partial. | Unknown/partial. | Unknown/partial. | Unknown/partial. | Often chapter-list only; volume tags vary by uploader. | Covers and chapter uploader metadata. | Upload-driven. | High; not publisher-canonical. |
| Fandom wikis | Direct HTML blocked with 403, but MediaWiki API reachable for sampled page structures. | Not sampled; likely per-franchise. | Strong: One Piece wiki has volume-list sections. | Strong: Berserk wiki has releases page with volumes and unvolumized episodes sections. | Per-volume pages exist; sampled `Vol.73` has Synopsis, Chapters, Pages sections. | Yes, but schema differs per wiki. | Best UX bonus: synopsis, cover, chapter list, sometimes dates/pages. | Community-maintained; often very current for popular series. | High. Expect bespoke parser per wiki family. |
| MangaSee / MangaFire / similar reader peers | Reachable landing pages for some peers; not volume-first canonical metadata. | Likely full or partial depending site. | Likely partial/full for popular titles. | Likely partial/full for popular titles. | Unknown. | Many reader pages encode volume in chapter titles, not normalized metadata. | Covers, descriptions, reader chapter lists. | Unofficial and unstable. | High legal/maintenance risk; not recommended as backbone. |

## Per-Source Details

### 1. MangaUpdates

Probe endpoints:

- `POST https://api.mangaupdates.com/v1/series/search`
- `GET https://api.mangaupdates.com/v1/series/{series_id}`
- Root health check: `GET https://api.mangaupdates.com/v1/`

Sample search body:

```json
{"search":"Kingdom","page":1,"per_page":10}
```

Probe results:

| Series | series_id | latest_chapter | status field | completed | last_updated |
|---|---:|---:|---|---|---|
| Death Note | 3479935384 | 108 | 12 Volumes + 1 Extra Volume (Complete); 7 Bunkoban Volumes (Complete); 1 Bunkoban Volume (Complete) | true | 2026-05-06T05:16:39-07:00 |
| One Piece | 55099564912 | 1182 | 114 Volumes (Ongoing) | false | 2026-05-08T03:35:56-07:00 |
| Berserk | 51239621230 | 383 | 43 Volumes (Ongoing) | false | 2026-05-10T05:29:37-07:00 |
| Kingdom | 4324727424 | 832 | 79 Volumes (Ongoing) | false | 2026-05-13T23:35:36-07:00 |

Observations:

- This is the strongest live source for the specific AniList-null problem.
- The `status` field is not a typed `volume_count` integer; it is human text. Still, the first `(\d+) Volumes` token is easy to parse for standard tankobon counts.
- It exposes `latest_chapter`, `completed`, authors, publishers, image, description, original/official links, and `last_updated`.
- It does not expose a volume table with chapter ranges in the tested payload. It solves count, not binding.
- It is source-of-truth-like for obscure-in-the-West Kingdom: 79 volumes and chapter 832 were visible with a May 13, 2026 update. That is exactly the failure case AniList and MangaDex struggle with.

Maintenance call:

- Best v1 candidate. Implement as a small `MangaUpdatesClient` with search-by-title, exact-match/author disambiguation, and detail fetch. Parse only the leading tankobon count from `status`; keep raw `status` in cache for debugging.

### 2. AniList

Endpoint:

- `POST https://graphql.anilist.co`

Observed payload:

- Death Note returns 108 chapters and 12 volumes.
- One Piece, Berserk, and Kingdom return null chapter and volume totals.

Observations:

- AniList remains excellent for user-facing search, banner/cover art, English/romaji/native titles, descriptions, format, status, start year, and genres.
- It should remain the identity/search provider.
- It cannot be the volume-count provider for ongoing series.
- It does not expose public per-chapter-to-volume mapping in the current Tankoban query shape.

Maintenance call:

- Keep AniList, but add a source-priority resolver: AniList identity first, MangaUpdates volume count second, MangaDex/Fandom binding third when available.

### 3. MyAnimeList via Jikan

Endpoint:

- `GET https://api.jikan.moe/v4/manga?q=<query>&limit=5`

Probe results:

| Series | MAL id | chapters | volumes | status |
|---|---:|---:|---:|---|
| Death Note | 21 | 108 | 12 | Finished |
| One Piece | 13 | null | null | Publishing |
| Berserk | 2 | null | null | Publishing |
| Kingdom | 16765 | null | null | Publishing |

Observations:

- Jikan is easy to call and useful for MAL IDs and alternate metadata.
- It reproduces the same null-for-ongoing problem as AniList for the test set.
- No per-chapter volume binding was visible in the tested manga search/detail shape.

Maintenance call:

- Not a fix for the current problem. Keep only as optional identity crosswalk if needed.

### 4. MangaDex Aggregate API

Endpoints:

- `GET https://api.mangadex.org/manga?title=<query>&includes[]=author`
- `GET https://api.mangadex.org/manga/{id}/aggregate`
- `GET https://api.mangadex.org/manga/{id}/aggregate?translatedLanguage[]=en`

Probe IDs:

- Death Note: `75ee72ab-c6bf-4b87-badd-de839156934c`
- One Piece: `a1c7c817-4e59-43b7-9365-09675a149a6f`
- Berserk: `801513ba-a712-498c-8f57-cae55b38cc92`
- Kingdom: `077a3fed-1634-424f-be7a-9a96b7f07b78`

Probe results:

| Series | All-language aggregate | English aggregate | Interpretation |
|---|---|---|---|
| Death Note | 3 volume keys, 15 chapters | 1 volume key, 3 chapters | Very sparse for a 12-volume completed control. |
| One Piece | 62 volume keys, 665 chapters | 2 volume keys, 6 chapters | Good for newest tagged volumes plus scattered early volumes, not full. |
| Berserk | 44 volume keys, 675 chapters | 44 volume keys, 401 chapters | Strong coverage for this title. |
| Kingdom | 11 volume keys, 96 chapters | 4 volume keys, 5 chapters | Sparse; fails the hardest test case. |

Observations:

- MangaDex is the best probed source for structured chapter-to-volume binding where it has uploads.
- It is not a canonical publication-count source. It is upload-driven and community-tag-driven.
- Kingdom proves the failure mode: all-language aggregate exposes only 11 volume keys versus MangaUpdates' 79 volumes.

Maintenance call:

- Use MangaDex as a binding enhancer, not as the count backbone.

### 5. Anime News Network Encyclopedia

Endpoints:

- `GET https://www.animenewsnetwork.com/encyclopedia/reports.xml?id=155&type=manga&name=<title>`
- `GET https://www.animenewsnetwork.com/encyclopedia/api.xml?manga=<id>`

Probe results:

| Series | ANN id | Count field | Release rows | Notes |
|---|---:|---|---:|---|
| Death Note | 4354 | `Number of tankoubon` = 12 | 28 | Complete control works. |
| One Piece | 1223 | no count field in API response | 230 | Many release rows, but no direct count. |
| Berserk | 2298 | no count field in API response | 58 | Release rows present. |
| Kingdom | 11117 | no count field in API response | 6 | English/release coverage too sparse. |

Observations:

- ANN is stable XML and good for older/completed facts.
- It is not reliable for current Japanese ongoing volume counts.
- It can provide release dates and product rows for English editions, but the rows are not a direct Japanese tankobon count.

Maintenance call:

- Useful tertiary reference, not v1 backbone.

### 6. BookWalker JP

Probe shape:

- Search and series/product pages under `https://bookwalker.jp/`.
- Search snippets and live pages show official ebook listings for the target works.

Observations:

- BookWalker is official retail metadata and should have every digital Japanese volume currently sold.
- It is strong for all four test cases, including Kingdom, because it is publisher/retailer-driven rather than English-fandom-driven.
- It exposes covers, prices, publisher, release dates, and purchase/product URLs.
- It generally does not provide a normalized API for chapter-to-volume binding. Individual product pages may include descriptions/table-of-contents, but the shape is not guaranteed.

Maintenance call:

- Excellent v2 source for official counts, cover art, and release dates if MangaUpdates becomes insufficient.
- Higher integration cost than MangaUpdates because it is a scraper target with language/JS/layout concerns.

### 7. Shueisha / S-Manga / Shonen Jump Plus

Probe shape:

- Official Shueisha and S-Manga catalog links are exposed inside MangaUpdates descriptions for Shueisha works.
- Shonen Jump Plus links exist for some digital/episode variants, but it is not a universal volume catalog for every target.

Observations:

- Strong for Shueisha titles: Death Note, One Piece, Kingdom.
- Not applicable for Berserk, which is Hakusensha.
- S-Manga/Shueisha are closer to publisher-canonical than BookWalker but require publisher-specific parsing.
- Shonen Jump Plus itself is not the general solution; it is only one Shueisha surface and can mix episode/volume/color edition product models.

Maintenance call:

- Treat as a discovered official-source family. Valuable for v2 verification and maybe source-specific enrichments, but not the first v1 integration.

### 8. Comixology / Amazon Kindle

Probe shape:

- Comixology search now routes through Amazon-style digital comics/product pages.

Observations:

- Good for English release metadata, covers, product descriptions, and English release dates.
- Bad for Japanese canonical counts. Kingdom is the clearest failure case because its English licensed release started too late to represent the full Japanese catalog.
- Amazon scraping carries high layout, anti-bot, region, and ToS maintenance risk.

Maintenance call:

- Do not use for v1. Use only if a future feature explicitly needs English-edition retail metadata.

### 9. AniDB

Observations:

- AniDB is an anime database, not a manga-volume metadata source.
- It can help link anime adaptations to manga in some cases, but it does not solve volume counts or chapter binding.

Maintenance call:

- Exclude from this arc.

### 10. Baka-Updates Manga

Observations:

- Baka-Updates Manga is the historical/common name for MangaUpdates.
- The live canonical integration target is `mangaupdates.com` and `api.mangaupdates.com`.

Maintenance call:

- Do not build a separate connector. Treat this as the same source.

### 11. MangaPark and Bato

Probe results:

- `https://mangapark.to/` timed out from this environment.
- Sample MangaPark Kingdom URLs returned Cloudflare 521.
- `https://bato.to/` search/title paths timed out or failed from this environment.

Observations:

- These sites may carry volume tags, but they are reader/upload ecosystems, not publisher-canonical metadata providers.
- Even if reachable, their data has the same fundamental issue as MangaDex: it reflects uploaded chapters, not publication truth.

Maintenance call:

- Not recommended for the metadata backbone. At most, use as manual research evidence when a specific title is missing elsewhere.

### 12. Fandom Wikis

Probe shape:

- Direct HTML pages returned 403 from this environment.
- MediaWiki API `action=parse&prop=sections` was reachable.

Sampled structures:

| Wiki | Sample page | API-observed structure |
|---|---|---|
| One Piece | `onepiece.fandom.com/wiki/Chapters_and_Volumes/Volumes` | Sections include `Volume List`, then ranges `Volume 1 To 10`, `Volume 11 To 20`, etc. |
| Berserk | `berserk.fandom.com/wiki/Releases_(Manga)` | Sections include `Volumes`, `Unvolumized Episodes`, and `Episode Publication History`. |
| Kingdom | `kingdom.fandom.com/wiki/Vol.73` | Per-volume page sections include `Synopsis`, `Chapters`, `Pages`, `References`. |

Observations:

- Fandom is the best UX-bonus source family. It can provide per-volume synopsis, chapter list, page count, release references, and sometimes cover art.
- Structures are not uniform enough for a single generic scraper. One Piece has an aggregate volume list, Berserk has a releases page with unvolumized episodes, and Kingdom uses per-volume pages.
- Fandom is strong for popular series and some hard cases, but maintenance is wiki-by-wiki.

Maintenance call:

- Use in v2/v3 for Stremio-style row descriptions, not as v1 count backbone.

## mangareader.to Autopsy + Successor Hunt

### Live reachability

Observed on 2026-05-16:

- DNS still resolves:
  - A: `104.21.36.254`, `172.67.201.162`
  - AAAA: Cloudflare IPv6 addresses
- TCP 443 succeeds.
- HTTP `http://mangareader.to/` returns 301 to `https://mangareader.to/`.
- HTTPS IPv4 `curl -4 -I --max-time 20 https://mangareader.to/` returns Cloudflare `522`.
- HTTPS IPv6 times out with zero bytes in 20 seconds.

Interpretation:

- The domain is not deregistered and not a seizure-banner domain.
- Cloudflare is still in front of it.
- The origin is not responding to Cloudflare, or Cloudflare cannot reach it. That points to origin death, origin firewall/misconfig, hosting takedown, or operator shutdown. It is not currently evidence of registrar seizure.

### News/takedown search

Searches run:

- `mangareader.to shutdown dead seizure takedown`
- `"mangareader.to" dead down shutdown`
- `"mangareader.to" seizure "Shueisha" "VIZ"`
- `site:torrentfreak.com mangareader.to shutdown`

Findings:

- I did not find a reliable TorrentFreak, Anime News Network, or major-rightsholder article naming a specific `mangareader.to` takedown in the last 90 days.
- The visible evidence is operational, not legal: DNS/Cloudflare remains live, but origin reachability fails.
- Many third-party "is it down" and clone/alternative pages exist, but they are not authoritative.

Hypothesis - mangareader.to is currently in an origin-unreachable state behind Cloudflare, not a confirmed public seizure or DNS deregistration. Agent 1 to validate only if this becomes a dependency.

### Clone and successor probe

High-level only; no content downloading was performed.

| Candidate | Status from probe | Volume-first successor verdict |
|---|---|---|
| `mangareader.it.com` | 301 redirects to `mangabuddy.click`; live HTML landing. | Not a real MangaReader successor; redirect/SEO doorway. |
| `mangareader.blog` | 403 from probe. | Not verified. |
| `mangafire.to` | 200 OK landing. | Live manga reader peer, but not verified as volume-first or Kingdom-complete. |
| `mangasee123.com` | 200 OK landing. | Live peer with historically volume-ish naming, but not publisher-canonical and not verified as generalized volume metadata. |
| MangaPark | Timed out / Cloudflare 521 on sampled paths. | Not usable in this session. |
| Bato | Timed out / failed from this environment. | Not usable in this session. |

Autopsy conclusion:

- No clean clone surfaced that preserves the old `mangareader.to` volume-first UX and is reliable enough to build against.
- The closest obscure peers are still reader sites, not metadata APIs. They may help a human inspect a specific title, but they should not be part of Tankoyomi's volume-data backbone.

## Source Ranking

### Top 3 by data quality for volume counts

1. MangaUpdates
   - Best combined coverage, freshness, and low integration cost.
   - Only probed source that returned all four title counts cleanly, including Kingdom.
2. BookWalker / official publisher catalogs
   - Best canonical authority and release metadata.
   - Higher scraper cost and publisher/retailer-specific behavior.
3. MangaDex aggregate
   - Best structured chapter-to-volume binding where data exists.
   - Not reliable for count coverage; Kingdom fails badly.

### Top 3 by ease of integration

1. MangaUpdates API
   - Simple HTTP JSON.
   - Estimated v1 lift: 120-180 LOC for client/cache/parser plus 30-50 LOC resolver wiring, assuming reuse of existing QNetworkAccessManager patterns.
2. MangaDex aggregate API
   - Simple HTTP JSON and known endpoint.
   - Estimated lift: 100-160 LOC to add optional binding enhancer and cache.
3. Jikan
   - Very easy API, but it does not fix ongoing counts. Useful only as an identity fallback.

### Top 3 by UX enrichment

1. Fandom per-series wikis
   - Best for volume descriptions, chapter lists, page counts, release notes, and sometimes cover art.
   - Highest maintenance burden because every wiki can use a different page model.
2. BookWalker / official retailer pages
   - Best for official cover/release-date/product metadata.
   - Less likely to provide useful volume synopsis/binding.
3. ANN releases
   - Stable release dates and older English product rows.
   - Sparse for current Japanese ongoing counts.

## Recommendations

### V1 path of least resistance

Use MangaUpdates as the single v1 replacement for missing AniList ongoing volume counts.

Integration shape:

1. Keep AniList as search/identity/art/backdrop provider.
2. On series detail fetch, if `totalVolumes <= 0` or `totalChapters <= 0`, query MangaUpdates by title plus author/year disambiguation.
3. Parse MangaUpdates `status` for the first standard tankobon count token, such as `114 Volumes (Ongoing)` or `79 Volumes (Ongoing)`.
4. Store both `mangaupdatesSeriesId` and raw `status` in the local AniList cache sidecar or a new metadata-augment cache.
5. Feed the volume count into `AniListVolumeMapper` so ongoing titles render real `Volume 1..N` rows instead of only `Vol X`.

Estimated lift:

- Client and parser: 120-180 LOC.
- Resolver wiring/cache: 40-80 LOC.
- Mapper adjustment/tests: 40-80 LOC.
- Total: roughly 200-340 LOC.

Risk:

- Low API risk.
- Medium parsing risk because the count is inside human-readable status text.
- Low product risk because MangaUpdates can be used only when AniList totals are missing.

### V2/V3 ambition for Stremio-style row descriptions

Use Fandom wikis selectively, not generically.

Likely shape:

- One parser module per franchise/wiki family.
- One Piece parser: aggregate volume-list page.
- Berserk parser: releases page plus unvolumized episodes section.
- Kingdom parser: per-volume pages such as `Vol.73`.

Expected maintenance:

- Medium-high per supported title family.
- Budget 100-250 LOC per wiki parser plus fixtures/snapshots.
- Add parsers only for high-value series where the UX payoff is visible: One Piece, Berserk, Kingdom, Naruto, Bleach, Jujutsu Kaisen, Chainsaw Man, etc.

Payoff:

- This is the best route to Stremio-like "episode description per row" translated into "volume synopsis per row".
- It also gives chapter lists and sometimes page counts/release references.

### Synthesis path

The best durable backbone is a three-source stack:

1. AniList: identity, search, cover/banner, description, status, genres.
2. MangaUpdates: canonical-ish live count and latest chapter for ongoing series.
3. MangaDex aggregate: optional per-chapter binding where upload metadata is rich.
4. Fandom or official retailer/publisher pages: optional v2 enrichments for descriptions, covers, release dates.

Material benefit over any single source:

- AniList alone fails 30-50 percent of reasonable ongoing catalog volume counts.
- MangaUpdates alone lacks structured binding and the richer AniList UI art/search shape.
- MangaDex alone fails obscure and upload-sparse titles like Kingdom.
- Fandom alone is too bespoke to scale as a general backbone.

Recommended v1 resolver order:

1. Trust AniList totals when both `chapters` and `volumes` are non-null.
2. If AniList totals are missing, use MangaUpdates volume count.
3. If MangaDex aggregate has volume keys for the same series, use it only to attach known chapter ranges to rows.
4. If MangaDex binding is sparse, still render MangaUpdates count rows with unknown chapter range text.
5. Use Fandom only for manually supported high-value enrichments.

## Gaps and Risks

**P0: No probed source besides MangaUpdates solved the Kingdom count case with low integration cost.**
Observed: MangaUpdates returned Kingdom `79 Volumes (Ongoing)` and latest chapter 832. MangaDex returned only 11 all-language volume keys and 4 English volume keys. AniList/Jikan returned null totals. Impact: if MangaUpdates is not used, Tankoyomi stays stuck on placeholder rows for the hardest class of series.

**P1: No single source gives count, binding, descriptions, art, and low maintenance.**
Observed: MangaUpdates gives count but not binding. MangaDex gives binding but sparse coverage. Fandom gives descriptions but bespoke parsers. BookWalker gives official release metadata but not normalized binding. Impact: the long-term UX has to be layered.

**P1: Official sources are publisher/retailer-specific.**
Observed: Shueisha/S-Manga works for Shueisha titles, BookWalker works across publishers but as a retailer scrape, and Berserk is outside the Shueisha family. Impact: official-source integration is valuable but not the first low-risk fix.

**P2: Reader-site successors are not dependable metadata sources.**
Observed: mangareader.to is Cloudflare-origin-unreachable, MangaPark/Bato probes were unstable, and clone candidates did not present a clean volume-first successor. Impact: do not base a multi-year backbone on pirate-adjacent reader-site HTML.

## Hypothesized Root Causes

- **Hypothesis -** AniList and MAL leave ongoing manga totals null because their data model/community policy treats totals as completed-publication facts, not rolling publisher counts. **Agent 1 to validate.**
- **Hypothesis -** MangaDex aggregate is sparse for Kingdom because MangaDex volume keys are derived from uploaded chapter metadata, not publisher volume publication records. **Agent 1 to validate.**
- **Hypothesis -** mangareader.to is not registrar-seized but has an unreachable origin behind Cloudflare, based on live DNS, HTTP 301, and HTTPS 522 behavior. **Agent 1 to validate.**

## Recommended Follow-Ups

- Consider a v1 `MangaUpdatesClient` spike that returns `{seriesId, title, latestChapter, volumeCount, rawStatus, lastUpdated}` for the four control titles.
- Consider one `VolumeMetadataResolver` pure-logic test suite with the four title fixtures: Death Note, One Piece, Berserk, Kingdom.
- Investigate MangaDex binding as a non-authoritative enrichment layer after MangaUpdates count integration lands.
- Pick one wiki, probably One Piece, for a v2 Fandom parser proof-of-concept before committing to a generalized Fandom subsystem.

## Source URLs Used

- MangaUpdates API root: https://api.mangaupdates.com/v1/
- MangaUpdates Death Note: https://www.mangaupdates.com/series/1ljv3bs/death-note
- MangaUpdates One Piece: https://www.mangaupdates.com/series/pb8uwds/one-piece
- MangaUpdates Berserk: https://www.mangaupdates.com/series/njeqwry/berserk
- MangaUpdates Kingdom: https://www.mangaupdates.com/series/1zitx1c/kingdom
- AniList GraphQL: https://graphql.anilist.co
- Jikan API: https://api.jikan.moe/v4/manga
- MangaDex API: https://api.mangadex.org
- MangaDex aggregate docs path used: https://api.mangadex.org/manga/{id}/aggregate
- ANN reports API: https://www.animenewsnetwork.com/encyclopedia/reports.xml
- ANN encyclopedia API: https://www.animenewsnetwork.com/encyclopedia/api.xml
- BookWalker JP: https://bookwalker.jp/
- Shonen Jump Plus: https://shonenjumpplus.com/
- S-Manga: https://www.s-manga.net/
- Amazon/Comixology: https://www.amazon.com/comixology
- One Piece Fandom API sample: https://onepiece.fandom.com/api.php?action=parse&page=Chapters_and_Volumes/Volumes&prop=sections&format=json
- Berserk Fandom API sample: https://berserk.fandom.com/api.php?action=parse&page=Releases_(Manga)&prop=sections&format=json
- Kingdom Fandom API sample: https://kingdom.fandom.com/api.php?action=parse&page=Vol.73&prop=sections&format=json
- mangareader.to: https://mangareader.to/
- MangaFire peer probe: https://mangafire.to/
- MangaSee peer probe: https://mangasee123.com/
