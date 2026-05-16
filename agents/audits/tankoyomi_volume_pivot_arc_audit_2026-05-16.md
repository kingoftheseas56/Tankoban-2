# TANKOYOMI_VOLUME_PIVOT Arc Audit

By Agent 7 (Codex), Trigger C audit, 2026-05-16.

Scope: read-only whole-arc audit of the dirty working-tree TANKOYOMI_VOLUME_PIVOT implementation before the deferred Hemanth smoke session. Compared against the design spec, 13-phase plan, smoke recipe, arc RTC line, current code, and local `StreamDetailView` behavior. No build, smoke, or web research was run; this audit is code-trace only.

## Executive Summary

- Not ready for the 7-case smoke as-is. Smoke 2 can fail before source selection: `AniListClient::seriesById` stores only `totalChapters` and `totalVolumes`, but `AniListVolumeMapper::map` returns zero rows when `detail.chapters` is empty.
- Nyaa runtime dispatch does not tolerate `fileIndex=-1`. There is no metadata-time "pick the only cbz" fallback; priorities are all off, piece matching never triggers, and completion cannot fire.
- The `seriesId="anilist_<N>"` string contract itself is coherent for cover routing, but the completion/index contract around it is incomplete: WC completions are not registered in `MangaDownloadIndex`, and nyaa synth entries carry no chapterIds.
- The WC cover-extractor mirror is mostly faithful: finalize emits `volumeCompleted`, creates `PremiumCoverExtractor`, proxies `coverReady` to `volumeCoverReady`, and `ComicsPage` forwards it to `ComicsSeriesView`. It still depends on rows existing and matching the active series.
- Scoped-stub was reasonable for Phase 7 as a compileable scaffold, but not enough for arc close. The stub omitted load-bearing StreamDetailView behaviors: library toggle, downloaded-state row paint, open-local-volume path, row context menu, and a working row action button.

## Findings

### P0 (blocker, fix before smoke)

#### P0-1 - Series view can render zero volume rows from live AniList data

File: `src/core/manga/anilist/AniListClient.cpp:54`, `src/core/manga/anilist/AniListClient.cpp:274`, `src/core/manga/anilist/AniListClient.cpp:277`, `src/core/manga/anilist/AniListVolumeMapper.cpp:22`, `src/ui/pages/comics/ComicsSeriesView.cpp:361`

What is wrong: `AniListClient::seriesById` queries scalar `chapters` and `volumes`, then assigns only `detail.totalChapters` and `detail.totalVolumes`. It explicitly leaves `detail.chapters` for "callers" to populate. No production caller does that. `AniListVolumeMapper::map` immediately returns an empty list when `detail.chapters` is empty, and `ComicsSeriesView::renderDetail` renders exactly that list into the table.

Evidence: the query requests `chapters` and `volumes` only (`AniListClient.cpp:54-55`), the reply handler assigns totals (`AniListClient.cpp:274-275`), then comments that `chapters[]` is populated elsewhere (`AniListClient.cpp:277-278`). The mapper exits on empty chapters (`AniListVolumeMapper.cpp:22`). The series view sets its row count from the mapper output (`ComicsSeriesView.cpp:361-372`).

Impact: Smoke 2 expects Death Note to show 12 rows. A fresh live fetch has enough scalar data to infer 12 volumes, but the mapper will output zero rows unless the cache already contains synthetic chapter objects from some prior hand-built path. Smoke 3, 4, 5, and 7 depend on volume rows existing.

Suggested fix: before smoke, either synthesize `AniListChapter` entries from `totalChapters` inside `AniListClient::onSeriesReplyFinished`, or make `AniListVolumeMapper::map` synthesize numbered chapters when `detail.chapters` is empty and `totalChapters > 0`. Add one integration-style test that feeds a real `MediaDetail` shape from `AniListClient` with no `chapters[]` and expects Death Note-like volume rows.

#### P0-2 - Nyaa runtime `fileIndex=-1` has no fallback and can starve forever

