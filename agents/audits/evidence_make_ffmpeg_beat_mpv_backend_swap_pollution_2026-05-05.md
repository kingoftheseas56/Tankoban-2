# Evidence — MAKE_FFMPEG_BEAT_MPV backend-swap pollution confirmed — 2026-05-05 ~10:30am

Anchor: `MAKE_FFMPEG_BEAT_MPV.md` Task 6; supersedes the disconfirmed causal claim in `agents/audits/evidence_make_ffmpeg_beat_mpv_task4_diagnosis_171210.md`.

Captured by: Agent 3, this wake.

## Headline finding

**The "ffmpeg stutters" symptom that motivated this entire arc is caused by `VideoPlayer::switchBackendTo` leaving residual state across mpv→ffmpeg transitions, NOT by:**
- ❌ Generic ffmpeg pipeline weakness
- ❌ Picture-quality differences (audit P0b — already disconfirmed 2026-05-04 ~16:46pm)
- ❌ Kohli-specific Clipchamp re-encode quirks (looked plausible 2026-05-04 ~17:12pm but framing was confounded by swap state)
- ❌ `set_audio_speed` traffic causing dispatcher back-pressure (causal direction was wrong — already walked back 2026-05-04 ~18:00pm)
- ❌ System load (VS Code updater + heavy Chrome — was a contributor but not the load-bearing cause)

**The actual cause:** when `switchBackendTo` is called for the second time on the same mpv→ffmpeg path within one Tankoban session, the new ffmpeg sidecar comes up in a degraded state from the start. State that persists across the swap (SyncClock, m_lastSentAudioSpeed, audio device, SHM segment, GPU resources, audio thread state, or some combination) puts the next sidecar at a disadvantage.

## Reproducer (3-step right-click sequence)

Single Tankoban process, no env-var overrides, default mpv backend (post-MAKE_MPV_SOLO Task 11 default flip 2026-05-02). Standing fixture: Gill 43 (clean broadcast capture, smooth in cold ffmpeg state).

1. **Cold-launch Tankoban** via `build_and_run.bat`.
2. **Right-click Gill 43 → "Play with ffmpeg"** → calls `switchBackendTo(mpv→ffmpeg)`. SidecarProcess #1 starts. Plays ~30s SMOOTH per Hemanth verdict.
3. **Close player. Right-click any video → "Play with mpv"** → calls `switchBackendTo(ffmpeg→mpv)`. SidecarProcess #1 destroyed (its IPC log session_end fires). mpv plays ~10s.
4. **Close player. Right-click Gill 43 → "Play with ffmpeg"** → calls `switchBackendTo(mpv→ffmpeg)` again. SidecarProcess #2 starts. Plays ~30s STUTTERY per Hemanth verdict.
5. Close Tankoban. Logs flush.

## Hard data

Two consecutive ffmpeg sidecar sessions in the same Tankoban process, ~1 minute apart, identical fixture, identical hardware. Source: `out/ipc_latency.log` final two `## session_end=` blocks.

### Sidecar #1 (first ffmpeg play, smooth) — session_end=2026-05-05T10:29:29

```
total_commands=31 distinct_cmd_types=7 pending_unmatched=6
cmd=set_audio_speed     count=15  p50=1ms   p99=30ms   max=30ms
cmd=set_volume          count=11  p50=2ms   p99=5ms    max=5ms
cmd=open                count=1   p50=0ms   p99=0ms    max=0ms
cmd=set_audio_delay     count=1   p50=5ms   p99=5ms    max=5ms
cmd=set_canvas_size     count=1   p50=2ms   p99=2ms    max=2ms
cmd=set_sub_visibility  count=1   p50=0ms   p99=0ms    max=0ms
cmd=stop                count=1   p50=0ms   p99=0ms    max=0ms
```

### Sidecar #2 (post-swap-back, stuttery) — session_end=2026-05-05T10:30:35

```
total_commands=40 distinct_cmd_types=6 pending_unmatched=5
cmd=set_audio_speed     count=35  p50=68ms  p99=273ms  max=273ms
cmd=set_audio_delay     count=1   p50=62ms  p99=62ms   max=62ms
cmd=set_sub_visibility  count=1   p50=63ms  p99=63ms   max=63ms
cmd=open                count=1   p50=50ms  p99=50ms   max=50ms
cmd=pause               count=1   p50=24ms  p99=24ms   max=24ms
cmd=stop                count=1   p50=0ms   p99=0ms    max=0ms
```

### Comparison

| Metric | Sidecar #1 (smooth) | Sidecar #2 (stuttery) | Delta |
|---|---|---|---|
| `set_audio_speed` count | 15 | 35 | **+133%** |
| `set_audio_speed` p50 | 1ms | 68ms | **68×** |
| `set_audio_speed` p99 | 30ms | 273ms | **9.1×** |
| `set_audio_speed` max | 30ms | 273ms | 9.1× |
| `open` round-trip | 0ms | 50ms | n/a (file open) |
| `set_audio_delay` round-trip | 5ms | 62ms | 12× |
| `set_sub_visibility` round-trip | 0ms | 63ms | huge |

The slowdown isn't limited to `set_audio_speed`. EVERY IPC command in sidecar #2 takes substantially longer. Even `open` (the very first command sent) takes 50ms vs 0ms in sidecar #1. **The second sidecar is born slow, not gradually degraded.**

That is a strong signal — the dispatcher in sidecar #2 is starting under contention from the moment it's spawned. Whatever pollution `switchBackendTo` leaves behind is in place BEFORE the new sidecar processes its first command.

## What this rules out + rules in

