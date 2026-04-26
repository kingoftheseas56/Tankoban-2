# Building Tankoban from source

Tankoban targets **Windows 10 / 11 only** (Qt6 + MSVC 2022 + native libtorrent + FFmpeg). Linux / macOS support is not in scope.

**Quick path (recommended for fresh clones)**: install Qt 6.10.2 + MSVC 2022 Build Tools, then run `setup.bat` once — it auto-clones vcpkg if needed, installs libtorrent + Boost + OpenSSL via the manifest at `vcpkg.json`, and runs the first cmake configure. After that, normal dev cycle is just `build_and_run.bat`. Native sidecar deps stay manual (it builds with MinGW separately) — see § Native sidecar prerequisites below.

---

## Prerequisites

All paths below match what `build_and_run.bat` expects. Use these exact paths to avoid editing the script.

### 1. Qt 6.10.2 (msvc2022_64)

Install via the [Qt online installer](https://www.qt.io/download-qt-installer) into `C:\tools\qt6sdk\6.10.2\msvc2022_64`. Required components:

- Qt6 Core, Gui, Widgets, Network, OpenGL, OpenGLWidgets, Svg
- Optional but recommended: Multimedia, WebEngineWidgets, WebSockets

### 2. MSVC 2022 Build Tools

Install [Visual Studio 2022 Build Tools](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022) with the "Desktop development with C++" workload. The build expects `vcvarsall.bat` at the standard path:

```
C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat
```

### 3. Main app native dependencies (vcpkg-managed)

`setup.bat` handles these automatically. Manifest is at `vcpkg.json`; baseline pinned for reproducible builds. First-time install of all deps takes ~30 minutes (libtorrent + Boost + OpenSSL build from source). Subsequent runs use vcpkg's binary cache.

If you want to install vcpkg manually before running setup.bat:

```cmd
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics
set VCPKG_ROOT=C:\vcpkg
```

setup.bat detects vcpkg at `C:\vcpkg`, `C:\tools\vcpkg`, or wherever `VCPKG_ROOT` env var points.

| Library | Source |
|---|---|
| libtorrent-rasterbar 2.0.x | vcpkg `libtorrent` |
| Boost system + filesystem | vcpkg `boost-system` + `boost-filesystem` |
| OpenSSL | vcpkg `openssl` (transitive via libtorrent) |
| FFmpeg shared (sidecar-side, not main app) | manual install — `C:\tools\ffmpeg-master-latest-win64-gpl-shared` |
| Sherpa-ONNX (TTS) | `third_party/sherpa-onnx/sherpa-onnx-v1.12.21-win-x64-shared/` (vendored in-tree) |

### 4. Native sidecar prerequisites

The FFmpeg sidecar (`native_sidecar/`) builds with **MinGW + GCC** (separate from the main app's MSVC). Install:

- MinGW-w64 (GCC 13+) on PATH
- libplacebo, libass, libfribidi, libharfbuzz, lcms2, vulkan-1, libuchardet — all under `C:\tools\` per `native_sidecar/build.ps1`

The sidecar binary lives in `resources/ffmpeg_sidecar/ffmpeg_sidecar.exe` after a successful build. The committed source is in `native_sidecar/src/`.

---

## First-time setup (fresh clone)

```cmd
setup.bat
```

This script:

1. Verifies Qt 6.10.2 is installed at the expected path; prints install instructions if not.
2. Verifies MSVC 2022 Build Tools is installed; prints install instructions if not.
3. Detects vcpkg at `C:\vcpkg` / `C:\tools\vcpkg` / `VCPKG_ROOT`; auto-clones to `C:\vcpkg` if missing.
4. Runs `vcpkg install --triplet x64-windows` driven by `vcpkg.json` (~30 min first time, cached after).
5. Runs `cmake --preset default` to produce a configured build tree at `out/`.

After this succeeds, normal dev cycle is `build_and_run.bat`.

## Quick build (after setup)

From a Developer Command Prompt for VS 2022, at the repo root:

```cmd
build_and_run.bat
```

This script:

1. Kills any running `Tankoban.exe` / `ninja.exe` / `cl.exe`.
2. Calls `vcvarsall.bat x64`.
3. Auto-sets `VCPKG_ROOT` to `C:\vcpkg` or `C:\tools\vcpkg` if not already in env.
4. Configures cmake via `cmake --preset default` (skipped if `out\CMakeCache.txt` exists).
5. Builds the `Tankoban` executable (and `tankoctl.exe`, the dev-control client) with `cmake --build out --parallel`.
6. Deploys book-reader resources to `out/resources/book_reader/`.
7. Sets PATH to include Qt + FFmpeg + Sherpa runtime DLLs.
8. Launches `Tankoban.exe --dev-control` so the dev bridge is live for development smoke testing.

---

## Compile-only verification (agents / CI)

For a fast compile-only check (no exe run, no GUI spawn), use:

```cmd
build_check.bat
```

It produces `BUILD OK` on success or `BUILD FAILED exit=<n>` + the last 30 lines of cl.exe output on failure.

---

## Native sidecar build

The sidecar is a separate cmake subproject:

```powershell
powershell -File native_sidecar/build.ps1
```

This installs the produced `ffmpeg_sidecar.exe` into `resources/ffmpeg_sidecar/`. `build_and_run.bat` does not invoke the sidecar build — it picks up whatever's already deployed. After editing `native_sidecar/src/`, run `build.ps1` then re-run `build_and_run.bat`.

---

## Tests

Tests are opt-in. Configure with `-DTANKOBAN_BUILD_TESTS=ON`; cmake fetches GoogleTest via `FetchContent` on first configure.

```cmd
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release -DTANKOBAN_BUILD_TESTS=ON ^
    -DCMAKE_PREFIX_PATH="C:/tools/qt6sdk/6.10.2/msvc2022_64" ^
    <other -D flags from build_and_run.bat lines 29-37>
cmake --build out --target tankoban_tests
cd out
ctest --output-on-failure -R tankoban_tests
```

Tests target pure-logic primitives only (scanner utils, persistence helpers). The video pipeline + sidecar IPC are smoke-tested via `tankoctl` + manual playback rather than unit tests.

---

## Repo-consistency lint

Quick static check that no committed file regresses Phase 1 hygiene fixes (hardcoded developer paths, missing CMakeLists entries, bare debug-log literals):

```bash
bash scripts/repo-consistency.sh
```

Runs in ~1s. CI runs the same script on every push via `.github/workflows/repo-consistency.yml`.

---

## Troubleshooting

### "ffmpeg_sidecar.exe not found"

The sidecar wasn't deployed. Run `powershell -File native_sidecar/build.ps1` to build + install it into `resources/ffmpeg_sidecar/`.

### LNK1168: cannot open Tankoban.exe for writing

A previous Tankoban instance is still holding the file. Close the app and / or:

```cmd
taskkill /F /IM Tankoban.exe
```

Then re-run the build.

### libtorrent link errors

Verify `C:\tools\libtorrent-2.0-msvc\lib\torrent-rasterbar.lib` exists and was built with the same MSVC version (2022) and runtime config (Release / multi-threaded DLL) as Tankoban.

### Qt DLL "not found" at runtime

`build_and_run.bat` adds Qt's `bin/` to PATH before launching. If you launch `out\Tankoban.exe` directly from Explorer, Windows can't find the Qt DLLs. Either launch via the script or copy `Qt6Core.dll`, `Qt6Gui.dll`, etc. next to the exe.

### CMake reconfigure needed after pulling new commits

If `out\CMakeCache.txt` is stale (someone added a new `.cpp` to `set(SOURCES ...)`), build_and_run.bat skips reconfigure and you'll see LNK2019 errors. Force a reconfigure:

```cmd
del out\CMakeCache.txt
build_and_run.bat
```

---

## Pre-built dependency links

Where to find or build the native deps if you don't have them already:

- libtorrent-rasterbar 2.0 — [build from source](https://github.com/arvidn/libtorrent) with MSVC, or pull a community pre-build
- Boost 1.84 — [official download](https://www.boost.org/users/history/version_1_84_0.html), build via `b2`
- OpenSSL — [Win64 OpenSSL pre-builds](https://slproweb.com/products/Win32OpenSSL.html) (use the MSVC-built variant)
- FFmpeg shared — [BtbN's win64 builds](https://github.com/BtbN/FFmpeg-Builds/releases) (`ffmpeg-master-latest-win64-gpl-shared`)
