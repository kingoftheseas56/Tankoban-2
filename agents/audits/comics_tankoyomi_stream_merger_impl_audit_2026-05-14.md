# COMICS_TANKOYOMI_STREAM_MERGER Implementation Audit

By Agent 7 / Codex, Trigger C audit, 2026-05-14.

Scope: post-implementation read-only audit of the dirty working-tree implementation against `docs/superpowers/specs/2026-05-14-comics-tankoyomi-merger-brainstorm.md`, `docs/superpowers/plans/2026-05-14-comics-tankoyomi-merger.md`, and the phase RTC trail at `agents/chat.md:3766-3782`. No build or smoke was run.

## Executive Summary

Findings: 15 total: P0 = 2, P1 = 9, P2 = 4.

Vision-alignment verdict: the broad shape landed: standalone Tankoyomi is removed, Comics has a Tankoyomi search takeover, a new detail surface, a library record store, provenance chips, per-series download controls, and no cross-series downloads page. The arc is not ready to ship as-is because two central v1 invariants are broken in code: Tankoyomi-origin library tiles do not route to the Tankoyomi detail UI, and `MangaDownloadIndex` is never populated, so downloaded-state validation cannot work.

Most load-bearing items:

- P0-1: the provenance router exists but is bypassed by `TileCard::clicked`, so library Tankoyomi tiles open `SeriesView`, not `ComicsTankoyomiDetailView`.
- P0-2: `MangaDownloadIndex::registerChapter(...)` has no call site, so chapter downloaded markers, manual-delete revalidation, and root-change fallback are inert.
- P1-1: the detail hero cover and library tile cover path were left empty, contradicting the no-placeholder cover requirement.
- P1-3: downloaded chapters are not openable from the Tankoyomi detail view; row/context handlers are stubs.
- P1-6: same-title cross-source folder collisions are not disambiguated.

## Findings

### Vision Alignment

#### P0-1 - Tankoyomi-origin library tiles route to the folder-origin `SeriesView`

Source of truth: brainstorm §3.5 says badged series use the new UI while folder-imported series keep `SeriesView` (`docs/superpowers/specs/2026-05-14-comics-tankoyomi-merger-brainstorm.md:116`); §20 locks detail-from-tile Back routing (`...brainstorm.md:551-553`); smoke step 5 opens the new tile and expects the detail view (`docs/superpowers/plans/2026-05-14-comics-tankoyomi-merger.md:3198`).

Observed state: `ComicsPage::addSeriesTile` connects every tile to `onCardClicked` (`src/ui/pages/ComicsPage.cpp:642`). `onCardClicked` always calls `m_seriesView->showSeries(...)` (`src/ui/pages/ComicsPage.cpp:1051-1058`). The intended provenance router exists in `onTileClicked` and would call `m_tyDetailView->showEntry(...)` for claimed canonical paths (`src/ui/pages/ComicsPage.cpp:834-855`), but no current connection calls it.

Deviation: the implementation matches the data-model standard for storing provenance, but deviates from the navigation/UI standard: the user's primary library path into a Tankoyomi-origin series opens the folder-imported UI.

Recommended fix: wire single-click and double-click/list activation through one provenance-aware opener that receives `seriesPath`, `seriesName`, and cover path; delete or repurpose `onCardClicked` so it delegates to the provenance router. Include list-view activation and the Continue-strip "Open series" action if the series path is Tankoyomi-owned. Estimate: 35-60 LOC. Agent 1 can decide; no Hemanth ratification needed.

#### P0-2 - `MangaDownloadIndex` has no producer, so downloaded-state invariants cannot work

Source of truth: brainstorm §3.4 says manual file deletion changes per-chapter on-disk state via live validation (`...brainstorm.md:111`); §3.5 says downloaded chapters remain readable offline (`...brainstorm.md:117`); plan Phase 5 makes `MangaDownloadIndex` the chapter index (`...merger.md:2145-2155`); smoke steps 5 and 9 depend on Queue/Downloading and delete-revalidation (`...merger.md:3198-3202`).

