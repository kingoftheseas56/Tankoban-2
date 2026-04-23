# REQUEST IMPLEMENTATION — Agent 7 (Trigger D) — Video Zoom / Overscan port

**Requester:** Agent 3 (Video Player), 2026-04-23 post-Congress-8.
**Trigger:** D (implementation; `src/` writes authorized; RTC flag required; MCP smoke required for exit).
**Expected wake length:** ~90 min (~45 LOC implementation + smoke verification + reference-cite reading).
**Ownership:** Agent 7 (Codex) executes end-to-end. Ship code, MCP-smoke on F1, flag RTC for Agent 0 sweep.

---

## 1. Context — closes Hemanth's reported sports scoreboard bottom-chop bug

Hemanth repeatedly reports: on fullscreen playback of sports broadcasts (e.g. IPL cricket), the scoreboard at the bottom of the video renders with its bottom half cut off below the screen edge. Prior diagnosis this wake (by Agent 3 via MCP smoke + `_player_debug.txt` tail) established:

- Geometry logs are pixel-perfect: `videoRect={0,0,1920,1080} d3dVp={0,0,1920,1080} scissor={0,0,1920,1080} srcCrop={0,0,0,0}` in fullscreen on a 150% DPI 1920×1080 display.
- Autocrop (`scanBakedLetterbox`) correctly does NOT fire on cricket content.
- Source videos are clean 1920×1080 h264 yuv420p (ffprobe verified on all three Sports fixtures).

Conclusion: **Tankoban's renderer is correct.** The sports-broadcast scoreboards are encoded at the very bottom rows of the 1080p source with characters' descenders sitting at or past y=1079. On a TV with overscan (~3-5% edge crop) the scoreboard is fully visible; on a PC fullscreen at exact pixel dimensions (which Tankoban does correctly), the bleeding-edge content shows cut.

**Fix = user-selectable Zoom / Overscan feature**, matching the pattern VLC + mpv + PotPlayer all ship: scale the video up by 5-15% uniformly, cropping edges symmetrically; user picks the zoom level via the right-click menu; setting persists per-user.

This is a Congress-8-style reference-driven port: all three ref players have the feature, Tankoban doesn't, and the fix is narrow + testable.

---

## 2. Reference cite (Congress 8 discipline — read BEFORE writing code)

**Primary reference — mpv** (on disk at `C:/Users/Suprabha/Downloads/Video player reference/mpv-master/mpv-master/`):
- `options/options.c` — search for `"video-zoom"` OPT entry. Defines the user-facing property.
- `video/out/aspect.c` — `mp_get_src_dst_rects()` and related: how mpv composes `video-zoom` with aspect/crop math into the final rendered rect.
- `input/cmd.c` or similar — `add video-zoom 0.05` style command binding (increments zoom).

**Secondary reference — VLC** (on disk at `C:/Users/Suprabha/Downloads/Video player reference/vlc-master/vlc-master/`):
- `modules/gui/qt/menus/menus.cpp` — Zoom submenu structure (VLC exposes 0.25 / 0.5 / 1 / 2 / 4 presets). Read for menu layout conventions.
- `src/video_output/vout_intf.c` — `var_Create(p_vout, "zoom", ...)` — per-vout variable lifecycle for zoom.

**Tertiary reference — PotPlayer:** UI-level only (closed source). Observe behavior in preferences: Screen Size submenu + Screen Aspect Ratio custom scale. Optional MCP smoke on PotPlayer with F1 to confirm 105% overscan hides scoreboard cut.

**Cite format in commit message + code comment:** per Congress 8, code comment near `applyUserZoom` (or wherever the zoom factor enters the viewport math) must cite at least one of the above file:line references. Example shape:

```cpp
// User-facing video zoom / overscan (port 2026-04-23, Congress 8 FC).
// Mirrors mpv's `video-zoom` property (mpv-master/options/options.c
// OPT_FLOAT("video-zoom", ...)) + VLC's Zoom menu (VLC qt/menus/menus.cpp
// RendererMenu::aspectCroppingMenu(...)). Scales video uniformly by
// (1 + zoomPct/100) with symmetric edge crop — TV-overscan analog for
// sports broadcasts with scoreboard at bleeding-edge y=1079.
```

---

## 3. Scope — files + LOC

**Files to edit (~45 LOC net):**

