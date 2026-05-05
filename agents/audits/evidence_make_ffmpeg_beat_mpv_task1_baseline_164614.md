# Evidence — MAKE_FFMPEG_BEAT_MPV Task 1 baseline — 2026-05-04 ~16:46pm

Anchor: `MAKE_FFMPEG_BEAT_MPV.md` Task 1; audit `agents/audits/make_ffmpeg_beat_mpv_2026-05-04.md` (Codex / Agent 7).

Captured by: Agent 3 (Video Player), this wake.

## Fixture + harness

- File: `C:\Users\Suprabha\Desktop\Media\TV\Sports\Virat Kohli 141(175) Vs Australia 1st Test 2014 Ball By Ball.mp4` (~21 Mbps SDR H.264 1080p 30fps).
- Launch: `scripts\compare-mpv.bat` + `scripts\compare-ffmpeg.bat` (Agent 3 spawned both at 16:46:14; Hemanth Win+Left/Win+Right'd them).
- Both windows played from the same start; Hemanth muted one to avoid double audio.

## Hemanth's verdict (verbatim)

> "all the problems come from the stuttering. the visual quality of the picture, the colour and texture too is same when paused.. but when the motion is so stuttery, the quality of the tankoban player becomes unwatchable. everything is effected by stutter except for grass texture because it remains motionless"

**Verdict tag: RED on smoothness. GREEN on picture quality at parity (when motion removed).**

## What this empirically establishes

1. Smoothness is the entire user-experience gap. Stutter → unwatchable; absent stutter → quality match.
2. Color, texture, and detail quality are at parity between the ffmpeg and mpv paths on the cricket fixture in their CURRENT shipping state — i.e., ffmpeg on `SWS_FAST_BILINEAR` swscale + plain D3D11 blit looks the same to Hemanth as mpv on `pl_filter_ewa_lanczossharp` + libplacebo composite, on this clip, when paused.
3. Stutter does not simply hurt motion smoothness — it also visibly degrades motion-content detail (crowd, bat-on-ball, pans). Static-content detail (grass texture) is preserved because there is no motion to disturb.

## What this empirically refines in the audit

The audit's P0 is split into two findings:

- **P0a** — "ffmpeg currently loses the live cricket smoothness comparison" → **CONFIRMED + dominant**. Smoothness is the whole gap.
- **P0b** — "ffmpeg SDR quality path is below mpv by default" → **REFUTED on this fixture**. Hemanth's eyes show no quality gap on the cricket clip when motion is removed. The audit's reasoning was correct in code (swscale uses fewer taps than ewa_lanczossharp) but on this content the difference is below the perceptibility floor for SDR-content-of-this-bitrate. Higher-bitrate or higher-detail content might still show the gap; the cricket fixture does not.

Plan-shape implication: Tasks 4 + 5 + 7 (per-frame telemetry → zero-copy hardening → FrameCanvas pacing) are the load-bearing path for this arc. Tasks 2 + 3 (env-gated libplacebo SDR A/B + default-flip) demote from quality-lever to precondition-check — they only matter to confirm the libplacebo SDR path doesn't COST stutter. If it costs nothing, default it for content where it might still help; if it costs stutter, skip it entirely.

## Numbers from logs (the latest representative session before the just-now relaunch)

Source: `out/ipc_latency.log` (ffmpeg-side control-plane) and `out/mpv_telemetry.log` (mpv-side per-second video telemetry). Both writers append a `## session_end=...` block per Tankoban exit.

### ffmpeg side — `out/ipc_latency.log` session_end=2026-05-04T16:44:25

```
total_commands=219 distinct_cmd_types=8 pending_unmatched=5
cmd=set_audio_speed       count=189  p50=23ms   p99=307ms  max=326ms
cmd=set_volume            count=23   p50=194ms  p99=261ms  max=261ms
cmd=open                  count=1    p50=62ms   p99=62ms   max=62ms
cmd=set_sub_visibility    count=1    p50=61ms   p99=61ms   max=61ms
cmd=set_audio_delay       count=1    p50=60ms   p99=60ms   max=60ms
cmd=pause                 count=1    p50=25ms   p99=25ms   max=25ms
cmd=set_canvas_size       count=2    p50=4ms    p99=4ms    max=4ms
cmd=stop                  count=1    p50=0ms    p99=0ms    max=0ms
```

Standout: `set_audio_speed` is firing ~189 times in a session and the p99 control-plane round-trip is 307ms. This is the SyncClock-derived AV-sync feedback loop the audit flagged as P2. For comparison, an earlier 16:02:32 session showed 102 `set_audio_speed` commands with p99=264ms. Command churn scales with the stutter — not a coincidence.

### mpv side — `out/mpv_telemetry.log` session_end=2026-05-04T16:44:23 (paired window, same fixture)

```
samples=56 duration_sec=962.80 avg_vf_fps=30.00 total_drops=56
total_vo_delayed=0 buffering_ticks=0/56
hwdec=no vo=libmpv ao=wasapi video_codec=H.264
```

(The "total_drops=56" line is mpv's accounting of frames lost across the session; the 56-sample blocks show drops cumulating from 28 → 74 within the window. Drops are present but rare; vf_fps holds 30.00; no buffering ticks. Smooth.)

### Cross-side numerical contrast on the same fixture

| Surface | ffmpeg | mpv |
|---|---|---|
| Per-frame jitter telemetry | None (control-plane only) | Direct (drops, vo_delayed, buffering) |
| Heavy command-loop work in steady state | `set_audio_speed` p99 307ms × 189 commands | n/a (no equivalent IPC, in-process) |
| Drops in steady state | unmeasured | 28→74 cumulative across ~280s |
| Frame rate stability | unmeasured | 30.00 fps avg, no buffering |

Per-frame ffmpeg jitter is opaque from current logs — the audit's O7 / P1 finding. Closing that gap is exactly Task 4.

## Carry-forward for the rest of the arc

- Task 1 closed GREEN (data captured + verdict recorded).
- Task 2 ran sequentially today — see follow-on evidence note timestamped at the Task 2 close.
- Audit P0b can be downgraded if the same parity-when-paused holds on the Task 10 corpus re-test.

## Files in scope for this evidence note

- This file: `agents/audits/evidence_make_ffmpeg_beat_mpv_task1_baseline_164614.md`
- Reference: `agents/audits/make_ffmpeg_beat_mpv_2026-05-04.md`
- Reference: `MAKE_FFMPEG_BEAT_MPV.md`
- Logs cited: `out/ipc_latency.log` (session 16:44:25 block), `out/mpv_telemetry.log` (session 16:44:23 block).