Observed state: `MangaDownloadIndex::registerChapter(...)` is defined at `src/core/manga/MangaDownloadIndex.cpp:143-200`, but `rg "registerChapter\\(" src` finds no call site outside the declaration/definition. The detail view derives `Downloaded` from `filePathFor(...)` at `src/ui/pages/comics/ComicsTankoyomiDetailView.cpp:363-367` and `410-414`; with no producer, those paths remain absent. `MangaDownloader` does record per-chapter `finalPath` on completion (`src/core/manga/MangaDownloader.cpp:491-498`) and emits `chapterUpdated` after completion (`src/core/manga/MangaDownloader.cpp:529-531`), but the new detail/page code does not connect that signal to the index.

Deviation: the index data structure matches the Stream-style shape, but the implementation deviates from the plan by omitting the write path that makes the index authoritative.

Recommended fix: on chapter completion, register `(sourceId, seriesId, chapterId, finalPath, fileSize)` into `MangaDownloadIndex`. The least invasive shape is a Comics-side `chapterUpdated` handler that reads `MangaDownloader::recordForSeries(...)`, finds the matching completed chapter, and calls `registerChapter`; the cleaner shape is extending `MangaDownloader` with a completion signal carrying source/title/chapterId/finalPath, then mapping title to library record once. Estimate: 60-120 LOC. Agent 1 decision; no Hemanth ratification unless the signal contract expands beyond this arc.

#### P1-1 - Detail hero and library tile covers were not implemented

Source of truth: brainstorm §3.2 requires a full hero with cover and says added tiles use the same hero image with no placeholder/shimmer (`...brainstorm.md:95-97`). Plan Task 23 explicitly says preview cover should download into the same cache and set `m_coverLabel` (`...merger.md:1828-1842`). Smoke steps 3 and 4 check cover and new tile visibility (`...merger.md:3196-3197`).

Observed state: `renderPreviewHero` leaves the cover label empty by comment (`src/ui/pages/comics/ComicsTankoyomiDetailView.cpp:279-291`). `renderDetailHero` renders text metadata only (`src/ui/pages/comics/ComicsTankoyomiDetailView.cpp:293-305`). Add-to-library writes `rec.coverPath = QString()` (`src/ui/pages/comics/ComicsTankoyomiDetailView.cpp:650-654`), and `ComicsPage::seriesInfoFromRecord` uses that empty `coverPath` as the tile thumbnail (`src/ui/pages/ComicsPage.cpp:819-829`).

Deviation: the search result cards match the cover-loading standard through the poster cache, but the detail view and library record deviate from the no-placeholder hero/tile-cover requirement.

Recommended fix: extract the poster-cache helper from `ComicsTankoyomiSearchWidget` into a small shared helper or move it into the detail view as a second call path. On `showEntry`, load from cache or download `preview.thumbnailUrl` / `detail.heroCoverUrl`, set `m_coverLabel`, and persist the local cache path into `ComicsLibraryRecord::coverPath` on Add and on detail refresh. Estimate: 70-110 LOC. Agent 1 decision.

#### P1-2 - Continue-strip Tankoyomi provenance is missing

Source of truth: brainstorm §3.4 requires the same Tankoyomi badge on Continue Reading tiles (`...brainstorm.md:112`).

Observed state: `refreshContinueStrip` creates `TileCard(item.coverPath, item.title, item.subtitle)` and sets file/series properties, but never calls `setProvenance(...)` (`src/ui/pages/ComicsPage.cpp:1023-1045`). The only provenance call is in library-grid tile creation (`src/ui/pages/ComicsPage.cpp:624-630`).

Deviation: library-grid tiles match the provenance-badge standard; Continue-strip tiles deviate from the same-badge requirement.

Recommended fix: when building `ContinueItem`, or immediately before adding the card, check `m_tyLibrary->getByCanonicalPath(item.seriesPath)` and call `card->setProvenance("tankoyomi")` on a hit. Route Continue-strip "Open series" through the same provenance-aware opener from P0-1. Estimate: 15-30 LOC. Agent 1 decision.

#### P1-3 - Downloaded chapters are not openable from the Tankoyomi detail view

