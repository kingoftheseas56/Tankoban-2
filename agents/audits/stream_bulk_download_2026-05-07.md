# STREAM_BULK_DOWNLOAD Audit and Improvement Brainstorm

Date: 2026-05-07  
Author: Agent 7 (Codex)  
Scope: Trigger C audit of `docs/superpowers/specs/2026-05-07-stream-bulk-download-design.md`  
Deliverable pair: `agents/audits/stream_bulk_download_action_plan_2026-05-07.md`

This report is advisory. It challenges architecture and lifecycle choices downstream of Hemanth's locked product decisions Q1-Q5 without reopening those decisions.

## Sources Consulted

Project sources:

- `docs/superpowers/specs/2026-05-07-stream-bulk-download-design.md`
- `src/ui/pages/stream/StreamSourceChoice.{h,cpp}`
- `src/core/stream/StreamAggregator.{h,cpp}`
- `src/ui/pages/StreamPage.{h,cpp}`
- `src/ui/pages/stream/StreamDetailView.{h,cpp}`
- `src/ui/pages/TankorentPage.{h,cpp}`
- `src/core/torrent/TorrentClient.{h,cpp}`
- `src/core/torrent/TorrentEngine.{h,cpp}`
- `src/ui/pages/VideosPage.{h,cpp}`
- `src/core/VideosScanner.cpp`
- `src/core/ScannerUtils.{h,cpp}`
- `src/ui/MainWindow.{h,cpp}`

Reference sources:

- Stremio addon stream response documentation: <https://stremio.github.io/stremio-addon-sdk/api/responses/stream.html>
- Stremio addon subtitles response documentation: <https://stremio.github.io/stremio-addon-sdk/api/responses/subtitles.html>
- Plex TV show naming conventions: <https://support.plex.tv/articles/naming-and-organizing-your-tv-show-files/>
- Jellyfin TV show naming conventions: <https://jellyfin.org/docs/general/server/media/shows/>
- Sonarr media management and naming docs: <https://wiki.servarr.com/sonarr/settings>
- Sonarr import and queue FAQ: <https://wiki.servarr.com/sonarr/faq>
- Radarr media management docs: <https://wiki.servarr.com/radarr/settings>
- qBittorrent Web API: <https://github.com/qbittorrent/qBittorrent/wiki/WebUI-API-(qBittorrent-4.1)>
- libtorrent torrent handle reference: <https://www.libtorrent.org/reference-Torrent_Handle.html>

## 1. Spec Coverage Assessment

### Well Thought Out

- The spec correctly treats bulk season download as a Stream-to-Tankorent handoff, not as a Videos feature. `MainWindow::onAddToTankorentRequested` already routes a single Stream magnet into Tankorent, so extending that precedent is coherent.
- Approach 3 is directionally right: Tankorent owns transfer state, Videos owns scanning, and Stream owns source discovery. The spec avoids making Videos responsible for torrent lifecycle, which would cross ownership lines.
- The pre-flight summary is necessary. The existing Stream path discovers sources at play time through `StreamAggregator::load`, and bulk mode would otherwise hide N independent addon failures behind one button click.
- The spec identifies restart resilience and canonical rename persistence as first-class problems. That is the correct level of concern for a torrent workflow.
- The final library shape `<Show>/Season NN/` is compatible with Plex and Jellyfin conventions. Both reference apps expect show folders with season subfolders and `SxxEyy` episode filenames.

### Under-Specified

