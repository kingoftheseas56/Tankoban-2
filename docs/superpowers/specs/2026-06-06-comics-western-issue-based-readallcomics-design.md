# Western Comics → issue-based, downloads via readallcomics (2026-06-06)

**Owner:** Agent 1 · **Status:** approved-to-build (Hemanth, 2026-06-06)

## Why (context anchor)

This session: Hemanth flagged Invincible "Volume 1 = the finale" (fixed `24bde55`, forward-order).
Then "GetComics isn't working." Root-caused live: GetComics has **no clean per-volume Invincible
download** — the matched post (Compendium Vol 1) offers only browser-gated mirrors
(Terabox/Mega/Pixeldrain/WeTransfer); Pixeldrain is even network-unreachable here. GetComics only
carries Invincible as compendiums + a whole-series collection, never as 25 individual TPBs.

We considered an embedded-browser downloader (universal, future-proof) — **PARKED** per Hemanth
("we'll think about getcomics later"). Decision instead: **make western comics issue-based and wire
the readallcomics issue-by-issue source in.** readallcomics serves every issue (#1..#144) as plain
blogspot `<img>` pages → cbz; verified live this session (search OK, issue #144 = 55 images).
`readcomicsonline.ru` itself is now Cloudflare-locked (403 challenge) — readallcomics is the working
member of that family; getting `.ru` itself would need exactly the parked browser work.

## Scope (Hemanth-confirmed)

- **Western comics ONLY.** Manga (Manga tab / Tankoyomi) stays **volume-first** — untouched.
- **Browse stays series-level** — you still browse series cards (Invincible, Saga); "issue-based"
  applies *inside* a series.
- Series cover/synopsis still come from our western catalogue; the **issue list + downloads come
  live from readallcomics**.
- GetComics (`WesternVolumeDownloader`/`GetComicsResolver`/`GetComicsParse`) stays compiled but
  **inactive** — nothing deleted, deferred.

## What changes for the user

Open a western series (Invincible) → instead of TPB tiles, see its **issues #1..N** (live from
readallcomics). Click an issue → it downloads that issue as a cbz and opens to read. Downloads work.

## Current wiring (ground truth)

- Browse → open series: `ComicsPage::openWesternSeriesFromCatalog(catalog, jsonPath, onShelf)`
  (`ComicsPage.cpp:2807`) → `m_tyVolumeSeriesView->populateVolumeRowsFromCatalog(catalog)` renders
  the baked **TPB** editions as tiles.
- Download click: `ComicsSeriesView::downloadWesternEditionRequested(volumeNumber, editionTitle,
  tierLabel, sourceHref)` → handler at `ComicsPage.cpp:1027` currently calls
  `m_westernDownloader->requestVolume(...)` (**GetComics**).
- **Dormant + proven** readallcomics path: `ComicsPage::startWesternIssueDownload(seriesTitle,
  issueNumber, editionTitle, volumeNumber, destPath)` (`ComicsPage.cpp:1218`). Pattern:
  `m_readAllComicsScraper->search(title,60)` → `searchFinished(QList<MangaResult>)` → pick best
  category by normalized-title score → `fetchChapters(slug)` → `chaptersReady(QList<ChapterInfo>)`
  → match issue# → `m_mangaDownloader->startDownload(seriesTitle, "readallcomics", {ch}, destPath,
  "cbz")`. Completion already wired: `MangaDownloader::chapterCompleted` with source
  `"readallcomics"` flips the tile to Read (`ComicsPage.cpp:368`).
- `ReadAllComicsScraper` is registered in `MangaSourceRegistry` and cached in
  `m_readAllComicsScraper` (`ComicsPage.cpp:291`).
- Types: `MangaVolume{volumeNumber,titleEnglish,groupingLabel,sourceHref,coverUrlJapanese,...}`,
  `MangaCatalog{seriesId,seriesTitle,seriesSynopsis,seriesCover,volumes,source,...}`,
  `ChapterInfo{id,url,name,chapterNumber(double),source}`.

## The build (3 changes)

### 1. Series view shows readallcomics issues (the core new code)
`ComicsPage::openWesternSeriesFromCatalog`: keep the header/nav/shelf setup. Replace the direct
`populateVolumeRowsFromCatalog(catalog)` (TPBs) with:
- Populate a **header-only** catalog first (copy of `catalog`, `volumes` cleared) so cover/synopsis
  render immediately; set an "Searching readallcomics for issues…" status; `setWesternOnShelf(onShelf)`
  AFTER (it resets the shelf flag).
- New helper `ComicsPage::fetchAndRenderWesternIssues(const MangaCatalog& seriesMeta)`:
  one-shot-connect `m_readAllComicsScraper` `searchFinished`/`chaptersReady`/`errorOccurred`
  (same disconnect-on-terminal pattern as `startWesternIssueDownload`); on `chaptersReady`, build a
  `MangaCatalog issueCat = seriesMeta` with `volumes` = one `MangaVolume` per issue:
  `volumeNumber = qRound(ch.chapterNumber)`, `groupingLabel = "Issue"`, `coverUrlJapanese =
  seriesMeta.seriesCover`, `titleEnglish = ""`; sort ascending by issue number; call
  `m_tyVolumeSeriesView->populateVolumeRowsFromCatalog(issueCat)` then `setWesternOnShelf(onShelf)`.
- Error/empty: leave the existing empty-state label (spec §8) + a "No issues found on readallcomics"
  status.
- Concurrency guard: stamp the active series id; ignore a late `chaptersReady` if the user navigated
  away (compare against `m_pendingWesternSeriesId`).

### 2. Re-point the download click → readallcomics
`ComicsPage.cpp:1027` handler: replace the `m_westernDownloader->requestVolume(...)` block (keep the
dest-path + auto-shelf-add logic) with
`startWesternIssueDownload(seriesTitle, /*issueNumber*/ double(volumeNumber), editionTitle,
volumeNumber, destPath)`. (Rows are now issues, so `volumeNumber == issue number`.)

### 3. Tile label reads "Issue N"
Plumb a unit word so issue tiles don't say "Volume N":
- `VolumeTileData` (`VolumeTile.h`): add `QString unit;` (empty → treated as "Volume").
- `displayTitleForVolume` (`VolumeTile.cpp:122`): use `unit.isEmpty()?"Volume":unit` as the word.
- `populateVolumeRowsFromCatalog` (`ComicsSeriesView.cpp`): set the row/tile `unit` from
  `MangaVolume.groupingLabel == "Issue" ? "Issue" : "Volume"` (carry through the `VolumeRow`
  intermediate). Update the `tr("Volume %1")` sources-context label (`:2531`) to use the unit too.

## Risks / notes
- Live-fetch on series-open adds a readallcomics round-trip (cached by the scraper). Acceptable;
  show a searching state. If readallcomics 403s under load (CF IUAM flagged in the June recipe),
  surface "couldn't reach source" — do NOT hang.
- Download currently re-searches readallcomics (startWesternIssueDownload is self-contained). v1
  keeps that (proven); a later optimization can pass the resolved issue slug from the render fetch.
- Issue #000 → volumeNumber 0 is valid (some series have a #0). Decimal issues (#1.5) round; revisit
  if a series needs fractional rows.

## Smoke (Definition of Done)
1. `build_and_run.bat` → open Comics → Western → Invincible.
2. Series view shows issues #1..#144 (live), labelled "Issue N", in order. (`comics-get-series` via
   tankoctl shows issue rows; introspect-tree shows "Issue 1".)
3. Click issue #1 → status "Downloading…" → a valid cbz lands under Media/Comics/Invincible; tile
   flips to Read; opening it shows real pages.
4. Manga tab unchanged (volume-first). GetComics code still compiles.
5. Build green; cross-engine review (producer≠reviewer) before "done"; Hemanth visual confirm = gate.
