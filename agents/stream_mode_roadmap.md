# Stream Mode Roadmap -- Agent 4

**Date:** 2026-04-02
**Owner:** Agent 4 (Stream & Sources)
**Status:** PROPOSAL -- awaiting Hemanth's approval

---

## Why the Groundwork Failed

The Python groundwork's stream mode had 9 diagnosed architectural problems (documented in `stream_mode_rebuild_plan.md`). The root cause:

**It was episode-centric when it needed to be session-centric.**

- Library, choices, progress, and torrent state were scattered across 5+ separate stores
- Two competing backends (WebTorrent sidecar vs Sources torrent client) fought for control
- Player was launched with empty show_id, breaking track persistence
- No first-class "streaming session" object existed -- the mode tried to reconstruct state from fragments at runtime
- Playlist was faked by patching one-episode-at-a-time resolution
- Auto-next depended on the next episode already having a saved choice

**In simple terms:** it could sometimes start an episode, but it didn't understand what a streaming show was.

---

## Design Principles for Tankoban 2

1. **One engine, total isolation.** Stream mode uses the SAME libtorrent TorrentEngine we already have, but with a completely separate TorrentClient instance. Different port range, different cache directory, different resume data, different JSON records. Tankorent's transfer list never sees stream torrents. Period.

2. **Session-centric, not episode-centric.** A `StreamSession` is the first-class object. It owns: the magnet, the torrent handle, the file-to-episode mapping, the playlist, the buffer state. One session per show/movie viewing.

3. **Sequential download + buffer gate.** Torrent downloads sequentially. VideoPlayer doesn't open the file until a minimum buffer threshold is met (configurable, default 5MB or 2% of file, whichever is larger). The sidecar (ffmpeg) handles partial files natively -- it reads what's available and waits/retries on EOF.

4. **Reuse VideoPlayer as-is.** Stream mode feeds a file path to `VideoPlayer::openFile()` exactly like Videos mode. No HTTP server needed -- libtorrent writes to disk, we point the player at the file. The player already handles the sidecar's ffmpeg which can read growing files.

5. **Metadata via Cinemeta + Torrentio.** Search catalog from Torrentio, series metadata from Cinemeta, stream sources from Torrentio. All via HTTPS JSON APIs (QNetworkAccessManager).

6. **Completely separate from Tankorent/Tankoyomi.** Stream mode is NOT a third tile in SourcesPage. It gets the existing Ctrl+4 placeholder page in MainWindow -- its own top-level page, fully independent.

---

## Architecture Overview

```
MainWindow
  |-- StreamPage (Ctrl+4, replaces placeholder)
  |     |-- StreamSearchWidget     (Cinemeta/Torrentio catalog search)
  |     |-- StreamLibraryGrid      (saved shows/movies as tiles)
  |     |-- StreamContinueStrip    (recently watched, unfinished)
  |     |-- StreamDetailView       (seasons/episodes browser)
  |     |-- StreamSourcePicker     (torrent source selection dialog)
  |     |-- StreamBufferOverlay    (buffering progress during resolve)
  |     |
  |     |-- StreamClient           (isolated torrent client for streaming)
  |     |-- StreamSession          (active session state machine)
  |     |-- StreamService          (persistence: library, progress, choices)
  |
  |-- VideoPlayer (shared, already exists)
        Stream mode calls openFile() just like Videos mode
```

### File Ownership (all Agent 4)

**New files:**
- `src/ui/pages/StreamPage.h/.cpp` -- main page widget
- `src/ui/pages/stream/StreamSearchWidget.h/.cpp` -- catalog search
- `src/ui/pages/stream/StreamLibraryGrid.h/.cpp` -- library tile grid
- `src/ui/pages/stream/StreamContinueStrip.h/.cpp` -- continue watching
- `src/ui/pages/stream/StreamDetailView.h/.cpp` -- season/episode browser
- `src/ui/pages/stream/StreamSourcePicker.h/.cpp` -- torrent picker dialog
- `src/ui/pages/stream/StreamBufferOverlay.h/.cpp` -- buffering UI
- `src/core/stream/StreamClient.h/.cpp` -- isolated torrent client
- `src/core/stream/StreamSession.h/.cpp` -- session state machine
- `src/core/stream/StreamService.h/.cpp` -- persistence layer
- `src/core/stream/CinemetaApi.h/.cpp` -- Cinemeta metadata fetcher
- `src/core/stream/TorrentioApi.h/.cpp` -- Torrentio catalog + streams

**Modified files (shared, will announce in chat.md):**
- `CMakeLists.txt` -- add new source files
- `src/ui/MainWindow.cpp` -- replace stream placeholder with StreamPage

---

## Data Model

