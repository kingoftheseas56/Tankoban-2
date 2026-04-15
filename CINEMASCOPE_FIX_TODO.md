# Cinemascope Fix TODO

**Owner:** Agent 3 (Video Player). **Created:** 2026-04-15.

## Context

Both bugs surface only on non-16:9 source content (cinemascope 2.39:1, e.g. The Boys S03 AMZN WEB-DL x265 at 1920×804). Standard 1920×1080 (Sopranos, One Piece anime) works correctly. A full session of one-line patches didn't land — these are deeper than symptom-fixes. Writing them down so we don't re-run the same loop next session.

Reference work already shipped:
- Fix 1 (sidecar): `video_decoder.cpp` gates GPU→GPU copy on `!sub_blend_needed`. Confirmed firing via `[sub-path-diag]` log — `sub_blend_needed=1, zero_copy_active=0, fmt_is_hw=1` for The Boys. Not the full fix.
- Fix 2 (main-app): removed dead `subtitleText → QLabel` connect. Cosmetic.
- Fix 3 (main-app): aspect diagnostic log. Writes to `_player_debug.txt` per (frame-dim × widget-dim) change. Useful for the aspect bug.
- `m_forcedAspect = 0.0` reset on openFile + checkable Aspect Ratio radio group. Shipped.
- `m_tracksRestored` latch on `restoreTrackPreferences`. Shipped.

Agent 7 audit: `agents/audits/video_player_subs_aspect_2026-04-15.md`. Confirms architectural split as root cause of subtitle bug.

## Bug 1 — Subtitles invisible on cinemascope content

**Symptom:** Subtitle tracks show in menus, `set_tracks` + `set_sub_visibility` commands flow correctly, sidecar's `[sub-path-diag]` confirms `sub_blend_needed=1` with Fix 1 firing. But no subtitles render on screen for 1920×804 content. Same codec (HEVC), same track selection flow, same main-app path — works on 1920×1080, fails on 1920×804.

**Hypothesis (high confidence):** `D3D11Presenter::present_cpu` (`native_sidecar/src/d3d11_presenter.cpp:96-109`) calls `resize(width, height)` when incoming CPU frame dims differ from the shared texture's current dims. Resize calls `release_textures()` + `create_textures()` which creates a NEW shared texture with a NEW `nt_handle_`. The `d3d11_texture` event is emitted to the main-app exactly once, at `video_decoder.cpp:806` inside the `!first_frame_fired` guard. No re-emission on subsequent resizes. Main-app's `m_importedD3DTex` is opened from the OLD, now-closed handle. Subsequent `present_cpu` uploads land in the new texture that main-app can't see.

Trigger on cinemascope: HW-decoded `raw_frame->width/height` may differ from `codecpar->width/height` that the presenter was initialized with (crop handling asymmetry between probe and HW decoder output). `present_cpu` sees the mismatch on first blend-inclusive frame and triggers resize. From that point on, the shared texture main-app displays is stale while the blended-subs texture is the new one.

### Fix option A (narrow, fast)

Re-emit `d3d11_texture` event on every successful `resize()`. Main-app's `attachD3D11Texture` handler re-opens the shared handle and swaps `m_importedSrv`.

**Files:**
- `native_sidecar/src/d3d11_presenter.cpp` — track the resize through a signal/callback (or expose a "handle_changed" flag the video_decoder polls each frame).
- `native_sidecar/src/video_decoder.cpp` — check `d3d_presenter->handle_changed()` per frame; if true, re-emit `d3d11_texture` via `on_event_`.
- `src/ui/player/FrameCanvas.cpp` — `attachD3D11Texture` already re-imports; verify no leaks when the main-app swaps handles mid-playback.

**Success:** The Boys plays with subs visible. Sopranos still works (no regression). Stats badge shows `drops=0` steady-state (no extra CPU cost on non-cinemascope).

**Scope:** ~20-30 LOC sidecar + ~5 LOC main-app safety on handle swap.

### Fix option B (structural, Agent 7's recommendation)

Port QMPlay2's pattern: render libass `ASS_Image` output as a GPU draw pass onto the shared D3D texture BEFORE publishing, instead of CPU-blending into a separate buffer. Eliminates the dual-path split entirely. Same rendering model mpv, IINA, QMPlay2 all use.