- Pack coverage is not specified strongly enough. `detectPackType()` identifies release text shape, not file-list coverage. Stremio's `fileIdx` field is an index into a torrent, and the official docs say that when it is not specified, the largest file is selected. Absence of a file index is therefore not proof that the torrent covers the season.
- The spec does not define a staging/publish boundary. Directly downloading into the Videos library means `VideosPage` can see partially complete folders because it listens for `rootFoldersChanged("videos")`, and `TorrentClient` emits root-folder notifications after completions under category paths.
- The rename lifecycle is too casual. The local engine already exposes `renameFile`, while the spec proposes post-download filesystem rename. qBittorrent exposes torrent file rename as a torrent operation, and libtorrent tracks file paths internally. Renaming files behind an active torrent risks recheck failures, recreated files, or broken seeding state.
- The group persistence model is split across `TorrentInfo.streamGroupId` plus a separate canonical map file. That is not enough for restart-safe group UI, retry-failed, future group shapes, or canonical rename recovery.
- The source sort contract in the spec does not match the current code. `buildPickerChoices()` currently ranks direct streams before magnets, magnet seeders descending, quality descending, and size descending. It does not prefer SDR over HDR/DV and does not prefer smaller size.
- The spec treats per-episode parallel aggregation as straightforward. In current code, each `StreamAggregator` fans out to every stream addon with its own transports and 10 second timeout. Running one aggregator per episode can multiply addon requests quickly and needs throttling/cancellation.
- The spec does not define the identity key for a planned item strongly enough. For stream packs and pack-like per-episode payloads, `infoHash` alone is not unique because different episodes may share one torrent with different `fileIdx` values.

### Load-Bearing but Fragile

- `packCoversAllEpisodes()` is the highest-risk function in the spec. If it accepts a pack based on text classification or missing `fileIdx`, the locked "season pack always wins" rule can choose one large file and skip the rest of the season.
- "No Videos-side code needed" is only true if publishing into the Videos root happens after canonical files are in place. With direct download, current scanner behavior can expose partially named or incomplete artifacts.
- "Canonical maps in separate JSON" is a fragile compromise. It avoids bloating `m_records`, but it creates a second source of truth unless it becomes an explicit group record with lifecycle state.
- Group-level pause/resume/cancel needs more than a UI grouping column. `TankorentPage::refreshTransfers()` currently renders a flat list from `TorrentClient::listActive()`, and context menu actions operate on selected hashes. Group state affects selection, progress aggregation, context actions, restart restore, and retry filtering.

## 2. Locked Decisions Q1-Q5 Reverified

These decisions are product-locked. I am not reopening them. I am flagging implementation constraints where reference apps show proven patterns that the spec should respect.

### Q1. Quality Fallback Cascade: 1080p -> 4K -> 720p -> Any

Observation: The cascade is product-legible and maps well to the current `StreamPickerChoice::qualitySort` model.

Reference behavior: Sonarr and Radarr use quality profiles and custom formats rather than a fixed cascade. They separate "acceptable" from "preferred", then let scoring and cutoff rules pick releases. This is more expressive than Tankoban needs for v1.

Risk: The current picker sort is not the spec's cascade. It ranks by direct/magnet availability, seeders, quality, size, and title. It does not encode SDR/HDR preference. A bulk implementation that reuses picker order will silently violate Q1.

Refinement: Add an explicit bulk comparator. Keep UI picker ordering untouched. The bulk comparator should filter by Q1 tier, then rank by pack/per-episode rule, source kind, seeders, quality score, HDR/DV flag policy, size policy, and title. Mark unknown quality as "any" only after the preferred tiers miss.

### Q2. Per-Season Trigger Only

Observation: The decision is consistent with the existing `StreamDetailView` season-row affordance. A season-level button can use the currently selected season and current episode table snapshot.

Reference behavior: Sonarr exposes series and season monitoring, but a per-season action is a well-known scope for TV automation. Plex and Jellyfin folder conventions are also season-oriented.

Risk: `StreamDetailView` currently owns the visible season selector and episode table, while `StreamPage` owns Stream orchestration and routing. Pulling live widget state from `StreamBulkDownloader` would create a UI coupling.

Refinement: Add a simple immutable season snapshot emitted from `StreamDetailView` to `StreamPage`, then pass that snapshot into the bulk planner. The downloader should not inspect combo boxes or tables.

### Q3. Season Pack Always Wins

Observation: This is the most dangerous locked decision, because it converts pack detection into correctness-critical behavior.

Reference behavior: Sonarr can parse full-season releases, but the import pipeline still maps files to episodes and applies quality/profile rules. It does not treat "looks like a season pack" as sufficient to skip episode-level mapping. Stremio stream payloads do not provide a first-class "this torrent covers all episodes" flag. `fileIdx` is a file index, and missing `fileIdx` means the largest file is selected, not the whole torrent.