### StreamSession (runtime, not persisted)
```cpp
struct StreamSession {
    QString imdbId;
    QString mediaType;        // "movie" or "series"
    QString title;
    QString magnetUri;
    QString infoHash;
    int     fileIndex = -1;   // selected video file in torrent
    QString filePath;         // local path to the downloading file
    qint64  fileSize = 0;

    // Episode mapping (for series)
    QMap<QString, int> episodeFileMap;  // "s01e03" -> fileIndex
    QStringList playlist;              // ordered file paths

    // Buffer state
    float   downloadProgress = 0.f;
    qint64  downloadedBytes  = 0;
    bool    readyToPlay      = false;

    // Playback state
    QString currentEpisodeId;
    double  resumePositionSec = 0.0;

    // Lifecycle
    QDateTime createdAt;
    QString   state;          // "resolving", "buffering", "playing", "stalled", "error"
};
```

### Persistence (JSON, under AppDataDir/stream/)
```
AppDataDir/stream/
  stream_library.json      -- IMDB-keyed catalog of added shows/movies
  stream_progress.json     -- per-episode watch state (position, duration, finished)
  stream_choices.json      -- per-show torrent source selections
  stream_metadata.json     -- cached Cinemeta season/episode metadata (24h TTL)
```

### Torrent Isolation
```
AppDataDir/stream_cache/
  session.state            -- DHT state (separate from Tankorent's)
  resume/                  -- .fastresume files (separate)
  downloads/               -- active stream downloads (auto-cleaned)
```

