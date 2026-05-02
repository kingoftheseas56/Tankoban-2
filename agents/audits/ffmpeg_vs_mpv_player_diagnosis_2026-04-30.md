# ffmpeg-vs-mpv Player Diagnosis

Date: 2026-04-30  
Author: Agent 7 (Codex)  
Scope: Trigger-C comparative audit for Hemanth's ffmpeg keep/delete decision after the MPV_RENDER_API_INTEGRATION Phase 0 Q6 dual-backend window.  
Output status: Advisory. Hemanth keeps final authority.

Skills invoked: Codex-local equivalents of `/superpowers:brainstorming` for recommendation framing and `/superpowers:requesting-code-review` for self-review. The named slash skills are not installed in this Codex session. `/security-review` is not applicable because this is a capability/architecture audit and no security-surface change was made.

## Evidence Boundary

Observations below are backed by current repository files, current local memories, existing player logs, and the Stremio source reference retrieved from upstream because the local `C:\tools\stremio-shell\src\mpv.cpp` path was not present in this environment.

Fresh dual-backend live smoke was requested, but no new Hemanth/Agent-0/Agent-3 captured run was available in the repo at audit time. Therefore:

- Runtime numbers in Section 2 use existing logs: `out/ipc_latency.log`, `_player_debug.txt`, `sidecar_debug_live.log`, `out/mpv_baseline_132114.log`, and `scripts/compare-mpv-tanko.ps1`.
- I do not treat those numbers as final deletion telemetry.
- Any deletion decision should require one fresh same-file smoke on both backends after this audit, using the already requested capture set.

## §1. Architecture Diagnosis - Both Backends

### ffmpeg Path

Observation: The ffmpeg backend is a separate native process launched by the Qt app. `SidecarProcess::start` constructs a `QProcess`, starts `ffmpeg_sidecar.exe`, and waits asynchronously for process start (`src/ui/player/SidecarProcess.cpp:123-143`). The command surface is JSON-over-stdin: `sendCommand` writes objects shaped as `{ "type": "cmd", "name": ..., "sessionId": ..., "seq": ..., "payload": ... }` to the sidecar process (`src/ui/player/SidecarProcess.cpp:153-165`).

Observation: The app-side dispatcher parses JSON events from stdout and maps them into the shared player signal surface. The event switch handles acknowledgements, state, time updates, first frame, track lists, media info, D3D11 textures, overlay shared memory, process close, and IPC timing (`src/ui/player/SidecarProcess.cpp:472-632`). The ACK latency recorder appends per-command latency summaries to `out/ipc_latency.log` (`src/ui/player/SidecarProcess.cpp:979-1035`).

Observation: Frame delivery is a D3D11 zero-copy path into `FrameCanvas`. `FrameCanvas::processPendingImport` opens a shared NT handle with `ID3D11Device1::OpenSharedResource1`, creates a shader resource view, records dimensions, marks D3D active, and emits `zeroCopyActivated(true)` (`src/ui/player/FrameCanvas.cpp:1502-1571`). `VideoPlayer` wires sidecar `d3d11Texture` events into `FrameCanvas::attachD3D11Texture` and `overlayShm` into `FrameCanvas::attachOverlayShm` (`src/ui/player/VideoPlayer.cpp:2148-2170`).

Observation: Subtitle overlays are custom-rendered on the sidecar path. The app imports overlay shared memory in `FrameCanvas::attachOverlayShm` (`src/ui/player/FrameCanvas.cpp:2140-2183`). The native renderer applies subtitle position and PGS placement during blend (`native_sidecar/src/subtitle_renderer.cpp:677-684`, `native_sidecar/src/subtitle_renderer.cpp:974-1036`).

Observation: HDR/tone mapping is also custom. The app maps color/HDR parameters into shader constants (`src/ui/player/FrameCanvas.cpp:2232-2275`). The sidecar routes HDR content through `GpuRenderer`/libplacebo and sets HDR metadata before playback (`native_sidecar/src/main.cpp:880-907`). Tone-mapping and ICC command handlers are explicit sidecar IPC handlers (`native_sidecar/src/main.cpp:1675-1696`).

Observation: The ffmpeg stream path has app-specific stream behavior. The native sidecar includes explicit paused-for-cache style stall pause/resume handlers (`native_sidecar/src/main.cpp:1163-1184`). `StreamPage` places the shared `VideoPlayer` in stream mode, connects buffered ranges, and opens an HTTP stream URL (`src/ui/pages/StreamPage.cpp:2165-2167`, `src/ui/pages/StreamPage.cpp:2231`).

