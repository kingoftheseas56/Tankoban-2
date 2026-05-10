# STREAM_DOWNLOADED_LIBRARY — Design Spec

**Date:** 2026-05-10
**Author:** Agent 4 (Stream mode)
**Status:** Awaiting Hemanth review
**Brainstorm pipeline:** /superpowers:brainstorming → THIS SPEC → /superpowers:writing-plans → /superpowers:executing-plans
**Hemanth-locked rules (verbatim from wake prompt):** A–G (see §1.1).
**Hemanth-locked product calls (this brainstorm):** P1–P6 (see §3).

---

## 1. Intent

Make Stream mode the playback gateway for episodes that have been bulk-downloaded to disk. The user clicks a show in Stream library home, sees Cinemeta-rich detail (poster, episode titles, descriptions, season nav), clicks an episode — if the file is on disk, the local-file player launches immediately with no source-pick; if not, the existing source-pick flow runs. A right-click "Show alternate streams" on any episode (downloaded or not) provides the streaming escape hatch.

This overhaul closes both shipped problems flagged by Hemanth on 2026-05-10:

- **P1 (subfolder bloom in Videos):** becomes irrelevant. Stream-mode UI renders from Cinemeta metadata — folder structure on disk doesn't drive the Stream UI. Files can land in any layout.
- **P2 (UI loss on hand-off):** closed directly. User stays in Stream mode for playback; never bounces to Videos for downloaded episodes.

### 1.1 Locked rules from the wake prompt (A–G)

- **Rule A** — Downloaded episodes show up in Stream mode UI, not Videos mode UI (for playback purposes).
- **Rule B** — Show-thumbnail in Stream library home gets a "downloaded" badge / tag.
- **Rule C** — Click an episode whose file is on disk → auto-play (no source-pick popup, no streaming roundtrip).
- **Rule D** — Right-click an episode (downloaded or not) → "Show alternate streams" affordance lets user fall back to streamed source.
- **Rule E** — Undownloaded seasons / episodes still show streams as today.
- **Rule F** — Download path stays the same. One of the user's existing Videos root folders is the storage destination. No new Stream-private cache folder.
- **Rule G** — Folder structure on disk no longer drives the UI. Stream mode's UI renders from Cinemeta metadata; the local file is just the playback source when present.

These rules are locked. The spec explores **how**, not **whether**.

## 2. Scope

### 2.1 In scope (v1)

- Per-episode "downloaded" tracking in a new sibling JSON `stream_downloads.json`.
- Tile badge: binary `DOWNLOADED` chip on Stream library tiles for shows with ≥1 downloaded episode.
- Detail-view per-episode marker (downloaded ↔ streamable visual distinction).
- Click-to-auto-play for downloaded episodes (bypasses source-pick + StreamPlayerController; routes through `MainWindow::openVideoPlayer(localPath)` with synthetic Stream-mode metadata).
- Right-click "Show alternate streams" on episode rows → existing `StreamSourceList` overlay; alternate-stream playback is a one-shot (does NOT modify the downloaded-state of the episode).
- Videos-mode scanner skips files registered in `stream_downloads.json` (single-source-of-truth marker).
- First-launch retroactive rescue: scan Videos roots for canonical-layout folders + Cinemeta title+year match → populate index. One-shot, with a progress dialog.
- Lazy disk-state self-healing: stat the canonical path on Stream library refresh + on episode-row click; missing files get evicted from the index, file re-appears in Videos on next scan.
- Continue Watching strip integration: downloaded plays count as Stream playbacks, write to `stream_progress.json` like any other Stream session.
- Subtitles for downloaded playback: `SubtitlesAggregator` runs (using imdbId+season+episode from the index entry), so OpenSubs language list is available — same UX as streamed playback. Embedded mkv subs continue to work natively per existing player.

### 2.2 Out of scope (v1) — explicit NOs