File: `src/ui/pages/ComicsPage.cpp:1540`, `src/ui/pages/ComicsPage.cpp:1554`, `src/core/manga/TorrentVolumeProvider.cpp:183`, `src/core/manga/TorrentVolumeProvider.cpp:241`, `src/core/manga/TorrentVolumeProvider.cpp:289`, `src/core/torrent/TorrentEngine.cpp:1478`

What is wrong: `ComicsPage::onDownloadDispatchRequested` synthesizes a `PremiumVolumeEntry` with `fileIndex=-1` and a comment saying `TorrentVolumeProvider` treats negative fileIndex as "pick the only cbz from metadata". The provider does not do that. It discards the metadata `files` array, applies priorities using the negative file index as-is, and completion logic rejects negative file indices.

Evidence: `onMetadataReady` explicitly `Q_UNUSED(files)` (`TorrentVolumeProvider.cpp:183-190`). `applyUnionPriorities` initializes `maxIndex` to 0, skips every negative `fileIndex`, and calls `setFilePriorities` with a vector containing only priority 0 (`TorrentVolumeProvider.cpp:251-260`). `onPieceFinished` filters by `pieceStart` and `pieceEnd`, which are still -1 for the synthesized nyaa entry (`TorrentVolumeProvider.cpp:297-298`; synth does not set them at `ComicsPage.cpp:1552-1558`). If completion is checked, `TorrentEngine::fileByteRangesOfHavePieces` returns empty for negative fileIndex (`TorrentEngine.cpp:1491-1493`), and `fileSizeBytes` is also zero by default.

Impact: A clicked runtime nyaa source enters AwaitingMetadata, then likely clears upload-only with all files priority-off. Even if data arrives, no piece range belongs to the request, progress stays 0, `checkFileCompletion` cannot pass, and `volumeCompleted` never fires.

Suggested fix: add a metadata-resolution step before `applyUnionPriorities`: if an in-flight request has `fileIndex < 0`, inspect `files` for exactly one archive candidate, or match by volume number/title, then fill `fileIndex`, `fileSizeBytes`, `pieceStart`, `pieceEnd`, and `cbzFileName` before priority assignment. If that cannot be resolved, emit `volumeFailed("metadata_file_match_failed", ...)` instead of starting the torrent.

#### P0-3 - Synthesized download completions do not update the new volume UI/index contract

File: `src/ui/pages/ComicsPage.cpp:203`, `src/ui/pages/ComicsPage.cpp:322`, `src/core/manga/WeebCentralVolumePacker.cpp:287`, `src/core/manga/TorrentVolumeProvider.cpp:500`, `src/ui/pages/comics/ComicsSeriesView.cpp:421`, `src/ui/pages/comics/ComicsSeriesView.cpp:427`

What is wrong: the post-download UI/index path is incomplete for the two synthesized sources. `ComicsPage` connects provider `volumeCoverReady` signals, but there is no `volumeCompleted`/`volumeProgress`/`volumeFailed` connection for either provider to repaint the visible row. WC packer emits `volumeCompleted`, but nothing registers that file into `MangaDownloadIndex`. Nyaa synth uses `TorrentVolumeProvider`, but its synthesized volume has an empty `chapters` list, so the provider's internal `registerVolume` block is skipped.

Evidence: `ComicsPage` only connects `WeebCentralVolumePacker::volumeCoverReady` (`ComicsPage.cpp:203-210`) and `TorrentVolumeProvider::volumeCoverReady` (`ComicsPage.cpp:322-329`). WC emits completion at `WeebCentralVolumePacker.cpp:287`, but `rg "volumeCompleted" src/ui/pages/ComicsPage.cpp` finds no consumer. Torrent registration occurs only when catalog-derived `chapterIds` is non-empty (`TorrentVolumeProvider.cpp:500-508`); the nyaa synth path appends a volume with no chapters (`ComicsPage.cpp:1552-1558`). The visible table still hardcodes "Not downloaded" and its Download button emits an otherwise-unconnected `openVolume` signal (`ComicsSeriesView.cpp:421-436`).