Observation: The current sidecar has code that addresses one historically sharp edge: slow track/subtitle work is now moved outside the main dispatcher path. `set_tracks_worker` uses a three-phase mutex pattern and keeps subtitle preload outside the session mutex (`native_sidecar/src/main.cpp:1320-1476`). This does not erase the historical risk; it shows the risk was real enough to require dispatcher-specific repair.

Observation: Frame delivery and IPC have had fragile edges. The app has session filtering and detailed stale-event suppression in the dispatcher (`src/ui/player/SidecarProcess.cpp:472-632`), and sidecar command latency logs include outliers, including one existing run where `set_audio_speed` reached `p99=12374ms` and `set_canvas_size` reached roughly `11982ms` (`out/ipc_latency.log`, 2026-04-25 22:35:47 run).

### mpv Path

Observation: The mpv backend is not a subprocess. `MpvBackend::initialize` calls `mpv_create()`, sets libmpv options, calls `mpv_initialize()`, and installs a wakeup callback (`src/ui/player/MpvBackend.cpp:117-175`). No `QProcess` is involved in the mpv backend path.

Observation: The bundled libmpv is local and linked into the app build. `CMakeLists.txt` includes `MpvProbe`, `MpvBackend`, and `MpvVideoWidget`, defines `HAS_LIBMPV=1`, links Qt OpenGL/OpenGLWidgets plus `opengl32`, links `libmpv.dll.a`, and deploys `libmpv-2.dll` beside the app (`CMakeLists.txt:17-18`, `CMakeLists.txt:334-335`, `CMakeLists.txt:418-445`, `CMakeLists.txt:506-517`). The bundled metadata identifies shinchiro/mpv-winbuild-cmake release `20260421` and git revision `5921fe5` (`resources/libmpv/windows/VERSION.txt:1-5`).

Observation: The mpv render path is a `QOpenGLWidget` direct-paint path. `MpvVideoWidget` extends `QOpenGLWidget` (`src/ui/player/MpvVideoWidget.h:30`). Its constructor requests OpenGL 3.2 core profile and connects update requests through a queued Qt signal (`src/ui/player/MpvVideoWidget.cpp:26-44`).

Observation: The render context is created with the libmpv OpenGL render API. `MpvVideoWidget::initializeGL` builds `mpv_opengl_init_params`, passes `MPV_RENDER_API_TYPE_OPENGL`, calls `mpv_render_context_create`, registers an update callback, and sets `vo=libmpv` (`src/ui/player/MpvVideoWidget.cpp:82-118`).

Observation: Painting is Qt-owned. The update callback is queued back to the widget thread (`src/ui/player/MpvVideoWidget.cpp:139-156`). `paintGL` obtains the widget's `defaultFramebufferObject()`, packages it as `MPV_RENDER_PARAM_OPENGL_FBO`, sets `MPV_RENDER_PARAM_FLIP_Y`, and calls `mpv_render_context_render` (`src/ui/player/MpvVideoWidget.cpp:159-183`).

Observation: The mpv path delegates decode, AV sync, audio output, subtitle decode/rendering, and most video pipeline behavior to libmpv. Tankoban observes mpv properties for `time-pos`, `duration`, `pause`, `eof-reached`, `track-list`, selected audio/subtitle IDs, subtitle visibility/delay, audio delay, metadata, and filename (`src/ui/player/MpvBackend.cpp:211-227`). Property events are translated into the same shared player signals (`src/ui/player/MpvBackend.cpp:346-442`).

Observation: The mpv path has a curated command/property surface, not full mpv exposure. Tankoban maps volume, mute, speed, audio delay, dynamic range compression via `af=acompressor`, audio/subtitle track selection, subtitle delay/position, external subtitle add, sub-scale, raw `vf`/`af`, tone mapping, and HDR peak to libmpv commands/properties (`src/ui/player/MpvBackend.cpp:581-779`). Some feature hooks remain explicit stubs, including subtitle style and filter batch behavior (`src/ui/player/MpvBackend.cpp:734-766`).