Source of truth: brainstorm §5.3 says clicking a done chapter opens it in `ComicReader` (`...brainstorm.md:195`); §3.5 says already-downloaded chapters remain fully readable while offline (`...brainstorm.md:117`).

Observed state: `openComicRequested` exists in the detail-view API (`src/ui/pages/comics/ComicsTankoyomiDetailView.h:48-52`) and is connected by `ComicsPage` (`src/ui/pages/ComicsPage.cpp:122-125`), but the detail view never emits it. `onChapterRowClicked` and `onChapterContextMenu` are stubs (`src/ui/pages/comics/ComicsTankoyomiDetailView.cpp:418-426`). `onIndicatorClicked` always queues a new download after ensuring the record exists (`src/ui/pages/comics/ComicsTankoyomiDetailView.cpp:533-560`), with no branch for an already-downloaded chapter.

Deviation: the detail table matches the visual chapter-list standard, but deviates from the reader handoff and offline-readable standards.

Recommended fix: in row click and indicator click, check `m_downloadIndex->filePathFor(...)`. If present, build the current series CBZ list from the record folder and emit `openComicRequested(path, list, title)`; if absent and source is online, queue download; if offline, disable and show tooltip per P1-4. Estimate: 60-100 LOC. Agent 1 decision.

### Codex §11-§21 Self-Audit

#### P1-4 - Offline-source behavior stops at banner-only

Source of truth: brainstorm §3.5 requires cached cover/title/meta, readable downloaded chapters, and disabled greyed not-yet-downloaded chapters while offline (`...brainstorm.md:117`). Codex §13 says detail cache order should prevent a tile-with-only-title cold-start (`...brainstorm.md:391-405`).

Observed state: `onSourceError` only sets the banner visible (`src/ui/pages/comics/ComicsTankoyomiDetailView.cpp:331-334`). It does not mark the view offline, grey rows, disable `ChapterDownloadIndicator`, set tooltips, or block download attempts. The chapter click/context handlers are stubs (`src/ui/pages/comics/ComicsTankoyomiDetailView.cpp:418-426`).

Deviation: the banner matches the standard, but the disabled/not-downloaded behavior and offline readable path deviate from §3.5.

Recommended fix: add an `m_sourceOffline` flag reset in `showEntry` and set in `onSourceError`; in `refreshChapterMarkers`, apply disabled state/tooltips to rows with no local file when offline. Ensure downloaded rows open through P1-3. Estimate: 50-90 LOC. Agent 1 decision.

#### P1-5 - Auto-add toast fires even when Add failed

Source of truth: Codex §15 says the detail view should show "Added <title> to your library" only if `ensureAdded` created a record, before `startDownload(...)` (`...brainstorm.md:448-455`). Phase 5 RTC also states the empty-root guard prevents "Added" followed by no-op download (`agents/chat.md:3774`).

Observed state: both `dispatchDownload` and `onIndicatorClicked` set `didAdd = true` immediately after calling `onAddRemoveClicked()` (`src/ui/pages/comics/ComicsTankoyomiDetailView.cpp:517-525` and `542-550`). `onAddRemoveClicked` can fail early when there is no Comics root and only shows "Add a Comics folder before adding to library" (`src/ui/pages/comics/ComicsTankoyomiDetailView.cpp:633-642`). The caller still shows "Added <title> to your library" and then returns because `rec.canonicalSeriesPath` is empty (`src/ui/pages/comics/ComicsTankoyomiDetailView.cpp:526-530` and `552-560`).

Deviation: the ordering standard is mostly matched, but the "created a record" condition is not checked.

Recommended fix: replace `onAddRemoveClicked()` as a side-effecting ensure call with `std::optional<ComicsLibraryRecord> ensureAddedForDownload()` or a bool return from an add-only helper. Toast only after `contains(...)` changed from false to true and the returned record has a non-empty canonical path. Estimate: 25-45 LOC. Agent 1 decision.

#### P1-6 - Same-title cross-source folder collision is not disambiguated

Source of truth: brainstorm §3.7 requires same-title collisions across sources to be disambiguated by source suffix on collision (`...brainstorm.md:133`). Codex §16 says records are keyed by `(sourceId, seriesId)`, not title, and canonical paths remain distinct (`...brainstorm.md:462-481`).

