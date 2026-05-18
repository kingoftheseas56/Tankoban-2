# TANKORENT_CINEMETA_PACK_MAPPING — Brainstorm + Design

**Arc:** TANKORENT_CINEMETA_PACK_MAPPING
**Authored:** 2026-05-18 (Agent 4)
**Owner:** Agent 4 (Stream mode) — concurrently owns the in-flight THEATRE_DOWNLOAD_OVERHAUL Phases D-G, so coordination is intra-agent, not cross-agent
**Status:** Spec drafted; awaiting Hemanth file-level review; implementation plan via `/superpowers:writing-plans` follows approval

---

## 1. Hemanth's vision (verbatim, from kickoff prompt)

> "now our next course of action is connecting the single season, multi-season, full series packs to the cinemato catalog, so when I open the torrent and click download, it actively has to map to individual episodes to the torrents contents before downloading, and the downloading UI remains the same as the normal downloading UI, maybe we can use a different color for each downloadd. the normal download has one colour, our tankorent sidebar result download will have a different colour. but you get my vision right?"

**Vision mirror ratified by Hemanth (batch 1, Q1):** When the user opens the Tankorent pack picker (single season / multi-season / full series) and clicks Download, the torrent's actual file contents are mapped to individual Cinemeta episodes **before** the download starts — not at the end like today. Per-episode progress + state surfaces in the same episode-row UI a normal addon-bulk download uses, with a visible differentiator so the user can tell at a glance which row is sourced from Tankorent vs which row is sourced from a standard Stremio addon. The downloading UI surface itself does not change — same row, same chip, same shape. Only the source-provenance differentiator varies.

---

## 2. Problem statement

### 2.1 Current state (2026-05-18, pre-arc)

