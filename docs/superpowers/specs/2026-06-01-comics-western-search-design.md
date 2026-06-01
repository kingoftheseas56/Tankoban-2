# Spec — Comics Western Live Search & Add

**Status:** DRAFT (spec) · **Author:** Agent 1 (Opus) · **Date:** 2026-06-01
**Arc:** COMICS_WESTERN — arc 2 of 3 (richness ✔ shipped 2026-06-01 → **search (this)** → downloads)
**Predecessor reading:** `2026-06-01-comics-western-richness-design.md` (richness spec) · `scripts/comics_catalogue/RICHNESS_RECON.md` (free-source ceiling) · recap `brother-agent-1-2026-06-01-amber-heron.md`

---

## 1. The one-sentence shape

Point the **live RCO search that already exists in C++** at the Western shelf so a user can type a comic title, see results, open a real series page, and **Add** it as a permanent shelf fixture — mirroring manga mode exactly, no new search machinery.

## 2. The problem this solves (in Hemanth's words)

- *"let's get this done asap so that we move on to the real stuff.. actually being able to search, add comics and download them and read them"* — this arc is the **search + add** half of that real loop.
- The baked 13 are a fixed showcase. The marquee titles we couldn't auto-seed (Walking Dead, Saga, Sandman, Hellboy, The Boys — their collected editions hide under non-obvious RCO sibling slugs) become reachable simply by **typing the name**, because RCO's own search indexes them. The old slug-discovery TODO folds into search.
- Constraint from Hemanth: *"mimic the manga mode, don't make it complicated."* Reuse, don't rebuild.

## 3. Ratified decisions (Hemanth, 2026-06-01)