Impact: Smoke 4/5 pass criteria require row status flips, landing/index updates, and post-completion openability. Catalog entries with chapter refs can register internally; WC and nyaa synth do not have an equivalent index producer. The downloaded CBZ may exist, but the new Comics UI can still show it as "Not downloaded" and fail to surface it on the landing page.

Suggested fix: wire provider completion into one ComicsPage adapter that knows `(source kind, seriesId, anilistId, volumeNumber, chapterIds, finalPath)`: register WC volumes with the same `chapterIds` used for packing; give nyaa synth rows chapterIds from the selected `VolumeRow`; update or re-render the active `ComicsSeriesView` row after index mutation. For nyaa, this should land together with P0-2 because completion is unreachable until file metadata resolves.

### P1 (important, fix soon)

#### P1-1 - Bookmark store exists, but no series-view Add/Remove affordance calls it

File: `src/core/manga/anilist/AniListCache.h:36`, `src/core/manga/anilist/AniListCache.cpp:231`, `src/ui/pages/ComicsPage.cpp:161`, `src/ui/pages/comics/ComicsSeriesView.h:50`, `src/ui/pages/stream/StreamDetailView.cpp:1805`

What is wrong: the cache implements `addBookmark`/`removeBookmark`, and Comics landing renders the BOOKMARKED strip, but no UI call site invokes those methods for the new series view. The design explicitly keeps Add/Remove as a save-for-later bookmark, and StreamDetailView has an in-view library toggle.

Evidence: `AniListCache` exposes bookmark writes (`AniListCache.h:36-39`, implementation at `AniListCache.cpp:231-259`). `ComicsPage` comments assume add/remove comes from `ComicsSeriesView` (`ComicsPage.cpp:161-164`). `ComicsSeriesView` has no cache write method, no bookmark signal, and no button in its header. StreamDetailView's equivalent `refreshLibraryButton` and `onLibraryButtonClicked` are at `StreamDetailView.cpp:1805-1916`.

Impact: Smoke 6 is blocked unless the smoke manually edits `_bookmarks.json`, exactly as the smoke recipe warns. This also means offline bookmarked-series survival cannot be validated through the intended UI.

Suggested fix: add a scoped Add/Remove button to `ComicsSeriesView`, backed by `AniListCache::isBookmarked/addBookmark/removeBookmark`, and refresh it on `showSeries`, `bookmarksChanged`, and detail refetch.

#### P1-2 - Visible volume-row Download button is an orphaned action

File: `src/ui/pages/comics/ComicsSeriesView.cpp:129`, `src/ui/pages/comics/ComicsSeriesView.cpp:432`, `src/ui/pages/ComicsPage.cpp:221`

What is wrong: row click populates the Sources panel, and source-row click dispatches a download. The visible "Download" button in each volume row does something else: it emits `openVolume(volNum, QString())`. `ComicsPage` only connects `downloadDispatchRequested`, not `openVolume`.

Evidence: the constructor comment says the col-6 button emits `openVolume` independently (`ComicsSeriesView.cpp:129-133`). The button lambda does that (`ComicsSeriesView.cpp:432-436`). `ComicsPage` connects only `downloadDispatchRequested` from this view (`ComicsPage.cpp:221-224`); `rg "ComicsSeriesView::openVolume" src/ui/pages/ComicsPage.cpp` has no hit.

Impact: Hemanth can reasonably click the row's visible Download button during Smoke 3/4 and observe no download. The working path is less obvious: click a non-button cell to populate sources, then click a source row.

Suggested fix: either remove/disable the col-6 button until row-state logic lands, or make it select the row and focus/populate the Sources panel. Once index state exists, convert it to a stateful icon like StreamDetailView's action column.

#### P1-3 - Downloaded/open-local volume behavior was not ported from StreamDetailView

File: `src/ui/pages/stream/StreamDetailView.h:97`, `src/ui/pages/stream/StreamDetailView.h:156`, `src/ui/pages/stream/StreamDetailView.cpp:1414`, `src/ui/pages/comics/ComicsSeriesView.cpp:417`, `src/ui/pages/comics/ComicsSeriesView.cpp:421`

