# Audit - MAKE_FFMPEG_BEAT_MPV - 2026-05-04

By Agent 7 (Codex). For Agent 3 (Video Player) and Hemanth.

Reference comparison: Tankoban ffmpeg sidecar, Tankoban mpv backend, libplacebo documentation, mpv manual, FFmpeg libswscale docs, Microsoft DXGI waitable-swapchain docs.

Scope: This audit compares the current Tankoban ffmpeg and mpv playback pipelines after the MAKE_MPV_BEAT_FFMPEG arc. It focuses on SDR picture quality, HDR parity, frame pacing, IPC observability, and the strategic sidecar-vs-in-process question. It does not prescribe implementation fixes; the execution plan lives in `MAKE_FFMPEG_BEAT_MPV.md`.

## Observed behavior in Tankoban

### O1 - Empirical compare-mode baseline

Hemanth's side-by-side compare-mode smoke on 2026-05-04 used:

- Fixture: `C:\Users\Suprabha\Desktop\Media\TV\Sports\Virat Kohli 141(175) Vs Australia 1st Test 2014 Ball By Ball.mp4`
- Harness: `scripts\compare-mpv.bat` and `scripts\compare-ffmpeg.bat`
- Verdict recorded in `agents/chat.md`: mpv was smoother; ffmpeg stuttered.

The latest mpv telemetry for that cricket file shows 21 samples over 105 seconds at 30.00 fps, with `total_drops=0`, `total_vo_delayed=0`, and no buffering ticks (`out/mpv_telemetry.log:1359-1381`).

The latest ffmpeg-side IPC summary from the same time window shows command round-trip latency, not per-frame jitter: `set_audio_speed` had 102 samples with p50 45 ms, p99 264 ms, max 330 ms; `set_canvas_size` had one 118 ms sample (`out/ipc_latency.log:299-303`). This log proves control-message latency exists during the ffmpeg run, but it does not directly prove per-frame transport jitter.

### O2 - ffmpeg SDR default path is still swscale fast bilinear

The ffmpeg sidecar creates `GpuRenderer` only for HDR or when SDR libplacebo is enabled by `TANKOBAN_LIBPLACEBO_SDR=1` (`native_sidecar/src/main.cpp:904-924`). The env gate is cached by `libplacebo_sdr_enabled()` (`native_sidecar/src/main.cpp:347-353`).

When `GpuRenderer` is absent or fails, `VideoDecoder` performs the final conversion through `sws_getContext(..., SWS_FAST_BILINEAR, ...)` and `sws_scale` into BGRA (`native_sidecar/src/video_decoder.cpp:1085-1108`). High-bit-depth SDR also takes an earlier `SWS_FAST_BILINEAR` conversion to 8-bit YUV before the BGRA conversion when the GPU renderer is not active (`native_sidecar/src/video_decoder.cpp:1001-1040`).

### O3 - ffmpeg HDR/env-SDR path uses libplacebo, but downloads BGRA back to CPU

`GpuRenderer::init()` creates Vulkan/libplacebo and sets `pl_render_default_params` with `pl_filter_ewa_lanczossharp` upscaler and `pl_filter_hermite` downscaler (`native_sidecar/src/gpu_renderer.cpp:73-115`). `GpuRenderer::render_frame()` maps the incoming AVFrame to libplacebo textures, renders to a BGRA target, then downloads the rendered texture into CPU memory with `pl_tex_download` (`native_sidecar/src/gpu_renderer.cpp:119-215`).

This means the ffmpeg libplacebo path improves scaler/color quality, but still returns to the sidecar's BGRA/FrameCanvas transport shape.

### O4 - mpv now uses an in-process libplacebo composite with matching high-quality scalers

The mpv backend initializes `vo=libmpv`, defaults `hwdec=no`, and documents why the old `d3d11va-copy` baseline dropped frames on Intel UHD 620 (`src/ui/player/MpvBackend.cpp:220-257`). The current mpv render path attaches `MpvLibplaceboRenderer` to the libmpv render context (`src/ui/player/MpvBackend.cpp:411-415`).

The mpv libplacebo composite uses `pl_render_default_params` plus `pl_filter_ewa_lanczossharp` and `pl_filter_hermite`, matching the sidecar's high-quality scaler family (`src/ui/player/MpvLibplaceboRenderer.cpp:1040-1067`). It renders directly into the mpv Vulkan widget's swapchain path rather than returning frames through the ffmpeg sidecar IPC path.

