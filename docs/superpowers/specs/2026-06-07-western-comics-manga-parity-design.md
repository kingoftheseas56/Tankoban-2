# Western Comics → manga-parity (Library + Continue Reading + live search)

**Owner:** Agent 1 · **Status:** approved-to-spec (Hemanth, 2026-06-07) · **Supersedes the browse-grid model of** `2026-06-06-comics-western-issue-based-readallcomics-design.md` (issue-render + download still hold; the catalogue-as-library grid is replaced here).

## Why (context anchor)

This session Hemanth said: *"comic mode is still uncoooked, it needs to function like the manga mode. continue reading, library and all those things."* Then, after a first (wrong) fix attempt: *"nothing changed. and invincible is in manga continue library. comic library still has the placeholder series i never added to library."*

Two ground-truth problems were confirmed in code:

1. **The Western tab is a catalogue dump, not a library.** `ComicsPage::refreshWesternGrid()` (`ComicsPage.cpp:2904`) loads **every** `data/western_catalogue/*.json` (14 shipped files: chew → watchmen) into `m_westernGrid`. "Add to library" (`addWesternToLibraryRequested`) writes to the **same** `WesternCatalogLoader::canonicalDataDir()`. So the shipped catalogue and the user's library are the same folder → every curated series shows as "added." That is the "placeholder series I never added" complaint.
2. **Western has no Continue Reading and no library/browse separation.** The first fix attempt (this session, reverted) tried to wire western issues into the **manga** Continue Reading / Library strips by removing the June-6 `readallcomics` filters — which made Invincible leak into the **Manga** tab. Wrong direction; reverted clean.