Observation: The mpv architecture changed from the original Phase 0 D3D11/GL interop plan. The current memory says Phase 5 redux pivoted to direct `QOpenGLWidget` because Intel UHD 620 WGL D3D11/GL interop failed. The source reflects that: mpv owns a GL render widget, and `VideoPlayer` hides `FrameCanvas` while showing `MpvVideoWidget` for mpv playback (`src/ui/player/VideoPlayer.cpp:1293-1335`). The stale `sendSetZeroCopyActive` comment in `MpvBackend` still mentions WGL interop, but the active path is direct GL (`src/ui/player/MpvBackend.cpp:767-779`).

### Shared Chrome and UI Integration

Observation: Tankoban intends the same player chrome regardless of backend, and the current code mostly follows that shape. `VideoPlayer::buildUI` creates the shared `FrameCanvas`, conditionally creates an `MpvVideoWidget` for mpv, then constructs the rest of the HUD/chrome after the video surface (`src/ui/player/VideoPlayer.cpp:1293-1335`). Because the same `VideoPlayer` owns the transport, popovers, overlays, and state machine, the chrome is not duplicated per backend.

Observation: First-frame progression is normalized into shared UI signals. On the ffmpeg path, the sidecar emits a real `firstFrame` event that `VideoPlayer` uses to dismiss loading and watchdog UI (`src/ui/player/SidecarProcess.cpp:472-632`, `src/ui/player/VideoPlayer.cpp:1838-1853`). On the mpv path, `MpvBackend` synthesizes the open/probe/decoder/frame progression from mpv events, including `FILE_LOADED` and `PLAYBACK_RESTART` (`src/ui/player/MpvBackend.cpp:241-304`).

Observation: The shared-chrome claim is architecturally true, but not pixel-identical at the video-surface layer. The ffmpeg surface is `FrameCanvas` with D3D11 texture import and overlay SHM; the mpv surface is a sibling `QOpenGLWidget` that hides `FrameCanvas` (`src/ui/player/VideoPlayer.cpp:1293-1335`, `src/ui/player/VideoPlayer.cpp:2148-2170`). Chrome above it is shared; the rendering substrate below it is not.

## §2. Capability Matrix - Current Tankoban Delivery

This section is phrased as capability placement, not as a better/worse score.

