# Agent 3: Video Player

You are Agent 3. You own the **Video Player** — ffmpeg-based video playback with native Qt rendering.

## CRITICAL: NO CHILD WINDOWS
**The user (Hemanth) absolutely forbids child windows.** No `wid` hacks, no foreign HWNDs, no embedding external player windows. The video must render as a native Qt widget — part of the app's own render tree. The user already built a custom ffmpeg player specifically to avoid mpv's child window approach.

## Your Mission
Build a video player using ffmpeg that renders decoded frames directly into a Qt widget. The original Python app has a complete implementation you should study and port.

## Existing FFmpeg Player (REFERENCE — study this first!)
The user already built a full custom ffmpeg player in Python/Qt. It lives at:
```
C:\Users\Suprabha\Desktop\TankobanQTGroundWork\app_qt\ui\player\
```

### Architecture (sidecar model):
- **FFmpeg sidecar process** — separate process decodes video frames
- **Shared memory ring buffer** — sidecar writes BGRA frames, Qt reads them
- **Multiple rendering backends** (pick the best for C++):
  - `ffmpeg_frame_canvas.py` — QPainter consumer, reads from shared-memory ring buffer (~125Hz poll)
  - `d3d11_frame_canvas.py` — zero-copy GPU: D3D11 shared texture → OpenGL texture → QOpenGLWidget (holy grail path)
  - `gl_frame_canvas.py` — OpenGL fallback
  - `rhi_frame_canvas.py` — Qt RHI path
- **Process manager** (`ffmpeg_process_manager.py`) — lifecycle, IPC, state machine
- **Sidecar protocol** (`ffmpeg_sidecar/`) — command dispatch, ack matching, timeout monitoring

### Key files to study:
- `app_qt/ui/player/ffmpeg_frame_canvas.py` — simplest rendering path (QPainter + shared memory)
- `app_qt/ui/player/d3d11_frame_canvas.py` — GPU zero-copy path
- `app_qt/ui/player/ffmpeg_process_manager.py` — sidecar lifecycle
- `app_qt/ui/player/ffmpeg_player_surface.py` — player surface with HUD overlays
- `app_qt/ui/player/embedded_player_host.py` — host bridge, progress, controls
- `app_qt/ui/player/ffmpeg_sidecar/` — IPC protocol, ring buffer, client

### Player UI files:
- `player_qt/ui/bottom_hud.py` — seek bar, time display, controls
- `player_qt/ui/volume_hud.py` — volume control
- `player_qt/ui/track_popover.py` — audio/subtitle track selection
- `player_qt/ui/playlist_drawer.py` — episode list sidebar

## Your Files (YOU OWN THESE)
- `src/ui/player/VideoPlayer.h` / `VideoPlayer.cpp` (create these)
- `src/ui/player/FrameCanvas.h` / `FrameCanvas.cpp` (Qt widget that paints decoded frames)
- `src/ui/player/FfmpegDecoder.h` / `FfmpegDecoder.cpp` (decode thread using libavcodec/libavformat)
- Any new files under `src/ui/player/`
- You may create `src/ui/pages/VideoSeriesView.h/.cpp` for episode lists

## DO NOT TOUCH
- `src/ui/MainWindow.h` / `MainWindow.cpp` — shared
- `src/main.cpp` — shared
- `CMakeLists.txt` — shared (tell the user what to add)
- `src/core/CoreBridge.*` / `src/core/JsonStore.*` — shared (read-only usage)
- `src/ui/readers/*` — Agent 1 and 2's territory
- `src/ui/pages/ComicsPage.*` / `src/ui/pages/BooksPage.*`

## Recommended C++ Approach

### Option A: In-process ffmpeg (simpler for C++)
In C++, you can use libavcodec/libavformat directly in-process (no sidecar needed):
1. `FfmpegDecoder` runs on a `QThread`
2. Decodes frames using avcodec → converts to QImage (BGRA) via swscale
3. Emits `frameReady(QImage)` signal to the main thread
4. `FrameCanvas` (QWidget) receives frames and paints them in `paintEvent`
5. Audio via `QAudioOutput` or SDL2 audio

### Option B: Sidecar (matches Python architecture)
Port the sidecar model — separate process, shared memory ring buffer. More complex but proven.

### Recommendation: Start with Option A for MVP, it's much simpler in C++.

## Player UI
- Fullscreen overlay (same pattern as ComicReader — child of MainWindow root)
- FrameCanvas fills the widget, paints decoded video frames
- Bottom control bar (auto-hide after 3s):
  - Play/Pause button
  - Seek bar (current position / duration)
  - Volume slider
  - Audio/subtitle track buttons
- Escape to close

## Feature Roadmap

### Phase 1: Basic Playback
1. FfmpegDecoder — decode video frames on a background thread
2. FrameCanvas — QWidget painting decoded QImage frames
3. VideoPlayer overlay — fullscreen with play/pause, seek, volume
4. Click a video tile → episode list → play

### Phase 2: Audio & Tracks
1. Audio playback (sync with video)
2. Audio track switching
3. Subtitle rendering (internal + external .srt/.ass)

### Phase 3: Progress & Navigation
1. Progress persistence via CoreBridge
2. Resume from last position
3. Episode navigation (prev/next)
4. Mark as watched

### Phase 4: Polish
1. Playback speed control
2. Keyboard shortcuts (Space, F, M, arrows)
3. Volume OSD
4. Smooth seeking

## Build System
- Qt6 at `C:\tools\qt6sdk\6.10.2\msvc2022_64`
- MSVC 2022 Build Tools
- `build_and_run.bat` compiles and launches
- You'll need ffmpeg dev libraries:
  - libavcodec, libavformat, libavutil, libswscale, libswresample
  - Download from https://github.com/BtbN/FFmpeg-Builds/releases (shared build)
  - Add include/lib paths to CMakeLists

## Integration Points
When ready to wire into the app, tell the user to:
1. Add your files to `CMakeLists.txt`
2. Add ffmpeg include/lib paths
3. Add `#include "player/VideoPlayer.h"` in `MainWindow.cpp`
4. Create `m_videoPlayer = new VideoPlayer(root)` in MainWindow constructor
5. Connect `VideosPage::playVideo(filePath)` → `MainWindow::openVideoPlayer(filePath)`
6. Ship ffmpeg DLLs alongside the exe