What is wrong: StreamDetailView has an explicit download-index dependency, row state resolution, local-play signal, and action-icon repaint. ComicsSeriesView has placeholders for Progress/Status and no MangaDownloadIndex pointer, no open-local signal connection, and no row-state resolver.

Evidence: StreamDetailView exposes `setStreamDownloadIndex` (`StreamDetailView.h:97-102`) and emits local-file playback (`StreamDetailView.h:156-163`). Its action icon state checks the index (`StreamDetailView.cpp:1423-1438`). ComicsSeriesView progress is empty and status is hardcoded "Not downloaded" (`ComicsSeriesView.cpp:417-424`).

Impact: even after a volume lands on disk, the series view cannot present the Stream-like "downloaded means open locally" contract. This is part of the same smoke-visible family as P0-3, but it remains a feature gap after index registration is fixed.

Suggested fix: pass `MangaDownloadIndex` into `ComicsSeriesView` or provide a narrow `setVolumeDownloadState(...)` API from ComicsPage. Row click should branch: downloaded -> emit open CBZ to ComicReader; not downloaded -> populate Sources.

### P2 (minor or deferrable)

#### P2-1 - Catalog cover events can still repaint a stale visible series

File: `src/ui/pages/comics/ComicsSeriesView.cpp:559`

What is wrong: `setVolumeCoverFromDisk` correctly parses `anilist_<N>` and compares it to `m_currentAnilistId`, but non-prefixed catalog seriesIds fall through unconditionally.

Evidence: the stale guard only returns for parsed `anilist_` mismatches (`ComicsSeriesView.cpp:568-573`); all non-prefixed ids continue to `applyPixmapToVolumeRow` (`ComicsSeriesView.cpp:576-584`).

Impact: if a catalog cover extraction finishes after the user navigates to a different series with the same volume number, the wrong row can be repainted until the next render. This does not corrupt disk state, but it is a visible stale-event gap.

Suggested fix: keep a current catalog seriesId/anilistId map in the view, or have ComicsPage convert catalog `seriesId` to anilist id before forwarding the cover event.

#### P2-2 - AniList throttle blocks the caller thread

File: `src/core/manga/anilist/AniListClient.cpp:154`

What is wrong: the client uses `QThread::msleep` inside `throttleIfNeeded`. In this arc, `AniListClient` is constructed on `ComicsPage`'s UI thread, so back-to-back search/open requests can freeze the UI for up to one second.

Evidence: the code comment acknowledges the UI-thread replacement is future work (`AniListClient.cpp:154-157`), and the implementation sleeps synchronously (`AniListClient.cpp:158-165`).

Impact: not a correctness blocker for smoke, but rapid search/result-open sequences can feel like a hang.

Suggested fix: replace with a queued request pump using `QTimer::singleShot`, or move the client to a worker thread before production smoke/perf polish.

### P3 (nit)

#### P3-1 - New code has ASCII-governance drift

File: `src/core/manga/WeebCentralVolumePacker.cpp:153`, `src/ui/pages/comics/ComicsTankoyomiSearchWidget.cpp:52`

What is wrong: new comments/UI strings include non-ASCII glyphs (`section sign` in a comment and a left-arrow button label). The repo's current PowerShell display already shows mojibake in older files, and this arc was asked to keep audit/doc protocol ASCII-clean.

Evidence: `rg -n "[^\\x00-\\x7F]" src/ui/pages/comics src/core/manga/anilist src/core/manga/WeebCentralVolumePacker.cpp src/core/manga/NyaaRuntimeSource.cpp` reports the cited lines.

Impact: low runtime risk. It is a conventions cleanup item before commit sweep if the brotherhood wants strict source ASCII here.

Suggested fix: replace glyph text with ASCII equivalents such as `< Back to library` and `section`.

## Per-Surface Deep Dives

### 1. Synth-entry / `fileIndex=-1` nyaa dispatch

