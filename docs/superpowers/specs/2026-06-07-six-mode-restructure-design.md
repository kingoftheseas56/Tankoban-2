# Six-Mode Restructure — Design Spec

**Date:** 2026-06-07
**Author:** Agent 0 (brainstorm with Hemanth)
**Status:** Draft — pending Hemanth review → writing-plans
**Scope:** Tankoban 2 (Qt). The Flutter migration is explicitly **out of scope** — this is the only Tankoban.

---

## 1. Motivation

Today the app has **3 top-level modes**: Comics, Books, Theatre. We are restructuring to **6**:

> **Manga · Comics · Books · Anime · TV · Movies**

Drivers (all confirmed by Hemanth):
- **Tailored experiences** — anime ≠ a Hollywood movie ≠ a TV series; each deserves its own home.
- **Cleaner navigation** — movies, series, and anime no longer jumbled in one mixed feed.
- **Different sources** — anime pulls from anime trackers (Nyaa/AniList/Kitsu); movies + TV from Cinemeta/Stremio addons.
- **Different per-item UX** — movies = single file; TV = seasons/episodes; anime = seasons + absolute numbering + sub/dub.

## 2. Locked decisions (brainstorm outcomes)

| Decision | Outcome |
|---|---|
| Mode count | **6 top-level modes** (string-keyed pills, per-mode nav stacks) |
| Manga ↔ Comics line | **Asian vs Western.** Manga = manga + manhwa + manhua + webtoons; Comics = Western (Marvel/DC/indie/European) |
| Anime line (the crux) | **All Japanese animation → Anime mode** (anime series AND anime films). TV/Movies are **non-anime only** |
| Video mode feel | **Shared shell, tailored where it matters** — Anime gets sub/dub, seasons, absolute numbering; TV/Movies stay leaner |
| Catalog per video mode | **Tailored per mode** — Anime → Kitsu/AniList + anime addons; TV/Movies → Cinemeta |
| Library + Continue | **Per-mode** — each mode its own library + its own Continue strip. **No cross-mode view** |
| Existing content | **Start fresh** — new mode libraries begin empty; old Theatre library archived/left as-is |
| Build target | **Tankoban 2 (Qt).** Flutter out of scope |

## 3. Architecture — "split the faces, share the engines"

Keep **one comics backend** and **one video backend**; the 6 modes are **tailored front pages** over them.

- **Comics engine** (manga scrapers + Western scrapers + downloaders + libraries — already largely separate) → surfaced as **Manga** + **Comics** pages.
- **Video engine** (libtorrent + AddonRegistry/transport + player + `StreamDownloadIndex`) → surfaced as **Anime** + **TV** + **Movies** pages, each filtering by the type/anime-flag the engine already computes, each with its own catalog brain + library + Continue strip.

**Rejected alternatives:**
- **B — fully separate engine stacks per mode:** maximal isolation but huge duplication (esp. the video engine) for zero capability gain.
- **C — keep Comics/Theatre as containers with internal sub-tabs:** not real top-level modes; contradicts the "6 modes" intent.

**Why A is sound:** the grounded map (below) shows the separation mostly already exists — manga vs Western run as two separate brains under one page today, and the video engine already tracks content `type` and already detects anime. So this is front-end surgery + wiring, not a backend rebuild.

## 4. Arc 1 — Comics → Manga + Western (mechanical)

**Current state:** `ComicsPage` hosts both, but the brains are already separate:
- **Manga (Asian):** AniList + MangaUpdates catalogs; MangaFire/Nyaa + Premium (torrent volume) downloads; `ComicsTankoyomiLibrary`; `MangaDownloader`.
- **Western:** GetComics + ReadComics/ReadAllComics scrapers; `WesternCatalogLoader`; `WesternLibrary`; `WesternVolumeDownloader`.

**Work:**
1. Add mode constants `PAGE_MANGA = "manga"`, `PAGE_COMICS = "comics"` (Western keeps the "comics" identity) + two `navDefs` entries.
2. Split `ComicsPage` into `MangaPage` + `WesternComicsPage` (or a thin parent delegating to two sub-pages). The split point is the UI layer — the `Mode` enum, search flow, and home grid become per-page. This is the main lift.
3. Wire each page to its already-separate scrapers/catalogs/libraries.
4. Partition `MangaDownloadIndex` by `sourceId` prefix (`anilist:`/`mangaupdates:` → Manga; `getcomics:`/`readcomics:` → Western) — sources are already prefixed, so this is a filter, not a schema change.
5. Progress: add `UnifiedProgressStore` domains `"manga"` and `"western_comics"` (migrate existing comics progress per §6).

**Reader:** unchanged — both modes use the existing comic reader.

## 5. Arc 2 — Theatre → Anime + TV + Movies (the tailored one)

**Shared engine (unchanged):** libtorrent + `AddonRegistry`/`AddonTransport` + the player + `StreamDownloadIndex` + `StreamAggregator`/`MetaAggregator`.

**Classification rule** — an item's mode is derived from data the engine already computes:
- **anime-flag true** (`AnimeCatalogResolver` / Kitsu reroute / `animeCatalogActive` signal) → **Anime mode** (whether film or series).
- else **type == "series"** → **TV mode**.
- else **type == "movie"** → **Movies mode**.

**Per-mode design:**