### O5 - ffmpeg has a partial D3D11 zero-copy path, but it is conditional

The sidecar copies D3D11VA decoded texture slices into a shared D3D11 texture through `D3D11Presenter::present_slice()` (`native_sidecar/src/d3d11_presenter.cpp:80-113`). The shared texture is created with `D3D11_RESOURCE_MISC_SHARED_NTHANDLE` and exported with `CreateSharedHandle` (`native_sidecar/src/d3d11_presenter.cpp:137-180`).

The decoder only short-circuits the CPU/SHM path when zero-copy is active, the D3D11 copy succeeded, no active video filter blocks it, and subtitles are either not needed or the overlay path is ready (`native_sidecar/src/video_decoder.cpp:858-872`). The main app receives `d3d11_texture` and attaches it to `FrameCanvas` (`src/ui/player/SidecarProcess.cpp:614-620`; `src/ui/player/VideoPlayer.cpp:4268-4274`).

### O6 - FrameCanvas has mature waitable-swapchain pacing and telemetry hooks

`FrameCanvas` creates a waitable swap chain with `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` (`src/ui/player/FrameCanvas.cpp:237-246`), calls `SetMaximumFrameLatency(1)`, and uses `GetFrameLatencyWaitableObject()` to drive rendering from a wait thread (`src/ui/player/FrameCanvas.cpp:509-586`). It logs one-second `[PERF]` summaries with timer interval, draw time, present time, skipped presents, and DXGI queue estimates (`src/ui/player/FrameCanvas.cpp:842-920`).

FrameCanvas also contains latency-derived skip-next logic (`src/ui/player/FrameCanvas.cpp:807-819`) and can log chosen frame id, fallback usage, producer drops, and consumer lateness through `VsyncTimingLogger` when enabled (`src/ui/player/FrameCanvas.cpp:821-839`).

### O7 - ffmpeg command IPC telemetry exists, but per-frame ffmpeg telemetry is incomplete

`SidecarProcess::sendCommand()` stamps send time per command and `recordIpcAck()` records command ack latency (`src/ui/player/SidecarProcess.cpp:153-179`; `src/ui/player/SidecarProcess.cpp:989-998`). `dumpIpcLatency()` writes per-command p50/p99/max summaries to `out/ipc_latency.log` (`src/ui/player/SidecarProcess.cpp:1001-1056`).

This is useful for control-plane latency. It does not by itself show whether the frame producer is missing deadlines, whether the main app consumes stale frames, whether zero-copy is active for every steady-state frame, or whether the waitable loop is skipping presents during the visible stutter window.

### O8 - mpv old hwdec-copy baseline shows why telemetry matters

The archived mpv `d3d11va-copy` baseline on Community S01E01 produced 404 total drops over 260 seconds (`out/mpv_telemetry_baseline_d3d11va-copy.log:1-54`). Later mpv CPU-decode/libplacebo work reduced this class of drop in the new pipeline. This is a precedent for treating hard telemetry as the gate before architectural deletion or replacement decisions.

## Reference behavior and external constraints

### R1 - libplacebo presets

libplacebo documents `pl_render_fast_params` as the fastest configuration, `pl_render_default_params` as recommended defaults with somewhat higher quality scaling and dithering, and `pl_render_high_quality_params` as intended for higher-end machines with more advanced processing. Source: https://libplacebo.org/renderer/

libplacebo's options page documents the `high_quality` preset as setting the upscaler to `ewa_lanczossharp` and enabling debanding, with peak detection and debanding both noted as potentially heavy on weaker devices. Source: https://libplacebo.org/options/

### R2 - mpv scaler and pacing controls

mpv documents GPU renderer options including `--scale=<filter>`, identifies bilinear as fastest and low quality, and documents video-sync/display-resample behavior and interpolation constraints for display-paced playback. Source: https://mpv.io/manual/stable/

### R3 - FFmpeg swscale

FFmpeg documents libswscale as its color conversion and scaling library and enumerates `SWS_FAST_BILINEAR`, `SWS_BILINEAR`, `SWS_BICUBIC`, `SWS_LANCZOS`, `SWS_SPLINE`, and related flags. Source: https://www.ffmpeg.org/doxygen/7.1/group__libsws.html

### R4 - DXGI waitable swapchain constraints

