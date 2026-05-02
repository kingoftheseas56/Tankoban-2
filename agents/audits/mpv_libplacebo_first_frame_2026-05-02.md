# mpv libplacebo first frame - 2026-05-02

## Scope

MAKE_MPV_BEAT_FFMPEG Task 3: show one decoded mpv frame through the new libplacebo + Vulkan window. This does not implement continuous playback tuning, subtitles-over-libplacebo, HDR metadata pass-through, or mouse-over-video HUD reveal.

## Render API choice

Chosen path: `MPV_RENDER_API_TYPE_SW`.

Why: Task 3 needs the smallest proof that pixels can flow through mpv -> libplacebo -> Vulkan. The SW API gives a CPU RGB buffer with no GPU-context sharing between mpv and Tankoban's libplacebo/Vulkan swapchain. GPU-zero-copy remains the Task 4+ performance path if SW upload cost is too high.

Important lifecycle detail: mpv's render context must exist before file load. The renderer is therefore owned by `MpvBackend`, not by `MpvVulkanWidget`; the widget only attaches as the Vulkan presenter.

## Frame-readout shape

- `MpvBackend` sets `vo=libmpv` at `src/ui/player/MpvBackend.cpp:212`.
- `MpvBackend` constructs `MpvLibplaceboRenderer` immediately after `mpv_initialize()` and before `ready()` at `src/ui/player/MpvBackend.cpp:342-345`.
- `MpvLibplaceboRenderer::attachMpv()` creates the SW render context and registers `mpv_render_context_set_update_callback()` at `src/ui/player/MpvLibplaceboRenderer.cpp:46-67`.
- The update callback only sets an atomic dirty flag and runs a scheduler lambda; it does not call mpv render APIs from the callback thread (`src/ui/player/MpvLibplaceboRenderer.cpp:213-228`).
- `renderToSwapchain()` calls `mpv_render_context_update()`, then `mpv_render_context_render()` with `SW_SIZE`, `SW_FORMAT="rgb0"`, 64-byte-aligned `SW_STRIDE`, and aligned `SW_POINTER` (`src/ui/player/MpvLibplaceboRenderer.cpp:131-170`).
- `MpvBackend::sendOpen()` calls `resetFrameState()` before each `loadfile`, so a new file cannot start by reusing the previous file's first-frame state (`src/ui/player/MpvBackend.cpp:1164-1166`).

## libplacebo composite shape

- The CPU `rgb0` buffer is uploaded fresh every successful render via `pl_tex_upload()` at `src/ui/player/MpvLibplaceboRenderer.cpp:176-181`.
- The source is a single RGB texture plane with alpha ignored, full-range 8-bit SDR sRGB metadata (`src/ui/player/MpvLibplaceboRenderer.cpp:183-202`).
- The target frame comes from `pl_frame_from_swapchain()`, and the image is composited with `pl_render_image(..., &pl_render_fast_params)` at `src/ui/player/MpvLibplaceboRenderer.cpp:204-208`.
- `MpvVulkanWidget::onRenderTick()` calls the renderer after `pl_swapchain_start_frame`; if rendering returns false, it keeps the Task 2 black clear fallback (`src/ui/player/MpvVulkanWidget.cpp:400-414`).
- `firstFrameRendered()` now fires only after `renderedMpvFrame == true`, not after black clear (`src/ui/player/MpvVulkanWidget.cpp:423-425`).

## Lifecycle and plumbing

- `MpvBackend` owns the renderer via `std::unique_ptr` and exposes a concrete `libplaceboRenderer()` accessor (`src/ui/player/MpvBackend.h:52,168`).
- `MpvBackend::teardownMpv()` emits `mpvHandleInvalidating()`, then resets the renderer before `mpv_terminate_destroy()` (`src/ui/player/MpvBackend.cpp:389-390`).
- `MpvVulkanWidget` stores the renderer as a non-owning pointer and clears scheduler/GPU resources on detach or Vulkan teardown (`src/ui/player/MpvVulkanWidget.cpp:104-126,306-312`).
- `VideoPlayer::syncMpvIntegrationToBackend()` passes the backend renderer into the Vulkan widget on ready/immediate attach and clears it on backend invalidation or ffmpeg swap-away (`src/ui/player/VideoPlayer.cpp:4287-4350`).
- The disabled `mouseMoveEvent`, `nativeEvent`, and `mouseActivityAt -> showControls()` paths remain disabled.

