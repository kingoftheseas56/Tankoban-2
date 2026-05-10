# STREAM_BULK_DOWNLOAD Actionable Implementation Pre-Plan

Date: 2026-05-07  
Author: Agent 7 (Codex)  
Companion audit: `agents/audits/stream_bulk_download_2026-05-07.md`

This is not a `/superpowers:writing-plans` output. It is a sequenced implementation pre-plan that incorporates the audit findings into concrete phases, gates, and risks.

## A. Implementation Phases

### Phase 0: Freeze Contracts and Add Pure Planning Types

Goal: Define a `StreamBulkPlan` contract before any UI or torrent lifecycle work.

Files touched:

- `src/ui/pages/stream/StreamBulkDownloader.h`
- `src/ui/pages/stream/StreamBulkDownloader.cpp`
- Possibly a new stream-owned helper such as `src/ui/pages/stream/StreamBulkPlan.{h,cpp}`
- Test files under `tests/` or the existing `tankoban_tests` area

Implementation steps:

1. Define immutable inputs: series id, series title, season number, episode numbers, episode titles, target videos root.
2. Define pure outputs: canonical folders, canonical filenames, existing-skip decisions, missing episodes, source-plan warnings.
3. Define keys: `itemKey`, `torrentKey`, `fileKey`, and `destinationKey`.
4. Add the forward sanitizer used by skip checks and publish paths.
5. Add pure tests for Windows-invalid characters, duplicate titles, extension preservation, `Season NN`, and `Show Name - SxxEyy - Title.ext`.

Verification gate:

- `build_check.bat`
- Opt-in unit run per `CLAUDE.md` Build Quick Reference if available.
- `rg "cleanMediaFolderTitle" src/ui/pages/stream src/core/stream` should not show the bulk planner using scanner cleanup as its output sanitizer.

Risks / unknowns:

- Hemanth may need to approve whether canonical filenames include show name. Plex and Jellyfin conventions support including it; the spec should accept this if product text is not locked.
- Long-path handling may require a project-wide path helper if one already exists.

Audit cross-reference:

- A7, Q4, Improvement 1, Improvement 8.

### Phase 1: Add a Generic Group Store

Goal: Make group state durable without overloading `TorrentInfo`.

Files touched:

- `src/core/torrent/TorrentClient.h`
- `src/core/torrent/TorrentClient.cpp`
- New or existing persistence helper for `stream_bulk_groups.json`
- Possibly `src/ui/pages/TankorentPage.{h,cpp}` for read-only group lookup later

Implementation steps:

1. Add `streamGroupId` to `TorrentInfo` and torrent records as a foreign key only.
2. Create a group record schema with `groupKind`, label, source ids, destination/staging paths, items, canonical names, state, last errors, and retry generation.
3. Load/save group records at startup and on item transitions.
4. Add reconciliation: group references to missing inactive torrents become failed/orphaned items, not silently dropped.
5. Add lazy GC only after reconciliation exists.

Verification gate:

- `build_check.bat`
- Restart smoke with a hand-authored or test-created group JSON record.
- `rg "streamGroupId" src/core/torrent src/ui/pages/TankorentPage.cpp`
- Verify old `m_records` without `streamGroupId` still load.

Risks / unknowns:

- Existing active record schema must remain backward-compatible.
- Group store corruption handling needs a deterministic fallback.

Audit cross-reference:

- A4, A10, section 3, Improvement 4.

### Phase 2: Build Source Collection With Throttling

Goal: Gather episode source choices without flooding stream addons.

Files touched:

- `src/ui/pages/stream/StreamBulkDownloader.{h,cpp}`
- `src/core/stream/StreamAggregator.{h,cpp}` only if cancellation or shared throttle requires it
- `src/ui/pages/StreamPage.{h,cpp}`

Implementation steps:

1. Add a bounded episode discovery queue, for example 3-4 episodes in flight.
2. Create one `StreamAggregator` per active episode, parented for cleanup.
3. Add cancellation that deletes/disconnects active aggregators and drains queued episodes.
4. Capture addon timeout/failure counts as warnings in `StreamBulkPlan`.
5. Avoid reusing `buildPickerChoices()` ordering as the bulk decision policy.

Verification gate:

- `build_check.bat`
- Fake or controlled source smoke with a season of at least 8 episodes; verify only the chosen concurrency window is active.
- Cancel mid-discovery and verify no late signal mutates UI or group state.

Risks / unknowns:

- `StreamAggregator` may need a small API extension for cleaner cancellation, but do not turn it into a bulk-aware class.
- Addon failure messages may be too noisy; aggregate by episode and count.

Audit cross-reference:

- A5, A6, Q1, Improvement 6, Improvement 7.

