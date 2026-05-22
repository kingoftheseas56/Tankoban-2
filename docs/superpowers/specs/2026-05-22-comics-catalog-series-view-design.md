# Comics Catalog Browser + Series-View Wiring — Design Spec

**Date:** 2026-05-22
**Owner:** Agent 1 (Comics + Tankoyomi domain)
**Arc:** COMICS_TANKOYOMI_STREAM_MERGER
**Brainstorm artifacts:** [.superpowers/brainstorm/12161-1779466989/content/](../../../.superpowers/brainstorm/12161-1779466989/content/) (routing-choice / row-density-v2 / architecture-v2 / volume-row-geometry / volumetile-design)

---

## 1. Goal

Build a browsable Catalog screen for the 107 Fandom-sourced manga catalogs landed on 2026-05-22. Tile click opens the existing Stream-blueprint series detail view (banner + hero + volume list + sources panel) populated from the local catalog JSON. Take every chance to mimic Stream mode's actual implementation — reuse `TileCard` for the grid, build `VolumeTile` as the `EpisodeTile` parallel for volume rows.

**Out of scope:** further catalog ingest, the 20 EXTRACT_FAILED recovery, the Yandex-search auto-discovery follow-up, the parser refactor commits.

---

## 2. Anchor decisions (ratified during brainstorm)

| # | Decision | Why |
|---|----------|-----|
| 1 | **Routing** — catalog tile click builds a `MediaPreview` from the catalog JSON and calls existing `ComicsSeriesView::showSeries(MediaPreview)`. The local-first short-circuit in `ComicsPage::dispatchFandomResolve` handles volume row population. | Reuses the entire Stream-blueprint surface ([ComicsSeriesView.h:74-90](../../../src/ui/pages/comics/ComicsSeriesView.h)) without building a parallel view. |
| 2 | **Catalog grid tile shape** — cover-emphasis. Use shared `TileCard` (`DEFAULT_WIDTH=200, DEFAULT_IMAGE_HEIGHT=308`), not a private `CatalogTile`. | Stream mimicry directive. TileCard already powers 6 page contexts. |
| 3 | **Empty stubs hidden** — the 20 EXTRACT_FAILED catalog JSONs are filtered out at load time in `ComicsCatalogScreen::loadAllCatalogs`. | Cleanest UX. Empty stubs aren't actionable; resurfacing them is a future-Yandex-rediscovery problem. |
| 4 | **Sort default** — alphabetical by `seriesTitle`. | Predictable. |
| 5 | **Hero fallback** — empty title strip when AniList has no banner. No volume-cover-as-banner trick. | No fake content. |
| 6 | **Volume row geometry** — 80×120 cover, 130px row height. Honors "bigger covers" memory without 168px row height. | ~5 rows above the scroll fold vs today's 3. Covers stay readable as manga covers. |
| 7 | **Full Stream mimicry, phased** — Phase 1 lands the Catalog button working end-to-end with existing volume table; Phase 2 builds `VolumeTile` widget and swaps the `QTableWidget`. | Phase 1 ships the visible feature this week. Phase 2 is peak-mimicry follow-on without blocking Phase 1's smoke. |
| 8 | **Identity passing** — pass `anilistId` from the catalog JSON when `> 0`, fall back to title-hint via `m_localCatalogIndex.slugForSeriesTitle`. | Mirrors the dual-path logic already in `dispatchFandomResolve`. |
| 9 | **NavHistory** — `Mode::Catalog` layer already exists ([ComicsPage.cpp:2336-2367](../../../src/ui/pages/ComicsPage.cpp)). Back from series-detail returns to Catalog grid (not Library). | Matches `showSearchMode` pattern. |
| 10 | **Library / Force-refresh / Sources panel** behavior unchanged on catalog entry. | Reuse pays off. |

---

## 3. Phase 1 — Catalog grid via TileCard

### What you see

- Click "Catalog" button in the Comics search bar → grid of 107 tiles, 6 per row at typical window width, each tile is a cover (110×150 within the 200×308 TileCard frame), one-line title underneath, "N vols" caption.
- Tiles sorted alphabetically by series title.
- Cover loading goes through the same 3-layer PosterCache that Stream's catalog uses ([src/core/PosterCache.h](../../../src/core/PosterCache.h)): memory hit (instant), disk hit (PosterCache's per-app disk dir ~ms), network fetch (background). Same singleton instance Stream uses — accessed via `PosterCache::instance()` per existing convention in [CatalogBrowseScreen.cpp](../../../src/ui/pages/stream/CatalogBrowseScreen.cpp). No new ctor parameter on `ComicsCatalogScreen` — singleton lookup at construction time.
- Click any tile → swaps to existing ComicsSeriesView. Banner + hero + ranked tags come from AniList (background fetch). Volume table populates IMMEDIATELY from the catalog JSON (local-first short-circuit hits before network).
- Back button on series detail → returns to Catalog grid (NavHistory layer).
- Force-refresh button on series detail → unchanged: invalidates `FandomCatalogCache` + re-resolves.

