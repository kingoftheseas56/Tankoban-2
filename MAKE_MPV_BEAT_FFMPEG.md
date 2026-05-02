# Make MPV Beat FFmpeg

## Plan-specific rules (Hemanth-quoted, verbatim)

> I need the mpv to be equal to ffmpeg in all regards NOW, and later I want the mpv to surpass, but that's for later.

> The new md file should be simple too, like task 1, task 2, the same way make mpv solo, no overcomplicated or overstuffed phases, just simpler focused managable and testable tasks.

> Don't ask Hemanth questions or give him options unless it concerns the user-facing side of the app, meaning how it affects the experience of using the app.

> Talk to me in non-coder terms and simpler language so I can better follow instructions if the agent needs me to test something.

*Agent-facing translation: simple flat numbered tasks (no phase clusters), each focused and testable. Plain-English smoke instructions for Hemanth. Technical decisions are the agent's call (Rule 14); only product/UX decisions reach Hemanth. Each task ships in one wake or fewer; if a task would take more than ~600 LOC, split it.*

---

## Why this arc exists

Today's mpv playback is smooth on light files but the picture is softer than ffmpeg's. Yesterday's diagnostic chain (Task 10.5 → 10.7 → 12.A → 12.B) landed honest data: mpv's renderer uses **OpenGL** which has roughly half the GPU shader budget of ffmpeg's renderer (**libplacebo on Vulkan**) on Intel UHD 620. Sharp scalers on mpv tip into stutter on heavy content (Sopranos S06E04: 6–8 dropped frames per second); the only mpv-side fix that plays smooth is bilinear, which loses sharpness vs ffmpeg.

The cure is replacing mpv's renderer with libplacebo+Vulkan — the SAME renderer ffmpeg already uses today via `native_sidecar/src/gpu_renderer.cpp`. After this arc, mpv inherits ffmpeg's shader budget AND picture quality, AND mpv's playback-engine advantages (faster seeking, native subtitle handling, better A/V sync, HDR metadata pass-through) layer on top — that's the "surpass ffmpeg later" slope, which begins after Task 9 closes.

---

## General direction

Destination: mpv playback that **looks identical to or better than the ffmpeg sidecar's libplacebo path** on Hemanth's Intel UHD 620, on both light content (Community S01E01 SDR) and heavy content (Sopranos S06E04 1080p HEVC 10-bit / Boys S03E01 HDR).

Architectural change: mpv keeps decoding video + handling audio + subtitles + transport + property observation. mpv's OpenGL renderer is replaced by a libplacebo+Vulkan renderer that draws to a Vulkan-backed window. The libplacebo integration already lives at `native_sidecar/src/gpu_renderer.cpp` (used by the ffmpeg sidecar today); we reuse it.

Holding pattern during this arc: bilinear scalers (Task 12.B diagnostic ship) stay live so daily mpv use remains smooth. Once Task 5 of this arc lands, libplacebo's own scaler config takes over and the mpv-side scaler properties become irrelevant.

No phase labels, no clusters — flat numbered tasks below.

---

## Tasks