Verdict: not safe. There is no "pick the only cbz" fallback in the current provider.

Trace:

- `ComicsPage::onDownloadDispatchRequested` creates `synth.seriesId = "anilist_<id>"`, copies the nyaa magnet/infoHash, and creates one `PremiumVolumeEntry` with `fileIndex=-1` (`ComicsPage.cpp:1546-1558`).
- `TorrentVolumeProvider::requestVolume` copies that value directly into `Inflight.fileIndex`, plus default zero `fileSizeBytes` and -1 piece range (`TorrentVolumeProvider.cpp:120-130`).
- Metadata arrival does not inspect files (`TorrentVolumeProvider.cpp:183-190`).
- Priority assignment skips negative file indices, so the only priority sent is off (`TorrentVolumeProvider.cpp:251-260`; engine forwards the vector directly at `TorrentEngine.cpp:753-764`).
- Piece-finished handling ignores every real piece because `pieceStart` and `pieceEnd` are both -1 (`TorrentVolumeProvider.cpp:297-298`).
- Completion/progress helpers ask for `fileByteRangesOfHavePieces(..., -1)`, which returns empty (`TorrentEngine.cpp:1491-1493`).

Smoke guidance: avoid runtime nyaa rows until this is fixed. Catalog rows with real catalog file metadata are a different path.

### 2. `seriesId="anilist_<N>"` string contract

Verdict: the string/int parsing contract is coherent where it exists, but it is not enough to make completed synthesized downloads show up in the library.

Trace:

- Synthesis: both nyaa and WC paths use `fallbackSeriesId = "anilist_<id>"` (`ComicsPage.cpp:1476-1477`). WC stores that in `VolumePackRequest.seriesId` (`ComicsPage.cpp:1603-1608`).
- Landing resolution: `ComicsPage::anilistIdForDownloadEntry` strips the `anilist_` prefix and parses the numeric suffix (`ComicsPage.cpp:1103-1107`).
- WC staging: the packer rejects separators, backslashes, `..`, and colons before interpolating `seriesId` into staging/quarantine paths (`WeebCentralVolumePacker.cpp:89-112`), so `anilist_<N>` is filesystem-safe.
- Cover row match: `ComicsSeriesView::setVolumeCoverFromDisk` parses `anilist_<N>` with `QStringView(seriesId).mid(8).toInt(&ok)` and compares it to `m_currentAnilistId` (`ComicsSeriesView.cpp:568-573`).

No string-vs-int mismatch found for WC cover replacement. The gap is producer-side: WC `volumeCompleted` is not registered in `MangaDownloadIndex`, so `anilistIdForDownloadEntry` may never see a WC-packed volume. Nyaa synth also lacks chapterIds for registration.

### 3. Three-stage cover resolution

Verdict: provider wiring is mostly faithful, but the chain is blocked by missing volume rows in P0-1 and incomplete state/index wiring in P0-3.

Trace:

- Stage 1/2: `ComicsSeriesView::renderDetail` chooses `row.art.thumbnailUrl` or `detail.preview.coverThumbUrl` and lazy-loads via the AniList client's NAM (`ComicsSeriesView.cpp:394-405`, `ComicsSeriesView.cpp:478-506`).
- Banner fallback is similarly wired (`ComicsSeriesView.cpp:440-447`, `ComicsSeriesView.cpp:509-536`).
- Torrent provider: `TorrentVolumeProvider` owns `PremiumCoverExtractor`, emits `volumeCompleted`, then calls `extract(finalFile, iff.seriesId, iff.volumeNumber, ...)` (`TorrentVolumeProvider.cpp:512-535`).
- WC provider: `WeebCentralVolumePacker::finalizePack` emits `volumeCompleted`, lazily creates `PremiumCoverExtractor`, proxies `coverReady` to `volumeCoverReady`, and calls `extract(req.destinationPath, req.seriesId, req.volumeNumber, m_coversDir, ...)` (`WeebCentralVolumePacker.cpp:287-315`).
- Page forwarding: `ComicsPage` forwards WC cover events (`ComicsPage.cpp:203-210`) and torrent cover events (`ComicsPage.cpp:322-329`) to `ComicsSeriesView::setVolumeCoverFromDisk`.