Risk: The spec's current `packCoversAllEpisodes()` direction can produce false positives. A season-pack-looking release may be incomplete, mislabeled, a single stitched file, or a folder with extras and missing episodes.

Refinement: Implement "season pack always wins" only after metadata verification proves coverage. Minimum proof should be torrent file metadata containing one accepted video file per expected episode, matched by `SxxEyy`, episode number token, or a strict fallback mapping. If metadata cannot be resolved, the pack candidate should not win; use per-episode fallback and report "pack unverified" in pre-flight.

### Q4. Skip-if-Exists by Canonical Name Only

Observation: This is simple and auditable. It also matches the intended final library convention.

Reference behavior: Plex and Jellyfin rely heavily on stable naming. Sonarr/Radarr import pipelines organize files into canonical destination names and can skip/import based on final paths and library state.

Risk: Canonical-name-only skip needs exact sanitization and collision rules. If the bulk planner and post-download rename disagree on sanitized title or extension, the skip check is wrong. The current `cleanMediaFolderTitle()` is scanner cleanup, not a forward naming policy.

Refinement: Add one pure naming function for show folder, season folder, and episode filename. Use it for pre-flight skip checks, planned destination paths, rename maps, and tests. Preserve the selected file extension. Include show name in filenames: `Show Name - S03E01 - Episode Title.ext`. Plex and Jellyfin both document that pattern, and it makes files portable outside their folder.

### Q5. Group-Level Pause/Resume/Cancel Only; Partial Failures Tolerated; Retry Failed

Observation: Group-level control is a good v1 boundary. It avoids file-level management UI in the first implementation.

Reference behavior: qBittorrent exposes categories/tags for grouping and filtering, while each torrent remains independently actionable. Sonarr/Radarr queue views group by series/movie context but still retain per-item status for import failure handling.

Risk: The spec says group-level only, but "retry failed" requires per-item status. Without persistent item records, Tankorent can pause/resume/cancel a group but cannot know which source-pick failures, metadata failures, duplicate skips, transfer failures, or rename failures are retryable after restart.

Refinement: Keep visible controls group-level, but persist item-level states under the group record. Right-click "Retry failed" should build a new plan from failed item descriptors, not scrape visible table rows.

## 3. Architecture Review

### Approach 3 Is the Right Direction, but the Boundary Is Too Thin

Approach 3 should stay: Stream discovers, Tankorent transfers, Videos scans the final library. The issue is not the ownership split. The issue is that the proposed persistence and lifecycle boundary is underspecified.

`streamGroupId` on `TorrentInfo` is useful as a foreign key. It is not enough as the group model. The group needs durable metadata that is not a torrent:

- `groupId`
- `groupKind` such as `streamSeason`
- display label
- source identifiers such as IMDb/TMDB id, season number, expected episode count
- destination root and final folder
- staging folder if used
- item records keyed by `(infoHash, fileIndex)` or by planned canonical path
- chosen source metadata, including quality, size, seeders, pack/per-episode mode
- canonical rename plan
- current item state and last error
- creation/update timestamps
- retry generation

Recommendation: Treat `streamGroupId` as a foreign key into a `stream_bulk_groups.json` store, not as the group itself. This is still Approach 3. It is just the minimum persistent shape needed for restart safety and future group types.

### `StreamBulkDownloader` Is Doing Too Much

The spec assigns `StreamBulkDownloader` source discovery, planning, pre-flight, canonical map creation, group id generation, add-to-Tankorent handoff, and error aggregation. That class will become a god object before the first retry bug is fixed.

Better split:

- Pure planning helpers: build expected episode list, canonical names, skip decisions, quality fallback, pack coverage evaluation. These are unit-testable without network or UI.
- Source collector: runs StreamAggregator fanout with throttling, cancellation, and result collation.
- Group store: owns persistent group records, canonical maps, item states, and restart reconciliation.
- Tankorent ingest: starts torrents with category, destination, file priorities, group id, and item keys.
- UI presenter: pre-flight dialog and group row rendering.

This can still be implemented with one public `StreamBulkDownloader` facade, but the logic should not all live there.

### Pack Mode Must Be Torrent-Centric, Not Episode-Centric

