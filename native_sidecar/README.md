# native_sidecar

Tankoban 2's video decode/render sidecar. Runs as a subprocess (`ffmpeg_sidecar.exe`) of the main app; communicates over stdin/stdout JSON per `src/protocol.h`.

## Why a sidecar

FFmpeg + libass + PortAudio + libplacebo in-process with Qt was too much shared-state risk. Keeping decode/audio/subtitle rendering in a separate process gives us clean crash isolation (see Player Polish Phase 6 Batch 6.1 auto-restart) and lets the decoder thread hammer libavcodec without fighting Qt's event loop.

## Building

```powershell
.\build.ps1          # configure + build + install to ../resources/ffmpeg_sidecar/
.\build.ps1 -Clean   # clean build
```

Requires:
- MinGW 64-bit at `C:\tools\mingw64\`
- CMake 3.20+ at `C:\tools\cmake-*\`
- Pre-built deps at `C:\tools\{ffmpeg,portaudio,libass,libplacebo,vulkan,glslang,lcms2,uchardet,freetype,fribidi,harfbuzz}\` — see `CMakeLists.txt` for the hint paths.

Output: `{repo_root}/resources/ffmpeg_sidecar/ffmpeg_sidecar.exe` + MinGW runtime DLLs.

`SidecarProcess::sidecarPath()` in the main app looks up the binary in this order: next to `Tankoban.exe` (production), `{repo_root}/resources/ffmpeg_sidecar/` (dev install), `native_sidecar/build/` (dev raw build output).

## Source layout

- `src/main.cpp` — command/event dispatch loop + process lifecycle.
- `src/demuxer.*` — input probe + stream enumeration.
- `src/video_decoder.*` / `audio_decoder.*` — per-stream decode threads.
- `src/filter_graph.*` — audio/video FFmpeg filter chains.
- `src/subtitle_renderer.*` — libass + PGS bitmap rendering.
- `src/gpu_renderer.*` — libplacebo HDR/tonemap path (optional).
- `src/d3d11_presenter.*` — Holy Grail zero-copy texture publisher.
- `src/shm_helpers.*` + `ring_buffer.*` — shared-memory frame transport.
- `src/av_sync_clock.*` — audio-master A/V clock.
- `src/heartbeat.*` + `state_machine.*` — lifecycle + watchdog.
- `src/protocol.*` — JSON event/command serialization.
- `vendor/nlohmann/json.hpp` — header-only JSON library.

## Migration note

Moved from `TankobanQTGroundWork/native_sidecar/` to `Tankoban 2/native_sidecar/` on 2026-04-15 per Hemanth's "Tankoban 2 must be completely individual" ruling. The groundwork copy is now obsolete; any future edits land here.

Build artifacts (`build/`) are gitignored. Produced exe at `resources/ffmpeg_sidecar/` is also gitignored (rebuild locally via `build.ps1`).