The WC mirror is faithful on cover extraction. The cover cannot save the smoke if the table has no volume rows, the wrong row state remains "Not downloaded", or the current displayed series changed and the event is a non-prefixed catalog id.

### 4. P7 scoped stub vs literal fork

Verdict: scoped stub was a good Phase 7 scaffold, but it should not be treated as a complete StreamDetailView fork for smoke.

Correctly dropped or replaced:

- Season picker and episode preselect: manga has no seasons/calendar nav.
- Source-tab widgets: replaced by `ComicsSourcesPanel`.
- Calendar nav: not applicable.
- Meta line: manga's `format + year + status + volumes/chapters + genres` shape is sufficient for smoke; missing runtime/IMDb fields are video-specific.

Left on the floor:

- Library Add/Remove button: Stream has `m_libraryBtn` and full add/remove behavior (`StreamDetailView.cpp:1805-1916`); Comics has no equivalent despite bookmark support.
- Loading states: Stream sets `m_statusLabel` to "Loading..." and hides table surfaces during fetch (`StreamDetailView.cpp:178-186`); Comics shows an empty table until data lands and has no visible loading/error state except synopsis fallback.
- Download state/open-local path: Stream has row state, action icons, local playback, and right-click action menu (`StreamDetailView.h:97-168`, `StreamDetailView.cpp:1307-1385`, `StreamDetailView.cpp:1414-1585`); Comics leaves Progress empty, Status hardcoded, and `openVolume` unconnected.
- Row action ergonomics: Stream's action column is stateful; Comics has a text "Download" button that does not dispatch.

Smoke expectation: Smokes 1-3 may pass only after P0-1 is fixed. Smokes 4-6 will surface the stub's missing state/bookmark behavior unless patched first.

## General Lens

- Signal/slot lifetimes: QPointer use inside cover/banner reply lambdas is good (`ComicsSeriesView.cpp:491-506`, `ComicsSeriesView.cpp:522-536`). Provider cover signals use `Qt::QueuedConnection` before touching UI (`ComicsPage.cpp:203-210`, `ComicsPage.cpp:322-329`).
- Ownership: `m_anilistClient`, `m_anilistCache`, `m_nyaaRuntime`, and `m_weebCentralPacker` are allocated with `this` as parent or otherwise under `ComicsPage`; no leak/double-delete issue found (`ComicsPage.cpp:88-101`, `ComicsPage.cpp:196-197`).
- Esc routing: the inline patch routes Esc from `m_tyVolumeSeriesView` through `onDetailBack`, which clears the detail state (`ComicsPage.cpp:443-453`, `ComicsPage.cpp:1431-1451`). No conflict found.
- Stale requests: the detail fetch guard is sound (`ComicsSeriesView.cpp:306-324`), and source-panel nyaa request ids drop stale results (`ComicsSourcesPanel.cpp:230-237`). Catalog cover stale matching remains weak for non-`anilist_` ids (P2-1).
- Color/style discipline: no colored UI state beyond grayscale found in the inspected new UI. Some stylesheet snippets are inline widget-level color styles rather than object-name scoped QSS; I treated that as lower risk than the functional blockers.

## Scoping Recommendations

- Fix before any smoke: P0-1. Without volume rows, the matrix cannot start.
- Fix before any nyaa-runtime real download: P0-2.
- Fix before Smokes 4/5 are judged: P0-3 plus the row-state part of P1-3. Otherwise a real file can land while the UI still fails the pass criteria.
- Fix before Smoke 6: P1-1.
- Defer safely: P2-1, P2-2, P3-1, unless the smoke session is explicitly broadened to stale-cover or rapid-search polish.

## Hypothesized Root Causes

None. The findings above are direct code observations rather than root-cause claims; Agent 1 owns implementation diagnosis and final fix choices.