### Anime
- Catalog brain: **Kitsu/AniList** + anime addons (**Amatsu**, Nyaa). Amatsu is already id-prefix-scoped to `{kitsu, anilist}`.
- Extras (this is the "tailored where it matters"): **sub/dub** selection, **seasons + absolute episode numbering**, anime-style detail screen (flat Kitsu episode list, no Cinemeta season chips).
- Holds **anime series AND anime films**.
- Play path uses the existing `kitsu:<id>:<absoluteEp>` Torrentio/Nyaa route.

### TV
- Catalog brain: **Cinemeta** series catalog, non-anime.
- Standard seasons/episodes detail + episode picker.

### Movies
- Catalog brain: **Cinemeta** movie catalog, non-anime.
- Single-file play; no season/episode UI.

**Per-mode storage:** `anime_library.json`, `tv_library.json`, `movies_library.json`; Continue strips via `UnifiedProgressStore` domains `"anime"`, `"tv"`, `"movies"`. The existing `type` field in `StreamLibraryEntry` + `StreamDownloadIndex.Entry` already carries movie/series; anime-flag is the added discriminator.

**Pre-warm:** persist the IMDb→Kitsu mapping (`AnimeIdMapCache`) to disk and pre-warm at startup so re-opening an anime title doesn't require a live re-fetch.

## 6. Data / storage + migration

- **Mode wiring:** add 3 (+2 comics) `PAGE_*` constants + `navDefs` entries; instantiate the new pages in `buildPageStack()`; `PerModeNavController` already supports N independent per-mode back-stacks (no change).
- **Libraries:** one `*_library.json` per mode (pattern already exists — `StreamLibrary` is the template).
- **Progress:** `UnifiedProgressStore` is domain-keyed; add the new domains.
- **Migration differs by side:**
  - **Comics (Manga/Western) = direct map, NO data loss.** The two libraries are *already* separate stores (`ComicsTankoyomiLibrary` → Manga, `WesternLibrary` → Comics), so existing content flows straight into its mode. The folder-scanned comics library is reconciled by origin. (Hemanth's "start fresh" answer was about Theatre, not comics — his manga library is preserved.)
  - **Video (Anime/TV/Movies) = start fresh.** New mode libraries begin empty; the old mixed `stream_library.json` is **archived/left in place, not auto-rebucketed**. Downloaded files on disk are untouched.
  - *(Which mode inherits the legacy `"comics"` page-id/data vs gets a new id is an implementation detail for the plan; the user-facing split is Manga=Asian / Comics=Western.)*

## 7. Out of scope / deferred

- **Flutter** — not this product.
- **Cross-mode "Continue" view** — explicitly NOT wanted; Continue is per-mode.
- **Vestigial "Videos" (local-file) page** — currently an internal page, not a pill. Decision deferred: fold local video into **Movies**, or drop it. Not blocking the split.
- **Auto-rebucketing old Theatre content** — out (start-fresh chosen).

## 8. Decomposition / sequencing

Two **independent** implementation arcs (different engines), each gets its own plan:
1. **Comics split (Manga + Western)** — mechanical; the brains are separate, the work is splitting `ComicsPage`.
2. **Video split (Anime + TV + Movies)** — larger; the tailored catalogs + anime classification + per-mode libraries.

Either can ship first. The video split is the higher-design-surface one.

## 9. Grounded file references

- **Mode constants / pills / page stack:** `src/ui/MainWindow.cpp:63-75` (constants), `:530-542` (navDefs), `:698-900` (buildPageStack), `:1039-1097` (activatePage), `:1111-1125` (resetActivePageToRoot).
- **Per-mode nav:** `src/ui/PerModeNavController.h:10-57`.
- **Comics brains:** `src/ui/pages/ComicsPage.h` (UI); manga: `src/core/manga/anilist/`, `mangaupdates/`, `MangaDownloader`, `ComicsTankoyomiLibrary`; Western: `GetComicsScraper`, `ReadComicsScraper`, `WesternCatalogLoader`, `WesternLibrary`, `WesternVolumeDownloader`; index: `src/core/manga/MangaDownloadIndex.h` (sourceId-keyed).
- **Video type/anime:** `src/core/stream/StreamLibrary.h:15` (`type`), `StreamDownloadIndex.h:33` (`type`), `AnimeCatalogResolver.cpp:12-20`, `AnimeIdMapCache.h`, `MetaAggregator.h:49-52,:97-100`, `StreamAggregator.h:71-75`, `addon/AddonRegistry.h`, `addon/MetaItem.h:51-72`.
- **Storage:** `src/core/JsonStore.h`, `src/core/CoreBridge.h:34-38` (domain-scoped progress), `src/core/stream/UnifiedProgressStore.h`, `StreamProgress.h:19-28`.

## 10. Definition of Done (acceptance)

- **6 mode pills** in the topbar; each opens its own page with its own per-mode back-stack.
- **Manga vs Comics** correctly separated by source (Asian scrapers vs Western scrapers); each with its own library + Continue.
- **Anime / TV / Movies** correctly classified: every anime title (series or film) lands in Anime; non-anime series in TV; non-anime films in Movies — verified on real titles (e.g. Demon Slayer → Anime; a non-anime series → TV; a film → Movies).
- **Anime extras** present: sub/dub, seasons + absolute numbering.
- Each video mode has its **own catalog/discovery** (Anime: Kitsu/Amatsu; TV/Movies: Cinemeta) + **own library + Continue strip**; no cross-mode bleed.
- **No data loss:** old Theatre/comics content archived, downloaded files on disk untouched.
- **No regression:** Books unchanged; the comic reader + the video player + the torrent engine behave as before.
- Verified against the running app (Hemanth's test is the gate); producer ≠ reviewer before merge.
