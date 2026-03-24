# Agent 4: Stream & Sources

You are Agent 4. You own **Stream** (torrent streaming) and **Sources** (content discovery).

## Your Mission
Build the Stream and Sources pages — torrent-based streaming and content discovery/search.

## Current State
- Stream and Sources tabs exist in the topbar but show placeholder pages
- No streaming or discovery code exists yet
- The original Python app uses webtorrent (Node.js) for torrent streaming
- The original has a `sources_core` Python package for content discovery

## Your Files (YOU OWN THESE)
- `src/ui/pages/StreamPage.h` / `StreamPage.cpp` (create these)
- `src/ui/pages/SourcesPage.h` / `SourcesPage.cpp` (create these)
- `src/core/StreamService.h` / `StreamService.cpp` (create these)
- `src/core/SourcesService.h` / `SourcesService.cpp` (create these)
- Any new files under `src/ui/pages/stream/` or `src/ui/pages/sources/`

## DO NOT TOUCH
- `src/ui/MainWindow.h` / `MainWindow.cpp` — shared
- `src/main.cpp` — shared
- `CMakeLists.txt` — shared (tell the user what to add)
- `src/core/CoreBridge.*` / `src/core/JsonStore.*` — shared (read-only usage)
- `src/ui/readers/*` — Agent 1 and 2's territory
- `src/ui/player/*` — Agent 3's territory
- `src/ui/pages/ComicsPage.*` / `src/ui/pages/BooksPage.*` / `src/ui/pages/VideosPage.*`

## Approach

### Stream Page
The stream feature lets users search for content, find torrent sources, and stream video directly.

**Architecture options (pick the simplest):**
1. **Shell out to webtorrent-cli** — simplest, no C++ torrent library needed
2. **Embed libtorrent (C++)** — full native integration, complex build
3. **Node.js sidecar** — run the existing webtorrent Node.js code as a subprocess, communicate via IPC

The original Python app uses option 3 (Node.js webtorrent sidecar at `resources/webtorrent/`).

**Recommended for MVP:** Start with option 3 — reuse the existing Node.js webtorrent runtime. The node_modules are already at `C:\Users\Suprabha\Desktop\TankobanQTGroundWork\resources\webtorrent\windows\`.

### Sources Page
Content discovery — search for shows/movies by name, get metadata (poster, year, synopsis).

**The original uses:**
- IMDB/TMDB API for metadata
- Custom scraper modules for torrent sources
- `sources_core` Python package

**For C++ MVP:** Use Qt's `QNetworkAccessManager` to call a metadata API (TMDB). Display results in a tile grid.

## Feature Roadmap

### Phase 1: Sources Search
1. Search bar at the top of Sources page
2. Query TMDB API for movies/shows
3. Display results as tiles (poster + title + year)
4. Click result → show details (synopsis, seasons, episodes)

### Phase 2: Stream Integration
1. Stream page shows "Continue Watching" and search
2. User picks a show/movie from Sources → routes to Stream
3. Find torrent sources (requires scraper or API)
4. Start streaming via webtorrent sidecar
5. Hand off video URL to the Video Player (Agent 3's domain, coordinate via signals)

### Phase 3: Progress & History
1. Watch history persistence
2. Continue watching row
3. Episode tracking per show

## Build System
- Qt6 at `C:\tools\qt6sdk\6.10.2\msvc2022_64`
- MSVC 2022 Build Tools
- `build_and_run.bat` compiles and launches
- You'll need `Qt6::Network` for HTTP requests (add to `find_package`)
- Node.js may be needed for the webtorrent sidecar

## Integration Points
When ready to wire into the app, tell the user to:
1. Add your files to `CMakeLists.txt`
2. Replace the Stream/Sources placeholder pages in `MainWindow::buildPageStack()`
3. Add `Qt6::Network` to `find_package` and `target_link_libraries`
4. For video playback, emit a signal that MainWindow routes to Agent 3's VideoPlayer

## Important Notes
- Stream and Sources are the most complex features and depend on external services
- Start with Sources (search + metadata) since it's self-contained
- Stream requires coordination with Agent 3 (Video Player) for actual playback
- Don't over-engineer — an MVP that searches TMDB and shows results is a great start

## Reference
Original Python sources at:
- `C:\Users\Suprabha\Desktop\TankobanQTGroundWork\app_qt\ui\pages\sources_page.py`
- `C:\Users\Suprabha\Desktop\TankobanQTGroundWork\app_qt\ui\pages\stream\` (multiple files)
- `C:\Users\Suprabha\Desktop\TankobanQTGroundWork\app_qt\services\stream_service.py`
- `C:\Users\Suprabha\Desktop\TankobanQTGroundWork\resources\webtorrent\` (Node.js sidecar)
