# STREAM_BULK_DOWNLOAD — Design Spec

**Date:** 2026-05-07
**Author:** Agent 4 (Stream mode)
**Status:** Awaiting Agent 7 (Codex) audit + Hemanth final review
**Brainstorm pipeline:** /superpowers:brainstorming → THIS SPEC → /superpowers:writing-plans → /superpowers:executing-plans
**Hemanth-ratified product calls (in order):** Q1 fallback policy, Q2 affordance, Q3 pack priority, Q4 conflict policy, Q5 lifecycle granularity (see §3 Decisions Locked).

---

## 1. Intent

A new feature in Tankoban's Stream mode that lets the user download an entire season of a show in one click. The system picks the right torrent per episode using addon results, hands the magnets off to Tankorent for the actual transfer, but presents the user-visible state in Tankorent as ONE grouped row (not N individual torrents). On completion, the files land in a Stream-style folder structure under the Videos library so the existing Videos-mode scanner picks them up naturally.

Hemanth's verbatim ask, 2026-05-07:

> Can we add a feature to download entire seasons or maybe even full series in stream modes. The simple rule can be, for a full season download:
>
> 1. It downloads the most seeded 1080p version of every episode
> 2. But Tankorent should not show me 8 different downloads while downloading, only the name of the show and season with a special tag somewhere calling it the 'stream download'
> 3. It should auto-create a folder for the show and the sub folder for the season which will have the episodes I downloaded
> 4. All the names of the episodes must be what they are in the stream mode's page for the TV show and not the video name in the torrent
> 5. And obviously this download is transferred to videos mode.

These five rules are LOCKED — design explores HOW, not WHETHER.

## 2. Scope

### In scope (v1)

