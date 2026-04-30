# MPV Render-API Integration TODO — Tankoban 2

**Authored 2026-04-27 by Agent 3 (HEMANTH-DRIVEN MODE, per direct request "craft a TODO" 2026-04-27 after architecture lock).**
**Owner: Agent 3.** Player domain. Sole consumer of `src/ui/player/*`.

Source intent: Hemanth ratified the architecture 2026-04-27 after correcting two prior misreads on my side:

1. He NEVER wanted external mpv launcher (I misread "open mpv separately through context menu on tiles" as `QProcess::startDetached("mpv.exe")` — that was wrong; he meant in-app integration).
2. He NEVER wanted libmpv via `wid` embedding ("window inside a window") — the precursor TankobanQTGroundWork did exactly that pattern and Hemanth wants to AVOID it. He wants libmpv via the **render API**: link libmpv into Tankoban.exe, render INTO our existing D3D11 surface (no child native HWND), composite into our Qt scene the same way ffmpeg-decoded frames are composited today.

End state: when the user picks the libmpv backend, mpv-decoded video appears INSIDE FrameCanvas's existing pixels — visually indistinguishable from ffmpeg playback today. HUD overlays unchanged. No second window, no HWND seam, no child window stacking artifact.

**UI PARITY REQUIREMENT (non-negotiable, Hemanth-locked 2026-04-27):** The player UI when mpv backend is active MUST be bit-identical to the ffmpeg backend's UI — same HUD, same chips (Subtitles / Audio / Settings / Playlist), same right-click context menu, same popovers, same keyboard shortcuts (Z/X/C for speed, Space for pause, etc.), same fullscreen behavior, same close button, same center flash, same subtitle overlay positioning, same toast hud, same volume hud. The user must NOT be able to tell which backend is rendering frames from any visible UI element. The ONLY user-facing surface that differs between backends is the backend-selection entries (right-click "Use mpv player" / "Use ffmpeg player" entries on library tiles + the Settings popover row) — and those entries must match existing menu styling exactly. Every Phase past P2 must verify this requirement; Phase 6 acceptance smoke is the load-bearing check.

**Comparable in scope to STREAM_SERVER_PIVOT + the previously-reverted MPV_BACKEND_INTEGRATION arc combined.** ~20-30 summons across 8 phases.

---

## Architecture lock

**libmpv render API → OpenGL FBO → D3D11/OpenGL interop bridge → FrameCanvas's existing D3D11 swap chain.**