The current `TorrentClient::isDuplicate()` checks by `infoHash`. If a pack source is represented as repeated episode streams with the same `infoHash` and different `fileIdx`, starting one torrent per episode will collide. The only safe pack implementation is one torrent per pack with multi-file priorities and a file-to-episode rename map.

For per-episode mode, the identity key should still include `fileIndex`. Some addons can return the same `infoHash` for multiple episode-specific streams. A map keyed only by `infoHash` will overwrite or misrename.

### Direct-to-Library Needs a Publish Discipline

The spec's "download to Videos root, then rename" path is weaker than the *arr pattern. Sonarr/Radarr download to a client-controlled area, then import, rename, and publish into the library. That staging boundary prevents partial files from becoming library entries.

Tankoban can keep Hemanth's product result, meaning final files land in Videos and scan with no Videos changes, while still using a staging discipline internally:

- Download into a hidden or non-library staging path under Tankorent, then publish canonical files into `<VideosRoot>/<Show>/Season NN/`.
- Or download into `<Show>/.tankoban-partial/<groupId>/` and move into `Season NN` only after group completion.
- Or, if direct-to-final is required, suppress or delay `rootFoldersChanged("videos")` until group publish completes and avoid canonical renames while libtorrent owns files.

The first option is closest to Sonarr/Radarr and least likely to expose partial files.

## 4. Open Items From Spec Section 11

### 11.1 Subtitle Sidecars

Observation: Current Stream subtitle loading is session-oriented. `SubtitlesAggregator` builds addon requests from `videoHash`, `videoSize`, filename, and stream metadata. Bulk download does not currently persist the selected Stream payload strongly enough to replay subtitle searches later.

Reference behavior: Stremio's subtitle protocol is a separate addon resource, not an automatic property of torrent download. Plex and Jellyfin support external subtitle files when named beside media files, but they do not fetch them for the user.

Current direction: Embedded subtitles only for v1 is correct.

Refinement: Persist enough selected-stream hints in the group item record to enable future sidecar fetch: filename, videoHash, videoSize if known, imdb id, season, episode, language preference, and canonical destination. Do not implement sidecar fetch in v1.

### 11.2 Pause-Then-Prioritize

Observation: `TorrentClient::startDownload()` already accepts `AddTorrentConfig.filePriorities` and applies them before start. That is good for pack mode. The unresolved question is metadata timing and correctness, not UI.

Reference behavior: qBittorrent exposes file priority as a torrent operation. libtorrent supports file priorities at the torrent handle level.

Current direction: The spec's file-priority approach is sound, but priority matching by "episode-looking files" is underspecified.

Refinement: Do not prioritize pack files until metadata proves coverage and maps files to expected episodes. For ambiguous metadata, fail the pack candidate and use per-episode fallback. Persist the chosen file index list in the group record.

### 11.3 Future Non-Stream Bulk Groups

Observation: The spec anticipates batch-add URL lists, movie collections, and comic series but models only `streamGroupId`.

Reference behavior: qBittorrent categories/tags are generic grouping metadata, not feature-specific. Sonarr/Radarr queues retain domain-specific context while download clients remain generic.

Current direction: A string group id on `TorrentInfo` is useful but insufficient.

Refinement: Add a generic group record with `groupKind`. Use `streamSeason` for this feature. Later groups can be `urlBatch`, `movieCollection`, or `comicSeries` without changing the torrent record schema.

### 11.4 Queue Scheduling

Observation: `TorrentClient` already has queue limit APIs. Bulk source discovery and torrent start should not bypass those limits.

Reference behavior: Download clients expose global queue limits, while *arr apps submit batches and let the client enforce active limits.

Current direction: Do not invent a separate bulk scheduler unless existing queue limits prove inadequate.

Refinement: Throttle Stream addon discovery separately from torrent queueing. Use a small source-discovery concurrency window, then submit downloads to `TorrentClient` with the normal queue limits.

### 11.5 Persisted Pre-Flight

Observation: Restarting after pre-flight but before user confirmation only matters if the source plan is expensive to compute or if users expect a draft queue.

Reference behavior: Sonarr/Radarr persist queue/import state after items are grabbed, not every search result draft. qBittorrent persists torrents after add, not unsent add dialogs.