## Files modified

- `CMakeLists.txt:531-532` - adds `MpvLibplaceboRenderer` to the libmpv source group.
- `src/ui/player/MpvLibplaceboRenderer.h:18-64` - new renderer bridge API and state.
- `src/ui/player/MpvLibplaceboRenderer.cpp:46-302` - SW context, update callback, CPU buffer, texture upload, and libplacebo composite.
- `src/ui/player/MpvBackend.h:52,168` - renderer accessor and backend ownership.
- `src/ui/player/MpvBackend.cpp:212,342-345,389-390,1164-1166` - `vo=libmpv`, renderer lifecycle, reset on open.
- `src/ui/player/MpvVulkanWidget.h:43,91` - renderer attachment API and non-owning pointer.
- `src/ui/player/MpvVulkanWidget.cpp:104-126,306-312,400-425` - scheduler attach, GPU detach, render-or-clear tick, real-frame first signal.
- `src/ui/player/VideoPlayer.cpp:1353-1358,4287-4350` - renderer plumbing and first-frame comment update.
- `agents/audits/evidence_mpv_libplacebo_first_frame_2026-05-02.png` - paused-frame screenshot evidence.

## Verification evidence

- `cmd /c build_check.bat` - `BUILD OK`.
- `powershell -File native_sidecar/build.ps1` - succeeded; produced `ffmpeg_sidecar` and `sidecar_tests` targets.
- Launched `out/Tankoban.exe --dev-control`; process survived more than 30 seconds before playback.
- `out/tankoctl.exe ping` returned schema `tankoban.dev.v1`.
- `out/tankoctl.exe get-state` returned `windowVisible:true`.
- `out/tankoctl.exe play-file "C:\Users\Suprabha\Desktop\Hemanth's Folder\Community Season 1  [1080p x265 10bit FS89 Joy]\Community S01E01 Pilot  [1080p x265 10bit Joy].mkv"` returned `opened:true`.
- Because the library resume state was near the end of S01E01, playback auto-advanced through the season and the final paused visual screenshot was captured on Community S01E25. The screenshot is still the same mpv/libplacebo/Vulkan pipeline and shows a non-black paused frame with roughly correct color and sizing.
- `out/tankoctl.exe get-player` after click + Space returned `paused:true` and `firstFrameSeen:true`.
- Screenshot saved at `agents/audits/evidence_mpv_libplacebo_first_frame_2026-05-02.png`.

## Code-walk proof points

- Fresh upload: every successful `renderToSwapchain()` calls `mpv_render_context_render()` and then `pl_tex_upload()` before `pl_render_image()` (`src/ui/player/MpvLibplaceboRenderer.cpp:163-208`).
- No-frame fallback: no context, no GPU, no swapchain FBO, no first frame, render failure, upload failure, or composite failure returns `false`; `MpvVulkanWidget` then clears black (`src/ui/player/MpvLibplaceboRenderer.cpp:137-181,205-207`; `src/ui/player/MpvVulkanWidget.cpp:410-414`).
- No mouse regression: the Task 2 crash hotfix remains commented out; no native mouse bridge was re-enabled.

READY TO COMMIT - [Agent 3 (Codex), MAKE_MPV_BEAT_FFMPEG Task 3 - first frame through libplacebo]: add backend-owned mpv SW render bridge and composite uploaded frames into the Vulkan/libplacebo swapchain with black fallback | Skills invoked: [] | files: CMakeLists.txt, src/ui/player/MpvLibplaceboRenderer.h, src/ui/player/MpvLibplaceboRenderer.cpp, src/ui/player/MpvBackend.h, src/ui/player/MpvBackend.cpp, src/ui/player/MpvVulkanWidget.h, src/ui/player/MpvVulkanWidget.cpp, src/ui/player/VideoPlayer.cpp, agents/audits/mpv_libplacebo_first_frame_2026-05-02.md, agents/audits/evidence_mpv_libplacebo_first_frame_2026-05-02.png, agents/chat.md
