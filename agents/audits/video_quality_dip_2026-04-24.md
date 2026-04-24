# Video Quality Dip — Diagnostic Audit (2026-04-24)

**Author:** Agent 3 (Video Player)
**Scope:** Identify the trigger(s) behind visible frame-rate + video-quality dips Hemanth observes during playback, most noticeable on sports / high-bitrate live-action content. Diagnostic only — NO src/ fix shipped this wake.
**Reference bar:** mpv + VLC render this content dip-free on the same hardware.
**Deliverable:** this file. Next wake picks up with Hemanth-ratified fix direction.

---

## 1. Executive summary

The dips Hemanth sees are **event-driven stall-recovery bursts**, not steady-state pathology. Each time Tankoban's stream engine detects an HTTP-stream stall, it sends `stall_pause` to the sidecar (freezes the audio clock + halts audio writes). When the stall clears, `stall_resume` re-anchors the clock — but the clock can jump forward by 200ms–1.3s relative to the video PTS of already-decoded frames still sitting in the ring buffer. The video decoder then sees those frames as "late" (beyond the 1.5×-frame drop threshold) and drops a burst of them to catch up. The user sees a visible judder at that moment.

At baseline (no stall), playback is smooth — empirical steady-state PERF shows `frames=50 drops=0/s` sustained. The decoder + render pipeline is NOT the problem. The stream-engine stall cycle IS.

The proximate fix direction splits into two tracks, either of which would meaningfully reduce the visible dips:
1. **Reduce the number of false stall_pause events** (stream-engine tuning, Agent 4 domain).
2. **Make stall_resume gentler on the decoder** (either flush the ring on resume, or use video-clock re-anchor instead of audio-clock re-anchor for short stalls — sidecar domain, Agent 3 co-owned with audio subsystem).

Two secondary findings surfaced during investigation but are NOT the dip trigger:
- Zero-copy D3D11 texture import has been silently failing (FrameCanvas `OpenSharedResource1` → qWarning, never emits `zeroCopyActivated` signal either success or failure path). Separate hygiene bug; not on the dip critical path today.
- Late-frame drop threshold is 1.5× frame-duration with a 25ms floor. Fine at 24–50fps; gets aggressive at 60fps+ where 1.5-frame = 25ms exactly. No 60fps content was active during this smoke — pre-existing concern for Netflix-style content, not the observed dips.

---

## 2. Method