Current direction: Do not persist unconfirmed pre-flight in v1.

Refinement: Persist only after user confirms. If pre-flight source discovery is slow, add an in-memory cache scoped to the detail page session, not disk persistence.

### 11.6 Existing Non-Canonical Files

Observation: Q4 says canonical-name-only skip. That is product-locked.

Reference behavior: Sonarr/Radarr can import existing files by parsing names and matching metadata, but that is a broader library management feature. Plex/Jellyfin rely on good naming and may ignore or misclassify messy files.

Current direction: Canonical-only skip is acceptable for v1.

Refinement: Pre-flight should count "existing canonical", not "episode exists". If a non-canonical possible match exists, surface it as a non-blocking warning only if cheap to detect. Do not skip it automatically.

### 11.7 Failure Notifications

Observation: The spec lists mid-download failure states but does not define where persistent failure messages live.

Reference behavior: Sonarr/Radarr queue/import failures remain visible until resolved. qBittorrent exposes torrent states and errors in the transfer list.

Current direction: Group row plus expanded items is correct.

Refinement: Store `lastErrorCode`, `lastErrorText`, and `failedAt` per item in the group record. UI can show a count on the group row and detailed messages in expansion. Avoid toast-only failures; they vanish on restart.

### 11.8 Auto-Retry Policy

Observation: Auto-retry can be harmful with weak source ranking because it can hammer addons or repeatedly add bad torrents.

Reference behavior: Download clients retry network work internally, while automation apps usually leave failed imports/grabs visible for intervention unless configured.

Current direction: Manual "Retry failed" is the right v1 default.

Refinement: Retry should re-run source selection for failed episodes, not blindly reuse the same magnet unless the failure was transient transfer state. For pack failure, retry should be allowed to fall back to per-episode mode if the pack failed metadata or missing-file validation.

### 11.9 Progress Denominator

Observation: Torrent byte progress and episode count progress answer different user questions.

Reference behavior: qBittorrent emphasizes byte progress per torrent. Sonarr/Radarr queue/import views emphasize item status and completion. For a season group, episode completion is the better group-level denominator.

Current direction: The spec should use episode/item completion as the primary group row count and byte progress as secondary.

Refinement: Display `episodes complete / expected episodes` and aggregate byte progress only across wanted files. For pack mode, do not let extras inflate the denominator.

### 11.10 Rollback on Cancel

Observation: Group cancel without cleanup leaves partial files in the library if downloading directly into final folders.

Reference behavior: qBittorrent can delete files when deleting torrents. Sonarr/Radarr can remove failed/intermediate download artifacts through the download client/import flow.

Current direction: Needs a product call if deleting partial files is user-visible. However, the architecture can reduce the call's blast radius.

Refinement: Use staging. Cancel can safely remove staging artifacts without touching the visible library. If direct final download remains, cancel should default to stop transfers and mark partials, not delete library files, unless Hemanth explicitly approves destructive cleanup.

## 5. Issues Not in Section 11

### A1. `fileIdx == -1` Is Not Pack Coverage

Observation: Stremio documents `fileIdx` as the file index inside a torrent and says that if it is not specified the largest file is selected. Local `StreamAggregator` parses missing `fileIdx` into `-1`. That is an internal sentinel, not a Stremio pack signal.

Gap: The spec currently treats `fileIdx == -1` as evidence that the whole torrent is the stream.

Hypothesis - If implemented as written, some season-pack selections will download only the largest file or a wrong single file while marking the season planned. Agent 4 to validate.

### A2. Direct Videos Publication Can Expose Partial Libraries

Observation: `VideosScanner` groups by first-level subdirectory and recursively scans videos under it. `VideosPage` listens for root-folder changes and triggers scans. `TorrentClient` emits category root changes when completed torrents touch category paths.

Gap: Approach 3 says no Videos-side code is needed, but that is only true after publish. Current direct-to-library behavior can show incomplete or non-canonical files.

Hypothesis - Without a staging or delayed-publish mechanism, Hemanth can see partially completed season folders in Videos before rename and group completion. Agent 4 to validate.

