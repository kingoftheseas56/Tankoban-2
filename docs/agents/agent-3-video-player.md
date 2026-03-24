# Agent 3: Video Player

You are Agent 3. You own the **Video Player** — mpv-based video playback.

## Your Mission
Build a video player using libmpv that can play video files from the Videos library page.

## Current State
- `src/ui/pages/VideosPage.h/.cpp` — tile grid showing video shows/folders
- `src/core/VideosScanner.h/.cpp` — scans for mp4/mkv/avi/etc files
- Videos page has no click-to-play behavior yet
- No player exists — you're building from scratch
- The original Python app uses `libmpv-2.dll` located at `C:\Users\Suprabha\Desktop\TankobanQTGroundWork\resources\mpv\windows\libmpv-2.dll`

## Your Files (YOU OWN THESE)
- `src/ui/player/VideoPlayer.h` / `VideoPlayer.cpp` (create these)
- `src/ui/player/MpvWidget.h` / `MpvWidget.cpp` (mpv rendering surface)
- Any new files under `src/ui/player/`
- You may create `src/ui/pages/VideoSeriesView.h/.cpp` for episode lists

## DO NOT TOUCH
- `src/ui/MainWindow.h` / `MainWindow.cpp` — shared
- `src/main.cpp` — shared
- `CMakeLists.txt` — shared (tell the user what to add)
- `src/core/CoreBridge.*` / `src/core/JsonStore.*` — shared (read-only usage)
- `src/ui/readers/*` — Agent 1 and 2's territory
- `src/ui/pages/ComicsPage.*` / `src/ui/pages/BooksPage.*`

## Approach

### mpv Integration
- Use libmpv's client API (`mpv/client.h`) for playback
- Render via `mpv_render_api` with OpenGL, or use the simpler `mpv_set_option_string(mpv, "wid", ...)` approach to embed into a Qt widget's native window handle
- The simpler `wid` approach: create a plain QWidget, pass its `winId()` to mpv as the render target
- Link against `libmpv-2.dll` — copy it to the build output directory

### Player UI
- Fullscreen overlay (same pattern as ComicReader)
- Video rendering surface fills the widget
- Bottom control bar (auto-hide like ComicReader toolbar):
  - Play/Pause button
  - Seek bar (current position / duration)
  - Volume slider
  - Fullscreen toggle
  - Audio/subtitle track selection
- Escape to close

### Episode Navigation
- When opened from a show folder, know all episodes in the folder
- Next/previous episode buttons
- Auto-play next episode option

## Feature Roadmap

### Phase 1: Basic Playback
1. MpvWidget — embed mpv in a QWidget via wid
2. VideoPlayer overlay — fullscreen with basic controls
3. Play/pause, seek bar, volume
4. Click a video show tile → show episode list → play episode

### Phase 2: Track Selection
1. Audio track switching
2. Subtitle track switching (internal + external .srt/.ass)
3. Subtitle delay adjustment

### Phase 3: Progress & Navigation
1. Progress persistence (save position per video via CoreBridge)
2. Resume from last position on reopen
3. Episode navigation (prev/next)
4. Mark as watched

### Phase 4: Polish
1. Playback speed control
2. Screenshot (S key)
3. On-screen display (OSD) for volume/seek feedback
4. Keyboard shortcuts (Space=pause, F=fullscreen, M=mute, arrows=seek)

## Build System
- Qt6 at `C:\tools\qt6sdk\6.10.2\msvc2022_64`
- MSVC 2022 Build Tools
- `build_and_run.bat` compiles and launches
- You'll need to add mpv to the build:
  - Download mpv-dev from https://sourceforge.net/projects/mpv-player-windows/files/libmpv/
  - Or use the existing `libmpv-2.dll` from the original project
  - Link against `mpv.lib` (import library) and ship `libmpv-2.dll` alongside the exe

## Integration Points
When ready to wire into the app, tell the user to:
1. Add your files to `CMakeLists.txt`
2. Add mpv include/lib paths to CMakeLists
3. Add `#include "player/VideoPlayer.h"` in `MainWindow.cpp`
4. Create `m_videoPlayer = new VideoPlayer(root)` in MainWindow constructor
5. Connect `VideosPage::playVideo(filePath)` signal → `MainWindow::openVideoPlayer(filePath)`
6. Copy `libmpv-2.dll` to build output dir

## Reference
Original Python player at `C:\Users\Suprabha\Desktop\TankobanQTGroundWork\player_qt\` and mpv DLL at `resources\mpv\windows\libmpv-2.dll`.
