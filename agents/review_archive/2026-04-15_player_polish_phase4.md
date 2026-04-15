# Archive: Player Polish Phase 4 — Audio Polish

**Agent:** 3 (Video Player)
**Reviewer:** Agent 6 (Objective Compliance Reviewer)
**Objective source:**
- Kodi `xbmc/cores/AudioEngine/Engines/ActiveAE/ActiveAEResampleFFMPEG.cpp` :: `Resample` method — drift correction via `swr_set_compensation`, ±5% bound matches `m_maxspeedadjust`
- Kodi general AE patterns (amp soft-clip, feed-forward compressor)
- `PLAYER_POLISH_TODO.md:162-202` (Phase 4 batch definitions) — incl. `:180` (soft-knee DRC spec)

**Outcome:** REVIEW PASSED 2026-04-15.
**Shape:** 0 P0, 1 P1, 8 P2, 5 questions. P1 fixed in-session. 8 P2 handled via defer-with-justification (5) + two concrete fixes queued as follow-up polish + one documentation improvement committed.

**Full review body + chat.md history:** review posted with procedural swap into REVIEW.md earlier 2026-04-15; Phase 5 took REVIEW.md slot per Hemanth's "review phase 5 now" directive before Agent 3 responded; Phase 4 review body preserved in chat.md post at the "REVIEW posted for Agent 3 Player Polish Phase 4 (Audio)" header and Agent 3's full response at chat.md:11185-11212.

---

## Initial review summary

Three shipped batches reviewed: (4.1) main-app 500ms velocity-forward ticker + new `set_audio_speed` sidecar command + audio-thread `swr_set_compensation` re-arm; (4.2) volume slider 0–200% range extension + sidecar `tanh` soft-clip in the amp zone; (4.3) sidecar feed-forward DRC compressor + EqualizerPopover DRC checkbox. Batch 4.4 (passthrough) Hemanth-ratified defer out of scope.

### Parity (Present) — 11 features correct

- Sidecar `set_audio_speed` command + ±5% clamp defense-in-depth on both sides.
- Phase 1 SyncClock → Phase 4 audio feedback loop closes end-to-end for the first time.
- `swr_set_compensation` sign + formula correct for Tankoban's speed semantic (trace-verified against Kodi's opposite-convention `ratio` semantic).
- Unknown-command graceful degradation on pre-Phase-4 sidecars.
- `tanh` amp soft-clip — per-sample, cheap, stays in `(-1, +1)`.
- DRC compressor shape correct: feed-forward, stereo-peak-driven shared gain, threshold -12 dB, ratio 3:1, attack 10ms, release 100ms.
- Pipeline order correct: swr_convert (with compensation) → volume (tanh) → DRC → PortAudio.
- MMCSS Pro Audio scheduling preserved.
- Pre-warmed PortAudio stream path untouched.
- VolumeHud slider range extension with numeric cap indicator.
- DRC envelope state thread-local, reset per-file-open.

### Gaps raised

**P0:** none.

**P1:**

1. **`swr_set_compensation` is one-shot per libswresample semantics; Tankoban re-arms only on speed-change.** Kodi re-arms every Resample call when `ratio != 1.0`. `swr_set_compensation` schedules `sample_delta` samples across the NEXT `distance` output samples then returns to zero. Steady speed=0.995 drift triggers ONE re-arm of ~21ms at 48kHz then nothing — continuous drift correction fails. Fix (a): drop the speed-change guard, re-arm unconditionally when `speed != 1.0`. One line, matches Kodi.

**P2:** (8 items)
1. Soft-knee DRC per plan; hard-knee shipped.
2. No makeup gain on DRC — under-delivers "dialogue audible at low volume" use case.
3. `tanh` compresses at all levels, not just peaks.
4. 500ms velocity ticker coarser than Kodi per-chunk cadence.
5. `kCompDistance = 1024` hardcoded vs Kodi's chunk-scaled distance.
6. Amp + DRC without makeup = double-compression.
7. DRC envelope state per-file-open (correct) but not per-toggle — ~100ms transient if user re-enables mid-playback.
8. Atomic `memory_order_relaxed` — accepted as completeness note, not fix-ask.

### Questions asked

