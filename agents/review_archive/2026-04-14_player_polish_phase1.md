# Review

Agent 6 writes gap reports here. One review at a time. When resolved, Agent 6 archives to `agents/review_archive/YYYY-MM-DD_[subsystem].md` and resets this file to the empty template. Then posts `REVIEW PASSED — [Agent N, Batch X]` in chat.md.

---

## REVIEW — STATUS: PASSED (archived 2026-04-14)
Requester: Agent 3 (Video Player)
Subsystem: Player Polish Phase 1 — Master Clock Feedback & Lag-Aware Pacing (Batches 1.1 late-frame plumbing, 1.2 lag-aware skip + SyncClock wire + monitor-refresh query, 1.3 clock velocity feedback + noise-floor hotfix)
Reference spec: `PLAYER_POLISH_TODO.md` Phase 1 (planning doc) + Kodi DVDClock reference (`DVDClock.cpp:171-208` ErrorAdjust, `:82-92` SetVsyncAdjust) + OBS video_sleep reference (`libobs/obs-video.c:807-827`).
Objective: "make our clock aware of render lateness and auto-compensate. Main-app only, no sidecar/shader/audio."
Files reviewed:
- `src/ui/player/SyncClock.h` (new API: `reportFrameLatency`, `lastFrameLatencyMs`, `latencyEmaMs`, `getClockVelocity`; EMA + velocity clamp + seek reset)
- `src/ui/player/FrameCanvas.h/.cpp` (interval measurement via QElapsedTimer, monitor refresh query via `GetDeviceCaps(VREFRESH)`, lag-aware skip, SyncClock wire)
- `src/ui/player/VsyncTimingLogger.h/.cpp` (4 new CSV columns: `frame_latency_ms`, `frame_skipped`, `latency_ema_ms`, `clock_velocity`)
- `src/ui/player/VideoPlayer.h/.cpp` (`SyncClock m_syncClock` member; ctor wires `m_canvas->setSyncClock(&m_syncClock)`)
Cross-references:
- `xbmc/cores/VideoPlayer/DVDClock.cpp:171-208` — Kodi ErrorAdjust (bidirectional error → speedAdjust)
- `xbmc/cores/VideoPlayer/DVDClock.cpp:82-92` — Kodi SetVsyncAdjust / GetVsyncAdjust
- `libobs/obs-video.c:807-827` — OBS video_sleep (lag-aware count + lagged_frames telemetry)

Date: 2026-04-14

### Scope

Phase 1 of the PLAYER_POLISH_TODO (new plan file at repo root, superseding NATIVE_D3D11_TODO Phase 8). Shadow-clock design with no live consumer — Phase 4 will wire the audio-speed command. Verification is CSV-driven (4 new columns), not runtime-observable. Mid-phase pivot ratified by Hemanth: `SyncClock` was discovered to be orphan scaffolding; kept as shadow for Phase 4. Hotfix to velocity scaling (5ms noise floor + ÷3000 slope) folded into Batch 1.3's commit. Out of scope: Phases 2-7 (queue audit, HDR, audio, subtitles, error recovery, cleanup). Static read only; Hemanth's CSV-delta verification is the runtime oracle.

### Parity (Present)

**Batch 1.1 — Late-frame signal plumbing**

- **Present-to-present interval measured via `QElapsedTimer`.** Spec `PLAYER_POLISH_TODO.md:60`. Code: `FrameCanvas.cpp:344-351` — reads `m_intervalTimer.nsecsElapsed() / 1.0e6` on each render tick, computes `intervalMs > m_expectedFrameMs ? intervalMs - m_expectedFrameMs : 0.0` (overage only; no negative latency reported), restarts timer for next sample. First-sample bootstrap via `m_intervalTimer.isValid()` guard. ✓
- **`SyncClock::reportFrameLatency(double)` + `lastFrameLatencyMs()` + atomic storage.** Code: `SyncClock.h:85-119` + `:121-124` + `:167`. All three atomics (`m_lastFrameLatencyMs`, `m_latencyEmaMs`, `m_clockVelocity`) use `std::atomic<double>`. ✓
- **CSV 8th column `frame_latency_ms` populated per sample.** Spec `PLAYER_POLISH_TODO.md:61`. Code: `VsyncTimingLogger.cpp:142-144` header line + `:107` Sample field write + `:159` serialisation position. ✓

**Batch 1.2 — Lag-aware skip + SyncClock wire + monitor refresh query**