| Capability | ffmpeg sidecar path today | mpv path today | Parity direction |
|---|---|---|---|
| Local file decode coverage | Custom FFmpeg/libav sidecar. Format coverage depends on the sidecar FFmpeg build and Tankoban's custom decode/render decisions. | libmpv bundle, which includes mpv's FFmpeg/libass/libplacebo stack per bundled release metadata (`resources/libmpv/windows/VERSION.txt:1-5`). | mpv grows by exposing libmpv controls; ffmpeg grows by implementing more pipeline behavior. |
| Video surface | D3D11 shared texture import into `FrameCanvas` (`src/ui/player/FrameCanvas.cpp:1502-1571`). | libmpv render API into `QOpenGLWidget` FBO (`src/ui/player/MpvVideoWidget.cpp:159-183`). | Different substrates; shared chrome above them. |
| HDR/tone mapping | Custom shader/libplacebo path with explicit IPC controls (`src/ui/player/FrameCanvas.cpp:2232-2275`, `native_sidecar/src/main.cpp:880-907`, `native_sidecar/src/main.cpp:1675-1696`). | libmpv-native rendering controls exposed where wired; current commands include tone mapping and HDR peak (`src/ui/player/MpvBackend.cpp:581-779`). | mpv feature growth is mostly command/property exposure; ffmpeg growth is render-pipeline work. |
| ASS/SRT subtitles | Custom subtitle path with libass-style render/blend and overlay SHM (`native_sidecar/src/subtitle_renderer.cpp:677-684`, `src/ui/player/FrameCanvas.cpp:2140-2183`). | libmpv-native subtitle handling, with exposed track visibility/delay/position/external sub add/sub-scale (`src/ui/player/MpvBackend.cpp:346-442`, `src/ui/player/MpvBackend.cpp:581-779`). | mpv uses upstream subtitle machinery; ffmpeg keeps app-owned subtitle rendering. |
| PGS subtitles | Custom PGS blend path exists (`native_sidecar/src/subtitle_renderer.cpp:974-1036`). | Expected through libmpv if the bundle supports the file's subtitle stream, but no fresh mpv PGS smoke evidence was captured for this audit. | Needs same-file corpus smoke before deletion. |
| Audio output / AV sync | Custom audio decoder/output path in sidecar; audio decoder setup is explicit (`native_sidecar/src/main.cpp:925`). Stream stall handling is app-specific (`native_sidecar/src/main.cpp:1163-1184`). | libmpv-native audio and sync. Tankoban maps selected controls, delays, DRC, speed, and track state (`src/ui/player/MpvBackend.cpp:581-779`). | mpv inherits mpv's sync/audio machinery; ffmpeg retains custom control. |
| Audio passthrough | Not proven in the files/logs reviewed. | Not proven in the files/logs reviewed. | Treat passthrough as unverified for both until a device/file smoke verifies it. |
| Seek precision | Existing ffmpeg logs show low command latency in normal runs and outliers in some runs (`out/ipc_latency.log`). | `out/mpv_baseline_132114.log` contains prior direct-mpv seek baseline evidence, but not a fresh integrated mpv smoke. | Needs fresh same-file seek smoke for the deletion call. |
| Frame pacing | Existing sidecar debug shows many 24/25 fps windows with present p50 around 1-2ms and total p50 around 2-4ms, plus occasional late/dropped bursts (`sidecar_debug_live.log`). `_player_debug.txt` shows D3D11 first frame at 1920x800 with `wall_clock_delta_from_open_ms=1437` and render timer p50 around 16.6ms in one run. | Existing mpv baseline log and memory show direct GL was viable after the Intel UHD 620 pivot, but no fresh integrated mpv log was captured for this audit. | Do not make final reliability claims without new same-file smoke. |
| CPU/GPU cost | Existing app render logs show low per-frame draw/present cost on the ffmpeg D3D11 path in at least one run (`_player_debug.txt`). | No fresh integrated mpv CPU/GPU telemetry captured here. | Needs direct capture. |
| Stream mode | App-specific ffmpeg stream path exists through `StreamPage`, buffered ranges, HTTP stream URL open, and sidecar stall pause/resume (`src/ui/pages/StreamPage.cpp:2165-2167`, `src/ui/pages/StreamPage.cpp:2231`, `native_sidecar/src/main.cpp:1163-1184`). | No complete Tankoban stream-mode integration today. BackendFactory has a policy to force ffmpeg when `isStreamMode` is true (`src/ui/player/BackendFactory.cpp:24-36`), but current `VideoPlayer` construction calls `chooseFor(false)` once (`src/ui/player/VideoPlayer.cpp:192-193`), and `setStreamMode(true)` does not reselect the backend (`src/ui/player/VideoPlayer.cpp:704-831`). | mpv needs stream adapter work before ffmpeg deletion. |

### Concrete Existing Numbers

Observation: Normal ffmpeg IPC runs show small command round trips: one existing run recorded `open p50=3ms`, `pause p50=23ms`, and `stop p50=68ms`; another recorded `open p50=2ms`, `pause p50=8ms`, `set_tracks p50=31ms p99=35ms`, `volume p50=2ms p99=26ms`, and `stop p50=27ms` (`out/ipc_latency.log`, 2026-04-25 runs).

Observation: Existing ffmpeg IPC also has outliers. One run recorded `set_audio_speed p99=12374ms` and `set_canvas_size p50/p99/max` near `11982ms`; another later run recorded `open p50=581ms`, `set_audio_delay p50=769ms`, and `set_tracks p99=770ms` (`out/ipc_latency.log`, 2026-04-25/26 runs). These are evidence of tail-latency risk in the IPC/custom dispatcher architecture, not a fresh current-regression finding.

Observation: Existing ffmpeg frame logs show a successful D3D11/overlay run: first frame arrived with payload `width=1920 height=800`, texture/overlay events arrived immediately after, and render perf samples show timer p50 near 16.6ms with draw/present p50 generally under 1ms in that captured segment (`_player_debug.txt`).

Observation: Existing sidecar performance logs show mostly stable 24/25 frame windows with p50 blend/present totals in low milliseconds, but also occasional late-frame and drop bursts (`sidecar_debug_live.log`). That supports "capable but sensitive", not "failed".

Observation: The comparison harness exists and classifies divergence by parsing mpv and Tankoban logs (`scripts/compare-mpv-tanko.ps1`). Prior chat evidence around that harness mentioned `mpv_drops=0`, `tanko_drops=332`, and `tanko stall pauses=53`, but that was historical evidence for a specific stream/stall investigation, not a current Phase 7 integrated mpv-vs-sidecar file-mode smoke.

## §3. Integration Shape - Stremio-Like or wid-Style?

