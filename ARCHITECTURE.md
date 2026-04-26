# Architecture

High-level orientation map for Tankoban. This isn't exhaustive — it's the picture you'd want before opening source files for the first time.

---

## Process model

Tankoban runs as **three Windows processes** during normal use:

```
┌──────────────────────────────────────────────────────────────────┐
│  Tankoban.exe                                                     │
│  ─────────────                                                    │
│  Qt6 GUI app. Library scanning, page navigation, all UI.          │
│  Hosts libtorrent-rasterbar in-process (Tankorent + Stream        │
│  torrents share one session).                                     │
│                                                                   │
│   stdin/stdout JSON                                               │
│   ↓                                                               │
│  ┌───────────────────────────┐    ┌──────────────────────────┐   │
│  │ ffmpeg_sidecar.exe        │    │ stream-server.exe        │   │
│  │ ────────────────────      │    │ ─────────────────────    │   │
│  │ Subprocess launched on    │    │ Stremio's reference HTTP │   │
│  │ video play. Owns FFmpeg   │    │ stream server. Bridges   │   │
│  │ decode, libplacebo HDR    │    │ libtorrent piece state   │   │
│  │ render, libass subs,      │    │ to a localhost HTTP URL  │   │
│  │ PortAudio output. Renders │    │ that ffmpeg_sidecar      │   │
│  │ frames to a shared-memory │    │ reads as input. Active   │   │
│  │ ring the Qt FrameCanvas   │    │ only during Stream-mode  │   │
│  │ widget reads.             │    │ playback.                │   │
│  └───────────────────────────┘    └──────────────────────────┘   │
└──────────────────────────────────────────────────────────────────┘
```

**Why three processes?**

- **Crash isolation.** A bad codec or corrupt subtitle file kills the sidecar, not the Qt UI. Tankoban auto-restarts the sidecar with the last-known position.
- **License separation.** FFmpeg under LGPL+GPL stays subprocess-isolated from the Qt UI under LGPL.
- **Toolchain separation.** The sidecar builds with MinGW + GCC (better libplacebo + libass support); the Qt app builds with MSVC. Each picks the toolchain that fits its dependencies best.

A fourth optional process (`tankoctl.exe`) is the dev-control client — a console exe that connects to a `QLocalServer` named pipe inside Tankoban for fast state queries during agent-driven development. Gated behind `--dev-control`; not advertised in production builds.

---

## Core components (Tankoban.exe)

### `src/main.cpp`

Entry point. Single-instance gate via QLocalServer pipe `TankobanSingleInstance` (second launch raises the existing window and exits). Optional dev-control bridge gate via `--dev-control` flag or `TANKOBAN_DEV_CONTROL=1` env var.

### `src/core/`

Backend that the UI sits on top of.