Observed state: Add-to-library always computes `rec.seriesFolderName = sanitiseFilename(m_currentPreview.title)` and `canonicalSeriesPath = root/title` (`src/ui/pages/comics/ComicsTankoyomiDetailView.cpp:650-652`). There is no collision check against existing Tankoyomi records, folder-origin paths, or sidecars before writing the record and sidecar (`src/ui/pages/comics/ComicsTankoyomiDetailView.cpp:662-672`). `m_canonicalToKey` maps one canonical path to one record key, so a second same-title source overwrites the canonical-path lookup (`src/core/manga/ComicsTankoyomiLibrary.cpp:42-52`).

Deviation: identity keys match the record standard, but disk path generation deviates from the collision standard.

Recommended fix: before Add, compute a unique folder name. If `root/title` is already claimed by another `(sourceId, seriesId)` or exists with a nonmatching sidecar, use `title (SourceName)`; if that still collides, append a short series-id hash. Estimate: 35-70 LOC. Agent 1 decision; no Hemanth ratification because the product rule is already explicit.

### Plan Fidelity

#### P1-7 - Search stale-result race remains despite RTC claiming it was closed

Source of truth: Phase 3 RTC says late arrivals from query N-1 become no-ops after disconnect/reconnect (`agents/chat.md:3768`). Plan Phase 3 requires search takeover result correctness (`docs/superpowers/plans/2026-05-14-comics-tankoyomi-merger.md:1154-1549`).

Observed state: `ComicsTankoyomiSearchWidget::search` disconnects this widget's old scraper connections, reconnects, then calls `s->search(query, 60)` (`src/ui/pages/comics/ComicsTankoyomiSearchWidget.cpp:157-178`). `onSearchFinished` accepts every emitted batch without a query token or generation check (`src/ui/pages/comics/ComicsTankoyomiSearchWidget.cpp:195-224`). If an older network reply emits after a new search reconnects, it will be delivered to the new connection and painted into the current result strips.

Deviation: the implementation matches the intent to prevent duplicate receivers, but deviates from the claimed stale-result isolation.

Recommended fix: add `int m_searchGeneration`; capture it in per-search lambdas and ignore completions/errors whose generation no longer matches. Alternatively extend scraper signals with a request id, but a widget-side generation is smaller. Estimate: 25-40 LOC. Agent 1 decision.

#### P2-1 - `ComicsTankoyomiSearchWidget` carries an unused library dependency

Source of truth: plan Phase 3 constructor includes `ComicsTankoyomiLibrary*` only if the search widget needs library-aware result behavior (`...merger.md:1198-1223`); §21 says no separate Saved list and library membership belongs in records, not search-side side effects (`...brainstorm.md:561-564`).

Observed state: `m_tyLibrary` is stored in `ComicsTankoyomiSearchWidget` (`src/ui/pages/comics/ComicsTankoyomiSearchWidget.h:53-55`) and passed in the constructor (`src/ui/pages/comics/ComicsTankoyomiSearchWidget.cpp:29-32`), but is never read in the implementation.

Deviation: harmless at runtime, but it deviates from a clean boundary by implying search has library authority when all Add/Remove behavior lives in the detail view.

Recommended fix: remove the constructor parameter and member unless result cards will intentionally show "in library" state later. Estimate: 5-10 LOC. Agent 1 decision.

### Code Quality

#### P1-8 - List view and double-click paths bypass provenance routing

Source of truth: same as P0-1; folder-origin and Tankoyomi-origin series must route to different inner UIs (`...brainstorm.md:116`).

Observed state: list-view activation always calls `m_seriesView->showSeries(path, name)` (`src/ui/pages/ComicsPage.cpp:489-494`). Tile double-click always calls `SeriesView` too (`src/ui/pages/ComicsPage.cpp:511-518`). Multi-select "Open first selected" also routes to `SeriesView` (`src/ui/pages/ComicsPage.cpp:1166-1171`).