- **Movie support.** No "Download movie" trigger; movies stay stream-only. Migration scan ignores movie-shaped folders. Future arc.
- **Auto-delete on Remove from Library.** Files always stay on disk; only index entries clear.
- **In-flight bulk visualization on Stream tile.** Tankorent group row still owns "currently downloading" UX; Stream tile only flips to `DOWNLOADED` after the FIRST episode lands.
- **Manual user-triggered migration.** First-launch auto-rescue is the only migration path.
- **Multi-state stateful tile badge** (`S03`, `12/45 ep`, etc.). Binary only.
- **Per-show "auto-delete watched / keep last N" storage policies.** Future arc.
- **Cross-device sync of downloaded state.** Local-only.
- **Smart re-download / quality upgrades for already-downloaded episodes.** Future arc.
- **Filesystem watcher (`QFileSystemWatcher`)** on Videos roots. Three-layer lazy validation is sufficient.
- **Pre-registration at bulk-dispatch time.** Index entries land at per-episode-completion only — accept the brief in-flight transient where Videos may show files before bulk completes.
- **Re-match Cinemeta** action for migration mistakes. User recovery is Remove from Library + re-bulk.
- **Season-combo decoration** ("(downloaded)" suffix on season selector). Per-episode markers convey state at episode level for v1.

### 2.3 Future extensions to flag (do NOT design)

- Movie support (separate v2 brainstorm).
- "Download all seasons" bulk trigger (already noted as future in the prior bulk spec).
- Per-show storage policy.
- Quality-upgrade path (re-download if better source surfaces).
- "Re-scan for stream downloads" manual action in Stream library settings.
- "Re-match Cinemeta" action for migration corrections.
- Filesystem watcher for active drift detection.
- Per-episode bulk trigger ("download just S03E09").

## 3. Decisions Locked (this brainstorm)

| ID | Decision | Rationale |
|---|----------|-----------|
| **P1** | **Videos×Stream relationship: Stream owns, Videos hides bulk-downloaded files.** Files land at canonical paths under Videos roots (Rule F). A stream-side index tells the Videos scanner to skip them. The show no longer appears in Videos library; Stream is the sole UI. | Closes both P1 (subfolder bloom) and P2 (UI loss) cleanly. Most invasive option, but Hemanth's verbatim ("I am going to lose the UI of the stream mode") signals he wants Stream-mode primacy. |
| **P2** | **Migration: rescue retroactively, auto on first launch.** Scan Videos roots for canonical-layout folders, match to Cinemeta via title+year, register in index. One-shot, with progress dialog. | Avoids leaving the user with a hybrid library indefinitely. The canonical naming convention (Plex/Jellyfin) is regex-detectable. |
| **P3** | **Tile badge: binary `DOWNLOADED` chip.** Appears if ≥1 episode of the show is on disk. Same chip QSS as the existing `STREAM` Tankorent group chip. | Matches Hemanth's verbatim ("a tag or badge of downloaded"). Mixed-state nuance is revealed inside the detail view via per-episode markers. |
| **P4** | **Remove from Library: leave files on disk, clear index entry. Files re-appear in Videos.** Non-destructive default. | Removing from Stream Library is purely a library-tracking action; files are too valuable to auto-delete by default. Rebuilds via re-add + rescue. |
| **P5** | **Movie scope: series-only v1.** Movies stay stream-only forever in this overhaul. | Hemanth's verbatim is 100% series-language. Cleanest v1 scope. |
| **P6** | **Persistence: sibling `stream_downloads.json` keyed by canonical path.** Single JSON file. Mirrors the existing `stream_library.json` / `stream_progress.json` sibling pattern. | Cleanest single-source-of-truth; canonical-path key is naturally O(1) lookup; one JSON to migrate / debug. |

## 4. Data Model

### 4.1 `stream_downloads.json` schema (NEW persistence file)

Lives at `<dataDir>/stream_downloads.json` alongside `stream_library.json` and `stream_progress.json`. Owned by a new `StreamDownloadIndex` class.

```json
{
  "version": 1,
  "byPath": {
    "<canonicalKey>": {
      "imdbId":         "tt6741278",
      "type":           "series",
      "season":         1,
      "episode":        1,
      "canonicalPath":  "<absolute filesystem path>",
      "addedAt":        1778401234567,
      "sourceGroupId":  "stream:tt6741278:s01:1746615720123",
      "fileSizeBytes":  856100000
    }
  }
}
```

**Fields:**