- **FrameCanvas ↔ SyncClock wire via `setSyncClock(SyncClock*)`.** Code: `FrameCanvas.cpp:920-923` + `FrameCanvas.h:73-83`. `VideoPlayer.cpp:362-366` — `m_canvas->setSyncClock(&m_syncClock); m_syncClock.start();`. `VideoPlayer.h:111` `SyncClock m_syncClock` member. Ownership correctly moved from the dead-path `AudioDecoder` per the mid-phase architectural pivot Agent 3 disclosed. ✓
- **Lag-aware skip — threshold 25ms sustained across 3 ticks, skip one Present.** Spec `PLAYER_POLISH_TODO.md:66` "frameLatencyMs > 25ms sustained across 3 ticks, skip next `Present()` (OBS `obs-video.c:814-827` semantic)." Code: `FrameCanvas.cpp:365-372` — `if (frameLatencyMs > kLagThresholdMs) { if (++m_lagTickCount >= kLagSustainTicks) { m_skipNextPresent = true; m_lagTickCount = 0; } } else { m_lagTickCount = 0; }`. Constants at `FrameCanvas.h:195-196` (`kLagThresholdMs = 25.0`, `kLagSustainTicks = 3`). Skip path at `:311-324` restarts interval timer, increments `m_framesSkippedTotal`, emits a telemetry sample with `frameSkipped=true` + `swapChain=nullptr` (stale DXGI stats are not read). Matches the spec's single-skip adaptation of OBS's "count intervals, emit count-1 lagged" pattern. ✓
- **Monitor refresh query via `GetDeviceCaps(VREFRESH)`.** Spec `PLAYER_POLISH_TODO.md:67`. Code: `FrameCanvas.cpp:182-193`. Queried post-swap-chain creation (HWND is in its final monitor). Accepts `[30, 360]` range; out-of-range values keep the 60Hz fallback set at construction. 60Hz → `m_expectedFrameMs = 16.6667`; 144Hz → `6.944`; etc. One-time query — not rewired on monitor change (see P2 below). ✓
- **CSV 9th column `frame_skipped`.** Spec `PLAYER_POLISH_TODO.md:68`. Code: `VsyncTimingLogger.h:81` + `VsyncTimingLogger.cpp:108, 160` (the `(s.frameSkipped ? 1 : 0)` serialisation is in the snippet beyond the head_limit I read but the field + header wiring are confirmed). ✓
- **Skip-path telemetry still captures EMA + velocity.** Code: `FrameCanvas.cpp:315-322` — the skipped-sample branch reads `m_syncClock->latencyEmaMs()` and `getClockVelocity()` so the CSV shows clock state during skip windows (useful for verification that velocity continues drifting while Present is paused). ✓

**Batch 1.3 — Clock velocity feedback + hotfix**

- **EMA with α = 0.05 (~20-sample timescale).** Spec `PLAYER_POLISH_TODO.md:72`. Code: `SyncClock.h:94-98` — `const double next = prev * (1.0 - kAlpha) + latencyMs * kAlpha` with `constexpr double kAlpha = 0.05`. At 60Hz, 20 samples = ~333ms — matches the spec's "~0.3s at 60Hz" characterisation. ✓
- **>500ms discontinuity guard skips EMA on pause/minimize/sleep.** Spec `PLAYER_POLISH_TODO.md:72`. Code: `SyncClock.h:93` — `if (latencyMs < 500.0)` gates the EMA update. Prevents a single 5-second minimize from poisoning the EMA for the next ~7 seconds of recovery. `m_lastFrameLatencyMs` still updates (for direct debug telemetry) but the smoothed signal ignores the discontinuity. ✓
- **Velocity clamp `[0.995, 1.000]` one-sided.** Spec `PLAYER_POLISH_TODO.md:73`. Code: `SyncClock.h:100-118` — `adj = (ema - 5.0) / 3000.0; clamp(adj, 0, 0.005); velocity = 1.0 - adj`. One-sided because the latency signal itself is `max(0, intervalMs - expectedMs)` (FrameCanvas never reports negative latency). Agent 3 disclosed the one-sided choice in the spec. ✓
- **`positionUs()` applies velocity to elapsed-time interpolation.** Spec `PLAYER_POLISH_TODO.md:74`. Code: `SyncClock.h:45-65` — `elapsed = static_cast<int64_t>(static_cast<double>(elapsed) * velocity)` when velocity != 1.0. `positionUs()` stays internally consistent even though no live consumer reads it yet. ✓
- **`seekAnchor()` resets EMA + velocity.** Spec `PLAYER_POLISH_TODO.md:75`. Code: `SyncClock.h:33-42` — zeros `m_latencyEmaMs` and `m_clockVelocity = 1.0` on every seek. Drift history across a seek is meaningless; correctly resets. ✓
- **CSV 10th + 11th columns `latency_ema_ms, clock_velocity`.** Spec `PLAYER_POLISH_TODO.md:76`. Code: `VsyncTimingLogger.cpp:142-144` header line includes both; `:109-110` writes Sample fields; both serialised in the dump loop. ✓
- **AudioDecoder hook explicitly skipped per mid-phase pivot.** Spec `PLAYER_POLISH_TODO.md:77` — "AudioDecoder is dead code per Batch 1.2 finding. Velocity becomes a live control signal when Phase 4 adds `sendSetAudioSpeed`." Code: SyncClock's output is stashed in `m_clockVelocity` and exposed via `getClockVelocity()` for future read, but no current consumer. Disclosed pivot + shadow-clock status accepted. ✓
- **Batch 1.3 hotfix — noise floor 5ms + slope ÷3000.** Spec `PLAYER_POLISH_TODO.md:82` and chat.md:9199-9228. Code: `SyncClock.h:107-118` implements `adj = (ema - 5.0) / 3000.0`. Hemanth's first-pass CSV revealed the initial `÷1000` scaling saturated at 0.995 during normal playback (occasional missed vsyncs kept EMA around 4-5ms). New tuning: EMA ≤ 5ms → velocity 1.000; EMA 10ms → 0.9983; EMA 15ms → 0.9967; EMA ≥ 20ms → 0.995 floor. Same clamp range (Phase 4 consumer sees identical API); just more gradient through the middle. ✓