### Phase 3: Implement Explicit Bulk Selection Policy

Goal: Make Q1-Q3 deterministic and testable.

Files touched:

- `src/ui/pages/stream/StreamBulkDownloader.{h,cpp}`
- `src/ui/pages/stream/StreamBulkPlan.{h,cpp}` if added
- Unit tests

Implementation steps:

1. Implement the Q1 fallback cascade independently of picker row sort.
2. Track HDR/DV flags as warnings or badges, not as invisible sort side effects.
3. Identify pack candidates from release metadata and repeated `infoHash`, but do not accept them yet.
4. Require torrent metadata verification before a pack can win.
5. For per-episode choices, key mappings by `(infoHash, fileIndex)` and item key, not `infoHash` alone.

Verification gate:

- Unit tests for: 1080p found, fallback to 4K, fallback to 720p, any-quality fallback, no-source episode, same-infoHash different-fileIndex, pack-looking but unverified.
- `build_check.bat`
- `rg "itemsByInfoHash" src` should not show infoHash-only item maps for bulk rename state.

Risks / unknowns:

- Metadata resolution timing may force pack verification into Phase 4 if the current API can only resolve metadata through Tankorent.
- Unknown quality strings need conservative classification.

Audit cross-reference:

- A1, A4, A5, Q1, Q3, Improvement 2, Improvement 8.

### Phase 4: Add Torrent Metadata Verification and File Priorities

Goal: Make pack mode safe by proving coverage before add/start.

Files touched:

- `src/core/torrent/TorrentClient.{h,cpp}`
- `src/core/torrent/TorrentEngine.{h,cpp}` only if metadata API needs extension
- `src/ui/pages/stream/StreamBulkDownloader.{h,cpp}`
- Group store files

Implementation steps:

1. Resolve torrent metadata for pack candidates before accepting pack wins.
2. Match video files to expected episodes using strict `SxxEyy` first, then constrained episode-number fallback.
3. Reject pack candidates with missing expected episodes or ambiguous duplicate matches.
4. Build wanted-file priority vectors for accepted packs.
5. Store matched file indexes in the group record.

Verification gate:

- Unit tests for metadata file list mapping.
- `build_check.bat`
- Manual metadata smoke with a known season pack and a known incomplete pack if available.

Risks / unknowns:

- Some torrents expose bad filenames until metadata finishes; the pre-flight may need a spinner state.
- Extras can be large and should not inflate progress denominator.

Audit cross-reference:

- A1, A4, Q3, 11.2, Improvement 2.

### Phase 5: Implement Staging and Publish Lifecycle

Goal: Prevent partial or non-canonical files from appearing in Videos.

Files touched:

- `src/core/torrent/TorrentClient.{h,cpp}`
- `src/core/torrent/TorrentEngine.{h,cpp}`
- `src/ui/pages/TankorentPage.{h,cpp}`
- Group store files
- Possibly a small path/publish helper near torrent or stream bulk code

Implementation steps:

1. Choose staging outside the Videos scanner root if possible.
2. Start torrents with category `videos`, explicit staging save path, group id, and file priorities.
3. On completion, publish canonical files into `<VideosRoot>/<Show>/Season NN/`.
4. Use torrent-aware `renameFile` while the torrent owns files, or release/remove torrent ownership before filesystem moves. Do not blindly rename active torrent files.
5. Emit Videos root-folder change only after publish completes.
6. Persist publish success/failure per item.

Verification gate:

- `build_check.bat`
- Smoke: while downloads are incomplete, `VideosPage` must not show partial group files.
- Smoke: after publish, `VideosPage` detects `<Show>/Season NN/`.
- Restart smoke after download complete but before publish completes; publish resumes or fails visibly.

Risks / unknowns:

- Hemanth may need to approve whether stream bulk torrents are intended to keep seeding after publish. That decision determines rename/release strategy.
- Cross-volume move from staging to Videos may require copy-and-verify rather than rename.

Audit cross-reference:

- A2, A3, 11.10, Improvement 3, Improvement 5.

### Phase 6: Add Tankorent Group Ingest and UI

Goal: Render and control grouped transfers without regressing flat transfers.

Files touched:

- `src/ui/pages/TankorentPage.h`
- `src/ui/pages/TankorentPage.cpp`
- `src/core/torrent/TorrentClient.{h,cpp}`
- Group store files

Implementation steps:

1. Add `addStreamBulkGroupFromExternal(plan)` or equivalent, not a magnet-list-only API.
2. Convert active transfer display into a view model containing group rows and torrent rows.
3. Show group row label, item count, failed count, aggregate wanted-byte progress, and episode completion progress.
4. Add expand/collapse to reveal child torrent rows.
5. Implement group-level pause/resume/cancel by applying actions to child hashes.
6. Implement "Retry failed" from persisted item states.
7. Keep existing single-torrent actions working for ungrouped torrents.