**StreamClient** wraps TorrentEngine with:
- Separate listen port (6891 vs Tankorent's 6881)
- Separate cache directory
- Separate resume data path
- Auto-cleanup: when user stops watching, torrent is removed + files deleted
- No persistence to Tankorent's `torrents.json` -- completely invisible to Tankorent

---

## Phase Plan

### Phase 1: Stream Infrastructure (no UI)

**Goal:** StreamClient + StreamService + API clients. The foundation.

| Batch | Files | Scope |
|-------|-------|-------|
| 1 | StreamClient.h/.cpp | Isolated TorrentEngine wrapper: own port, own cache dir, add/remove/sequential/progress, auto-cleanup |
| 2 | StreamSession.h/.cpp | Session struct + state machine: resolving -> buffering -> playing -> done |
| 3 | StreamService.h/.cpp | JSON persistence: library CRUD, progress save/load, choice save/load, metadata cache with TTL |
| 4 | CinemetaApi.h/.cpp | `searchCatalog(query)` -> QList<CinemetaResult>, `fetchSeriesMeta(imdbId)` -> seasons/episodes. Async via QNetworkAccessManager. |
| 5 | TorrentioApi.h/.cpp | `fetchStreams(imdbId, season, episode)` -> QList<TorrentioStream> with magnet, title, seeders, size. Async. |

**Dependency:** None. Can start immediately.
**Touches shared files:** CMakeLists.txt (add new sources).

---

### Phase 2: Stream Page Shell + Library UI

**Goal:** Replace the Ctrl+4 placeholder with a real page. Show library grid and search.

| Batch | Files | Scope |
|-------|-------|-------|
| 6 | StreamPage.h/.cpp | QStackedWidget with 3 layers (browse/detail/player). Navigation: Escape goes back one level. Wire into MainWindow replacing placeholder. |
| 7 | StreamSearchWidget.h/.cpp | Search bar + async worker calling CinemetaApi. Result cards with poster, title, year, type. Click toggles library add/remove. |
| 8 | StreamLibraryGrid.h/.cpp | TileStrip-based grid of library entries. Poster tiles with status badges (watching/finished). Sort dropdown, filter input. Click -> detail view. |
| 9 | StreamContinueStrip.h/.cpp | Horizontal strip at top of browse layer. Shows recently-watched unfinished episodes. Click -> resume playback. |

**Dependency:** Phase 1 (StreamService for library/progress data).
**Touches shared files:** MainWindow.cpp (replace placeholder), CMakeLists.txt.

---

### Phase 3: Detail View + Source Selection

**Goal:** Season/episode browser with torrent source picker.

| Batch | Files | Scope |
|-------|-------|-------|
| 10 | StreamDetailView.h/.cpp | Show header (title, year, poster, description). Season tabs or dropdown. Episode list with progress indicators. Click episode -> source picker. Movie: single Play button. |
| 11 | StreamSourcePicker.h/.cpp | Modal dialog: fetch TorrentioApi streams, show sorted by seeders. Columns: Title, Size, Seeders, Source. User picks -> save choice via StreamService. If previously chosen, auto-use (skip dialog). |

**Dependency:** Phase 1 (APIs + service), Phase 2 (navigation).

---

### Phase 4: Playback + Buffer Gate

**Goal:** The core streaming experience. Resolve torrent, buffer, play.

| Batch | Files | Scope |
|-------|-------|-------|
| 12 | StreamBufferOverlay.h/.cpp | Overlay widget: "Connecting... 0%" -> "Buffering... 45%" -> auto-hides. Cancel button. Progress bar. |
| 13 | StreamPage.cpp (playback wiring) | Play flow: user picks source -> StreamClient adds magnet (sequential) -> poll progress every 500ms -> when buffer threshold met -> `VideoPlayer::openFile(filePath, playlist, index)` -> hide overlay. Resume from saved position (rewind 5s). |
| 14 | StreamPage.cpp (auto-next) | On VideoPlayer end-of-file: 10-second countdown overlay "Next episode in 10s [Cancel]". On timeout: resolve next episode (reuse pack mapping if available), start buffering, play. On cancel: return to detail view. |
| 15 | StreamPage.cpp (session lifecycle) | On stop/exit: save progress, stop torrent, clean downloaded files. On app shutdown: clean all stream cache. On page deactivate: pause stream (don't clean). |

**Dependency:** Phase 3 (detail view emits play signal), VideoPlayer (already exists).
**Touches shared files:** MainWindow.cpp (forward VideoPlayer signals for stream mode).

---

### Phase 5: Episode-File Mapping + Pack Intelligence

**Goal:** Multi-file torrent packs mapped to episodes automatically.

| Batch | Files | Scope |
|-------|-------|-------|
| 16 | StreamSession.cpp (file mapper) | When metadata arrives from torrent: parse filenames for season/episode tokens (S01E03, 1x03, "Episode 3", etc). Build `episodeFileMap`. Build playlist of all mapped episodes in order. |
| 17 | StreamPage.cpp (pack reuse) | When user plays a different episode from same torrent pack: don't re-add torrent, just switch fileIndex. Playlist naturally includes all mapped episodes. Track language preferences per show (audio/subtitle). |

**Dependency:** Phase 4 (playback working).

---

### Phase 6: Polish + Error Handling

| Batch | Files | Scope |
|-------|-------|-------|
| 18 | StreamPage.cpp | Error states: no seeders -> "No seeds, try different source" with re-picker. Metadata timeout (25s) -> try next candidate. Hard timeout (150s) -> error, return to detail. |
| 19 | StreamClient.cpp | Cache management: limit stream downloads dir to 2GB. Auto-prune oldest on exceeded. Clean on app exit. |
| 20 | StreamPage.cpp | Context menus on library tiles (ContextMenuHelper): Open, Continue, Mark Watched, Remove. Progress save on every position update (~1/sec). |

**Dependency:** Phase 5 complete.

---

## Total: 20 batches across 6 phases

| Phase | Batches | What |
|-------|---------|------|
| 1 | 1-5 | Infrastructure (client, session, service, APIs) |
| 2 | 6-9 | Page shell, library grid, search, continue strip |
| 3 | 10-11 | Detail view, source picker |
| 4 | 12-15 | Playback, buffer gate, auto-next, lifecycle |
| 5 | 16-17 | Episode-file mapping, pack reuse |
| 6 | 18-20 | Error handling, cache management, polish |

---

## Key Isolation Guarantees

1. **StreamClient has its own TorrentEngine instance** -- separate port (6891), separate DHT state, separate resume data directory. TorrentEngine is already designed to support multiple instances.

2. **StreamClient's torrents never appear in TorrentClient's listActive()** -- different m_records maps, different JSON persistence files, different cache paths. Zero data sharing.

3. **Stream downloads auto-clean.** When user stops watching, the torrent is removed and files deleted. Tankorent downloads are permanent; stream downloads are ephemeral. Fundamentally different lifecycle.

4. **No cross-contamination in UI.** StreamPage is Ctrl+4. SourcesPage (Tankorent/Tankoyomi) is Ctrl+5. Different top-level pages. No shared transfer lists, no shared status labels, no shared tab badges.

5. **VideoPlayer is the only shared component.** Both Videos mode and Stream mode call `openFile()`. The player doesn't know or care where the file came from. This is the correct contract -- same as groundwork's design intent.

---

## What's Different from Groundwork

| Aspect | Groundwork (Python) | Tankoban 2 (C++) |
|--------|---------------------|------------------|
| Streaming backend | WebTorrent Node.js sidecar OR libtorrent (competing) | Single libtorrent TorrentEngine (isolated instance) |
| File delivery | HTTP Range server on localhost | Direct file path (ffmpeg sidecar reads growing files) |
| Session model | Scattered across 5 stores | First-class StreamSession struct |
| Player launch | Empty show_id, one-item playlist | Full playlist with show identity |
| Auto-next | Depended on pre-saved choices | Uses pack file mapping from session |
| Cleanup | Manual/unreliable | Auto-clean on stop, cache limit enforced |
| Isolation from downloads | Same engine, fragile separation | Separate TorrentEngine instance, separate port, separate storage |

---

## Open Questions for Hemanth

1. **Poster images:** Cinemeta returns poster URLs. Should we download and cache them locally (like library thumbnails), or load async from URL each time the page opens?

2. **Stream library persistence:** Should removing a show from the stream library also wipe its watch progress? Or keep progress in case they re-add?

3. **Auto-next scope:** Should auto-next work across seasons (finish S01E10 -> start S02E01)? Or only within a season?

4. **Buffer threshold:** Default 5MB or 2% -- want this configurable in the More menu, or is a fixed default fine?

5. **Concurrent streams:** Allow only 1 active stream at a time (like Stremio), or allow multiple? I recommend 1 -- simpler, less resource contention.