Concretely:
1. Tankoban.exe links `libmpv-2.dll` (in-process).
2. We create a hidden `QOpenGLContext` + `QOffscreenSurface` for libmpv to render against.
3. FrameCanvas (or a sister helper) creates a **D3D11 shared texture** with `D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX`.
4. We register that D3D11 texture with WGL via `wglDXRegisterObjectNV` → get an OpenGL texture handle.
5. Bind the GL texture to a GL FBO color attachment.
6. Pass that FBO to libmpv via `mpv_render_context_render` with `MPV_RENDER_PARAM_OPENGL_FBO`.
7. On libmpv's update callback, lock the GL/DX object, libmpv writes one frame to the FBO, unlock.
8. FrameCanvas imports the D3D11 texture as a shader resource view (existing `m_importedSrv` path used today for ffmpeg sidecar's zero-copy NT-handle frames at `FrameCanvas.cpp:1502-1569`) and presents via existing DXGI swap chain.
9. HUD widgets (transport bar, popovers, right-click menu, center flash, subtitle overlay) stack on top via existing Qt widget z-order — unchanged.

**Key reused infrastructure:**
- `src/ui/player/FrameCanvas.{h,cpp}` — its `attachD3D11Texture(quintptr ntHandle, int w, int h)` accepts a shared NT handle and renders the texture via the existing pixel shader. The libmpv path produces the same shape of input, so most of FrameCanvas is reused unchanged.
- `native_sidecar/src/d3d11_gl_bridge.cpp` — already in the repo, currently unused by FrameCanvas (bridge was built for sidecar's GPU path then bypassed). We RELOCATE / link this into the main app for the libmpv interop.
- `Qt6::OpenGL` + `Qt6::OpenGLWidgets` — already linked into Tankoban.exe per `CMakeLists.txt:17-18`. The `QOpenGLContext` + `QOffscreenSurface` + WGL extension headers are usable today.

**Why OpenGL FBO + D3D11 interop instead of `MPV_RENDER_API_TYPE_SW`:** SW = libmpv outputs BGRA via CPU memory. Simpler integration (just route bytes into FrameCanvas's existing CPU upload path) but loses libmpv's GPU renderer (no libplacebo upscaling, no tone-map quality). Render-API OpenGL path keeps libmpv's `gpu` vo with libplacebo upscaling + tone-map + ICC — only loses `gpu-next` (Vulkan-exclusive). Hemanth ratified the perf/quality trade-off in conversation already.

---

## Reference slate

- **libmpv render API canonical header**: https://github.com/mpv-player/mpv/blob/master/include/mpv/render.h — quoted via WebFetch 2026-04-27 (see authoring chat thread). Backends: `MPV_RENDER_API_TYPE_OPENGL` + `MPV_RENDER_API_TYPE_SW` only. NO native D3D11 (hence the WGL_NV_DX_interop bridge).
- **libmpv OpenGL setup struct**: `mpv_opengl_init_params` declared in `render_gl.h` (sister header to `render.h`). Ships with libmpv distribution; will live in `resources/libmpv/windows/include/mpv/`.
- **Precursor reference (TankobanQTGroundWork)**: `C:\Users\Suprabha\Desktop\TankobanQTGroundWork\player_qt\ui\player_surface.py` + `render_host.py` — used `wid` (window-in-window) pattern. NEGATIVE EXAMPLE for our work — Hemanth specifically rejects this.
- **Stremio-shell legacy reference**: https://github.com/Stremio/stremio-shell/blob/master/mpv.cpp — used libmpv render API into a `QQuickFramebufferObject` (Qt Quick scene graph). Architecturally similar to our plan but inside Qt Quick instead of Qt Widgets + D3D11. Read for pattern, NOT copied.
- **Stremio-shell-ng (Rust)**: https://github.com/Stremio/stremio-shell-ng — shows the `wid` pattern reborn for perf reasons after the FBO approach proved 2-5x slower. Negative reinforcement for OUR pick — but Hemanth has accepted the perf cost in exchange for the seamless integration.
- **WGL_NV_DX_interop spec**: `wglDXRegisterObjectNV` / `wglDXLockObjectsNV` / `wglDXUnlockObjectsNV` — standard NV extension supported on NVIDIA + AMD desktop GPUs. Risk 1 below: spotty on Intel iGPUs / WARP — SW fallback (Phase 8 Q7) is the bypass.
- **Memory baselines**:
  - `reference_mpv_install.md` — mpv 0.41 at `C:\tools\mpv\` for reference + dev iteration.
  - `feedback_dxgi_resizebuffers_flags_must_match.md` — DXGI swap chain creation flag invariant; relevant when we destroy + recreate the libmpv shared texture on resize (Risk 3).
  - `feedback_subtitle_position_yoffset_not_libass.md` — current ffmpeg-side sub-position pattern. mpv backend bypasses this entirely via native `sub-pos` property (Phase 6).
  - `feedback_lifecycle_parity_with_mainwindow.md` — open/close discipline; MpvBackend's lifecycle must mirror SidecarProcess (Phase 3 + Phase 7).
  - `feedback_hemanth_driving_player_domain.md` — player work is HEMANTH-DRIVEN end-to-end.
  - `project_video_player_state_2026_04_24.md` — current state of FrameCanvas + DXGI pipeline.
- **Existing Tankoban surfaces (cite into):**
  - `src/ui/player/FrameCanvas.{h,cpp}` — D3D11 swap chain owner; `attachD3D11Texture` import path at `FrameCanvas.cpp:1502-1569`; reused 1:1 for libmpv path.
  - `src/ui/player/SidecarProcess.{h,cpp}` — current ffmpeg backend; gets retrofitted as `: public IPlayerBackend` in Phase 2.
  - `src/ui/player/VideoPlayer.{h,cpp}` — orchestrator widget; routes through `IPlayerBackend* m_backend` after Phase 2.
  - `native_sidecar/src/d3d11_gl_bridge.cpp` — WGL_NV_DX_interop primitives; relocated to main app in Phase 5.
  - `src/ui/pages/VideosPage.cpp` — library tile right-click menu; gets backend-swap entry in Phase 7.
- **TODO authoring template**: `feedback_fix_todo_authoring_shape.md` 14-section ratified shape, mirrored here.

---

## Decisions (Rule 14 — Hemanth ratifies wholesale or per-question)

PROPOSED picks below. Hemanth says "yes do all of them" OR overrides individual items. Phase 1 starts after lock.

### Q1 — Distribution: bundle libmpv-2.dll or rely on external install?
**PROPOSED: BUNDLE.** Drop `libmpv-2.dll` + dependencies (~30-50MB) into `resources/libmpv/windows/` as part of the build. CMake install step copies them next to `Tankoban.exe`. No "install mpv first" friction for new users. `LoadLibrary` resolves from the app dir before any `PATH` lookup.

### Q2 — Default backend after this lands?
**PROPOSED: ffmpeg stays default through the dual-backend window.** mpv becomes the default at deprecation time IF telemetry shows it earned the slot. Same pattern as the prior MPV_BACKEND_INTEGRATION_TODO Q2 — keep existing user behavior unchanged during validation.

### Q3 — Per-app-global vs per-show vs per-file backend preference?
**PROPOSED: per-app-global** (matches the precursor TankobanQTGroundWork pattern). Single setting in Settings popover; right-click context menu on library tiles also shows "Use mpv player" / "Use ffmpeg player" with "(current)" badge for one-click swap. Simpler than per-show and matches Hemanth's earlier confirmation that he wants this menu entry on tiles.

### Q4 — Stream mode?
**PROPOSED: stream mode locked to ffmpeg through this TODO.** Stream mode currently routes ffmpeg sidecar against the stream-server HTTP source. mpv CAN pull HTTP directly but stream-server pipeline is purpose-built for buffering/peer awareness/seekable HTTP. Adding mpv to stream mode opens a separate complexity surface — out of scope.

### Q5 — Subtitle pipeline: mpv-native or override through Tankoban's existing libass+Y-offset hack?
**PROPOSED: mpv NATIVE.** mpv's libass implementation is the gold standard. Subtitle position slider translates to mpv's `sub-pos` property (1-line API call vs the 3-iteration learning curve we hit on the ffmpeg-side hack per memory `feedback_subtitle_position_yoffset_not_libass.md`). Override would defeat the purpose.

### Q6 — Deprecation commitment? (LOAD-BEARING)
**PROPOSED: 60-day dual-backend window starting at Phase 8 ship, then DECISIVE pick.** Same shape as the prior TODO. On the deadline date Hemanth picks a survivor based on telemetry (which backend got used more, fewer bugs, felt better) and the loser is deleted entirely. NO permanent dual-backend state.

### Q7 — D3D11 interop fallback: SW path as a backstop if WGL_NV_DX_interop fails on a given driver?
**PROPOSED: YES, ship the SW path as a fallback.** If `wglDXRegisterObjectNV` returns null on driver-X (rare but possible on Intel iGPUs / WARP), libmpv falls back to `MPV_RENDER_API_TYPE_SW` and frames flow through the existing CPU upload path FrameCanvas uses for sidecar SHM frames. Quality is lower in fallback mode but playback works. ~50 LOC additional to support both paths.

---

## Phases

### Phase 0 — Decisions (Hemanth ratifies)

7 questions above. Wholesale ratification or per-question. Phase 1 unblocked at lock.

**Files:** none — pre-code.

**Acceptance:** Hemanth answers all 7 OR ratifies all PROPOSED picks wholesale.

---

### Phase 1 — Distribution + build infrastructure

**Scope:**
- Acquire official libmpv Windows build (mpv.io official site or `shinchiro/mpv-winbuild-cmake` releases). Pin a specific version (e.g., libmpv 0.40.0 stable). SHA-256 record in CMakeLists.txt comment.
- NEW `resources/libmpv/windows/` directory: drop `libmpv-2.dll`, dependencies (`libplacebo-*.dll`, etc.), and `include/mpv/*.h` (client.h, render.h, render_gl.h).
- MODIFIED `CMakeLists.txt` — add `find_package` shim or hand-rolled `target_link_libraries` for libmpv-2; copy DLLs to install target alongside `Tankoban.exe`; add include dir.
- MODIFIED `.gitignore` — exclude bundled DLLs from git per existing `resources/ffmpeg_sidecar/` precedent (binaries live on disk, not in git).
- NEW `setup_libmpv.bat` (or extend existing `setup.bat`) — one-shot dev helper to download + extract libmpv into `resources/libmpv/`. Idempotent.

**Files:** `CMakeLists.txt`, `resources/libmpv/` (new), `setup_libmpv.bat` (new or extension), `.gitignore`.

**Acceptance:**
- Tankoban.exe builds with libmpv linked.
- A trivial test in `src/ui/player/MpvProbe.cpp` (one-off temp file) calls `mpv_create()` + `mpv_initialize()` + `mpv_terminate_destroy()` cleanly and prints version. Verifies the DLL is loaded + the API is reachable.
- ~1-2 summons.

---

### Phase 2 — Backend abstraction (RE-DO of the previously reverted work)

**Scope:**
- NEW `src/ui/player/IPlayerBackend.h` — pure-virtual interface mirroring SidecarProcess's public surface (~54 methods + 33 signals + SubtitleTrackInfo struct). Same as the previously-reverted P1 ship-shape, exact same template.
- MODIFIED `src/ui/player/SidecarProcess.{h,cpp}` — `class SidecarProcess : public IPlayerBackend`; constructor base init `IPlayerBackend(parent)`; `override` keywords on all interface methods; signals: block removed (inherited from interface).
- MODIFIED `src/ui/player/VideoPlayer.{h,cpp}` — member `m_sidecar` becomes `m_backend` of type `IPlayerBackend*`. ~95 call-site rename + ~27 connect-site signal-class rename. Construction site at `:182` still constructs `SidecarProcess` concretely (BackendFactory comes in Phase 7).
- MODIFIED `src/ui/player/SubtitlePopover.{h,cpp}` — setSidecar param + member type → `IPlayerBackend*`.
- MODIFIED `src/ui/pages/StreamPage.cpp` — include + 2 connect-site signal-class rename.
- MODIFIED `CMakeLists.txt` — register `IPlayerBackend.h` for moc.

**Files:** Same 8 files I touched in the now-reverted P1 ship. Same shape, same edit sequence (well-trodden).

**Acceptance:**
- BUILD OK first try (or first-fix-trivial like last time's constructor base init).
- Grep proofs: zero standalone `m_sidecar`, zero `&SidecarProcess::` outside SidecarProcess.cpp's own private-slot self-connects, zero live `SidecarProcess*` type usage outside the construction site + class definition.
- Behavior smoke: existing playback byte-identical to pre-Phase-2.
- ~3-5 summons.

---

### Phase 3 — `MpvBackend` skeleton (no rendering yet)

**Scope:**
- NEW `src/ui/player/MpvBackend.{h,cpp}` — `IPlayerBackend` implementation. Constructor: `mpv_create()` + set initial options (`vo=libmpv`, `hwdec=auto`, `osd-level=0`, `keep-open=yes`, `sub-ass-force-margins=yes`) + `mpv_initialize()`.
- IPC translation layer: `sendOpen(path, sec)` → `mpv_command_async({"loadfile", path, "replace", "start=N"})`. `sendPause()` → `mpv_set_property({"pause", true})`. `sendSeek(sec)` → `mpv_command_async({"seek", sec, "absolute", "exact"})`. Etc.
- `mpv_observe_property` for `time-pos`, `duration`, `pause`, `aid`, `sid`, `eof-reached`, `track-list`, `metadata`. Each property change emits the corresponding Qt signal (`timeUpdate`, `tracksChanged`, etc.) translated into Tankoban's IPlayerBackend vocabulary.
- mpv's wakeup callback (`mpv_set_wakeup_callback`) posts a Qt event onto the GUI thread; the GUI thread drains `mpv_wait_event` and translates events to signals.
- `ensureTerminated(timeoutMs)` calls `mpv_terminate_destroy()` synchronously (libmpv's destroy is itself synchronous; no kill-backstop needed because there's no subprocess to outlive us).
- "Force mpv backend" debug toggle: env var `TANKOBAN_FORCE_MPV=1` makes the temporary BackendFactory in Phase 2 return `MpvBackend` for new files. Per-file selection comes in Phase 7.
- **Rendering NOT wired yet** — Phase 3 just verifies decode + audio + property events flow. Video has nowhere to go (libmpv's default `vo=libmpv` errors without a render context). For Phase 3 smoke we use `vo=null` instead and verify audio-only playback + time-pos updates.

**Files:** `src/ui/player/MpvBackend.{h,cpp}`, `CMakeLists.txt`.

**Acceptance:**
- BUILD OK.
- `TANKOBAN_FORCE_MPV=1 build_and_run.bat`, play any video. Audio plays through Tankoban's HUD. HUD time labels populate from libmpv's `time-pos` events. Pause/resume/seek work. NO video (expected — no render context yet).
- Close button kills the libmpv instance cleanly within ~500ms (CLOSE_AUDIO_CONTINUES_FIX equivalent — `mpv_terminate_destroy` is synchronous).
- ~3-5 summons.

---

### Phase 4 — Offscreen OpenGL context + render-context creation

**Scope:**
- NEW `src/ui/player/MpvRenderContext.{h,cpp}` — wraps the OpenGL setup:
  - `QOffscreenSurface` + `QOpenGLContext` (4.5 core profile) created once, reused across files.
  - `mpv_render_context_create` with `MPV_RENDER_PARAM_API_TYPE = MPV_RENDER_API_TYPE_OPENGL` + `MPV_RENDER_PARAM_OPENGL_INIT_PARAMS` populated with our `get_proc_address` callback.
  - `mpv_render_context_set_update_callback` posts a queued event to MpvBackend to trigger render.
- MpvBackend gains `setRenderContext(MpvRenderContext*)` API.
- For Phase 4 smoke we render into a TEMPORARY GL texture (no FBO sharing yet) and don't hand off to FrameCanvas. Proves the render API is alive and producing frames into our GL context.
- ~5-second `[MPV-RENDER]` log line per render call dumps frame counter + timing.

**Files:** `src/ui/player/MpvRenderContext.{h,cpp}`, `MpvBackend.{h,cpp}`, `CMakeLists.txt`.

**Acceptance:**
- BUILD OK.
- `TANKOBAN_FORCE_MPV=1`, play video. No video on screen yet (FrameCanvas integration is Phase 5), but `out/` debug log shows libmpv producing render callbacks at ~24-60Hz matching source FPS. `[MPV-RENDER]` lines confirm.
- ~3-5 summons.

---

### Phase 5 — D3D11/OpenGL interop bridge into FrameCanvas

**Scope:**
- RELOCATE `native_sidecar/src/d3d11_gl_bridge.{cpp,h}` → `src/ui/player/D3D11GLBridge.{cpp,h}` (or keep in place + add to main app's link list — Rule 14 at impl, prefer relocation for clarity since it's now main-app territory not sidecar).
- NEW glue in `MpvRenderContext`:
  - Create a D3D11 shared texture (`D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX`) sized to current video dims.
  - Register with WGL: `wglDXRegisterObjectNV(m_d3d_device, m_shared_tex, GL_TEXTURE_2D, ...)` → get `m_shared_tex_gl`.
  - Bind `m_shared_tex_gl` to a GL FBO color attachment.
  - On libmpv render callback: `wglDXLockObjectsNV(...)`, call `mpv_render_context_render` with `MPV_RENDER_PARAM_OPENGL_FBO = {fbo_id, width, height, internal_format=GL_RGBA8}`, `wglDXUnlockObjectsNV(...)`.
  - Emit a Qt signal `frameReady(quintptr ntHandle, int w, int h)` carrying the D3D11 shared NT handle.
- MODIFIED `src/ui/player/VideoPlayer.cpp` — `MpvBackend::frameReady` connects to the existing `m_canvas->attachD3D11Texture(handle, w, h)` slot (same one ffmpeg sidecar's `d3d11Texture` event uses).
- FrameCanvas needs zero changes — it already has the import + present path for D3D11 shared NT handles.

**Files:** `src/ui/player/MpvRenderContext.{h,cpp}`, `src/ui/player/D3D11GLBridge.{cpp,h}` (relocated), `src/ui/player/VideoPlayer.cpp`, `CMakeLists.txt`.

**Acceptance:**
- BUILD OK.
- `TANKOBAN_FORCE_MPV=1`, play SDR file (Saiki Ep 12 / The Boys S05E04). Video plays IN THE TANKOBAN WINDOW — no separate window, no titlebar seam, no taskbar entry for mpv. HUD overlays cleanly on top.
- Same source file env-OFF (ffmpeg path) and env-ON (mpv path): visually similar quality. Subjective Hemanth check: "looks at least as good."
- Resize the Tankoban window: video re-renders at new size. mpv's render context picks up the new FBO size on next render.
- HDR file (if Hemanth has one): plays through the libmpv path with correct color reproduction.
- ~3-5 summons.

---

### Phase 6 — Feature parity

**Scope:**
- Subtitle position via mpv `sub-pos` property (replacing the libass+Y-offset hack used on the ffmpeg path).
- Audio delay via mpv `audio-delay` property. Per-Bluetooth-device persistence (already in VideoPlayer; backend-agnostic).
- Subtitle delay via mpv `sub-delay`.
- Track switching via mpv `aid` / `sid` properties.
- External subtitle load via mpv `sub-add` command.
- Aspect ratio overrides via mpv `video-aspect-override`.
- Speed via mpv `speed` property (Z/X/C key bindings backend-agnostic).
- Per-show preferences (aspect, audio lang, sub lang, sub visibility, position) work backend-agnostically.
- Continue Watching position save: existing periodic position save via `timeUpdate` is backend-agnostic.

**Files:** `src/ui/player/MpvBackend.{h,cpp}`.

**Acceptance:**
- All player features work identically on mpv backend (Hemanth side-by-side smoke matrix on Saiki / The Boys / HDR file).
- Subtitle position slider on mpv backend is SMOOTHER than ffmpeg's (mpv native `sub-pos`).
- Per-show prefs inherit correctly when switching episodes within same show on mpv backend.
- **UI parity check (load-bearing, per Hemanth-locked requirement)**: Hemanth opens same file twice, once with each backend, and confirms the player UI looks IDENTICAL — same HUD, same chip layout, same menu entries, same keyboard shortcut behavior, same close + Continue Watching update behavior. The ONLY visible difference between backends should be in the right-click backend-selection menu entries (which show "(current)" badge tracking). If Hemanth's eye spots ANY UI difference (chip styling, animation timing, popover position, font, color, padding, anything), that's a P0 regression to fix before P7 starts.
- ~3-5 summons.

---

### Phase 7 — Backend selection UX + persistence

**Scope:**
- NEW `src/ui/player/BackendFactory.{h,cpp}` — `BackendFactory::createForFile(path) → IPlayerBackend*`. Reads QSettings key `videoPlayer/defaultBackend` (default: `"ffmpeg"` per Q2).
- MODIFIED `src/ui/MainWindow.cpp` — `openVideoPlayer` dispatches construction through factory.
- NEW context menu entry in VideosPage tile right-click: "Use mpv player" / "Use ffmpeg player" with `(current)` badge per the precursor's pattern. Selection persists to QSettings + applies to next openFile (mid-playback swap not supported; queued for next file). **Styling matches existing menu entries exactly** — same font, same indent, same hover treatment as "Reveal in File Explorer" / "Copy path" entries. No special chrome.
- NEW Settings entry in app preferences (top-right palette icon area or dedicated Settings page): "Default video backend: ○ ffmpeg ○ mpv". **Matches existing settings-row styling** — same row height, same label font, same radio-button treatment as any other setting in the popover.
- HDR auto-routing question (Phase 0 Q-side): if smoke shows mpv handles HDR meaningfully better, BackendFactory checks probe-detected HDR flag and overrides default — defer until Phase 6 smoke confirms the perceptible difference.

**Files:** `src/ui/player/BackendFactory.{h,cpp}` (NEW), `src/ui/MainWindow.cpp` (MODIFIED), `src/ui/pages/VideosPage.cpp` (MODIFIED — new menu entry), maybe a Settings popover edit.

**Acceptance:**
- Right-click "Use mpv player" on a tile → next play uses mpv. Setting persists across restart.
- Settings → switch default backend → next file open uses new default.
- Stream mode files locked to ffmpeg (Phase 0 Q4).
- ~2-3 summons.

---

### Phase 8 — SW fallback + 60-day dual-backend window + deprecation

**Scope:**
- SW fallback path (Phase 0 Q7): if `wglDXRegisterObjectNV` returns null at MpvRenderContext init, fall back to `MPV_RENDER_API_TYPE_SW` and route BGRA-from-CPU through a new path mirroring the existing OverlayShmReader pattern (FrameCanvas's `m_videoTexture` upload via `UpdateSubresource`). Single error-toast on first init failure documenting the fallback. ~50 LOC.
- Telemetry: BackendFactory logs every `openFile` with backend chosen + session duration to `out/backend_telemetry.log`. After 60 days, easy to see usage split.
- Begin 60-day dual-backend window. Calendar marker = Phase 8 ship date + 60.
- `/schedule` candidate for the deprecation decision wake.
- Memory writes: NEW `feedback_libmpv_render_api_lessons.md` capturing WGL_NV_DX_interop driver quirks, SW fallback frequency, render-cost real-world numbers.
- Deprecation execution wake (separate summon at +60d): Hemanth picks survivor → loser deleted entirely (`MpvBackend.{h,cpp}` + libmpv DLLs + libmpv-related CMake removed, OR `SidecarProcess.{h,cpp}` + `native_sidecar/` deleted entirely + remaining backend renamed to drop the abstraction).

**Files:** `MpvRenderContext.{h,cpp}` (SW fallback path), `BackendFactory.{h,cpp}` (telemetry), CLAUDE.md (Build Quick Reference doc), STATUS.md, memory files.

**Acceptance:**
- SW fallback verified on a contrived failing-driver path (force `wglDXRegisterObjectNV` to fail via init flag).
- Telemetry actively recording.
- `/schedule` wake created for +60d deprecation pick.
- ~2-3 summons (Phase 8 itself) + 1 summon for deprecation execution.

---

## Risk surface

1. **WGL_NV_DX_interop driver compatibility.** The extension is well-supported on NVIDIA + AMD desktop GPUs but historically flaky on Intel iGPUs and absent on WARP. Mitigation: SW fallback (Phase 8 Q7). Smoke on Hemanth's actual machine in Phase 5; if it fails there, SW path is the production reality.
2. **OpenGL context lifecycle vs Qt's main thread.** libmpv's render callback can fire from any thread; we marshal back to a Qt-controlled thread that owns the OpenGL context. Standard pattern but easy to mis-thread. Mitigation: Phase 4 smoke verifies the wakeup-callback → Qt-event → render path is single-threaded against the GL context.
3. **D3D11 shared texture size churn.** Resize the Tankoban window → libmpv's FBO needs a new size → we destroy + recreate the shared texture + re-register with WGL. ~10ms hiccup on resize. Acceptable; document the cost.
4. **Subtitle blend ordering shift.** Mpv blends subtitles INTO its rendered frame BEFORE handing back the FBO. So mpv-backend subtitles appear in the D3D11 texture FrameCanvas imports — they're already on the video. Our existing overlayShm subtitle path (used by ffmpeg backend) becomes a no-op for mpv backend. Verify in Phase 6 smoke that subtitles render correctly on mpv backend without the OverlayShmReader being attached.
5. **gpu-next loss documented.** Hemanth ratified this trade-off in conversation. Document explicitly in the Phase 5 RTC so future-me doesn't try to "fix" it.
6. **Distribution size +30-50MB.** libmpv-2.dll + libplacebo-*.dll + dependencies. Tankoban.exe distribution grows. Acceptable; document in Phase 1 RTC + bundling notes.
7. **Permanent dual-backend trap.** Phase 0 Q6 explicitly commits to deprecation. `/schedule` wake at +60d enforces the decision.
8. **The 100-call-site refactor in Phase 2.** Same risk + mitigation as the prior IPlayerBackend extraction (now reverted). Grep proofs verify zero leakage.

---

## Out of scope (explicitly Phase 8+ or post-deprecation)

- mpv-as-only-backend (decided by deprecation pick at +60d).
- `vo=gpu-next` Vulkan path (not exposed via render API; would require dropping interop and going back to `wid` which Hemanth rejects).
- Stream mode mpv routing (Phase 0 Q4 = locked to ffmpeg through this TODO).
- Per-show backend preference (Phase 0 Q3 = per-app-global only).
- mpv as external launcher / "Open with mpv" right-click on tiles (Hemanth never wanted this; my prior implementation was a misread, fully reverted).
- libmpv linked via `wid` embedding (Hemanth explicitly rejects this — "window inside a window").

---

## Verification per phase

- Phase 1 self-smoke: build green + libmpv DLL discoverable + version string prints from MpvProbe one-off.
- Phase 2 self-smoke: build green + grep proofs + behavioral smoke shows zero change vs pre-Phase-2.
- Phase 3 self-smoke: TANKOBAN_FORCE_MPV=1 → audio-only playback + HUD-time-labels-update + close-kills-mpv-cleanly.
- Phase 4 self-smoke: render callbacks fire at source FPS into a temp GL texture (no FrameCanvas integration yet).
- Phase 5 Hemanth smoke: video plays IN the Tankoban window via libmpv, HUD overlays cleanly, no separate window. SDR file pass + HDR file pass if available.
- Phase 6 Hemanth smoke matrix: feature parity check across Saiki / The Boys / HDR file. Subtitle position slider must work on mpv backend.
- Phase 7 Hemanth smoke: right-click menu swap + Settings default-backend swap + per-app persistence across restart.
- Phase 8 Hemanth: SW fallback verified on failing-driver simulation + telemetry recording + `/schedule` reminder in place.

---

## Files to be modified (summary)

**NEW files:**
- `src/ui/player/IPlayerBackend.h`
- `src/ui/player/MpvBackend.{h,cpp}`
- `src/ui/player/MpvRenderContext.{h,cpp}`
- `src/ui/player/D3D11GLBridge.{h,cpp}` (relocated from `native_sidecar/src/`)
- `src/ui/player/BackendFactory.{h,cpp}`
- `resources/libmpv/windows/` (libmpv-2.dll + dependencies + headers)
- `setup_libmpv.bat`
- Memory file `feedback_libmpv_render_api_lessons.md` (Phase 8 close-out)

**MODIFIED files:**
- `src/ui/player/SidecarProcess.{h,cpp}` (Phase 2 inheritance + override)
- `src/ui/player/VideoPlayer.{h,cpp}` (Phase 2 m_sidecar→m_backend; Phase 5 mpv-frameReady connection; Phase 7 factory dispatch)
- `src/ui/player/SubtitlePopover.{h,cpp}` (Phase 2 setSidecar param)
- `src/ui/pages/StreamPage.cpp` (Phase 2 connect signal-class; Phase 7 stream-locked-to-ffmpeg guard)
- `src/ui/pages/VideosPage.cpp` (Phase 7 right-click menu entry)
- `src/ui/MainWindow.cpp` (Phase 7 factory dispatch in openVideoPlayer)
- `src/ui/player/FrameCanvas.cpp` (Phase 5 — minimal; existing attachD3D11Texture path is reused unchanged in the happy path)
- `CMakeLists.txt` (Phase 1 libmpv linkage; Phase 2 IPlayerBackend.h registration; Phase 5 D3D11GLBridge sources; Phase 7 new files)
- `CLAUDE.md` (Phase 8 docs)
- `agents/STATUS.md` (Phase 8 status update)

---

## Sign-off

Authored 2026-04-27 by Agent 3 (HEMANTH-DRIVEN MODE, per direct request "craft a TODO" 2026-04-27). Plan ratified via ExitPlanMode same-session.

~20-30 summons across 8 phases. Comparable in scope to STREAM_SERVER_PIVOT and the previously-reverted MPV_BACKEND_INTEGRATION arc combined.

The technical risk lives in WGL_NV_DX_interop driver compat (Risk 1) — mitigated by SW fallback. The architectural risk is gone: Hemanth has locked in-process libmpv via render API, no `wid` embedding, paint into our own surface. The governance risk is bounded by the 60-day deprecation commitment (Phase 0 Q6).

Awaiting Hemanth Phase 0 ratification to unblock Phase 1.