Verification gate:

- `build_check.bat`
- UIA smoke: ungrouped torrent row still appears and context menu still works.
- UIA smoke: grouped season row expands, child rows appear, pause/resume/cancel apply to child hashes.
- Restart smoke: group row restores collapsed/expanded default and failure counts.

Risks / unknowns:

- Existing selection and info-file pane code may assume every table row has one infoHash.
- Aggregate progress must avoid extras and skipped files.

Audit cross-reference:

- A9, A10, Q5, 11.7, 11.9.

### Phase 7: Add Stream UI Entry and Pre-Flight Dialog

Goal: Expose the season download workflow after the backend plan is reliable.

Files touched:

- `src/ui/pages/stream/StreamDetailView.h`
- `src/ui/pages/stream/StreamDetailView.cpp`
- `src/ui/pages/StreamPage.h`
- `src/ui/pages/StreamPage.cpp`
- `src/ui/MainWindow.{h,cpp}`
- Possibly a new pre-flight dialog under `src/ui/dialogs/`

Implementation steps:

1. Add a season-level download action beside the season selector.
2. Emit a season snapshot from `StreamDetailView` to `StreamPage`.
3. `StreamPage` invokes the bulk planner/source collector.
4. Show pre-flight: selected, skipped, no source, pack verified/unverified, known size, unknown sizes, HDR/DV count, destination warnings.
5. On confirmation, route the full plan to Tankorent and switch pages using the existing single-add precedent.

Verification gate:

- `build_check.bat`
- UIA smoke: button appears for series detail and not for movie detail.
- UIA smoke: pre-flight cancel changes nothing in Tankorent.
- UIA smoke: confirm switches to Tankorent and creates one group row.

Risks / unknowns:

- Series metadata may have missing episode titles; naming must fall back deterministically.
- Existing detail view row activation must not change.

Audit cross-reference:

- Q2, Q4, Improvement 1, Improvement 7.

### Phase 8: Failure, Retry, and Restart Hardening

Goal: Make the feature survive real torrent and app lifecycle failures.

Files touched:

- Group store files
- `src/ui/pages/TankorentPage.{h,cpp}`
- `src/core/torrent/TorrentClient.{h,cpp}`
- `src/ui/pages/stream/StreamBulkDownloader.{h,cpp}`

Implementation steps:

1. Define item states: planned, skippedExisting, sourceMissing, metadataFailed, queued, downloading, completed, publishFailed, failed, canceled.
2. Persist state transitions immediately.
3. Retry failed re-runs source selection for source/metadata failures and can reuse magnet for transient transfer failures.
4. Pack failure can fall back to per-episode retry if metadata coverage failed.
5. Add visible stale/orphan state after restart reconciliation.

Verification gate:

- `build_check.bat`
- Restart at three points: after confirm before adds, during transfer, after transfer before publish.
- UIA smoke for retry failed after source-missing and publish-failed simulated states.

Risks / unknowns:

- Some failure simulations may need test hooks or hand-authored JSON.
- Auto-retry should stay out of v1 unless product explicitly changes.

Audit cross-reference:

- A10, 11.7, 11.8, Q5.

## B. Risk Register

| Risk | Severity | Mitigation | Audit Ref |
|---|---:|---|---|
| Missing `fileIdx` treated as pack coverage | P0 | Require torrent metadata coverage before pack wins | A1, Q3 |
| Canonical map keyed only by `infoHash` | P0 | Use `(infoHash, fileIndex)` plus item keys | A4 |
| Filesystem rename while libtorrent owns files | P0 | Use torrent rename APIs or release before filesystem publish | A3 |
| Partial files visible in Videos | P1 | Use staging and publish only after group completion | A2 |
| Bulk source order differs from locked fallback | P1 | Add explicit bulk comparator and tests | A5, Q1 |
| Addon fanout overload | P1 | Bounded source discovery and cancellation | A6 |
| Group row breaks flat Tankorent actions | P1 | Build a transfer view model and smoke both grouped and ungrouped paths | A9 |
| Restart leaves orphan group state | P1 | Reconcile group store against active records on startup | A10 |
| Skip check disagrees with rename output | P2 | Single forward sanitizer and path planner | A7, Q4 |
| Disk or destination failure after confirm | P2 | Pre-flight target writability and known/unknown size warnings | A8 |
| Subtitle sidecar scope creep | P2 | Persist future hints, keep v1 embedded-only | 11.1 |
| Cancel cleanup destroys user-visible files | P1 | Stage outside library; destructive cleanup only in staging | 11.10 |

## C. Test Strategy

