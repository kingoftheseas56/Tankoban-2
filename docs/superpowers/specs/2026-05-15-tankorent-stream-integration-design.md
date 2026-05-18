# TANKORENT_STREAM_INTEGRATION — Design Spec

**Author**: Agent 4 (Stream mode owner)
**Date**: 2026-05-15
**Status**: RATIFIED by Hemanth in brainstorm conversation 2026-05-15 ~5pm GMT+5:30; written-spec review gate explicitly skipped at Hemanth's direction
**Brainstorm session**: `.superpowers/brainstorm/1704-1778839507/`
**Supersedes**: Same-day VIDEO_STREAM_MERGE brainstorm direction (pivoted away from 2026-05-15 ~4pm; see § Pivot history)
**Cross-domain**: Joint Agent 4 (Theatre/Stream) + Agent 4B (Tankorent/Sources) + Agent 5 (Library UX); Agent 3 explicitly NOT involved per 2026-05-15 directive

---

## 1. Motivation

Hemanth's verbatim framing 2026-05-15 ~4:30pm GMT+5:30:

> "tankorent IS healthy and torrents can be found and the only reason I would want to use tankorrent is for the sake of consistency. say if I want to watch the show, I want to watch the episodes from the same torrent, with the same quality and conistency. stream mode has a download season option but more often than not those downloads are a collection of diffrent torrent's episodes, so the quality varies."

The existing Stream-mode auto-download (STREAM_DOWNLOADS_NETFLIX_OVERHAUL, shipped just before this arc) Frankensteins a season's episodes from whatever torrents happen to have each one — fine for "I just want to watch this episode now" but poor for "I want to actually watch this show start to finish at a consistent quality." Tankorent's existing per-torrent download flow already produces cohesive packs (one torrent = one source = one quality across N episodes). The gap is that Tankorent downloads do NOT auto-register into Stream/Theatre's library, so the user has to track them manually — no show-view, no Continue Watching integration, no IMDB-keyed identity.

This arc bridges Tankorent downloads into the existing Stream/Theatre library substrate. The user picks a show from Cinemeta → picks a cohesive torrent pack from Tankorent's indexers → downloads → episodes auto-appear in the show-view with full Stream-mode UX.

---

## 2. The 20 ratified decisions

### Identity, source-priority, and progress

1. **Identity-bound progress per `(imdbId, season, episode)`** — one progress value per episode, single canonical store. Any playback (Tankorent-downloaded file, Stream auto-source) reads and writes the same value. Switching sources mid-episode loads at the persisted position; only scrub-back lowers it.
2. **Tankorent-downloaded file wins over Stream sources on click** — when a show-view episode tile has both a registered Tankorent file AND working Stream sources, click plays the local file; Stream is "switch source" fallback.
3. **Highest-quality wins on duplicate downloads** — if user downloads the same episode from two different torrents (e.g., 1080p pack then later a 4K pack), the higher-quality binding evicts the older one. Reuses the "highest-quality wins" semantic from the abandoned VIDEO_STREAM_MERGE brainstorm.

### Flow shape

4. **Show-first flow** — `/superpowers:brainstorming` answer A. User searches in Theatre top search → Cinemeta returns show cards → clicks show → show-view opens with IMDB identity in context → clicks "Download via Tankorent" on a season header → `TorrentPackPicker` modal opens with indexer fan-out scoped to that show+season. Identity is captured upfront because the user started from a Cinemeta show.
5. **Tankorent page repurposed** — existing standalone `TankorentPage` narrows to "Direct torrent search" for non-Cinemeta content (sports recordings, software, random torrents). Downloads from this path do NOT register into Stream/Theatre library — they land in Theatre's Local files section.
6. **Multi-season "complete series" packs surface prominently** — when `TorrentPackPicker` detects a torrent containing episode files spanning multiple seasons (e.g., "Sopranos.Complete.Series.1999-2007"), it surfaces as a special row at the top: "Get the whole show in one consistent pack."

### UI placement and naming