**Files:**
- `native_sidecar/src/subtitle_renderer.*` — expose ASS_Image list per frame (already computed in `render_blend`; add a variant that returns it without blending).
- `native_sidecar/src/d3d11_presenter.*` — new `present_with_osd(texture, ass_images)` that does `CopyResource` HW frame → shared texture, then a second draw pass with libass bitmaps as textured quads onto the shared texture RTV.
- `native_sidecar/src/video_decoder.cpp` — route cinemascope (and eventually all) frames through the new OSD-aware present.

**Success:** all content (any aspect, HW or SW decode) shows subs identically. No CPU blend path. Matches reference players.

**Scope:** ~200-300 LOC sidecar. New shaders for OSD draw pass (minimal — textured quad with alpha blend).

**Recommended ordering:** ship Option A first (fastest cinemascope subs fix, validates the resize-handle hypothesis). If it works, Option B becomes optional cleanup. If Option A reveals additional complications, pivot straight to Option B.

## Bug 2 — Fullscreen not actually entering fullscreen

**Symptom:** User presses F or double-clicks. Aspect diagnostic log shows `widget=1920x974` consistently — same value before and after fullscreen toggle. `974 physical = 649 logical @ DPR=1.5`, where `monitor_height_logical - Windows_taskbar(40) - Qt_titlebar(31) ≈ 649`. Window is in **maximized** state, not true fullscreen.

**Hypothesis:** `MainWindow::showFullScreen()` is being called (per [MainWindow.cpp:131](src/ui/MainWindow.cpp#L131)), but either:
- It doesn't produce a borderless monitor-filling window (Qt::WindowFullScreen flag not getting set on the underlying HWND), OR
- The MainWindow top-bar (`m_topBar`, fixed height 56) isn't being hidden during fullscreen, OR
- The `VideoPlayer::setGeometry(centralWidget()->rect())` in `resizeEvent` doesn't fire after the fullscreen transition, keeping VideoPlayer at pre-fullscreen dims.

### Fix plan

1. Log `MainWindow::isFullScreen()`, `windowFlags()`, and `geometry()` before and after `showFullScreen()` call at [MainWindow.cpp:131](src/ui/MainWindow.cpp#L131).
2. Confirm whether VideoPlayer's `setGeometry` fires after the fullscreen transition by adding a one-line log in [MainWindow.cpp:184-186](src/ui/MainWindow.cpp#L184-L186).
3. If `isFullScreen()` returns true but widget stays at 974 physical — top bar is eating space; hide `m_topBar` + `showFullScreen()` combined.
4. If `isFullScreen()` returns false — the flag isn't taking effect, probably a Qt platform-plugin issue or Windows DWM quirk. Try `setWindowState(Qt::WindowFullScreen)` instead.

**Files:** `src/ui/MainWindow.cpp` (+ possibly `VideoPlayer.cpp` for the resize path).

**Success:** F key → video fills the entire monitor. No title bar, no taskbar visible. All aspect ratios render correctly for their source (cinemascope letterbox top+bottom, 16:9 fills fully, 4:3 pillarbox sides).

## Verification files (for reference next session)

- `_player_debug.txt` — main-app logs. Contains `[FrameCanvas aspect]` diagnostic lines.
- `sidecar_debug_live.log` — sidecar stderr. Contains `[sub-path-diag] first_frame:`, `HOLY_GRAIL: emitting d3d11_texture event:`, `preload_subtitle_packets` traces.
- Test files:
  - Known working: Sopranos (1920×1080 HEVC softsubs), One Piece anime (1920×1080 HEVC with or without hardsubs).
  - Known broken: The Boys S03E06 Herogasm (1920×804 HEVC 10-bit softsubs via x265 AMZN WEB-DL).

## Session history

- Session of 2026-04-15 spent most of the day on speculation-driven patches. All failed on cinemascope. Agent 7 audit midway identified the architectural split as root cause. Agent 3 (me) confirmed sidecar-side `sub_blend_needed` + `fmt_is_hw` gating is firing correctly but subs still invisible — pointing at the present_cpu resize / stale handle theory.
- Do NOT re-ship small patches against these symptoms. Start from this document and pick Option A or Option B, not one-line guards.