- **`canonicalKey`** is a normalized form of `canonicalPath` for lookup: `QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()).toLower()`. Handles case-insensitive Windows paths + slash normalization. The dictionary key.
- **`canonicalPath`** is the original (display-form) path used for opening. Persisted alongside the key for traceability.
- **`sourceGroupId`** preserves traceability back to the Tankorent group row that produced the file (already exists on `TorrentInfo` per the prior bulk work). Empty string `""` for migration-rescued entries.
- **`fileSizeBytes`** is a cheap drift signal — if the file at `canonicalPath` exists but its size has changed since the entry was written, the entry is suspect. Used by lazy validation to flag for reconciliation but NOT to evict on its own (eviction only on file-missing).

### 4.2 In-memory shape

`StreamDownloadIndex` exposes three lookup maps, all O(1), all mutex-guarded:

| Map | Key | Value | Used by |
|---|---|---|---|
| `m_byPath` | `canonicalKey` | full `Entry` | `VideosScanner` skip lookup; `validateAll` stat |
| `m_byEpisode` | `"<imdb>:<NN>:<NN>"` (encoded tuple) | `canonicalKey` | `StreamDetailView` per-episode "downloaded?" state; episode-row click dispatch |
| `m_imdbHasAny` | `imdbId` | `bool` | `StreamLibraryHomeView` tile-badge render |

All three maps are derived from `m_byPath` on load. Mutations (add, evict) update all three atomically under `QMutex`.

### 4.3 `StreamLibraryEntry` — NO changes

The existing entry shape stays flat per the persistence-shape decision (P6). Tile badge uses `StreamDownloadIndex::hasAnyForImdb(imdbId)`, not a field on the library entry.

### 4.4 `TorrentInfo` — NO new fields

The prior bulk work already added `streamGroupId`. The `StreamDownloadIndex` reads it via the bulk-completion handler when writing per-episode entries. No further extension.

### 4.5 Persistence invariants

- **Atomic write.** `JsonStore::commitToDisk` (the existing safe-write path with the latest-values-map separation, fixed in REPO_HYGIENE Phase 4) is reused. Either the entire `stream_downloads.json` lands or the previous version is preserved — no torn writes.
- **Forward-compat only.** `version: 1` for v1; future bumps reseed via the same protected-defaults migration pattern used by `AddonRegistry`.
- **Eviction is non-destructive.** Removing an entry never touches the file on disk.

## 5. Architecture

### 5.1 New classes

**`StreamDownloadIndex`** (`src/core/stream/StreamDownloadIndex.{h,cpp}`, ~250 LOC) — the persistent download tracker. Owned by `MainWindow` (single instance, passed by pointer to subscribers). Threadsafe via `QMutex` because the VideosScanner reads it from a worker thread.

```cpp
struct Entry {
    QString imdbId;
    QString type;
    int     season;
    int     episode;
    QString canonicalPath;
    qint64  addedAt;
    QString sourceGroupId;
    qint64  fileSizeBytes;
};

class StreamDownloadIndex : public QObject {
    Q_OBJECT
public:
    void registerEpisode(QString imdbId, int s, int e,
                         QString canonicalPath, QString sourceGroupId,
                         qint64 fileSizeBytes);
    void evictByImdb(const QString& imdbId);
    void evictByPath(const QString& canonicalKey);
    void validateAll();   // off-thread; evicts missing entries

    bool isStreamOwned(const QString& canonicalKey) const;
    std::optional<QString> filePathFor(const QString& imdbId, int s, int e) const;
    bool hasAnyForImdb(const QString& imdbId) const;
    QList<Entry> entriesForImdb(const QString& imdbId) const;
    QList<Entry> all() const;

signals:
    void entriesChanged();
};
```

**`StreamRescueScanner`** (`src/core/stream/StreamRescueScanner.{h,cpp}`, ~200 LOC) — transient one-shot used only at first-launch migration. Walks `VideoCategoryStore`'s "Videos" roots, regex-matches the canonical-layout pattern, groups by show folder, queries Cinemeta `meta.json` once per matched show via the existing `MetaAggregator`, and pushes confirmed `(imdbId, season, episode, path)` tuples into `StreamDownloadIndex`. Runs on a `QThreadPool` worker; emits `progressUpdate` + `complete(stats)` signals. Skipped on subsequent launches via a `migrationVersion` pin in `<dataDir>/stream_downloads_meta.json`.

### 5.2 Modified files