Microsoft documents that `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` must be specified at swapchain creation and kept consistent through resize, and that waitable swapchains use per-swapchain latency with default latency 1 to avoid queuing more than one frame. Source: https://learn.microsoft.com/en-us/windows/uwp/gaming/reduce-latency-with-dxgi-1-3-swap-chains

## Gaps ranked P0 / P1 / P2

### P0 - ffmpeg currently loses the live cricket smoothness comparison

Observed: Hemanth's compare-mode verdict was mpv smoother and ffmpeg stuttering; the latest mpv cricket telemetry shows 0 total drops over 21 samples while the ffmpeg-side log only exposes command IPC latency, including `set_audio_speed` p99 264 ms (`agents/chat.md` REQUEST AUDIT block; `out/mpv_telemetry.log:1359-1381`; `out/ipc_latency.log:299-303`).

Reference: mpv's current Tankoban path is in-process libmpv plus libplacebo composite (`src/ui/player/MpvBackend.cpp:411-415`; `src/ui/player/MpvLibplaceboRenderer.cpp:1040-1067`). DXGI guidance supports low queue depth for latency-sensitive rendering (`src/ui/player/FrameCanvas.cpp:530-551`; Microsoft DXGI waitable-swapchain docs).

Impact: The strategic premise of MAKE_FFMPEG_BEAT_MPV is not met until ffmpeg is visibly smoother than mpv on the standing cricket fixture.

### P0 - ffmpeg SDR quality path is below mpv by default

Observed: ffmpeg SDR still uses `SWS_FAST_BILINEAR` unless `TANKOBAN_LIBPLACEBO_SDR=1` is set (`native_sidecar/src/main.cpp:904-924`; `native_sidecar/src/video_decoder.cpp:1085-1108`). mpv already uses libplacebo with high-quality scalers by default (`src/ui/player/MpvLibplaceboRenderer.cpp:1040-1067`).

Reference: libplacebo documents default and high-quality renderer presets and identifies high-quality upscaling as a first-class render setting. FFmpeg documents `SWS_FAST_BILINEAR` as one scaler constant among higher-quality alternatives such as bicubic, lanczos, and spline.

Impact: ffmpeg cannot reliably beat mpv's SDR picture quality while its default SDR path remains the cheaper scaler.

### P1 - ffmpeg has zero-copy infrastructure, but no current proof it is active throughout the stutter case

Observed: `present_slice()` and `d3d11_texture` attach exist (`native_sidecar/src/d3d11_presenter.cpp:80-180`; `src/ui/player/VideoPlayer.cpp:4268-4274`), but the fast path is conditional on zero-copy active state, D3D11 copy success, filters, and subtitle overlay readiness (`native_sidecar/src/video_decoder.cpp:858-872`).

Reference: The current mpv path avoids this cross-process D3D11/SHM decision tree for video presentation. DXGI waitable-swapchain guidance emphasizes tight frame queue control, but the ffmpeg path needs evidence that producer and consumer remain aligned.

Impact: Without explicit per-frame evidence, it is not clear whether stutter is decode, copy, IPC, SHM fallback, subtitle overlay gating, audio-speed feedback, or present cadence.

### P1 - IPC telemetry is control-plane only

Observed: `out/ipc_latency.log` measures JSON command ack round-trips (`src/ui/player/SidecarProcess.cpp:153-179`; `src/ui/player/SidecarProcess.cpp:989-1056`), and the latest session shows high `set_audio_speed` latency (`out/ipc_latency.log:299-303`). It does not record frame arrival cadence or display-time lateness.

Reference: `FrameCanvas` and `VsyncTimingLogger` already have fields for chosen frame id, fallback usage, producer drops, and consumer lateness (`src/ui/player/FrameCanvas.cpp:821-839`).

Impact: Current logs can show command churn but cannot close the root pacing question.

### P1 - HDR parity is likely close, but not proven identical

Observed: ffmpeg `GpuRenderer` and mpv `MpvLibplaceboRenderer` both use libplacebo and the same named scaler pair (`native_sidecar/src/gpu_renderer.cpp:108-114`; `src/ui/player/MpvLibplaceboRenderer.cpp:1040-1067`). ffmpeg renders into an 8-bit BGRA CPU download target (`native_sidecar/src/gpu_renderer.cpp:169-183`; `native_sidecar/src/gpu_renderer.cpp:202-210`), while mpv renders through the Vulkan widget's swapchain path (`src/ui/player/MpvLibplaceboRenderer.cpp:787-815`; `src/ui/player/MpvLibplaceboRenderer.cpp:1040-1067`).

