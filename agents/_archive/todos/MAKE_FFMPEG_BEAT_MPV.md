# Make FFMPEG Beat MPV

## Plan-specific rules

> Don't ask Hemanth questions or give him options unless it concerns the user-facing side of the app, meaning how it affects the experience of using the app.

> Talk to me in non-coder terms and simpler language so I can better follow instructions if the agent needs me to test something.

Agent-facing translation: the executing agent picks the implementation details. Hemanth judges what he sees and hears. Smoke instructions stay plain-English.

---

## General direction

Destination: keep ffmpeg's better Tankoban integration, but make ffmpeg look sharper and play smoother than mpv on the standing cricket compare fixture. The route is measurement first, then ffmpeg SDR libplacebo defaulting, then pacing/zero-copy/audio-speed cleanup, then quality tuning, then a small corpus re-test. Killing the sidecar and linking FFmpeg in-process is a late decision gate, not the default path.

Audit anchor: `agents/audits/make_ffmpeg_beat_mpv_2026-05-04.md`.

Standing smoke fixture: `C:\Users\Suprabha\Desktop\Media\TV\Sports\Virat Kohli 141(175) Vs Australia 1st Test 2014 Ball By Ball.mp4`.

Standing harness: `scripts\compare-ffmpeg.bat` and `scripts\compare-mpv.bat`.

---

## Tasks

Tasks 1-12 authored. Tasks 13+ should only land if Task 10's corpus re-test or Task 11's sidecar decision gate surfaces new work.

- [x] **1. Bench ffmpeg vs mpv on the cricket fixture.** — CLOSED 2026-05-04 ~16:46pm. Hemanth verdict: RED on smoothness; GREEN on picture quality (color/texture at parity when paused). Evidence: `agents/audits/evidence_make_ffmpeg_beat_mpv_task1_baseline_164614.md`. Refines audit P0b: SDR scaler quality is NOT the perceptible gap on this fixture; smoothness is the whole user-experience problem.
  - **What this involves (plain English):** No code changes. Run the current compare setup and capture today's truth before changing anything. The agent reads the logs afterward so we know what the app measured, while Hemanth records how each side felt by eye.
  - **What Hemanth does for the smoke test:** Open `scripts\compare-ffmpeg.bat` and `scripts\compare-mpv.bat`, snap the two windows side-by-side, and watch the cricket clip. Look at bat-on-ball moments, grass texture, crowd pans, and motion smoothness. Report GREEN / YELLOW / RED for whether ffmpeg is already beating mpv.
  - **Goal:** Establish the fair baseline for this arc.
  - **What success looks like:** The audit/evidence folder has a short baseline note with Hemanth's verdict, and the agent has captured the relevant `out/ipc_latency.log` and `out/mpv_telemetry.log` numbers for the same test run.
  - **Files in scope:** `agents/audits/`, `out/ipc_latency.log`, `out/mpv_telemetry.log`.
  - **Dependencies:** None.
  - **Smoke owner:** Hemanth gives the visual verdict; Agent reads logs.
  - **Audit finding:** Audit P0 - ffmpeg currently loses the live cricket smoothness comparison; Audit O1 - current telemetry baseline.