| File | Change | LOC est |
|---|---|---|
| `src/core/stream/StreamDownloadIndex.{h,cpp}` (NEW) | core class | ~250 |
| `src/core/stream/StreamRescueScanner.{h,cpp}` (NEW) | first-launch migration | ~200 |
| `src/core/torrent/TorrentClient.cpp` | bulk-completion hook calls `StreamDownloadIndex::registerEpisode` | +~30 |
| `src/ui/MainWindow.{h,cpp}` | own StreamDownloadIndex instance + boot-time rescue gate; new `onPlayLocalFileFromStreamRequested(path, metadata)` slot | +~50 |
| `src/ui/pages/StreamPage.{h,cpp}` | wire `StreamDownloadIndex` into detail view + library home | +~40 |
| `src/ui/pages/stream/StreamDetailView.{h,cpp}` | per-episode-row downloaded marker; `onEpisodeActivated` branch on `filePathFor`; right-click context menu | +~150 |
| `src/ui/pages/stream/StreamLibraryHomeView.cpp` (likely `StreamHomeBoard`) | tile-badge decoration | +~30 |
| `src/core/scanner/ScannerUtils.cpp` (or wherever `walkVideosRoot` lives) | skip-if-stream-owned check | +~25 |
| `src/core/stream/StreamLibrary.cpp` | `remove(imdb)` also calls `StreamDownloadIndex::evictByImdb(imdb)` | +~5 |
| `resources/icons/downloaded.svg` (NEW) | episode-row marker icon | small |
| `CMakeLists.txt` | register new source/header pairs + icon resource | +5 |

**Total estimated impact:** ~775 LOC across 2 NEW class pairs + 7 MODIFIED files + 1 new icon asset. About 80% of the prior bulk-download spec's footprint.

### 5.3 Threading model

- `StreamDownloadIndex` lives on the GUI thread. Read APIs are `const` and mutex-guarded for VideosScanner worker reads.
- All write paths (`registerEpisode`, `evictByImdb`, `evictByPath`) run on the GUI thread.
- `entriesChanged()` is queued via `Qt::QueuedConnection` so VideosScanner workers don't accidentally trigger UI repaints from off-thread.
- `StreamRescueScanner` runs on a `QThreadPool` worker; emits results via queued signals to the index on GUI thread.

## 6. Data Flow

### 6.1 Bulk-download completes → index write

```
TorrentEngine::torrentFinished →
  TorrentClient::onTorrentFinished detects streamGroupId non-empty →
  existing publish/rename pipeline lands files at canonical paths →
  NEW: TorrentClient calls StreamDownloadIndex::registerEpisode per file
       (using the (groupId, fileIdx-or-infohash)→(imdb,s,e,canonicalPath) map
        already maintained for the rename step) →
  StreamDownloadIndex updates 3 in-memory maps + commits JSON →
  entriesChanged() fires →
  StreamLibraryHomeView refreshes tile badge for that imdbId →
  if StreamDetailView is currently rendering this show, episode rows re-render with markers.
```

### 6.2 Episode click in StreamDetailView (downloaded path)

```
StreamDetailView::onEpisodeActivated(row, col) →
  query StreamDownloadIndex::filePathFor(imdb, season, episode) →
  if present:
    stat the file →
      if missing: StreamDownloadIndex::evictByPath; fall through to source-pick;
                  show transient toast "File missing — falling back to streams."
      if present: emit playLocalFileFromStreamRequested(path, episodeMetadata)
  if absent (not downloaded): existing source-pick flow unchanged →
  MainWindow::onPlayLocalFileFromStreamRequested(path, meta) →
    m_videoPlayer->openFile(path) (same path VideosPage takes) +
    sets video-player title from Cinemeta metadata +
    SubtitlesAggregator::load(SubtitleLoadRequest{type, "imdb:s:e", synthetic Stream})
    so OpenSubs subs work the same as streamed playback +
    StreamProgress writes per-episode progress (Continue Watching integration)
```

### 6.3 Right-click episode → "Show alternate streams"

```
StreamDetailView::contextMenuEvent →
  QMenu shows single action "Show alternate streams" (universally available) →
  user clicks →
  existing source-pick flow runs (StreamSourceList overlay) →
  user picks → existing StreamPlayerController launchPlayer path →
  on close: index UNCHANGED. Downloaded file on disk still there;
            index entry intact; tile badge unchanged.
```

