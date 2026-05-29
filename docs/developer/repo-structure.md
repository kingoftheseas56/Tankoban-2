# Repository Structure

A directory-by-directory tour of the repo **as it is today**. A cleanup is in progress (see `REPO_STRUCTURE_CLEANUP_FIX_TODO.md` at repo root); where a directory is slated to change, it's noted inline.

## Top level

- **`src/`** — the Qt6 main application.
  - `src/core/` — backend logic, organized by domain: `book/`, `manga/`, `stream/`, `torrent/` (plus shared primitives like the library scanner, `CoreBridge`, `JsonStore`, `DebugLogBuffer`).
  - `src/ui/` — the interface: `pages/` (one per mode — `comics/`, `books/`, `stream/`, plus the mode roots), `readers/` (comic + book readers), `player/` (the video player surface), and shared widgets.
  - `src/devtools/` — the dev-control bridge surface (gated, dev-only).
  - `src/main.cpp` — app entry point.
- **`native_sidecar/`** — the FFmpeg sidecar process, a **separate CMake project** built independently. Communicates with the main app over stdin/stdout JSON. Holds the platform-specific decode/render/audio code (today: Windows D3D11/DXGI). This boundary is the focus of the planned macOS port.
- **`tests/`** — GoogleTest suite, opt-in via `-DTANKOBAN_BUILD_TESTS=ON`. Mirrors `src/` layout (`core/`, `ui/`, `fixtures/`).
- **`tools/`** — development tools, notably `tankoctl` (the dev-control console client).
- **`scripts/`** — build, smoke, lint, and health helpers (PowerShell + batch).
- **`resources/`** — embedded Qt assets (`resources.qrc` — icons, shaders, QSS) alongside copied-at-build runtime payloads (book reader, stream-server, ffmpeg sidecar, fandom manifests). *(Slated to split embedded vs runtime assets in the cleanup.)*
- **`docs/`** — project documentation. Start at [`docs/README.md`](../README.md).
- **`agents/`** — internal multi-agent coordination state (governance, status, chat, audits). Operational, not public docs; external readers can ignore it.
- **`.github/workflows/`** — CI: `build.yml` (full Windows build on every push/PR), `release.yml` (tag-driven NSIS installer), `repo-consistency.yml` (fast lint).

## Build system

- **Root `CMakeLists.txt`** is the source of truth for the main app: it carries the full source + header lists, the executable target, the test target (`tankoban_tests`), and runtime-asset copy rules. The only broad include root is `${CMAKE_SOURCE_DIR}/src`. *(The flat lists are slated to move into `cmake/*.cmake` includes in the cleanup — moving a source file today requires updating both the app and test lists in this file.)*
- **`native_sidecar/CMakeLists.txt`** builds the sidecar separately (MinGW toolchain; Windows/DirectX libs today).
- **`vcpkg.json`** pins libtorrent + Boost + OpenSSL; `setup.bat` drives the first configure.

## Platform note

About 90% of the app (Comics, Books, Theatre business logic, scrapers, library, UI) is platform-independent Qt/C++. The Windows-specific surface is concentrated in the video decode/render layer (`native_sidecar/` + `src/ui/player/`). The planned macOS support keeps one shared core and adds a per-platform video backend selected at build time — it does **not** fork the rest of the app. See `project_macos_target_end_user` (internal memory) and the `MACOS_DUAL_BACKEND` arc.