1. 2-hour drift verification run? (Agent 3: no, not yet — Phase 4 sidecar rebuild pending Hemanth's `build_qrhi.bat`.)
2. Soft-knee vs hard-knee DRC intentional? (Agent 3: no, oversight — deferred pending empirical evidence.)
3. Makeup gain use case tested? (Agent 3: no — acknowledged under-delivery for low-volume half, parked pending Hemanth listening.)
4. Tanh vs cubic trade-off record? (Agent 3: recorded; comment to be added.)
5. `kCompDistance` calibration story? (Agent 3: chosen as ~20ms at 48kHz / typical chunk size; comment to be improved; parameterization deferred.)

---

## Agent 3 response (Phase 4 fix-batch)

**P1 — ACCEPTED + FIXED (sidecar rebuild required).**

Changed the guard from "re-arm on speed change" to "re-arm on every chunk when speed != 1.0," plus: when speed drops back to unity (within 1e-4 of 1.0) and previous compensation was non-unity, explicitly call `swr_set_compensation(swr, 0, 0)` so no pad/drop residual leaks into the first unity chunk. Matches Kodi's cadence + closes the drop-back path cleanly.

Q1 answer (2-hour drift test run?): NO. STATUS.md:42 confirms "sidecar rebuild pending for Phase 4 functional verification" — audio path has not been exercised end-to-end since the Phase 4 sidecar changes shipped. P1 stays P1. Fix (a) unblocks the verification.

**P2 dispositions:**
- #1 (soft-knee) + #2 (makeup gain) + #3 (tanh levels) + #6 (amp+DRC double-compression) + #7 (DRC envelope per-toggle) — all acknowledged; deferred pending Hemanth empirical feedback per Agent 3's "want empirical evidence before touching" posture. Defensible defer-with-justification for audio subjective behaviors.
- #4 (500ms ticker) — accepted as scaffolding cost; P1 #1 fix makes ticker cadence less critical.
- #5 (kCompDistance hardcode) — comment to be improved; parameterization deferred.
- #8 (atomics) — accepted as hygiene note; no action.

Files touched: `native_sidecar/src/audio_decoder.cpp` :485-521.

---

## Agent 6 re-verdict

**P1 fix code-verified at [audio_decoder.cpp:485-521](file:///C:/Users/Suprabha/Desktop/TankobanQTGroundWork/native_sidecar/src/audio_decoder.cpp#L485-L521):**

```cpp
const double requested_speed = speed_.load(std::memory_order_relaxed);
if (std::fabs(requested_speed - 1.0) > 1e-4) {
    // Per-chunk re-arm when speed != 1.0 — matches Kodi.
    constexpr int kCompDistance = 1024;
    const int delta = static_cast<int>(kCompDistance * (1.0 / requested_speed - 1.0));
    swr_set_compensation(swr, delta, kCompDistance);
    last_applied_speed = requested_speed;
} else if (std::fabs(last_applied_speed - 1.0) > 1e-4) {
    // Drop-back to unity — explicit zero to clear any pending compensation.
    swr_set_compensation(swr, 0, 0);
    last_applied_speed = 1.0;
}
```

- **Main branch** re-arms every chunk when `speed != 1.0`. Matches Kodi's `CActiveAEResampleFFMPEG::Resample` at :208-215 shape exactly. ✓
- **Drop-back branch** explicitly zeros compensation when speed returns to unity from a non-unity state. This is a minor improvement OVER Kodi — Kodi's code path simply stops calling `swr_set_compensation` when `ratio == 1.0` and relies on libswresample's auto-drain of the last scheduled window. Agent 3's explicit zero is cleaner + more predictable.
- **Steady-unity** path skips both branches — no wasted syscall, no leakage. Correct.
- Formula + sign convention unchanged (`delta = 1024 * (1/speed - 1)` with one-sided-DOWN velocity feeding positive delta → slower audio → video catches up).
- Comment block at :490-505 documents the libswresample one-shot semantic + Kodi parity + the drop-back edge.

**P2 dispositions all sound.** Audio-subjective P2s (#1/#2/#3/#6/#7) waiting on Hemanth empirical feedback is appropriate — static review can flag the theoretical concerns but real trade-offs in tone-mapping / compression / soft-clip behavior need listening tests. #4/#5/#8 dispositions accepted.

**Note for Hemanth:** Phase 4 functional verification (sidecar rebuild + 2-hour drift test + amp/DRC subjective listening) remains pending — this REVIEW PASSED closes the static-compliance gate only. The fix batches on the same sidecar rebuild as Phase 5's P1 #1 drop (single `build_qrhi.bat` run covers both).

### Verdict

- [x] All P0 closed (n/a — none raised)
- [x] All P1 closed or justified (P1 fixed)
- [x] Ready for commit (Rule 11)

**REVIEW PASSED — [Agent 3, Player Polish Phase 4]** 2026-04-15.

Phase 4 closes. Batch 4.4 (passthrough) remains Hemanth-ratified defer as before. Audio-subjective P2s deferred pending empirical feedback per Agent 3's explicit framing. `READY TO COMMIT -- [Agent 3, Phase 4 follow-up]` at chat.md:11216 stands; batches with Phase 5 sidecar rebuild.
