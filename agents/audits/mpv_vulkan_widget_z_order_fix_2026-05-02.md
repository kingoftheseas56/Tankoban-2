# MPV Vulkan Widget Z-Order Fix

Date: 2026-05-02
Author: Agent 7 (Codex)
Scope: Trigger D implementation for Agent 3, MAKE_MPV_BEAT_FFMPEG Task 2 follow-up

## Root Cause

F1 was a widget hierarchy failure, not a Vulkan/libplacebo rendering failure.

The working ffmpeg surface, `FrameCanvas`, uses exactly these widget attributes:

- `Qt::WA_PaintOnScreen`
- `Qt::WA_NativeWindow`
- `Qt::WA_NoSystemBackground`

See `src/ui/player/FrameCanvas.cpp:30-32`.

`MpvVulkanWidget` used that same native-HWND pattern but added two divergent Qt paint hints:

- `Qt::WA_OpaquePaintEvent`
- a widget stylesheet with `background: #000000`

That combination made Qt treat the full-size native child as an opaque paint island. On Windows, a full-rect opaque native child can cause Qt's parent/backing-store paint to be clipped. The visible symptom is exactly what Hemanth saw after the attempted `lower()`: HUD widgets themselves can paint, but their semi-transparent regions no longer blend over the video/player black; they expose stale content behind the player, in this case the library page.

After Hemanth's post-fix screenshot, one more constraint became clear: even with `WA_OpaquePaintEvent` removed, Qt still cannot safely use a native swapchain child as the alpha backing surface for alien HUD widgets. The ffmpeg path can keep its 0.50 HUD alpha because FrameCanvas is a proven local path, but the mpv/Vulkan path must treat HUD/chrome as opaque UI islands until the renderer architecture changes.

The prior z-order attempt then made the failure worse:

- raising the HUD widgets was directionally right but incomplete because mouse/cursor forwarding was still missing;
- `m_mpvWidget->lower()` pushed the native Vulkan HWND out of the useful underlay position, so transparent HUD areas could show the library instead of the Vulkan surface/player black.

FrameCanvas avoids the bug because it does not set `WA_OpaquePaintEvent`, does not stylesheet the native render child, forwards mouse movement from the native child HWND, and uses `DXGI_ALPHA_MODE_IGNORE` for its swapchain (`src/ui/player/FrameCanvas.cpp:236`). The libplacebo Vulkan wrapper does not expose a DXGI-style composite-alpha switch in `pl_vulkan_swapchain_params`; its exposed swapchain hints include `alpha_bits` only (`C:/tools/libplacebo-msvc/include/libplacebo/vulkan.h:324-365`). In this Task 2 clear-only path we also clear with alpha 1.0 (`src/ui/player/MpvVulkanWidget.cpp:343`), so the observed bleed was from Qt/native child stacking and backing-store clipping, not from transparent Vulkan frames.

## Fix Shape Applied

1. Matched `MpvVulkanWidget` to the working `FrameCanvas` native-widget contract.

   `src/ui/player/MpvVulkanWidget.cpp:63-73`

   - removed `Qt::WA_OpaquePaintEvent`
   - removed the render-child stylesheet background
   - kept `WA_PaintOnScreen`, `WA_NativeWindow`, `WA_NoSystemBackground`
   - added an in-code comment documenting why `WA_OpaquePaintEvent` stays off

2. Added native-child mouse forwarding to the mpv Vulkan surface.

   `src/ui/player/MpvVulkanWidget.h:61-74`
   `src/ui/player/MpvVulkanWidget.cpp:131-134`

   This mirrors `FrameCanvas::mouseActivityAt` (`src/ui/player/FrameCanvas.h:184`, `src/ui/player/FrameCanvas.cpp:405`) so `VideoPlayer::showControls()` can unblank the cursor and reveal the HUD when the mouse moves over the mpv surface.

3. Constructed `MpvVulkanWidget` before HUD widgets.

   `src/ui/player/VideoPlayer.cpp:1347-1359`

   This restores the same construction order as FrameCanvas: render surface first, HUD widgets after. The lazy-create branch remains as a defensive fallback for unusual future construction paths.

4. Removed the failed `m_mpvWidget->lower()` path and kept the explicit HUD raise chain.

   `src/ui/player/VideoPlayer.cpp:4334-4358`

   The Vulkan surface is shown, then HUD/transient widgets are raised. The previous lower call is gone, so the Vulkan HWND stays in the render-surface layer instead of being pushed beneath the player/root backing stack.