7. **Stream → Theatre rename** — user-facing strings rename: sidebar label, page title, settings keys, env vars (`TANKOBAN_STREAM_*` → `TANKOBAN_THEATRE_*` with one-release deprecation aliases), `tankoctl --page stream` → `--page theatre`. Internal C++ class names stay `Stream*` (`StreamPage`, `StreamLibrary`, `StreamDownloadIndex`) to avoid a high-risk grep-and-replace pass for zero user benefit.
8. **Videos mode tab is removed entirely** — the standalone Videos page goes away.
9. **Theatre gets a "Local files" section** — bottom row of Theatre library, reuses the existing `VideosScanner` capability to surface user-configured root folders containing personal content (Hemanth verbatim: "personal files and other stuff like sports"). No matching, no Cinemeta, no S/E extraction. Click → flat folder browser.
10. **Movies in v1 with full parity** — single-file movies (`Inception (2010).mkv`) bind identically to series. Click → local file plays.

### First-launch + onboarding + history

11. **Skip walkthrough** — no first-launch explainer screens. First launch behaves like every subsequent launch.
12. **Existing pre-arc Tankorent downloads are ignored** — clean break; only torrents downloaded via the new flow (from the show-first entry point) register into Theatre library. Old downloads remain on disk but are invisible to Theatre show-views. No "back-register history" pass.

### Sort and ranking

13. **Quality × seeders combined-score sort** — `TorrentPackPicker` sorts results by a combined score: `(quality × W_q + health × W_h) / (W_q + W_h)`. Quality score from filename tags (4K=100, 1080p=80, 720p=60, 480p=40, else=20) and source quality (BluRay > WEB-DL > HDTV > WEBRip). Health score from seeders: `log2(seeders + 1) × 10`, capped at 100.
14. **User-adjustable weight slider** — Theatre settings has a "Prefer quality / Prefer reliability" slider. Default weights `W_q=0.6, W_h=0.4`.

### Architecture