Answer: Tankoban's current mpv integration is on the Stremio-like side of Hemanth's veto line. It is in-process libmpv render API into a Qt-owned framebuffer/widget. It is not a subprocess and not `--wid` child-window embedding.

### Tankoban Path

Observation: `MpvVideoWidget` is a Qt GL widget: `class MpvVideoWidget final : public QOpenGLWidget` (`src/ui/player/MpvVideoWidget.h:30`). The constructor sets a Qt `QSurfaceFormat`, requests OpenGL 3.2 core profile, and uses queued Qt signals for update delivery (`src/ui/player/MpvVideoWidget.cpp:26-44`).

Observation: Tankoban creates the libmpv render context in the Qt GL context. It passes `MPV_RENDER_API_TYPE_OPENGL` and calls `mpv_render_context_create(&m_renderContext, m_mpv, params)` (`src/ui/player/MpvVideoWidget.cpp:82-118`).

Observation: Tankoban renders into the widget-owned framebuffer. `paintGL` uses `defaultFramebufferObject()`, sets `MPV_RENDER_PARAM_OPENGL_FBO`, `MPV_RENDER_PARAM_FLIP_Y`, and calls `mpv_render_context_render(m_renderContext, params)` (`src/ui/player/MpvVideoWidget.cpp:159-183`).

Observation: Tankoban does not assign a native window ID to mpv. I found no `wid` property usage in `src/ui/player/MpvBackend.*` or `src/ui/player/MpvVideoWidget.*`. The mpv handle is passed to `MpvVideoWidget`, which renders with libmpv's render API (`src/ui/player/VideoPlayer.cpp:1293-1335`).

### Stremio Comparison

Observation: Stremio's reference architecture is also libmpv render API into a Qt-owned framebuffer. Upstream `mpv.cpp` defines `MpvRenderer : public QQuickFramebufferObject::Renderer`, creates the render context with `MPV_RENDER_API_TYPE_OPENGL`, obtains `framebufferObject()`, and calls `mpv_render_context_render` (Stremio `mpv.cpp`, upstream lines 44, 70, 74, 86, 101: https://github.com/Stremio/stremio-shell/blob/master/mpv.cpp).

Observation: Stremio's object owns an in-process mpv handle created by `mpv_create()`, sets `vo=libmpv`, installs a wakeup callback, and uses queued Qt invocation for updates (Stremio `mpv.cpp`, upstream lines 108, 150, 176, 215, 309: https://github.com/Stremio/stremio-shell/blob/master/mpv.cpp).

Assessment: Tankoban is not identical to Stremio's Qt Quick implementation: Tankoban uses `QOpenGLWidget`, while Stremio uses `QQuickFramebufferObject`. But the architectural family is the same for Hemanth's decision: in-process libmpv render API, Qt-owned framebuffer, Qt compositor/chrome above it.

### Negative wid Comparison

Observation: The rejected TankobanQTGroundWork path uses native-window embedding. `player_surface.py` tracks `_last_mpv_wid`, marks the render host as a native window with `WA_NativeWindow`, calls `attach_mpv`, and assigns `self._mpv.wid = value` in `_set_mpv_wid` (`C:\Users\Suprabha\Desktop\TankobanQTGroundWork\player_qt\ui\player_surface.py:76`, `:200-201`, `:1416-1420`, `:1425-1431`).

Conclusion for Q3: The new Tankoban mpv implementation is not the rejected GroundWork pattern. It is not a child mpv window inside a Qt window. It is a Qt-owned render surface using libmpv's render API. If Hemanth's veto condition is "no subprocess and no wid-style child window", this implementation passes that condition. If Hemanth's veto condition is stricter, namely "mpv must render through the exact same `FrameCanvas` D3D11 texture path as ffmpeg", the current implementation does not meet that stricter condition; it uses a sibling `QOpenGLWidget` surface under shared chrome.

## §4. Future-Dev Parity Question

### Growing mpv Toward "Real mpv" Parity

Observation: Tankoban's mpv path already has the correct growth shape for most mpv-native features: add property observation, map a command/property setter, and translate mpv events into existing `VideoPlayer` signals. The current code shows that pattern repeatedly: observed properties are declared in one place (`src/ui/player/MpvBackend.cpp:211-227`), property events are translated in the event loop (`src/ui/player/MpvBackend.cpp:346-442`), and feature commands are mostly wrappers around `mpv_set_property*` or `mpv_command*` (`src/ui/player/MpvBackend.cpp:581-779`).