- [ ] **2. Verify the existing env-gated ffmpeg SDR libplacebo path.** — DEMOTED to optional precondition-check after Task 1's empirical verdict. Picture quality is at parity on the cricket fixture without libplacebo on the SDR path; quality-improvement is no longer the motivation. The only remaining reason to run this A/B is to rule OUT a smoothness COST from defaulting libplacebo (more GPU work). Do this AFTER Tasks 4-7 land, when ffmpeg pacing is already stable, so the libplacebo smoothness-cost is measured against a known-good ffmpeg baseline. Skip if Tasks 4-7 close the smoothness gap without it.
  - **What this involves (plain English):** Test the already-built higher-quality ffmpeg picture path before making it the default. This is an A/B check: ffmpeg normal, ffmpeg with the hidden SDR libplacebo switch, then mpv.
  - **What Hemanth does for the smoke test:** Run the cricket clip once through normal ffmpeg, once through ffmpeg with the agent-provided env-gated launch, and once through mpv. Watch the same grass/crowd/bat-on-ball moments. Say whether the env-gated ffmpeg picture is sharper, the same, or worse than normal ffmpeg and mpv.
  - **Goal:** Prove the existing ffmpeg SDR libplacebo path is useful and not visibly worse before making it default.
  - **What success looks like:** `TANKOBAN_LIBPLACEBO_SDR=1` visibly improves or matches ffmpeg without new stutter, color shift, subtitle drift, or first-frame delay that Hemanth notices.
  - **Files in scope:** `agents/audits/` evidence files and logs; optional temporary launch helper only if the existing scripts are not enough.
  - **Dependencies:** Task 1.
  - **Smoke owner:** Hemanth for visual verdict; Agent reads sidecar and IPC logs.
  - **Audit finding:** Audit P0 - ffmpeg SDR quality path is below mpv by default; Audit O2/O3 - SDR libplacebo already exists behind an env gate.

- [ ] **3. Remove the SDR libplacebo env gate after Task 2 is GREEN.** — DEMOTED with Task 2. Only fires if Task 2 confirms libplacebo SDR adds zero smoothness cost, AND Hemanth wants the cleanup for content-types other than the cricket fixture (e.g., higher-bitrate, finer-detail SDR where the parity might not hold). Otherwise leave the env gate in place.
  - **What this involves (plain English):** Make the better ffmpeg SDR picture path the normal path. The old swscale path stays as a backup if libplacebo fails to start.
  - **What Hemanth does for the smoke test:** Launch ffmpeg normally, with no special environment flag. Open the cricket clip. Confirm it looks like the improved Task 2 version, not the older softer ffmpeg version.
  - **Goal:** Every normal ffmpeg SDR file gets the libplacebo renderer by default.
  - **What success looks like:** The sidecar log confirms SDR files instantiate `GpuRenderer` without `TANKOBAN_LIBPLACEBO_SDR=1`; normal playback still falls back cleanly if libplacebo init fails.
  - **Files in scope:** `native_sidecar/src/main.cpp`.
  - **Dependencies:** Task 2 must close GREEN.
  - **Smoke owner:** Agent verifies log path; Hemanth verifies picture and first-frame feel.
  - **Audit finding:** Audit P0 - ffmpeg SDR quality path is below mpv by default; Audit O2/O3 - current env gate is the quality cliff.

- [x] **4. Add ffmpeg per-frame pacing telemetry.** — CLOSED 2026-05-04 ~17:12pm. Shipped: `VsyncTimingLogger` extended with `zero_copy_active` field; `FrameCanvas` default-on logging + auto-CSV-dump on session-end + 1Hz `[PACING]` line in DebugLogBuffer. Ruled OUT five hypotheses (stale repeats, producer drops, zero-copy state, fallback rate, skipped presents). CONFIRMED `set_audio_speed` IPC back-pressure as the real cause — Kohli p99=60ms vs Gill p99=34ms correlates with frame_latency p99=61ms vs 40ms. Evidence: `agents/audits/evidence_make_ffmpeg_beat_mpv_task4_diagnosis_171210.md`. Files: `src/ui/player/VsyncTimingLogger.{h,cpp}` + `src/ui/player/FrameCanvas.{h,cpp}`.
  - **What this involves (plain English):** Add better measurement for why ffmpeg stutters. Today's log can say when commands are slow, but not whether video frames arrive late, get skipped, or repeat stale frames. This task adds those missing facts.
  - **What Hemanth does for the smoke test:** Run the cricket clip once on ffmpeg and say whether it still stutters. The agent reads the new log and explains the result in plain English: "frames are arriving late," "the app is repeating old frames," or "zero-copy is off."
  - **Goal:** Make ffmpeg stutter diagnosable from logs instead of guessing from eyesight alone.
  - **What success looks like:** A ffmpeg playback log includes producer frame timestamps, chosen frame id, lateness, skipped presents, zero-copy state, and fallback state for the cricket clip.
  - **Files in scope:** `native_sidecar/src/video_decoder.cpp`, `src/ui/player/FrameCanvas.cpp`, `src/ui/player/VsyncTimingLogger.cpp`, `src/ui/player/VsyncTimingLogger.h`, `src/ui/player/ShmFrameReader.cpp`, `src/ui/player/ShmFrameReader.h`.
  - **Dependencies:** Task 1. Can run before or after Task 3, but Task 3 makes the quality path representative.
  - **Smoke owner:** Agent reads logs; Hemanth confirms whether the visible feel matches the log.
  - **Audit finding:** Audit P1 - IPC telemetry is control-plane only; Audit O6/O7 - FrameCanvas already has pacing hooks but they do not yet close the ffmpeg frame question.