### 6.4 Videos scanner skip

```
VideosScanner::walkVideosRoot(<root>) for each file →
  compute canonicalKey = QDir::toNativeSeparators(...).toLower() →
  if StreamDownloadIndex::isStreamOwned(canonicalKey) → skip →
  else → emit videoFound() (existing path)
```

### 6.5 Remove from Library

```
StreamLibraryHomeView "Remove" action →
  StreamLibrary::remove(imdb) clears library entry →
  NEW: StreamLibrary::remove also calls StreamDownloadIndex::evictByImdb(imdb) →
  entriesChanged() fires; tile disappears from Stream library home →
  next VideosPage scan no longer skips these files → they re-appear in Videos →
  files on disk: UNTOUCHED.
```

## 7. UI Specs

### 7.1 Tile badge — `DOWNLOADED` chip

- **Position:** top-right corner of the show poster, matching the existing `STREAM` chip pattern.
- **Style:** existing `chipStyle` QSS — small-caps gray-background label, no color, no emoji, no icon (per `feedback_no_color_no_emoji.md`).
- **Text:** `"DOWNLOADED"` (matches verbatim ask).
- **Trigger:** visible iff `StreamDownloadIndex::hasAnyForImdb(imdbId)` returns true.
- **Live update:** the home view subscribes to `entriesChanged()` and re-decorates affected tiles in place — no full-page rebuild.

### 7.2 Episode-row downloaded marker in `StreamDetailView`

- Existing row layout (`# / Title / Description / Progress / Status`) is preserved.
- **New:** a small grayscale SVG icon prepended to the `Title` cell — uses a new asset `resources/icons/downloaded.svg` (simple downward-arrow-into-tray glyph, ~12px, gray fill matching the existing icon family).
- **Trigger per row:** `StreamDownloadIndex::filePathFor(imdbId, season, episode).has_value()`.
- **Tooltip:** `"On disk: <canonical filename>"`.
- **Self-healing:** `onEpisodeActivated` stats before launch; missing files are evicted, the row re-renders without the icon, click falls through to source-pick. Plus a transient toast `"File missing — falling back to streams"`.

### 7.3 Right-click → "Show alternate streams"

- **Trigger:** `contextMenuEvent` on any episode row (downloaded or not).
- **Menu shape (v1):** single action — `"Show alternate streams"`.
- **On click:** opens existing `StreamSourceList` overlay; live aggregator query for `<imdb>:<s>:<e>`. User picks; playback proceeds via existing `StreamPlayerController`.
- **Index invariant:** alternate-stream playback NEVER mutates the index. Downloaded file stays at canonical path; entry stays in index; tile stays badged.
- **Return-to-state:** on player close, user lands back on the same `StreamDetailView` row.

### 7.4 Mid-bulk state visualization

- **Tile badge:** appears as soon as the FIRST episode of a bulk lands at canonical path (`registerEpisode` per file, NOT at group completion).
- **Detail view per-episode marker:** appears per-episode as files complete.
- **In-flight progress is NOT shown in Stream UI.** Tankorent owns the download-monitoring UX.

### 7.5 Visual asset deltas

- **NEW** `resources/icons/downloaded.svg` — design pass during plan-writing. Suggested motif: simple downward arrow into a tray, gray fill, ~12×12px.
- **REUSE** existing `chipStyle` QSS class for the tile badge.
- **REUSE** existing tooltip QSS for the row marker.

## 8. Videos-mode integration

### 8.1 Scanner skip-check

The skip-check lives in `ScannerUtils::walkVideosRoot`:

```cpp
const QString canonicalKey =
    QDir::toNativeSeparators(QFileInfo(absPath).absoluteFilePath()).toLower();
if (m_streamDownloadIndex && m_streamDownloadIndex->isStreamOwned(canonicalKey)) {
    continue;  // skip — Stream-mode owns this file
}
emit videoFound(absPath, ...);  // existing path
```

`m_streamDownloadIndex` is passed into the scanner constructor by VideosPage at construction time.

### 8.2 Performance