### A3. Filesystem Rename Conflicts With Torrent Ownership

Observation: The local engine exposes `renameFile`, qBittorrent exposes file rename through torrent APIs, and libtorrent tracks file paths in torrent state. Tankoban's own `releaseFolder` path exists because folder renames can conflict with active torrent ownership.

Gap: The spec proposes post-download filesystem rename while the torrent record may still exist.

Hypothesis - Direct `QFile::rename` after download can desynchronize libtorrent state, break seeding, or trigger recreated files on resume/recheck. Agent 4 to validate.

### A4. Canonical Map Keying by InfoHash Is Unsafe

Observation: Stremio streams can identify a file inside a torrent with `fileIdx`. Multiple episode streams can share one `infoHash` with different file indexes. `TorrentClient::isDuplicate()` also operates on `infoHash`.

Gap: The proposed `itemsByInfoHash` shape cannot distinguish two planned items from the same torrent.

Hypothesis - Per-episode groups using repeated `infoHash` values can overwrite canonical mappings or reject later episodes as duplicates. Agent 4 to validate.

### A5. Existing Picker Sort Contract Is Not the Bulk Contract

Observation: `StreamSourceChoice.cpp` currently sorts direct streams first, then magnet seeders, seeders descending, quality descending, size descending, title ascending. It does not implement SDR-over-HDR/DV or size ascending.

Gap: The spec references a bulk ranking policy that does not exist in code.

Hypothesis - Reusing `buildPickerChoices()` order will produce surprising bulk choices and violate the locked Q1 fallback summary. Agent 4 to validate.

### A6. Source Discovery Needs Backpressure

Observation: `StreamAggregator::load()` fans out to all stream addons. One aggregator per episode multiplies requests. AddonTransport has a 10 second timeout but no cross-episode throttle.

Gap: The spec does not define a concurrency limit, cancellation primitive, or addon failure budget.

Hypothesis - Large seasons can temporarily flood addons and make the UI appear stuck for up to the timeout window per wave. Agent 4 to validate.

### A7. Naming Needs a Forward Sanitizer

Observation: `ScannerUtils::cleanMediaFolderTitle()` cleans scanned names for display. It is not a canonical output naming contract.

Gap: The spec does not define Windows reserved names, invalid characters, trailing dots/spaces, duplicate titles, extension preservation, or collisions.

Hypothesis - Canonical skip and post-download rename will diverge on edge-case episode titles unless a single planner-owned sanitizer is introduced. Agent 4 to validate.

### A8. Pre-Flight Disk Space Is Not Enough

Observation: Product chose informational disk-space warnings only. Torrent sizes are sometimes unknown until metadata.

Gap: The spec does not require target path writability, long-path safety, or unknown-size accounting.

Hypothesis - A group can be confirmed with an unwritable destination or misleading size estimate, then fail after partial adds. Agent 4 to validate.

### A9. Group UI Has More Surface Area Than One Row

Observation: `TankorentPage::refreshTransfers()` renders flat rows, and context menus operate on selected hashes. Existing tabs, status labels, info-file view, row selection, and counts are flat.

Gap: The spec frames grouping as mostly a row-rendering change.

Hypothesis - A minimal group row will regress selection, context actions, or detail panes unless the transfer view model is explicitly split into group rows and torrent rows. Agent 4 to validate.

### A10. Restart Reconciliation Is Underdefined

Observation: `TorrentClient` persists active torrent records. A separate canonical map file can be missing, stale, or refer to torrents no longer active.

Gap: The spec says lazy GC after Videos navigation, but group UI and retry need reconciliation before then.

Hypothesis - Restart after partial completion can leave orphan group records, missing canonical rename steps, or impossible retry states. Agent 4 to validate.

## 6. Brainstormed Improvements

### Improvement 1: Make a Bulk Plan the Central Artifact

Instead of starting from "download these magnets", start from a durable `StreamBulkPlan`:

- expected episodes
- existing canonical files
- selected mode: `pack` or `perEpisode`
- source choices
- unresolved episodes
- canonical destinations
- staging destinations
- file priority plan
- rename/publish plan
- warnings

