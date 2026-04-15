# Review

Agent 6 writes gap reports here. One review at a time. When resolved, Agent 6 archives to `agents/review_archive/YYYY-MM-DD_[subsystem].md` and resets this file to the empty template. Then posts `REVIEW PASSED — [Agent N, Batch X]` in chat.md.

---

## REVIEW — STATUS: PASSED (archived 2026-04-14)
Requester: Agent 3 (Video Player)
Subsystem: Player Polish Phase 2 — Frame Queue Audit & Telemetry (Batches 2.1 `readBestForClock` audit + FrameCanvas consumer switch + per-tick frame-selection telemetry, 2.2 SHM-boundary overflow-drop telemetry)
Reference spec: `PLAYER_POLISH_TODO.md` Phase 2 (planning doc) + OBS async_frames pattern (`libobs/obs-source.c:4130-4191` `ready_async_frame`, `:4175-4191` `get_closest_frame`, `:1263`-ish context).
Objective: "confirm OBS-style closest-PTS pick is correct; add overflow-drop telemetry."
Files reviewed:
- `src/ui/player/FrameCanvas.h` (new members: `m_lastChosenFrameId`, `m_lastFallbackUsed`, `m_previousConsumedFrameId`, `m_lastProducerDrops`, `m_lastConsumerLateMs`)
- `src/ui/player/FrameCanvas.cpp` (consumer switch at `:792-848`, per-tick reset at `:336-337`, telemetry capture at `:405-408`)
- `src/ui/player/VsyncTimingLogger.h` (`recordSampleFromSwapChain` gets four new params; Sample struct gains four new fields)
- `src/ui/player/VsyncTimingLogger.cpp` (CSV header expanded to 15 cols, Sample serialisation writes new fields)
- `src/ui/player/ShmFrameReader.cpp:136-192` — reviewed as-is (audit subject; spec allows unchanged if audit clears)
Cross-references:
- `libobs/obs-source.c:4130-4171` — OBS `ready_async_frame` walk + 2ms tolerance
- `libobs/obs-source.c:4175-4191` — OBS `get_closest_frame` pop-front

Date: 2026-04-14

### Scope

Two-batch phase: 2.1 audits the producer-side frame-selection semantic + migrates the consumer path + adds per-tick frame-selection telemetry; 2.2 adds SHM-boundary overflow-drop telemetry (consumer-inferred, no sidecar/ShmFrameReader change). Shadow-clock status from Phase 1 still applies — SyncClock velocity has no live consumer, but Phase 2's clock source for frame selection is `readClockUs()` (sidecar audio PTS from SHM header), not SyncClock. Out of scope: Phases 3+ (HDR shader, audio drift correction, libass subtitles, error recovery). Static read only.

### Parity (Present)

**Batch 2.1 — `readBestForClock` audit + consumer switch + frame-selection telemetry**