5. Added a raw Win32 mouse event path for the Vulkan HWND.

   `src/ui/player/MpvVulkanWidget.h:76`
   `src/ui/player/MpvVulkanWidget.cpp:138-148`

   Qt mouse tracking was not enough on Hemanth's smoke: only keyboard input revealed the HUD. The widget now listens for `WM_MOUSEMOVE` on its own HWND and emits the same `mouseActivityAt` signal as the normal Qt mouse path. That drives `VideoPlayer::showControls()`, which also unblanks the cursor on the mpv surface.

6. Made the mpv HUD backing opaque while preserving ffmpeg's translucent HUD.

   `src/ui/player/VideoPlayer.cpp:1503`
   `src/ui/player/VideoPlayer.cpp:4287`
   `src/ui/player/VideoPlayer.cpp:4339`
   `src/ui/player/VideoPlayer.cpp:4369-4389`

   `applySurfaceOverlayStyle()` now sets `VideoControlBar` to `#0a0a0a` only when the active backend is `MpvBackend`; otherwise it restores the existing ffmpeg style `rgba(10, 10, 10, 0.50)`. The video chrome plate was also made fully opaque (`src/ui/player/VideoPlayer.cpp:1440`) so the main-window chrome cannot show through it.

## Approaches Discarded

- QVulkanWindow plus `createWindowContainer`: not used. It is still a native child-window island and would keep the same overlay-stacking class of problem, with more migration risk.
- Making `VideoPlayer` explicitly native: not used. `WA_NativeWindow` on the child already forces native ancestors on Qt unless `WA_DontCreateNativeAncestors` is set, and the failure matched paint clipping/z-order rather than missing parent HWND creation.
- libplacebo composite-alpha tuning: not used. The MSVC libplacebo header exposes `alpha_bits` as a format preference, not a Win32/DXGI-equivalent alpha-mode override; the clear path already writes alpha 1.0.
- Keeping `lower()`: discarded. It can make HUD widgets visible, but it removes the Vulkan surface from the underlay role and causes transparent HUD regions to expose the library.

## Files Modified

- `src/ui/player/MpvVulkanWidget.h:61-76`
- `src/ui/player/MpvVulkanWidget.cpp:63-73`, `src/ui/player/MpvVulkanWidget.cpp:131-148`
- `src/ui/player/VideoPlayer.cpp:1347-1359`, `src/ui/player/VideoPlayer.cpp:1440`, `src/ui/player/VideoPlayer.cpp:1503`, `src/ui/player/VideoPlayer.cpp:4303-4305`, `src/ui/player/VideoPlayer.cpp:4334-4358`, `src/ui/player/VideoPlayer.cpp:4369-4389`
- `src/ui/player/VideoPlayer.h:288-304`, `src/ui/player/VideoPlayer.h:543-547`

## F1 Sub-Pathology Status

Implementation targets all four reported symptoms:

- HUD transparency to library: addressed by making the mpv control bar fully opaque (`#0a0a0a`) instead of relying on alpha over a native swapchain child.
- Cursor disappears over player: addressed by adding both Qt mouse forwarding and a raw `WM_MOUSEMOVE` native-event path from `MpvVulkanWidget`.
- Chrome duplication: addressed by making the video chrome plate opaque (`#141418`) so the main-window chrome cannot show through the per-view chrome.
- Library bleed: addressed by removing the `lower()` path and making mpv HUD/chrome regions opaque where Qt alpha composition cannot use the Vulkan child as backing.

Visual MCP verification is intentionally pending Agent 3 per the Trigger D handoff. Code-level verification is complete: native sidecar build passed and `cmd /c build_check.bat` returned `BUILD OK`.

## Verification

- `powershell -File native_sidecar/build.ps1`: passed. Produced `ffmpeg_sidecar` and `sidecar_tests` targets, then redeployed `resources/ffmpeg_sidecar/ffmpeg_sidecar.exe`.
- `cmd /c build_check.bat`: passed with `BUILD OK`.
- Follow-up after Hemanth screenshot: `cmd /c build_check.bat` passed with `BUILD OK` after the `WM_MOUSEMOVE` and opaque-HUD changes.
- Follow-up after Hemanth screenshot: `powershell -File native_sidecar/build.ps1` still passed.