The fix: give Western its **own** manga-shaped surfaces (Continue Reading + My Library), keep Manga isolated, and make discovery search-driven against the working live source (readallcomics; `readcomicsonline.ru` is Cloudflare-locked and returns nothing — that's why the current Western search feels dead).

## Decisions (Hemanth, 2026-06-07, locked via brainstorm)

1. **Tab layout = pure manga parity.** Western tab shows **Continue Reading + My Library only**. The curated catalogue moves *behind search* (not a default shelf).
2. **A series enters My Library two ways (Both):** auto-add when you download any issue, **and** an explicit **+ Add to Library** button on the series view.
3. **Search = live readallcomics** (any western comic, not just the 14).
4. **Empty state = bare** "Search to find comics" (no starter shelf). True parity; an empty Western library looks empty.

## Scope

- **Western comics only.** Manga (Manga tab / Tankoyomi) is untouched and stays volume-first.
- **Manga isolation is preserved.** The June-6 filters that keep `sourceId == "readallcomics"` out of `refreshLibraryStrips()` and `refreshContinueStrip()` **stay**. Western gets its own strips. No leak.
- **Issue-render + download path is unchanged** (built + verified 2026-06-06): open series → `fetchAndRenderWesternIssues` → issue rows → download issue → cbz → tile flips to Read. This spec builds the *library + continue-reading + search* shell around that working core.
- **GetComics path** (`WesternVolumeDownloader`/`GetComicsResolver`) stays compiled but inactive. Nothing deleted.

## What changes for the user

- Open Comics → **Western**: instead of a wall of 14 series you never added, you see **Continue Reading** (western issues you're mid-read) and **My Library** (only series you've downloaded from or hit **+ Add** on). Empty at first, with a "Search to find comics" prompt.
- **Search** any western comic by name (live) → open it → see its issues → download (auto-adds to your library) or **+ Add to Library** to shelf it first.
- **Continue Reading** for western issues now works and lives in the Western tab — never in Manga.
- Manga tab is clean again — no western issues leaking into its Continue Reading or Library.

## Architecture (ground truth + new pieces)

### Current western wiring (unchanged core)
- Search bar built by `buildSearchRow(m_westernSearchBar, …, sourceId="readcomicsonline")` (`ComicsPage.cpp:2829`); submit → `showSearchMode(q)` with active source = that sourceId.
- Open series: `openWesternSeriesFromCatalog(catalog, jsonPath, onShelf)` → `fetchAndRenderWesternIssues` renders live readallcomics issues as rows.
- Download click → `startWesternIssueDownload(...)` → `MangaDownloader::startDownload(source="readallcomics")` → cbz; completion → `onProviderVolumeCompleted(..., WesternGetComics)` registers in `MangaDownloadIndex` and flips the tile.
- `m_pendingWesternSeriesId` / `m_currentDetailSeriesTitle` track the open western series; `m_bridge->dataDir()` is the per-user app-data root.

### New piece 1 — Western library store (`WesternLibrary`)
A small per-user store of "my western series," distinct from the shipped catalogue.

- **Location:** `<m_bridge->dataDir()>/western_library/<seriesId>.json` (writable per-user). The shipped `data/western_catalogue/` stays read-only enrichment.
- **Record shape:** `{ seriesId (slug), title, coverUrl, addedAt, source = "readallcomics", lastReadIssue? }`. Minimal — enough to render a tile and re-open the series.
- **API (thin, testable):**
  - `addOrUpdate(record)` — write/merge the json (used by auto-add + + Add).
  - `remove(seriesId)` — for a future remove-from-library (context-menu parity).
  - `all()` → `QList<WesternLibraryRecord>` — drives My Library render.
  - `contains(seriesId)` / `get(seriesId)`.
- **Rationale:** western has no AniList id, so it can't ride the manga bookmark/downloaded signals. A dedicated slug-keyed store is the smallest thing that separates "mine" from "catalogue." Mirrors the role `ComicsTankoyomiLibrary` plays for Tankoyomi-origin series.

### New piece 2 — My Library view (replaces the catalogue dump)
- `refreshWesternLibrary()` replaces `refreshWesternGrid()`'s data source: load from `WesternLibrary::all()`, **not** the shipped catalogue dir.
- Render one tile per record: cover from `record.coverUrl` (async `fetchPosterForTile`, same remote-url path the grid already uses), title from `record.title`.
- Click / single-click → open that series: `openWesternSeriesFromCatalog` using enrichment (see piece 5) or a header-only catalog built from the record + live issue fetch.
- Empty → "Search to find comics" empty-state label (no catalogue fallback).

### New piece 3 — Auto-add on download
- In `onProviderVolumeCompleted(...)`, `WesternGetComics` branch: after registering the volume, call `WesternLibrary::addOrUpdate({ seriesId = m_pendingWesternSeriesId, title = m_currentDetailSeriesTitle, coverUrl = <current series cover> })`.
- Then `refreshWesternLibrary()` so the tile appears immediately (Stremio/manga reflex: download implies library).

### New piece 4 — Explicit + Add to Library button
- `ComicsSeriesView` already exposes `addWesternToLibraryRequested()` + `setWesternOnShelf(bool)` (`ComicsSeriesView.h:269,116`). Repoint its handler (`ComicsPage.cpp:517`) from "bake json into canonicalDataDir" to `WesternLibrary::addOrUpdate(...)` for the open series; set the on-shelf flag from `WesternLibrary::contains(seriesId)` instead of "file exists in catalogue dir."
- Button shows "Added ✓" / disabled when already in library.

### New piece 5 — Western Continue Reading (own strip)
- Add a western Continue Reading section to the Western page (`buildWesternScreen`): a `TileStrip` above the library grid (`m_westernContinueStrip`).
- `refreshWesternContinueStrip()`: read `m_bridge->allProgress("comics")`, keep only **western issues** (filename matches the `" #\d"` issue pattern — the same marker the manga strip uses to *exclude* them; here we *include* them), unfinished, dedup per series (most-recent issue), sort by `updatedAt`, cap 40.
- Tile: title = series, subtitle = "Issue N · Page X/Y", cover = series cover (from `WesternLibrary` record or curated enrichment). Click → open that issue's cbz via `openComic` (reuse the existing emit).
- Registration: when a western issue is opened to read, register its progress-key → cbz path so the strip can resolve it (a western analogue of `ensureTankoyomiChapterInMap`, scoped to western; map keyed the same way manga's `m_progressKeyMap` is, or a dedicated `m_westernProgressKeyMap`).
- **Manga stays excluded:** `refreshContinueStrip()` keeps its `" #\d"` skip; this is the mirror.

### New piece 6 — Live readallcomics search + enrichment
- Repoint the western search source from `"readcomicsonline"` → `"readallcomics"` in `buildWesternScreen` (`ComicsPage.cpp:2832`). Verify `showSearchMode` routes western queries through `m_readAllComicsScraper` → result cards → open → series view (issues already render live).
- **Enrichment on open:** when opening a searched series whose slug matches a shipped `data/western_catalogue/<slug>.json`, merge the curated `seriesCover` + `seriesSynopsis` (richer art) over the readallcomics metadata. No match → use readallcomics metadata as-is.

## Phasing (smoke each)

- **Phase 1 — My Library.** `WesternLibrary` store + `refreshWesternLibrary()` (replace catalogue dump) + auto-add on download + **+ Add** button + bare empty state. *Smoke:* fresh Western tab is empty with the prompt; download an Invincible issue → Invincible appears in My Library; the other 13 do **not**.
- **Phase 2 — Continue Reading.** Western continue strip + progress registration. *Smoke:* read an Invincible issue, back out → it shows in Western Continue Reading with page progress; Manga Continue Reading stays clean.
- **Phase 3 — Live search + enrichment.** Search → readallcomics live; open a non-curated series and a curated one (Invincible) → curated uses rich art. *Smoke:* search "Saga" → open → issues → download → lands in My Library.

## Data flow

```
Search "Invincible" → readallcomics live → result card → open series
   → fetchAndRenderWesternIssues (live issues)  + curated enrichment (rich art if slug∈14)
   → download issue → MangaDownloader(source="readallcomics") → cbz
        → onProviderVolumeCompleted(WesternGetComics)
             → MangaDownloadIndex.registerVolume   (existing)
             → WesternLibrary.addOrUpdate          (NEW: auto-add)
             → refreshWesternLibrary               (tile appears)
   → + Add to Library button → WesternLibrary.addOrUpdate (NEW: explicit)
   → read issue → progress saved + western progress-key registered
        → refreshWesternContinueStrip (NEW: western CR)
```

## Error / edge handling
- readallcomics unreachable (CF IUAM) → search shows "couldn't reach source," never hangs (existing pattern in `startWesternIssueDownload`).
- Library record with a dead cover URL → tile falls back to text placeholder (TileCard is cover-tolerant).
- Issue #0 / decimal issues → existing rounding rules from the 2026-06-06 spec carry over.
- A series downloaded before this arc (already a cbz on disk, no library record) → first open or a one-shot reconcile adds it to `western_library` so it isn't orphaned. (Phase 1 reconcile: on `refreshWesternLibrary`, also scan `MangaDownloadIndex` for `readallcomics` entries lacking a library record and back-fill.)

## Testing
- Unit: `WesternLibrary` add/update/remove/all/contains round-trip (JSON store), dedup-per-series in the continue-strip builder, the `" #\d"` western-issue classifier (include vs the manga exclude).
- Smoke (dev-bridge + Hemanth visual): the three phase smokes above; `comics-get-library` / `comics-get-downloads` to verify only-added series; introspect-tree for the new sections.

## Manga-isolation invariant (regression guard)
The June-6 filters MUST remain:
- `refreshLibraryStrips()`: `if (e.sourceId == "readallcomics") continue;`
- `refreshContinueStrip()`: skip basenames matching `" #\d"`.
Western surfaces are the mirror (include where manga excludes). A test asserting western issues never appear in the manga strips guards the regression I introduced and reverted this session.

## Definition of Done
1. `build_and_run.bat` → Comics → Western: empty tab shows "Search to find comics"; no 14-series dump.
2. Search "Invincible" (live) → open → issues → download #1 → **Invincible only** in My Library (not the other 13).
3. Read issue #1, back out → Western **Continue Reading** shows Invincible · Issue 1 · page progress.
4. Manga tab Continue Reading + Library show **no** western issues.
5. **+ Add to Library** shelves a series without downloading.
6. Build green; cross-engine review (producer ≠ reviewer) before "done"; Hemanth visual confirm = the gate.
