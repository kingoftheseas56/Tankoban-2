# Build Prep: Main-App libplacebo/Vulkan for MSVC

Date: 2026-05-02
Agent: Agent 7 (Codex)
Requesting arc: Agent 3, MAKE_MPV_BEAT_FFMPEG Task 1 build prep

## Installed Paths

- Vulkan SDK: `C:/VulkanSDK/1.4.341.1`
  - Headers verified at `C:/VulkanSDK/1.4.341.1/Include/vulkan/vulkan.h`
  - MSVC import library verified at `C:/VulkanSDK/1.4.341.1/Lib/vulkan-1.lib`
  - `VULKAN_SDK` user environment variable set to `C:\VulkanSDK\1.4.341.1`
- libplacebo MSVC-compatible install: `C:/tools/libplacebo-msvc`
  - DLL: `C:/tools/libplacebo-msvc/bin/libplacebo-362.dll`
  - Import library: `C:/tools/libplacebo-msvc/lib/libplacebo.lib`
  - Headers: `C:/tools/libplacebo-msvc/include/libplacebo/*.h`

## Versions

- Vulkan SDK: `1.4.341.1`
- Vulkan SDK glslangValidator: `glslang Khronos 16.2.0`
- libplacebo source checkout: `C:/tools/libplacebo-src`
- libplacebo commit SHA: `33b5dfada6a84692912e4d41f673f895df79479e`
- libplacebo project version: `7.362.0`
- LLVM/clang-cl installed via winget: `22.1.4`, target `x86_64-pc-windows-msvc`
- vcpkg dependency installs:
  - `lcms:x64-windows 2.18`
  - `pkgconf:x64-windows 2.5.1#4`

## Meson Configure

Final build directory: `C:/tools/libplacebo-src/build-msvc`

Final configure command, run after VS2022 `vcvarsall.bat x64` with `CC` and `CXX` set to `C:\Program Files\LLVM\bin\clang-cl.exe`:

```bat
meson setup build-msvc --wipe --vsenv --buildtype=release --default-library=shared -Dc_args= -Dvulkan=enabled -Dvk-proc-addr=enabled -Dlcms=enabled -Dglslang=enabled -Dshaderc=disabled -Dopengl=disabled -Dd3d11=disabled -Ddemos=false -Dtests=false -Dvulkan-sdk=C:/VulkanSDK/1.4.341.1 --prefix=C:/tools/libplacebo-msvc
meson compile -C build-msvc
meson install -C build-msvc
```

Relevant environment:

```bat
set VULKAN_SDK=C:\VulkanSDK\1.4.341.1
set VCPKG_ROOT=C:\vcpkg
set PKG_CONFIG=C:\vcpkg\installed\x64-windows\tools\pkgconf\pkgconf.exe
set PKG_CONFIG_PATH=C:\vcpkg\installed\x64-windows\lib\pkgconfig;C:\vcpkg\installed\x64-windows\share\pkgconfig
set CMAKE_PREFIX_PATH=C:\VulkanSDK\1.4.341.1;C:\vcpkg\installed\x64-windows
```

## CMake Changes

Changed `CMakeLists.txt`:

- Line 95: added `src/ui/player/MpvLibplaceboBuildProbe.cpp` to the main `SOURCES` list.
- Lines 378-414: added main-app-only Vulkan/libplacebo discovery and link block:
  - `find_package(Vulkan REQUIRED)`
  - `LIBPLACEBO_MSVC_ROOT` cache path defaulting to `C:/tools/libplacebo-msvc`
  - `find_path` for `libplacebo/log.h` and `libplacebo/renderer.h`
  - `find_library` for `libplacebo.lib`
  - `find_file` for `libplacebo-362.dll` and `lcms2-2.dll`
  - `HAS_LIBPLACEBO_MAIN=1`
  - `target_link_libraries(Tankoban PRIVATE Vulkan::Vulkan "${LIBPLACEBO_MAIN_LIBRARY}")`
  - post-build copy for `libplacebo-362.dll` and `lcms2-2.dll`

Added `src/ui/player/MpvLibplaceboBuildProbe.cpp`:

- Includes `<libplacebo/log.h>` and `<vulkan/vulkan.h>`.
- Exports `tankoban_mpv_libplacebo_build_probe()`.
- References `pl_log_create` and `vkGetInstanceProcAddr` so both import libraries must resolve during the main app link.

## Gotchas

- `Invoke-WebRequest` timed out while downloading the Vulkan SDK installer. `curl.exe` completed the same download successfully.
- `vcpkg install` initially ran in repo manifest mode. Running `C:\vcpkg\vcpkg.exe install --classic lcms:x64-windows pkgconf:x64-windows` outside the repo installed the intended global packages.
- Plain `cl.exe` cannot compile this libplacebo commit without a larger upstream portability port. It fails on GNU C extensions used throughout libplacebo internals and generated sources (`__typeof__`, `__attribute__`, and `__builtin_*`).
- `clang-cl` under the VS2022 x64 environment was used instead. This produces MSVC ABI/import-library outputs while accepting the GNU C extensions libplacebo uses.
- Two small patches were required in the external `C:/tools/libplacebo-src` checkout:
  - `meson.build`: gate GCC warning/math flags when the compiler driver uses MSVC argument syntax.
  - `src/glsl/meson.build`: pass the Vulkan SDK library directory to `glslang` lookup and link `SPIRV-Tools` plus `SPIRV-Tools-opt`, because SDK `glslang.lib` depends on them.
- `build_check.bat` was LF-only in the working tree, and `cmd.exe` parsed its comment text as commands before the build. The file was mechanically normalized to CRLF so `cmd /c build_check.bat` runs as intended.
- `native_sidecar/CMakeLists.txt` was not edited; it still points to `C:/tools/libplacebo` and `C:/tools/vulkan`.

## Verification

- `powershell -NoProfile -ExecutionPolicy Bypass -File native_sidecar/build.ps1`: passed.
  - Produced `resources/ffmpeg_sidecar/ffmpeg_sidecar.exe`.
  - Produced `native_sidecar/build/tests/sidecar_tests.exe`.
- `cmd /c build_check.bat`: passed with `BUILD OK`.
  - Produced `out/Tankoban.exe`.
  - Post-build copied `out/libplacebo-362.dll` and `out/lcms2-2.dll`.
  - `out/_build_check.log` contains no `unresolved external`, `LNK2019`, or `LNK1120` diagnostics for `pl_log_create` or `vkGetInstanceProcAddr`.

## Sources

- LunarG Windows release notes for SDK `1.4.341.1` and VS2022 support: https://vulkan.lunarg.com/doc/view/latest/windows/release_notes.html
- LunarG Windows getting started for install path and environment behavior: https://vulkan.lunarg.com/doc/view/latest/windows/getting_started.html