1. **Lean now, full later.** A live-added series comes in with **cover + synopsis + the readable collected-editions list**. Author/publisher/genre/year (the Wikidata metadata) are **deferred** to a later richness pass — not built in this arc.
2. **Permanent on shelf.** "Add" writes the series to disk in the same folder the baked 13 load from (`data/western_catalogue/`). Search once, it's yours forever. No separate bookmark store.
3. **Same bar, mode-aware.** The existing top Comics search bar searches comics (RCO) when the user is on the Western shelf, manga (WeebCentral) otherwise. One bar, no new UI.
4. **Search solves marquee discovery** (Rule 14, Agent 1's call). No hand-curated "popular Western" shelf. Type the name → find → add. Keeps the arc lean.
5. **No badge.** Live-added tiles look identical to the baked 13 — both are RCO-sourced, so there is no meaningful distinction to surface.

## 4. User-facing flow

On the Western shelf (the 13 covers), the user types "Saga" in the top search bar. Because they're in Western mode, it searches comics live and shows a results strip of covers (same look as manga search). They click Saga → it opens the Western series page (cover, synopsis, collected-editions list) just like the baked 13, except fetched live a second ago. They hit **Add to Library** → Saga becomes a permanent tile on the Western shelf, present every launch. That is the search → add half of the real loop.

## 5. The honest scope line (download→read dependency)

This arc delivers **search → add → browse the series page**. It does **not** deliver reading the pages, and this is where Western diverges from manga:

- RCO's page reader is **dead on plain HTTP** — reader images are injected at runtime by an external obfuscation script (`21wiz.com/s.js`) and are invisible to a direct fetch (`ReadComicsScraper::fetchPages` already returns empty, by design).
- Therefore Western comics **cannot stream-read** the way manga does. Western reading comes through **downloading** the edition first (GetComics) — the **next** arc.
- The real loop for Western is **search → add → download → read**, where "read" genuinely depends on the download arc landing.

After this arc: a populated, searchable Western shelf with real series pages. The "open and read" payoff arrives one arc later. Stated explicitly so there is no false "it works like manga" expectation.

## 6. Architecture — reuse, three small new bits

The flow mirrors manga's existing path. Everything load-bearing already ships; only three small C++ pieces are new.

### 6.1 What already exists (reused as-is)
- **Live search:** `ReadComicsScraper::search(query)` (`src/core/manga/ReadComicsScraper.cpp:57`) — hits `https://rcostation.xyz/Search/Comic?keyword=`, returns `QList<MangaResult>` (series-level, with cover thumbnails). Registered in `MangaSourceRegistry` under `"readcomicsonline"`.
- **Series page fetch:** `ReadComicsScraper::fetchChapters(slug)` (`:118`) — hits `/Comic/<slug>`, returns a **flat** `QList<ChapterInfo>` (issues + collected editions mixed, no tier grouping).
- **Search UI:** the top search bar `ComicsPage::m_searchBar` (`ComicsPage.cpp:940`) → `showSearchMode()` (`:2957`) → `ComicsTankoyomiSearchWidget::search()` (`ComicsTankoyomiSearchWidget.cpp:100`) → results render as `TileCard`s in a strip (`onSearchFinished`/`addResultCard`, `:128`/`:146`).
- **Western series view:** `ComicsSeriesView` already renders the Western shape behind the `source=="rco"` gate; opened via `ComicsPage::openWesternSeriesFromJson()` (`~:2310`) from a schema-v2 JSON object.
- **Shelf load + persistence read path:** `WesternCatalogLoader::loadFromFile()` (`WesternCatalogLoader.cpp:38`) maps `data/western_catalogue/<seriesId>.json` (schema v2) → `MangaCatalog`; `ComicsPage::refreshWesternGrid()` scans that folder.

### 6.2 The three new bits
1. **Mode-aware search routing.** `ComicsTankoyomiSearchWidget` currently hardcodes `m_sourceRegistry->find("weebcentral")`. Make the source selectable so that on the Western shelf the bar resolves `"readcomicsonline"` instead. ComicsPage knows the current mode and sets the active source before delegating the query. (Wiring only.)
2. **Edition classifier + synopsis parse (the only genuinely new logic).**
   - **Classifier:** a pure function `label → formatTier` mirroring the offline `edition_classify.py` keyword tiers (0 Compendium / 1 Omnibus / 2 TPB / 3 Deluxe / 4 Vol / 99 single-issue→excluded) + `is_collected()`. Runs over the labels in the flat `ChapterInfo` list from `fetchChapters` to produce the grouped, single-issues-filtered editions list the series view expects. **This is the unit-testable core** (see §9).
   - **Synopsis:** port `parse_rco.parse_series_summary` (one regex over the series-page HTML) to C++ to pull RCO's "Summary:" block; if thin (< ~120 chars), fall back to Wikipedia REST (`en.wikipedia.org/api/rest_v1/page/summary/<title>` with the existing suffix-ladder: "comics" → "comic book" → bare). Both are plain HTTP via NetSeam.
3. **Add-to-shelf persistence.** On **Add to Library** from a live-opened Western series, assemble the schema-v2 JSON in memory (seriesId, seriesTitle, source `"rco"`, seriesCover, synopsis, editions[] with label/href/formatTier; metadata fields left empty per "lean now") and write `data/western_catalogue/<seriesId>.json`, then `refreshWesternGrid()`. Reuses the loader read path verbatim.

## 7. Data flow

```
type query (Western mode)
  └─ ComicsPage::showSearchMode → ComicsTankoyomiSearchWidget::search
       └─ MangaSourceRegistry::find("readcomicsonline")->search   [EXISTS]
            └─ RCO /Search/Comic?keyword=  → QList<MangaResult>     [EXISTS]
  └─ results strip of TileCards (async cover fetch)                 [EXISTS]
pick a result
  └─ fetch /Comic/<slug>  → flat QList<ChapterInfo>                 [EXISTS]
       └─ edition classifier → grouped collected editions          [NEW: pure fn]
       └─ parse RCO Summary (+ Wikipedia fallback) → synopsis       [NEW: regex + HTTP]
  └─ build in-memory schema-v2 JSON → openWesternSeriesFromJson     [EXISTS view]
Add to Library
  └─ write data/western_catalogue/<seriesId>.json                  [NEW: persist]
  └─ refreshWesternGrid → permanent tile                            [EXISTS]
```

## 8. Error handling / edge cases

- **No results / RCO search empty:** results strip shows the existing "no results" empty state (mirror manga). No crash, no spinner-forever — guard on empty `searchFinished`.
- **RCO unreachable / fetch error:** `errorOccurred` already exists on the scraper; surface the existing error/empty affordance, don't hang the loading overlay.
- **Series already on the shelf** (same seriesId): Add is idempotent and **skip-if-present** is the locked default — if `data/western_catalogue/<seriesId>.json` already exists, do not rewrite or duplicate the tile; the **Add** button instead reads "On shelf"/disabled. (Overwrite-to-refresh is a later-richness concern, not this arc.) The loader keys by `seriesId`, so no duplicate tiles regardless.
- **Series page has only single issues** (no collected editions, e.g. some marquee primary slugs): the classifier filters all items out → editions list empty. Show the series page with synopsis but an empty/"no collected editions found" editions state rather than a broken page. (This is the exact case the marquee-slug problem produces; surfacing it honestly is the lean answer.)
- **Reader pages won't load:** out of scope — see §5. Edition tiles open to the (download-gated) reading path that the downloads arc fills; until then they have nothing to render, consistent with §5.
- **Cover fetch:** reuse the async remote-cover fetch already proven this arc (`TileCard` ctor loads local only; remote needs async `fetchPosterForTile`/`setThumbPath`). Cache key = full hash, not truncated (the `.left(40)` collision bug from `d881d3d` — already fixed, do not regress).

## 9. Testing

- **Edition classifier = pure logic → TDD (GoogleTest, `tankoban_tests`).** Feed representative RCO labels (Compendium / Omnibus / TPB N / Deluxe / Vol N / Issue #N / "(2003) #32" year-trap) → assert tier + collected/excluded. Mirrors `edition_classify.py`'s own cases. This is the one piece worth test-first.
- **Synopsis parse:** small fixture test over a captured RCO series-page HTML snippet → assert Summary extracted; thin-summary → fallback triggers.
- **Everything else = smoke** via the running app (agent-driven, tankoctl/pywinauto): search "Saga" → results paint → open → editions + synopsis render → Add → tile persists across a relaunch. Visual eyeball gate for "looks like the 13."

## 10. Out of scope (deferred, by Hemanth's sequencing — "do 4 once 2 and 3 are right")

- **Full richness** of live-added series (author / publisher / genre / year via Wikidata) — a later richness pass. The JSON schema already carries the fields; live-add leaves them empty for now.
- **Per-edition covers** (RCO gives a series hero only; per-TPB covers need GetComics matching — downloads arc).
- **Download + read** the actual pages — the next arc (GetComics). §5 is the dependency line.

## 11. Engine routing (build phase)

Per the multi-engine-brother default (CLAUDE.md, ratified 2026-06-01): design/deliberation stayed on Opus (this spec). The build phase routes through `scripts/engines/engine.py` on Agent 1's lane:
- **grunt → DeepSeek/Agent 9:** the C++ ports (edition classifier, synopsis-parse, wiring the mode-aware source, persistence write) are well-templated, scoped src/ — DeepSeek-shaped, with a mandatory reviewer pass before master (same pattern that landed richness Half B clean).
- **review → Codex (sparing):** the classifier logic + the persistence write (load-bearing: it mutates the shelf folder) get a sign-off review.
- Opus (me) owns the plan, the stitching, and the final review/merge.

## 12. File pointers

- Search: `src/core/manga/ReadComicsScraper.cpp:57` (search), `:118` (fetchChapters), `:134` (parseChaptersHtml — flat list), `:194` (fetchPages — dead reader).
- Search UI: `src/ui/pages/comics/ComicsTankoyomiSearchWidget.cpp:100` (search, hardcoded source), `:128`/`:146` (results render).
- ComicsPage: `src/ui/pages/ComicsPage.cpp:940` (search bar), `:2957` (showSearchMode), `:2982` (onSearchResultActivated), `~:2310` (openWesternSeriesFromJson), `refreshWesternGrid` (grep), `~:3030` (onDetailBack Western branch).
- Western shelf: `src/core/manga/WesternCatalogLoader.cpp:38` (loadFromFile), `:25` (tierLabel map), `:105` (canonicalDataDir); data at `data/western_catalogue/<seriesId>.json` (schema v2).
- Offline reference (logic to mirror, NOT runtime): `scripts/comics_catalogue/edition_classify.py`, `parse_rco.py:parse_series_summary`, `wikipedia_fallback.py`.
- Richness gate already in the view: `src/ui/pages/comics/ComicsSeriesView.cpp` `source=="rco"` Western-only branch.