### What happens (control flow)

```
User clicks Catalog button
  → ComicsPage::showCatalogMode()                      [existing]
  → m_catalogScreen->refresh()                          [existing]
    → loadAllCatalogs(): scan data/fandom_catalog/*.json
    → filter: skip files where volumes[] is empty (the 20 stubs)
    → sort: by seriesTitle (case-insensitive)
    → for each populated catalog:
        - construct TileCard with cover URL + title + "N vols"
        - PosterCache::ensure(coverUrl) → cache hit OR network fetch
        - connect TileCard::clicked → onCatalogTileActivated(seriesId, title, anilistId)

User clicks a tile
  → ComicsCatalogScreen::tileClicked → emits seriesActivated(seriesId, seriesTitle, anilistId)
  → ComicsPage::onCatalogTileActivated(seriesId, seriesTitle, anilistId)   [NEW lambda body]
    - if anilistId > 0:
        build minimal MediaPreview{ id: anilistId, title: seriesTitle, ... }
        m_seriesView->showSeries(preview)
    - else:
        build MediaPreview with title only; AniList side may not resolve
        m_seriesView->showSeries(preview)
    - showSeries internally calls dispatchFandomResolve(seriesId, qidHint, titleHint)
      - local-first branch finds data/fandom_catalog/<seriesId>.json synchronously
      - emits resolved(FandomCatalog) → populateVolumeRowsFromFandom
      - volume table renders IMMEDIATELY (today's 168px row shape — Phase 1)
    - AniList background fetch fills banner / hero / ranked tags asynchronously
```

### How we know it works

