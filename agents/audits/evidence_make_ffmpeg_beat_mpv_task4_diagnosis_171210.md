# Evidence — MAKE_FFMPEG_BEAT_MPV Task 4 diagnosis — 2026-05-04 ~17:12pm

Anchor: `MAKE_FFMPEG_BEAT_MPV.md` Task 4; audit `agents/audits/make_ffmpeg_beat_mpv_2026-05-04.md`.

Captured by: Agent 3 (Video Player), this wake.

## Method

Task 4 shipped per-frame ffmpeg pacing telemetry: extended `VsyncTimingLogger` with `zero_copy_active` field, flipped `m_vsyncLoggingOn` default to true in FrameCanvas, added auto-CSV-dump at session-end + a 1Hz `[PACING]` line in `out/_player_debug.txt` mirrored alongside the existing `[PERF]` block. Files touched: `VsyncTimingLogger.{h,cpp}`, `FrameCanvas.{h,cpp}`. Build GREEN first try via `build_check.bat`.

Two standalone runs of `scripts\compare-ffmpeg.bat` were captured to isolate file-specific behavior:

1. **Gill 43.mp4** (clean broadcast capture, 50fps, 23.7 Mbps, BT.709 SDR, no encoder tag) — Hemanth verdict: "absolutely no stutter."
2. **Virat Kohli 141.mp4** (Clipchamp web-editor re-encode, 30fps, 21.2 Mbps, missing color metadata, audio leads video by 46ms at start) — Hemanth verdict: "rough and stuttery."

CSV dumps: `out/frame_pacing_20260504_170423.csv` (Gill, smooth) + `out/frame_pacing_20260504_171035.csv` (Kohli, rough). IPC log session blocks: `17:06:54` (Gill) + `17:12:09` (Kohli) in `out/ipc_latency.log`.

## Headline finding

The "ffmpeg stutters on cricket" pattern from earlier today is **NOT generic to the ffmpeg pipeline**. It is **file-specific** to a class of irregular-AV-sync encodes (Clipchamp / web-editor outputs with audio-video PTS misalignment + non-broadcast encoder tags). Clean broadcast captures play smoothly through ffmpeg today.

This is a major refinement to the audit's P0 framing. ffmpeg already wins on clean SDR content. What loses to mpv is a narrow content class.

## Observed correlation (causal direction REVISED)