### Pure Logic Unit Tests

Run through `tankoban_tests` or the repo's opt-in test build path per `CLAUDE.md`.

Test groups:

- Canonical naming:
  - invalid Windows chars
  - trailing dot/space
  - empty episode title
  - duplicate episode title
  - extension preservation
  - `Show Name - S03E01 - Episode Title.ext`
- Quality fallback:
  - 1080p preferred
  - 4K fallback
  - 720p fallback
  - any fallback
  - unknown quality handling
- Source identity:
  - same `infoHash`, different `fileIdx`
  - missing `fileIdx`
  - direct URL excluded or handled explicitly if bulk supports only magnets
- Pack coverage:
  - full season pack
  - incomplete pack
  - extras-only pack
  - duplicate episode file matches
  - ambiguous episode tokens
- Group record:
  - old torrent records load without group id
  - group store missing child torrent
  - stale child torrent without group store
  - retry generation increments

### Build Gates

Use these after each implementation phase touching compiled code:

- `build_check.bat`
- If sidecar is touched, `powershell -File native_sidecar/build.ps1`
- `rg "streamGroupId" src` after persistence phases
- `rg "itemsByInfoHash" src` after planner phases
- `rg "cleanMediaFolderTitle" src/ui/pages/stream src/core/stream` to prevent scanner cleanup from becoming output naming policy

### MCP / UIA Smoke Recipes

Use the shared desktop lock discipline before driving the app.

Smoke 1: Stream entry

- Open a series detail page with seasons.
- Verify a season-level download button appears beside the season selector.
- Verify movie detail pages do not show the season action.
- Activate the button and cancel pre-flight.
- Verify no Tankorent row is created.

Smoke 2: Pre-flight plan

- Use a known series season with at least one existing canonical file.
- Verify pre-flight shows selected, skipped existing, no-source, known size, unknown-size count, and pack verified/unverified state.
- Confirm and verify navigation to Tankorent.

Smoke 3: Tankorent group row

- Verify exactly one group row appears for the season.
- Expand it and verify child rows.
- Pause/resume/cancel group; verify actions apply to children.
- Verify ungrouped torrent rows still work.

Smoke 4: Restart resilience

- Start a group and close app during source discovery, during transfer, and after completion before publish.
- Restart and verify group state is not lost.
- Verify retry failed is available only when failed items exist.

Smoke 5: Videos publish

- During active download, open Videos and verify partial/staging files are not shown.
- After publish, open Videos and verify the show appears under the expected show folder and season.
- Confirm scanner sees canonical filenames.

### Manual Hemanth Smokes

Manual checks should focus on real-world behavior that automation cannot reliably fake:

- Does the pre-flight summary make the risk obvious without asking for technical choices?
- Does a real season pack win only when it truly contains the season?
- Are group row labels and failure counts understandable?
- Does cancel behavior match expectations for partial files?
- Does the final Plex/Jellyfin-style folder look right on disk?

## D. First-Summon Recommendation

The spec's suggested phases start with class skeletons and then broad source preflight. I would change the first summon.

Smallest coherent ship:

1. Pure `StreamBulkPlan` with canonical naming, identity keys, skip-if-exists, and explicit quality fallback tests.
2. Generic group record schema with `streamGroupId` as a foreign key.
3. A dev-only or test-only per-episode plan ingestion path into Tankorent that creates a group row without pack handling.

Why this first:

- It proves the architecture's durable identity model before pack complexity.
- It forces the canonical naming and group persistence decisions early.
- It exercises Tankorent grouping with lower risk.
- It leaves pack mode behind a metadata-proof gate instead of implementing the most dangerous part first.

If Agent 4 requires the first user-visible slice to obey "season pack always wins", then the first summon must include metadata-proven pack coverage. Do not ship text-detected pack wins, even as an early slice.

## E. Traceability

| Implementation Step | Reason |
|---|---|
| Pure plan before UI | Audit Improvement 1: pre-flight, skip checks, and publish need one artifact |
| Explicit bulk comparator | Q1 and A5: picker ordering is not bulk policy |
| Metadata-proven pack mode | Q3 and A1: Stremio `fileIdx` is not coverage |
| Generic group store | Section 3 and A10: group state exceeds `TorrentInfo` |
| File keys include index | A4: `infoHash` alone is unsafe |
| Staging/publish | A2 and 11.10: direct library writes expose partials and complicate cancel |
| Torrent-aware rename/release | A3: active torrent ownership conflicts with blind filesystem rename |
| Group transfer view model | A9: Tankorent flat table assumptions are broad |
| Persist item failures | Q5, 11.7, 11.8: retry failed requires item state |
| Videos smoke despite no Videos code | Improvement 10: no code needed remains a verification claim |