- **`CoreBridge`** — central facade Qt UI talks to. Owns root-folder lists, library data, JsonStore.
- **`JsonStore`** — bounded coalescing async JSON persistence layer. Per-file write-queue runs on a background thread; reads always see the latest in-process write. (REPO_HYGIENE Phase 4 P4.1 separated the read-truth map from the disk-write-queue to fix a race.)
- **`DebugLogBuffer`** — bounded ring-buffer (500 entries) for structured debug events. In-memory by default; flushes to `<AppData>/Tankoban/debug.log` when `TANKOBAN_DEBUG_LOG=1`.
- **`LibraryScanner` / `BooksScanner` / `VideosScanner`** — domain-specific scanners running on dedicated `QThread`s, fed by `ScannerUtils` shared walk + filter logic.
- **`ScannerUtils`** — recursive file walk with cooperative cancellation, depth cap, symlink-loop guard.
- **`stream/`** — Stremio addon protocol client: catalog aggregation, meta lookup, addon registry, stream-server subprocess wrapper.
- **`torrent/`** — libtorrent-rasterbar wrapper. `TorrentClient` is the high-level facade; `TorrentEngine` owns the libtorrent session.
- **`book/`** — book-source scrapers (LibGen, Anna's Archive) + `BookDownloader` for the actual file fetch.
- **`manga/`** — manga-source scrapers (WeebCentral, ReadComics) + `MangaDownloader`.

### `src/ui/`

The Qt6 widget tree.

- **`MainWindow`** — top-level QMainWindow. Page stack (QStackedWidget) over the 5 domain pages. Hosts overlay readers (ComicReader, BookReader) and the VideoPlayer overlay.
- **`pages/`** — one per domain: ComicsPage, BooksPage, VideosPage, StreamPage, SourcesPage. Each owns its scanner thread + tile strip + list view + search bar.
- **`pages/stream/`** — Stream mode UI: home board, catalog browse, calendar, source picker, search, addon manager.
- **`pages/tankorent/`** + **`pages/tankoyomi/`** + **`pages/tankolibrary/`** — the three Sources sub-apps (torrent client, manga search, book search).
- **`readers/`** — ComicReader (CBZ/CBR + scroll-strip + double-page), BookReader (EPUB renderer + Edge TTS), with the BookBridge between Qt and the embedded HTML reader.
- **`player/`** — VideoPlayer + SidecarProcess + FrameCanvas (shared-memory frame reader) + HUD overlays (SeekSlider, VolumeHud, popovers, subtitle overlay).
- **`Theme.{h,cpp}`** — single-axis Mode picker (5 dark modes: Dark, Nord, Solarized, Gruvbox, Catppuccin). Per-Mode QSS + accent colors. Persisted via QSettings.
- **`devtools/`** — `DevControlServer` (QLocalServer-backed dev bridge for agent smokes). Listens on `TankobanDevControl` named pipe; main app gates the listen behind `--dev-control` flag.

---

## Native sidecar (`native_sidecar/`)

Standalone cmake subproject that builds `ffmpeg_sidecar.exe`. Communicates with the Qt app over stdin/stdout JSON (one command/event per line, schema-versioned `tankoban.dev.v1`).

Key components:

- **`main.cpp`** — command dispatcher. Each command runs in a worker (e.g. `open_worker`, `set_tracks_worker`) so long-running operations don't block the IPC dispatcher.
- **`demuxer.{h,cpp}`** — FFmpeg AVFormatContext wrapper. Probes streams + extracts metadata.
- **`video_decoder.{h,cpp}`** — AVCodec decode loop, hardware-accelerated when D3D11 is available.
- **`audio_decoder.{h,cpp}`** — AVCodec audio decode + PortAudio playback.
- **`subtitle_renderer.{h,cpp}`** — libass-based ASS/SSA renderer, Y-offset support for user-adjustable subtitle position.
- **`gpu_renderer.{h,cpp}`** — libplacebo-based HDR pipeline (Vulkan + ewa_lanczossharp + tone-mapping). Activates for HDR sources by default; SDR opt-in via `TANKOBAN_LIBPLACEBO_SDR=1`.
- **`av_sync_clock.h`** — master A/V clock. Audio thread anchors it; video thread reads it. Stall pause/resume freezes/re-anchors during stream buffering.
- **`ring_buffer.h`** — single-producer single-consumer frame queue between decode + render threads.
- **`shm_helpers.h`** — Windows shared-memory wrapper for the frame ring + overlay textures (read by Qt's FrameCanvas widget).

The sidecar emits a `version` event with `{schema:"v2", session_strict:true}` at startup; the Qt app's filter rejects session-scoped events with empty sessionId once strict mode is on.

---

## Stream mode pipeline

```
User picks an episode in Stream mode
   ↓
StreamPlayerController fetches stream URLs from Stremio addon
   ↓
StreamServerProcess starts stream-server.exe (if not running)
   ↓
StreamServerClient asks the server to start a torrent for the picked stream
   ↓
stream-server returns a localhost HTTP URL (e.g. http://127.0.0.1:11470/<hash>/<file>)
   ↓
VideoPlayer.openFile("http://...") → SidecarProcess.sendOpen(httpUrl)
   ↓
ffmpeg_sidecar opens the URL with avformat_open_input — FFmpeg streams over HTTP
   ↓
stream-server pulls torrent pieces in sequential order behind the scenes
   ↓
On stall (peer drought), StreamEngine emits stallDetected → VideoPlayer.sendStallPause
   ↓
On recovery, sendStallResume re-anchors AVSyncClock
```

This pipeline is intentionally separate from the local-file pipeline: local files use the same VideoPlayer + SidecarProcess but bypass `stream-server` entirely. The `setStreamMode(true)` flag on VideoPlayer gates the buffered-range overlay paint + stall-watchdog wiring.

---

## IPC + persistence

- **Tankoban → sidecar**: newline-delimited compact JSON over stdin/stdout. Request shape `{type:"cmd", name, sessionId, seq, payload}`; response shape `{type:"event", name, sessionId, seq, payload}`.
- **Tankoban → tankoctl** (dev-control): newline-delimited compact JSON over the `TankobanDevControl` named pipe (one command per connection). Schema-versioned `tankoban.dev.v1`.
- **Settings**: `QSettings` (per-user Windows registry under `HKCU\Software\Tankoban\Tankoban`).
- **Library data**: `JsonStore` writes to `<AppData>/Tankoban/data/<filename>.json`. Coalescing async writer prevents the per-write fsync stall that QSaveFile would cause at 1 Hz `saveProgress` cadence.
- **Posters**: SHA1(showPath)-keyed JPGs under `<AppData>/Tankoban/data/posters/`.
- **Debug log** (when env-gated on): `<AppData>/Tankoban/debug.log`. Bounded ring buffer; capacity 500 entries, oldest popped first.

---

## Where to read first

If you're trying to understand:

- **How the video player works end-to-end** — start at `src/ui/player/VideoPlayer.cpp`, follow `openFile` → `SidecarProcess::sendOpen` → `native_sidecar/src/main.cpp:open_worker`.
- **How a library tile becomes a play action** — `src/ui/pages/VideosPage.cpp` (or BooksPage / ComicsPage) → `MainWindow::openVideoPlayer` → VideoPlayer.openFile.
- **How Stream mode finds a playable URL** — `src/ui/pages/stream/StreamPlayerController.cpp` is the orchestrator; the Stremio addon dance lives in `src/core/stream/`.
- **How libtorrent state gets into the UI** — `src/core/torrent/TorrentEngine.cpp` exposes signals; `TorrentClient` adapts; consumers in `src/ui/pages/TankorentPage.cpp` and stream pages.
- **Theme system** — `src/ui/Theme.{h,cpp}` is the single source. `applyThemeFromSettings(app)` in main.cpp wires the palette + QSS.

For internal LLM-agent coordination (which file is whose responsibility, governance rules, in-flight fix-TODOs), see `agents/GOVERNANCE.md` + `agents/ONBOARDING.md`. None of that is required to read or contribute to the code.