- **Audit verdict PASS — `ShmFrameReader::readBestForClock` semantically matches OBS `get_closest_frame` / `ready_async_frame`.** Reference: OBS walks `async_frames` DARRAY, advances cursor on frames whose timestamp precedes `sys_time` with 2ms tolerance (`MAX_TS_VAR`), pops front. Tankoban: `ShmFrameReader.cpp:136-192` walks SHM slots, filters to `valid==1 && fid > m_lastFrameId && ptsUs ≤ clockUs + toleranceUs`, picks **highest fid** of the eligible set. Structural mapping:
  - OBS `async_frames` DARRAY ↔ Tankoban SHM slot ring (N fixed slots).
  - OBS mutex-protected pop ↔ Tankoban lock-free walk + torn-write re-check at `:181-185`.
  - OBS `source->last_frame_ts` watermark (monotonic timestamp) ↔ Tankoban `m_lastFrameId` watermark (monotonic uint64 frameId). `fid > m_lastFrameId` is a stricter dedup (OBS can theoretically re-show a popped frame after wrap; we can't). ✓
- **Deliberate deviations from OBS, both disclosed and defensible:**
  - OBS picks front-of-queue (oldest eligible); Tankoban picks highest fid (newest eligible). Semantically the right choice for a video renderer — show the freshest ready frame, not a stale "nearest" frame. mpv and VLC both do it this way. ✓
  - OBS tolerance `MAX_TS_VAR = 2000000ns` (2ms); Tankoban tolerance 8000us (8ms), matching mpv's half-60Hz-frame default. Gives a clean handoff across the vblank boundary. ✓
- **Torn-write safety via double-read of `valid` flag.** Code: `ShmFrameReader.cpp:182-185` reads valid, copies metadata + data pointer, re-reads valid — returns invalid on mismatch. Lock-free under single-writer-sidecar / single-reader-main-app topology. Equivalent safety to OBS's async_mutex under its own multi-writer model. ✓
- **Zero `ShmFrameReader` changes.** Spec's scope-fence at `PLAYER_POLISH_TODO.md:90` listed `ShmFrameReader.cpp`, but the audit came back clean — implementation was already correct. No code shipped in that file. Consistent with the spec's implicit exit criterion (2.1 audit + switch works either way — if the producer needed fixing, it'd be in the diff; since it didn't, it isn't). ✓
- **FrameCanvas consumer switch from `readLatest` to `readBestForClock` with fallback.** Code: `FrameCanvas.cpp:814-823`:
  ```
  const int64_t clockUs = m_reader->readClockUs();
  auto f = m_reader->readBestForClock(clockUs, 8000);
  m_lastFallbackUsed = false;
  if (!f.valid) {
      f = m_reader->readLatest();
      m_lastFallbackUsed = true;
  }
  ```
  Fallback rationale: on startup / immediately post-seek, `readClockUs()` (sidecar audio PTS) may be 0 or behind every frame's PTS → `readBestForClock` returns invalid → fall back to `readLatest` so the display shows SOMETHING while the clock catches up. ✓
- **Clock source is `readClockUs()` (sidecar audio PTS), not `SyncClock::positionUs()`.** Agent 3's disclosure at chat.md:9345 — sidecar owns audio today; SyncClock remains Phase-4-shadow. Phase 4 swaps this source when the velocity-adjusted loop lights up. Pragmatic; avoids coupling Phase 2 to the still-dead SyncClock feedback path. ✓
- **Per-tick telemetry: CSV columns 12 + 13.** Spec `PLAYER_POLISH_TODO.md:95` ("Log frameId choice per tick for correctness check"). Code: `VsyncTimingLogger.cpp:152, 172-173` — `chosen_frame_id` (uint64) + `fallback_used` (0/1). Defaults to 0/false when no frame consumed this tick (zero-copy path, skip tick, or nothing new in ring). ✓
- **Verifiable steady-state invariant.** Agent 3's disclosure at chat.md:9351 — for each contiguous span of nonzero `chosen_frame_id`, values should be strictly monotonically increasing (no dupe IDs, no skips beyond `frame_skipped=1` rows). Provides a post-hoc correctness check against the OBS spec's "no frame dupes/skips during steady-state." ✓

**Batch 2.2 — SHM-boundary overflow-drop telemetry**

- **Producer-drops inferred at consumer via consumed-id gap.** Code: `FrameCanvas.cpp:832-837` — `if (m_previousConsumedFrameId > 0 && f.frameId > m_previousConsumedFrameId) { drops = frameId - prevId - 1 }`. First-consume edge handled: `m_previousConsumedFrameId == 0` → drops = 0 because there's no baseline (sidecar may start at fid=1 legitimately). ✓
- **Consumer-late as signed raw value** in milliseconds. Code: `FrameCanvas.cpp:847` — `m_lastConsumerLateMs = (clockUs - f.ptsUs) / 1000.0`. Positive = stale display (frame is behind clock), negative = ahead within the 8ms tolerance (expected steady-state). On fallback paths (readLatest post-seek), may show large negatives until clock catches up — informative, not a bug. ✓
- **Per-tick reset + conditional update.** Code: `FrameCanvas.cpp:336-337, 832-838, 847`. Top-of-tick zeros `m_lastChosenFrameId` + `m_lastFallbackUsed`; `consumeShmFrame` sets producer-drop / consumer-late only when it actually consumes a frame. Skip ticks and no-new-frame ticks correctly emit zeros for these fields. ✓
- **CSV columns 14 + 15.** Spec `PLAYER_POLISH_TODO.md:101`. Code: `VsyncTimingLogger.cpp:153, 174-175` — `producer_drops_since_last` (uint32) + `consumer_late_ms` (double). ✓
- **Zero `ShmFrameReader` changes** (again). Agent 3's disclosure at chat.md:9456 — consumer-inferred drop detection is approximate (can't distinguish "sidecar wrote and we lost it" from "sidecar never wrote those ids because of seek/discontinuity"). For Phase 2's steady-state verification purposes this is adequate; a producer-authoritative counter would require a sidecar protocol extension (deferred to Phase 4 if needed). ✓
- **Full CSV at 15 columns:** `wall_ns, qt_interval_ns, dxgi_valid, present_count, present_refresh, sync_qpc_time, sync_refresh, frame_latency_ms, frame_skipped, latency_ema_ms, clock_velocity, chosen_frame_id, fallback_used, producer_drops_since_last, consumer_late_ms`. Phase 1 → Phase 2 grew the schema 11 → 15. Header string matches at `VsyncTimingLogger.cpp:147-153`. ✓

**OBS reference alignment**

- **`ready_async_frame` + `get_closest_frame` semantic — audit clean.** Reference: `obs-source.c:4130-4191` advances through the queue, compensates for timing jumps via `MAX_TS_VAR`, pops the front once ready. Tankoban's `readBestForClock` is a ring-slot analogue with the same spirit (filter by clock + tolerance, pick eligible, update watermark) adapted to lock-free SHM semantics. The newest-vs-oldest-eligible swap is the semantically correct departure for a video renderer.
- **OBS telemetry (lagged_frames counter) → Tankoban `frame_skipped` + `producer_drops_since_last` + `consumer_late_ms`.** OBS counts lagged frames at the render boundary; Tankoban counts present-skips (Phase 1.2) + infers ring-boundary drops + measures clock-vs-PTS lag. More granular than OBS; same telemetry intent.

### Gaps (Missing or Simplified)

Ranked P0 / P1 / P2.

**P0:** None.

**P1:** None.

**P2:**

- **Spec scope-fence text lists `.cpp` files but headers also touched.** `PLAYER_POLISH_TODO.md:90` lists `ShmFrameReader.cpp, FrameCanvas.cpp, VsyncTimingLogger.cpp`. Actual diff touched `FrameCanvas.h` and `VsyncTimingLogger.h` in addition (member declarations + logger signature). Headers are implied by the .cpp additions (can't declare a method in .cpp without a header decl), but strictly speaking the scope-fence literal is incomplete. Harmless — no out-of-scope surface touched.
- **`consumer_late_ms` vs plan's `consumer_late_since_last`.** Spec `PLAYER_POLISH_TODO.md:102` names the column `consumer_late_since_last` (implying count). Actual shipped: `consumer_late_ms` (raw signed ms). Agent 3 disclosed the deviation at chat.md:9454 + :9476: "raw value can derive any threshold-based count from the raw value but can't go the other way." Defensible — the raw value strictly subsumes the count semantic. Retroactively updating PLAYER_POLISH_TODO.md would close the spec-vs-code drift.
- **Producer-drops is consumer-inferred, not producer-authoritative.** Spec `PLAYER_POLISH_TODO.md:100` says "Track producer-side drops + consumer-late counts." Actual: producer-drops inferred from consumed-id gaps (Agent 3's disclosure at chat.md:9456). Can't distinguish "sidecar dropped" from "sidecar never wrote those ids because of seek/discontinuity." For steady-state verification (Hemanth's `60fps on 60Hz → ~0 drops; 30Hz cap → ~30/sec` criterion) this is adequate. Producer-authoritative counter would require sidecar SHM header change + corresponding reader field — Phase 4-ish territory. Acceptable Phase-2 scaffolding.
- **8ms tolerance is mpv-matching, not OBS-matching.** OBS's `MAX_TS_VAR = 2ms`. Tankoban's 8ms (half-60Hz-frame) handles vblank-boundary handoff cleanly and matches mpv. Agent 3 explicitly disclosed the deliberate deviation. No action — noting so the OBS-vs-Tankoban numeric difference is on the record.
- **Tolerance is a hardcoded `8000` microseconds at call site.** `FrameCanvas.cpp:815`. On a 144Hz monitor (6.94ms per frame), 8ms is more than one frame of tolerance — might select a frame from the previous vblank when the current is ready. Consider `toleranceUs = static_cast<int64_t>(m_expectedFrameMs * 500)` (half the detected frame interval, in microseconds) so the tolerance scales with refresh rate. Same concern I flagged on Phase 1's `kLagThresholdMs = 25.0`. Not urgent while 60Hz is the verification target.
- **Skip-tick rows emit zero-defaults for Batches 2.1/2.2 fields.** `FrameCanvas.cpp:320-322` — on `m_skipNextPresent` path, the logger is called with `frameSkipped=true` and the new fields default to 0/false at their call-site default-argument values. Correct behavior (no consume happened this tick), but Agent 6-side CSV verification should ignore `chosen_frame_id=0` + `fallback_used=0` + `producer_drops_since_last=0` + `consumer_late_ms=0.0` on rows where `frame_skipped=1`. Would be cleaner to distinguish "no-consume-this-tick" (skip / zero-copy / no-new-frame) from "consumed frame with id=0" — technically ambiguous if the sidecar ever legitimately emitted fid=0. sidecar always starts at fid ≥ 1 in practice, so exposure is zero.
- **Verification is CSV-dump-after-the-fact, no in-tick assertion.** Spec `PLAYER_POLISH_TODO.md:97` says "no frame dupes/skips during steady-state." Monotonicity of `chosen_frame_id` is checked post-hoc. In-tick `Q_ASSERT(m_lastChosenFrameId == 0 || f.frameId > m_lastChosenFrameId)` would catch regressions sooner but would bloat the hot path and only fires in debug builds. Acceptable as-is.
- **CSV schema versioning absent.** Header row at `VsyncTimingLogger.cpp:147-153` has no explicit version marker; the analyzer tool (if it checks columns by name) is resilient, but if it checks by index a future reorder breaks silently. Consider adding `# tankoban_csv_v2` comment line ahead of the header when the next phase adds/reorders. Same note I raised in Phase 1 review — still applies.

### Answers to spec-vs-ship discrepancies

Two disclosed spec deviations accepted:

1. **`consumer_late_ms` (signed ms) vs `consumer_late_since_last` (count).** Raw value strictly subsumes count semantic. Spec TODO doc should be updated to match shipped naming.
2. **Producer-drops inferred at consumer, not sidecar-side counter.** Sidecar protocol extension deferred as Phase 4+ territory if ever needed.

Both are Agent-3-disclosed and spec-equivalent for the verification goal. No gap escalation.

### Questions for Agent 3

1. **Producer-authoritative drop counter** — ever going to land (sidecar SHM header extension), or permanently consumer-inferred? If latter, flagging in the PLAYER_POLISH_TODO.md as "out of scope" would close the loop with future readers.
2. **`consumer_late_ms` naming** — update PLAYER_POLISH_TODO.md:102 retroactively? One-line edit, closes the doc drift.
3. **Refresh-proportional tolerance** (P2 #5) — fold into a Phase 7 cleanup batch, or leave the 8ms hardcode forever since the 60Hz baseline is the verification target?
4. **Skip-tick zero-default columns** — is the "ignore if `frame_skipped=1`" implicit contract documented anywhere the `analyze_vsync.py` tool consults, or is it tribal knowledge? If the latter, a one-line note in the TODO would help.

### Verdict

- [x] All P0 closed — none found.
- [x] All P1 closed or justified — none found.
- [x] Ready for commit (Rule 11).

**REVIEW PASSED — [Agent 3, Player Polish Phase 2], 2026-04-14.** Clean two-batch pass: OBS async_frames audit clean with disclosed deliberate mpv-alignment deviations (newest-eligible vs front-of-queue, 8ms vs 2ms tolerance); consumer switch correctly gated with `readLatest` fallback for startup/post-seek window; CSV schema cleanly expanded 11 → 15 columns. Two disclosed spec deviations (naming + inference approach) both defensible. Eight P2 observations, all informational. Four Qs for doc hygiene. Agent 3 clear for Rule 11 commit of Phase 2. Phase 3 (HDR color pipeline) unblocked.

**Bonus process note:** Agent 3's mid-Phase-2 pivot to Tankostream Batch 5.2 (handoff to Agent 4) was correctly paused at 2.1 complete and resumed cleanly on Hemanth's call. Scope-fence preserved throughout. No cross-contamination between the two tracks.

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