- [ ] **5. Verify and harden the D3D11 zero-copy path.** — DEMOTED after Task 4 diagnosis. Zero-copy is at 0% throughout BOTH Gill (smooth) and Kohli (rough) playback per `frame_pacing_*.csv` data. Not load-bearing for the current smoothness gap. Future quality/efficiency work, not a stutter fix. Revisit after Task 6 lands.
  - **What this involves (plain English):** Confirm the ffmpeg video is really using the faster shared-texture path during cricket playback. If it falls back to the older copy path, fix only that reason.
  - **What Hemanth does for the smoke test:** Open the cricket clip on ffmpeg after the agent says the zero-copy check is ready. Watch for stutter during pans and quick cuts. Report GREEN / YELLOW / RED on smoothness.
  - **Goal:** Keep ffmpeg frames on the fast D3D11 shared-texture route whenever the file and settings allow it.
  - **What success looks like:** Logs show `d3d11_texture` import, zero-copy active, and sidecar fast path during steady-state cricket playback; visible stutter is reduced or the remaining stutter has a different measured cause.
  - **Files in scope:** `native_sidecar/src/video_decoder.cpp`, `native_sidecar/src/d3d11_presenter.cpp`, `src/ui/player/FrameCanvas.cpp`, `src/ui/player/VideoPlayer.cpp`.
  - **Dependencies:** Task 4 is preferred so the hardening has measurement.
  - **Smoke owner:** Agent verifies logs; Hemanth verifies smoothness.
  - **Audit finding:** Audit P1 - ffmpeg has zero-copy infrastructure, but no current proof it is active throughout the stutter case; Audit O5 - zero-copy is conditional.

- [ ] **6. Reduce noisy audio-speed IPC churn.** — **REFRAMED 2026-05-05 ~10:30am as a SYMPTOM, not the actual fix target.** The audio-speed traffic differential between sidecars #1 and #2 (15 vs 35 commands; p99 30ms vs 273ms) is a downstream consequence of `VideoPlayer::switchBackendTo` leaving residual state across mpv→ffmpeg transitions. See Task 13 below for the actual P0. The original Task 6 attempt (SyncClock noise floor 5→10ms + VideoPlayer deadband 0.0005→0.002, 2026-05-04 ~17:48pm) regressed Gill from smooth → stuttery and was reverted; that was diagnostic — it confirmed the corrections are responding to drift, not causing it. Functional state restored to baseline; comment blocks document the attempt + reasoning.

