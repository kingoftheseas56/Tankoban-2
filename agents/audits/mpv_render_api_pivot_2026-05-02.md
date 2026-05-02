# mpv Render API Pivot - 2026-05-02

## Decision Trail

Task 3's SW render API path proved the pixel route but did CPU readout, CPU memcpy upload, and Vulkan composite on the GUI thread. Continuous playback made the UI responsive problem visible, so Task 3.5 pivots to mpv's GPU render API.

Probe result:
- `MPV_RENDER_API_TYPE_VULKAN`: rejected. The bundled headers do not expose `render_vulkan.h` / `MPV_RENDER_API_TYPE_VULKAN`, and the runtime probe returned `operation not implemented`.
- `MPV_RENDER_API_TYPE_OPENGL`: chosen. The runtime probe returned `invalid parameter` when called without `mpv_opengl_init_params`, which means libmpv recognizes OpenGL rendering.
- Direct libplacebo OpenGL: rejected. `C:\tools\libplacebo-msvc\include\libplacebo\config.h` has Vulkan enabled and OpenGL disabled.
- Threaded SW: rejected for this task. The Intel UHD 620 WGL probe reported `GL_EXT_memory_object`, `GL_EXT_memory_object_win32`, `GL_EXT_semaphore`, and `GL_EXT_semaphore_win32`, so the requested GPU interop path is available.

## Implementation

`MpvLibplaceboRenderer` now owns a dedicated render thread. That thread creates a `QOpenGLContext` + `QOffscreenSurface`, creates `mpv_render_context` with `MPV_RENDER_API_TYPE_OPENGL`, registers mpv's update callback, and runs all `mpv_render_context_update()` / `mpv_render_context_render()` calls on that same thread.

The GUI thread still owns the Qt widget, libplacebo Vulkan swapchain, and present path. It never calls mpv render APIs. It only composites the latest GPU-resident texture into the current swapchain frame.

Texture sharing:
- The GUI/Vulkan side allocates a 3-slot ring of libplacebo textures exported as `PL_HANDLE_WIN32`.
- Each slot also has two exported Vulkan semaphores: Vulkan-to-GL and GL-to-Vulkan.
- The GL render thread duplicates and imports those Win32 handles via `glImportMemoryWin32HandleEXT` and `glImportSemaphoreWin32HandleEXT`.
- mpv renders into a GL FBO backed by the imported Vulkan texture.
- The GUI thread releases the slot back to libplacebo with `pl_vulkan_release_ex`, composites it with `pl_render_image`, presents, then marks it as the displayed frame.
- The displayed slot is retained and re-composited on 60 Hz ticks until a newer decoded frame arrives. This fixed the black flashing caused by clearing between mpv update callbacks.

Frame orientation:
- The first interop attempt used a libplacebo plane flip and Hemanth reported upside-down video. The final path sets `src.planes[0].flipped = false`; Hemanth verified the image is now upright.

## Lifecycle

- `MpvBackend` still owns `MpvLibplaceboRenderer`.
- Renderer thread/context creation still completes during mpv initialization before `ready()`.
- `mpvHandleInvalidating()` still clears the widget's non-owning renderer pointer before backend teardown.
- `MpvVulkanWidget` attaches the renderer with `setLibplaceboRenderer()`. Its scheduler now means "a GPU texture is ready for GUI composite", not "do mpv readout on the GUI thread".
- `detachGpu()` destroys GL imports on the render thread before destroying Vulkan textures/semaphores on the GUI side.
- Pre-first-frame behavior is unchanged: `MpvVulkanWidget` clears black until a real mpv-rendered texture is available.

## Files Modified

- `src/ui/player/MpvLibplaceboRenderer.h:13` - public renderer bridge, including `finishPresentedFrame()`.
- `src/ui/player/MpvLibplaceboRenderer.cpp:193` - render-thread state, slot state machine, GL/Vulkan interop objects.
- `src/ui/player/MpvLibplaceboRenderer.cpp:268` - render thread setup, OpenGL render-context creation, mpv callback registration.
- `src/ui/player/MpvLibplaceboRenderer.cpp:733` - GUI-side `renderToSwapchain()` imports/allocates the interop ring, consumes ready textures, and reuses the displayed frame between updates.
- `src/ui/player/MpvLibplaceboRenderer.cpp:958` - `finishPresentedFrame()` retires the previous displayed slot only after the new frame has been presented.
- `src/ui/player/MpvVulkanWidget.cpp:104` - renderer scheduler remains queued to the GUI thread but no longer runs mpv render/readout work there.
- `src/ui/player/MpvVulkanWidget.cpp:397` - presenter calls `renderToSwapchain()` and black-falls back only when no mpv frame has ever been displayed.
- `src/ui/player/MpvVulkanWidget.cpp:415` - presenter calls `finishPresentedFrame()` after submit/swap so GL cannot overwrite a texture while libplacebo may sample it.
- Existing Task 3 plumbing retained: `src/ui/player/MpvBackend.cpp:342`, `src/ui/player/MpvBackend.h:52`, `src/ui/player/VideoPlayer.cpp:4336`, and `CMakeLists.txt:531`.

## Verification

- `cmd /c build_check.bat`: `BUILD OK`.
- `powershell -File native_sidecar/build.ps1`: succeeded; rebuilt `ffmpeg_sidecar.exe` and `sidecar_tests.exe`.
- Runtime:
  - Launched `out\Tankoban.exe --dev-control`.
  - `out\tankoctl.exe ping`: schema `tankoban.dev.v1`.
  - `out\tankoctl.exe get-state`: `windowVisible:true`, `videoPlayerVisible:true`.
  - `out\tankoctl.exe play-file "C:\Users\Suprabha\Desktop\Hemanth's Folder\Community Season 1  [1080p x265 10bit FS89 Joy]\Community S01E01 Pilot  [1080p x265 10bit Joy].mkv"`: `opened:true`.
  - `out\tankoctl.exe get-player`: `firstFrameSeen:true`, `paused:false`, playback position advancing.
  - Debug logs showed `OpenGL render context ready on dedicated render thread`, `OpenGL/Vulkan shared texture ring ready: 3 interopSlots 1920x1008`, successful GL mpv renders, and Vulkan composites.
- MCP visual evidence: `agents/audits/evidence_mpv_render_api_pivot_2026-05-02.png` shows an upright non-black Community frame through the mpv/OpenGL-to-libplacebo/Vulkan path.
- Snappiness smoke: Hemanth live-tested the build after the pivot and reported the UI is responsive. After the final frame-retention fix, Hemanth reported: "it works perfectly, thank you."

READY TO COMMIT - [Agent 3 (Codex), MAKE_MPV_BEAT_FFMPEG Task 3.5 - pivot frame path off GUI thread]: mpv frames now render on a dedicated OpenGL thread into Win32-shared Vulkan textures, with GUI thread limited to libplacebo composite/present | Skills invoked: [] | files: src/ui/player/MpvLibplaceboRenderer.h, src/ui/player/MpvLibplaceboRenderer.cpp, src/ui/player/MpvVulkanWidget.cpp, agents/audits/mpv_render_api_pivot_2026-05-02.md
