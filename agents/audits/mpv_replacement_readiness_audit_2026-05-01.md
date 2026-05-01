# mpv Replacement Readiness Audit - 2026-05-01

Agent 7 (Codex) Trigger C audit. Reference only.

Question: should Tankoban commit to mpv as the sole player and decommission the ffmpeg sidecar?

Short answer: yes as a staged target, not as a today cutover. The current mpv backend is far enough along to justify closing the remaining parity work around it. It is not yet ready to be the only shipped backend.

Source note: the prompt anchored on Phase 1 close from 2026-04-30. The local repo has moved forward. Phase 2 subtitle work is also marked closed in `CLAUDE.md`, `agents/chat.md`, and `project_mpv_ffmpeg_parity_p2_complete.md`. This audit uses the source state present on 2026-05-01.

External references consulted:

1. [mpv manual](https://mpv.io/manual/master/)
2. [mpv subtitle options](https://mpv.io/manual/master/#options-for-subtitles)
3. [mpv HDR peak option](https://mpv.io/manual/master/#options-hdr-peak-decay-rate)
4. [libmpv render API header](https://github.com/mpv-player/mpv/blob/master/libmpv/render.h)
5. [libass wiki](https://github.com/libass/libass/wiki)
6. [Stremio shell mpv render reference](https://github.com/Stremio/stremio-shell/blob/master/mpv.cpp)

Local references consulted include `src/ui/player/IPlayerBackend.h`, `SidecarProcess.cpp`, `MpvBackend.cpp`, `MpvVideoWidget.cpp`, `VideoPlayer.cpp`, `BackendFactory.cpp`, `src/ui/pages/stream/StreamPlayerController.cpp`, `src/ui/pages/StreamPage.cpp`, `native_sidecar/src/main.cpp`, `MPV_FFMPEG_PARITY_FIX_TODO.md`, `MPV_RENDER_API_INTEGRATION_TODO.md`, `agents/chat.md:2162-2217`, `agents/chat.md:2566-2651`, and the requested memory files.

## §1. ffmpeg Feature Inventory: Current Sidecar Surface

This is the parity bar. Anything Tankoban sends through `IPlayerBackend` is part of the player contract, even when the implementation lives below the UI.

1. Backend lifecycle.

   `IPlayerBackend` requires start/running/termination/reset semantics, stop fencing, process-close/crash signals, and readiness state. The ffmpeg path implements this through `SidecarProcess` and a child process boundary. The sidecar dispatcher has explicit shutdown, stop, and restart-oriented behavior. See `src/ui/player/IPlayerBackend.h:54-71`, `src/ui/player/SidecarProcess.cpp:214-254`, and `native_sidecar/src/main.cpp:1866-2002`.

2. Core playback.

   Tankoban depends on open, pause, resume, transparent stall pause/resume, seek, seek mode, frame-step, stop, loop-file, EOF, time updates, and state changes. These are first-class backend methods and signals in `IPlayerBackend.h:71-93`, `IPlayerBackend.h:197-205`, and sidecar command wrappers in `SidecarProcess.cpp:181-293`.

3. Frame delivery and render path.

   The ffmpeg sidecar emits D3D11 shared textures and overlay shared-memory frames. `VideoPlayer` consumes those through `d3d11Texture` and `overlayShm` signals and attaches them to `FrameCanvas`. This is part of the visible player contract, not just an implementation detail. See `IPlayerBackend.h:239-242`, `SidecarProcess.cpp:664-675`, `native_sidecar/src/main.cpp:797-829`, and `VideoPlayer.cpp:3808-3819`.

4. Audio control.

   The sidecar surface includes volume, mute, playback rate, audio delay, audio speed, dynamic-range compression, audio track enumeration, and audio track switching. The sidecar also emits media audio-device metadata used by Tankoban for per-device audio-delay recall. See `IPlayerBackend.h:95-113`, `SidecarProcess.cpp:296-337`, `native_sidecar/src/main.cpp:119`, `native_sidecar/src/main.cpp:562`, and `VideoPlayer.cpp:3763`.

5. Subtitle control.

   The sidecar surface includes subtitle track enumeration and switching, visibility, delay, style, subtitle position, subtitle position mode, local external subtitles, subtitle URL staging, pixel offset, size, active subtitle index, and subtitle text delivery. See `IPlayerBackend.h:113-146`, `SidecarProcess.cpp:340-399`, and the cached subtitle URL/persistence helpers in `SidecarProcess.cpp:800-970`.

6. Subtitle rendering and fonts.

   The ffmpeg path uses Tankoban's bundled libass renderer in `native_sidecar/src/subtitle_renderer.h`. Phase 2 added embedded MKV font loading before subtitle parsing and on track switches. See `native_sidecar/src/main.cpp:872-885` and `native_sidecar/src/main.cpp:1362-1428`.

7. Video filters and picture controls.

   Tankoban exposes structured filter control through `sendSetFilters` plus raw filter control through `sendRawFilters`. The sidecar dispatches filter settings and applies pending specs. It also accepts tone mapping, ICC profile, hardware acceleration, and color-management commands. See `IPlayerBackend.h:152-157`, `SidecarProcess.cpp:402-463`, `native_sidecar/src/main.cpp:1685-1728`, and `native_sidecar/src/main.cpp:1983-1995`.

8. HDR, tone mapping, and color metadata.

   The sidecar emits media info with HDR, color primaries, transfer characteristics, chapters, duration, and audio-device details. `VideoPlayer` reads this to set HDR state, pass color info to the canvas, populate chapters, and restore audio-device offsets. See `native_sidecar/src/main.cpp:562`, `VideoPlayer.cpp:3719-3763`, and `VideoPlayer.cpp:3742`.

9. Streaming mode.

   Stream mode hands Tankoban a stream-server HTTP URL, stream progress state, buffered ranges, and stall edges. The ffmpeg backend remains the default daily stream backend. Stream progress is persisted by `StreamPage`, while stream stall pause/resume is backend-facing through `IPlayerBackend`. See `IPlayerBackend.h:76-80`, `StreamPlayerController.cpp:151-295`, `StreamPage.cpp:1971-2008`, `StreamPage.cpp:2154-2231`, and `VideoPlayer.cpp:786-794`.

10. Buffering and cache telemetry.

   The sidecar emits buffering, playing, and cache-state events. `SidecarProcess` maps those into `bufferingStarted`, `bufferingEnded`, and `cacheStateChanged`. The UI uses these for the loading overlay and transparent stall semantics. See `IPlayerBackend.h:249-255`, `native_sidecar/src/main.cpp:752-780`, and `SidecarProcess.cpp:685-702`.

11. Error and diagnostic surfaces.

   The sidecar sends structured errors, decode errors, process closed/crashed events, probe stages, and IPC latency logs. Tankoban wires these into UI and debug behavior. See `IPlayerBackend.h:205-213`, `native_sidecar/src/main.cpp:782-795`, `native_sidecar/src/main.cpp:441-447`, and `SidecarProcess.cpp:590-635`.

12. Persistence.

   Persistence is partly backend-agnostic and partly dependent on backend event shape. Library progress, show preferences, audio/subtitle IDs, language preferences, subtitle visibility, subtitle delay, subtitle position, subtitle position mode, subtitle size, stream progress, and per-device audio delay all depend on the backend emitting enough state for `VideoPlayer` and `StreamPage` to save and restore. See `VideoPlayer.cpp:2797-2994`, `StreamPage.cpp:1971-2008`, and `StreamPage.cpp:2171-2231`.

## §2. mpv Coverage Inventory

Coverage labels:

1. WIRED means Tankoban calls a method or receives a signal and the mpv backend implements useful behavior today.
2. STUBBED means a method exists but is a no-op or partial bridge.
3. MISSING means the Tankoban feature has no confirmed mpv-side surface yet.
4. NATIVE means mpv handles the media capability internally, but Tankoban may still need a bridge for UI, persistence, or telemetry.

Current coverage:

1. Backend lifecycle: WIRED.

   `MpvBackend` creates and initializes libmpv in-process, observes properties, handles stop/shutdown, and supports stop callbacks. See `MpvBackend.cpp:120-182` and `MpvBackend.cpp:998-1017`.

2. Local file open: WIRED.

   `sendOpen` calls mpv `loadfile` and passes a start position option. It works for library playback. See `MpvBackend.cpp:620-643`.

3. Stream HTTP open: WIRED under FORCE_MPV, not default daily routing.

   The mpv backend can play stream-server HTTP URLs, and the Phase 1 evidence file confirms a five-minute FORCE_MPV stream probe with progress persistence and no ffmpeg sidecar process. See `MpvBackend.cpp:620-643`, `agents/audits/evidence_mpv_stream_probe_180214.txt`, and `BackendFactory.cpp:28-38`.

4. Stream daily routing: MISSING as the default route.

   `BackendFactory::chooseFor` still returns ffmpeg for stream mode unless `TANKOBAN_FORCE_MPV=1` is set. See `BackendFactory.cpp:24-38`.

5. Pause, resume, seek, seek mode, frame step, stop, loop: WIRED.

   mpv maps these to `pause`, `seek`, `frame-step`, `frame-back-step`, `stop`, and `loop-file`. See `MpvBackend.cpp:646-735` and `MpvBackend.cpp:990-995`.

6. Transparent stall semantics: WIRED for Phase 1.

   mpv observes `paused-for-cache` and `demuxer-cache-state`, emits buffering edges and throttled cache-state updates, and gates mpv self-pause while in transparent stall. See `MpvBackend.cpp:260-272`, `MpvBackend.cpp:513-584`, and `MpvBackend.cpp:646-682`.

7. Time updates and duration: WIRED.

   mpv observes `time-pos` and `duration` and emits `timeUpdate`. See `MpvBackend.cpp:243-272` and `MpvBackend.cpp:409-417`.

8. Render path: WIRED through QOpenGLWidget, but not the original D3D11 texture route.

   `MpvVideoWidget` creates a libmpv OpenGL render context, switches `vo` to `libmpv`, renders through `mpv_render_context_render`, and emits first-frame notification. See `MpvVideoWidget.cpp:82-116` and `MpvVideoWidget.cpp:155-190`. This uses the official libmpv OpenGL render API surface documented in `render.h`.

9. HUD and render surface flip: WIRED with a known edge.

   `VideoPlayer` can switch backends mid-session, hide the FrameCanvas, and show `MpvVideoWidget`. See `VideoPlayer.cpp:3836-3920`. Phase 1 memory still flags a HUD reveal/mouse-event-routing edge under `MpvVideoWidget`.

10. Audio tracks: WIRED.

   mpv parses `track-list`, emits audio/subtitle track arrays, and maps track selection to `aid` and `sid`. See `MpvBackend.cpp:446-490` and `MpvBackend.cpp:793-817`.

11. Basic audio controls: WIRED.

   Volume, mute, playback rate, and audio delay are mapped to mpv properties. See `MpvBackend.cpp:740-768`.

12. Audio speed and DRC: STUBBED/PARTIAL.

   `sendSetAudioSpeed` maps to mpv `speed`, which changes playback speed rather than proving a sidecar-equivalent audio-only sync path. `sendSetDrcEnabled` overwrites `af` with `acompressor` or clears it, which can collide with other audio filter state. See `MpvBackend.cpp:770-789`.

13. Per-device audio-delay recall metadata: MISSING.

   `VideoPlayer` expects `mediaInfo.audio_device` and host API fields. mpv currently emits a minimal mediaInfo payload with duration and file path only. See `MpvBackend.cpp:304-320` and `VideoPlayer.cpp:3719-3763`.

14. Subtitle tracks, visibility, delay, size, and standard position: WIRED.

   mpv maps these through `sid`, `sub-visibility`, `sub-delay`, `sub-scale`, and `sub-pos`. See `MpvBackend.cpp:793-817`, `MpvBackend.cpp:822-864`, and `MpvBackend.cpp:922-944`. The mpv manual documents these subtitle option families.

15. Subtitle styling baseline: WIRED after Phase 2.

   mpv startup options set Tankoban's baseline subtitle colors, shadow, outline style, and ASS override behavior. `sendSetSubStyle` maps font size, vertical margin, and border size. See `MpvBackend.cpp:151-160` and `MpvBackend.cpp:838-864`.

16. Subtitle Force-position mode and pixel offset: STUBBED.

   `sendSetSubtitlePositionMode("force")` logs a one-time warning and returns without behavior. `sendSetSubtitlePixelOffset` emits the change but does not set mpv state. See `MpvBackend.cpp:866-899` and `MpvBackend.cpp:913-920`.

17. External subtitle file: WIRED.

   `sendLoadExternalSub` calls mpv `sub-add`. See `MpvBackend.cpp:891-899`.

18. External subtitle URL with offset and delay: STUBBED/PARTIAL.

   `sendSetSubtitleUrl` calls `sub-add` for the URL, but ignores the offset and delay parameters. See `MpvBackend.cpp:901-911`.

19. MKV embedded fonts and ASS rendering: NATIVE, with Tankoban style bridge still relevant.

   mpv/libass can handle ASS rendering and embedded fonts internally. Tankoban still needs to decide which style overrides are allowed and how much of the Phase 2 "best possible" subtitle surface is UI-exposed.

20. Media info for HDR, color, chapters, and audio device: MISSING/PARTIAL.

   mpv sends only duration and file path on file load. It does not yet bridge HDR flags, color primaries, transfer characteristics, chapters, or audio device details into Tankoban's `mediaInfo` consumer. See `MpvBackend.cpp:304-320` and `VideoPlayer.cpp:3719-3763`.

21. HDR and tone mapping commands: STUBBED/PARTIAL.

   `sendSetToneMapping` sets mpv `tone-mapping` and maps `peakDetect` through `hdr-peak-decay-rate`. The mpv manual documents `hdr-peak-decay-rate` as a numeric decay-rate option relevant when HDR peak computation is enabled, so this needs validation. See `MpvBackend.cpp:966-972` and the mpv HDR option reference.

22. Structured video filters and EQ: STUBBED.

   `sendSetFilters` currently returns a sequence number and does nothing. `sendRawFilters` can set raw `vf` and `af`, but that is not parity with Tankoban's structured UI/filter path. See `MpvBackend.cpp:948-964`.

23. Zero-copy/canvas/resize methods: STUBBED with mpv-specific rationale.

   `sendSetZeroCopyActive`, `sendSetCanvasSize`, and `sendResize` are no-ops today. The mpv widget path renders into its own OpenGL widget, so not every sidecar texture concept needs a direct mpv equivalent. The remaining question is whether resize, frame timing, and visual chrome behave to Tankoban's standard in all player modes. See `MpvBackend.cpp:974-987`.

24. Buffering overlay and cache state: WIRED.

   Phase 1 wired `paused-for-cache` and `demuxer-cache-state` into Tankoban's buffering overlay and cache-state signal shape. See `MpvBackend.cpp:513-584`.

25. Probe stages: PARTIAL.

   mpv synthesizes several stage signals on file load and playback restart, but it does not mirror the sidecar's detailed probe tier, packet, decoder, and error surfaces. See `MpvBackend.cpp:304-355` and `native_sidecar/src/main.cpp:441-447`.

26. Error surfaces: PARTIAL.

   mpv emits initialization/log-related errors and EOF, but this audit did not find sidecar-equivalent structured open failure, parse failure, recoverable decode error, or process-crash parity in the mpv path. See `IPlayerBackend.h:205-213`, `MpvBackend.cpp:304-355`, and `native_sidecar/src/main.cpp:782-795`.

27. Frame-drop and performance telemetry: MISSING.

   Phase 1 memory explicitly flags frame-drop telemetry as not ported. Source review found render stats logging in `MpvVideoWidget.cpp:198-207`, but not a sidecar-equivalent frame-drop/decoder-health bridge.

28. Persistence: PARTIAL.

   Library and stream progress are mostly backend-agnostic and already work with mpv where the relevant time updates flow. Track preference restore uses backend track IDs and should work for library playback after mpv emits track-list. Subtitle size and standard position restore through backend commands. Force-position mode, pixel offset, audio-device offset recall, and any feature that depends on missing mediaInfo remain partial. See `VideoPlayer.cpp:2797-2994`, `VideoPlayer.cpp:2168`, and `StreamPage.cpp:1971-2231`.

## §3. STANDARD/DEVIATES Gap Analysis

This section uses STANDARD for today's Tankoban ffmpeg behavior and DEVIATES for today's mpv behavior. That is not a quality judgment. Some deviations are desirable because mpv should become the new standard. Subtitle quality is one of those areas.

1. Backend selection and stream routing.

   STANDARD: daily stream playback routes through ffmpeg. Library playback can use mpv by saved preference or explicit backend swap. FORCE_MPV is a dev override.

   DEVIATES: mpv stream playback is proven only through `TANKOBAN_FORCE_MPV=1`. The default stream path still has the §Q4 lock to ffmpeg in `BackendFactory.cpp:38`.

   Effort: small to medium. The policy change is small. The validation burden is medium because this becomes the daily stream route.

2. Core playback.

   STANDARD: open, pause, resume, seek, frame step, stop, loop, EOF, and time updates work through ffmpeg.

   DEVIATES: mpv covers this surface for normal playback. No ship-blocking core playback gap found in source.

   Effort: small. Remaining work is regression smoke, not a new subsystem.

3. Render surface and chrome.

   STANDARD: ffmpeg frames enter `FrameCanvas` through D3D11 texture/SHM paths, and Tankoban chrome is layered around that expected surface.

   DEVIATES: mpv renders through `MpvVideoWidget` using libmpv OpenGL rendering. This is functional, but it is not the same FrameCanvas route. The known HUD reveal/mouse routing edge means UI parity is not closed.

   Effort: medium. The render path exists. The work is chrome parity, resize/focus/mouse behavior, and smoke on real files.

4. Audio tracks and basic audio controls.

   STANDARD: ffmpeg exposes track enumeration/switch, volume, mute, playback rate, audio delay, audio-speed sync, DRC, and audio-device metadata.

   DEVIATES: mpv covers tracks, volume, mute, rate, and audio delay. Audio-speed and DRC are partial. Audio-device metadata is missing from mpv mediaInfo.

   Effort: medium. Audio-device metadata plus filter-chain-safe DRC are the load-bearing items.

5. Subtitles.

   STANDARD: after Phase 2, ffmpeg has Tankoban's custom libass styling path, embedded font loading, Standard/Force positioning, subtitle size/delay persistence, and external subtitle surfaces. It also still carries some older positioning history from the Y-offset pattern called out in memory.

   DEVIATES: mpv now has a strong native subtitle baseline using libass and mpv `sub-*` options. It covers track selection, visibility, delay, style, size, standard position, local external subtitles, and native ASS rendering. It still does not implement Force-position mode, pixel offset, URL subtitle offset/delay, or the deferred "best possible" controls such as outline thickness slider, background plate toggle, font dropdown, explicit force-authored toggle, and max-line handling.

   Effort: small to medium. The remaining bridge work is bounded. The higher external bar is Netflix/Crunchyroll-style subtitle quality, not ffmpeg's old Y-offset behavior.

6. HDR and tone mapping.

   STANDARD: ffmpeg emits HDR/color metadata to `VideoPlayer`, and the sidecar has tone-mapping/color-management command handlers.

   DEVIATES: mpv has native HDR/tone-mapping capabilities, but Tankoban has not bridged enough metadata to prove parity. `sendSetToneMapping` is partial, and the `hdr-peak-decay-rate` mapping needs validation against mpv's documented numeric option shape.

   Effort: heavy. This needs source work plus subjective display validation. Per `feedback_subjective_over_trace.md`, if metrics and eyes disagree, Hemanth's visual read wins.

7. Video filters, EQ, and raw filter control.

   STANDARD: ffmpeg accepts structured filter state and raw filters.

   DEVIATES: mpv raw filter setting exists, but structured `sendSetFilters` is a no-op.

   Effort: medium. The filter graph decisions are technical, but the user-facing contract is already named by `IPlayerBackend`.

8. Media info: chapters, color, duration, audio device.

   STANDARD: ffmpeg emits a rich `media_info` object consumed by `VideoPlayer`.

   DEVIATES: mpv emits only duration and file path at file-load time. Chapters, HDR, color, and audio-device details are not bridged.

   Effort: medium. mpv can expose much of this through properties, but Tankoban needs a translated payload matching the existing `mediaInfo` consumer.

9. Streaming cache and stall telemetry.

   STANDARD: ffmpeg emits cache state, buffering edges, and transparent stall semantics.

   DEVIATES: mpv now maps `paused-for-cache` and `demuxer-cache-state` into Tankoban's cache/buffering shape. The evidence file confirms the steady-state stream surface. Targeted stall induction and default-route stream smoke are still needed.

   Effort: small to medium. The implementation exists; the remaining work is confidence and default-route removal.

10. Error handling.

   STANDARD: ffmpeg sidecar can report process crashes, structured errors, decode errors, EOF, probe failure, and close events.

   DEVIATES: mpv is in-process, so process-crash semantics do not map one-to-one. That is acceptable, but Tankoban still needs structured open/decode/network error surfaces that make UI failures understandable.

   Effort: medium. The work is translation and test coverage, not media decoding itself.

11. Diagnostics and telemetry.

   STANDARD: ffmpeg has process-boundary logs, probe stages, decode-error events, cache-state events, and sidecar-focused performance hooks.

   DEVIATES: mpv has render stats and cache-state wiring, but no frame-drop telemetry, no equivalent diagnostic bundle, and no clear "why did playback fail" surface for support.

   Effort: medium.

12. Persistence.

   STANDARD: ffmpeg provides enough state for library progress, stream progress, track prefs, subtitle prefs, subtitle position/size, and per-device audio delay.

   DEVIATES: mpv covers progress and much of track/subtitle restore. It does not yet support the missing Force/pixel-offset subtitle pieces or audio-device offset recall.

   Effort: small to medium after mediaInfo work lands.

13. Decommission readiness.

   STANDARD: Tankoban currently carries both backends, a saved backend preference, right-click "Play with X" choice surface, FORCE_MPV override, and a stream-mode lock.

   DEVIATES: mpv cannot be the sole backend until those surfaces are removed or repurposed after feature parity closes.

   Effort: medium to heavy. The code removal is straightforward only after behavior is already proven.

Hypothesis - the largest risk is not libmpv playback itself; it is the Tankoban-specific bridge around mediaInfo, HDR, filters, errors, and chrome parity. Agent 3 to validate.

## §4. Verdict

READY WITH CONDITIONS.

mpv should not replace ffmpeg today. It can become Tankoban's sole player if the remaining bridge gaps close first.

The current mpv backend already plays library files. It can also play stream-server HTTP URLs under FORCE_MPV with buffering overlay, cache-state telemetry, transparent stall semantics, and stream progress persistence. Phase 2 also moved subtitle quality forward.

The blockers are not basic playback. The blockers are the Tankoban contract around the player:

1. Stream mode is still locked to ffmpeg in the daily route.
2. mpv mediaInfo is too thin for HDR, chapters, and per-device audio-delay recall.
3. HDR/tone mapping has not been evaluated to the Tankoban bar.
4. Structured filters and EQ are not wired.
5. Audio-speed/DRC behavior is partial.
6. Subtitle Force-position, pixel offset, and URL subtitle offset/delay are not wired.
7. Error and diagnostic surfaces are not at ffmpeg's current standard.
8. HUD reveal/focus/mouse behavior under `MpvVideoWidget` still needs closure.
9. Frame-drop/performance telemetry is not ported.
10. The dual-backend choice surfaces cannot be removed until the above pass validation.

That makes immediate decommission premature. It does not argue for keeping dual-backend permanently.

## §5. Gap Closure Plan

Recommended target: stage toward mpv-only. Close the load-bearing bridge gaps, switch daily routing, then remove ffmpeg.

Phase A - ship blockers before mpv can become default.

1. Default stream routing cutover.

   Target: ffmpeg's current daily stream behavior, using mpv as the backend.

   Work area: `src/ui/player/BackendFactory.cpp`, `src/ui/player/VideoPlayer.cpp`, `src/ui/pages/StreamPage.cpp`, `src/ui/pages/stream/StreamPlayerController.cpp`, `src/ui/player/MpvBackend.cpp`.

   Effort: small to medium.

   Dependencies: targeted stream stall smoke, default-route stream smoke, saved-progress smoke, buffered-range HUD smoke.

2. mpv mediaInfo bridge.

   Target: ffmpeg's current `media_info` payload consumed by `VideoPlayer`.

   Work area: `src/ui/player/MpvBackend.cpp`, possibly `src/ui/player/MpvBackend.h`, and `VideoPlayer.cpp` only if the existing consumer needs backend-neutral cleanup.

   Effort: medium.

   Dependencies: define mpv property/event sources for HDR flag, color primaries, transfer characteristics, chapters, duration, filename, and audio-device fields. Audio-device may need a non-mpv Qt or platform query because mpv may not expose the same host API shape Tankoban expects.

3. HDR and tone-mapping parity.

   Target: external quality bar plus current ffmpeg metadata-driven behavior. For HDR, the bar is "looks correct on Hemanth's display", not just a property trace.

   Work area: `MpvBackend.cpp`, `MpvVideoWidget.cpp`, `VideoPlayer.cpp`, and any canvas/HDR bridge touched by the current ffmpeg path.

   Effort: heavy.

   Dependencies: mediaInfo bridge first. Then compare SDR and HDR files under both backends. Validate mpv tone-mapping options against mpv manual semantics before exposing them as equivalent controls.

4. Structured filters and EQ.

   Target: ffmpeg's current structured `sendSetFilters` behavior.

   Work area: `MpvBackend.cpp`.

   Effort: medium.

   Dependencies: decide the mpv `vf`/`af` chain composition rules so `sendSetFilters`, `sendRawFilters`, DRC, and audio filters do not overwrite each other.

5. Error and diagnostic surface.

   Target: ffmpeg's current user-visible and log-visible failure semantics.

   Work area: `MpvBackend.cpp`, maybe `VideoPlayer.cpp` if UI handling assumes sidecar-shaped process failures.

   Effort: medium.

   Dependencies: exercise file-not-found, unsupported codec, broken subtitle URL, network drop, EOF, and stream-server interruption.

Phase B - close user-facing parity deltas.

6. Subtitle residuals.

   Target: best-possible subtitle rendering, with Netflix and Crunchyroll as the external quality benchmark. Do not preserve the old Y-offset pattern as the target when it conflicts with authored subtitle layout.

   Work area: `MpvBackend.cpp`, `SubtitlePopover.cpp`, `SettingsPopover.cpp`, and related persistence keys in `VideoPlayer.cpp`.

   Effort: small to medium.

   Dependencies: Agent 3's subtitle context at `agents/chat.md:2170-2217` plus Phase 2 close notes. Required mpv-side closures are Force-position mode decision, pixel offset behavior if still exposed, URL subtitle offset/delay, and persistence for deferred style controls if they become product surface.

7. Audio polish.

   Target: ffmpeg's current user-facing audio control surface.

   Work area: `MpvBackend.cpp`, `VideoPlayer.cpp`.

   Effort: medium.

   Dependencies: filter-chain composition from Phase A. Per-device audio-delay recall depends on mediaInfo or a replacement audio-device source.

8. HUD, focus, mouse, and keyboard parity.

   Target: existing ffmpeg player chrome behavior.

   Work area: `MpvVideoWidget.cpp`, `MpvVideoWidget.h`, `VideoPlayer.cpp`.

   Effort: small to medium.

   Dependencies: visual smoke on hover reveal, right-click menu, fullscreen, close button, popovers, keyboard shortcuts, and stream HUD buffered ranges.

9. Frame-drop and performance telemetry.

   Target: enough mpv telemetry to trust subjective playback reports and diagnose regressions.

   Work area: `MpvBackend.cpp`, `MpvVideoWidget.cpp`, debug tooling.

   Effort: medium.

   Dependencies: decide which mpv properties become the standard diagnostic set. Good candidates are frame drop, mistimed frame, decoder state, cache state, and render callback timing where available.

Phase C - cutover and remove dual-backend surface.

10. mpv default switch.

   Target: mpv is the normal backend for library and stream playback. ffmpeg remains temporarily available only as an emergency fallback during the final validation window.

   Work area: `BackendFactory.cpp`, settings persistence, right-click backend menu, launch scripts, logs.

   Effort: small to medium.

   Dependencies: Phases A and B closed.

11. ffmpeg sidecar decommission.

   Target: one player backend.

   Work area: sidecar build wiring, `SidecarProcess`, backend selection UI, sidecar resources, native sidecar packaging, docs/TODO state.

   Effort: medium to heavy.

   Dependencies: mpv default survives library playback, stream playback, subtitles, HDR, filters, errors, and persistence smokes. Do not start deletion before default mpv has had a validation window.

## §6. Strategic Recommendation

Recommend path (c): stage toward mpv-only.

Do not keep dual-backend permanently. The dual state has real cost:

1. Every player feature now needs two implementations or a documented exception.
2. Stream mode has a special lock that hides behavior from normal users.
3. The right-click "Play with X" surface exposes implementation detail.
4. Bugs can split by backend and become harder to reproduce.
5. The ffmpeg sidecar adds process lifecycle, packaging, IPC, and native maintenance load.

Do not decommission ffmpeg immediately either. mpv is not ready to be sole backend today because several Tankoban-specific contracts are still partial.

The right call is staged commitment:

1. Treat mpv-only as the intended destination.
2. Close Phase A blockers first: stream default route, mediaInfo, HDR, filters, and errors.
3. Close Phase B user-facing gaps: subtitles, audio polish, chrome parity, diagnostics.
4. Make mpv the default for a validation window.
5. If validation holds, remove the ffmpeg sidecar and delete the dual-backend choice surface.

This keeps the brotherhood focused without pretending today's mpv path is already complete.