**Rules out** (already-disconfirmed framings now provably noise):
- "Kohli's Clipchamp re-encode is the underlying cause." Wrong. Gill 43 also stutters when played as the second ffmpeg via the swap path. The clean-vs-quirky-encode comparison from 2026-05-04 ~17:12pm was likely contaminated — the Kohli test happened to follow swap state, the Gill test happened to be cold-launch. Same code, same files, opposite conclusion.
- "set_audio_speed back-pressures dispatcher → frame_latency spikes." Wrong direction. Both metrics are EFFECTS of the same upstream cause — whatever swap-residual state degrades sidecar startup. (Already walked back 2026-05-04 ~18:00pm on Gill regression evidence.)
- "Tonight's regressions were environmental load (VS Code updater + Chrome)." That contributed perceptually but the empirical cause is the swap path. Hemanth's evening reproducers all crossed swap boundaries.

**Rules in** (must be investigated next session):
- `VideoPlayer::switchBackendTo` (`src/ui/player/VideoPlayer.cpp:4305`) leaves stale state across mpv→ffmpeg transitions. The state is in place from the moment sidecar #2 starts.
- Candidate state-leak sources, ranked by suspicion:
  1. **`SidecarProcess` cleanup race.** `ensureTerminated(500)` in the swap path (VideoPlayer.cpp:4321) is bounded; the dtor path uses `ensureTerminated(3000)`. If the first sidecar takes longer than 500ms to fully die (release SHM, audio device, GPU handles), the new sidecar starts under resource contention. Test: bump swap-path timeout to 3000ms and re-run reproducer.
  2. **Audio device WASAPI state.** mpv uses libmpv's audio output; ffmpeg sidecar uses its own. Swap may leave audio device in a contested state. AudioDeviceWatcher might also be amplifying via mid-swap recall events.
  3. **SHM segment / shared D3D11 texture lifetime.** Named SHM persists on Windows until last handle closes. If FrameCanvas's mapping and the dying sidecar's shared texture aren't released in lockstep, the new sidecar may see stale resources or fail to acquire.
  4. **`m_syncClock` state.** Owned by VideoPlayer, not by the backend. EMA, velocity, anchor PTS, anchor time persist across the swap. Mpv may not call `update()` (it handles AV sync internally), so the clock carries stale values into ffmpeg #2.
  5. **`m_lastSentAudioSpeed` cache.** VideoPlayer-side member; not reset on swap. After mpv playback, the cached value is whatever was last sent; the new ffmpeg sidecar's first `set_audio_speed` may fire immediately based on stale delta calculation.
  6. **`m_audioSpeedTicker` keeps firing across the swap window.** Even when m_backend is being torn down + replaced, the 500ms QTimer keeps ticking. Could fire a `sendSetAudioSpeed` to a half-constructed new backend.

## Plan implications

**Task 6 (audio-speed IPC churn) is NOT the right framing.** The audio-speed traffic is a SYMPTOM of the swap pollution. The fix lives in `switchBackendTo` and/or `SidecarProcess` cleanup, not in widening deadbands or moving handlers to worker threads.

The new operative P0 for the arc is: **Task 6.5 (or new Task 13) — fix backend-swap state leak.** Concrete next session start:

1. Re-enable Task 4 instrumentation via env var (carry-forward debt) — `TANKOBAN_FFMPEG_PACING=1` to opt in, default off so daily-driver isn't burdened.
2. Add a tankoctl `swap-backend <ffmpeg|mpv>` command (~30 LOC in DevControlServer + tankoctl.cpp) so this reproducer is automated end-to-end without MCP navigation. Future debug iterations cost 5 lines of tankoctl, not 5 minutes of right-clicking.
3. Pick the smallest candidate first: bump `ensureTerminated(500)` to `ensureTerminated(3000)` in `switchBackendTo` — single-LOC change. Run reproducer. Compare sidecar #2 IPC stats.
4. If timeout bump alone closes the gap, ship. Otherwise iterate down the candidate list.

## Files referenced

- This evidence note: `agents/audits/evidence_make_ffmpeg_beat_mpv_backend_swap_pollution_2026-05-05.md`
- Pacing CSV (instrumentation re-enabled briefly for this test): `out/frame_pacing_20260505_102803.csv` (6241 samples, 122.86s span, captured both ffmpeg sidecar lifetimes + intermediate mpv window)
- IPC log: `out/ipc_latency.log` final two `## session_end=` blocks (10:29:29 + 10:30:35)
- Code paths: `src/ui/player/VideoPlayer.cpp:4305` (`switchBackendTo`), `src/ui/player/SidecarProcess.cpp:106` (`ensureTerminated`), `src/ui/player/SidecarProcess.cpp:94` (`~SidecarProcess`)
- Plan: `MAKE_FFMPEG_BEAT_MPV.md`
- Prior evidence (causal claims now superseded): `agents/audits/evidence_make_ffmpeg_beat_mpv_task1_baseline_164614.md` + `agents/audits/evidence_make_ffmpeg_beat_mpv_task4_diagnosis_171210.md`

## Carry-forward debt updated

- **(SUPERSEDED)** Re-baseline Gill on quiet system — no longer load-bearing; the bug is swap pollution, not system load.
- **(STILL OPEN)** Gate Task 4 instrumentation behind `TANKOBAN_FFMPEG_PACING=1` env var.
- **(NEW)** Add tankoctl `swap-backend` command.
- **(NEW PRIMARY)** Investigate + fix `switchBackendTo` state leak. Smallest test first: bump ensureTerminated timeout from 500ms to 3000ms.