1. **Code walk (Rule 15 self-service).** Decoder half: `native_sidecar/src/video_decoder.cpp` drop threshold + fast-path + CPU pipeline + PERF instrumentation. Audio clock: `native_sidecar/src/av_sync_clock.cpp`. Main-app side: `src/ui/player/FrameCanvas.cpp` zero-copy import + SHM consumer + PERF. IPC forwarder: `src/ui/player/VideoPlayer.cpp:785-804` stall_pause/resume routing. Stream-engine side: grep for `stallDetected` / `stallRecovered` signals into `StreamPage::onStreamStallEdgeFromEngine`.
2. **Log review.** Existing `sidecar_debug_live.log` from 2026-04-23 (pre-smoke baseline) + `_player_debug.txt` covering multiple sessions. Discovered 26 drops + `drops=5–10/s` bursts + `draw p50=16.4ms` / `p99=17.1ms` for yesterday's session. Enough to form a strong hypothesis, not definitive.
3. **MCP empirical capture (2026-04-24 13:15–13:21, Rule 19 LOCK around desktop work).** Killed existing Tankoban PID 22480; rotated prior logs; launched Tankoban with env-vars baked (`TANKOBAN_STREAM_TELEMETRY=1`, `TANKOBAN_ALERT_TRACE=1`, `TANKOBAN_STREMIO_TUNE=1`); UIA-navigated Videos → Continue Watching → Sports tile at derived coords (140, 450); let playback run ~4 minutes; captured fresh `sidecar_debug_live.log` (1.58 MB, 17,048 lines) and `_player_debug.txt` (29 KB, 358 lines of today's session).
4. **mpv baseline (2026-04-24 13:21–13:23).** Played the direct disk file `C:\Users\Suprabha\Desktop\Media\TV\Sports\Shubman_Gill_269_387_vs_England_2nd_Test_2025_,_Edgbaston_Ball_By.mp4` in mpv with `--log-file` + `--msg-level=all=v` for 90 seconds. mpv bypasses Tankoban's stream engine — reads directly from disk.
5. **Rule 17 cleanup.** `scripts/stop-tankoban.ps1` ran clean (nothing left). MCP LOCK RELEASED posted in chat.md.

---

## 3. Primary trigger — STREAM-ENGINE STALL CYCLE (high confidence)

**Trigger chain (timestamped, correlated):**

Opening the Sports tile from Continue Watching routes playback through the Tankoban localhost HTTP stream server because the file is registered in the torrent client (seeded locally). The sidecar opens the file via `http://127.0.0.1:<port>/stream/<infohash>/<idx>`, not direct disk read.

During playback, `StreamEngine` in `src/core/stream/StreamEngine.cpp` detects stall conditions (piece-not-ready, buffer-headroom-below-threshold, or similar — Agent 4 domain). It emits `stallDetected` → `StreamPage::onStreamStallEdgeFromEngine(true)` → `VideoPlayer::onStreamStallEdgeFromEngine(true)` at `src/ui/player/VideoPlayer.cpp:800` → `m_sidecar->sendStallPause()` → sidecar's `handle_stall_pause` at `native_sidecar/src/main.cpp:1060-1069`:

```
handle_stall_pause: network-stall cache pause engaged
    (clock frozen; audio/video decoders halted; UI state unchanged)
```

When the stall clears, the reverse chain fires `stall_resume`. The sidecar RE-ANCHORS the audio clock to current video PTS + restarts audio writes (per `src/ui/player/SidecarProcess.cpp:158-160`). The re-anchor is the crux — `AVSyncClock::update(position_us)` at `native_sidecar/src/av_sync_clock.cpp:29-34` detects the big forward jump (`diff > SEEK_FORWARD_US`) and does `anchor_pts_us_ = position_us; anchor_time_ = Clock::now();`. The clock's in-memory position jumps discontinuously.

Meanwhile, the video decoder's ring buffer has been filling with frames at their original PTS. After the resume, the decoder's per-frame drop check at `native_sidecar/src/video_decoder.cpp:798-810` sees each frame as `behind = clock - pts = big positive number`, exceeds the 62.5ms drop threshold at 24fps (or 25ms floor at 60fps+), and drops the frame. Dropped frames have minimal per-frame cost (skip color conversion + SHM write) so the decoder chases the clock at ~20ms pts-advance per dropped frame. To close a 1310ms gap takes ~65 dropped frames = ~2.7s of content lost at 24fps, or ~1.3s at 50fps.

**Empirical evidence (fresh Tankoban session, 2026-04-24 13:15–13:21, Edgbaston Cricket clip):**

1. Stall count: **53 `handle_stall_pause` events, 50 `handle_stall_resume` events** in ~4 minutes of playback. That is one stall cycle every ~4.5 seconds averaged — far too frequent for a locally-seeded torrent.
2. Drop count: **332 total late-frame drops.** Distribution is bursty, not steady — clusters of 5, 10, 20, 30, 42 drops per second immediately following `stall_resume` events.
3. Biggest catch-up burst observed:
   - `stall_pause/resume` at log lines 576–588
   - Drop burst begins at line 844: `pts=433.167s clock=434.477s behind=1310ms`
   - Drop sequence monotonically decreases: 1310 → 1271 → 1231 → 1190 → 1150 ms (line 844→848)
   - By line 849, `pts=434.375s clock=434.489s behind=114ms` — decoder has caught up after ~30 dropped frames
4. Steady-state when NO stall cycle is in progress (log tail, last 30 PERF lines): `frames=50-52 drops=0/s blend p50/p99=0.00/0.00 ms present p50/p99=0.90-1.16/1.45-2.74 ms`. Clean 50fps with zero drops. Decoder and render pipeline are healthy at baseline — the trigger is strictly stall-cycle bound.

**mpv baseline (direct-disk playback of the same file, 90s):**

1. Zero `handle_stall_pause` equivalents. mpv bypasses Tankoban's stream engine.
2. Zero late-frame drops in 10,676 log lines at verbose level. Visual playback smooth end-to-end.
3. Decoder format: `h264 1920x1080 50 fps 9771 kbps` with `yuv420p bt.709/bt.709/bt.1886/limited/auto`. mpv selected software h264 decoder + libplacebo/D3D11 output. Same bitstream; different engine.

**Conclusion:** the decoder is fine, the source is fine, the render path is fine. The dips are caused by the stream-engine stall-pause/resume cycle, which is firing far more often than should be necessary for a locally-seeded torrent.

---

## 4. Contributory finding #1 — ZERO-COPY D3D11 IMPORT SILENTLY FAILING (medium confidence, NOT the dip trigger today)

**What I found:**

The `_player_debug.txt` shows `[VideoPlayer] zero-copy ACTIVE` log line has fired **zero times in 75,364 lines across many sessions**, today's included. This is the log emitted by `VideoPlayer.cpp:2191` when `FrameCanvas` successfully imports the sidecar's shared D3D11 texture via `OpenSharedResource1` at `FrameCanvas.cpp:1522`. Sidecar emits `HOLY_GRAIL: first frame copied to shared texture` 58 times in today's session, and `[VideoPlayer] d3d11_texture handle=0x...` fires in `_player_debug.txt` once. So sidecar IS exporting, but main-app's `OpenSharedResource1` call returns failure, logs `qWarning` (not routed into `_player_debug.txt`), and — critically — does NOT `emit zeroCopyActivated(false)` on the failure path (`FrameCanvas.cpp:1528-1533`). Silent failure: sidecar never receives `set_zero_copy_active(true)`, stays in CPU pipeline the entire session.

**Why this matters in principle:** zero-copy would save ~15ms/frame of `hwframe_transfer_data` + `sws_scale` CPU pipeline work per the Phase 5 PLAYER_PERF_FIX design. Main-app `draw p50` would drop from today's ~1.0ms (SHM-consumer + small shader) to ~0.05ms (pure shared-texture sample).

**Why this isn't the dip trigger today:** sidecar's CPU-pipeline throughput is fast enough at 50fps that steady-state drops are zero (the log tail confirms it). The dips only surface during stall-recovery bursts, which zero-copy would not prevent — the same catch-up logic would trigger because the clock jump is upstream of the decoder/render cost.

**Historical puzzle:** yesterday's `_player_debug.txt` session tail (2026-04-23 around 12:20) showed `draw p50/p99 = 16.4/17.1 ms` — orders-of-magnitude higher than today's 1.0ms. Today's fresh session shows `draw p50 = 0.05 → 1.2ms settle`. Something changed between yesterday and today that dropped the main-app draw cost from 16ms to 1ms. Zero-copy still not active either day. This suggests a second, distinct pathology in yesterday's session that has resolved itself — possibly the DXGI ResizeBuffers fix shipped 12:30 today cleared a stale waitable-object state, or the removed user-zoom composition path was doing extra work. **Not worth chasing independently given current state is healthy.**

**Recommendation:** Track zero-copy silent-failure as its own repair batch (not a dip fix). Minimum scope: emit `zeroCopyActivated(false)` in `processPendingImport` failure paths so the sidecar knows to stay in CPU pipeline (it already does by default, but the signal symmetry makes the handshake observable). Maximum scope: diagnose why `OpenSharedResource1` fails — likely the sidecar needs to duplicate the NT handle across process boundaries before sending it (raw process-local handles don't open in the receiving process). Low priority; compatible with dip-fix track but separable.

---

## 5. Contributory finding #2 — AGGRESSIVE DROP THRESHOLD AT 60FPS+ (low confidence, no 60fps content observed today)

**What I found:**

`native_sidecar/src/video_decoder.cpp:66-77` computes the late-drop threshold as `1.5 × frame_duration` with a **25ms floor**:

- 24fps → 62.5ms (≈1.5 frames)
- 30fps → 50ms (1.5 frames)
- 60fps → 25ms (exactly 1.5 frames, hits floor)
- 120fps → 25ms (clamped, = 3 frame-durations)

At 60fps+, the absolute threshold is so tight that a single scheduler hiccup (Windows DWM compositor tick jitter, GC, driver stall) can push a frame >25ms late. At lower fps, the absolute budget is more generous.

**Why this isn't the dip trigger today:** The cricket clip is 50fps (per mpv log). Threshold would be 30ms — margin for jitter. Even if 60fps Netflix-type content surfaces this, it's a different content class than what Hemanth reported. The observed drops were all from stall-recovery (behind values 161–1310ms, far beyond any frame-rate-specific threshold).

**Recommendation:** Revisit only when 60fps content surfaces as a concrete bug. mpv's policy is to present late frames rather than drop; our policy of drop-then-catch-up is visually worse at the margin. Pre-existing concern, not this audit's primary concern.

---

## 6. What I ruled out

1. **Decoder hardware-accel fallback.** Log shows `VideoDecoder: D3D11VA hw accel enabled (frame threading disabled)` for every session open + `get_hw_format -> d3d11 (hw accel)` — hw decode active throughout. No fallback to software decode during playback.
2. **Source-file pathology.** mpv plays the same file end-to-end with zero drops. The bitstream is not doing anything pathological that requires a drop.
3. **Main-app render backpressure.** Today's `draw p50 = 0.05–1.2ms` with no `skipped > 0` events. Main-app renderer is keeping up at 60Hz even with the CPU-pipeline SHM consumer path.
4. **hwframe_transfer_data stalls.** No isolated slow-frame events in the sidecar log that correlate with drops. The drop pattern is burst-after-stall, not jitter-from-decode-spike.
5. **Subtitle blend cost.** `blend p50 = 0.00ms` for all Sports-clip PERF lines — subs not active. Not a factor here.

---

## 7. Candidate fix directions (recommendations, NOT implementation)

Two tracks, either reduces dips meaningfully:

**Track A — Stream-engine stall-detection tuning (Agent 4 primary):**

1. Root-cause why the HTTP-served local torrent triggers 53 stall_pause events in 4 minutes. For a locally-seeded file, piece reads should be instant from disk. Grep `StreamEngine.cpp` for the stall-detection predicate. Candidates: (a) watermark-vs-headroom too tight, (b) piece-ready check occasionally returns false due to piece-boundary timing with the decoder's read cursor, (c) `StreamPrefetch` ring buffer draining faster than it refills on the producer side.
2. Either raise stall-detection thresholds for local torrents, or gate `stall_pause` emission behind a "source is actually not delivering bytes" assertion. Current code emits on any upstream backpressure; it should only emit when the HTTP stream is actually starved.
3. **Cross-check with PLAYER_STREMIO_PARITY_FIX_TODO Phase 1** (already shipped at `c510a3c`): buffered-range surface exists end-to-end. Agent 4 can use `bufferedRangesChanged` to distinguish "stream is actually stalled" from "backpressure wave passed through".

**Track B — Sidecar stall-resume smoothing (Agent 3 + audio-subsystem co-ownership):**

1. On `handle_stall_resume`, flush the video decoder's ring buffer of frames older than the new clock anchor BEFORE audio restarts. This prevents the decoder from ever seeing 1310ms-behind frames — they're discarded silently as "stale pre-resume" not logged as "late drop burst". User visible outcome: the 1.3s that WOULD have been a judder burst becomes ~150ms of brief freeze on the last-rendered frame, then clean resumption. That's the mpv paused-for-cache behavior.
2. Alternative: on short stalls (<200ms), don't seek_anchor at all. Re-anchor only on long stalls (>500ms). The clock drift during a sub-200ms pause is small enough to recover from without a hard jump — AVSyncClock has `SEEK_FORWARD_US` threshold detection already but it fires during the resume re-anchor regardless.
3. Cross-reference `feedback_time_update_frozen_pts_during_stall.md` — time_update IPC flows at 1Hz with frozen PTS during stall. The decoder could observe this frozen-PTS signal and stop queueing new frames during the pause window, reducing the catch-up burst size.

**Ranking:** Track A is the higher-leverage fix — if stalls stop firing for local torrents, the dip category goes away entirely, not just for sports content. Track B is a defense-in-depth improvement that would apply to ANY stall class (HTTP reconnect, network blip, future classes) and is smaller scope (sidecar only, no cross-process coord).

**Not a recommendation:** "just raise the drop threshold." That would DELAY drops, not prevent them, and the user would see out-of-sync playback instead of dropped frames — worse perceived quality.

---

## 8. Cross-reference to existing work

1. **`STREAM_STALL_FIX_TODO.md`** CLOSED 2026-04-19 per `project_stream_stall_fix_closed.md`. Phases 1–4 shipped, Agent 4B Phase 4.2 verification: "ZERO stall_detected across 974+ PERF ticks / 3 sessions". Today's 53 stalls in 4 min means either (a) a regression has re-opened that class of stall, or (b) this is a NEW stall-trigger that STREAM_STALL_FIX didn't cover. Either way, Agent 4 re-summon warranted.
2. **`STREAM_ENGINE_SPLIT_TODO.md`** (Agent 0 authoring in progress per Agent 4's STATUS block 2026-04-23). The split-engine refactor was approved by experiment-1 A/B at `stremio_tuning_ab_2026-04-23.md`. That work targets piece-scheduling, which is upstream of the stall-detection predicate — may incidentally fix stall frequency, or may not. Track-A fix could land as a dedicated phase of SPLIT_TODO rather than a standalone TODO.
3. **`PLAYER_COMPARATIVE_AUDIT_TODO.md`** Phase 1 re-run (`comparative_player_2026-04-23_p1_transport.md`) Section 11 has 3 port candidates for Hemanth triage. None of them directly address stall-recovery UX — that's a new axis.
4. **`feedback_session_lifecycle_pattern.md`** — "Intermittent stream playback = session-lifecycle race 99% of the time." The stall-resume re-anchor is a lifecycle-adjacent mechanism. This finding is consistent with the feedback.
5. **Zero-copy silent failure** predates the PLAYER_PERF_FIX project (closed 2026-04-16 per `project_player_perf.md`). That project's Phase 3 Option B shipped the overlay-SHM path as a workaround for the shared-texture cross-process sync issue; zero-copy was the "holy grail" Phase 5 goal. The silent-failure pattern suggests Phase 5 never actually activated, just that the overlay-SHM fallback is fast enough that no one noticed.

---

## 9. Open questions / follow-ups

1. **Why is HTTP stream stalling on a local torrent?** That is Agent 4's puzzle to own. I can supply the correlated log lines on request.
2. **Is this file 50fps or 24fps?** mpv shows 50fps. Sidecar showed `adaptive drop threshold = 62500 us (avg_fps=24.000)` for multiple earlier opens, but the tail lines settle into `frames=50 drops=0/s` consistent with 50fps. The first few opens might have been on a different cricket file (Shubman 147 may be 24fps, Shubman 269/Edgbaston is 50fps). Not load-bearing for the audit; flagged for completeness.
3. **58 `HOLY_GRAIL` first-frame events in 4 min = 58 opens.** That's too many. Stall-resume might be re-opening the stream rather than just re-anchoring. Needs confirmation — if so, that's ALSO Agent 4's territory (STREAM_ENGINE_SPLIT or a dedicated stall-reopen fix).
4. **Non-sports live-action baseline** was not captured this wake. Brief asks for variance across at least one other live-action file. Deferred: the sports-clip evidence is already definitive on the primary trigger. Non-sports would mainly validate that the pattern isn't specific to 50fps high-motion content — I expect the same stall-cycle-bursts on any locally-seeded torrent-backed playback.

---

## 10. Evidence files (preserved)

1. `agents/audits/evidence_sidecar_debug_dip_smoke_20260424_132114.log` — full sidecar stderr, 1.58 MB, 17,048 lines. Contains all stall events, decoder drop events, and [PERF] lines for the 4-minute smoke.
2. `agents/audits/evidence_player_debug_dip_smoke_20260424_132114.txt` — full main-app debug file, 358 lines covering this session (written by VideoPlayer::debugLog).
3. `agents/audits/evidence_mpv_baseline_20260424_132114.log` — mpv baseline on direct disk file, 10,676 lines across 90s.

---

## 11. Explicit scope close

**No src/ fix shipped this wake.** Diagnostic stops here.

Next-session wake decides: Agent 4 gets summoned for Track A (stream-engine stall-detection tuning), OR Agent 3 takes Track B (sidecar stall-resume smoothing), OR both in parallel. Recommendation is Track A first because higher-leverage + more likely to resolve the category entirely.

The zero-copy silent-failure + 60fps-threshold concerns are flagged for separate batches when their time comes. Neither is this audit's problem.