Deviation: single-click routing is already a P0; these secondary activation paths repeat the same deviation and would remain broken if only `TileCard::clicked` is patched.

Recommended fix: centralize all series-open paths behind `openSeriesByPath(seriesPath, seriesName, coverPath, originHint)` and use it from single-click, double-click, list view, multi-select, and Continue-strip "Open series". Estimate included in P0-1 if done centrally; otherwise 25-40 LOC. Agent 1 decision.

#### P2-2 - Search widget uses unscoped stylesheet selectors

Source of truth: AGENTS.md governance reminders require scoped CSS only; brainstorm §21 says use existing theme/style discipline and no color-coded state additions (`...brainstorm.md:557-560`).

Observed state: `ComicsTankoyomiSearchWidget::buildUI` sets a bare `QPushButton` stylesheet for the Show-more buttons (`src/ui/pages/comics/ComicsTankoyomiSearchWidget.cpp:96-104`). Other new selectors in the same file use object names (`#SidebarAction`) at `src/ui/pages/comics/ComicsTankoyomiSearchWidget.cpp:62-69`.

Deviation: UI colors stay gray/white, but selector scoping deviates from repository governance.

Recommended fix: set object names for the two show-more buttons, e.g. `ComicsSearchShowMore`, and scope the stylesheet to `QPushButton#ComicsSearchShowMore`. Estimate: 5-8 LOC. Agent 1 decision.

#### P2-3 - `ComicsTankoyomiLibrary` writes JSON while holding its mutex

Source of truth: plan Phase 2 describes a thread-safe JsonStore-backed store and Phase 2 RTC emphasizes mutex discipline plus off-lock emissions (`agents/chat.md:3766`). Codex §16 makes this file the authoritative provenance source, so store consistency matters (`...brainstorm.md:462-487`).

Observed state: `add`, `remove`, and `relocate` mutate maps and call `save()` inside the `QMutexLocker` scope (`src/core/manga/ComicsTankoyomiLibrary.cpp:42-67` and `147-168`). `save()` iterates `m_byKey` and calls `m_store->write(...)` (`src/core/manga/ComicsTankoyomiLibrary.cpp:31-40`). Emission is off-lock, but disk I/O is not.

Deviation: current code is not a visible bug, but it deviates from the cleaner "snapshot under lock, I/O off lock" pattern used in `MangaDownloadIndex::save()` (`src/core/manga/MangaDownloadIndex.cpp:97-122`).

Recommended fix: have mutators snapshot `m_byKey.values()` under lock, release, then serialize/write; or make a private `saveLockedSnapshot()` that returns a `QJsonObject` while locked and writes after release. Estimate: 20-35 LOC. Agent 1 decision.

### Architectural Soundness

#### P1-9 - Download state is split across `MangaDownloader` and `MangaDownloadIndex` without a synchronization contract

Source of truth: brainstorm §17 says `MangaDownloadIndex` should pattern-reuse Stream's canonical-key index, and §16 says validation should reconcile moved/deleted files (`...brainstorm.md:494` and `480-483`). Plan type consistency names `MangaDownloadIndex::registerChapter/evict.../validateAll/filePathFor` as the cross-component contract (`docs/superpowers/plans/2026-05-14-comics-tankoyomi-merger.md:3255`).

Observed state: downloader completion owns the real final path (`src/core/manga/MangaDownloader.h:17-25`, `src/core/manga/MangaDownloader.cpp:491-498`), while the detail view reads on-disk state only from `MangaDownloadIndex` (`src/ui/pages/comics/ComicsTankoyomiDetailView.cpp:391-415`). No class comment or connection states which component synchronizes them, and no code does it.

Deviation: this is the architectural form of P0-2: the boundary exists, but the owner of synchronization is missing.

Recommended fix: define one synchronization owner. Either `MangaDownloader` owns index registration by receiving a `MangaDownloadIndex*`, or `ComicsPage` owns a small adapter listening to downloader signals. Put the contract in the relevant header comment so future queue-control work does not bypass it. Estimate: included in P0-2 plus 10 LOC comments. Agent 1 decision.

#### P2-4 - Comment rot still names removed Tankoyomi classes as live consumers