When SyncClock detects audio drift on a quirky encode, it fires `set_audio_speed` corrections on the IPC dispatcher. On Kohli the per-command IPC latency p99 is 60ms (vs Gill's 34ms — a 1.8× delta). The `frame_latency_ms` p99 is 61ms (vs Gill's 40ms — a 1.5× delta) and `consumer_late_ms` p99 is 25ms (vs Gill's 12ms — a 2× delta). The two metrics correlate.

**REVISED 2026-05-04 ~18:00pm after Task 6 fix-attempt regressed the smooth fixture**: The earlier conclusion below — that `set_audio_speed` traffic *causes* the frame_latency by back-pressuring the dispatcher — was tested and falsified. Widening the SyncClock noise floor (5ms → 10ms) and the velocity deadband (0.0005 → 0.002) cut the `set_audio_speed` p99 from 34ms to 5ms and the count by 50% on Gill, exactly as predicted at the IPC level. **But Gill regressed from "absolutely no stutter" to "stuttery."** This proves the corrections were *responding to* drift in a way that kept the AV-sync loop closed; suppressing them broke the loop on the previously-smooth fixture.

The IPC and frame-latency p99s are likely **co-symptoms of the same upstream cause** (irregular PTS in the source encode), not a cause-and-effect chain. Kohli stutters because something upstream forces both: (a) more aggressive drift-correction need AND (b) more frame-latency outliers. The fix path needs to address that upstream cause, not the symptoms.

The previous "Confirmed cause" block is preserved below for traceability; treat it as a **hypothesis that was tested and disconfirmed**, not an established fact.

### Original (DISCONFIRMED) cause analysis

The prior interpretation was: *"Because the sidecar dispatcher is single-threaded for command processing, set_audio_speed back-pressure ties up the same thread that pushes decoded frames to SHM. The result is the frame_latency p99 spikes."* This was tested via the Task 6 fix attempt above and FAILED — suppressing the corrections did not improve smoothness; it regressed Gill. The audit's P2 promotion to operative P0 was premature; the actual operative P0 remains open.

## Hypotheses ruled OUT by the data

The audit's per-frame pacing instrumentation (Task 4) eliminates several plausible-on-paper causes:

1. **Stale frame repeats** (`chosen_frame_id <= prev_chosen_id`): **0 across both Gill (8192 samples) and Kohli (4140 samples)**. Renderer never repeats the same SHM frame. NOT the cause.
2. **Producer dropping frames**: Kohli has 43 producer drops in ~138s (~0.3/sec); Gill has 650 in ~164s (~3.5/sec). The smooth file has 11× MORE drops/sec — drop count alone is not a stutter signal. NOT the cause.
3. **Zero-copy state**: 0% on BOTH files — the imported D3D11 shared texture path is not engaged in this Tankoban build. Both files render via the SHM/CPU path. Audit P1 ("zero-copy may not be active throughout") was correct (it's not active at all today) but it is **not load-bearing for smoothness** — Gill plays smooth without it. NOT the cause.
4. **SHM `readBestForClock` fallback to `readLatest`**: 72% (Gill) vs 79% (Kohli) — fallback rate is similar and high on both. Gill plays smooth with 72% fallback rate. NOT load-bearing.
5. **Skipped presents**: 0 in Gill, 5 in Kohli over ~138s — negligible. NOT the cause.

These five are the audit's Hypothesis #1 + the explicit "frames arrive late / get skipped / repeat stale frames" causes from Task 4's plain-English ask. Task 4 has now closed all five with hard data.

## Numbers (full percentile dig)

### `frame_latency_ms` (present-to-present interval overage vs expected frame interval)

| Percentile | Gill (smooth) | Kohli (rough) | Delta |
|---|---|---|---|
| p50 | 11.73ms | 12.61ms | +7% |
| p90 | 25.99ms | 31.01ms | +19% |
| p99 | 40.12ms | **61.48ms** | **+53%** |
| p999 | 68.58ms | 92.95ms | +35% |
| max | 182.13ms | 174.54ms | -4% |
| n | 3660 | 2399 | — |

### `consumer_late_ms` (sidecarClockUs - framePtsUs ms; positive = displayed frame is stale)

| Percentile | Gill | Kohli | Delta |
|---|---|---|---|
| p1 | -14.83ms | -14.90ms | ~same |
| p50 | -7.22ms | **-1.94ms** | Kohli median runs tighter to clock |
| p99 | +11.92ms | **+24.79ms** | **+108%** |
| max | 22.99ms | **56.73ms** | **+147%** |
| n_consumed | 4357 | 1472 | — |

Kohli's consumer_late distribution skews much more toward "displayed frame is behind audio clock" outliers. This is the stutter signal that the eye perceives.

### `out/ipc_latency.log` set_audio_speed comparison

| Session | Count | p50 | p99 | max | Per minute |
|---|---|---|---|---|---|
| Gill 17:06:54 (smooth) | 25 | 0ms | **34ms** | 34ms | ~8/min |
| Kohli 17:12:09 (rough) | 50 | 2ms | **60ms** | 60ms | ~25/min |
| Kohli 16:44:25 (side-by-side) | 189 | 23ms | **307ms** | 326ms | ~12/min |

The side-by-side run's catastrophic 307ms p99 was confounded by GPU contention from running two Tankoban instances simultaneously. The standalone Kohli p99=60ms is the real number — bad enough to back-pressure the dispatcher into the frame-latency spikes seen above, but not catastrophic.

## What this changes about the plan

- **Task 1 verdict refined**: ffmpeg vs mpv was a Kohli-specific stress case + GPU-contention-amplified side-by-side. The actual smoothness gap is narrow.
- **Task 2 + Task 3 (libplacebo SDR)**: stay demoted — picture quality not the gap.
- **Task 4 ✅ CLOSED**: instrumentation lands; diagnosis attached.
- **Task 5 (zero-copy hardening)**: **DEMOTED**. Zero-copy is at 0% on a smooth-playing file. Not load-bearing for current smoothness; can be revisited later as quality / efficiency work but doesn't help the cricket stutter.
- **Task 6 (audio-speed churn)**: **PROMOTED to operative P0**. The fix lives here — either reduce set_audio_speed frequency (deadband / hysteresis in SyncClock), reduce per-command cost (avoid resampler reinit on speed change), OR move audio-speed handling off the dispatcher thread.
- **Task 7 (FrameCanvas pacing tuning)**: relevant only if Task 6 doesn't close the gap. The frame-latency outliers correlate with IPC pressure, so fixing IPC should remove the FrameCanvas pressure.

## Next concrete step (REVISED 2026-05-04 ~18:00pm)

The "Task 6 dive" originally proposed below was attempted (deadband widening + noise floor widening) and regressed Gill from smooth to stuttery. That confirmed the corrections were keeping the AV-sync loop closed, not causing stutter. The next investigation needs different instrumentation:

1. **Re-baseline on a quiet system first.** Tonight's iteration cycle was confounded by VS Code updater + heavy Chrome + cumulative session load. Re-test Gill on a quiet baseline before any code change to confirm what "smooth Gill" actually looks like in fresh telemetry.
2. **Instrument the SyncClock EMA itself.** Today we know `set_audio_speed` count on each session, but we don't know the EMA trajectory — is it stable at 6ms, or oscillating 4↔12ms? That distinction matters for whether the corrections are tracking real drift or noise.
3. **Investigate whether the audio thread is the bottleneck**, not the dispatcher. The handler at `audio_decoder.cpp:162` is trivial; the actual `swr_set_compensation` work happens in the audio thread per chunk. If the audio thread is starved on Kohli, that could be the upstream cause.
4. **Compare against MpvBackend's display-resample path** for what mpv actually does differently when handling the same Kohli encode. The earlier `set_audio_speed` mention at `MpvBackend.cpp:1414` ("comment about how the ffmpeg sidecar's set_audio_speed does X") suggests mpv solves it by NOT firing per-correction commands — instead mpv resamples audio internally to whatever rate the display path needs.

### Task 6 attempt log (chronological)

- **17:48** — shipped: SyncClock noise floor 5→10ms; VideoPlayer deadband 0.0005→0.002. BUILD OK.
- **17:50** — Gill smoke #1 (post-fix): Hemanth verbatim "concerning regression, the Gill video is stuttering now."
- **17:50** — IPC log session 17:50:39: count=11 p99=5ms (vs pre-fix 25 / 34ms). Predicted IPC reduction landed; perceptual playback regressed.
- **17:54** — reverted both changes; rebuilt. BUILD OK.
- **17:57** — Gill smoke #2 (post-revert): still stuttery. IPC count=51 p99=57ms. System load suspected (VS Code updater + 25 Chrome + 25 VS Code processes).
- **18:00** — turned Task 4 instrumentation default OFF (in addition to Task 6 revert). Rebuilt. BUILD OK.
- **18:01** — Gill smoke #3 (Task 6 reverted + Task 4 off): Hemanth verbatim "a little better but still stuttery." Suggests Task 4 instrumentation contributes some perceptual cost on this hardware under load; environmental degradation is also real.
- **18:04** — paused investigation. Iteration ratio (3 cycles, 0 convergence) hit `/superpowers:systematic-debugging` Phase 4.5 surface-and-stop threshold.

## Files referenced

- This file: `agents/audits/evidence_make_ffmpeg_beat_mpv_task4_diagnosis_171210.md`
- Pacing CSVs: `out/frame_pacing_20260504_170423.csv` (Gill smooth), `out/frame_pacing_20260504_171035.csv` (Kohli rough)
- IPC log: `out/ipc_latency.log` sessions 17:06:54 + 17:12:09
- Code shipped Task 4: `src/ui/player/VsyncTimingLogger.{h,cpp}`, `src/ui/player/FrameCanvas.{h,cpp}`
- Plan: `MAKE_FFMPEG_BEAT_MPV.md`
- Audit: `agents/audits/make_ffmpeg_beat_mpv_2026-05-04.md`
- Task 1 baseline (now refined): `agents/audits/evidence_make_ffmpeg_beat_mpv_task1_baseline_164614.md`
