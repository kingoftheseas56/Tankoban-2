# COMICS_WESTERN_RICHNESS — Design Spec

**Date:** 2026-06-01 · **Owner:** Agent 1 · **Status:** design, awaiting plan
**Arc family:** COMICS_WESTERN_CATALOGUE (Phase 5 follow-on)
**Prereq recon:** `scripts/comics_catalogue/RICHNESS_RECON.md` (free-source ceiling, probed)

## 1. Purpose

Western catalogue series currently render as a bare list of collected editions
(TPBs/compendiums) with a cover and title — no synopsis, no credits, no metadata.
This arc gives each Western series **manga-grade series-level richness** —
synopsis + author/creators + publisher + year + genre — surfaced on the series
detail page, **auto-sourced and baked at harvest, with zero signup**.

This is a **stepping stone**, deliberately scoped lean. The "real loop" (search →
add → download → read) is the destination; richness makes the catalogue feel
real on the way there. Do not gold-plate.

## 2. Locked decisions (from brainstorm 2026-06-01)

| # | Decision | Rationale |
|---|---|---|
| D1 | **No signup** (hard line for now) | [[project_western_richness_no_signup_ladder]]; ladder = free-scrape → Agent 7 → signup last resort |
| D2 | Synopsis ← **RCO `Summary:` block primary, Wikipedia fallback** | RCO summary is already in the harvested HTML (zero extra fetch); Wikipedia REST extract covers the gaps |
| D3 | Author/publisher/year/genre ← **Wikidata** | structured, free, no-signup — the closest analog to manga's AniList; clean fields not prose-guessed |
| D4 | Fields surfaced: **synopsis, author/creators, publisher+year, genre/status** | all four, per Hemanth; all free-gettable |
| D5 | **Baked at harvest** (not live-on-open) | mirrors manga's `mangafire_scraper`; the Western catalogue is a curated harvested set |
| D6 | Per-edition covers **deferred to Downloads arc** | both need the same RCO-edition→GetComics-post match; don't build matching twice |

## 3. Why this is work at all (the asymmetry)

Manga inherited richness for free because **AniList** exists — a free, no-signup,
*structured* metadata API covering ~all manga. **There is no free, no-signup
AniList for Western comics** (ComicVine is the equivalent and is signup-gated —
the bottom rung of the ladder). So we synthesize an AniList-substitute from two
scrappier free sources: RCO's own summary text + Wikidata's structured facts.
Same automatic principle as manga; different (messier) sources.

## 4. Architecture — two halves

### Half A — Harvester enrichment (Python, Agent 1)

Extends the existing `scripts/comics_catalogue/` pipeline (currently 13 series,
26 tests green). Per series, at harvest time:

1. **RCO summary parse** — new `parse_rco.parse_series_summary(html)`: extract the
   `Summary:` block from the series-page HTML the harvester *already fetches*
   (no new request). → `seriesSynopsis`.
2. **Wikidata enrichment** — new module `wikidata_enrich.py`:
   - Match series title → Wikidata entity, **disambiguated** by instance-of
     (comic series / comic book / graphic novel types) so "Saga" the comic ≠
     "Saga" the Norse term. Use the Wikidata REST/`wbsearchentities` + a
     `P31` (instance-of) type filter. No API key.
   - Extract: author/creators (`P50` author, `P58` screenwriter, `P110`
     illustrator as available), publisher (`P123`), genre (`P136`), start year
     (`P577` publication date / `P571` inception). Exact property set verified at
     impl; the module isolates this so it's swappable.
3. **Wikipedia fallback** — when the RCO summary is empty or thin (< ~120 chars,
   tunable), fetch the Wikipedia REST summary `extract` for the disambiguated
   title → `seriesSynopsis`.
4. **Bake** — write the enriched fields into the catalogue JSON. Fields map 1:1
   onto the existing `MangaCatalog` schema (§5) — no schema invention.

Politeness: one-time at harvest, batched, existing `time.sleep(1.0)` cadence.
Each source isolated in its own module + pure-logic tested (§7).

### Half B — Render wiring (C++, Agent 2 or 9)

1. **`WesternCatalogLoader`** (`src/core/manga/WesternCatalogLoader.cpp`):
   currently sets only `seriesSynopsis` + `status`. Extend to read `author`,
   `studio`(=publisher), `genres`, `publishedYearStart/End` from the JSON into
   `MangaCatalog`. (All these fields **already exist** on the struct.)
2. **Western series detail header** — the Western open path
   (`ComicsPage::openWesternSeriesFromJson` → `ComicsSeriesView::
   populateVolumeRowsFromCatalog`) renders edition rows but **clears the
   meta-line and paints no about-block**. Add a series **about-block header**
   (synopsis + author + publisher + year + genre) above the editions for the
   catalog-populate path, mirroring manga's series-detail hero. Must NOT route
   through `showSeries`/`dispatchCatalogResolve` (preserves the Guard #3
   no-auto-enrich invariant — Western must never fire AniList/mangafire).

## 5. Data shape

No new fields. The enriched JSON populates existing `MangaCatalog` members
(`src/core/manga/MangaCatalogTypes.h`):

```
seriesSynopsis      <- RCO Summary | Wikipedia extract
author              <- Wikidata P50/P58/P110 (joined)
studio              <- Wikidata P123 (publisher)   [reuse 'studio' slot as publisher]
genres              <- Wikidata P136
publishedYearStart  <- Wikidata P577/P571
status              <- (existing; "FINISHED"/"RELEASING" if derivable, else empty)
```

JSON adds sibling keys to the current `{seriesId, seriesTitle, source,
seriesCover, editions[]}` shape: `synopsis`, `author`, `publisher`, `genres[]`,
`yearStart`, (`yearEnd`, `status` optional). `schemaVersion` bumped; loader
tolerates old files missing these keys (empty → graceful).

## 6. Graceful degradation (cover-tolerant principle)

- RCO summary missing → Wikipedia fallback → else empty synopsis (about-block
  omits the synopsis line, still shows editions).
- Wikidata entity not found / ambiguous (no confident instance-of match) → skip
  structured fields; render what exists. Never block harvest, never broken UI.
- Blockbuster-first scope ⇒ strong coverage for heavy-hitters; long tail
  degrades quietly. Harvester logs per-series what it could/couldn't fill
  (no silent caps).

## 7. Testing

Pure-logic Python tests alongside the existing 26 (`scripts/comics_catalogue/
tests/`):
- `parse_series_summary`: HTML fixture → expected summary text; missing-summary → "".
- `wikidata_enrich` field extraction: mock Wikidata entity JSON → expected fields.
- disambiguation: two same-named entities (comic vs non-comic) → picks the comic.
No network in tests (fixtures/mocks). C++ half verified by `build_check` + a
render smoke (open a Western series, eyeball the about-block).

## 8. Scope fence (explicitly OUT — separate arcs)

- **Per-edition (per-TPB) covers** → Downloads arc (shared GetComics matching).
- **Per-TPB plot synopsis** → retailer-scrape experiment, later (the recon ceiling).
- **Live search / add** → Search arc.
- **Download wiring** → Downloads arc (Task 8).

## 9. Sequencing

Half A (Python) and Half B (C++) are independent until the JSON contract (§5) is
fixed — so the JSON keys are the seam. Land the contract first, then both halves
can proceed in parallel. Keep it lean: ship series-level richness, then move to
the Search and Downloads arcs (the real loop).