- [ ] **1. Survey what we already have, lock the integration shape.**
  - **What this involves (plain English):** agent reads the existing libplacebo code in `native_sidecar/src/gpu_renderer.cpp` end-to-end + mpv's render API headers + a working reference integration (Iina or Celluloid), then writes down a one-page summary of the pipeline shape: how mpv hands frames out, what format they're in, how libplacebo accepts them, how the Vulkan swapchain presents them. No code yet — paper architecture.
  - **What Hemanth does for the smoke test:** nothing. Agent task. Hemanth reads the one-pager and confirms it sounds sensible at a high level (don't worry about the technical specifics — just "does this read like a coherent plan?").
  - **Goal:** lock the technical shape before writing any code so we don't paint ourselves into a corner mid-implementation.
  - **What success looks like:** a one-page architecture summary lives in `agents/audits/mpv_libplacebo_integration_2026-05-02.md` (or similar). Hemanth gives it a green/yellow/red read.
  - **Files in scope:** read-only — `native_sidecar/src/gpu_renderer.{h,cpp}`, `src/ui/player/MpvVideoWidget.{h,cpp}`, libmpv's `<mpv/render.h>`. Output: one new audit file.
  - **Smoke owner:** Hemanth (one-pager read).

- [ ] **2. Stand up an empty Vulkan window where mpv used to draw.**
  - **What this involves (plain English):** the part of the player that shows your video is currently a Qt OpenGL widget. Replace that with a Qt Vulkan-capable window. No video appears yet — just a black panel where the picture used to be. This proves the new graphics highway is alive.
  - **What Hemanth does for the smoke test:** open Tankoban. Start any mpv-backed file (right-click → Play with mpv). Confirm the player UI loads (HUD shows up, controls work, popovers open, audio plays). The video area itself will be black — that's expected. Press Esc to close. If everything else still works, this task passes.
  - **Goal:** prove the Vulkan window mounts in place of the OpenGL one without breaking anything else in the player.
  - **What success looks like:** Tankoban launches normally. Audio plays. HUD/popovers/keyboard shortcuts/seekbar all work. Video area is black. No crash. Hemanth confirms the player UI feels normal.
  - **Files in scope:** NEW `src/ui/player/MpvVulkanWindow.{h,cpp}` (or whichever name fits); MODIFIED `src/ui/player/VideoPlayer.cpp` (widget plumbing); MODIFIED `CMakeLists.txt` (Vulkan loader link).
  - **Smoke owner:** Hemanth.

- [ ] **3. Show one frame on the Vulkan window through libplacebo.**
  - **What this involves (plain English):** open a video, pause it. The agent wires up the smallest possible frame path — get one decoded frame from mpv, hand it to libplacebo, libplacebo draws it on the Vulkan window. Just one static picture. No animation yet.
  - **What Hemanth does for the smoke test:** open a video, pause it within the first few seconds (press Space). The paused frame should appear on screen. Picture might look plain (no fancy scaling yet, no color correction yet). The point is: pixels are flowing through the new pipeline.
  - **Goal:** prove the frame path works end-to-end — mpv decoder → frame bytes → libplacebo → Vulkan → screen.
  - **What success looks like:** paused frame visible on screen, roughly right colors, roughly right size. Quality may be plain. No crash on pause/resume cycle.
  - **Files in scope:** NEW `src/ui/player/MpvLibplaceboRenderer.{h,cpp}`; MODIFIED `src/ui/player/MpvBackend.cpp` (frame readout path).
  - **Smoke owner:** Hemanth.

- [ ] **4. Continuous playback through the new pipeline.**
  - **What this involves (plain English):** frames now flow continuously at the source rate (24 fps for most films, 30 for some shows, 60 for sports). Light content (Community S01E01 SDR) plays through libplacebo without dropping frames. The new render path is now THE path.
  - **What Hemanth does for the smoke test:** play Community S01E01 for ~2 minutes on mpv. Picture should move smoothly, audio should sync. No stutter, no black frames, no judder. Close cleanly.
  - **Goal:** prove the steady-state pipeline holds at full frame rate for a normal-bitrate file.
  - **What success looks like:** smooth playback for 2 full minutes. Telemetry (`out/mpv_telemetry.log` last session block) shows ~0.10–0.24 drops/sec on Community SDR — same floor as Task 10.5 baseline. No regression vs current bilinear ship.
  - **Files in scope:** same as Task 3, plus throttling/timing logic in `MpvLibplaceboRenderer.cpp`.
  - **Smoke owner:** Hemanth + agent (telemetry).

- [ ] **5. Match ffmpeg's picture quality on heavy content.**
  - **What this involves (plain English):** turn on libplacebo's high-quality scalers — the same `ewa_lanczossharp` upscaler + `hermite` downscaler the ffmpeg sidecar uses today (`native_sidecar/src/gpu_renderer.cpp:110-111`). With Vulkan handling the work instead of OpenGL, the GPU should fit these scalers comfortably. Sopranos S06E04 should now play smoothly AND look as sharp as ffmpeg.
  - **What Hemanth does for the smoke test:** play Sopranos S06E04 on mpv for ~2 minutes. Eyeball-compare to ffmpeg playback of the same file (right-click "Play with ffmpeg" on the same episode for ~30 seconds). Verdict: matches ffmpeg / softer / sharper. We need "matches" or "sharper."
  - **Goal:** reach the picture-quality bar that started this whole arc — equal to ffmpeg on heavy content.
  - **What success looks like:** Hemanth says "same as ffmpeg" or "sharp as ffmpeg" or "actually sharper." Telemetry on Sopranos shows drops near floor (under 0.5/sec) — confirming smooth playback under high-quality scalers.
  - **Files in scope:** `MpvLibplaceboRenderer.cpp` (scaler config matching gpu_renderer.cpp:110-111).
  - **Smoke owner:** Hemanth.

- [ ] **6. HDR films render correctly.**
  - **What this involves (plain English):** HDR color metadata (the gamma curve `pq` for HDR10, `hlg` for HLG broadcast, color primaries `bt.2020`) flows from mpv's decode through libplacebo's tone-mapping path the same way it flows on the ffmpeg side today. Boys S03E01 (HDR) on mpv should look like Boys S03E01 on ffmpeg — same skin tones, same highlight roll-off, same shadow detail.
  - **What Hemanth does for the smoke test:** play Boys S03E01 on mpv for ~90 seconds. Eyeball-compare to ffmpeg playback (right-click "Play with ffmpeg"). Verdict.
  - **Goal:** HDR parity with ffmpeg's HDR.
  - **What success looks like:** Hemanth confirms HDR films look right on mpv (skin tones, highlights, shadows match ffmpeg). Same-shape verdict as MAKE_MPV_SOLO Task 4 close ("yeah it looks good as can be so green").
  - **Files in scope:** `MpvLibplaceboRenderer.cpp` (HDR pass-through to libplacebo's tone-mapping); `MpvBackend.cpp` (HDR metadata bridge if libplacebo can't read it directly from the frames).
  - **Smoke owner:** Hemanth.

- [ ] **7. Subtitles render on top of the new pipeline.**
  - **What this involves (plain English):** mpv produces subtitles as graphics that need to be drawn on top of the video. Three kinds: anime ASS karaoke (libass-rendered), standard SRT (libass-rendered, simpler), and Blu-ray PGS (bitmap subtitles). Currently mpv's old renderer composited these for us. With the new renderer, libplacebo handles the compositing — agent wires mpv's subtitle output (libass surface + PGS bitmaps) into libplacebo as overlay textures.
  - **What Hemanth does for the smoke test:** test all three subtitle types: (a) play an anime file with ASS subs (Vinland S02E01 or Apothecary S02E01) — confirm karaoke effects animate, font rendering looks right; (b) play Sopranos with English PGS subs — confirm subtitles appear at the right position; (c) play any Western show with English SRT — confirm timing matches dialogue. Also confirm the existing "Subtitle position" slider in the Settings popover still moves subtitles up/down, and the size slider still resizes them.
  - **Goal:** subtitle parity with current mpv behavior + ffmpeg behavior.
  - **What success looks like:** all three subtitle types render correctly on the new renderer. Position + size sliders still work.
  - **Files in scope:** `MpvLibplaceboRenderer.cpp` (subtitle compositing layer); possibly `src/ui/player/SubtitleOverlay.cpp` (if mpv subs need bridging through the existing overlay surface).
  - **Smoke owner:** Hemanth.

- [ ] **8. Edge-case sweep — nothing else broke.**
  - **What this involves (plain English):** the renderer change is invasive and has lots of surfaces around it. Run through every interaction the player supports and confirm none of them broke: fullscreen toggle (F key + double-click on video), resize the window mid-playback, take a snapshot (Ctrl+S), press Z/X/C to change speed, switch audio device while playing (Bluetooth headphones plug/unplug — Task 8.B path), move the brightness slider (Task 9), open every popover (Subtitles / Audio / Brightness / Settings / Playlist), seek with arrow keys, scrub the seekbar, right-click "Play with ffmpeg" emergency revert.
  - **What Hemanth does for the smoke test:** run through each interaction once. Anything that misbehaves goes in a quick numbered list (e.g. "1. fullscreen freezes when toggling back; 2. snapshot saves a black image"). Agent triages each one.
  - **Goal:** regression-sweep. The renderer change shouldn't quietly break shipping behavior elsewhere.
  - **What success looks like:** every interaction works the same as before the renderer swap.
  - **Files in scope:** whatever surfaces have regressions; likely small fixes to `MpvBackend.cpp` / `MpvLibplaceboRenderer.cpp` / `VideoPlayer.cpp`.
  - **Smoke owner:** Hemanth (run through the matrix); agent (triage each finding one-at-a-time).

- [ ] **9. Remove the dead OpenGL renderer code.**
  - **What this involves (plain English):** once the new pipeline is solid across Tasks 4–8, delete the old QOpenGLWidget code that mpv used to draw into. Tankoban is now mpv-on-libplacebo end-to-end. Build is smaller, future maintainers don't get confused about which path is live.
  - **What Hemanth does for the smoke test:** nothing — agent code change. After it lands, do one final regression sweep: open a few different files (light SDR, heavy SDR, HDR, anime), confirm nothing broke that worked at Task 8 close.
  - **Goal:** remove the dead code and lock the architecture.
  - **What success looks like:** build still green; mpv playback unchanged from Task 8 close; the prior `MpvVideoWidget` QOpenGLWidget code is gone (or rewritten as a thin compatibility wrapper if anything references it externally).
  - **Files in scope:** `src/ui/player/MpvVideoWidget.{h,cpp}` (deleted or thin-wrapped); `CMakeLists.txt` pruning; `src/ui/player/VideoPlayer.cpp` (clean up any OpenGL-widget-specific branches).
  - **Smoke owner:** Hemanth (final regression sweep).

---

_Plan archive-ready after Task 9 closes. Surpass-ffmpeg work — better-than-ffmpeg picture quality (debanding, sigmoid upscaling, ICC profile management, custom shader pipelines libplacebo offers) — belongs in a future arc once parity is locked here. The "later I want the mpv to surpass" goal is real but explicitly out of scope of this arc; see the parent MAKE_MPV_SOLO arc + future MAKE_MPV_SURPASS or similar for that work._
