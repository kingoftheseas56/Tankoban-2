# STREAM_DOWNLOADS_NETFLIX_OVERHAUL — Design Spec

**Date:** 2026-05-12
**Author:** Agent 4 (Stream mode)
**Status:** Awaiting Hemanth review
**Brainstorm pipeline:** /superpowers:brainstorming → THIS SPEC → /superpowers:writing-plans → /superpowers:executing-plans
**Hemanth-locked product calls (this brainstorm):** P1–P8 (see §4).

---

## 1. Intent

Excise Tankorent from the user's stream-download flow. Stream downloads become a first-class Stream-mode UX: episode-row–native triggers, season-header–native controls, a sidebar-drawer-summoned cross-show Downloads page, and library-tile in-flight + downloaded badges. Tankorent stays as-is for non-stream torrents (manual magnet adds, manual torrent search). libtorrent remains the shared engine — only the user-facing rendering and control surface changes.

Hemanth's verbatim ask 2026-05-12:

> "Removal of Tankorent from the process of stream downloads (season downloads or select episode downloads), the app can still use our libtorrent but I shouldn't need to go Tankorent to track my downloads. The process must become very simplified, like how downloading in Netflix app works, in fact we can use the Netflix app's download UI and UX as a primary reference for this new implementation."

The Netflix UX reference is structural, not chromatic — match the per-episode-row + cross-show Downloads tab patterns, NOT the red-accent color palette (per `feedback_no_color_no_emoji.md` Tankoban stays grayscale).

## 2. Scope

### 2.1 In scope (v1)

- TankorentPage filters out any transfer whose `TorrentInfo.streamGroupId` is non-empty (Layer 1 field already shipped). Stream-originated downloads disappear from Tankorent's UI entirely.
- Inline per-row state + controls on `StreamDetailView`'s episode list: per-row checkbox, per-row single-morphing-icon (download / pause / continue / done / retry), live progress %.
- Inline season-header controls: a single morphing button (Download Season / Pause Season / Continue Season) plus a right-click "Cancel Season" affordance on the button OR the season-combo.
- Right-click row context menu: Cancel (deletes file) + Show alternate streams (Layer 3 Rule D, preserved).
- Two season-header buttons for selective dispatch: "Download Season" (queues every row) and "Download Selected (N)" (queues only checked rows; visible only when N ≥ 1).
- New tile chip `DOWNLOADING` on Stream library home, parallel to existing Layer 3 `DOWNLOADED` chip; in-flight wins when both apply.
- New sidebar-drawer entry "Downloads" → routes to a new full-page `StreamDownloadsPage` showing Active and History sections grouped by show.
- 90-day history retention via extension of the existing `pruneTerminalStreamBulkGroups` TTL from 7 days to 90 days. No new persistence file.
- Cohort scheduler from Layer 2 Phase 2 unchanged: sequential WITHIN each show's cohort; PARALLEL across shows; no global cap.
- Cancel semantics locked: per-episode cancel deletes the one file; season cancel deletes the whole cohort's files (including already-completed episodes); pause preserves files.
- Existing Layer 3 surfaces preserved untouched: `DOWNLOADED` chip, click-to-auto-play, "Show alternate streams" right-click, Videos-scanner skip, first-launch rescue migration, `StreamDownloadIndex` storage, `Remove from Library` evicts index.

### 2.2 Out of scope (v1) — explicit NOs