15. **No new external matcher class** — entire arc extends the existing `onFileRenamed → registerEpisode` hook at `TorrentClient.cpp:2554` to fire for the single-add Tankorent path (currently only fires for bulk-cohort downloads originating from Stream's auto-download flow).
16. **No AniList integration in v1** — the show-first flow eliminates the need for title→IMDB reverse-lookup, which was the only thing that needed AniList. Pure Cinemeta is enough.
17. **No filesystem scanner extension for matching** — the new arc does NOT scan arbitrary folders for show identification. The only filesystem-walk is the existing `VideosScanner`, repurposed to feed the Local files section.
18. **In-process work only, no subprocess, no new build target** — all new logic lives in the main Tankoban app process.

### Cross-domain ownership

19. **Joint Agent 4 + Agent 4B + Agent 5 arc** — split documented in § 10.
20. **Agent 3 explicitly NOT involved** per Hemanth's 2026-05-15 directive locking Agent 3 to video player work only.

---

## 3. Architecture overview

Three areas of work, almost entirely extending existing code:

1. **Identity passthrough plumbing** — `AddTorrentConfig` gets new `imdbId` + `season` fields, propagated through `TorrentClient` records to `onTorrentFinished`.
2. **Single-add completion hook** — fix the early-exit at `TorrentClient.cpp:2426` to call `StreamDownloadIndex::registerEpisode()` for Tankorent-originated downloads using the existing `BulkPackVerifier::matchEpisodeFileForSeason()` filename parser + existing `StreamDownloadIndex` registration.
3. **New show-first UI inside Theatre** — "Download via Tankorent" button in show-view → `TorrentPackPicker` modal that fans out indexer searches with show identity already in context.

No new classes for matching. No AniList client. No filesystem scanner extension. No subprocess. No JsonStore-extension layer beyond the existing `StreamDownloadIndex`. The whole identity-binding pipeline was already built by STREAM_DOWNLOADS_NETFLIX_OVERHAUL; this arc extends it to single-add Tankorent flows.

---

## 4. New UI surface in Theatre

### 4.1 Show-view enrichment (Agent 5)

Each season header in show-view gets a **"Download via Tankorent"** button alongside the existing Stream entry. Click → opens `TorrentPackPicker` inline (modal or expanded section, design-time call).

Existing show-view episode tiles get a small "local file available" chip in the corner when `StreamDownloadIndex::filePathFor(imdbId, S, E)` returns a path. Click priority is local-wins per decision 2; the chip is informational, not interactive.

### 4.2 `TorrentPackPicker` (Agent 4B, new)

Modal that fans out indexer searches with query `"{show name} S{season}"` plus reasonable variations (year-suffixed, year-prefixed, romaji for anime — same query-variation logic the existing Tankorent search uses).

Per-pack row:
- Pack name (e.g., `The.Sopranos.S06.1080p.BluRay-RARBG`)
- Detected quality from filename tags
- Detected episode count from torrent file list (via `BulkPackVerifier`-style filename scan on the metadata's file array)
- Seeders / leechers
- Total size
- "Download" action button

Sort: quality×seeders combined score, weights configurable via Theatre settings slider (decision 14).

Multi-season "complete series" packs (decision 6) surface as a special pinned row at the top with copy: "Whole show in one consistent pack — N seasons, M episodes."

### 4.3 Local files section (Agent 5)

Bottom row of Theatre library: **Local files**. Reuses the existing `VideosScanner` (no code changes to the scanner; its output gets routed to a new Theatre UI section instead of the now-removed Videos page).

Per-folder tile (top-level subdirectory of each configured root). No Cinemeta lookup. No matching. Click → opens a flat file browser for that folder; clicking a file → plays via the existing video player.

### 4.4 Repurposed Tankorent page (Agent 4B)

The existing `TankorentPage` stays in the sidebar but narrows scope. New page title: **"Direct torrent search"**. Use case: non-Cinemeta content the user wants to download anyway (sports recordings, software, random torrents). Downloads from this page have empty `imdbId`/`season` in `AddTorrentConfig`, so they do NOT register into `StreamDownloadIndex`. The downloaded files land on disk and surface in Theatre's Local files section automatically via the existing scanner.

### 4.5 Theatre rename (Agent 5)

Mechanical sweep:
- Sidebar entry label "Stream" → "Theatre"
- Page title, breadcrumbs, settings panel headers
- Settings keys `streamMode/*` → `theatreMode/*` (with one-release legacy-key fallback read so old QSettings still resolve)
- Env vars `TANKOBAN_STREAM_*` → `TANKOBAN_THEATRE_*` (with one-release deprecation aliases — both names work, the new name wins on conflict)
- `tankoctl --page stream` → `--page theatre` (with `stream` alias for one release)
- Telemetry scope tags
- Stream-search-widget placeholder strings

Internal C++ class names (`StreamPage`, `StreamLibrary`, `StreamDownloadIndex`, `StreamSearchWidget`, etc.) stay `Stream*`. The risk of a full code-symbol rename touching dozens of files for zero user-visible benefit was the explicit tradeoff Hemanth accepted in the brainstorm.

### 4.6 Removed: Videos mode tab

`VideosPage` sidebar entry removed. Codepath kept alive (the `VideosScanner` is reused by the Local files section), but the standalone page is no longer reachable from the sidebar.

---

## 5. Data flow — happy path

```
User types "Sopranos" in Theatre top search
  → MetaAggregator::searchByTitle("Sopranos", "series", callback)
  → Cinemeta returns show cards with imdbId="tt0141842"
  → User clicks Sopranos tile → StreamDetailView::showEntry("tt0141842")
  → Show-view renders with imdbId in m_currentImdb
  → User clicks "Download via Tankorent" on Season 6 header
  → TorrentPackPicker opens with context (imdbId="tt0141842", season=6, showName="The Sopranos")
  → Picker fans out to indexers (Nyaa / PirateBay / 1337x / EZTV) with query variations
  → Each indexer returns TorrentResult list (existing TorrentResult shape)
  → Picker enriches each result with BulkPackVerifier file-list scan to detect episode count
  → Picker filters to packs containing ≥N% of season's expected episode count (e.g., ≥80%)
  → Multi-season packs (episode files spanning multiple seasons) get flagged and pinned at top
  → Sort by quality × seeders combined score
  → User picks one row → AddTorrentDialog opens with:
       imdbId = "tt0141842"           ← NEW (passed from picker context)
       season = 6                       ← NEW (passed from picker context; or 0 for multi-season pack)
       category = "tv"
       destinationPath = (user-configured)
       contentLayout = "original"
       sequential = false
       startPaused = false
  → User confirms → m_client->startDownload(infoHash, config)
  → TorrentClient persists imdbId + season into m_records[infoHash]
  → libtorrent downloads
  → Per-file completion → onFileRenamed (existing path — works as today for bulk; works for single-add too because we just need the final path)
  → All-files complete → torrent_finished_alert → TorrentClient::onTorrentFinished(infoHash)
       ├─ Existing: if (!streamGroupId.isEmpty()) publishStreamBulkItemsForTorrent(infoHash)
       └─ NEW:      else if (!record["imdbId"].toString().isEmpty()) publishTankorentItemsForTorrent(infoHash)
  → publishTankorentItemsForTorrent enumerates downloaded video files:
       ├─ For each file: BulkPackVerifier::matchEpisodeFileForSeason(path, configSeason)
       │     → returns (detectedSeason, detectedEpisode) — uses regex S##E## etc.
       └─ For each successful match:
             m_streamDownloadIndex->registerEpisode(
               imdbId,
               detectedSeason,                    // detected, NOT config — handles multi-season packs
               detectedEpisode,
               file.path,
               QString("tankorent:") + infoHash,  // sourceGroupId — distinct from bulk's groupId
               QFileInfo(file.path).size()
             )
  → StreamDownloadIndex::registerEpisode handles dedup: if existing entry for (imdbId, season, episode):
       ├─ If new entry has higher quality score (decision 3) → evict old, insert new
       └─ Else → keep existing, drop new (no overwrite)
  → StreamDownloadIndex emits entriesChanged()
  → Theatre show-view refreshes — episode tiles for downloaded episodes show local-file chips
  → User clicks episode → file plays via existing player; progress writes to UnifiedProgressStore
```

Multi-season pack flow (decision 6) is identical except `config.season = 0`; `publishTankorentItemsForTorrent` relies on `BulkPackVerifier` to detect each file's season from its filename, so the per-episode registration spans multiple seasons correctly.

Repurposed Tankorent direct-search flow: `config.imdbId.isEmpty()` → `publishTankorentItemsForTorrent` is not called → file lands on disk → Theatre's Local files section surfaces it via existing scanner.

---

## 6. Identity passthrough — the small but critical plumbing

### 6.1 `AddTorrentConfig` (Agent 4B)

```cpp
// AddTorrentDialog.h — additions
struct AddTorrentConfig {
    // ...existing fields (category, destinationPath, contentLayout, streamGroupId,
    //   sequential, startPaused, selectedIndices, filePriorities)...

    QString imdbId;       // NEW — empty when not from show-first flow
    int     season = 0;   // NEW — 0 when not season-bound (multi-season pack or
                          //   non-show direct-search)
};
```

### 6.2 `TorrentClient::startDownload` (Agent 4)

Persist new fields into the per-torrent JSON record:

```cpp
record.insert(QStringLiteral("imdbId"), config.imdbId);
record.insert(QStringLiteral("season"), config.season);
```

### 6.3 `TorrentClient::onTorrentFinished` (Agent 4)

Change the early-exit at `TorrentClient.cpp:2426`. Currently:

```cpp
bool hasBulkGroup = !streamGroupId.isEmpty();
if (hasBulkGroup) {
    publishStreamBulkItemsForTorrent(infoHash);  // existing bulk path
}
emit torrentCompleted(infoHash);
if (hasBulkGroup) return;
// fall-through (single-add) → currently does nothing relevant for binding
```

Becomes:

```cpp
const bool hasBulkGroup = !streamGroupId.isEmpty();
const QString recordImdbId = record.value(QStringLiteral("imdbId")).toString();
const bool hasTankorentBinding = !hasBulkGroup && !recordImdbId.isEmpty();

if (hasBulkGroup) {
    publishStreamBulkItemsForTorrent(infoHash);   // existing bulk path
} else if (hasTankorentBinding) {
    publishTankorentItemsForTorrent(infoHash);    // NEW — single-add Tankorent bind
}
emit torrentCompleted(infoHash);
if (hasBulkGroup || hasTankorentBinding) return;
// fall-through (non-Cinemeta direct-search) → file lands in Local files section
//   automatically via the existing scanner; no registration needed
```

### 6.4 `TorrentClient::publishTankorentItemsForTorrent` (Agent 4, new ~50 LOC)

Sketch:

```cpp
void TorrentClient::publishTankorentItemsForTorrent(const QString& infoHash) {
    if (!m_streamDownloadIndex) return;

    const QJsonObject record = m_records.value(infoHash).toObject();
    const QString imdbId = record.value(QStringLiteral("imdbId")).toString();
    const int configSeason = record.value(QStringLiteral("season")).toInt(0);
    if (imdbId.isEmpty()) return;

    const QString sourceGroupId = QStringLiteral("tankorent:") + infoHash;

    // Enumerate downloaded video files for this torrent
    const QList<DownloadedFile> files = downloadedVideoFilesFor(infoHash);
    for (const auto& f : files) {
        const auto match = BulkPackVerifier::matchEpisodeFileForSeason(
            f.path, configSeason  // 0 → detect both season and episode
        );
        if (match.season <= 0 || match.episodeNum <= 0) continue;

        const qint64 fileSize = QFileInfo(f.path).size();
        m_streamDownloadIndex->registerEpisode(
            imdbId,
            match.season,
            match.episodeNum,
            f.path,
            sourceGroupId,
            fileSize
        );
    }
}
```

### 6.5 `StreamDownloadIndex::registerEpisode` (Agent 4)

Existing method already exists. Modification: add the highest-quality-wins dedup logic (decision 3):

```cpp
void StreamDownloadIndex::registerEpisode(...) {
    const QString episodeKey = computeEpisodeKey(imdbId, season, episode);

    QMutexLocker lock(&m_mutex);
    auto existing = m_byEpisode.find(episodeKey);
    if (existing != m_byEpisode.end()) {
        const Entry& oldEntry = m_byPath[*existing];
        if (qualityScore(newPath) <= qualityScore(oldEntry.canonicalPath)) {
            return;  // existing is equal or better quality; keep it
        }
        // New entry wins: evict old by-path entry
        m_byPath.remove(*existing);
    }
    // Insert/update new entry as before
    // ...
}
```

`qualityScore(path)` is a static helper applying the filename-tag parser from the picker side (decision 13) — same scoring function, called on the file's basename.

---

## 7. Quality scoring helper

`QualityScorer` static helper class (Agent 4B) shared by `TorrentPackPicker` and `StreamDownloadIndex` dedup:

```cpp
class QualityScorer {
public:
    static int resolutionScore(const QString& filename);  // 4K=100, 1080p=80, 720p=60, 480p=40, else=20
    static int sourceScore(const QString& filename);      // BluRay=100, WEB-DL=80, HDTV=60, WEBRip=50, DVDRip=40
    static int qualityScore(const QString& filename);     // weighted combo of resolution + source
    static int healthScore(int seeders);                  // log2(seeders + 1) × 10, capped at 100
    static double combinedScore(int quality, int health, double wQuality, double wHealth);
};
```

Tags detected via case-insensitive regex on filename basename. Same regex set used in the existing `ScannerUtils::cleanMediaFolderTitle` quality-strip pass — reuse, don't reinvent.

Settings slider: stored as single double `theatre.qualityWeight` in QSettings (range 0.0 to 1.0, default 0.6 = prefer quality). `wHealth = 1.0 - wQuality`.

---

## 8. `UnifiedProgressStore` refactor

Currently `StreamLibrary` + Stream-mode playback tracker + `VideosPage` playback tracker have separate progress concepts. Merge into one canonical store:

```cpp
class UnifiedProgressStore : public QObject {
public:
    // Episode-bound progress (Tankorent-downloaded files, Stream auto-sources, anything with IMDB identity)
    void   setProgress(const QString& imdbId, int season, int episode, double positionSec, double durationSec);
    double resumePositionFor(const QString& imdbId, int season, int episode) const;

    // Path-keyed progress (Local files section content, non-show torrents)
    void   setProgressByPath(const QString& canonicalPath, double positionSec, double durationSec);
    double resumePositionForPath(const QString& canonicalPath) const;

    // Continue Watching feed — merges both key types into one sorted list
    QList<ProgressEntry> continueWatchingEntries(int limit = 20) const;

signals:
    void progressChanged();  // any write fires this; CW strip listens

private:
    JsonStore* m_store;
    QHash<QString, ProgressEntry> m_byEpisodeKey;  // "imdb:S:E" → entry
    QHash<QString, ProgressEntry> m_byPathKey;     // canonicalPath → entry
};
```

The CW leak fix (separate pending TODO) is subsumed by this refactor — one canonical store, no leak by construction.

Migration: existing per-mode progress data is dropped on first launch of the new build per decision 12 ("existing pre-arc data ignored"). Clean break.

---

## 9. Error handling + edge cases

- **Torrent finishes with no recognizable episode files** (extras-only pack, weird filenames) — `publishTankorentItemsForTorrent` enumerates zero matches; no `registerEpisode` calls. The downloaded files remain on disk and surface in Theatre's Local files section automatically.
- **Same episode bound twice from different torrents** — `StreamDownloadIndex::registerEpisode` runs the highest-quality-wins dedup (§ 6.5). If new is higher quality, old binding evicts (the old file remains on disk, just no longer surfaces in show-view). If equal/worse, new binding is silently dropped.
- **User deletes a Tankorent-downloaded file from disk** — existing `StreamDownloadIndex::validateAll()` runs at scan-start and evicts entries pointing to missing paths. Show-view episode tile loses local-file chip; Stream sources become primary again.
- **Multi-season pack with unusual filename layout** (e.g., `Sopranos/Season01/S01E01.mkv` vs `Sopranos/S01/01.mkv`) — `BulkPackVerifier::matchEpisodeFileForSeason` regex handles common cases (`S##E##`, `##x##`). Outlier filenames fail to bind silently; user falls back to manual via the abandoned right-click "Re-match" workflow if needed (not in v1 scope).
- **Cinemeta query during show-first search fails** (network down, server error) — Theatre top search shows "no results" or last-known cache. No degradation specific to this arc; same as existing Stream search behavior.
- **Indexer queries during `TorrentPackPicker` fan-out fail** — picker shows partial results from indexers that succeeded + a small "indexer N offline" badge. Same pattern as existing Tankorent search.
- **AddTorrentDialog for show-first flow vs direct-search flow** — both routes use the same dialog; `imdbId` + `season` fields are pre-filled when arriving from the show-view button, empty when from direct-search. Existing dialog code handles empty fields gracefully (already does for the bulk path).
- **JsonStore commit failure** on the StreamDownloadIndex or UnifiedProgressStore writes — existing handling: emit failure event, don't lose in-memory state. No new failure mode.

---

## 10. Cross-domain split

### Agent 4 (Theatre / Stream mode) — me

- `AddTorrentConfig` field consumption in `TorrentClient::startDownload`
- `TorrentClient::onTorrentFinished` early-exit modification
- `TorrentClient::publishTankorentItemsForTorrent` (new ~50 LOC)
- `StreamDownloadIndex::registerEpisode` highest-quality-wins dedup logic
- `UnifiedProgressStore` refactor (consolidation of per-mode progress stores)
- Integration testing + Hemanth visual smoke

### Agent 4B (Tankorent / Sources)

- `AddTorrentConfig` new fields (`imdbId`, `season`)
- `TorrentPackPicker` modal UI + indexer fan-out with show-context query
- Pack enrichment (episode-count detection per pack via metadata file-list scan)
- `QualityScorer` helper class
- Quality × seeders combined score sort
- User-adjustable weight slider in Theatre settings panel
- Multi-season pack detection + top-of-picker pinning
- Repurposing existing `TankorentPage` as "Direct torrent search" (page title, scope narrowing, breadcrumb update)

### Agent 5 (Library UX)

- Theatre rename sweep (user-facing strings only — sidebar label, page title, settings keys with legacy-fallback, env vars with deprecation aliases, `tankoctl` page alias)
- Show-view "Download via Tankorent" button placement in season header
- Show-view episode tile local-file chip rendering (consuming `StreamDownloadIndex::filePathFor`)
- Local files section UI — bottom row of Theatre library, folder tiles consuming `VideosScanner` output
- Removing Videos mode sidebar entry

### Agent 3 (Video Player) — explicitly NOT involved

Per Hemanth's 2026-05-15 directive locking Agent 3 to video player work only. The player overlay's "switch source" button (for switching between Tankorent file and Stream sources mid-playback per decision 2) is a post-v1 ask; not v1-blocking. v1 simply plays whichever source the show-view click chose.

---

## 11. Testing strategy

### `tankoban_tests` pure-logic primitives (opt-in via `-DTANKOBAN_BUILD_TESTS=ON`)

- `AddTorrentConfig` identity field round-trip (set → persist to JSON → restore)
- `BulkPackVerifier::matchEpisodeFileForSeason` filename regex — existing test surface; add Tankorent-typical filename cases if not already covered (`Sopranos.S06E04.1080p.BluRay.x264-DEMAND.mkv`, etc.)
- `QualityScorer::resolutionScore` / `sourceScore` / `combinedScore` — input/output table on synthetic filenames
- `StreamDownloadIndex::registerEpisode` highest-quality-wins dedup — synthetic two-entry scenarios
- `UnifiedProgressStore` merge scenarios — local-write-then-stream-resume, scrub-back lowering, etc.

### Integration smoke (agent-runnable via `tankoctl` + MCP)

1. Configure a small legal multi-file test torrent (Big Buck Bunny multi-cut pack, or similar) with known filename structure.
2. Launch Theatre, search a synthetic show that maps to the test torrent's pack name.
3. Click show → click "Download via Tankorent" → pick the only result → wait for completion.
4. Verify via `tankoctl get-state` that the show now appears in Theatre library.
5. Verify via `tankoctl get-videos --imdb <id>` (or equivalent) that registered episodes match expected (S, E) tuples.
6. Click a registered episode → verify `tankoctl get-player` shows local file path, not a Stream-source URL.

### Hemanth visual smoke

- Real Sopranos S06 1080p BluRay pack via show-first flow.
- Verify show-view binding quality (correct season, correct episodes, correct file paths).
- Verify Continue Watching strip surfaces the show after partial playback.
- Verify highest-quality-wins by downloading the same season twice at different qualities and confirming the higher-quality binding survives.

---

## 12. Delivery slicing

Estimated 3-4 elapsed wakes; ~6 agent-wakes total (some parallelizable).

1. **`AddTorrentConfig` identity fields** + record persistence + `onTorrentFinished` hook + `publishTankorentItemsForTorrent` + `registerEpisode` dedup — *Agent 4, ~1 wake*
2. **Show-view "Download via Tankorent" button** + `TorrentPackPicker` modal + indexer fan-out with show context — *Agent 4B, ~1 wake*
3. **`QualityScorer` helper** + sort + user-weight slider — *Agent 4B, ~0.5 wake*
4. **Theatre rename** + Local files section + Tankorent page repurposing + Videos mode removal — *Agent 5, ~1 wake*
5. **`UnifiedProgressStore` refactor** — *Agent 4, ~1 wake*
6. **Integration smoke** + Hemanth visual verify — *Agent 4 owns, ~0.5 wake*

Parallelizable: 1 and 2 hit different files. 4 can run alongside both. 3 depends on 2. 5 is mostly independent of 1-4. 6 gates everything.

A reasonable assembly order: 1 + 4 parallel (different agents) → 2 → 3 → 5 → 6.

---

## 13. Pivot history

This spec is the result of a same-day pivot during the brainstorm:

- **2026-05-15 ~3:30pm**: Brainstorm opened as VIDEO_STREAM_MERGE — combine Videos mode + Stream mode by scanning local folders, matching them to Cinemeta + AniList, binding files to show-view episode slots. 19 strategic decisions locked across ~10 batches of clarifying questions.
- **2026-05-15 ~4:00pm**: Hemanth pivoted verbatim: *"I just changed my entire mind on local files. Our focus should shift to tankorrent download. ... everything I download is from a torrent, I can delete them and download again so tankorent is where we should be focusing."*
- **2026-05-15 ~4:15pm**: Tankorent surface-map exploration confirmed the bind-on-completion hook (`onFileRenamed → registerEpisode` at `TorrentClient.cpp:2554`) was already shipped by STREAM_DOWNLOADS_NETFLIX_OVERHAUL; only the single-add path needs extending.
- **2026-05-15 ~4:30pm**: Hemanth clarified motivation — consistency, not just convenience. Tankorent gives cohesive one-source packs; Stream auto-download Frankensteins quality per episode.
- **2026-05-15 ~4:45pm**: Show-first flow chosen (decision 4). Identity-capture-upfront eliminated the need for post-hoc torrent-name → Cinemeta enrichment. AniList dropped (decision 16).
- **2026-05-15 ~5:00pm**: All 20 decisions ratified. Design proposal presented and approved. Spec authoring initiated.

Original VIDEO_STREAM_MERGE design dropped in entirety; only the substrate-map work product carries forward (which is folded into § 3 and § 6 of this spec by reference).

---

## 14. Open call-outs

None. All design decisions ratified. The lone open question — highest-quality-wins on duplicate downloads — was confirmed yes by Hemanth immediately before this spec was written (decision 3).

---

## 15. Next step

Transition to `/superpowers:writing-plans` skill to produce the implementation plan that lands this design as concrete tasks per the slicing in § 12.