`isStreamOwned` is a single `QHash::contains` against `m_byPath`. For 5 000 Videos files and 200 Stream-downloads, per-file cost is ~1 µs (mutex-guarded hash probe) — invisible against typical scanner cost. No measurable regression.

### 8.3 Live update — debounced rescan

VideosPage subscribes to `StreamDownloadIndex::entriesChanged` with a debounced re-scan: signal kicks a single-shot 500 ms `QTimer`. Subsequent signals restart the timer. When it fires, one rescan picks up all batched changes.

Handles two cases:
- Bulk-completion landing N episodes — N signals collapse to one rescan.
- Remove-from-Library evicting many entries — single rescan picks up un-hidden files.

### 8.4 Mid-bulk transient (accepted)

Between "bulk torrent finishes" and "publish/rename completes per file", canonical-named files briefly exist on disk but aren't yet registered. If user navigates to Videos in that ~seconds-long window, they appear. Once `registerEpisode` fires per file, the debounced rescan removes them.

Accepted as v1 behavior; documented out of scope to pre-register at dispatch time.

### 8.5 File moved by user via File Explorer

- **Within Videos roots:** old key fails `QFileInfo::exists()` on next stat → `evictByPath` clears stale entry → tile badge may flip false → file re-appears in Videos at new path.
- **Outside Videos roots:** same eviction; file effectively becomes invisible.
- **Renamed in place:** same as moved-within.

Lazy validation on next interaction is the recovery mechanism. No filesystem watcher in v1.

### 8.6 Drift signals (consolidated)

| Signal | Frequency | Cost |
|---|---|---|
| Lazy stat on `onEpisodeActivated` | per click | 1 stat |
| Eager `validateAll` at home load | per home-open | N stats off-thread |
| Reactive Videos rescan via debounced `entriesChanged` | per add/evict batch | one full scan |

## 9. Migration

### 9.1 Trigger

On `MainWindow` construction, after `StreamDownloadIndex` and `StreamLibrary` are loaded:
1. Check `<dataDir>/stream_downloads_meta.json` for `migrationVersion`.
2. If unset/absent → schedule `StreamRescueScanner::start()` for `QTimer::singleShot(0, ...)`.
3. If `migrationVersion >= 1` → skip silently.

### 9.2 Detection heuristic

For each Videos root, walk recursively and look for:

```
<root>/<showFolder>/<seasonFolder>/<file>.<ext>
```

Where:
- `<seasonFolder>` matches `^Season \d{2,3}$`.
- `<file>.<ext>` matches `^(.+) - S(\d{2,3})E(\d{2,3}) - (.+)\.(mkv|mp4|webm|m4v)$` — captures `(showTitle, season, episode, episodeTitle, ext)`.

**Sanity check:** the regex's `showTitle` capture must equal `<showFolder>` (after `sanitizePathSegment` normalization). If they diverge, skip the file.

Group surviving candidates by `<showFolder>`.

### 9.3 Cinemeta lookup per show

For each candidate show folder:

1. Sanitize (strip year suffix `(2021)` if present, normalize whitespace).
2. Query Cinemeta via `MetaAggregator::searchCatalog("series", searchTerm=showName)`.
3. **Match resolution:**
   - Single result → use its `imdbId`.
   - Multiple results → highest-`imdbRating` `type:"series"` result; record `"ambiguous_match"` in summary.
   - Zero results → skip the show; record as `"unmatched"`.

### 9.4 Per-episode registration

For each episode under a matched show:

1. Compute `canonicalKey`.
2. Stat file for size.
3. Call `StreamDownloadIndex::registerEpisode(imdbId, season, episode, canonicalPath, sourceGroupId="", fileSizeBytes)`.
4. Empty `sourceGroupId` distinguishes migration-rescued entries from real-bulk-completion entries.

### 9.5 StreamLibrary entry materialization

For each matched show whose `imdbId` is NOT already in `StreamLibrary`:
1. Fetch full metadata from Cinemeta (`name`, `year`, `poster`, `description`, `imdbRating`).
2. Call `StreamLibrary::add(entry)` with `addedAt = QDateTime::currentMSecsSinceEpoch()`.

### 9.6 Progress UX

A modal dialog `StreamRescueProgressDialog` (NEW, ~80 LOC) shown at scan start:

- Title: `"Migrating downloaded shows to Stream library"`
- Progress bar: percentage based on shows-matched / total-show-folders-found.
- Status line: current show name being processed.
- Cancel button: aborts mid-scan; partial results stay in index (idempotent — `migrationVersion` only written at full completion).

On complete: `"Done. Added <S> shows and <E> episodes to Stream library. <U> shows could not be matched to Cinemeta and remain in Videos."` + Close button.

### 9.7 Failure modes

| Mode | Action |
|---|---|
| No Cinemeta match for show name | Skip show; counted as `unmatched`; files remain in Videos |
| Ambiguous Cinemeta match | Pick highest `imdbRating` `type:"series"`; counted as `ambiguous_match` |
| File at canonical path is 0 bytes / unreadable | Skip the file (not the show) |
| Multi-language title not in Cinemeta index | Skip show; counted as `unmatched` |
| Show folder name has trailing year `(2021)` | Year stripped before Cinemeta lookup |
| Network failure to Cinemeta mid-scan | Show counted as `network_failure`; migration continues; `migrationVersion` NOT written → next launch re-runs |

### 9.8 Persistence of migration state

After full completion:

```json
{
  "migrationVersion": 1,
  "completedAt": 1778450000000,
  "stats": {
    "shows_matched": 12,
    "shows_unmatched": 2,
    "shows_ambiguous": 1,
    "episodes_registered": 89
  }
}
```

`migrationVersion: 1` gates future re-runs.

### 9.9 No automatic re-runs

Migration is strictly one-shot per `migrationVersion`. Files added by hand after migration are NOT auto-rescued — treated as Videos-side files.

## 10. Lifecycle + edge cases

### 10.1 Continue Watching integration

Downloaded playback uses the **same `StreamProgress` persistence path** as streamed playback. The local-file-vs-HTTP distinction is invisible to the progress layer: `epKey` shape unchanged, per-tick saves same, Continue Watching strip picks up entries unchanged.

### 10.2 Subtitles for downloaded playback

When `MainWindow::onPlayLocalFileFromStreamRequested` opens a downloaded file:

```cpp
SubtitleLoadRequest req;
req.type = "series";
req.id   = QString("%1:%2:%3").arg(imdb).arg(s).arg(e);

Stream synth;
synth.behaviorHints.filename  = QFileInfo(path).fileName();
synth.behaviorHints.videoSize = QFileInfo(path).size();
synth.behaviorHints.videoHash = "";   // omitted for local files
synth.source.fileNameHint     = synth.behaviorHints.filename;
synth.name                    = entry.title;
req.selectedStream = synth;

m_subtitlesAggregator->load(req);
```

OpenSubs aggregator runs the SAME way as for a streamed source — same popover, same tracks. Embedded mkv subs continue to work natively via the sidecar.

### 10.3 StreamLibrary materialization timing for NEW bulks

- **At dispatch time** (`StreamPage::triggerBulkSeasonDownload`): if show not in `StreamLibrary`, add immediately. Tile appears WITHOUT `DOWNLOADED` badge (no episodes landed yet).
- **At first per-episode completion**: badge appears.
- **At full bulk completion**: nothing special; `entriesChanged` already fired per episode.

### 10.4 Disk-state validation (consolidated)

| Layer | When | Cost |
|---|---|---|
| Lazy on click | `onEpisodeActivated` stats before launch | 1 stat |
| Eager at home load | `validateAll()` off-thread on home open | N stats <100 ms typical |
| Reactive rescan | Videos scanner debounces `entriesChanged` 500 ms | one scan per window |

### 10.5 Player close → return-to-state

Both downloaded playback and alt-stream playback close paths return user to the StreamDetailView at the row they invoked from. No accidental Videos-mode landing.

### 10.6 Detail view re-render on `entriesChanged`

`StreamDetailView` subscribes; updates per-episode markers in place (preserves scroll, selection, in-flight player launches).

### 10.7 Mixed-state seasons

User has S03 fully downloaded, S01/S02/S04/S05 not:
- S03 episodes show downloaded marker; click → auto-play.
- Other seasons' episodes show no marker; click → source-pick.
- Season combo decoration ("(downloaded)" suffix on S03) **deferred to plan-writing**.

### 10.8 New episode airs after migration