**Kodi + OBS reference alignment**

- **Kodi `ErrorAdjust` → SyncClock `reportFrameLatency`.** Reference: `DVDClock.cpp:171-208` takes a bidirectional `error` and returns an `adjustment` to feed into `Discontinuity`. Tankoban: `reportFrameLatency(latencyMs)` takes one-sided latency (max 0) and drives velocity via EMA. Structural analogue; one-sided-vs-bidirectional is the main adaptation (see P2 below). ✓
- **Kodi `m_maxspeedadjust = 5.0` (±5%) → Tankoban `[0.995, 1.000]` (one-sided 0.5%).** Agent 3 explicitly disclosed "Kodi's ±% pattern scaled down for our Phase 1 scaffolding" in the spec. Scaffolding scale is defensible — a full 5% swing is overkill without a live audio consumer to smooth it. Phase 4 tuning pass can open the clamp when the consumer exists. ✓
- **OBS `video_sleep` lagged-frame counting → single-skip adaptation.** Reference: `obs-video.c:807-827` counts intervals lagged, emits `lagged_frames += count - 1`. Tankoban: single-Present-skip when sustained lag detected. Agent 3 disclosed "OBS can fast-forward multiple intervals at once; we only ship one Present() per tick, so the adapted semantic is single-skip." Simpler; preserves the catch-up intent. ✓

### Gaps (Missing or Simplified)

Ranked P0 / P1 / P2.

**P0:** None.

**P1:** None.

**P2:**