| Check | Method |
|-------|--------|
| Catalog grid renders 107 tiles | `tankoctl comics-catalog-snapshot` (or visual smoke if dev-bridge cmd doesn't exist yet) |
| Empty stubs hidden | Count tiles in grid; expect 107 not 127 |
| Sort is alphabetical | Eye scan: A's first, Z's last |
| Tile click opens series detail | Visual smoke: click "One Piece" tile → ComicsSeriesView swaps in |
| Volume table populates from catalog | Visual smoke: vol 1 row shows "Romance Dawn", chapter range 1-8 |
| Back returns to catalog grid (not library) | Click back → grid visible, not Library |
| Cover thumbnails persist across restart | Restart Tankoban, click Catalog → tiles appear immediately (PosterCache disk hit) |
| AniList background fetch fills banner | Wait ~500ms after tile click; banner image appears on hero |

### Files

| File | Status | Change |
|------|--------|--------|
| [src/ui/pages/comics/ComicsCatalogScreen.h](../../../src/ui/pages/comics/ComicsCatalogScreen.h) | EDIT | Drop private `CatalogTile` forward decl; pull in `TileCard.h`. Add `anilistId` to `seriesActivated` signal payload. |
| [src/ui/pages/comics/ComicsCatalogScreen.cpp](../../../src/ui/pages/comics/ComicsCatalogScreen.cpp) | EDIT | Replace CatalogTile construction with `new TileCard(...)`. Use `PosterCache::instance()` (singleton) for 3-layer cover lookup. Filter empty volumes[] at load. Sort by seriesTitle (case-insensitive QCollator). |
| [src/ui/pages/ComicsPage.cpp](../../../src/ui/pages/ComicsPage.cpp) | EDIT | At ~line 2347, replace the stub lambda with `onCatalogTileActivated(seriesId, seriesTitle, anilistId)`. |
| [src/ui/pages/ComicsPage.h](../../../src/ui/pages/ComicsPage.h) | EDIT | Add `onCatalogTileActivated(QString, QString, int)` slot. |

**Estimated LOC:** ~80 added, ~40 deleted (net +40). One new lambda body, one signal-payload expansion, one PosterCache wiring.

---

## 4. Phase 2 — VolumeTile widget + ComicsSeriesView refactor

### What you see

- Series detail volume list is no longer a QTableWidget grid. Rows are full-width `VolumeTile` widgets in a vertical QScrollArea.
- Each row: bulk checkbox · volume number (32px column) · cover thumb (80×120 with provenance pin top-left if non-empty) · content stack (title / `ch X-Y · pages · date` meta / state chip + progress) · action button right.
- Row height 130px. ~5 rows above the scroll fold at typical window.
- State chip values: `Not started` (default gray) / `Queued · #N in queue` (amber) / `Downloading` + progress bar + size readout (blue) / `Downloaded` (green) / `Failed · {reason}` (red).
- Action button matches state: `Download` / `Cancel` / `Cancel` / `Open` / `Retry`.
- Shift+click range-fill on checkboxes (inherits EpisodeTile pattern via [PackListItem.cpp:THEATRE_BULK_PICKER_SHIFT_RANGE](../../../src/ui/pages/stream/PackListItem.cpp)).
- BookWalker cover resolver still wired — when post-download cbz is extracted, the cover thumb in the relevant VolumeTile updates via `setVolumeCoverFromDisk` routed to the right tile by volumeNumber.

### What happens (control flow)

```
ComicsSeriesView::populateVolumeRowsFromFandom(catalog)              [EDIT existing]
  → clear m_volumeTiles list + remove from scroll layout
  → for each volume in catalog.volumes:
      - construct VolumeTile with VolumeTileData{
          seriesId, volumeNumber, title, chapterRange, pages, date, coverUrl
        }
      - VolumeTile subscribes to MangaDownloadIndex::entriesChanged() (the only signal it has — no granular per-entry signal exists)
      - on each fire: VolumeTile re-reads its own (sourceId, seriesId, volumeNumber) via entryForSeriesAndVolume()
        - entry present with cbzPath → setState(Complete)
        - entry absent → setState(NotStarted) (unless a transient state was set via setVolumeStatusText)
      - append to m_volumeTilesLayout

ComicsSeriesView::populateVolumeRows(anilistRows, detail)             [EDIT existing]
  → same VolumeTile loop, but inputs come from anilist::VolumeRow instead
  → ensures AniList-path detail views also get VolumeTile treatment (full mimicry)

ComicsSeriesView::setVolumeDownloadState(volumeNumber, cbzPath, downloaded)
  → m_volumeTilesByVolumeNumber.value(volumeNumber)->setState(...)
  (replaces the QTableWidget cellWidget find-and-update path)

ComicsSeriesView::setVolumeStatusText(volumeNumber, statusText)
  → m_volumeTilesByVolumeNumber.value(volumeNumber)->setStatusText(statusText)

ComicsSeriesView::setVolumeCoverFromDisk(seriesId, volumeNumber, coverPath)
  → existing seriesId-prefix stale-event guard preserved (parses "anilist_<N>" or
    catalog slug prefix against m_currentAnilistId / m_currentSeriesKey;
    drops events for series not currently displayed)
  → m_volumeTilesByVolumeNumber.value(volumeNumber)->setCoverFromDisk(coverPath)

VolumeTile::toggled(bool)                                              [NEW signal]
  → ComicsSeriesView updates m_selectedRows (existing path)
  → if count >= 1: show "Download Selected (N)" button

VolumeTile::openRequested(volumeNumber)                                [NEW signal]
  → ComicsSeriesView emits openVolume(volumeNumber, cbzPath)

VolumeTile::downloadRequested(volumeNumber)                            [NEW signal]
  → ComicsSeriesView routes via existing sources panel dispatch path
```

### How we know it works

| Check | Method |
|-------|--------|
| Volume rows are 130px, cover 80×120 | Visual smoke + pixel measurement |
| ~5 rows visible above scroll fold | Visual smoke at default window size |
| State chips render correct color per state | Force a download → chip flips Not started → Queued → Downloading → Downloaded |
| Shift+click range-fill works | Click vol 1 checkbox; shift+click vol 5; expect vols 1-5 checked |
| Cover thumb updates post-download | Trigger BookWalker resolver via existing path; verify cover swaps on the specific VolumeTile |
| AniList-path detail also uses VolumeTiles | Open a non-catalog series (WeebCentral search result) → same VolumeTile rendering |
| MangaDownloadIndex subscription fires on entry change | Add an entry programmatically; verify subscribed VolumeTile flips to Downloaded |

### Files

| File | Status | Change |
|------|--------|--------|
| [src/ui/pages/comics/VolumeTile.h](../../../src/ui/pages/comics/VolumeTile.h) | NEW | `VolumeTileData` struct (identity + display fields), `VolumeTileState` struct (state + progressPct + provenance), `VolumeTile` QFrame class. Mirror EpisodeTile.h structure exactly. |
| [src/ui/pages/comics/VolumeTile.cpp](../../../src/ui/pages/comics/VolumeTile.cpp) | NEW | Layout, paint, signal wiring. MangaDownloadIndex subscription. ~400 LOC. |
| [src/ui/pages/comics/ComicsSeriesView.h](../../../src/ui/pages/comics/ComicsSeriesView.h) | EDIT | Replace `m_volumesTable` (QTableWidget*) with `m_volumeTilesLayout` (QVBoxLayout*) + `m_volumeTilesByVolumeNumber` (QHash<int, VolumeTile*>). Drop QTableWidget include. |
| [src/ui/pages/comics/ComicsSeriesView.cpp](../../../src/ui/pages/comics/ComicsSeriesView.cpp) | EDIT | Rewrite `populateVolumeRows`, `populateVolumeRowsFromFandom`, `setVolumeDownloadState`, `setVolumeStatusText`, `setVolumeCoverFromDisk`, `onVolumeCellClicked`, `onVolumeCurrentChanged`, `onVolumeCheckboxToggled` to operate on VolumeTile rows instead of QTableWidget cells. Drop QTableWidget construction + setRowHeight calls. |
| [CMakeLists.txt](../../../CMakeLists.txt) | EDIT | +1 entry for VolumeTile.cpp. |

**Estimated LOC:** ~400 new (VolumeTile), ~150 edited (ComicsSeriesView), ~10 added to header. Larger blast radius — touches BookWalker cover-resolver wiring, library button focus mgmt, bulk-select state.

---

## 5. State machine — VolumeTile

```
NotStarted ──Download──> Queued ──dispatch──> Downloading ──finalize──> Complete
                            │                       │
                            └──Cancel──┐            └──Cancel──┐
                            └──error───┴──> Failed             │
                                              │ ──Retry──> Queued
                                              │
                                              └──user dismiss──> NotStarted
```

| State | Source-of-truth |
|-------|-----------------|
| NotStarted | Absence of entry in MangaDownloadIndex + no active dispatch |
| Queued | Dispatched but `setVolumeStatusText("Queued · #N")` from dispatcher |
| Downloading | `setVolumeDownloadState(volumeNumber, "", false)` + `setVolumeStatusText("Downloading · N%")` from dispatcher |
| Complete | MangaDownloadIndex entry exists for (sourceId, seriesId, volumeNumber) with cbzPath |
| Failed | `setVolumeStatusText("Failed · {reason}")` from dispatcher; no MangaDownloadIndex entry |

---

## 6. Provenance values

| Value | When |
|-------|------|
| `""` (empty, no chip painted) | Unknown / not specified |
| `"Fandom"` | Volume rows populated from local Fandom catalog JSON |
| `"Tankoyomi"` | Volume rows populated from Tankoyomi WeebCentral source |
| `"LocalScan"` | Pre-existing cbz on disk discovered by LibraryScanner |

VolumeTile paints the provenance pin on the cover top-left, semi-transparent black background, 8px white text. Matches `TileCard::setProvenance` pattern.

---

## 7. Open questions deferred to writing-plans

- **Catalog mode entry surface** — only via Comics search-bar button (today's behavior). If Library mode needs a "Browse Catalog" affordance later, add as Phase 3 polish.
- **Catalog grid Header / filter bar** — Stream's catalog browser has a filter row. Comics catalog has one corpus, no filters needed for v1. If we add a "Show only downloaded series" toggle later, that's a Phase 3 polish.
- **VolumeTile bulk-action shape** — Phase 2 inherits EpisodeTile's bulk-select behavior. The bulk-download UI shape (top of list vs floating) carries over from ComicsSeriesView's existing `m_downloadSelectedBtn` pattern; no new design.
- **Stream's `setStreamDownloadIndex` parallel** — VolumeTile takes `MangaDownloadIndex*` via setter post-construction (matches EpisodeTile's pattern).

---

## 8. Risk + rollback

- **Phase 1 risk:** low. New lambda body + PosterCache wiring + grid filter. Rollback = revert one commit.
- **Phase 2 risk:** medium. ComicsSeriesView surgery touches multiple wired paths (BookWalker, library, bulk-select, status text, cover-from-disk). Smoke matrix must cover all 3 series identity paths (AniList-cached, WeebCentral search result, catalog tile click). Rollback = revert ComicsSeriesView.cpp delta + remove VolumeTile.{h,cpp}; QTableWidget path can be restored mechanically.

---

## 9. Done definition

**Phase 1 done:**
- Catalog button click renders 107-tile grid
- Tiles use shared TileCard, sorted alphabetical, empty stubs hidden
- Cover thumbnails persist across restart via PosterCache
- Click any tile → ComicsSeriesView opens with volume table populated from JSON
- Back returns to Catalog grid
- Build green, no regressions to existing AniList / WeebCentral paths

**Phase 2 done:**
- All series detail views (catalog / AniList / WeebCentral) render VolumeTile rows
- Row height 130px, cover 80×120, 5+ rows visible above fold
- All 5 states render correct chip + action button
- Shift+click range-fill works
- BookWalker cover resolver updates the correct VolumeTile post-download
- MangaDownloadIndex subscription fires correct state changes
- Build green, smoke matrix passes for all 3 series identity paths