`publishTankorentItemsForTorrent` ([src/core/torrent/TorrentClient.cpp:1788-1919](../../src/core/torrent/TorrentClient.cpp#L1788-L1919)) is the existing parser path. It fires from `onTorrentCompleted` ([line 2784](../../src/core/torrent/TorrentClient.cpp#L2784)) **after** a Tankorent-sourced torrent finishes downloading. The function:

1. Reads the torrent's record from `m_records` to extract `imdbId` + `season` (captured at download-init time by the show-first picker — TANKORENT_STREAM_INTEGRATION Task A2, shipped 2026-05-15)
2. Calls `m_engine->torrentFiles(infoHash)` to enumerate file entries
3. For each file, calls `BulkPackVerifier::matchEpisodeFileForSeason()` to parse (S, E) from the filename
4. For multi-season packs (configSeason==0), probes seasons 1..50
5. Calls `StreamDownloadIndex::registerEpisode()` for each parsed episode (creates an Entry record at the canonical path)
6. Falls back to `registerMovie()` for the largest video file if no episode matches

**The Entry record is binary** ([StreamDownloadIndex.h](../../src/core/stream/StreamDownloadIndex.h)): either an episode is **owned at this canonical path** (Entry exists) or **not owned** (Entry absent). There is no in-progress state. Episode rows in `StreamDetailView`'s table show ✓ in the `kColAction` column only once the parent torrent finishes.

### 2.2 Gap

Hemanth wants per-episode progress visible **during** download, not just at completion. The user-facing experience should be:

- Click Download → ~3 seconds later, all 13 episode rows light up with "Queued" chips
- Episode 1 starts: row chip flips to "Downloading 47%" with a progress bar
- Episode 1 completes: row flips to "Downloaded" ✓ — user can start watching while ep2-13 are still in flight
- All Tankorent-sourced rows carry a faint warm-amber tint distinguishing them from addon-bulk rows

### 2.3 Target state

`StreamDownloadIndex.Entry` carries a `state` enum and `progressPct`. The parser fires at `metadata_received_alert` time (when libtorrent first reports the file list) rather than at completion. Episodes register as `Pending` immediately, flip to `Downloading` as each file's first piece arrives, flip to `Complete` when the file finishes. The episode-row UI (both the existing `StreamDetailView` table AND the in-flight `EpisodeTile` widget from THEATRE_DOWNLOAD_OVERHAUL Phase D1) subscribes to state changes and re-renders. A faint warm-amber tint is applied to rows when `Entry.sourceGroupId.startsWith("tankorent:")` — render-time read, no schema field needed for provenance.

---

## 3. Decisions ratified

All decisions below are locked through Hemanth's brainstorm answers across three question batches plus the phasing approach pick. Decisions marked **(Hemanth)** were picked directly by Hemanth; decisions marked **(Agent 4)** are Rule-14 technical agent calls Hemanth deferred to me.

| # | Decision | Source |
|---|----------|--------|
| 1 | Vision mirror matches Hemanth's intent | Hemanth (batch 1 Q1) |
| 2 | Parser-and-register fires at libtorrent `metadata_received_alert` time | Agent 4 (Hemanth deferred, batch 1 Q2 "which do you recommend?") |
| 3 | Extend `StreamDownloadIndex.Entry` with `state` enum + `progressPct` field; schema bump v1→v2 with migration | Hemanth (batch 1 Q3) |
| 4 | Coordinate with THEATRE_DOWNLOAD_OVERHAUL D1 `EpisodeTile` in parallel; bake state-input contract into D1 while it's still wet clay | Agent 4 (coordination mechanics, Rule 14 + `feedback_coordination_mechanics_not_hemanth.md`) |
| 5 | Source-provenance differentiator: **faint actual warm-amber tint** on Tankorent rows — explicit one-off exception to the standing `feedback_no_color_no_emoji.md` strict gray/black/white contract, granted specifically for download-source provenance | Hemanth (batch 2 Q1 + batch 3 Q4) |
| 5b | **Exact hex/opacity/saturation of the amber tint deferred to live-eyeball ratification during Phase 4 smoke.** The bracket is locked (warm amber/honey/muted gold); the precise tone is not. | Hemanth (this brainstorm, post-Section 5 note) |
| 6 | Files that don't parse cleanly into (S, E) tuples (extras, samples, behind-the-scenes featurettes) are **skipped silently** from episode rows — preserves current `publishTankorentItemsForTorrent` behavior. Files still download to disk; just no row surfaces them. | Hemanth (batch 2 Q2) |
| 7 | **Cancel = evict EVERYTHING from this pack, even completed episodes.** Index only — files stay on disk where libtorrent saved them. | Hemanth (batch 2 Q3 + batch 3 Q1) |
| 7b | **Why evict-everything (Hemanth's rationale, verbatim):** *"until we polish every last piece of code in stream mode have netflix levels of clarity on download procedure, I want my download cancels to be absolute and apply for the entire season. we will change this rule much later down the line when I think I trust our app's UI enough to know the download's happening just as it is showing."* The rule is about earning UI trust first; once Hemanth trusts that Tankoban's UI accurately reflects torrent state, cancel behavior will be revisited to "preserve finished, drop unfinished" semantics. This is a deferred-future change, NOT a v1.x ticket. | Hemanth (this brainstorm, post-Section 6 note) |
| 8 | The Tankorent-source differentiator applies to **MOVIES too**, not just shows. Movie tile + `m_movieDownloadChip` (yesterday's Codex Trigger D #5 surface) gets the same amber tint when source = Tankorent. | Hemanth (batch 2 Q4) |
| 9 | **Sequential download priority:** libtorrent fetches episodes in order (episode 1 first to completion, then episode 2, etc.) so the user can start watching ep1 while ep13 is still downloading. Tradeoff accepted: marginally lower total pack throughput in exchange for early watchability. | Hemanth (batch 3 Q2) |
| 10 | **THEATRE_DOWNLOAD_OVERHAUL Decision 12 AMENDED:** addon-bulk cancel behavior changes from "preserve completed, drop unfinished" to "evict everything (Index only, files stay on disk)" — matching the new Tankorent path. Both paths now behave identically at cancel time until Hemanth re-ratifies under UI-trust conditions per Decision 7b. | Hemanth (batch 3 Q3, realign-in-other-direction pick) |
| 11 | **Phasing strategy: Approach A — Layered phases.** 4 phases (~22-28 tasks total): Substrate → Wire to metadata-ready → UI surfaces → Differentiator + cancel + sequential. Each phase is its own build-verify + smoke cycle, subagent-friendly per `feedback_plan_first_zero_errors.md`. | Hemanth (post-brainstorm phasing question) |
| 12 | **Full-series and multi-season pack-rule refinements** are explicitly deferred to v1.x. Current 1-50 season probe (inherited from `publishTankorentItemsForTorrent`) is acceptable for v1. Hemanth has vague ideas to refine the rules later; those are out of scope for this arc. | Hemanth (this brainstorm, post-Section 3 note) |
| 13 | **Error-case UX refinement deferred** until Theatre mode is shipped + running end-to-end. Agent-call defaults documented below; Hemanth ratification deferred per his directive *"if i ever encounter these errors and want them fixed, I will ask you to do it then when the entire theatre mode is up and running."* | Hemanth (this brainstorm, post-Section 4 note) |

---

## 4. Architecture overview

The Tankorent-pack download path is rewired so the parser-and-register step happens at `metadata_received_alert` time instead of at completion. Episodes register as `Pending` in `StreamDownloadIndex`. As libtorrent emits `pieceFinished` for each file, the episode flips `Pending → Downloading → Complete`. Episode rows in the UI subscribe to `entriesChanged()` (and a new granular `entryStateChanged()` signal) and re-render whatever state is current. Cancel = drop all entries for this pack's `sourceGroupId` from the Index (files stay on disk). Source-provenance differentiator is computed at render time: `Entry.sourceGroupId.startsWith("tankorent:")` → apply warm-amber tint. Sequential download is achieved by setting libtorrent piece priorities so file-1's pieces are highest, file-2's next, etc., walking priorities forward as each file completes.

The four moving pieces:

1. **`StreamPackParser`** — NEW pure-logic shared helper. Lifts the parser body out of `publishTankorentItemsForTorrent` so it can be called at metadata-ready time AND at completion (defensive double-pass).
2. **`StreamDownloadIndex.Entry` schema v1→v2** — adds `state` (Pending | Downloading | Complete | Failed) + `progressPct` (0-100). Persistence migrates v1 entries to `state=Complete, progressPct=100` on load.
3. **`EpisodeTile` state-input contract** — THEATRE_DOWNLOAD_OVERHAUL D1, mid-flight. Gains a `setEpisodeState(state, progressPct, provenance)` interface. Render reads `provenance` and applies amber tint when `Tankorent`. The existing `StreamDetailView` season-row table (`kColAction` column) gets the same state-input contract via a render function. Two surfaces, one contract.
4. **`SequentialPieceManager`** — NEW small TorrentClient-side helper. Maintains per-file libtorrent piece priorities for show packs downloaded sequentially. Opt-in via the existing `sequential` flag on `AddTorrentConfig`. Default = true for Tankorent show packs (matches Decision 9); existing addon-path stays default-off.

The addon-bulk download path (`m_streamBulkGroups` + `publishStreamBulkItemsForTorrent`) is **unchanged in shape**; it picks up the new `Entry.state` on read but its own write path stays identity-preserving. The differentiator is a render-time read of `sourceGroupId`; addon code does not need to know anything new.

---

## 5. Components

### 5.1 `StreamPackParser` (NEW)

**Location:** `src/core/stream/StreamPackParser.{h,cpp}`

**Purpose:** Pure-logic parser, no state, no Qt signals. Takes a torrent's file list + show identity, returns parsed episode/movie tuples. Lifts parser body out of `publishTankorentItemsForTorrent` (TorrentClient.cpp:1832-1882).

```cpp
namespace tankostream::stream {

struct ParsedFile {
    int     season;
    int     episode;
    int     fileIndex;      // index in libtorrent's file list
    QString relName;        // e.g. "Daredevil.S01E03.1080p.WEB-DL.mkv"
    qint64  sizeBytes;
};

struct ParsedPack {
    QString             imdbId;
    QString             type;          // "series" or "movie"
    QList<ParsedFile>   episodes;      // empty for movies
    ParsedFile          movieFile;     // valid only when type=="movie"
};

class StreamPackParser {
public:
    // Returns a ParsedPack with parsed episodes. configSeason == 0 triggers
    // multi-season probe (seasons 1..kMaxSeasonProbe). Returns empty episodes
    // + valid movieFile when no episode parsed but a clear "largest video"
    // candidate exists.
    static ParsedPack parsePack(
        const QJsonArray& files,
        const QString& imdbId,
        int configSeason
    );

    static constexpr int kMaxSeasonProbe = 50;
};

}  // namespace
```

**Why:** Today's `publishTankorentItemsForTorrent` does TWO jobs entangled (parse + register). Splitting them lets us call the parser at metadata-ready time (registers `Pending`) AND at completion (defensive double-pass to catch late-parse files). The `BulkPackVerifier::matchEpisodeFileForSeason` regex logic stays in `BulkPackVerifier`; the parser calls it.

**Unit tests:** TDD-clean. Pure inputs (JSON file list + imdbId + configSeason), pure outputs (ParsedPack). Test corpus includes:
- Single-season pack with clean S##E## naming
- Single-season pack with mixed naming (1x02 syntax, "Episode 03" syntax)
- Multi-season pack across S01-S06
- Multi-season pack with non-contiguous seasons (S02, S04, S06)
- Movie pack (single large .mkv + multiple .nfo/.srt extras)
- Edge: pack with all-unparseable filenames → empty episodes + movie fallback
- Edge: pack with NO video files → empty ParsedPack

### 5.2 `StreamDownloadIndex.Entry` schema v1→v2

**Location:** existing `src/core/stream/StreamDownloadIndex.{h,cpp}`

**Schema change:**

```cpp
struct Entry {
    QString imdbId;
    QString type;
    int     season = 0;
    int     episode = 0;
    QString canonicalPath;
    qint64  addedAt = 0;
    QString sourceGroupId;
    qint64  fileSizeBytes = 0;
    // NEW v2 fields
    enum State { Complete = 0, Pending = 1, Downloading = 2, Failed = 3 };
    State   state = Complete;
    int     progressPct = 100;
};
```

`State::Complete` is the default value so v1 entries on JSON load migrate cleanly (any entry lacking a `state` key gets `Complete`). New API methods:

```cpp
// Register an episode as Pending or Downloading.
// canonicalPath is the EXPECTED path (libtorrent's savePath + relName);
// the file may not exist on disk yet at Pending state.
void registerPendingEpisode(const QString& imdbId, int season, int episode,
                            const QString& canonicalPath,
                            const QString& sourceGroupId,
                            qint64 fileSizeBytes);

// Movie variant — parallels the existing registerMovie() but with state=Pending.
void registerPendingMovie(const QString& imdbId,
                          const QString& canonicalPath,
                          const QString& sourceGroupId,
                          qint64 fileSizeBytes);

// Update progress on an existing Pending/Downloading entry.
// Auto-flips state on threshold:
//   first call with progressPct > 0 → state = Downloading
//   progressPct == 100              → state = Complete
// Works for both episodes (season > 0) and movies (season == 0).
void updateEpisodeProgress(const QString& imdbId, int season, int episode,
                           int progressPct);

// Drop all entries for a given sourceGroupId (cancel semantics).
// Files on disk are NOT touched (per Decision 7).
void evictBySourceGroup(const QString& sourceGroupId);
```

The existing `registerEpisode()` (which registers as `Complete`) stays — kept for the defensive double-pass at completion time, and for `StreamRescueScanner` migration paths that surface already-on-disk files.

**New signal:** `entryStateChanged(QString imdbId, int season, int episode)` fires off-lock when a single entry's state changes. The existing `entriesChanged()` fires on bulk mutations (load, evictByImdb, evictBySourceGroup, validateAll). Granular subscribers (EpisodeTile, season-row table) wire to `entryStateChanged`; bulk consumers (library home grid) stay on `entriesChanged`.

**Persistence:** `stream_downloads.json` schema bumps `kSchemaVersion = 2`. Migration on load:
- v1 → v2: missing `state` field defaults to `Complete`, missing `progressPct` defaults to 100. No data loss.
- v2 → v2: read as-is. Pending/Downloading entries that survive a Tankoban restart get re-validated against libtorrent state on next session resume (see § 7 error handling).

### 5.3 `EpisodeTile` state-input contract (THEATRE_DOWNLOAD_OVERHAUL D1, mid-flight)

**Location:** `src/ui/pages/stream/EpisodeTile.{h,cpp}` (D1 author = Agent 4 = me)

The tile gains a state struct + setter on its public interface:

```cpp
struct EpisodeTileState {
    StreamDownloadIndex::Entry::State state;
    int     progressPct;            // 0-100; ignored if state != Downloading
    enum Provenance {
        AddonBulk,                  // sourced from Stremio addon download
        Tankorent,                  // sourced from Tankorent pack
        LocalScan                   // pre-existing file discovered by StreamRescueScanner
    };
    Provenance provenance;
};

void setEpisodeState(const EpisodeTileState& s);
```

Visual state-to-paint mapping:

| State | Chip text | Progress bar | ✓ glyph | Notes |
|-------|-----------|--------------|---------|-------|
| `Pending` | "Queued" | hidden | no | All rows in fresh pack at T+3s after metadata-ready |
| `Downloading` | "X%" (e.g. "47%") | 2px horizontal strip along bottom edge | no | Active download |
| `Complete` | "Downloaded" | hidden | yes (existing kColAction render) | Watchable from local |
| `Failed` | "Failed" | hidden | no | Muted-red chip color (separate from amber-provenance discipline) |

Provenance-to-tint mapping:

| Provenance | Row background tint |
|-----------|---------------------|
| `AddonBulk` | default (gray palette, no tint) |
| `Tankorent` | **faint warm amber** — exact tone TBD per Decision 5b live-eyeball review |
| `LocalScan` | default (gray palette, no tint) |

**The existing season-row table** in `StreamDetailView` (the `QTableWidget` with `kColAction` column) gets the SAME state-input contract via a render function — call sites that today build `kColAction` chips call into a shared `renderEpisodeStateChip(Entry::State, progressPct, provenance, cellWidget)` helper. Two surfaces (EpisodeTile + season-row table), one render contract.

### 5.4 `SequentialPieceManager` (NEW)

**Location:** `src/core/torrent/SequentialPieceManager.{h,cpp}`

**Purpose:** maintains per-file libtorrent piece priorities so episodes download in episode order.

```cpp
class SequentialPieceManager {
public:
    // Register a pack on Download. fileIndicesInEpisodeOrder is the libtorrent
    // file indices for episode 1, 2, 3, ... in episode order.
    void registerPack(const QString& infoHash,
                      const QList<int>& fileIndicesInEpisodeOrder);

    // Called from TorrentClient when pieceFinished fires for any piece.
    // Walks priorities forward when the current top-priority file completes.
    void onFileProgress(const QString& infoHash, int fileIndex, double pct);

    // Drop a pack from tracking on cancel/completion.
    void forgetPack(const QString& infoHash);
};
```

**Internals:**
- On `registerPack`: set file priorities so file-at-episode-1 is `priority_7` (highest), file-at-episode-2 is `priority_5`, file-at-episode-3 onwards are `priority_1` (lowest non-zero).
- On `onFileProgress`: when `pct >= 100` for the current top-priority file, promote the next file to `priority_7`, the one after to `priority_5`, etc.
- libtorrent's per-file priorities: 0=skip, 1-4=low, 5-6=normal, 7=highest.

**Activation:** Only registered for show packs with `configSeason > 0` OR multi-season packs. Movies are single-file, no manager needed. Opt-in per `AddTorrentConfig.sequential` flag (already exists per [obs 3775](#references)) — default true for Tankorent show packs (Decision 9), default false for addon-bulk packs (preserves existing behavior).

---

## 6. Data flow (timeline)

### 6.1 Happy path — Daredevil S01 1080p Complete Season pack

```
TIME       USER-VISIBLE STATE                        UNDER THE HOOD
─────────  ────────────────────────────────────────  ────────────────────────────────────
T+0s       User clicks Download on the pack          TorrentClient::addTorrent(...) called
                                                     AddTorrentConfig.sequential = true
                                                     AddTorrentConfig.imdbId = "tt18923754"
                                                     AddTorrentConfig.season = 1
                                                     m_records[infoHash] populated

T+0-3s     Download panel slides away                libtorrent fetches metadata via DHT/peers
           Episode rows show default state           No state in Index yet

T+3s       metadata_received_alert fires             TorrentEngine emits metadataReady(infoHash)
           ↓                                         TorrentClient::onMetadataReady() runs:
           All 13 episode rows light up               1. m_engine->torrentFiles(infoHash)
           with "Queued" chips                        2. StreamPackParser::parsePack(files, imdbId, 1)
           Faint warm amber tint on each row          3. For each ParsedFile:
                                                        m_streamDownloadIndex->registerPendingEpisode(
                                                          imdbId, 1, episode,
                                                          QDir(savePath).absoluteFilePath(relName),
                                                          "tankorent:"+infoHash,
                                                          sizeBytes)
                                                      4. SequentialPieceManager::registerPack(
                                                          infoHash,
                                                          fileIndicesInEpisodeOrder)
                                                      5. emit entriesChanged()
                                                     UI subscribers re-render kColAction +
                                                     EpisodeTile state

T+5s       Episode 1 chip → "Downloading 4%"          libtorrent pieceFinished for file-1 piece
           Progress bar appears under ep1 row         TorrentClient computes file-1 pct = 4%
                                                     m_streamDownloadIndex->updateEpisodeProgress(
                                                       imdbId, 1, 1, 4)
                                                     → state auto-flips Pending → Downloading
                                                     → entryStateChanged(imdbId, 1, 1) emitted
                                                     UI updates only the affected row

T+1m       Episode 1: "Downloading 47%"               Steady piece progression
           Episodes 2-13: "Queued" still              Sequential priorities keep ep2-13 throttled

T+8m       Episode 1: "Downloaded" ✓                  file-1 reaches 100%
           Episode 2 chip → "Downloading 6%"          updateEpisodeProgress(imdbId, 1, 1, 100)
           Sequential manager rotates priorities      → state auto-flips Downloading → Complete
                                                     SequentialPieceManager::onFileProgress sees
                                                     pct >= 100 for file-1 → promotes file-2 to
                                                     priority_7

T+8m+1s    Hemanth clicks ep1 row → plays from        kColAction renders ✓ + click-to-play
           local file via Stream gateway              StreamDownloadIndex::filePathFor(imdbId, 1, 1)
                                                     returns canonical path → VideoPlayer opens it

...continues sequentially episode-by-episode...

T+90m      All 13 episodes: "Downloaded" ✓            All 13 entries state=Complete, progressPct=100
           Faint amber tint persists forever          publishTankorentItemsForTorrent runs at
                                                     torrentCompleted (defensive double-pass) —
                                                     all entries already Complete, no-ops
                                                     SequentialPieceManager::forgetPack
```

### 6.2 Cancel mid-flight

```
T+15m+0s   User clicks Cancel on the pack            TorrentClient::cancelTorrent(infoHash)
                                                     libtorrent removes the torrent

T+15m+0s   ALL 13 episode rows → default state       m_streamDownloadIndex->evictBySourceGroup(
           (no chip, no amber tint,                    "tankorent:"+infoHash)
           kColAction back to "Download")             → emits entriesChanged()
                                                     SequentialPieceManager::forgetPack
                                                     Files on disk: untouched
                                                       (ep1.mkv, ep2.mkv saved per libtorrent
                                                        save_path; ep3.mkv partial; ep4-13 absent)
```

### 6.3 Movie pack

```
T+0s       User clicks Download on movie pack        AddTorrentConfig.imdbId = "tt0137523"
                                                     AddTorrentConfig.season = 0
                                                     AddTorrentConfig.sequential = false (single
                                                       file, no manager)

T+0-3s     Detail view stays                         libtorrent fetches metadata

T+3s       Movie tile in library:                    StreamPackParser::parsePack returns
           - "Downloading 0%" chip                    type="movie", movieFile populated
             (Codex Trigger D #5 surface)            m_streamDownloadIndex->registerPendingEpisode
           - Faint warm amber tint                    is NOT called; instead a new
                                                     registerPendingMovie() variant fires
                                                     emit entriesChanged()

...progresses linearly...

T+1h       Movie tile: "Downloaded" ✓ + amber tint   updateEpisodeProgress drives Pending →
           Hemanth clicks → plays from local         Downloading → Complete same way as episodes
```

### 6.4 Failed parse (some files unrecognized)

```
T+3s       11 of 13 rows light up Pending            ParsedPack.episodes has 11 entries; 2 files
           Rows 12 + 13 stay default                 didn't match BulkPackVerifier regex
           (no chip, no tint)                        Per Decision 6: skip silently

T+90m      11 rows Downloaded ✓ + amber tint         Files for rows 12+13 DOWNLOAD silently to
           Rows 12 + 13 STILL default                disk via libtorrent's normal flow but never
                                                     surface in any episode row.
                                                     Defensive double-pass at completion:
                                                     publishTankorentItemsForTorrent runs again;
                                                     if filename now matches (e.g. user renamed
                                                     mid-download — unlikely), late entry
                                                     registers as Complete.
```

---

## 7. Error handling (agent-call defaults)

Per Decision 13, error-case UX refinement is deferred until Theatre mode is shipped + running. The defaults below are what ships in v1; Hemanth ratification of UX polish (toast text, retry buttons, etc.) happens later when he encounters them in real use.

### 7.1 Dead torrent — metadata never arrives

**Default:** libtorrent gives up after ~60s with no metadata. TorrentEngine emits an error. TorrentClient drops the m_records entry, the torrent is silently removed from the session. No episode rows were ever registered (we register at metadata-ready, which never fired) → no rows to evict.

**Future UX polish (deferred):** small toast at bottom of detail view: "Metadata fetch failed — no peers."

### 7.2 App close mid-download

**On close:**
- `stream_downloads.json` already persists entries on every state change (via existing `save()` after every mutation)
- ep1, ep2 entries: `state=Complete, progressPct=100` (persisted)
- ep3 entry: `state=Downloading, progressPct=47` (persisted, will be stale on relaunch)
- ep4-13 entries: `state=Pending, progressPct=0` (persisted)

**On relaunch:**
- `StreamDownloadIndex::load()` migrates entries (v1→v2 no-op since they're already v2; v2→v2 reads as-is)
- libtorrent resumes the torrent via existing resume data
- A new **launch-validation pass** runs in the first 5 seconds: for each `Pending`/`Downloading` entry, check whether libtorrent has the torrent (via `m_engine->isActive(infoHash)` lookup against `sourceGroupId.mid(strlen("tankorent:"))`). If yes, leave entry alone — libtorrent will update progress via `pieceFinished`. If no (resume data lost), evict the entry.
- Stale `progressPct` from JSON is overwritten within ~5s of relaunch by real libtorrent progress signals.

### 7.3 Re-dispatch same pack after cancel

**Default:** Treat as fresh. User clicks Download again → TorrentClient adds torrent fresh → libtorrent finds on-disk resume data (or doesn't, depending on whether files were preserved). Note libtorrent's `metadata_received_alert` is single-shot per session lifetime per handle ([gotcha M1 from obs 3884](#references)) — but a fresh add after cancel means a fresh handle, so metadata_received fires normally.

**If user re-dispatches WITHIN the same Tankoban session (~10s after cancel):** libtorrent may return `duplicate_torrent` if internal handle cleanup hasn't completed. TorrentClient already handles this via `existingHandle` reuse — but `metadata_received_alert` won't re-fire on the reused handle. Workaround: TorrentClient.cpp:onAddTorrent path checks if metadata was previously received for this infoHash and synthesizes a `metadataReady` emit for the new handle. **This is a known fixup needed; tracked in Phase 1 task list.**

### 7.4 Files moved/deleted between launches

The existing `validateAll()` path (StreamDownloadIndex 2026-05-10 work) walks every entry and confirms `QFile::exists(canonicalPath)`. Missing files = evicted from index. With v2 schema this extends naturally to Pending/Downloading: if libtorrent has no record AND the file doesn't exist, evict. The launch-validation pass (§ 7.2) integrates here.

### 7.5 Parser misreads — wrong season number

The parser delegates to `BulkPackVerifier::matchEpisodeFileForSeason`. Misreads (e.g. parsing "S03E15" when the file is actually S04E01) are a parser-correctness issue **upstream of this arc**. This arc inherits whatever accuracy `BulkPackVerifier` provides today. If parser misreads become a real problem, that's a follow-up arc against `BulkPackVerifier`, not this one.

### 7.6 SequentialPieceManager edge cases

- **Pack contains episodes in non-monotonic file order** (e.g. file index 7 = episode 1, file index 3 = episode 2): manager uses the **episode-order list** passed at `registerPack()`, not file-index order. Parser already returns episodes in episode order.
- **Pack has gaps** (e.g. only S01E01, S01E03, S01E05): manager treats whatever's parsed as the order. Episode 2 + 4 are missing from the pack, so no priorities are set for them. Episode 1 → ep3 → ep5 in order.
- **Single-file pack** (movie): manager is NOT registered; libtorrent's default priorities apply.

---

## 8. Smoke matrix

Per Decision 11, the arc ships when these three smokes are GREEN end-to-end. Anything beyond these is v1.x.

### 8.1 Smoke 1 — Single-season show pack

**Goal:** Confirm the end-to-end happy path on a real show.

**Steps:**
1. Open Tankoban. Navigate to Stream mode. Search for "Daredevil".
2. Open Daredevil detail view. Wait for sources to load.
3. Open download panel. Filter to Tankorent. Pick "Daredevil S02 1080p Complete Season" (or whichever clean 1080p complete season pack the picker surfaces).
4. Click Download.

**Expected:**
- Within ~5 seconds: all 13 episode rows display "Queued" chips with faint warm-amber tint.
- Within ~30 seconds: episode 1 row shows "Downloading X%" with progress bar; episodes 2-13 stay "Queued".
- Approximately when episode 1 file size / your download speed = episode 1 download time: episode 1 row shows "Downloaded ✓"; episode 2 row begins "Downloading X%".
- Click episode 1 row: plays from local file via Stream gateway with full Cinemeta metadata.
- Continue letting it run: episodes complete sequentially.

### 8.2 Smoke 2 — Movie pack

**Goal:** Confirm differentiator applies to movies + movie progress flows.

**Steps:**
1. Search for "Fight Club" in Stream mode.
2. Open detail view. Open download panel. Filter to Tankorent. Pick "Fight Club 1080p BluRay" (or equivalent clean pack).
3. Click Download.

**Expected:**
- Within ~5 seconds: movie tile in library shows "Downloading 0%" chip (the Codex Trigger D #5 surface) + faint warm-amber tint.
- Progress chip updates in real time.
- On completion: movie tile shows "Downloaded ✓" + amber tint persists.
- Click tile: plays from local file.

### 8.3 Smoke 3 — Cancel + re-dispatch

**Goal:** Confirm cancel evicts all + re-dispatch is clean.

**Steps:**
1. Start Smoke 1's Daredevil S02 pack. Wait until at least one row says "Downloading X%".
2. Click Cancel on the pack.
3. **Verify:** all 13 rows go default — no chip, no progress bar, no amber tint anywhere on the season view. The kColAction column resets to "Download" affordance on every row.
4. Click Download on the same pack again.
5. **Verify:** rows light up "Queued" with amber tint again within ~5 seconds. libtorrent finds on-disk files (if any were complete pre-cancel, they show "Downloaded ✓" immediately; rest show "Queued"). Sequential download resumes from the first incomplete episode.

### 8.4 Continuous-confidence (Thread A) carry-through

Per Agent 8's kickoff Thread A, **every phase's smoke must also re-verify yesterday's Codex Trigger D #4 fixes still hold:**
- `StreamPage.cpp:2698` movie library-add still fires on dispatch
- `StreamDetailView.cpp:1617` grace-stamp move + `:1328` 30s widen still work
- `TorrentClient.cpp:759` `streamBulkGroupsChanged` emit still fires

Concretely: Smokes 1+2+3 above implicitly exercise the bulk-progress refresh path (`refreshEpisodeBulkProgress` + 30s grace + signal-driven UI refresh). If those break, this arc regressed Codex #4. Re-verify by checking that the bulk-progress chip on movies/seasons in the library tile + detail view stays accurate across the smoke.

---

## 9. Cross-arc impact

### 9.1 THEATRE_DOWNLOAD_OVERHAUL Decision 12 amendment

Per Decision 10, the previously-locked THEATRE_DOWNLOAD_OVERHAUL Decision 12 changes from:

> *"Cancel preserves already-finished episode downloads, drops unfinished."* (locked 2026-05-16)

…to:

> *"Cancel evicts the entire pack from the library (Index only, files stay on disk) for both addon-bulk and Tankorent paths."* (amended 2026-05-18, this arc)

**Rationale (Hemanth verbatim, Decision 7b):** *"until we polish every last piece of code in stream mode have netflix levels of clarity on download procedure, I want my download cancels to be absolute and apply for the entire season. we will change this rule much later down the line when I think I trust our app's UI enough to know the download's happening just as it is showing."*

**This is a deferred-future-change, NOT a v1.x ticket.** Once Hemanth trusts that Tankoban's UI accurately reflects torrent state, cancel behavior may be revisited to preserve-completed semantics.

**Mechanics of the amendment:**
- Append an amendment line to `agents/chat.md` at arc ship time: `## Agent 4 - THEATRE_DOWNLOAD_OVERHAUL Decision 12 AMENDED 2026-05-18 — cancel evicts entire pack (Index only, files stay on disk) for both addon and Tankorent paths. Supersedes original Decision 12 of 2026-05-16. Rationale: earn UI trust before allowing partial-state preservation.`
- Update [docs/superpowers/specs/2026-05-16-theatre-download-overhaul-brainstorm.md](2026-05-16-theatre-download-overhaul-brainstorm.md) with an inline amendment block at Decision 12 referencing this arc's spec.
- The addon-bulk path's cancel implementation may need code-level alignment if `cancelStreamBulkGroup` today preserves finished items. Verify during Phase 4 implementation; small touchup if needed.

### 9.2 THEATRE_DOWNLOAD_OVERHAUL Phase D1 `EpisodeTile` (mid-flight, same owner)

Per Decision 4, the `EpisodeTile` widget being authored in THEATRE_DOWNLOAD_OVERHAUL Phase D1 absorbs the `EpisodeTileState` interface (§ 5.3) natively. Since Agent 4 owns both arcs, no HELP request or external coordination needed — the D1 implementation pulls the state-input contract from § 5.3 of this spec. The contract is also applied to the existing `StreamDetailView` season-row table render path (`kColAction`) so the same state model surfaces on both UI surfaces.

### 9.3 No other cross-arc ripple

- The addon-bulk download path (`m_streamBulkGroups` + `publishStreamBulkItemsForTorrent`) is **unchanged in shape**.
- The Cinemeta resolver path is untouched (imdbId comes from the picker, same as today).
- The StreamLibrary persistence is untouched.
- The Stream-vs-Videos hiding logic (StreamDownloadIndex::isStreamOwned) is unchanged — it cares about canonical path, not Entry state. Pending/Downloading entries with a canonical path will hide their (not-yet-existent) files from Videos mode; this is fine because the files don't exist yet, so VideosScanner wouldn't surface them anyway.

---

## 10. Out of scope / v1.x deferrals

Explicitly NOT in this arc:

- **Full-series + multi-season pack-rule refinements** (Decision 12) — Hemanth has vague ideas; deferred until after v1 ships.
- **Error-case UX polish** (Decision 13) — toast text, retry buttons, "metadata fetch failed" surfaces, parser-misread alerting. Deferred until Theatre mode is shipped + running.
- **Cancel semantics reversion to "preserve finished"** (Decision 7b) — deferred until UI trust is earned.
- **Exact amber hex/opacity** (Decision 5b) — live-eyeball ratification during Phase 4 smoke; documented as TBD in code with sensible default.
- **`BulkPackVerifier` parser accuracy improvements** — upstream of this arc; separate follow-up.
- **`tankoctl` extensions for inspecting per-episode state** (e.g. `get-stream-pack-state <imdb>`) — useful for smoke verification but not blocking; if needed during smoke, ship as a single-task Codex Trigger D follow-up.
- **Library tile DOWNLOADING badge gap (F2 from last wake's recap)** — movies' library tile badge gap is separate; addressed independently or rolled in opportunistically during Phase 3 movie surface work.
- **F1 crash investigation from last wake** — pre-existing race in search-tile-click path, NOT a regression from this arc.

---

## 11. Phasing

Per Decision 11 (Approach A — Layered phases), the implementation plan that follows this spec uses 4 phases. Per-phase scope below is indicative; precise task breakdown is the job of the `/superpowers:writing-plans` skill output.

### Phase 1 — Substrate (~6 tasks)

1. Author `StreamPackParser.{h,cpp}` lifting parser body from `publishTankorentItemsForTorrent`.
2. Unit tests for `StreamPackParser::parsePack` (6+ test cases per § 5.1).
3. Extend `StreamDownloadIndex.Entry` with `state` enum + `progressPct` field. Schema bump v1→v2 with migration.
4. Implement `registerPendingEpisode`, `updateEpisodeProgress`, `evictBySourceGroup` methods + `entryStateChanged` signal.
5. Unit tests for state transitions (Pending → Downloading via updateEpisodeProgress; Downloading → Complete at 100%; evictBySourceGroup drops correct entries).
6. Refactor existing `publishTankorentItemsForTorrent` to use `StreamPackParser` internally (no behavioral change yet; still fires at completion).

**Phase 1 gate:** all tests GREEN. `build_check.bat` BUILD OK. No UI changes; no behavioral change for existing flows. Behind-the-scenes substrate only.

### Phase 2 — Wire to metadata-ready (~5 tasks)

7. Hook `StreamPackParser` into `TheatreDownloadPanel::onPackRowSelected` (or wherever `metadataReady` lands first for the new flow) to register Pending entries at metadata time. Gated by `AddTorrentConfig.sequential` + presence of `imdbId` (matches Tankorent-source detection).
8. Wire libtorrent `pieceFinished` signal in TorrentClient to compute per-file progress and call `updateEpisodeProgress`.
9. Add launch-validation pass (§ 7.2) for Pending/Downloading entries: walk on startup, drop entries libtorrent doesn't know about.
10. Handle the duplicate-torrent re-dispatch case (§ 7.3): synthesize a metadataReady emit for reused handles within-session.
11. Smoke: dispatch a real Tankorent show pack, observe rows light up Pending at metadata-ready; observe state transitions on disk via stream_downloads.json file tail.

**Phase 2 gate:** Phase 1 + 2 integrated. Index state model is fully driven by libtorrent events. No UI rendering yet (state is in JSON but episode rows don't surface it). `build_check.bat` BUILD OK.

### Phase 3 — UI surfaces (~7 tasks)

12. Extend `EpisodeTile` (D1, mid-flight) with `setEpisodeState(EpisodeTileState)` interface.
13. Implement state-to-paint mapping in EpisodeTile (Pending/Downloading/Complete/Failed → chip text + progress bar + ✓).
14. Extract shared `renderEpisodeStateChip(...)` helper and call it from EpisodeTile + StreamDetailView's existing season-row table `kColAction` render path.
15. Wire `entryStateChanged` signal subscribers on EpisodeTile + season-row table.
16. Extend movie tile + `m_movieDownloadChip` (Codex Trigger D #5 surface) for movie state rendering (Pending → Downloading → Complete).
17. Smoke: dispatch real packs (show + movie); observe rows light up and progress in real time.
18. Coordinate with the rest of THEATRE_DOWNLOAD_OVERHAUL D1 — ensure no regression on the in-flight D1 work.

**Phase 3 gate:** all three smoke scenarios (§ 8) pass except the amber tint (still default-color in this phase). `build_check.bat` BUILD OK.

### Phase 4 — Differentiator + cancel + sequential (~6 tasks)

19. Implement `SequentialPieceManager.{h,cpp}` + wire into TorrentClient on `addTorrent` and `pieceFinished` paths.
20. Implement amber-tint render in EpisodeTile + season-row table + movie tile when `sourceGroupId.startsWith("tankorent:")`. Initial hex/opacity default — Hemanth live-eyeball ratification in this phase's smoke.
21. Implement cancel evict-everything semantics: wire `cancelStreamBulkGroup` (and Tankorent equivalent) to call `evictBySourceGroup`.
22. Append THEATRE_DOWNLOAD_OVERHAUL Decision 12 amendment to `chat.md` per § 9.1.
23. Update `docs/superpowers/specs/2026-05-16-theatre-download-overhaul-brainstorm.md` Decision 12 inline amendment.
24. Smoke: full Smoke 1 + 2 + 3 (§ 8) GREEN. Hemanth live-eyeballs amber tint and ratifies (or requests adjustment). Continuous-confidence carry-through verified.

**Phase 4 gate:** arc shipped. RTC lines posted to `chat.md`. `/commit-sweep` by Agent 0.

---

## 12. References

**Sibling specs / plans (recent and adjacent):**
- [docs/superpowers/specs/2026-05-16-theatre-download-overhaul-brainstorm.md](2026-05-16-theatre-download-overhaul-brainstorm.md) — THEATRE_DOWNLOAD_OVERHAUL master spec; this arc amends its Decision 12.
- [docs/superpowers/plans/2026-05-16-theatre-download-overhaul.md](../plans/2026-05-16-theatre-download-overhaul.md) — 22-task plan; Phases A-C shipped 2026-05-16, D in-flight.
- [docs/superpowers/specs/2026-05-15-tankorent-stream-integration-design.md](2026-05-15-tankorent-stream-integration-design.md) — the arc that built `publishTankorentItemsForTorrent` and the show-first picker AddTorrentConfig.imdbId/season fields.
- [docs/superpowers/plans/2026-05-15-tankorent-stream-integration.md](../plans/2026-05-15-tankorent-stream-integration.md) — TANKORENT_STREAM_INTEGRATION plan.
- [docs/superpowers/specs/2026-05-10-stream-downloaded-library-design.md](2026-05-10-stream-downloaded-library-design.md) — original `StreamDownloadIndex` spec; this arc's schema v1→v2 bump amends.

**Key source files:**
- [src/core/torrent/TorrentClient.cpp](../../src/core/torrent/TorrentClient.cpp) — `publishTankorentItemsForTorrent` (lines 1788-1919), `streamBulkGroupsChanged` emit (line 759), `onTorrentCompleted` dispatch (line 2784).
- [src/core/torrent/TorrentClient.h](../../src/core/torrent/TorrentClient.h) — TorrentClient API; `streamBulkGroupsChanged` declared at line 266.
- [src/core/stream/StreamDownloadIndex.h](../../src/core/stream/StreamDownloadIndex.h) — Entry struct + thread-safety contract.
- [src/core/stream/StreamDownloadIndex.cpp](../../src/core/stream/StreamDownloadIndex.cpp) — registerEpisode + load/save + validateAll.
- [src/core/stream/BulkPackVerifier.h](../../src/core/stream/BulkPackVerifier.h) — `matchEpisodeFileForSeason` static helper.
- [src/ui/dialogs/AddTorrentDialog.h](../../src/ui/dialogs/AddTorrentDialog.h) — `AddTorrentConfig` struct including `imdbId`, `season`, `sequential` fields.
- [src/ui/pages/stream/EpisodeTile.h](../../src/ui/pages/stream/EpisodeTile.h) — THEATRE_DOWNLOAD_OVERHAUL D1, mid-flight; receives state-input contract per § 5.3.
- [src/ui/pages/stream/TheatreDownloadPanel.cpp](../../src/ui/pages/stream/TheatreDownloadPanel.cpp) — `onPackRowSelected` already calls `resolveMetadata`; the metadata-ready hook for this arc.
- [src/ui/pages/stream/StreamDetailView.cpp](../../src/ui/pages/stream/StreamDetailView.cpp) — existing season-row table + `kColAction` render path + `refreshEpisodeBulkProgress` (line 1305) + Codex Trigger D #4 fixes at 1328/1617.

**Memory references:**
- `feedback_no_color_no_emoji.md` — standing gray/black/white contract; Decision 5 is an explicit one-off exception.
- `feedback_decision_authority.md` — Rule 14 separating agent technical calls from Hemanth product/strategic calls.
- `feedback_coordination_mechanics_not_hemanth.md` — D1 coordination is mechanics, agent call.
- `feedback_plan_first_zero_errors.md` — Hemanth-locked 2026-05-18 directive: non-trivial tasks use `/superpowers:writing-plans` FIRST.
- `feedback_brainstorm_skill_for_big_arcs.md` — invoke brainstorming skill for multi-week features.
- `feedback_brainstorm_batches_of_four.md` — pacing pattern (originally Agent 1 scope, Agent 8 generalized for this arc).
- `feedback_one_fix_per_rebuild.md` — each phase task = one change, one rebuild, one verification.
- `feedback_commit_protocol.md` — Rule 11: RTCs in chat.md, Agent 0 batches.
- `feedback_simple_language.md` — Hemanth-facing communication discipline. (See § 13.)
- `project_tankorent_stream_integration_closed.md` — substrate this arc builds on.

**Key claude-mem observations cited during brainstorm:**
- Obs 3172: StreamDownloadIndex full API surface (pre-modification)
- Obs 2578: StreamDownloadIndex persistence schema baseline
- Obs 3441: TorrentPackPicker modal picker original
- Obs 4309: TorrentPackPicker Phases D1-D4 final
- Obs 3832: THEATRE_DOWNLOAD_OVERHAUL Phases A-C shipped
- Obs 3632: THEATRE_DOWNLOAD_OVERHAUL 20 strategic decisions locked
- Obs 3884: TheatreDownloadPanel D3 review — metadata_received_alert single-shot gotcha M1
- Obs 4313: Codex Trigger D #4 — grace window widening + signal-driven refresh
- Obs 4406: Cinemeta-Mapped Tankorent arc codebase topology confirmation

---

## 13. Drafting note (lessons for future-me)

During the brainstorm, Sections 2 and 4 of the design presentation leaned too heavily into implementation detail (struct shapes, signal wiring, error-mode enumeration) and Hemanth flagged this twice:

- Section 2: *"you know i didn't get any of that but i trust you"*
- Section 4: *"once again, greek and latin brother, greek and latin. if i ever encounter these errors and want them fixed, I will ask you to do it then when the entire theatre mode is up and running"*

The correction: **Hemanth-facing brainstorm sections present user-visible shape only.** Implementation detail goes silent into the spec doc (this doc). When in doubt, ask "would Hemanth click this, see this, or feel this?" — if no, the detail is implementation, not product, and doesn't belong in the spoken-aloud section.

This is consistent with the standing `feedback_simple_language.md` ("Lead with answer. Short sentences. Translate jargon. Hemanth-facing only.") but the brainstorm-skill structure with its "Components" / "Data flow" / "Error handling" headers actively tempts implementation-shape framing. Future-me should rewrite section headers to user-shape: "What you see when you click Download" / "What happens when you cancel" / "How we'll know it works." The information density on Hemanth's side should be 100% experiential, with implementation detail folded silently into the doc.

This will be saved as memory `feedback_design_sections_user_facing.md` (Agent 4 drafting pattern).