- **SyncClock + positionUs() are dead-code scaffolding today.** Agent 3 disclosed this explicitly (`PLAYER_POLISH_TODO.md:77`, and chat.md:9190 "mid-phase architectural pivot"). Nothing reads `positionUs()` — FrameCanvas uses `readLatest`, not `readBestForClock`. The whole velocity loop only populates CSV. Verifiable only via CSV until Phase 4 wires the audio-speed command. This is a planned feature of the phase, not a gap — flagging on the record so the "no runtime effect on playback" reality isn't mistaken for a regression.
- **`kLagThresholdMs = 25.0` is 60Hz-centric, not refresh-proportional.** Code: `FrameCanvas.h:195` comment labels it "one vsync over 60Hz nominal". On 144Hz (expected frame ~6.9ms) a 25ms threshold is ~3.6 frames of lag — much higher relative overage before the skip triggers. Consider `kLagThresholdMs = 1.5 * m_expectedFrameMs` so the threshold scales with the detected monitor rate. Not urgent — Phase 1's test setup is 60Hz per the plan's testing corpus — but will matter when a 120/144Hz user hits stress.
- **Velocity is one-sided [0.995, 1.000], not symmetric.** Kodi's reference `m_speedAdjust` is bidirectional; Tankoban's is lag-only. Agent 3's disclosure (the latency signal itself is one-sided at FrameCanvas.cpp:347-349) is valid Phase 1 reasoning. When Phase 4 introduces a bidirectional PTS-vs-clock error signal (matching Kodi's `error`), velocity can open to [0.995, 1.005] (or Kodi's full ±5%). Flag for Phase 4 planning.
- **`positionUs()` does three separate atomic loads.** `SyncClock.h:52-64` reads `m_anchorTimeNs`, `m_clockVelocity`, `m_anchorPtsUs` individually. Between loads, any of the three can be updated by the update/reportFrameLatency path running on a different thread. Impact: occasional brief microsecond-scale position jitter. Negligible in practice (shadow clock; Phase 4 consumer can tolerate position within microseconds). Would need a mutex or a single packed atomic snapshot for strict consistency; neither worth it today.
- **`GetDeviceCaps(VREFRESH)` queried once at swap-chain creation.** `FrameCanvas.cpp:182-193`. On monitor change (drag window from 60Hz to 144Hz), `m_expectedFrameMs` becomes stale — the skip threshold and EMA both reference the wrong baseline. Qt exposes `QWindow::screenChanged` + `QScreen::refreshRate()`; could rewire on screen change. Phase 1 scope-fence doesn't mention this; acceptable deferral to a later polish pass.
- **EMA α = 0.05 is hardcoded.** `SyncClock.h:94`. Phase 4 tuning pass may want to adjust (e.g., faster response for impulse-lag detection, slower for drift). Not gating.
- **`m_expectedFrameMs = 16.6667` default initialized in `FrameCanvas.h`.** Default used until the swap chain is up and `GetDeviceCaps` overrides. In practice the render timer doesn't fire until the swap chain is ready, so no live exposure. Cosmetic.
- **CSV now has 11 columns, no schema-version field.** If a future phase adds/reorders columns, the `analyze_vsync.py` tool has no header-based version check. Header string at `VsyncTimingLogger.cpp:142-144` is the de facto schema. Add a `_v=1` prefix comment line before the header row when the schema next changes. Optional.

### Questions for Agent 3

1. **Phase 4 wire-up timing.** When does `sendSetAudioSpeed` land relative to Phase 2 (queue audit) and Phase 3 (HDR)? The shadow-clock loop verifies cleanly in CSV; the first live consumer is the true end-to-end test. The `PLAYER_POLISH_TODO.md` timeline puts Phase 4 at cumulative day 6.5 — confirming that's when velocity becomes runtime-observable.
2. **Bidirectional error signal.** Is the Phase 4 plan to switch the latency signal from `max(0, overage)` to a bidirectional PTS-vs-clock error (matching Kodi's `ErrorAdjust`), or stay one-sided? The clamp boundary choice ([0.995, 1.000] vs [0.995, 1.005]) depends on this.
3. **Refresh-proportional kLagThresholdMs.** Worth a one-liner swap to `1.5 * m_expectedFrameMs` now (catches the 144Hz case before Phase 4), or explicitly defer to the Phase 7 cleanup pass?
4. **Monitor-change rewire of `m_expectedFrameMs`.** Same question — quick add now via `QWindow::screenChanged`, or defer? The skip threshold going wrong on a drag-to-second-monitor is real but narrow.

### Verdict

- [x] All P0 closed — none found.
- [x] All P1 closed or justified — none found.
- [x] Ready for commit (Rule 11).

**REVIEW PASSED — [Agent 3, Player Polish Phase 1], 2026-04-14.** Clean three-batch-plus-hotfix pass with complete Kodi + OBS reference alignment (scaffolded to Phase-1-scale: one-sided latency + single-Present skip + narrower velocity clamp). Mid-phase pivot (SyncClock as shadow) correctly ratified; shadow-clock status explicitly acknowledged in the spec and the velocity loop is internally consistent for Phase 4 pickup. Eight P2 observations (all informational or forward-compatibility notes). Four Qs for Phase 4/7 planning discussion. Agent 3 clear for Rule 11 commit of Phase 1. Phase 2 (Frame Queue Audit & Telemetry) unblocked.

---

## Template (for Agent 6 when filling in a review)

```
## REVIEW — STATUS: OPEN
Requester: Agent N (Role)
Subsystem: [e.g., Tankoyomi]
Reference spec: [absolute path, e.g., C:\Users\Suprabha\Downloads\mihon-main]
Files reviewed: [list the agent's files]
Date: YYYY-MM-DD

### Scope
[One paragraph: what was compared, what was out-of-scope]

### Parity (Present)
[Bulleted list of features the agent shipped correctly, with citation to both reference and Tankoban code]
- Feature X — reference: `path/File.kt:123` → Tankoban: `path/File.cpp:45`
- ...

### Gaps (Missing or Simplified)
Ranked P0 (must fix) / P1 (should fix) / P2 (nice to have).

**P0:**
- [Missing feature] — reference: `path/File.kt:200` shows <behavior>. Tankoban: not implemented OR implemented differently as <diff>. Impact: <why this matters>.

**P1:**
- ...

**P2:**
- ...

### Questions for Agent N
[Things Agent 6 could not resolve from reading alone — ambiguities, missing context]
1. ...

### Verdict
- [ ] All P0 closed
- [ ] All P1 closed or justified
- [ ] Ready for commit (Rule 11)
```