- [ ] **13. Fix backend-swap state leak in `VideoPlayer::switchBackendTo`.** — **NEW PRIMARY P0 2026-05-05 ~10:30am.** Reproducer: in a single Tankoban session, right-click Gill 43 → "Play with ffmpeg" (smooth, 30s); close player, right-click any → "Play with mpv" (10s); close, right-click Gill → "Play with ffmpeg" (STUTTERY, 30s). Empirical IPC delta between sidecar #1 and sidecar #2: `set_audio_speed` count 15→35 (+133%), p50 1ms→68ms (68×), p99 30ms→273ms (9.1×). EVERY IPC command in sidecar #2 takes substantially longer than in #1 — `open` 0ms→50ms, `set_audio_delay` 5ms→62ms. The second sidecar is born slow, not gradually degraded. Evidence: `agents/audits/evidence_make_ffmpeg_beat_mpv_backend_swap_pollution_2026-05-05.md`.
  - **What this involves (plain English):** When you right-click and switch backends mid-session, the app is supposed to fully tear down the old player and bring up a fresh new one. Right now something stale carries over and the new player starts hobbled. Find what carries over; clean it up.
  - **What Hemanth does for the smoke test:** Run the 3-step reproducer (right-click ffmpeg → swap to mpv → right-click ffmpeg again) on Gill 43. After fix, second ffmpeg should be as smooth as first. Then for full regression sweep, do the same on Kohli 141 and confirm both first AND second ffmpeg playbacks behave the same as their respective baselines.
  - **Goal:** Eliminate the perceptual stutter that appears on second-or-later mpv→ffmpeg swap within one Tankoban process.
  - **What success looks like:** Sidecar #2's IPC stats match sidecar #1's within ±10% on `set_audio_speed` p99 and command count, when both play the same fixture under the same backend choice.
  - **Files in scope (initial suspicion order):** `src/ui/player/VideoPlayer.cpp:4305-4346` (`switchBackendTo`), `src/ui/player/SidecarProcess.cpp:106` (`ensureTerminated` — 500ms swap-path timeout vs 3000ms dtor; this is the smallest first test), possibly `src/ui/player/AudioDeviceWatcher.{h,cpp}` (audio device handle), possibly `native_sidecar/src/main.cpp` SHM lifecycle.
  - **Smallest first test:** bump `ensureTerminated(500)` → `ensureTerminated(3000)` at VideoPlayer.cpp:4321. Single-LOC change, low blast radius. Run reproducer; if sidecar #2 stats normalize, ship. Otherwise descend the suspicion list.
  - **Dependencies:** None blocking. Task 4 instrumentation needs to be re-enabled (env var per the carry-forward debt) so we can measure the fix.
  - **Smoke owner:** Agent reads logs (sidecar #1 vs sidecar #2 IPC stats); Hemanth verifies smoothness on both fixtures.
  - **Audit finding:** Supersedes audit P0/P1/P2 framing on the smoothness question — the operative root cause is the swap path, not picture quality, not zero-copy, not generic IPC churn.
  - **What this involves (plain English):** The app sends many small audio-speed correction messages while playing. This task makes that chatter calmer so it does not compete with video playback.
  - **What Hemanth does for the smoke test:** Play Kohli 141 (the irregular-encode case that stutters) on ffmpeg and listen/watch normally. Confirm the video feels smoother and the sound still stays in sync with bat hits and commentary. Also play Gill 43 (the smooth case) to confirm no regression.
  - **Goal:** Keep AV sync correction without flooding the sidecar with command traffic.
  - **What success looks like:** `out/ipc_latency.log` shows lower `set_audio_speed` p99 latency and/or fewer commands; Kohli's `frame_pacing_*.csv` shows frame_latency_ms p99 closer to Gill's (~40ms target); Hemanth does not notice audio drifting out of sync on either file.
  - **Files in scope:** `src/ui/player/VideoPlayer.cpp`, `src/ui/player/SidecarProcess.cpp`, `src/ui/player/SyncClock.{h,cpp}`, `native_sidecar/src/main.cpp`, `native_sidecar/src/audio_decoder.cpp`.
  - **Dependencies:** Task 4 (closed). Task 5 demoted; not blocking.
  - **Smoke owner:** Agent verifies latency log + pacing CSV; Hemanth verifies smoothness + AV sync feel on both fixtures.
  - **Audit finding:** Audit P2 (now P0) - audio/AV sync churn is the primary smoothness gap; Task 4 evidence note `evidence_make_ffmpeg_beat_mpv_task4_diagnosis_171210.md` confirms the IPC↔frame-latency correlation.

- [ ] **7. Tune FrameCanvas pacing for ffmpeg cricket playback.**
  - **What this involves (plain English):** Use the new frame log to adjust the display side of ffmpeg playback. If the app is picking frames too early, repeating old frames, or skipping presents too aggressively, tune that behavior.
  - **What Hemanth does for the smoke test:** Play the cricket clip on ffmpeg and compare it to mpv side-by-side. Watch long camera pans and crowd movement. Report whether ffmpeg now feels as smooth as mpv, better than mpv, or still worse.
  - **Goal:** Make FrameCanvas display ffmpeg frames at a steady cadence.
  - **What success looks like:** The frame log shows steady frame selection with no unexplained stale-frame repeats or late-frame bursts, and Hemanth reports ffmpeg smoothness is at least tied with mpv.
  - **Files in scope:** `src/ui/player/FrameCanvas.cpp`, `src/ui/player/ShmFrameReader.cpp`, `src/ui/player/ShmFrameReader.h`, `src/ui/player/SyncClock.h`.
  - **Dependencies:** Tasks 4 and 5.
  - **Smoke owner:** Agent verifies logs; Hemanth verifies smoothness.
  - **Audit finding:** Audit P0 - ffmpeg currently loses the live cricket smoothness comparison; Audit O6 - FrameCanvas owns waitable pacing and skip-next behavior.

- [ ] **8. Tune ffmpeg libplacebo SDR quality to beat mpv.**
  - **What this involves (plain English):** After smoothness is under control, tune the ffmpeg libplacebo settings for cricket detail. The target is sharper grass, cleaner crowd detail, and crisp bat/ball edges without ugly ringing or shimmer.
  - **What Hemanth does for the smoke test:** Compare ffmpeg and mpv side-by-side on the cricket clip. Pause on grass/crowd/detail shots if helpful, but also watch motion. Report whether ffmpeg looks better, the same, or worse than mpv.
  - **Goal:** Push ffmpeg SDR picture quality above mpv, not merely equal.
  - **What success looks like:** Hemanth says ffmpeg is visibly sharper or cleaner than mpv on the cricket fixture, with no new shimmer, halos, color shift, or smoothness regression.
  - **Files in scope:** `native_sidecar/src/gpu_renderer.cpp`.
  - **Dependencies:** Tasks 3, 5, and 7 should be GREEN so quality tuning does not mask pacing bugs.
  - **Smoke owner:** Hemanth.
  - **Audit finding:** Audit P0 - ffmpeg SDR quality path is below mpv by default; Audit R1/R2 - libplacebo/mpv scaler settings are the quality lever.

- [ ] **9. Close HDR parity gaps.**
  - **What this involves (plain English):** Check whether ffmpeg and mpv use the same HDR color and tone-mapping choices. If not, align the ffmpeg path where needed.
  - **What Hemanth does for the smoke test:** When an HDR test file is available, play the same scene on ffmpeg and mpv. Look at highlights, skin tones, dark scenes, and whether subtitles still look clean. Report GREEN / YELLOW / RED for ffmpeg against mpv.
  - **Goal:** ffmpeg HDR should match or beat mpv, with no hidden downgrade while SDR work is happening.
  - **What success looks like:** The agent can point to matching or intentionally different libplacebo tone-map, peak-detect, dither, and color-space choices; Hemanth does not see ffmpeg HDR as worse than mpv.
  - **Files in scope:** `native_sidecar/src/gpu_renderer.cpp`, `src/ui/player/MpvLibplaceboRenderer.cpp` for reference only unless a specific implementation request expands scope.
  - **Dependencies:** Task 3. HDR visual smoke depends on having an HDR fixture.
  - **Smoke owner:** Hemanth when HDR fixture exists; Agent logs otherwise.
  - **Audit finding:** Audit P1 - HDR parity is likely close, but not proven identical; Audit O3/O4 - both paths use libplacebo but with different output paths.

- [ ] **10. Run a small corpus re-test.**
  - **What this involves (plain English):** Re-test more than just cricket so we do not overfit one sports clip. Keep it small: cricket plus a few representative SDR/HDR/subtitle files.
  - **What Hemanth does for the smoke test:** Pick 3-5 files you know well, including at least one subtitle-heavy file and one live-action file. Play each on ffmpeg and mpv. Give GREEN / YELLOW / RED for whether ffmpeg now beats mpv overall.
  - **Goal:** Confirm the ffmpeg improvements hold across normal viewing, not just the standing fixture.
  - **What success looks like:** The evidence note lists each file, Hemanth's verdict, and the key log numbers. No P0 smoothness or picture-quality regression remains open.
  - **Files in scope:** `agents/audits/` evidence files and logs.
  - **Dependencies:** Tasks 1-9 as applicable; Task 9 can be skipped until HDR content exists.
  - **Smoke owner:** Hemanth gives verdicts; Agent reads logs.
  - **Audit finding:** Audit O8 - telemetry precedents matter before architecture decisions; Audit strategic call - decide from evidence, not assumption.

- [ ] **11. Make the embed-vs-sidecar decision.**
  - **What this involves (plain English):** No code unless the previous tasks fail. Look at the evidence and decide whether the sidecar is now good enough or whether ffmpeg must move inside the main app.
  - **What Hemanth does for the smoke test:** Review the plain-English summary: did ffmpeg beat mpv, tie mpv, or still lose? If it still loses, Hemanth makes the strategic call on whether the heavier in-process route is worth it.
  - **Goal:** Make the architecture decision from measured playback results.
  - **What success looks like:** One clear decision is recorded: keep improving sidecar, accept sidecar as winner, or start an in-process ffmpeg feasibility plan.
  - **Files in scope:** `MAKE_FFMPEG_BEAT_MPV.md` status update or a follow-up TODO.
  - **Dependencies:** Task 10.
  - **Smoke owner:** Agent summarizes evidence; Hemanth makes the strategic call.
  - **Audit finding:** Audit strategic call - in-process ffmpeg is a decision gate only if incremental sidecar fixes still lose after Tasks 1-7.

- [ ] **12. Only if needed: plan in-process ffmpeg embedding.**
  - **What this involves (plain English):** Paper-only planning for the heavy option: linking FFmpeg directly into Tankoban so video playback no longer crosses a sidecar process boundary. No code in this task.
  - **What Hemanth does for the smoke test:** No app smoke. Read the plain-English tradeoff summary: what gets better, what gets riskier, and how many tasks it would take.
  - **Goal:** If the sidecar cannot beat mpv, create a separate implementation plan for the in-process ffmpeg route.
  - **What success looks like:** A new follow-up TODO exists with small tasks, file scope, build/packaging risks, rollback path, and Hemanth smoke steps.
  - **Files in scope:** New follow-up TODO only.
  - **Dependencies:** Task 11 must conclude that incremental sidecar work cannot beat mpv.
  - **Smoke owner:** Agent writes the plan; Hemanth ratifies the strategic direction.
  - **Audit finding:** Audit strategic call - in-process libavcodec/libavformat is heavier and should only follow failed incremental evidence.

---

## Tracking summary

- **Closed:** Task 1 (2026-05-04 ~16:46pm). Task 4 (2026-05-04 ~17:12pm; instrumentation shipped, diagnosis CAUSAL CLAIM disconfirmed + the new evidence at `evidence_make_ffmpeg_beat_mpv_backend_swap_pollution_2026-05-05.md` supersedes both the audit's P0/P1/P2 framing and the Task 4 evidence note's audio-speed-causes-stutter claim).
- **Open (active path):** **Task 13 (backend-swap state leak)** — NEW P0 promoted 2026-05-05 ~10:30am after empirical reproducer captured 9× IPC latency delta between sidecar #1 and sidecar #2 in the same Tankoban process. Task 6 (audio-speed churn) is now reframed as a SYMPTOM of Task 13's swap pollution, not a standalone fix target. Tasks 7 → 8 → 9 → 10 → 11 → 12 queued behind Task 13.
- **Demoted:** Tasks 2 + 3 (libplacebo SDR A/B + default-flip) — picture quality at parity. Task 5 (zero-copy hardening) — empirically 0% throughout both smooth and rough playback; not load-bearing.
- **Carry-forward debt (updated 2026-05-05 ~10:30am):**
  - (DONE) Walk back Task 4 evidence note's set_audio_speed→stutter causal claim (2026-05-04 ~18:00pm).
  - (SUPERSEDED) Re-baseline Gill on quiet system — no longer load-bearing; bug is swap pollution, not load.
  - (OPEN) Gate Task 4 instrumentation behind `TANKOBAN_FFMPEG_PACING=1` env var so default-off daily-driver isn't burdened, but tooling is one env-var away for future debugging.
  - (NEW, OPEN) Add a tankoctl `swap-backend <ffmpeg|mpv>` command (~30 LOC) so the Task 13 reproducer is fully automated end-to-end without needing MCP right-click navigation.
- **Standing fixture:** Virat Kohli 141(175) cricket clip.
- **Standing verdict format:** GREEN / YELLOW / RED from Hemanth after side-by-side ffmpeg vs mpv.
- **Architecture default:** keep the sidecar unless Tasks 4-10 prove it cannot beat mpv.

### Empirical refinement to audit framing (2026-05-04 ~16:46pm + ~17:12pm)

**Round 1 (~16:46pm)** — Hemanth's Task 1 verdict — "all the problems come from the stuttering. the visual quality of the picture, the colour and texture too is same when paused" — split the audit's P0:

- **P0a (ffmpeg loses smoothness)** → CONFIRMED + dominant. Smoothness is the entire user-experience gap.
- **P0b (ffmpeg SDR quality below mpv)** → REFUTED on the cricket fixture. Color/texture/detail at parity when motion is removed. The audit's code-level reasoning (swscale fewer taps than ewa_lanczossharp) is correct but below perceptibility floor on this content.

**Round 2 (~17:12pm)** — A/B between Kohli 141 (Clipchamp web-editor encode, stutters) and Gill 43 (clean broadcast capture, smooth) further narrows P0a:

- **P0a generalized (ffmpeg loses smoothness on all cricket)** → REFUTED. Clean broadcast captures play smoothly through ffmpeg.
- **P0a specific (ffmpeg loses smoothness on irregular-AV-sync encodes)** → CONFIRMED. Web-editor outputs with audio leading video + missing color metadata + Constrained Baseline profile trigger sustained `set_audio_speed` corrections that back-pressure the sidecar dispatcher. Kohli set_audio_speed p99=60ms ↔ frame_latency p99=61ms (1.5× Gill's 40ms).
- **Audit P2 (audio-speed churn)** → **PROMOTED to operative P0**. The fix lives in Task 6.
- **Audit P1 (zero-copy state)** → **NOT load-bearing**. CSV shows zero_copy_active=0% on a smooth-playing file.

This pinpoints the fix: Task 6 (audio-speed IPC churn reduction) is the path to closing the smoothness gap. Tasks 5 / 7 / 8 demote.

### Standing fixtures (added 2026-05-04 ~17:12pm)

- **Smooth baseline:** `C:\Users\Suprabha\Desktop\Media\TV\Sports\Gill 43.mp4` — 50fps, 23.7 Mbps, BT.709 SDR, no encoder tag. Should never regress to stuttery.
- **Rough fixture:** `C:\Users\Suprabha\Desktop\Media\TV\Sports\Virat Kohli 141(175) Vs Australia 1st Test 2014 Ball By Ball.mp4` — 30fps, 21.2 Mbps, Clipchamp re-encode, audio leads video by 46ms. Target: bring frame_latency p99 under 40ms after Task 6.

Both are required for Task 6's smoke (regression check + improvement check).