Tile badge stays `DOWNLOADED` (still has ≥1 episode). New episode row shows no marker → click → source-pick → stream normally.

### 10.9 Wrong Cinemeta match during migration

Files registered under wrong imdbId. Recovery v1: `Remove from Library` → entries evicted → files re-appear in Videos → user re-bulks under correct match. **NOT in v1**: "Re-match Cinemeta" action.

### 10.10 Bulk in flight when user invokes Remove from Library

Confirmation dialog: `"This show has an active bulk download. Cancel the download first, then Remove from Library."` with a `Cancel download + Remove` button that does both atomically. Pure-Remove without canceling is disabled while bulk is in flight.

## 11. Open items for plan-writing

These are details that the plan-writing phase resolves, NOT brainstorm decisions:

- **Exact regex** for canonical-layout detection — minor refinements to handle edge cases (multiple dots in show names, brackets, etc.).
- **Season-combo decoration** — whether to show "(downloaded)" suffix on the season selector, beyond per-episode markers. Deferred per §10.7.
- **`downloaded.svg` exact glyph** — design pass; suggested motif in §7.5 is non-binding.
- **Toast widget** for "File missing — falling back to streams" — reuse existing toast or new — implementation detail.
- **`StreamRescueProgressDialog` exact layout** — spec calls out content; QSS / spacing during plan.
- **Retry policy for `network_failure` shows during migration** — do they re-attempt within the same scan, or only on the next launch's re-run? §9.7 says next launch; could be tightened.
- **Order of operations in `StreamLibrary::remove`** — evict-before-or-after the existing remove() body — race-free either way; pick the cleaner one in plan.
- **Where exactly to schedule `validateAll()` on home load** — StreamLibraryHomeView constructor vs first show event vs explicit refresh. Tradeoffs minor.

## 12. Open items for Agent 7 audit (if Hemanth fires Trigger C)

Optional audit pass before plan-writing — Hemanth's call:

- A1: Is there a hidden coupling between `StreamDownloadIndex` and `VideoCategoryStore` we missed?
- A2: Does the canonical-layout regex correctly handle existing files (any edge cases the bulk spec didn't anticipate)?
- A3: `JsonStore::commitToDisk` invariants under crash-mid-write — does the post-Phase-4 path actually atomic-rename, or is there still a torn-write window?
- A4: Migration's Cinemeta-lookup rate-limiting — does `MetaAggregator::searchCatalog` throttle requests, or could a 100-show migration spam Cinemeta?
- A5: Threading correctness — VideosScanner reads `m_byPath` while GUI thread mutates it; the QMutex covers this, but are there reentrancy concerns inside `walkVideosRoot`?
- A6: SubtitlesAggregator with synthetic Stream — does the OpenSubs addon honor the `id` field with empty `videoHash` extras, or does it require non-empty extras to return results? (Likely fine per the just-shipped STREAM_SUBTITLES_NO_ADDON evidence — but worth a check for the local-file flavor specifically.)

## 13. References

- **Prior bulk-download spec:** `docs/superpowers/specs/2026-05-07-stream-bulk-download-design.md` — the foundation this overhaul builds on. Phases 0–7 shipped (RTCs in chat.md tail).
- **STREAM_SUBTITLES_NO_ADDON RTC** (2026-05-10 ~14:03pm) — fixed the OpenSubs addon-fetch path; downloaded playback inherits this fix via §10.2 synthetic Stream.
- **StreamLibrary persistence** — `src/core/stream/StreamLibrary.{h,cpp}`, `stream_library.json` sibling pattern.
- **JsonStore atomic writes** — `src/core/JsonStore.{h,cpp}` with REPO_HYGIENE Phase 4 fixes.
- **VideosScanner / ScannerUtils** — `src/core/scanner/ScannerUtils.cpp` recursive walker.
- **MetaAggregator** — `src/core/stream/MetaAggregator.{h,cpp}` for Cinemeta queries.
- **MainWindow::openVideoPlayer** — `src/ui/MainWindow.cpp:828`; the local-file player entry the new `onPlayLocalFileFromStreamRequested` will call.

---

**End of spec. Awaiting Hemanth review. Subsequent phases (`/superpowers:writing-plans` and `/superpowers:executing-plans`) fire separately.**