Reference: libplacebo color mapping, peak detection, dithering, and high-quality presets can materially affect output, especially around HDR and quantization.

Impact: "Both use libplacebo" is not enough to guarantee identical HDR/tone-map behavior.

### P2 - Audio/AV sync is coupled to the ffmpeg smoothness problem

Observed: `set_audio_speed` is sent through command IPC and is tied to SyncClock-derived render-latency feedback (`native_sidecar/src/main.cpp:1509-1527`; `out/ipc_latency.log:302`). This control loop is not the same surface as picture quality, but the latest compare session's heaviest command traffic is audio-speed updates.

Reference: mpv's manual documents display-paced video-sync behavior and the sensitivity of interpolation/resample logic to speed and display rate changes.

Impact: Even if audio quality is out of direct scope, AV-sync correction traffic may interact with perceived video smoothness.

## Hypothesized root causes (Agent 3 to validate)

- **Hypothesis -** ffmpeg stutter on the cricket clip is partly caused by the sidecar path falling out of D3D11 zero-copy during steady-state playback, forcing CPU conversion or SHM fallback on frames that should stay GPU-side. **Agent 3 to validate.**

- **Hypothesis -** high-frequency `set_audio_speed` updates are adding enough command-loop work or thread contention to disturb ffmpeg sidecar pacing during heavy playback, even though the log only measures command latency and not frame jitter. **Agent 3 to validate.**

- **Hypothesis -** ffmpeg's SDR quality gap is primarily the default swscale path, not libplacebo quality itself, because the sidecar's env-gated libplacebo path and the mpv libplacebo path already share the same named scaler family. **Agent 3 to validate.**

- **Hypothesis -** the sidecar does not need to be killed unless per-frame telemetry still shows visible pacing defects after SDR libplacebo defaulting, zero-copy verification, and command-churn reduction. **Agent 3 to validate.**

- **Hypothesis -** HDR parity differences, if visible later, will come from target format, color mapping, peak detection, dithering, or CPU download/output conversion differences rather than the scaler names alone. **Agent 3 to validate.**

## Strategic call: sidecar IPC fixes vs in-process ffmpeg

### Observations

The strongest argument for keeping ffmpeg is integration. The ffmpeg backend already renders through `FrameCanvas`, which has mature waitable-swapchain pacing, screenshot, overlay, aspect/crop, subtitle positioning, and HUD behavior. Hemanth's stated preference is to keep that integration while lifting quality and smoothness.

The strongest argument against the sidecar is architectural latency and complexity. ffmpeg decode/render lives out of process, uses JSON-over-stdin for control, uses shared-memory and/or D3D11 shared textures for frames, and has several state gates before the fast path is active. mpv is now in-process, so it has fewer cross-process timing boundaries.

Current evidence does not yet prove IPC is irreducible. The latest ffmpeg log shows command IPC latency, not frame transport jitter. The code already contains partial D3D11 zero-copy and FrameCanvas timing telemetry. Those should be measured and hardened before choosing the heavier in-process libavcodec/libavformat rewrite.

### Strategic assessment

Default strategic assessment: incremental sidecar fixes first.

Decision gate for in-process ffmpeg: only revisit linking libavcodec/libavformat directly into Tankoban if Tasks 1-7 in `MAKE_FFMPEG_BEAT_MPV.md` still leave ffmpeg visibly behind mpv while logs prove SDR libplacebo is active, zero-copy is active, audio-speed command churn is controlled, and FrameCanvas is not selecting stale frames.

In-process ffmpeg would eliminate the sidecar IPC class, but it also removes process isolation, increases crash blast radius, and increases build/packaging complexity around FFmpeg DLLs and MSVC compatibility. That trade is not justified by the current evidence alone.

## Recommended follow-ups (advisory)

The concrete follow-up tasks are documented in `MAKE_FFMPEG_BEAT_MPV.md`. The audit-level recommendation is to treat those tasks as evidence gates: first prove the SDR libplacebo path, zero-copy state, command churn, and FrameCanvas pacing with logs; then decide whether sidecar IPC remains the blocker.

## Plan handoff

The concrete work plan is `MAKE_FFMPEG_BEAT_MPV.md`. Each task cites the audit finding that justifies it and keeps Hemanth's smoke instructions plain-English.