Observation: For many future features, the work is adapter work, not media-pipeline work:

- Add more property hooks: extend `observeProperties`, add a `MPV_EVENT_PROPERTY_CHANGE` branch, emit an existing or new Qt signal.
- Add more command surface: create a `send*` method that calls `mpv_set_property_*`, `mpv_command_async`, or `mpv_command`.
- Add hwdec mode swaps: expose the relevant mpv property/option and ensure UI state maps cleanly.
- Add `gpu-next` or VO/render options: set libmpv options before initialization or during safe runtime windows, then verify on target hardware.
- Add ICC/profile controls: map libmpv properties/options if supported by the bundled mpv build, then expose app state.

Observation: The current mpv path still needs completion work. Subtitle style and filter batching are stubs today (`src/ui/player/MpvBackend.cpp:734-766`). Some comments still reflect the abandoned WGL interop plan (`src/ui/player/MpvBackend.cpp:767-779`). Those are integration-completion items, not evidence of the wrong architecture.

### Growing ffmpeg Toward the Same Feature Set

Observation: The ffmpeg path can be extended, but the shape of work is different. It requires maintaining custom decode, render, subtitle, audio, sync, HDR, and IPC behavior. Existing features are already app-owned: D3D11 texture import (`src/ui/player/FrameCanvas.cpp:1502-1571`), overlay SHM (`src/ui/player/FrameCanvas.cpp:2140-2183`), HDR/color shader state (`src/ui/player/FrameCanvas.cpp:2232-2275`), libplacebo sidecar setup (`native_sidecar/src/main.cpp:880-907`), subtitle blending (`native_sidecar/src/subtitle_renderer.cpp:974-1036`), and dispatcher/latency handling (`src/ui/player/SidecarProcess.cpp:472-632`, `src/ui/player/SidecarProcess.cpp:979-1035`).

Observation: Features that are mpv-native become multi-layer features on the ffmpeg path. For example:

- A new subtitle behavior may require sidecar decode changes, subtitle renderer changes, overlay texture handling, IPC payloads, and UI state.
- A new renderer behavior may require FFmpeg/libplacebo configuration, D3D shader or texture state changes, command IPC, and smoke tests on Intel UHD 620.
- A new audio behavior may require native decoder/output changes, sidecar command handling, app-side signals, and AV sync verification.
- A new stream buffering behavior may require stream-server behavior, sidecar session state, app overlays, and race/latency checks.

Concrete summon-unit comparison, expressed as shape rather than LOC:

- mpv-native property feature: usually one backend method, one property observer if stateful, one signal bridge, one smoke.
- ffmpeg-equivalent decoder/render feature: sidecar command handler, native media-pipeline implementation, app-side IPC wrapper, UI signal/state bridge, and renderer/subtitle/audio validation depending on feature.
- mpv-native pipeline feature such as hwdec/gpu/subtitle renderer behavior: mostly expose and verify upstream capability.
- ffmpeg-equivalent pipeline feature: implement or integrate that behavior in Tankoban's own FFmpeg/libplacebo/D3D/subtitle/audio stack.

This is the main strategic asymmetry. It is not that ffmpeg cannot be extended. It is that the custom path makes Tankoban the owner of features that libmpv already owns upstream.

### Streaming-Friendly Work

Observation: The ffmpeg path already owns stream-mode semantics in Tankoban. It is connected to `StreamPage`, buffered ranges, stream URL open, and native stall pause/resume behavior (`src/ui/pages/StreamPage.cpp:2165-2167`, `src/ui/pages/StreamPage.cpp:2231`, `native_sidecar/src/main.cpp:1163-1184`).

Observation: The mpv path has no complete stream-mode integration today. It may be able to `loadfile` an HTTP URL through libmpv, but Tankoban stream mode is more than "play a URL": it includes stream-server lifecycle, byte-range/cache semantics, buffering overlays, stall/resume policy, and shared UI expectations.

Observation: The intended Phase 0 Q4 lock exists in `BackendFactory`: `chooseFor(true, ...)` returns ffmpeg (`src/ui/player/BackendFactory.cpp:24-36`). But the current `VideoPlayer` constructor calls `chooseFor(false)` once (`src/ui/player/VideoPlayer.cpp:192-193`), and `setStreamMode(true)` only toggles stream UI/state (`src/ui/player/VideoPlayer.cpp:704-831`). I did not find a current per-open backend reselection that forces ffmpeg when `StreamPage` opens a stream URL.