1. `src/ui/player/FrameCanvas.h` — add `m_userZoom` double member; `setUserZoom(double)` public setter.
2. `src/ui/player/FrameCanvas.cpp` — in the video quad viewport math around [line 986-1023](../../src/ui/player/FrameCanvas.cpp#L986), compose `m_userZoom` into `cropZoom` (existing aspect-crop zoom). Default 1.0 = no change. 1.05 = 5% overscan.
3. `src/ui/player/VideoPlayer.h` — `setUserZoom(double)` public + `m_userZoom` state. Signal `userZoomChanged(double)` for menu checked-state sync.
4. `src/ui/player/VideoPlayer.cpp` — implement `setUserZoom` → `m_canvas->setUserZoom(z)` + persist via `QSettings("videoPlayer/userZoom", z)`. Restore on construction.
5. `src/ui/player/VideoContextMenu.h` — new `ZoomLevel` enum (Z100 / Z105 / Z110 / Z115 / Z120) + `SetZoom` action type.
6. `src/ui/player/VideoContextMenu.cpp` — build "Zoom" submenu under existing right-click menu. Radio-exclusive QActionGroup (only one checked at a time). Default checked = 100%. Emits `SetZoom` action with enum value.

**Files NOT touched:** sidecar (`native_sidecar/src/*`), stream code (`src/core/stream/*`), torrent (`src/core/torrent/*`). Sidecar is purely pixel source — zoom is main-app viewport math only.

**No API additions beyond the three setters.** 12-method stream API freeze is unaffected (different domain).

---

## 4. Implementation shape (for Codex to follow)

### 4.1 — FrameCanvas composition

In the video quad viewport math (FrameCanvas.cpp around lines 986-1023):

```cpp
// Current:
double cropZoom = 1.0;
if (m_cropAspect > 0.0 && frameAspect > 0.0 && m_cropAspect != frameAspect) {
    cropZoom = (m_cropAspect > frameAspect) ? m_cropAspect / frameAspect : frameAspect / m_cropAspect;
}

// New — compose user zoom on top of crop zoom:
cropZoom *= m_userZoom;  // 1.0 default = no change; 1.05 = 5% overscan
```

The downstream math (`croppedW = videoRect.w * cropZoom`, viewport expansion, scissor clipping to videoRect) already handles zoom > 1 by cropping edges — that's the exact mechanism the autocrop code uses. Composition is a one-line multiply.

### 4.2 — Menu structure

`VideoContextMenu::buildMenu()` — add between existing Aspect and Crop submenus:

```cpp
auto *zoomMenu = m->addMenu(QStringLiteral("Zoom"));
auto *zoomGroup = new QActionGroup(zoomMenu);
zoomGroup->setExclusive(true);
for (auto [label, pct] : {{"100%", 100}, {"105%", 105}, {"110%", 110}, {"115%", 115}, {"120%", 120}}) {
    auto *a = zoomMenu->addAction(label);
    a->setCheckable(true);
    a->setActionGroup(zoomGroup);
    if (pct == m_currentZoomPct) a->setChecked(true);
    connect(a, &QAction::triggered, [this, pct]() {
        emit actionRequested(ActionType::SetZoom, QVariant(pct));
    });
}
```

(Syntax above is illustrative — Codex adapts to Tankoban's existing menu-building patterns in VideoContextMenu.cpp.)

### 4.3 — Persistence

`QSettings` global, not per-file (matches VLC + mpv behavior — overscan is a user-environment preference, not file-specific). Key: `videoPlayer/userZoom`. Default 1.0.

---

## 5. Pre-flight

1. **MCP skies-clear:** verify no Tankoban.exe running; `tasklist /FI "IMAGENAME eq Tankoban.exe"`. Post Rule 19 LOCK before first smoke.
2. **Reference clones:** mpv + VLC sources already on disk at `C:/Users/Suprabha/Downloads/Video player reference/`. No fetch needed.
3. **Read the reference** per §2 before writing code. Note file:line of at least one cite.
4. **`build_check.bat`** before RTC — BUILD OK.
5. **Rule 17 cleanup:** `scripts/stop-tankoban.ps1` at wake close.

---

## 6. Smoke verification (required for exit)

Primary fixture: **F1 = `C:/Users/Suprabha/Desktop/Media/TV/Sports/Shubman Gill 84(50) Vs RR 2025 IPL Ball By Ball.mp4`** — known repro surface for scoreboard bottom-chop.

Procedure:
1. Launch Tankoban via `out/Tankoban.exe` (or `build_and_run.bat`). Kill any prior PID first.
2. Navigate Videos → play F1.
3. Press F for fullscreen.
4. Seek to a frame with the scoreboard visible (~30s-2min into the video).
5. Pause on scoreboard frame. Verify bottom-chop reproduces (descenders / bottom row cut).
6. Right-click → Zoom → 105%. Verify:
   - Scoreboard row now fully visible (no cut descenders)
   - Content is symmetrically cropped at ALL four edges (top loses ~27px, bottom loses ~27px, left/right lose ~48px)
   - Video is uniformly scaled (no distortion)
7. Try 110%, 115%, 120% — verify progressively tighter crop on the scoreboard + field area.
8. Select 100% — verify full-frame restored, scoreboard back to cut.
9. Close Tankoban. Relaunch. Re-open F1 fullscreen. Verify **last-selected zoom persists** (QSettings round-trip).
10. MCP screenshot at each zoom level. Save to `out/zoom_smoke_<pct>_1080p.png` via `System.Drawing.CopyFromScreen` for 1920×1080 full-res evidence.

Secondary fixture (non-regression): **F3 = Chainsaw Man** (cinemascope 2.40:1). Play F3 fullscreen. Verify:
- Default 100% behavior: no regression. Letterbox bars still render top + bottom as before.
- At 110%, letterbox bars shrink (crop into content). Acceptable — user-opt-in behavior.

---

## 7. Exit criteria

- `build_check.bat` = BUILD OK
- MCP smoke: 105%, 110%, 115%, 120% all render correctly on F1 with scoreboard appropriately visible
- Zoom persists across Tankoban restart
- F3 cinemascope non-regression at 100%
- Rule 17 cleanup clean
- Rule 19 LOCK released in chat.md
- RTC line posted: `READY TO COMMIT - [Agent 7 (Codex), VIDEO_ZOOM_OVERSCAN port — closes sports scoreboard bottom-chop via user-selectable 100%/105%/110%/115%/120% zoom]: <N> files / ~45 LOC / build_check BUILD OK / MCP smoke GREEN on F1 Shubman Gill RR IPL at 105% (scoreboard fully visible) + 100% default (full frame restored) + F3 Chainsaw Man non-regression verified. References cited: mpv-master/options/options.c video-zoom OPT, vlc-master/modules/gui/qt/menus/menus.cpp Zoom menu shape. Persistence via QSettings("videoPlayer/userZoom"). | files: src/ui/player/FrameCanvas.{h,cpp}, src/ui/player/VideoPlayer.{h,cpp}, src/ui/player/VideoContextMenu.{h,cpp}`
- Chat.md wake post summarizing the ship + linking evidence screenshots

---

## 8. Design calls — pre-decided by Agent 3, Codex does not revisit

These calls are locked to keep scope small. Do NOT renegotiate during implementation:

1. **Preset list: 100% / 105% / 110% / 115% / 120%** — 5 discrete presets, 5% steps, max 120%. Finer granularity (like VLC's free-form scale) adds UI complexity not worth it for the primary use case.
2. **No keyboard shortcut** — context menu only. Shortcut adds scope without clear need.
3. **Global QSettings persistence** — NOT per-file. User sets once, applies to all video playback. Matches VLC + mpv behavior.
4. **Compose with cropZoom, don't replace** — multiply into existing cropZoom in FrameCanvas. Preserves aspect-crop interaction.
5. **Symmetric crop only** — no asymmetric zoom (top-only, bottom-only). Overscan is uniform.
6. **No UI badge / toast on zoom change** — the menu checkmark is the feedback.

---

## 9. Out of scope

- Free-form zoom slider (future feature if Hemanth wants)
- Per-file zoom persistence
- Keyboard shortcuts for zoom in/out
- Aspect interaction redesign (zoom composes with existing aspect presets; no restructure)
- Autocrop changes
- PGS / subtitle overlay zoom coordination (overlay already scales with videoRect per existing code; no change needed)
- Stream-mode zoom behavior (zoom should work on stream playback identically; if it doesn't, flag but don't fix in this wake)

---

## 10. Agent 3 follow-up (after Codex ships)

- Domain review of Codex's commit: verify ref cite is present in code comment; verify `cropZoom *= m_userZoom` composition is one-line clean; verify no unintended touch of FC-1/FC-2 aspect code paths.
- Post chat.md review line as Agent 3.
- No re-smoke needed if Codex posts MCP screenshots per §6.10.

**End of REQUEST.** Execute when ready; claim Rule 19 LOCK first. Single-wake ship target.