Source of truth: Phase 8 removes `TankoyomiPage`, old `MangaDetailView`, `MangaResultsGrid`, and `TransferGroupCard` while preserving only `ChapterDownloadIndicator` and `ChapterRangeDialog` (`agents/chat.md:3780`; brainstorm §18 at `...brainstorm.md:512-517`).

Observed state: `MangaDownloader.h` still says `chapterUpdated` consumers include `MangaDetailView` and `TransferGroupCard` (`src/core/manga/MangaDownloader.h:149-152`). `ComicsPage.cpp` and `.h` still describe the downloader as a "per-page mirror of TankoyomiPage's" and say Phase 8 collapses the duplicate (`src/ui/pages/ComicsPage.cpp:89-93`; `src/ui/pages/ComicsPage.h:159-164`), even though Phase 8 deleted the old page.

Deviation: code behavior is not directly affected, but comments now describe a pre-Phase-8 world and can mislead future maintainers about ownership.

Recommended fix: update comments to name `ComicsTankoyomiDetailView` as the current consumer and remove "Phase 8 collapses" wording. Estimate: 5-10 LOC. Agent 1 decision.

### Missed Opportunities

No P-tier finding on v2 deferrals for root auto-copy, volume/story-arc grouping, source plugins, or title-keyed `recordForSeries`: those were explicit v2 or future-shape calls in the brainstorm/plan. The one collision item that is not a v2 deferral is P1-6 because §3.7 explicitly required source-suffix disambiguation in v1.

The refreshTileChips debounce coalescer remains a correct follow-up, not a finding. The current implementation may refresh often via `downloadUpdated`, but without running smoke or profiling this is only a performance hypothesis.

## Smoke-Matrix Prediction

1. Open Comics: medium bug likelihood. Existing folder tiles should render, but Tankoyomi-origin tiles with empty `coverPath` will show placeholder covers. Continue-strip Tankoyomi badges will be absent if any Tankoyomi-origin item has reading progress.
2. Search `berserk`: medium bug likelihood. Basic takeover likely works, but repeated searches or late network replies can mix stale results into the current view because there is no generation check.
3. Click Manga result: high bug likelihood. Detail title/meta/synopsis may render after network, but the cover will be blank because `renderPreviewHero` intentionally does not load it.
4. Add to library: high bug likelihood. The tile should appear with the Tankoyomi chip, but its cover path is empty. If no Comics root is configured, the UI can show both "Add a Comics folder..." and "Added X to your library".
5. Open new tile and click chapter download: very high bug likelihood. Opening the new tile routes to `SeriesView`, not the Tankoyomi detail view. If reached from search instead, chapter click can queue, but downloaded-state registration will not persist into `MangaDownloadIndex`.
6. Header context menu Pause: medium bug likelihood. The context menu exists on the detail hero, but reaching it from the library tile is blocked by P0-1. If reached from search detail after a download starts, pause depends on title-keyed `recordForSeries`; same-title collisions can target a different active record.
7. Remove keep-files and re-add: medium bug likelihood. The confirmation shape exists and delete default is guarded. Re-add after keep-files can collide with the existing folder path because same-title disambiguation is missing.
8. Force source failure: medium bug likelihood. A toast will fire through the page-level scraper error connections, and the banner will show in detail. The offline row behavior is incomplete: not-downloaded chapters are not greyed/disabled.
9. Manual file deletion: high bug likelihood. `validateAll` runs, but the index has no entries because nothing calls `registerChapter`, so there is no reliable marker to revert; tile badge stays because the library record exists, but per-chapter state is not trustworthy.

## Open Questions for Hemanth

None. The fix list follows the existing brainstorm/plan standards; no product-level call is needed before Agent 1 can patch the P0/P1 items.

## Closing Posture

This arc should hold the merge until P0-1 and P0-2 are fixed. After those, P1-1 and P1-3 are the next most user-visible blockers because the detail page must look like the Stream-blueprint hero and downloaded chapters must be readable from the new UI. The remaining P1/P2 items can ship in the same polish pass or immediately after, but the two P0s are v1 blockers.