Implication: mpv stream work has two parts:

1. Enforce backend selection correctly for stream opens until mpv stream mode exists.
2. Add an mpv stream adapter if mpv is to survive as the sole backend: stream-server REST/URL handoff, byte-range/cache behavior, buffering/stall events, and UI overlay parity.

Hypothesis - The stream-mode force-to-ffmpeg policy may currently rely on default preference/state rather than a hard per-stream backend switch. Agent 3 to validate.

## §5. Recommendation

Recommendation: Choose Option A as the strategic direction, but do not delete ffmpeg until mpv has passed a focused stream-mode retrofit gate and a fresh same-file file-mode smoke gate.

In direct terms: Hemanth should plan to ditch ffmpeg as the long-term player backend, not because the custom path is invalid, but because the mpv path is the right architecture for the future feature set he described. The integration is on the right side of the veto line: in-process libmpv render API, Qt-owned framebuffer/widget, no subprocess, no `wid` embedding. For future "real mpv" features, Tankoban mostly needs to expose and curate libmpv's command/property surface. The ffmpeg path can keep growing, but many mpv-native capabilities become Tankoban-owned decoder/render/subtitle/audio/IPC work.

Option A - Ditch ffmpeg, mpv sole backend:

- Required before deletion: mpv stream-mode adapter or an explicit replacement for current ffmpeg stream semantics.
- Required before deletion: fresh same-file smoke comparing ffmpeg and integrated mpv on local file playback, including subtitles, HDR where available, seeking, track changes, pause/resume, frame pacing, and close/reopen.
- Strategic risk: single-backend trap. If mpv becomes sole backend before stream and smoke gates pass, Tankoban loses the custom path that currently carries stream mode and app-specific D3D11 playback.
- Why still recommended: this aligns with Hemanth's future-dev goal. Most desired mpv-parity features are already upstream libmpv responsibilities; Tankoban should not keep reimplementing them in the custom ffmpeg path unless there is a hard product reason.

Option B - Ditch mpv, ffmpeg sole backend:

- This preserves the known stream path and current custom renderer ownership.
- It also makes Tankoban responsible for every future feature that would otherwise arrive through libmpv: command surface, renderer options, subtitle behavior, hwdec/gpu behavior, color management, and long-tail media compatibility.
- This is the wrong strategic direction if Hemanth wants "feature parity with real mpv" and streaming-friendly evolution with the least custom media-pipeline ownership.

Option C - Keep both indefinitely:

- This avoids an immediate deletion decision.
- It also creates the permanent dual-backend trap that Phase 0 Q6 was designed to prevent: two playback substrates, two bug surfaces, two sets of smoke matrices, and repeated parity work in shared chrome.
- This should be a temporary escape hatch only if the stream retrofit is not done by the Q6 deadline. It should not be the desired end state.

Assumptions that could flip the recommendation:

- Fresh integrated mpv smoke shows unacceptable file-mode regressions on Hemanth's hardware that are not fixable in the 60-day window.
- Hemanth decides stream mode is the highest-order player requirement and mpv stream adapter work will not be funded before deletion.
- The current `QOpenGLWidget` direct-paint surface fails a veto-grade UX requirement that is stricter than "not subprocess and not wid".
- A required feature depends on Tankoban-specific custom rendering that libmpv cannot expose cleanly through the render API.

Bottom line: The architecture evidence supports mpv as the intended survivor, with ffmpeg retained only until stream mode and fresh smoke evidence are closed. The strongest blocker to deletion is not integration shape; it is current stream-mode parity.

## Required Follow-Up Evidence Before Delete

1. Fresh ffmpeg same-file smoke: local file, subtitles, seek, track switch, pause/resume, close/reopen; capture `out/ipc_latency.log`, `_player_debug.txt`, and sidecar log.
2. Fresh mpv same-file smoke: same file and actions; capture libmpv output and app debug.
3. Stream-mode gate: either verify a hard force-to-ffmpeg backend switch while dual-backend remains, or implement/verify mpv stream adapter behavior before deleting ffmpeg.
4. Q3 veto confirmation from Hemanth: confirm that `QOpenGLWidget` direct render under shared chrome satisfies "truly part of the app" and that exact `FrameCanvas` substrate parity is not required.