The pre-flight dialog displays this plan. Tankorent ingests this plan. Retry failed derives a new plan from failed items. This removes the hidden mismatch between source selection, skip checks, and rename maps.

### Improvement 2: Use Metadata-Proven Pack Wins

Honor Q3, but define "pack" narrowly:

1. Candidate must be detected as pack-like by source data or repeated `infoHash`.
2. Torrent metadata must be resolved.
3. File list must map to all expected missing episodes.
4. Wanted file priorities must be known.
5. Only then does the season pack win.

If any step fails, report "pack unavailable/unverified" and fall back to per-episode. This keeps the product rule while avoiding false wins.

### Improvement 3: Adopt a Staging-to-Publish Lifecycle

Use a download staging area that Videos does not scan. After group completion, publish canonical files into `<Show>/Season NN/`. This is the single biggest architecture improvement because it solves partial library visibility, cancel cleanup, rename timing, and restart recovery.

If staging is rejected for v1, then the spec needs an explicit direct-final safety contract: no Videos rescan before publish, torrent paused or released before filesystem rename, and group state reconciled before notification.

### Improvement 4: Store a Generic Group Record

Do not create a stream-only sidecar map. Create a generic group record with `groupKind = "streamSeason"` and item records. Keep `streamGroupId` on torrents as a foreign key. This is more future-proof for batch URLs, movie collections, and comic series without making `TorrentInfo` a dumping ground.

### Improvement 5: Use Torrent Rename APIs or Release Before Filesystem Rename

Prefer torrent-aware file rename when libtorrent still owns the torrent. If the desired final state is non-seeding local media, explicitly release/remove the torrent before filesystem publish. The spec should choose one ownership model; it should not mix active torrent ownership with blind filesystem rename.

### Improvement 6: Split Source Collection From Planning

The bulk source collector should only gather candidate choices with throttling and cancellation. Pure planning functions should choose winners. That keeps Q1-Q5 testable without network and prevents UI regressions from addon timing.

### Improvement 7: Show "Unknowns" in Pre-Flight

Pre-flight should not pretend all risk is known. Include:

- episodes already present by canonical name
- episodes selected
- episodes with no source
- pack candidate verified or unverified
- total known size and unknown-size count
- HDR/DV count if detectable
- source failures by addon count
- destination warning if unwritable or low space

This supports Hemanth's locked product decisions without hiding uncertainty.

### Improvement 8: Use File Identity Keys Everywhere

Use keys such as:

- `torrentKey = normalizedInfoHash`
- `fileKey = normalizedInfoHash + ":" + fileIndex`
- `itemKey = seriesId + ":S" + season + "E" + episode`
- `destinationKey = canonicalRelativePath`

Different operations need different keys. A single `infoHash` key is wrong for this domain.

### Improvement 9: Make First Ship Per-Episode Plus Group Store, Not Pack First

The highest-risk feature is pack handling. A coherent first implementation should prove group persistence, canonical naming, direct routing, and group UI using per-episode downloads first, behind a test/dev path or narrow UI flag. Then add metadata-proven pack mode. If Agent 4 insists on pack mode in the first user-visible release because Q3 is locked, then pack mode should be metadata-only from day one.

### Improvement 10: Treat Videos "No Code Needed" as a Verification Claim

The spec can keep no Videos-side implementation, but the action plan must verify it:

- complete group publishes to `<Show>/Season NN/`
- `VideosScanner` groups the show correctly
- partial/staging folders are not scanned
- canonical filenames display as expected
- root-folder notification timing does not expose incomplete files

No code needed is not the same as no testing needed.

## 7. Priority Findings

P0:

- A1: `fileIdx == -1` is not pack coverage.
- A3: post-download filesystem rename can conflict with active torrent ownership.
- A4: infoHash-only canonical maps are unsafe.

P1:

- A2: direct library publication can expose partial seasons.
- A5: picker sort does not match bulk source policy.
- A6: source discovery needs backpressure.
- A9: group UI affects more than row rendering.
- A10: restart reconciliation is underdefined.

P2:

- A7: forward naming sanitizer is missing.
- A8: destination and unknown-size checks are underspecified.
- Subtitle sidecars should remain out of v1 but preserve metadata for later.