- Per-season bulk download triggered from `StreamDetailView`'s season-combo row.
- Source-pick: 1080p preferred → 4K (2160p/1440p) → 720p → any-quality fallback, with seeder-count as primary tie-break.
- Season-pack-first dispatch (1 magnet covering all episodes via `fileIndex`) with fallback to per-episode magnets when no qualifying pack exists.
- Skip-if-exists by canonical name (existing files at canonical path are preserved untouched; bulk only adds what's missing).
- Tankorent group representation as a single row with aggregated progress + "STREAM" badge.
- Group-level pause/resume/cancel; partial-failure tolerance with right-click "Retry failed".
- Post-download canonical rename (pack: file-index-keyed; per-episode: per-torrent).
- Auto-created folder structure `<Videos library root>/<Show name>/<Season NN>/`.
- Embedded subtitles only (sidecar fetch deferred).
- Movies and TV series-without-seasons (specials, miniseries) explicitly excluded.

### Out of scope (v1)

- Full-series download ("download all seasons" in one click). Per-season trigger only; user can click each season manually if they want all of them.
- Per-episode pause/resume/cancel from the UI. Group-level only.
- Per-show or per-user quality preferences (always 1080p default; no settings panel).
- Smart re-download (better source appears post-launch). Once started, the source is fixed.
- Subscription / auto-add new aired episodes. Manual trigger only.
- Sidecar subtitle fetch from OpenSubtitles. Embedded subs only.
- HDR/DV explicit preference toggle. HDR/DV variants compete by seeder count alongside SDR within the same quality tier.
- Disk-space pre-check beyond an informational summary line. (Bulk does not block on insufficient disk.)
- Cross-platform path-length adjustments. Windows-first; long path handling deferred.

### Future extensions to keep in mind (NOT designed, NOT foreclosed)

- Smart re-download when a strictly better source appears.
- "All seasons" bulk option at show-header level.
- Per-show quality preference saved in `StreamLibraryEntry` extension.
- Subscription model for episodes that air after the bulk.
- Sidecar subtitle fetch in parallel with download.

## 3. Decisions Locked

Hemanth ratified these during brainstorm 2026-05-07. They MUST NOT be re-litigated during plan-writing or implementation without an explicit re-open.

| Q | Decision | Rationale |
|---|----------|-----------|
| Q1 | **Source fallback cascade**: 1080p → 4K (2160p / 1440p) → 720p → any-quality. Pre-flight summary dialog displays breakdown before commit. | User wants predictable quality without strict skipping that would make full-season downloads chronically partial. |
| Q2 | **Per-season trigger only** for v1. Single "Download season" button on the season-combo row. No full-series button. | 80% case is "I just queued S03, mass-grab it". Full-series is a v2 if Hemanth actually wants it. |
| Q3 | **Season-pack always wins if it exists**. Per-episode-magnet path only fires when no qualifying pack exists. | Pack = single torrent, simpler grouping, often higher aggregate throughput. |
| Q4 | **Skip-if-exists by canonical name**. Files at non-canonical names in same folder are left untouched. Pre-flight summary shows skip count. | Simple, never overwrites; dedup by canonical match avoids parser-based false-positives. |
| Q5 | **Group-level pause/resume/cancel only**. Internally tolerates per-episode failures; right-click → "Retry failed" re-runs source-pick + dispatch for missing slots. | Rule 2's intent is grouped-as-one — per-episode interaction breaks that abstraction. Retry-failed handles the long tail. |

Sensible defaults locked under Rule 14 (no Hemanth menu-ing):

- **Concurrency**: trust libtorrent's existing global queue (`setQueueLimits`). No new bulk-specific cap.
- **HDR/DV inclusion**: `qualitySort==3` includes both 1080p SDR and 1080p HDR/DV; seeder count decides between them.
- **Subtitles**: embedded only in v1.
- **Tie-break order** within a quality tier: seeders desc → SDR over HDR/DV → file-size asc → fileSize desc as final fallback.
- **Badge text**: `STREAM` chip in the existing chip QSS — small-caps, no-color, no-emoji per `feedback_no_color_no_emoji.md`.
- **Group-id format**: `stream:<imdbId>:s<NN>:<unix-ms-timestamp>` (globally unique, sortable, traceable to source).

## 4. Architecture (Approach 3 — Hybrid)

Approach 3 was selected over Approach 1 (minimal extension) and Approach 2 (full orchestrator + first-class group entity) for the following balance:

- Group representation = simplest possible (one new field on `TorrentInfo`, no new JSON file).
- Orchestrator class isolates the genuinely-complex logic (source-pick fan-out, canonical-name map, pre-flight aggregation) without re-implementing aggregator infrastructure.
- Rename pipeline avoids libtorrent's `rename_file` async dance entirely. Mid-download torrent-named files are a non-issue per Rule 5 (Videos scanner only sees the folder when Hemanth navigates to Videos).
- Reuses STREAM_ADD_TO_TANKORENT routing pattern shipped 2026-05-06 (`MainWindow::onAddToTankorentRequested`) — bulk variant is a sibling overload, not a parallel routing graph.

### 4.1 File inventory

```
NEW    src/core/stream/StreamBulkDownloader.h          ~80 LOC
NEW    src/core/stream/StreamBulkDownloader.cpp        ~450 LOC

MOD    src/core/torrent/TorrentClient.h + .cpp         +~30 LOC
MOD    src/ui/pages/TankorentPage.h + .cpp             +~250 LOC
MOD    src/ui/pages/StreamPage.h + .cpp                +~80 LOC
MOD    src/ui/pages/stream/StreamDetailView.h + .cpp   +~60 LOC
MOD    src/ui/MainWindow.h + .cpp                      +~25 LOC
MOD    CMakeLists.txt                                  +2 entries
```

Total estimated LOC: ~975 across 6 modified + 1 new file pair.

### 4.2 Component responsibilities

**`StreamBulkDownloader` (NEW class)** — pure-Qt object owned by `StreamPage` for the duration of a single bulk operation.

- Owns: source-pick fan-out (N parallel `StreamAggregator` instances); canonical-name map construction; pre-flight summary aggregation; group-id minting.
- Does NOT own: torrent dispatch, transfer state, UI rendering, persistence.
- Lifecycle: created on `triggerBulkSeasonDownload()`; destroyed after `dispatchReady` emit (or `dispatchFailed` emit + user dismiss). NOT a singleton — multiple bulk operations can be in flight (one orchestrator each).
- Threading: lives on the GUI thread. Network I/O is delegated to `StreamAggregator` (which is already non-blocking).
- Cancellation: `cancel()` aborts in-flight aggregators, emits `cancelled()`, frees resources.

**`TorrentClient` (extended)**

- `TorrentInfo` schema: add `QString streamGroupId` field (default empty, indicating "ungrouped"). Existing single-torrent flows unaffected.
- `m_records` JSON persistence: read/write the field if present. Old records load with empty `streamGroupId` (forward-compatible).
- API surface: no new public methods. Group-aware UI logic lives in `TankorentPage`.

**`TankorentPage` (extended)**

- New public method: `addMagnetGroupFromExternal(const QString& label, const QList<BulkItem>& items, const QString& groupId)` — sibling to existing `addMagnetFromExternal`.
- Internal: iterates items, calls existing `resolveMetadata` + `startDownload` per magnet, applies `streamGroupId` to each `TorrentInfo` record, applies `file_priorities` for pack case.
- `m_transfersTable` rendering: new branch detects non-empty `streamGroupId`, collapses children into a single group row, displays aggregated progress + "STREAM" chip + episode-count summary ("8 episodes / 7 done / 1 failed").
- Group right-click menu: pause group / resume group / cancel group / retry failed / expand details / show files in folder.
- Post-completion rename pipeline: subscribes to existing `TorrentClient::torrentCompleted` signal; if completed torrent has non-empty `streamGroupId` AND a stored canonical-name map for that group, renames files in place.

**`StreamPage` (extended)**

- New slot: `triggerBulkSeasonDownload(int season)` — instantiates `StreamBulkDownloader`, connects its signals, kicks off source-pick fan-out.
- New slot: `onBulkSourcesReady(summary, canonicalMap, items)` — shows pre-flight dialog (modal QDialog with simple breakdown text + Start / Cancel buttons).
- On Start: emits new signal `addToTankorentBulkRequested(label, items, groupId)` → `MainWindow`.

**`StreamDetailView` (extended)**

- New `m_bulkDownloadBtn` next to `m_seasonCombo` in the season-row layout. Visible only when `m_currentType == "series"` AND a season is selected AND that season has ≥1 episode in `m_seasons`. Hidden for movies and unselected-season state.
- Button label: "Download season" (or "Download Season N" — Hemanth's pick later in plan-writing).
- New signal `bulkDownloadRequested(int season)` connected to StreamPage's `triggerBulkSeasonDownload` slot.

**`MainWindow` (extended)**

- New slot: `onAddToTankorentBulkRequested(label, items, groupId)` — sibling to existing `onAddToTankorentRequested`. Forwards to `m_tankorentPage->addMagnetGroupFromExternal(...)`. Same single-page-switch behavior as the existing single-magnet path (does NOT pause/teardown the player).

### 4.3 Data flow (happy path, season-pack found)

```
1.  User: clicks "Download season" on StreamDetailView (S03 selected, 10 episodes)
2.  StreamDetailView::bulkDownloadRequested(season=3)
        → connected via MainWindow buildPageStack to StreamPage
3.  StreamPage::triggerBulkSeasonDownload(3):
        ─ snapshots show metadata: { imdbId, title, year, season#=3,
                                     episodeList[]={ s,e,title,imdbId? } }
        ─ constructs StreamBulkDownloader(metadata, parent=StreamPage)
        ─ connects signals (sourcesReady, dispatchFailed, cancelled)
        ─ calls bulk->begin()
4.  StreamBulkDownloader::begin():
        ─ instantiates N=10 parallel StreamAggregator instances
        ─ for each ep i in 1..10: aggregators[i]->load({type:"series",
            id:f"<imdbId>:3:{i}", extra:[]})
        ─ accumulates streamsReady callbacks
5.  Each streamsReady → orchestrator runs buildPickerChoices(streams,
    addonsById) (existing function in StreamSourceChoice.cpp)
6.  After all 10 episodes' streams resolved (or per-episode timeout):
        ─ orchestrator runs pack-priority pick (see §5 algorithm)
        ─ if qualifying pack found: groupShape = "pack"
                packMagnet = chosenPackChoice
                fileIndices = { episodeNum → fileIndexInPack }
                canonicalMap = { fileIndexInPack → canonicalFilename }
        ─ else: groupShape = "per-episode"
                episodeMagnets = { episodeNum → chosenChoice }
                canonicalMap = { infoHash → canonicalFilename }
        ─ runs skip-if-exists check at target path
        ─ builds preflight summary: { total, toDownload, alreadyInLibrary,
                                       qualityBreakdown, missingNoSource }
7.  Orchestrator emits sourcesReady(summary, canonicalMap, magnetList,
                                     groupId, groupShape)
8.  StreamPage::onBulkSourcesReady() shows preflight dialog
9.  User clicks "Start download"
10. StreamPage emits addToTankorentBulkRequested(label="The Boys · Season 3",
        items=[{ magnetUri, infoHash, fileIndex (-1 for whole), canonicalName,
                 sizeBytes, episodeNum }, ...],
        groupId="stream:tt1190634:s03:1746615720123",
        groupShape, canonicalMap)
11. MainWindow::onAddToTankorentBulkRequested → page-switch to Tankorent
        → m_tankorentPage->addMagnetGroupFromExternal(label, items, groupId,
                                                       groupShape, canonicalMap)
12. TankorentPage::addMagnetGroupFromExternal():
        ─ stashes canonicalMap keyed by groupId (member m_pendingCanonicalMaps)
        ─ for each item:
                client->resolveMetadata(magnetUri)
                if groupShape == "pack" && fileIndex map known:
                    ─ apply file_priorities to skip non-listed indices
                    ─ NB: extras-detection deferred to §5.5
                client->startDownload(infoHash, AddTorrentConfig{
                    savePath = <library>/<Show>/<Season NN>/,
                    streamGroupId = groupId,
                    category = "" })
        ─ refreshTransfers() repaints with new group row visible
13. Group row appears: "The Boys · Season 3 [STREAM] · 0% · 0 done"
14. As each member's torrentCompleted fires → TankorentPage rename pipeline:
        ─ resolves canonicalMap for this groupId
        ─ pack: walks file_priority>0 files; renames each by fileIndex
                via QFile::rename in savePath
        ─ per-episode: renames the single .mkv to canonical
        ─ updates streamGroupId record's done-count
        ─ refreshTransfers() repaints group row's "N done" counter
15. All members complete → group row shows "Done · 8 episodes"
16. User next opens Videos page → existing scanner walks
        <library>/The Boys/Season 03/ → existing ShowInfo emit → tile
        appears in Videos library.
```

### 4.4 Data flow (per-episode fallback path)

Identical to §4.3 except:
- Step 6 `groupShape = "per-episode"`; one magnet per episode.
- Step 12 each magnet is its own torrent; no `file_priorities` applied (single-file torrents).
- Step 14 rename is per-torrent (one .mkv → canonical name).

### 4.5 Threading / lifecycle invariants

- `StreamBulkDownloader` lives on GUI thread. Aborts on parent-StreamPage destruction (Qt parent-child cleanup).
- All file rename operations execute on GUI thread post-`torrentCompleted` (small fast operations; no thread needed).
- Pre-flight dialog is modal but does NOT block libtorrent — once user clicks Start, dispatch happens on GUI thread, libtorrent does its work async.
- App-restart: `TorrentInfo.streamGroupId` persists; canonical-name maps DO need restart-restore (see §7.3).

## 5. Source-pick algorithm

### 5.1 Per-episode source aggregation

For each episode `i` in season `s`:
1. Construct `StreamLoadRequest{type:"series", id:"<imdbId>:s:i", extra:[]}`
2. Instantiate `StreamAggregator` and call `load(request)`.
3. On `streamsReady(streams, addonsById)` → run existing `buildPickerChoices(streams, addonsById)` which returns `QList<StreamPickerChoice>` already sorted by [magnet+seeders desc, quality desc, size desc, title asc].

After all N episodes' choice lists are ready (or per-episode timeout — default 30s, configurable later), proceed to pack-priority pick.

### 5.2 Pack-priority detection

Define `eligiblePack(choice, episodeCount)` as:
```
choice.sourceKind == "magnet"
&& choice.packType == "season"
&& choice.qualitySort >= 2  // 720p or better
&& choice.seeders > 0
&& packCoversAllEpisodes(choice, episodeCount)
```

`packCoversAllEpisodes(choice, episodeCount)`: examine the `Stream` payload's `behaviorHints` and `fileIdx` fields populated by Stremio addons. If the stream advertises `fileIdx==-1` (Stremio addon-protocol convention: "no per-file restriction, the entire torrent IS the stream") OR the stream's title/description matches `S<NN>` complete-season patterns AND no per-file restriction is advertised, treat it as covering all. Otherwise, fall back to per-episode.

NB: Stremio addon's `fileIdx==-1` is conceptually distinct from our `BulkDownloadItem.fileIndex==-1` (§9.1). The addon-protocol field flags a stream that targets the whole torrent (regardless of pack-or-not); our internal `BulkDownloadItem.fileIndex==-1` flags the per-episode-magnet case (single-file torrent, no in-pack file selection needed). The naming collision is unfortunate but each value lives in a different layer.

NB: Pack-coverage detection is heuristic. Plan-writing phase MUST flag this as an "implementation-risk surface" for Agent 7 audit. Worst case: a pack chosen incorrectly covers only part of the season — files for missing episodes never appear; group row shows "5/10 done" and stalls. Mitigation: per-episode timeout in dispatch lifecycle (see §7.4) flips a "stuck pack" group to per-episode-fallback retry.

### 5.3 Pack-pick selection (when ≥1 eligible pack exists)

Among all eligible packs found across all episodes' choice lists (the same pack will appear in every episode's list — deduplicate by `infoHash`):

1. Filter to `qualitySort==3` (1080p) only. If non-empty, pick highest seeders. STOP.
2. Else filter to `qualitySort==4 || qualitySort==5` (4K-tier). If non-empty, pick highest seeders. STOP.
3. Else filter to `qualitySort==2` (720p). If non-empty, pick highest seeders. STOP.
4. Else any quality, pick highest seeders.

The chosen pack is THE source for the entire season (single magnet, file_priorities select per-episode files, single rename map keyed by fileIndex).

### 5.4 Per-episode pick (when no eligible pack exists)

For each episode independently, walk its choice list:

1. Walk magnet entries with `qualitySort==3 && seeders > 0`. Pick first (already sorted by seeders desc within the buildPickerChoices contract). If found, STOP for this episode.
2. Else walk `qualitySort==4 || qualitySort==5 && seeders > 0`. Pick first. STOP.
3. Else walk `qualitySort==2 && seeders > 0`. Pick first. STOP.
4. Else walk any-quality magnets with `seeders > 0`. Pick first. STOP.
5. Else mark this episode as "no source available".

Tie-breaks within a quality tier (handled implicitly by buildPickerChoices's existing sort): seeders desc → quality (SDR over HDR/DV by default, but seeders takes precedence) → size asc. Step 1 specifically excludes HDR/DV variants only if a SDR variant exists at equal seeders — handled by buildPickerChoices's stable sort.

NB: the `fileIndex` for per-episode picks comes from `StreamPickerChoice.fileIndex` (already populated by addon).

### 5.5 Pack file selection (skipping extras)

Once a pack is picked, libtorrent's `file_priorities` array determines which files to download. The orchestrator builds the priority map:

1. List all files in the pack metadata (after `resolveMetadata` returns).
2. For each file:
   - Match against canonical-episode pattern: regex `S<season-2digit>E<NN>` AND extension matches `mkv|mp4|webm|m4v` AND size > 100 MB (skip thumbnails / sample / NFO).
   - If matched → `priority = 4` (normal); record `fileIndex` in `canonicalMap`.
   - Else → `priority = 0` (skip download).
3. Persist `file_priorities` via libtorrent before starting the download.

Risk: a pack with non-standard naming (e.g. `Episode01.mkv` without S/E) WILL fail this match. Plan-writing phase should consider whether to fall back to "include all files >500 MB and rename by file-order" as a secondary heuristic. Flagged for Agent 7 review.

### 5.6 Pre-flight summary aggregation

After picks resolve, build summary struct:

```cpp
struct PreflightSummary {
    int totalEpisodes;        // = season's episode count
    int toDownload;           // not in skip-list
    int alreadyInLibrary;     // canonical name exists at target path
    int missingNoSource;      // no magnet at any quality
    QMap<int, int> qualityBreakdown;  // qualitySort → count (e.g. {3:6, 2:2})
    bool packMode;            // true if pack pick won
    QString packLabel;        // displayed in summary if packMode
    qint64 estimatedTotalBytes;  // sum of source sizeBytes
};
```

Dialog text shape (final wording during plan-writing):

```
The Boys · Season 3
─────────────────────────────────────
8 episodes total
6 to download
2 already in library

Source: pack (1 torrent, 200 seeders, ~25 GB)
Quality: 6 at 1080p (HDR), 2 at 720p

[Cancel]              [Start download]
```

If `missingNoSource > 0`: extra line "1 episode unavailable (no source found)" before the action buttons; download proceeds for what's available.

## 6. Canonical naming and folder structure

### 6.1 Folder structure

Target root: `<Videos library root>/<Sanitized Show Name>/Season <NN>/`

Show-name source: `m_lastPreviewHint->name` from StreamDetailView (the metadata from preview/MetaItem). Year-suffix appended if present: `The Boys (2019)`. Sanitization removes Windows-illegal characters `< > : " / \ | ? *` replaced with empty string; trims leading/trailing whitespace + dots; collapses multiple spaces to single.

Season folder: `Season <NN>` with 2-digit zero-padded number. (`Season 03`, not `Season 3` or `S03`.)

Library root: `<Videos library root>` is the FIRST entry from `m_bridge->rootFolders("videos")`, mirroring the existing `BookDownloader` pattern (TankoLibraryPage uses `m_bridge->rootFolders("books").first()`).

If the FIRST videos rootFolder doesn't exist or is unreachable (network-mount disconnected, drive missing): bulk operation aborts at pre-flight stage with error "Videos library root not accessible: <path>". User must fix root-folder configuration before retrying.

### 6.2 Episode filename derivation

Canonical filename shape: `S<NN>E<NN> - <Episode Title>.<ext>`

Examples:
- `S03E04 - Glorious Five Year Plan.mkv`
- `S01E12 - You Found Me.mp4`
- `S02E01 - The Big Ride.mkv`

Source of episode title: `StreamEpisode.title` from `m_seasons[s][i].title` populated by addon's series meta (Cinemeta or equivalent). If empty string, fall back to `S<NN>E<NN>.<ext>` (no title segment).

Extension: derived from the source file's extension — pack case via libtorrent file metadata, per-episode case via `Stream.fileNameHint` or libtorrent metadata after `resolveMetadata` returns. Defaults to `.mkv` if extension absent.

Sanitization on title: same set as folder sanitization.

### 6.3 Canonical name map persistence

`canonicalMap` is built at orchestrator dispatch time and passed to `TankorentPage`. Persistence shape:

For pack groupShape:
```
m_pendingCanonicalMapsByFileIndex :
    QMap<QString /* groupId */, QMap<int /* fileIndex */, QString /* canonical */>>
```

For per-episode groupShape:
```
m_pendingCanonicalMapsByInfoHash :
    QMap<QString /* groupId */, QMap<QString /* infoHash */, QString /* canonical */>>
```

A given `groupId` populates exactly ONE of the two maps depending on `groupShape`. The orchestrator picks which to fill at dispatch time; rename pipeline reads from whichever has the entry.

Persisted to a NEW JSON file at `<AppLocalDataLocation>/Tankoban/data/stream_bulk_canonical_maps.json`:

```json
{
  "stream:tt1190634:s03:1746615720123": {
    "groupShape": "pack",
    "savePath": "C:/Users/.../Videos/The Boys/Season 03",
    "label": "The Boys · Season 3",
    "itemsByFileIndex": {
      "0": "S03E01 - Payback.mkv",
      "1": "S03E02 - The Only Man in the Sky.mkv"
    },
    "createdAt": 1746615720123
  },
  "stream:tt2306299:s02:1746615721000": {
    "groupShape": "per-episode",
    "savePath": "C:/Users/.../Videos/Vinland Saga/Season 02",
    "label": "Vinland Saga · Season 2",
    "itemsByInfoHash": {
      "abc123...": "S02E01 - Slave.mkv",
      "def456...": "S02E02 - Ketil's Farm.mkv"
    },
    "createdAt": 1746615721000
  }
}
```

The two key shapes (`itemsByFileIndex` vs `itemsByInfoHash`) are mutually exclusive per group entry; presence of one determines `groupShape` at load time.

Rationale for separate JSON file vs extending `torrents.json`: keeps the canonical-map data physically separate from libtorrent's resume state; prevents canonical-map-corruption from leaking into torrent state; cleanly garbage-collected (entry deleted after group reaches 100% AND user navigates to Videos page once).

### 6.4 Restart resilience

If Tankoban restarts mid-bulk:
- `torrents.json` `streamGroupId` field persists → group row reappears in Tankorent.
- `stream_bulk_canonical_maps.json` persists → rename pipeline still has the map.
- Libtorrent resume data → in-progress torrents pick up where they left off.
- If the canonical-map JSON is lost / corrupt: rename does not happen; files keep torrent names. Videos scanner still picks up the season folder (Rule 5 satisfied), just with non-canonical filenames. Group row shows a warning chip "rename map missing — torrent names preserved".

## 7. Tankorent UI rendering

### 7.1 Group row shape

Existing `m_transfersTable` columns (status quo): `[ Name | Size | Done | Status | DL | UL | Peers | Seeds | ETA ]`.

Group row injection:
- Detected by non-empty `streamGroupId` on at least one record in m_cachedActive.
- Group records aggregated client-side (no DB join — trivial linear scan over m_cachedActive).
- Group row position: top-of-table by default. Alternative: chronological by group `createdAt`. Plan-writing decides.
- Group row cells:
  - Name: `<group label>` + small `[STREAM]` chip (a `QLabel` in a cell-widget HBox — same pattern as the existing seeder/quality chips in m_resultsTable).
  - Size: aggregated `totalWanted` sum.
  - Done: `<doneCount>/<totalEpisodes>` text + a thin progress bar (aggregated `progress` weighted by `totalWanted`).
  - Status: derived state — "Downloading", "Paused (group)", "Done", "Stalled (3/8 paused)", "Failed (1 unrecoverable)".
  - DL/UL/Peers/Seeds: aggregated sums.
  - ETA: max of children's ETAs (if all children are downloading; else "-").

### 7.2 Expansion (read-only)

Click-to-expand: row reveals N indented child-rows with the same column layout but per-torrent values. Children are READ-ONLY in v1 (no per-child right-click menu). Children's Name cell shows the canonical name (post-rename) or the torrent name (pre-rename) — Hemanth's choice during plan-writing.

Expand/collapse persisted in QSettings keyed by groupId so the user's preferred view per-group survives restarts.

### 7.3 Group right-click menu

Order:
1. Pause group (disabled if no children downloading)
2. Resume group (disabled if no children paused)
3. Cancel group ... (confirmation dialog: "Cancel and delete N partial files? [Cancel] [Delete All] [Delete Files Too]")
4. ── separator
5. Retry failed (visible only if `failedCount > 0`)
6. ── separator
7. Show in folder (opens savePath in Windows Explorer)
8. Expand details / Collapse details (toggle)

### 7.4 Stuck / failed-state detection

Per-episode is considered "failed" when:
- `errorMessage` non-empty AND not in retryable-error list (network-flake / temporary-tracker errors stay "stalled").
- 0 seeds AND 0 peers continuously for 5 minutes (configurable in plan-writing).
- Pack-mode special case: file_priorities prevent download but pack metadata fails to fetch in 5 minutes → whole group marked "stuck", auto-fallback to per-episode-retry queued (see §8.3).

"Retry failed" right-click action:
- Walks the group's children for failed status.
- For each: re-runs the source-pick for that episode (same orchestrator, but scoped to single-episode), then re-dispatches.
- Pack failures triggering per-episode-fallback: same flow, but the pack itself is removed from candidates (orchestrator's pick-list excludes the dead pack's infoHash).

## 8. Lifecycle and error paths

### 8.1 Pre-flight error handling

If the orchestrator's source fan-out has any of:
- All N episodes return zero magnets → abort with "No sources found for any episode." Pre-flight not shown.
- ≥1 but <N episodes return magnets → pre-flight shown with `missingNoSource` count.
- Pack metadata fetch times out → pack disqualified; treat as no-pack-available, fallback to per-episode picks.
- Pre-flight summary build fails (canonical-name path inaccessible) → abort with diagnostic.

User cancellation at pre-flight: orchestrator destroyed, no torrents added, no persistence written.

### 8.2 Mid-download error handling

Per-episode failures (per §7.4) auto-quarantine — group continues with remaining slots. Group row badge updates: "7 done · 1 failed".

Pack-mode catastrophic failure (pack metadata cannot be fetched, or pack downloads but file_priorities map yields zero video files): orchestrator's "stuck pack" detection triggers; UI prompts "Pack download stuck. Re-try as per-episode? [Yes / No]". On Yes, pack is removed (cancel + delete-files), orchestrator re-dispatches in per-episode mode using the previously-collected per-episode picks.

Disk-space exhaustion mid-download: libtorrent surfaces error → group row shows "Failed: insufficient disk space". User must free space + manually retry.

App quit mid-bulk: torrents persist via libtorrent resume data; canonical maps persist via §6.3 JSON; group row reappears on next launch in same partial state.

### 8.3 Pause / resume

Pause group: iterate group's children, call existing `pauseTorrent(infoHash)` per child. Group row's status flips to "Paused".

Resume group: iterate group's children that are in paused state, call `resumeTorrent(infoHash)`. Children that succeeded earlier are skipped (already in seeding state).

### 8.4 Cancel group

User-initiated cancel: confirmation dialog with two actions:
- "Delete from list" (keeps files on disk, removes torrents + group entry)
- "Delete files too" (removes torrents + group entry + on-disk partial/complete files)

After cancel:
- Group's children iterated; each `deleteTorrent(infoHash, deleteFiles)` called.
- Canonical map JSON entry removed.
- Group row removed from `m_transfersTable`.

### 8.5 Completion

When all group children reach `progress >= 1.0` AND none in failed state:
- Group row status flips to "Done".
- Canonical-map JSON entry NOT deleted yet (preserved through one Videos-page-navigation cycle so a corrupted rename can be retried).
- One-time toast: "The Boys · Season 3 download complete (8 episodes)" (matches existing toast pattern in TankorentPage).
- After user navigates to Videos page once: canonical-map JSON entry deleted (lazy GC).

Partial completion (failed > 0):
- Group row status: "Done · 7/8" with retry-failed action visible.
- Canonical-map JSON preserved indefinitely until user explicitly cancels group or all retry-failed attempts give up.

## 9. Stream → Tankorent → Videos handoff API

### 9.1 New types

```cpp
namespace tankostream::stream {

struct BulkDownloadItem {
    QString magnetUri;
    QString infoHash;
    int     fileIndex = -1;          // -1 = whole torrent (per-episode case)
    QString canonicalFilename;
    qint64  sizeBytes = 0;
    int     episodeNum = -1;         // for traceability
};

struct PreflightSummary {
    int totalEpisodes;
    int toDownload;
    int alreadyInLibrary;
    int missingNoSource;
    QMap<int, int> qualityBreakdown;  // qualitySort → count
    bool packMode;
    QString packLabel;
    qint64 estimatedTotalBytes;
};

class StreamBulkDownloader : public QObject {
    Q_OBJECT
public:
    StreamBulkDownloader(const ShowMetadata& meta,
                         tankostream::addon::AddonRegistry* registry,
                         const QString& targetSavePath,
                         QObject* parent = nullptr);
    ~StreamBulkDownloader();

    void begin();
    void cancel();

signals:
    void sourcesReady(const PreflightSummary& summary,
                      const QString& groupId,
                      const QString& groupLabel,
                      const QString& savePath,
                      const QList<BulkDownloadItem>& items,
                      const QMap<int, QString>& canonicalMapByFileIndex,
                      const QMap<QString, QString>& canonicalMapByInfoHash);
    void dispatchFailed(const QString& reason);
    void cancelled();

    void progressUpdate(int aggregatorsResolved, int aggregatorsTotal);
};

} // namespace
```

### 9.2 StreamPage signal extension

```cpp
// existing
void addToTankorentRequested(const QString& magnetUri, const QString& displayName);

// NEW
void addToTankorentBulkRequested(const QString& label,
                                  const QList<BulkDownloadItem>& items,
                                  const QString& groupId,
                                  const QString& groupShape,
                                  const QString& savePath,
                                  const QMap<int, QString>& canonicalMapByFileIndex,
                                  const QMap<QString, QString>& canonicalMapByInfoHash);
```

### 9.3 MainWindow slot extension

```cpp
// existing
void onAddToTankorentRequested(const QString& magnetUri, const QString& displayName);

// NEW
void onAddToTankorentBulkRequested(const QString& label,
                                    const QList<BulkDownloadItem>& items,
                                    const QString& groupId,
                                    const QString& groupShape,
                                    const QString& savePath,
                                    const QMap<int, QString>& canonicalMapByFileIndex,
                                    const QMap<QString, QString>& canonicalMapByInfoHash);
```

Both forward to `m_tankorentPage->addMagnetGroupFromExternal(...)`.

### 9.4 TankorentPage public surface

```cpp
// existing
void addMagnetFromExternal(const QString& magnetUri, const QString& displayName);

// NEW
void addMagnetGroupFromExternal(const QString& label,
                                  const QList<tankostream::stream::BulkDownloadItem>& items,
                                  const QString& groupId,
                                  const QString& groupShape,
                                  const QString& savePath,
                                  const QMap<int, QString>& canonicalMapByFileIndex,
                                  const QMap<QString, QString>& canonicalMapByInfoHash);
```

### 9.5 Videos library handoff

No new code on the Videos side. The existing `VideosScanner::scan` walks `rootFolders` → groups by first-level subdir → emits `ShowInfo`. Folders matching `<root>/<Show>/<Season NN>/` will be picked up as a single show with N episode files (existing scanner pattern).

The Videos library will see the new show on next user navigation to the Videos page (existing scanner trigger). No notification, no auto-refresh from the bulk side — Rule 5 explicitly says the existing scanner "picks it up" — minimal new surface.

## 10. Testing strategy

### 10.1 Unit tests (`tankoban_tests`, opt-in via `-DTANKOBAN_BUILD_TESTS=ON`)

Pure-logic primitives testable without Qt event loop:

- `pickPackOrPerEpisode(choices, episodeCount)` returns `Pack(packChoice)` or `PerEpisode(map)` correctly.
- `pickQualityCascade(choices, qualityOrder)` returns expected choice for given input list.
- `sanitizeShowName("Mr. & Mrs. Smith")` → `Mr.  Mrs. Smith`.
- `canonicalEpisodeName(showName, season, ep, title, ext)` → expected string.
- `episodeNumFromFilename("The.Boys.S03E04.1080p.WEBRip.mkv")` → returns `(3, 4)`.

### 10.2 Manual smoke (Hemanth-driven)

1. **Happy path pack**: Stream tab → The Boys → Season 3 → Download Season → pre-flight → Start → wait → group row in Tankorent → completes → Videos tab → folder appears with 8 canonical-named episodes.
2. **Happy path per-episode**: A series Hemanth knows has no good packs (Vinland Saga rare anime case) → same flow → 8 separate torrents collapsed under 1 group row.
3. **Fallback cascade**: Find a season where E04 has no 1080p (e.g. older season) → pre-flight shows "6 at 1080p, 1 at 720p, 1 missing".
4. **Skip-if-exists**: Manually drop a canonical-named .mkv into the target folder pre-bulk → pre-flight reports "1 already in library" → after download, that file untouched.
5. **Pause/resume**: Mid-download, right-click group → Pause → all children pause → Resume → all resume.
6. **Cancel**: Mid-download, right-click → Cancel → Delete files → group row gone, no torrents in m_records, no files on disk.
7. **Partial failure**: Hard to provoke deterministically; observe in production wakes.
8. **Restart resilience**: Start bulk → quit Tankoban mid-download → relaunch → group row still visible, downloads resume.
9. **No sources**: Start bulk on an obscure show → all episodes return empty → "No sources found" toast, no torrents added.

### 10.3 Regression smokes (existing flows unchanged)

- Single-magnet add-from-card (STREAM_ADD_TO_TANKORENT) still works on a non-bulk source pick.
- Single-magnet add-from-URL flow unchanged.
- Tankorent batch-add-from-URL (no streamGroupId) renders as flat rows, not grouped.
- m_resultsTable / m_transfersTable click-sort still works on flat-row torrents.

### 10.4 Code-walk verification

Plan-writing phase MUST include a code-walk verification step before implementation kicks off — Approach 3's claim "no parallel routing graph" depends on the existing STREAM_ADD_TO_TANKORENT routing being correctly extensible. Verification: trace `MainWindow::onAddToTankorentRequested → TankorentPage::addMagnetFromExternal → startSingleAddFlow` and confirm the bulk variant slots in cleanly without forking.

## 11. Open items for Agent 7 (Codex) audit

Surface these explicitly for Agent 7's comparative analysis:

1. **Pack-coverage detection heuristic** (§5.2). The `packCoversAllEpisodes` check is fragile. How do reference apps (Stremio Web / qBittorrent + Sonarr / FlexGet) handle this? Is there a better signal in Stremio's Stream payload than what we're using?

2. **File-priority extras-skip heuristic** (§5.5). The "S/E pattern + extension + size > 100 MB" filter will fail on packs with non-standard naming. Does Sonarr/Radarr/qBittorrent's auto-management code have a more robust pattern? Should we ship a fallback ("download all video files >500 MB") as a secondary heuristic?

3. **Group representation choice** (§4.2). We picked Approach 1's groupId-on-TorrentInfo over Approach 2's separate TorrentGroup entity. Is this future-extensible enough for: (a) batch-add-from-URL grouping, (b) movie-collection grouping, (c) cross-show batch operations? Or will the field-on-record approach hit a wall at one of these?

4. **Pre-flight-then-dispatch latency** (§4.3). Source-pick fan-out across 10 episodes × ~10 addons each = ~100 simultaneous HTTP requests. Acceptable on the existing AddonTransport infrastructure? Should we throttle via a shared token-bucket rate limiter?

5. **Restart canonical-map invariant** (§6.4). Canonical-map JSON loss → files keep torrent names. Is there a recovery path that's cheaper than asking the user to retrigger the bulk? E.g., re-derive canonical names from the persisted `streamGroupId` + (if the show metadata is still fetchable from addons via imdbId) → reconstruct map?

6. **Concurrency cap** (Decisions Locked). We trust libtorrent's existing global queue. Is this realistic for the bulk case? Sonarr/Radarr cap concurrent downloads in non-libtorrent contexts; should we adopt a similar pattern (e.g. "max 4 episode torrents per group concurrent")?

7. **HDR/DV inclusion** (Decisions Locked). 1080p HDR is materially larger (often 2-3× SDR). Should `qualitySort==3` be split into `1080p-SDR` and `1080p-HDR` tiers, with SDR preferred by default for bulk size predictability?

8. **Subtitle handling** (out-of-scope). Embedded-only feels limiting. Is there a simple sidecar fetch path (OpenSubtitles addon already in use elsewhere?) that fits in v1 without major scope creep?

9. **Folder-naming convention compatibility**. We use `<Show>/Season NN/`. Does this match Plex / Jellyfin / Stremio's own scanning conventions? If a future Plex / Jellyfin integration needs subtle name shape ("Season 03" vs "S03"), should we ship the more compatible default now?

10. **Pre-flight summary UX**. Is the dialog text shape (§5.6) clear enough? Should it show source seeders / size estimates more prominently, or less? Hemanth likes minimal — is the current shape minimal or cluttered?

## 12. Implementation sequencing (for plan-writing)

Suggested phasing (NOT locked — plan-writing phase finalizes):

- **Phase 1** — `TorrentInfo.streamGroupId` field + persistence (smallest possible substrate for everything else).
- **Phase 2** — `StreamBulkDownloader` core: source-pick fan-out + pack-priority + per-episode cascade + canonical-name map (no UI yet, no Tankorent dispatch).
- **Phase 3** — `addMagnetGroupFromExternal` + canonical-map persistence + post-completion rename.
- **Phase 4** — Tankorent group row rendering + group right-click menu.
- **Phase 5** — Pre-flight dialog + StreamDetailView "Download season" button + StreamPage trigger wiring.
- **Phase 6** — Failure paths: stuck-pack auto-fallback + retry-failed + restart resilience.
- **Phase 7** — Polish: expand/collapse persistence + toasts + pre-flight-summary final wording + edge-case hardening.

## 13. Cursor

- Brainstorm: ✅ closed 2026-05-07
- Spec: ✅ this document
- Awaiting: Agent 7 (Codex) audit pass → Hemanth final review
- Next gate (Hemanth-fired): /superpowers:writing-plans
- Implementation gate (Hemanth-fired): /superpowers:executing-plans

---

End of spec. Author: Agent 4 (Stream mode). Pipeline: brainstorm → THIS → plan → execute.