- **No preflight dialog.** Layer 2 Phase 1's `StreamBulkPreflightDialog` is superseded by inline row selection. The dialog code remains in the working tree only until executing-plans phase removes it.
- **No Tankorent group-row representation of stream bulks.** Removed entirely from TankorentPage's render path. No "STREAM" chip in Tankorent. No grouped row. No group context menu.
- **No torrent vocabulary anywhere in the new surface.** No peers, no seeders, no info-hash, no ratio, no queue position, no magnet text. The user sees: show name, season, episode, progress %, paused/downloaded/failed state, action icon.
- **No migration of existing in-flight Tankorent stream-bulks.** Hemanth's explicit OK to nuke existing bulks during cutover. See §10 (Transition).
- **No global concurrent-shows cap.** All queued shows download in parallel. (Each show's cohort still serializes per Layer 2.)
- **No auto-retry of failed downloads.** Failed-state rows show [↻] retry; user clicks to retry. No background retry timer.
- **No Cancel All / Pause All on the Downloads page.** Per-show controls only; no cross-show batch actions. (Future extension.)
- **No movie support.** Series-only, same as Layer 3 §2.2.
- **No auto-delete on watched.** Same as Layer 3 §2.2.
- **No bandwidth cap, no smart-priority playback-aware throttling.** Future arc.
- **No tile DOWNLOADING-progress visualization.** The chip is text-only — no progress bar, no count. (Per anchor question lock.)

### 2.3 Future extensions to flag (do NOT design)

- Cross-show batch controls on Downloads page (Cancel All, Pause All).
- Per-show bandwidth caps.
- Auto-delete on watched / keep-last-N storage policies (Layer 3 §2.2 future).
- Movie support (separate v2 brainstorm).
- "Download all seasons" trigger.
- Quality upgrade re-download.

## 3. Reconciliation with prior specs

This overhaul builds on three prior efforts. Each is preserved, extended, absorbed, or superseded as called out below.

| Layer | Spec | Status | This overhaul's effect |
|---|---|---|---|
| **Layer 1** | `docs/superpowers/specs/2026-05-07-stream-bulk-download-design.md` | Shipped Phases 0–7 | **Preserved**: source-pick + canonical naming + libtorrent dispatch + `TorrentInfo.streamGroupId` field + `stream_bulk_canonical_maps.json` + `stream_bulk_groups.json` schema. **Superseded**: Tankorent group-row representation (§7 of Layer 1 spec). **Superseded**: Layer 1 §Q5 group-level-only granularity — Hemanth now wants BOTH per-episode AND per-season cohort controls. **Extended**: `pruneTerminalStreamBulkGroups` TTL 7d → 90d. |
| **Layer 2 (V2)** | (no formal spec — phased in chat.md; Phases 1+2+3 all shipped) | Shipped + hotfixes | **Preserved**: cohort scheduler (V2 Phase 2 — `cohortMaybeAdvance` in TorrentClient). **Preserved**: per-episode Status column in StreamDetailView (V2 Phase 3 — `streamBulkSnapshotForImdbSeason`). **Superseded**: V2 Phase 1 `StreamBulkPreflightDialog` — selection moves inline to episode rows. **Absorbed**: V2 hotfix chain (BULK_MENU_RESTRUCTURE, GROUP_PARENT_ROW_ALIGNMENT, BULK_ORPHAN_RECOVERY, BULK_COHORT_PERSISTENCE_FIX) — Tankorent UI those targeted goes away; the underlying engine fixes (cohort persistence, orphan recovery, file-priority handling) stay load-bearing. |
| **Layer 3** | `docs/superpowers/specs/2026-05-10-stream-downloaded-library-design.md` | Awaiting Hemanth review; implementation shipped end-to-end | **Preserved**: `StreamDownloadIndex` + `stream_downloads.json` persistence + click-to-auto-play (Rule C) + right-click "Show alternate streams" (Rule D) + Videos-scanner skip + first-launch rescue migration + `Remove from Library` evicts. **Extended**: §2.2's deferred "in-flight visualization on Stream tile" is closed by this overhaul via the `DOWNLOADING` chip. **Extended**: `StreamDetailView`'s per-episode marker now includes in-flight states, not just downloaded. |

Two specifically-load-bearing carry-throughs to flag:

- The 2026-05-12 wake's STREAM_DOWNLOAD_INDEX_BACKFILL + WIRING_HOIST RTCs (uncommitted in working tree at brainstorm-time) close the index wiring that this overhaul depends on. They land alongside this overhaul's executing-plans.
- The 2026-05-12 wake's instrumentation in `StreamDetailView::onEpisodeActivated` (uncommitted) was diagnostic-only; it can stay or be removed during executing-plans — non-blocking.

## 4. Decisions Locked (this brainstorm)

Hemanth ratified these during the 2026-05-12 brainstorm. They MUST NOT be re-litigated during plan-writing or implementation without an explicit re-open.

| ID | Decision | Rationale |
|---|----------|-----------|
| **P1** | **Primary surface = inline on StreamDetailView's episode list.** Per-row triggers + state + controls; per-season-header triggers + controls. No preflight dialog. | Hemanth verbatim: "the show's detail view IS the download surface. Everything happens there. No popup, no modal, no dialog." |
| **P2** | **Secondary surface = sidebar-drawer "Downloads" entry → new full page.** Two sections: Active (in-flight grouped by show) and History (completed grouped by show). 90-day retention. | Cross-show one-glance view, matches Netflix mobile's Downloads tab; sidebar entry pattern matches the SOURCES_SIDEBAR drawer that already hosts Tankorent / Tankoyomi / TankoLibrary. |
| **P3** | **Sequential within each show, parallel across shows.** Cohort scheduler (Layer 2 Phase 2) unchanged: one episode active per show's cohort. No cross-show concurrency cap. | Hemanth's pick when offered global-sequential vs within-show-sequential vs capped-parallel. |
| **P4** | **DOWNLOADING text chip on tile, parallel QSS to DOWNLOADED.** In-flight wins when both apply; flips to DOWNLOADED when cohort completes. | Match the existing Layer 3 chip QSS family; gray, no color, no count, no icon. Visual minimalism per Hemanth's verbatim. |
| **P5** | **Selection UX: checkboxes always visible + per-row trigger icon.** Each row has a checkbox AND a single morphing icon. Season header has "Download Season" (queues every row) AND "Download Selected (N)" (queues only checked). | Hemanth's pick when offered four selection-flow variants; gives both quick-trigger and explicit-subset. |
| **P6** | **Row control affordance: single morphing icon + cancel in right-click.** Icon swaps glyph per state: [↓] not-downloaded → [⏸] downloading → [▶] paused → [✓] done → [↻] failed. Cancel hidden in row's right-click menu. | Matches Tankoban's existing minimalism discipline; one button per row; cancel is destructive so right-click is appropriate. |
| **P7** | **Season header pattern mirrors row pattern.** One morphing button: "Download Season" → "Pause Season" → "Continue Season". Right-click that button OR the season-combo → "Cancel Season". Plus a second button "Download Selected (N)" visible only when ≥1 checkbox checked. | Pattern parity with rows; visual consistency. |
| **P8** | **Cancel deletes files; Pause preserves files.** Per-episode cancel deletes that one file. Cancel Season deletes the entire cohort's files (including already-completed episodes). Pause never deletes. | Hemanth verbatim: "Cancel = the files for the cancelled episodes are automatically deleted from disk… Pause leaves files in place." |

Sensible defaults locked under Rule 14 (no Hemanth menu-ing):

- **Data model:** reuse existing `stream_bulk_groups.json` + `stream_bulk_canonical_maps.json` + `stream_downloads.json` (Layer 3 index). ZERO new persistence files. Extend TTL on `stream_bulk_groups.json` from 7d to 90d so History section reads it directly.
- **Failure handling:** existing cohort-state machinery already labels failed states. Per-row [↻] retry icon dispatches `retryStreamBulkGroupFailedItems(groupId)` (already in TorrentClient).
- **Restart resilience:** inherited from Layer 1 §6.4 + Layer 2 cohort persistence + Layer 3 lazy validation. No new restart logic.
- **Storage path:** inherited from Layer 1 §6 + Layer 3 Rule F.
- **Sidebar entry icon:** new SVG `resources/icons/downloads.svg` (grayscale, ~16px, ↓-into-tray motif — same family as Layer 3's `downloaded.svg`).

## 5. Architecture

### 5.1 File inventory

```
NEW    src/ui/pages/stream/StreamDownloadsPage.h            ~60 LOC
NEW    src/ui/pages/stream/StreamDownloadsPage.cpp          ~350 LOC

MOD    src/ui/pages/stream/StreamDetailView.h + .cpp        +~250 / -~80 LOC
       (row checkbox column + morphing icon column + season-header
        controls + cohort-state-aware paint passes; removes preflight-
        dialog trigger code)

MOD    src/ui/pages/stream/StreamLibraryLayout.h + .cpp     +~30 LOC
       (DOWNLOADING chip alongside existing DOWNLOADED chip; both
        live-updated on entriesChanged / streamBulkGroupsChanged)

MOD    src/ui/pages/TankorentPage.h + .cpp                  -~150 LOC
       (remove stream-group-row rendering branch; remove group context
        menu; remove group right-click "Restart group" + "Remove +
        Delete Files" path that only existed for stream bulks)

MOD    src/ui/pages/StreamPage.h + .cpp                     +~40 LOC
       (wire StreamDownloadsPage; no preflight dialog path)

MOD    src/ui/MainWindow.h + .cpp                           +~30 LOC
       (new sidebar drawer entry "Downloads" + new page in
        m_pageStack + activatePage routing)

MOD    src/ui/sidebar/SidebarDrawer.h + .cpp                +~10 LOC
       (new entry registration)

MOD    src/core/torrent/TorrentClient.cpp                   +~10 LOC
       (extend pruneTerminalStreamBulkGroups TTL from 7d to 90d;
        emit streamBulkGroupsChanged on cohort-state transitions
        so the Downloads page + tile chips re-render live)

MOD    src/core/torrent/TorrentClient.h                     +~5 LOC
       (signal streamBulkGroupsChanged() declaration)

DEL    src/ui/dialogs/StreamBulkPreflightDialog.h + .cpp    -~180 LOC
       (V2 Phase 1 surface, removed)

NEW    resources/icons/downloads.svg                        small
NEW    resources/icons/download-arrow.svg                   small (per-row trigger glyph; may already exist)
NEW    resources/icons/pause-circle.svg                     small
NEW    resources/icons/play-circle.svg                      small
NEW    resources/icons/retry-arrow.svg                      small
       (downloaded.svg from Layer 3 reused for [✓] state)

MOD    CMakeLists.txt                                       +~6 entries / -~2 entries
```

Total estimated impact: net **+~600 LOC** across 1 new page class + 8 modified files + 4-5 new icon assets, minus the deleted preflight dialog and the removed Tankorent stream-row rendering. The footprint is mostly UI; the engine layer changes are minimal (TTL bump + one new signal).

### 5.2 Component responsibilities

**`StreamDownloadsPage` (NEW class)** — Stream's cross-show downloads view. Owned by `MainWindow`, hosted as a sibling of TankorentPage in `m_pageStack`. Navigated to via the SidebarDrawer "Downloads" entry.

- Reads `TorrentClient::m_streamBulkGroups` snapshot (via a new `streamBulkGroupsSnapshot()` public accessor or via existing query methods).
- Subscribes to a new `TorrentClient::streamBulkGroupsChanged()` signal for live updates.
- Renders two sections: **Active** (cohorts with any non-terminal item) and **History** (cohorts where all items are in `Published`/`Completed`/`Cancelled` terminal states).
- Each show card: collapsed by default; expand-to-see-episodes inline. Tapping the show title jumps to that show's `StreamDetailView` (existing navigation).
- Each card carries the same morphing-button pattern as the season header: Pause Season / Continue Season + right-click Cancel Season.
- History entries beyond 90 days don't render (gated by `pruneTerminalStreamBulkGroups`'s extended TTL).

**`StreamDetailView` (extended)** — primary download surface.

- Episode table gains TWO new columns prepended: `[Checkbox]` (col 0) and `[Action icon]` (col last). Existing columns (`#`, Title, Progress, Status) shift accordingly.
- New season-header layout: existing `m_seasonCombo` + new `m_downloadSeasonBtn` (morphing) + new `m_downloadSelectedBtn` (visible only when ≥1 row checked).
- New row paint pass `paintRowDownloadState(row, snapshot)`: for each row, derive state from the cohort snapshot + `StreamDownloadIndex::filePathFor` and update the row's icon glyph + status text + checkbox enable/disable.
- New context-menu entries: Cancel (when row is in flight or paused or completed) + existing "Show alternate streams" (preserved).
- The 1Hz `m_bulkPollTimer` from V2 Phase 3 drives both the Progress % cell text AND the morphing-icon glyph swap.

**`StreamLibraryLayout` (extended)** — tile chip rendering.

- Existing `DOWNLOADED` chip logic untouched (Layer 3 §7.1).
- New `DOWNLOADING` chip with identical QSS family. Visible iff TorrentClient reports the show's imdb has any non-terminal cohort item (via a new `TorrentClient::imdbHasActiveCohort(imdbId)` query).
- Conflict rule: when both chips would apply, only `DOWNLOADING` renders (in-flight wins). Same `entriesChanged` + `streamBulkGroupsChanged` subscription model.

**`TankorentPage` (modified)** — filter-out stream rows.

- The existing `renderTorrentRow` + group-row branches keep functioning for non-stream rows.
- New top-of-render filter: `if (info.streamGroupId.isNotEmpty()) continue;` — stream rows skipped entirely.
- The existing Tankorent-UI group context menu code (the QMenu construction + action wiring inside `TankorentPage::showGroupContextMenu`, plus the group-row rendering branches that drove it) becomes unreachable since stream-group rows no longer render there. The underlying TorrentClient APIs (`cancelStreamBulkGroup`, `restartStreamBulkGroup`, `retryStreamBulkGroupFailedItems`, `pauseTorrent`/`resumeTorrent` per-item) are STILL USED by the new surfaces and stay live. **Plan-writing decides** whether to delete the dead Tankorent UI menu code outright or keep it as scaffolding for a future non-stream batch grouping feature.

**`TorrentClient` (extended)** — minimal engine-side changes.

- `pruneTerminalStreamBulkGroups` TTL: 7d → 90d constant.
- New signal `streamBulkGroupsChanged(const QString& groupId)` emitted on cohort-state transitions (item state change, retry dispatch, group GC).
- New public query `imdbHasActiveCohort(const QString& imdbId) const` returning `bool` — walks `m_streamBulkGroups` for any non-terminal item under any group keyed `stream:<imdbId>:*`. Used by tile-chip render.
- All other engine methods (cohort scheduler, retry-source-pick, publish pipeline, orphan recovery) are unchanged.

**`StreamPage` (extended)** — slim wiring layer.

- Subscribes `StreamDetailView` to `TorrentClient::streamBulkGroupsChanged` for live progress repaints.
- Removes the `onBulkSourcesReady` slot that previously opened the preflight dialog; the new flow goes directly from `StreamDetailView::downloadSeasonRequested` → `StreamBulkDownloader::begin()` → orchestrator-emits-ready → MainWindow's existing `onAddToTankorentBulkRequested` slot (which itself becomes `onStreamBulkDispatchRequested` and stops routing to Tankorent UI).

**`MainWindow` (extended)** — new page + new sidebar entry.

- New `StreamDownloadsPage*` member; constructed alongside other pages in `buildPageStack`.
- New sidebar entry registration: `m_sidebar->addEntry("Downloads", ...)` routing to `activatePage(PAGE_STREAM_DOWNLOADS)`.
- The existing `onAddToTankorentBulkRequested` slot is RENAMED to `onStreamBulkDispatchRequested` and no longer page-switches to Tankorent; it stays on the current page (the user was on StreamDetailView, they stay there) while libtorrent picks up the dispatch in the background.

### 5.3 Threading

No new threading. All UI live on the GUI thread. `streamBulkGroupsChanged` emitted from TorrentClient's existing GUI-thread state-transition hooks. The 1Hz `m_bulkPollTimer` polling pattern from V2 Phase 3 unchanged.

## 6. Data Model

### 6.1 Reused persistence

| File | Owner | Role in overhaul |
|---|---|---|
| `<dataDir>/stream_bulk_groups.json` | TorrentClient | **Primary source of truth for in-flight + recent history.** TTL extended to 90 days. Already contains `groupId`, `sourceIds.seriesId`, `sourceIds.season`, items[], state, lastError, destinationKey, etc. New surface reads from this directly. |
| `<dataDir>/stream_bulk_canonical_maps.json` | TorrentClient | Unchanged — used by the publish/rename pipeline only. Cleaned up by lazy GC after canonical files land + index registration. |
| `<dataDir>/stream_downloads.json` | StreamDownloadIndex (Layer 3) | Unchanged — the "what's on disk" lookup. Drives the [✓] downloaded marker on rows and the DOWNLOADED tile chip. |
| `<dataDir>/stream_downloads_meta.json` | StreamRescueScanner (Layer 3) | Unchanged — first-launch rescue migration version pin. |
| `<dataDir>/stream_library.json` | StreamLibrary | Unchanged — what shows the user has added to Stream library. |
| `<dataDir>/torrents.json` | TorrentClient | Unchanged — libtorrent records + per-torrent `streamGroupId` field (Layer 1). |
| `<dataDir>/stream_progress.json` | StreamProgress | Unchanged — Continue Watching integration. |

### 6.2 No new persistence files

The overhaul deliberately avoids new JSON stores. Rule 14 rationale:

- Active downloads already persist in `stream_bulk_groups.json` (Layer 1).
- Completed-and-on-disk downloads already persist in `stream_downloads.json` (Layer 3).
- History within the 90-day window is the same `stream_bulk_groups.json` with extended TTL.
- The new surface is a render layer over existing state, not a new state layer.

### 6.3 Schema additions

None. `TorrentInfo` already has `streamGroupId` from Layer 1. The bulk-group schema is unchanged. The `StreamDownloadIndex` entry shape is unchanged.

### 6.4 Read APIs added on TorrentClient

```cpp
// Mutex-guarded snapshot for the Downloads page render.
QJsonObject streamBulkGroupsSnapshot() const;

// True iff any non-terminal cohort item exists under any group
// keyed "stream:<imdbId>:*". Drives the DOWNLOADING tile chip.
bool imdbHasActiveCohort(const QString& imdbId) const;
```

`streamBulkSnapshotForImdbSeason` (Layer 2 Phase 3) is already public and reused for per-episode row state.

## 7. UI Specs

### 7.1 StreamDetailView — episode table

#### Column layout

```
┌─────┬─────┬───────┬──────────────────────┬──────────┬──────────┬──────────┐
│ [☑] │  #  │ thumb │ Title / overview     │ Progress │ Status   │ [Action] │
├─────┼─────┼───────┼──────────────────────┼──────────┼──────────┼──────────┤
│ ☑   │  5  │  ▮▮   │ The Grand Design     │   42%    │ Downloading [⏸] │
│ ☑   │  6  │  ▮▮   │ Requiem              │    -     │ Queued    │ [↓]    │
│ ☑   │  7  │  ▮▮   │ The Hateful Darkness │   100%   │   ✓ Done  │ [✓]    │
│ ☐   │  8  │  ▮▮   │ The Southern Cross   │ Paused   │ Paused    │ [▶]    │
│ ☐   │  9  │  ▮▮   │ Last Rites           │ Failed   │   Failed  │ [↻]    │
└─────┴─────┴───────┴──────────────────────┴──────────┴──────────┴──────────┘
```

- **Column 0 — Checkbox:** `QCheckBox` cell widget. Persists checked state across season-combo changes? No — checkbox state is per-(show, season) and resets on season change. `m_selectedEpisodes` is a `QSet<int>` member.
- **Column N+1 — Action icon:** single morphing icon as a `QPushButton` (flat, icon-only, fixed 24×24). State→glyph mapping in §7.2.
- All other columns inherit V2 Phase 3 + Layer 3 visual treatment unchanged.

#### Row click behavior

- Click anywhere on the row (NOT the checkbox, NOT the action icon, NOT the title cell-widget — i.e., on a "neutral" cell) → existing `onEpisodeActivated(row, col)` flow (Layer 3 Rule C auto-play if downloaded, else source-pick).
- Click the checkbox → toggle `m_selectedEpisodes`; update the season-header "Download Selected (N)" button visibility + count.
- Click the action icon → state-driven dispatch (§7.2 §State map).
- Right-click anywhere on the row → context menu (§7.3).

#### Row state map

| State | Action icon | Status cell text | Row click behavior |
|---|---|---|---|
| **Not downloaded, no in-flight cohort entry** | `[↓]` download-arrow | "—" or watched-checkmark from existing Layer 1 logic | Existing source-pick flow |
| **Queued (cohort item state = Pending, no infoHash yet OR cohort head not advanced)** | `[↓]` (greyed; click queues NOW if user wants to bypass cohort order — same effect as season click) | "Queued" | Source-pick falls back since no local file yet |
| **Resolving metadata (`MetadataPending`)** | `[⏸]` greyed | "Resolving…" | source-pick fallback |
| **Downloading** | `[⏸]` pause-icon | "%" | source-pick fallback (file not landed yet) |
| **Publishing (file rename in flight)** | `[⏸]` greyed | "Done" | source-pick fallback (file landing in seconds) |
| **Published (registered in StreamDownloadIndex)** | `[✓]` downloaded-checkmark | "✓" | Layer 3 Rule C auto-play |
| **Paused (user-paused, libtorrent paused, cohort still tracking)** | `[▶]` play-arrow | "Paused" | source-pick fallback |
| **Failed / MissingSource / MetadataFailed / PublishFailed / Orphaned / Cancelled** | `[↻]` retry-arrow | "Failed" (with tooltip on icon explaining sub-state) | source-pick fallback (file may not exist or be in transient state) |

#### Action icon click handling

| Current state | Action icon click effect |
|---|---|
| `[↓]` not downloaded | Single-episode dispatch via new `TorrentClient::dispatchStreamBulkSingleEpisode(imdbId, season, episode)` (calls existing single-magnet path under the hood). |
| `[⏸]` downloading | `pauseTorrent(infoHash)`; sets cohort item state to `Paused` (new `kStatePaused` constant) |
| `[▶]` paused | `resumeTorrent(infoHash)`; cohort item back to `Downloading` |
| `[✓]` published | Same effect as row-click → Layer 3 auto-play |
| `[↻]` failed | `retryStreamBulkGroupFailedItems(groupId, [itemKey])` — single-item retry |

The `dispatchStreamBulkSingleEpisode` path is a new wrapper that synthesizes a 1-episode `BulkDownloadItem` list and feeds it through the existing Layer 1 dispatch. It joins an existing cohort if one already exists for that (imdb, season), or creates a new cohort.

#### Season header

```
┌──────────────────────────────────────────────────────────────────────────┐
│ Season: [Season 2 ▾]    [Download Season ↓]  [Download Selected (3) ↓]   │
└──────────────────────────────────────────────────────────────────────────┘
```

- `m_downloadSeasonBtn`: single morphing button. State→label/icon:
  - No cohort or all-terminal cohort → "Download Season" `[↓]`
  - Any non-terminal items in cohort → "Pause Season" `[⏸]`
  - All items paused → "Continue Season" `[▶]`
- `m_downloadSelectedBtn`: visible only when `m_selectedEpisodes.size() >= 1`. Label "Download Selected (N)". Click queues only checked rows; clears checkboxes on dispatch.
- Right-click on the season-combo OR on `m_downloadSeasonBtn` → "Cancel Season" menu entry. Confirmation dialog: "Cancel and delete N files? This cannot be undone." Then `cancelStreamBulkGroup(groupId, deleteFiles=true)`.

#### State-from-cohort resolution

Each row's state is computed at paint time from:

1. `m_downloadIndex->filePathFor(imdbId, season, episode)` — if Some, row is **published** (downloaded).
2. Else, `streamBulkSnapshotForImdbSeason(imdbId, season)[episode]` — if present, row carries that cohort state (Pending / Downloading / Publishing / Failed / etc.).
3. Else, row is **not downloaded, no cohort entry** — default `[↓]` action.

The two queries are O(1) each; cheap to call per row per refresh tick.

### 7.2 Tile chips on Stream library home

#### `DOWNLOADING` chip — NEW

- **Position:** same corner as existing `DOWNLOADED` chip (Layer 3 §7.1) — top-left of poster, 10px in from each edge.
- **Style:** identical QSS to `DOWNLOADED` chip: `color: #eee; background: rgba(0,0,0,190); padding: 3px 7px; font-size: 9px; font-weight: 600; letter-spacing: 0.4px; border-radius: 4px`. (Same as the 2026-05-11 tidy pass.)
- **Text:** `"DOWNLOADING"` — uppercase, no count, no progress.
- **Trigger:** `TorrentClient::imdbHasActiveCohort(imdbId)` returns true.
- **Conflict rule:** when both `DOWNLOADING` and `DOWNLOADED` would apply (the show has some episodes downloaded AND some in flight or paused), ONLY `DOWNLOADING` renders. The chip flips to `DOWNLOADED` only when the last cohort item reaches a TERMINAL state (`Published` / `Completed` for success; `Cancelled` / `Failed` / `Orphaned` / `MissingSource` / `MetadataFailed` / `PublishFailed` for terminal-failure). A user-paused cohort still shows `DOWNLOADING` because the cohort is "in flight from the user's POV"; pausing is a control, not a terminal state.
- **Live update:** library home subscribes to `streamBulkGroupsChanged()` and re-evaluates affected tile chips. No full-page rebuild.

### 7.3 Right-click context menu on episode rows

- "Cancel" — visible only when row is in flight, paused, or published. Deletes the file (per P8). Confirmation if row is published or paused; no confirmation if row is queued or downloading (cheap to re-dispatch).
- "Show alternate streams" — always visible (preserves Layer 3 Rule D).

That's it. Two-item menu. No Restart, no Move-to-top, no Rename — those are out of scope.

### 7.4 Sidebar drawer entry "Downloads"

- New entry registered in `SidebarDrawer` alongside Tankorent / Tankoyomi / TankoLibrary.
- Icon: `resources/icons/downloads.svg` (NEW).
- Label: "Downloads".
- Click → `activatePage(PAGE_STREAM_DOWNLOADS)` → routes to the new `StreamDownloadsPage`.

### 7.5 StreamDownloadsPage layout

```
┌───────────────────────────────────────────────────────────────────────────┐
│ Downloads                                                                 │
│                                                                           │
│ ACTIVE                                                                    │
│                                                                           │
│   ▸ Daredevil: Born Again · Season 2 · 4/8 done · 50%      [⏸]            │
│   ▸ Severance · Season 2 · 2/9 done · 22%                  [⏸]            │
│   ▸ The Boys · Season 3 · 1/6 done · 17%                   [⏸]            │
│                                                                           │
│ HISTORY                                                                   │
│                                                                           │
│   ▸ Invincible · Season 1 · 8/8 · 3 days ago                              │
│   ▸ The Bear · Season 2 · 10/10 · last week                               │
│   ▸ One Piece · Season 1 · 13/13 · last month                             │
└───────────────────────────────────────────────────────────────────────────┘
```

- Two `QFrame` sections with header labels `ACTIVE` and `HISTORY`.
- Each show card is a collapsible `QWidget` with `[▸]` / `[▾]` indicator. Click the indicator OR the card body to toggle expand.
- Expanded card shows per-episode rows in a smaller `QTableWidget`-or-inline-list layout (decision deferred to plan-writing — `QTableWidget` adds row-management complexity, inline `QHBoxLayout` is simpler).
- Card-header morphing button (Active section only):
  - Any non-terminal → "Pause" `[⏸]`
  - All paused → "Continue" `[▶]`
  - Right-click card → "Cancel cohort" entry.
- Tapping the show title (not the morphing button) navigates to `StreamDetailView` for that show + season. Same `activatePage(PAGE_STREAM) + showEntry(imdbId, season, ...)` pattern as the library tiles.
- Empty state: when no active and no history-within-90d cohorts, page shows "No downloads yet. Open any show in Stream and click Download Season."
- Refresh trigger: subscribes to `streamBulkGroupsChanged()`; full repaint debounced 500 ms.

### 7.6 Tankorent visual delta

- TankorentPage's transfers list filters out any row with `streamGroupId.isNotEmpty()` at the render layer.
- The existing group-row rendering code in TankorentPage stays in the codebase but becomes unreachable from the stream-bulk path. **Plan-writing decides** whether to delete it outright or leave it as scaffolding for a future non-stream batch-add-from-URL grouping feature (the latter was hinted in Layer 1 §11 audit item 3).
- The right-click context menu on Tankorent groups (Pause / Resume / Restart / Cancel / Cancel + Delete) is preserved structurally for non-stream groups (manual magnet adds, currently flat-rendered, may opt into grouping in the future).
- All stream-domain affordances that currently live in Tankorent (Restart group, group-level Cancel + Delete Files, etc.) MIGRATE to the new surfaces: Restart → row [↻] retry icon + season-level reset-via-cancel-then-redownload; Cancel + Delete Files → right-click row Cancel (per-episode) and season-header right-click Cancel Season.

## 8. Data Flow

### 8.1 User clicks "Download Season" on Daredevil S02 from a clean slate

```
1.  StreamDetailView::onDownloadSeasonClicked():
        ─ build episode list from m_seasons[2]
        ─ call StreamPage::triggerBulkSeasonDownload(season=2)
2.  StreamPage::triggerBulkSeasonDownload(2):
        ─ NEW: skip preflight dialog; orchestrator runs source-pick fan-out
        ─ instantiate StreamBulkDownloader(metadata, ...)
        ─ connect signals (sourcesReady, dispatchFailed)
        ─ bulk->begin()
3.  StreamBulkDownloader (Layer 1 logic, unchanged): fans out N=8 aggregators,
    runs pack-priority pick OR per-episode cascade, builds canonical-name map,
    emits sourcesReady(summary, items, canonicalMap, groupId)
4.  StreamPage::onBulkSourcesReady():
        ─ NEW: no preflight dialog; commit immediately
        ─ emit streamBulkDispatchRequested(label, items, groupId, ...)
5.  MainWindow::onStreamBulkDispatchRequested():
        ─ NEW: no page-switch to Tankorent; user stays on StreamDetailView
        ─ call torrentClient->dispatchStreamBulkGroup(label, items, groupId, ...)
6.  TorrentClient::dispatchStreamBulkGroup(): (existing Layer 1 + V2 logic)
        ─ adds magnet(s) via libtorrent
        ─ writes group entry to stream_bulk_groups.json
        ─ cohort scheduler (V2 Phase 2) advances head item
        ─ emit streamBulkGroupsChanged(groupId)
7.  StreamDetailView's bulk-poll timer (1Hz, V2 Phase 3) ticks:
        ─ snapshot = streamBulkSnapshotForImdbSeason("tt18923754", 2)
        ─ paint per-row state (status text + morphing icon)
        ─ season-header button morphs from "Download Season" to "Pause Season"
8.  StreamLibraryLayout subscribes to streamBulkGroupsChanged:
        ─ Daredevil tile's DOWNLOADING chip appears
9.  StreamDownloadsPage (if user navigates to it later) subscribes too:
        ─ Active section shows the new Daredevil S02 card
10. As each episode completes (Layer 1 publish pipeline):
        ─ file lands at canonical path
        ─ TorrentClient::registerEpisode → StreamDownloadIndex
        ─ StreamDownloadIndex::entriesChanged → StreamDetailView paints [✓]
                                              + StreamLibraryLayout paints DOWNLOADED chip
                                                (but DOWNLOADING still wins per P4)
11. When last cohort item lands → imdbHasActiveCohort returns false →
        DOWNLOADING chip disappears → DOWNLOADED chip remains.
```

### 8.2 User clicks single-episode `[↓]` on E11 of Daredevil S02 (rest of season already on disk)

```
1.  StreamDetailView's row 11 [↓] icon clicked.
2.  Call torrentClient->dispatchStreamBulkSingleEpisode("tt18923754", 2, 11):
        ─ synthesizes 1-item BulkDownloadItem
        ─ joins existing cohort if "stream:tt18923754:s02:*" group exists,
          else creates new cohort with single item
3.  Same cohort scheduler + publish pipeline as above.
4.  Row 11 morphs through Queued → Downloading → Publishing → ✓ Done.
```

### 8.3 User Selects 3 episodes and clicks "Download Selected (3)"

```
1.  Three rows have checkboxes checked → m_downloadSelectedBtn visible
2.  StreamDetailView::onDownloadSelectedClicked():
        ─ filtered episode list from m_seasons[s] by m_selectedEpisodes
        ─ same path as triggerBulkSeasonDownload but with the subset
3.  Same orchestrator + dispatch flow. Same cohort representation.
4.  Checkboxes clear on dispatch.
```

### 8.4 User right-clicks the season-combo → Cancel Season

```
1.  Context menu shows "Cancel Season".
2.  Confirmation dialog: "Cancel and delete 8 files? This cannot be undone."
3.  Yes → torrentClient->cancelStreamBulkGroup(groupId, deleteFiles=true):
        ─ existing Layer 1 logic + V2 hotfix path
        ─ files on disk deleted (including already-completed ones — P8)
        ─ libtorrent records removed
        ─ stream_bulk_groups.json entry pruned
        ─ StreamDownloadIndex::evictByImdb on the matched imdb (if all
          episodes of that imdb come from this cohort)
4.  streamBulkGroupsChanged emitted.
5.  StreamDetailView rows all reset to [↓] not-downloaded state.
6.  Tile chips: DOWNLOADING gone; DOWNLOADED also gone (no entries left).
7.  StreamDownloadsPage Active card disappears.
```

### 8.5 User clicks row [⏸] on a downloading episode

```
1.  TorrentClient::pauseTorrent(infoHash)
2.  NEW: cohort item state transitions Downloading → Paused (new
    constant kStatePaused = "Paused" — distinct from "Pending").
3.  Cohort scheduler does NOT auto-advance from a user-paused item
    (currently does NOT advance from Pending either, so the semantic
    is: cohort head stays on user-paused episode; user must hit [▶] to
    continue or right-click → Cancel to release the slot).
4.  Row icon morphs [⏸] → [▶]; status text "Paused".
```

### 8.6 User clicks row [↻] on a failed episode

```
1.  TorrentClient::retryStreamBulkGroupFailedItems(groupId, [itemKey])
   (extended from the existing group-level retry to accept an optional
    item-key filter; if filter empty, retries all failed in group).
2.  Existing source-pick rerun fires for that one episode.
3.  Row state transitions Failed → Pending → Downloading → ...
```

## 9. Lifecycle and edge cases

### 9.1 App quit mid-cohort

- libtorrent resume data + stream_bulk_groups.json + canonical_maps.json all persist.
- On relaunch: addFromResume loop + reconcileStreamBulkGroups + cohortMaybeAdvanceAll fire (the 2026-05-11 BULK_COHORT_PERSISTENCE_FIX ordering preserved).
- StreamDetailView's bulk-poll-timer kicks in when user navigates to the show; rows repaint correctly.

### 9.2 User pauses a downloading episode, then opens the player on an already-downloaded episode

- Pause + play are independent; libtorrent keeps paused torrent in session; player opens local file via Layer 3 Rule C.
- No cross-effect.

### 9.3 Show removed from Stream library mid-cohort

- `StreamLibrary::remove(imdbId)` already evicts `StreamDownloadIndex` per Layer 3 §6.5.
- NEW: also call `cancelStreamBulkGroup(groupId, deleteFiles=true)` for every active cohort with `sourceIds.seriesId == imdbId`. Confirmation prompt: "Active downloads will be cancelled and files deleted. Continue?"
- Plan-writing implements the cancel-on-remove wiring.

### 9.4 User cancels a single episode mid-publish (file rename in flight)

- Race between user click on row [⏸]/Cancel and the file-rename completing.
- libtorrent's `cancel + delete_files` handles partial files cleanly. If rename already completed when cancel fires, the canonical file gets deleted; if rename hadn't completed, the staging file gets deleted.
- StreamDownloadIndex eviction: lazy validation on next page refresh will clear the now-missing entry (Layer 3 §6.4).

### 9.5 Failed episode with no source available

- Item state goes to `MissingSource` (Layer 1 / V2 hotfix coverage).
- Row shows [↻] retry. Click → source-pick rerun. If still no source, returns to MissingSource. Loop until Hemanth cancels or a source surfaces (no auto-retry; user-driven).

### 9.6 User starts a download and immediately navigates away from Stream

- Dispatch and cohort scheduler keep running in TorrentClient regardless of UI page.
- Returning to Stream → tile chip + detail view rows reflect current state via the live subscriptions.

### 9.7 Mixed-state season (some episodes downloaded, some in flight, some failed, some untouched)

- Each row independently reflects its own state via the §7.1 state map.
- Season header morphing button reflects the dominant state: any non-terminal → "Pause Season" (in-flight wins); all-terminal mixed → button reverts to "Download Season" (offering to re-dispatch any not-yet-downloaded rows).

### 9.8 Re-dispatching a season that has some downloaded episodes

- "Download Season" with some [✓] rows still queues every row.
- The orchestrator's existing skip-if-exists path (Layer 1 §5.4) detects already-on-disk files and skips them.
- StreamDownloadIndex remains the source of truth; double-registration is idempotent.

### 9.9 90-day history cliff edge

- A cohort that completed 89 days ago is in History; on the 91st day it gets pruned by the extended `pruneTerminalStreamBulkGroups`.
- Pruning is at-save-time + at-load-time; no separate scheduled pruner.
- If the user has the Downloads page open when the cohort prunes, the page-refresh subscription removes the card on next signal.

## 10. Transition (clean-slate cutover)

Hemanth explicit 2026-05-12: existing in-flight bulk downloads can be wiped during cutover. No migration required.

### 10.1 Cutover procedure

When executing-plans phase ships:

1. **Pre-ship Hemanth step:** Hemanth manually cancels any in-flight cohort via the current Tankorent UI (right-click group → Cancel + Delete Files). Files for completed episodes can be preserved per his choice (right-click Cancel without files-delete, OR keep the cohort visible until he's ready to nuke).
2. **Ship the overhaul.** TankorentPage filter activates; remaining stream-bulk groups (if any) silently disappear from the UI but the underlying state-machine still runs in the background. They become invisible-but-present cohorts.
3. **Optional post-ship cleanup:** Hemanth can navigate to the new Stream Downloads page; any pre-cutover cohorts appear in either Active (if non-terminal) or History (if terminal). He can right-click → Cancel from there.

### 10.2 No persistent migration code

No `migrationVersion` pin, no migration JSON, no progress dialog. The overhaul simply changes which UI surface renders the existing data.

### 10.3 Rollback path

If the overhaul needs to be reverted:

- Revert the TankorentPage filter clause → stream-bulk groups re-appear in Tankorent.
- Revert the StreamDetailView column additions → episode list reverts to V2 Phase 3 layout.
- Revert the sidebar entry + StreamDownloadsPage → page disappears.
- All persistence stays intact (no schema bumps).

## 11. Open items for plan-writing

These are details that plan-writing resolves, NOT brainstorm decisions:

- **Exact icon glyphs** — SVG design pass for `download-arrow.svg`, `pause-circle.svg`, `play-circle.svg`, `retry-arrow.svg`, `downloads.svg` (sidebar). Grayscale, ~12-16px, matching the existing icon family. `downloaded.svg` from Layer 3 reused as-is.
- **Card expansion mechanism on Downloads page** — `QTableWidget` per card vs inline `QHBoxLayout` (§7.5). Lean inline layout for simplicity.
- **`kStatePaused` constant** — explicit new cohort item state distinct from `Pending`. Plan-writing decides whether to bake it into the existing `StreamBulkItemState` enum or treat "user-paused" as a flag on existing `Pending`/`Downloading` states.
- **`dispatchStreamBulkSingleEpisode` wrapper** — implementation detail; likely a thin wrapper over the existing dispatch path with a single-item input.
- **`retryStreamBulkGroupFailedItems` extension** — accept an optional `itemKey` filter for per-row retry (§8.6).
- **Sidebar drawer entry icon SVG content** — design pass.
- **The dead-code question on TankorentPage** — keep the stream-group rendering code as scaffolding for future non-stream batch grouping, OR delete outright. Lean delete (YAGNI; Layer 1 §11 future-extensions don't justify the dead-code overhead).
- **TankorentPage row-filter clause exact placement** — pre-render filter vs post-render hide; pre-render is simpler.
- **Per-episode pause/resume granularity** — the engine API `pauseTorrent(infoHash)` already exists; cohort scheduler interaction with user-paused items needs a clarification pass (currently the scheduler only advances on Published/Completed transitions; a user-paused item doesn't block advance? does block? — clarify).
- **`onAddToTankorentBulkRequested` → `onStreamBulkDispatchRequested` rename** — straightforward find/replace; flag for plan-writing to include the routing-graph adjustment.
- **The pre-existing 2026-05-12 uncommitted instrumentation** in `StreamDetailView::onEpisodeActivated` — keep as defensive trace, or remove. Plan-writing decides.

## 12. Open items for Agent 7 audit (if Hemanth fires Trigger C)

Optional audit pass before plan-writing — Hemanth's call:

- **A1:** Tankorent row-filter risk — does the existing TankorentPage transfers-list logic have any side-effects (sort order, totals row, etc.) that break if half the rows silently vanish?
- **A2:** Cohort-scheduler interaction with user-paused items — is `kStatePaused` distinct enough from `Pending` to not confuse `cohortMaybeAdvance`? Should `Paused` block advance (current effect) or release the slot for the next-in-line?
- **A3:** Single-episode dispatch joining an existing cohort — does the existing dispatch path tolerate "add 1 item to an existing group" or does it assume groups are dispatch-time-immutable?
- **A4:** `streamBulkGroupsChanged` signal emission frequency — too noisy if emitted on every progress tick? Should be on state-transition only?
- **A5:** TTL bump 7d → 90d — does `pruneTerminalStreamBulkGroups`'s implementation scale to 90 days of accumulated entries on a busy user? Storage growth?
- **A6:** Downloads page render performance with 50+ cards — pagination needed, or lazy expand-only?
- **A7:** TankorentPage stream-row filter — what's the right place in the render pipeline to filter (data layer? widget-tree layer? both?). Compare against the Layer 3 VideosScanner skip pattern for symmetry.
- **A8:** Conflict semantics if a user manually adds a `magnet:` URL via Tankorent that happens to match an existing stream-bulk group's infoHash — does the dedup logic stay correct?

## 13. Cursor

- Brainstorm: ✅ closed 2026-05-12
- Spec: ✅ this document
- Awaiting: Hemanth review.
- Next gate (Hemanth-fired): /superpowers:writing-plans
- Implementation gate (Hemanth-fired): /superpowers:executing-plans

## 14. References

- **Layer 1:** `docs/superpowers/specs/2026-05-07-stream-bulk-download-design.md` — STREAM_BULK_DOWNLOAD shipped Phases 0–7.
- **Layer 2 (V2):** chat.md RTCs for STREAM_BULK_DOWNLOAD_V2 Phases 1+2+3 + hotfix chain (no formal spec file).
- **Layer 3:** `docs/superpowers/specs/2026-05-10-stream-downloaded-library-design.md` — STREAM_DOWNLOADED_LIBRARY.
- **In-flight wake fixes (2026-05-12, uncommitted at brainstorm-time):** STREAM_DOWNLOAD_INDEX_BACKFILL + STREAM_DOWNLOAD_INDEX_WIRING_HOIST + diagnostic instrumentation + DOWNLOADED chip tidy in StreamLibraryLayout — all land alongside this overhaul's executing-plans.
- **SOURCES_SIDEBAR drawer pattern:** Agent 5 SOURCES_SIDEBAR (chat.md 2026-05-05 ~15:02).
- **feedback_no_color_no_emoji.md** — grayscale-no-emoji visual identity discipline.
- **feedback_decision_authority.md** — Rule 14 architectural-vs-product split.

---

**End of spec. Awaiting Hemanth review. Subsequent phases (`/superpowers:writing-plans` and `/superpowers:executing-plans`) fire separately.**

---

## 15. Post-ship revisions

### 15.1 Downloaded-row UX revision (2026-05-12, post-P3 / post-ship)

Hemanth-directed revision after the first eye-check. §7.1 / §7.3 didn't anticipate the visual + interaction overlap of having BOTH a left-column download marker AND a right-column action icon for downloaded rows — the left-column marker was redundant chrome.

**Decisions:**

- **D-15.1.a** — Drop the left-column downloaded marker. `kColEpisode` (the `#` column) carries the episode number only; no icon overlay. `refreshEpisodeMarkers()` retained as a no-op sweep (clears stale icons / tooltips on first refresh after upgrade, keeps the existing `entriesChanged` / `libraryChanged` signal subscriptions valid).
- **D-15.1.b** — The right-column action icon for `RowState::Published` becomes a tick (`:/icons/check.svg`) AND is the click-target for row actions. `enabled=true`, tooltip "Downloaded — options".
- **D-15.1.c** — Cancel → Remove label morph on Published rows. The action menu's first entry reads "Remove" when row state is `Published`, "Cancel" for in-flight states (Downloading / Publishing / Paused / Queued). Engine path unchanged — both labels call `TorrentClient::cancelStreamBulkItem(groupId, itemKey, deleteFile=true)`.
- **D-15.1.d** — Confirmation dialog text adapts: Published reads "Remove this download? The file will be deleted from disk."; Paused keeps the prior "Delete this episode's file from disk?" wording; non-destructive states (Queued / Downloading / Publishing) skip confirmation.
- **D-15.1.e** — Right-click context menu (Layer 3 Rule D) is preserved. Both the tick-click path (in `onActionIconClicked`'s Published branch) and the right-click path (in `onEpisodeContextMenu`) converge on a shared helper `showRowActionsMenu(season, episode, globalAnchorPos)` so the menu always reads the same way.

**§7.1 row state-map override (Published row only):**

| State | Action icon | Status cell text | Row click behavior | Tick click behavior |
|---|---|---|---|---|
| **Published** | `[✓]` check.svg | "✓" | Layer 3 Rule C auto-play (unchanged) | NEW — opens Remove + Show alternate streams menu |

All other states unchanged.

**Carry-through references:** post-P3 RTC in `agents/chat.md` 2026-05-13 carrying revision; `feedback_no_color_no_emoji.md`; `feedback_ui_grouping_by_intent.md` (Remove = post-completion delete; Cancel = in-flight abort); plan source: `docs/superpowers/plans/2026-05-12-stream-downloads-netflix-overhaul.md` Task 11 (`actionIconForState`) + Task 14 (`onEpisodeContextMenu`).
