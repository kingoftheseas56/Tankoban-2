# SIDECAR_DISPATCHER_NON_BLOCKING_FIX_TODO

**Status:** Phase A SHIPPED 2026-04-26 ~01:05 by Agent 4 (Stream mode). Sidecar BUILD GREEN, main app BUILD OK. §5 LOCKED VIA RULE 14 2026-04-25 ~22:35. Awaiting Hemanth smoke. Phase A.3 conditional on audio-track-switch surfacing as separate bug. Phase B queued.

**Owner:** Agent 4 (Stream mode), end-to-end execution. Bug surfaced in stream mode, but fix lands in `native_sidecar/src/`. Agent 3 is in HEMANTH-DRIVEN MODE (player UI direction-picking only — sidecar threading is fair game for Agent 4). See §5 question 5 for ratification.

**Source:**
- Memory `project_sidecar_dispatcher_non_blocking_decision.md` (architectural decision from previous wake `ade60215` 2026-04-25 ~21:30 — single sidecar, non-blocking dispatcher, NOT two sidecars).
- Plan file [`C:\Users\Suprabha\.claude\plans\so-create-a-comprehensive-effervescent-otter.md`](file:///C:/Users/Suprabha/.claude/plans/so-create-a-comprehensive-effervescent-otter.md) (Hemanth-approved 2026-04-25 ~22:15 in plan mode).
- Hemanth directive 2026-04-25 ~21:30: *"I want no compromises on our app. What is the TRUEST fix?"*

**Skills invoked at authoring** (per contracts-v3): `[/superpowers:verification-before-completion, /superpowers:systematic-debugging, /superpowers:writing-plans, /simplify]`. No `src/` touched at authoring.

---

## §1 Goal

Close the dispatcher-blocking bug class in the native sidecar — the structural defect that surfaced as Hemanth's two stream-mode bugs:

1. **"Can't pause the video in stream mode"** — Space registers but the sidecar doesn't process it for many seconds.
2. **"Video keeps playing even after I close the video"** — close handler fires but audio + video continue until the buffer drains.

Layer-1 parity fixes already shipped at [`StreamPage.cpp`](src/ui/pages/StreamPage.cpp) (`setFocus()` after show + `stopPlayback()` before stopStream) but they only fix the EASY path. The HARD path: when `handle_set_tracks` ([`native_sidecar/src/main.cpp:1195-1254`](native_sidecar/src/main.cpp#L1195-L1254)) is processing a subtitle preload on a 4.6 GB HTTP source, the dispatcher thread is wedged inside `preload_subtitle_packets` ([`main.cpp:1148-1187`](native_sidecar/src/main.cpp#L1148-L1187))'s tight `while (av_read_frame(...))` loop reading every packet from the network. Pause + stop + shutdown commands queue up behind it. Render keeps drawing because the render path doesn't go through the dispatcher.

**Truest fix:** the dispatcher is **never allowed to block.** Slow work must run on a worker thread; the dispatcher's job is read → parse → ack → hand off → loop. Once enforced, "can't pause" and "video keeps playing" become structurally impossible.

We do this in **one sidecar**, not two. Two-sidecar split would duplicate the engine and re-introduce the same bug class on each side.

---

## §2 Why now

- Hemanth-driven directive — "I want no compromises" + the user-facing bugs Hemanth reported can't be fixed at the StreamPage layer. The fix has to land in sidecar threading.
- Stream domain is otherwise idle — STREAM_SERVER_PIVOT P3 closed 2026-04-25 ~00:16; no other Agent 4 fix-TODO is in flight.
- Sidecar is in a quiet window — no Agent 3 player domain work scheduled (HEMANTH-DRIVEN MODE; agent-initiated player audits suspended).
- No open Congress, no open HELP request — wake bandwidth is clean.
- Plan-mode design + ground-truth exploration already done same-wake (saved at [`C:\Users\Suprabha\.claude\plans\so-create-a-comprehensive-effervescent-otter.md`](file:///C:/Users/Suprabha/.claude/plans/so-create-a-comprehensive-effervescent-otter.md)). Phase A landing zone is precisely identified — narrow, not a blanket dispatcher rewrite.

---

## §3 Constraints (load-bearing)

- **Single sidecar, not two.** Architectural pushback already made + locked in `project_sidecar_dispatcher_non_blocking_decision.md`. If Hemanth re-frames as two-sidecars, push back ONCE more then defer per memory directive. Two-sidecar split duplicates the engine + reintroduces the same bug class on each side.
- **Reuse existing threading primitives.** `std::thread` + `std::mutex g_session_mutex` + the writer-thread queue + condition variable in [`protocol.cpp:29-127`](native_sidecar/src/protocol.cpp#L29-L127). NO new thread pool. Mirror the existing `handle_open` → `open_worker` shape ([`main.cpp:920-935`](native_sidecar/src/main.cpp#L920-L935)) — that pattern is already correct.
- **Acks are already non-blocking.** `write_ack` enqueues to a queue; the writer thread flushes ([`protocol.cpp`](native_sidecar/src/protocol.cpp)). The bug is NOT ack-related. The bug is that the body AFTER the ack runs on the dispatcher thread. Phase A's job: move that body to a worker.
- **Cancellation is cooperative, not forced.** ffmpeg's `interrupt_callback` mechanism is the existing escape hatch. We add an atomic flag + check it inside the read loop. We do NOT thread-cancel or send signals.
- **No human-day deadlines.** Per `feedback_no_human_days_in_agentic.md` — phases measured in summons (Phase A ~1-2; Phase B ~3-5).
- **One-fix-per-rebuild.** Per `feedback_one_fix_per_rebuild.md` — Phase A.1 (subtitle preload off dispatcher) ships before Phase A.2 (audit pass cleanup). No batching.
- **Stranded edits in `native_sidecar/src/`.** Working tree shows pre-existing unstaged modifications to `audio_decoder.h` + `demuxer.h` + `stream_prefetch.{h,cpp}` + `video_decoder.h` + `subtitle_renderer.{h,cpp}`. Read those FIRST in Phase A pre-flight; confirm they don't conflict with Phase A scope. If they do, escalate to Hemanth before Phase A starts.

---

## §4 Out of scope

- **Main app (`src/`) changes.** This TODO is sidecar-only. The StreamPage parity fixes from prior wake are layer-1 and stay as-shipped.
- **Qt-side player widget split.** No two-VideoPlayer. The shared `VideoPlayer` widget across modes is correct.
- **Rewriting the IPC protocol.** No new commands, no new event types beyond what Phase A's worker needs to emit completion (likely reusing existing `tracks_changed` event).
- **General sidecar latency improvements** (audio glitch, frame pacing, HDR tone-mapping). Different bug classes, separate scope.
- **Stream-server-side fixes.** The Stremio stream-server (Node.js subprocess) is a different process; this TODO is the C++ sidecar only.
- **Phase C — two-entity revisit.** Listed in §6 for completeness but my read is Phase A + Phase B close the bug class entirely; Phase C never fires.

---

## §5 Decisions — LOCKED VIA RULE 14 (Agent 4 calls, 2026-04-25 ~22:35)

Five questions surfaced during planning. I initially menu'd them to Hemanth as PROPOSED picks; Hemanth invoked Rule 14 ("I cannot make the optimal choice because I do not know shit") — correctly, since all 5 are technical/coordination questions, not product/UX/strategic. Locked as Agent 4 calls per Rule 14 + `feedback_decision_authority.md` + `feedback_coordination_mechanics_not_hemanth.md`. Hemanth retains override authority if smoke surfaces a wrong call.

1. **Phase A scope — narrow or broad?**
   - ✅ **LOCKED — BROAD.** Phase A.1 = `handle_set_tracks` worker-thread split (smoking gun). Phase A.2 = audit pass on every other handler in [`main.cpp:1638-1745`](native_sidecar/src/main.cpp#L1638-L1745); fix obvious second blockers in the same phase, queue large refactors as Phase A.3.
   - **Rationale:** the audit is cheap (~30 min of code reading); shipping it inside Phase A avoids a "we knew there was another blocker but didn't look" tail wake.

2. **Cancellation behavior on rapid track-switch.**
   - ✅ **LOCKED — ABORT THE FIRST.** If a second `set_tracks` arrives while one is in-flight, the first worker checks the cancellation atomic, drops what it was doing, and the second worker proceeds.
   - **Rationale:** matches user intent (they wanted the second one). Alternatives — wait or ignore — produce stale subtitle state or unresponsive UI.

3. **Phase B environment gate.**
   - ✅ **LOCKED — ENV-GATED for one wake of soak.** New `Source` abstraction lives behind `TANKOBAN_SIDECAR_SOURCE_V2=1` for one wake, then drop the gate.
   - **Rationale:** Phase B touches probe + seek + preload + demuxer. Bigger surface, riskier rollout; soak window matches `feedback_one_fix_per_rebuild` discipline.

4. **Author + start same-wake, or author-then-ratify gate?**
   - ✅ **LOCKED — AUTHOR + START SAME-WAKE.** Architectural decision is already in memory; §5 just confirms scope shape.
   - **Rationale:** brotherhood norm is author-then-ratify, but the architecture is pre-settled. Compresses one wake.

5. **Owner.**
   - ✅ **LOCKED — Agent 4 owns end-to-end.** Bug is in stream mode's lane; Agent 3 is in HEMANTH-DRIVEN MODE (UI-only, sidecar-threading is fair game).
   - **Rationale:** Agent 4 already has the architectural memory. Cross-agent handoff would re-establish context.

---

## §6 Phases

### Phase A — Non-blocking dispatcher discipline (~1-2 summons, Agent 4)

**Goal in plain terms:** Move the slow subtitle scan off the dispatcher thread so Pause and Close always respond instantly, no matter what else is going on.

**Goal in code terms:** `handle_set_tracks` parses + validates + acks on the dispatcher thread, then spawns a `std::thread(set_tracks_worker, cmd)` for the slow body. The dispatcher returns to reading stdin within microseconds.

**Layman summary:**
> Today the engine has one worker doing everything. Phase A teaches that worker to *delegate* — when a slow job comes in, they push it onto a side desk and immediately go back to handling the next instruction. Pause and Close are now always milliseconds, never blocked.

**Scope (Phase A.1 — the fix):**
- Refactor `handle_set_tracks` ([`main.cpp:1195-1254`](native_sidecar/src/main.cpp#L1195-L1254)): keep parse + validate + ack on the dispatcher; move audio decoder stop/start + `preload_subtitle_packets` call into a new free function `set_tracks_worker(json cmd)`. Spawn it via `std::thread(set_tracks_worker, std::move(cmd)).detach()`.
- Mirror the existing `handle_open` → `open_worker` shape exactly. Same `std::thread + .detach()` pattern. Same mutex acquire inside the worker. NO new abstraction.
- Concurrency rule for `set_tracks_worker`: only one in flight at a time. Add `std::atomic<bool> g_set_tracks_cancel{false}` and `std::atomic<int> g_set_tracks_inflight{0}`. New worker increments inflight, sets cancel=true on entry if a previous worker is alive, waits briefly for it to drop out, then proceeds with cancel=false. Old worker checks cancel inside the preload loop.
- Cancellation point inside `preload_subtitle_packets` ([`main.cpp:1148-1187`](native_sidecar/src/main.cpp#L1148-L1187)): one `if (g_set_tracks_cancel.load()) { /* cleanup, return */ }` check at top of the `while (av_read_frame ...)` loop. RAII handles cleanup automatically (file handle, packet ref).
- Emit `tracks_changed` event on worker completion (existing IPC event; verify path).

**Scope (Phase A.2 — the audit):**
- Walk every handler in the dispatch table at [`main.cpp:1638-1745`](native_sidecar/src/main.cpp#L1638-L1745). For each, check if the body after `write_ack` does any of: (a) HTTP I/O via ffmpeg, (b) decoder restart, (c) file system traversal, (d) `std::this_thread::sleep_for`. Document findings inline in `main.cpp` with `// PHASE_A_AUDIT 2026-04-25` comments above each handler.
- If a second blocker is found and the fix is small (similar shape to `set_tracks_worker`), ship in Phase A.2 same-wake.
- If a second blocker is found and the fix is large (new abstraction needed), queue as Phase A.3 with its own scope block.

**Files touched:**
- `native_sidecar/src/main.cpp` — `handle_set_tracks` body split + new `set_tracks_worker` + `preload_subtitle_packets` cancellation hook + dispatch-table audit comments
- *Possibly* `native_sidecar/src/audio_decoder.{h,cpp}` if the decoder stop/restart sequence needs an async-friendly variant. Likely not — decoder's `start()` already spawns its own thread internally. Check during pre-flight.

**Acceptance:**
1. Sidecar build green: `powershell -File native_sidecar/build.ps1` produces fresh `ffmpeg_sidecar.exe`.
2. Main app build green: `build_check.bat` BUILD OK.
3. **Smoke A — HTTP heavy stream:** Open Tankoban → Stream tab → play Saiki Kusuo Ep 12 (or any 4 GB+ HTTP MKV with multiple subtitle tracks). Wait first frame. Toggle subtitle track. **While the subtitle worker is running** (visible as `set_tracks_worker started` line in `sidecar_debug_live.log`), press Space. Pause must register inside ~50ms (audio freezes, icon flips). Press Space again — resume. Press close — audio + video stop within ~100ms.
4. **Smoke B — local file regression:** Same flow but on a local file. Behavior unchanged from today.
5. **IPC latency log evidence:** `out/ipc_latency.log` (existing instrumentation per CLAUDE.md build quick reference) shows pause + stop p99 < 50ms during a `set_tracks` worker run.
6. Rule 17 cleanup: `scripts/stop-tankoban.ps1` post-smoke.

**Exit criterion:** Hemanth presses Space mid-track-switch on a heavy HTTP stream and pause registers instantly; presses close and the window closes instantly. The empirical proof is the `ipc_latency.log` numbers + Hemanth's "yeah, it works."

**Depends on:** §5 ratification (questions 1, 2, 4, 5 specifically — Phase B gate is question 3).

---

### Phase B — `Source` abstraction inside the sidecar (~3-5 summons, Agent 4, env-gated)

**Goal in plain terms:** Change the recipe for "load subtitles" based on whether the bytes come from a local file or HTTP. For local files, scan upfront (fast, fine). For HTTP, fetch each subtitle packet as the user actually crosses it (lazy, no cold scan).

**Goal in code terms:** Introduce `class Source` interface with `LocalSource` and `HttpSource` implementations. Replaces scattered `is_http` branches with virtual method dispatch. `HttpSource::preload_subtitle_packets()` is lazy / no-op; `LocalSource::preload_subtitle_packets()` keeps the current eager implementation.

**Layman summary:**
> Today the engine uses the same recipe for "load subtitles" no matter where the video came from. That works fine for a file on your hard drive (one quick scan) but it's wrong for streaming (it tries to download the entire file just for subtitles). Phase B introduces two recipes: one for local files, one for streaming. Same engine; the engine just picks the right recipe based on the source.

**Scope:**
- New `native_sidecar/src/source.{h,cpp}` defining `class Source` (abstract) + `LocalSource` (concrete, eager) + `HttpSource` (concrete, lazy). Each declares: `random_seek_cost()`, `supports_byte_range()`, `expected_latency_band()`, `preload_subtitle_packets(stream_idx)`, `seek(pts, flags)`, `probe_strategy()`.
- Demuxer construction picks a `Source` once at open time based on URL scheme. Replaces `is_http` string check at [`demuxer.cpp:186-187`](native_sidecar/src/demuxer.cpp#L186-L187).
- Subsystems that branched on `is_http` (probing tiers, preload, seek tuning, stream prefetch behavior) now call `source->method()` instead.
- Lazy subtitle preload for `HttpSource`: NO separate scan. Subtitle packets are demuxed on-the-fly during normal playback as the player crosses their PTS. Track metadata (count, language, codec) still comes from probe (separate, unaffected).
- Env gate: `TANKOBAN_SIDECAR_SOURCE_V2=1` enables new code path; absent or `=0` keeps the legacy `is_http` branches in place. One wake of soak.

**Files touched:**
- `native_sidecar/src/source.{h,cpp}` (NEW, ~150-250 LOC)
- `native_sidecar/src/demuxer.{h,cpp}` (rewire `is_http` branches → `source->method()`)
- `native_sidecar/src/main.cpp` (`set_tracks_worker` calls `source->preload_subtitle_packets()`)
- *Possibly* `native_sidecar/src/stream_prefetch.{h,cpp}` if it has parallel `is_http` branches

**Acceptance:**
1. All Phase A acceptance still passes (no regression).
2. Subtitle track toggle on HTTP source completes in **<500ms** (vs. 30+ seconds today). Subtitles appear correctly as you scrub through the file.
3. Subtitle track toggle on local source still works (eager preload preserved via `LocalSource`).
4. Probe time on HTTP unchanged or better.
5. Env gate flip OFF restores legacy `is_http` behavior bit-identical.

**Depends on:** Phase A shipped + soaked at least one wake. §5 question 3 ratification (env-gate vs direct land).

---

### Phase C — Two-entity revisit (DEFERRED, probably never)

Listed for completeness so future sessions don't re-litigate. Phase A + Phase B close the user-visible bug class entirely. If after Phase B soak Hemanth still observes mode-specific weirdness that the `Source` abstraction can't explain, Phase C considers two-entity. **Default expectation: Phase C never fires.**

---

## §7 Risk surface

- **R1: Cancellation race in subtitle preload.** Second `set_tracks` aborts the first; if the abort is mishandled, file handle / packet ref / mutex could leak.
  - **Mitigation:** atomic cancel flag checked at one point (top of read loop); cleanup uses RAII so abort = thread function returns = destructors fire automatically. No manual cleanup paths.

- **R2: Audio decoder restart while old decoder still on background thread.** Stop-old-then-start-new must be sequenced to prevent two decoders writing to the same audio output simultaneously.
  - **Mitigation:** `set_tracks_worker` holds `g_session_mutex` during the swap exactly as the synchronous version does today. The only thing that moved off the dispatcher is which thread holds the mutex. Decoder ordering invariant unchanged.

- **R3: Phase A audit reveals other dispatcher-blocking handlers we haven't seen.**
  - **Mitigation:** Phase A.2 is explicitly the audit pass. Small fixes ship same-wake; large refactors queue as Phase A.3 with their own RTC.

- **R4: Phase B `Source` abstraction has more `is_http` callsites than predicted.**
  - **Mitigation:** Phase B is summons 3-5 specifically because of LOC uncertainty. Time-box at summon 2; if scope creep hits, re-scope or re-phase.

- **R5: HTTP lazy subtitle fetch breaks subtitle UI.** Track list might show "0 packets" or subtitle popover might fail to populate until user scrubs.
  - **Mitigation:** track-list metadata comes from probe (`mediaInfo` event in [`main.cpp:462`](native_sidecar/src/main.cpp#L462)), NOT from preload. Already separate signal paths. Phase B keeps the metadata emit; only the packet preload becomes lazy.

- **R6: Smoke discovers the bug isn't actually preload — it's mutex contention on `g_session_mutex`.** If Pause is still slow with `set_tracks_worker` running, then the dispatcher thread is itself blocked trying to acquire `g_session_mutex` for a different command (e.g., `handle_pause` itself locks the mutex).
  - **Mitigation:** Phase A acceptance smoke explicitly measures pause-IPC round-trip. If R6 fires, Phase A.2 expands to mutex-granularity work — split `g_session_mutex` into command-class mutexes (decoder-state vs render-state vs IPC-state).

- **R7: ffmpeg's HTTP read inside `av_read_frame` doesn't honor cancellation flag — it sleeps on socket I/O for 30+ seconds before next iteration.**
  - **Mitigation:** ffmpeg supports `AVIOInterruptCB` for this exact case. If R7 surfaces in Phase A smoke, wire an `interrupt_callback` (~10 LOC) on the AVFormatContext used inside `preload_subtitle_packets`. The callback returns 1 (interrupt) when our atomic cancel flag is set.

- **R8: Stranded `native_sidecar/src/` edits (working-tree dirt) conflict with Phase A scope.**
  - **Mitigation:** Phase A pre-flight reads `audio_decoder.h`, `demuxer.h`, `stream_prefetch.{h,cpp}`, `video_decoder.h`, `subtitle_renderer.{h,cpp}` BEFORE touching code. Confirms whether they conflict; escalates to Hemanth if they do.

---

## §8 Files touched (cumulative)

**New:**
- `native_sidecar/src/source.{h,cpp}` (Phase B)

**Modified:**
- `native_sidecar/src/main.cpp` (Phase A: handler split + worker + cancellation; Phase B: source dispatch in worker)
- `native_sidecar/src/demuxer.{h,cpp}` (Phase B: source dispatch replaces is_http)
- `native_sidecar/src/audio_decoder.{h,cpp}` (Phase A: only if async-friendly stop/restart variant needed)
- `native_sidecar/src/stream_prefetch.{h,cpp}` (Phase B: only if parallel is_http branches exist)

**Memory updates (Phase A close):**
- Narrow `project_sidecar_dispatcher_non_blocking_decision.md` — flip status from "pending fresh-chat summon" to "Phase A SHIPPED + soaked."

**Memory updates (Phase B close):**
- Add `feedback_sidecar_source_abstraction.md` if Phase B surfaces patterns worth preserving (e.g., "always reach for `Source` dispatch over runtime is-http checks for new sidecar features").

---

## §9 Smoke / verification per phase

**Phase A.1 (worker split):**
- Sidecar + main app builds GREEN
- Smoke A (HTTP heavy stream): Pause + Close < 100ms during track-switch
- Smoke B (local file regression): no behavior change
- `out/ipc_latency.log` shows pause/stop p99 < 50ms during worker run

**Phase A.2 (audit pass):**
- Either: zero new blockers found → Phase A done
- Or: small blockers found + fixed inline → re-run Smoke A + B
- Or: large blockers found → Phase A.3 RTC

**Phase B (Source abstraction):**
- Phase A acceptance still passes (regression check)
- HTTP track-toggle < 500ms (vs 30+ sec today)
- Local file behavior unchanged
- Probe time on HTTP unchanged or better
- Env-gate OFF reverts to legacy behavior bit-identical

---

## §10 Honest unknowns

- **`audio_decoder->stop()` synchrony.** Does it join an internal thread (synchronous) or signal-and-return (async)? If synchronous, it adds latency to `set_tracks_worker` — not a bug since worker is off-dispatcher, but worth knowing for latency budget. Verified during Phase A pre-flight read of `audio_decoder.{h,cpp}`.
- **`av_read_frame` HTTP cancellation behavior.** Does the `while` loop honor an atomic flag check between iterations cleanly, or does ffmpeg sometimes hang inside `av_read_frame` itself on a stuck HTTP read? If the latter, R7 fires and we wire `AVIOInterruptCB` (~10 LOC). Verified during Phase A smoke.
- **Stranded sidecar working-tree dirt.** Existing modifications to `audio_decoder.h` + `demuxer.h` + `stream_prefetch.{h,cpp}` + `video_decoder.h` + `subtitle_renderer.{h,cpp}` are not RTC'd anywhere. Risk that they're someone else's in-flight work. Phase A pre-flight reads them and either (a) confirms they don't conflict, (b) escalates to Hemanth, or (c) `git checkout HEAD --` if they're orphan changes nobody owns.
- **Whether Phase B's `Source` abstraction needs a `seek()` virtual method.** If `LocalSource::seek` and `HttpSource::seek` end up identical, the virtual is redundant. Verified during Phase B implementation; can drop the virtual if so.
- **Whether `set_tracks` is called for things other than subtitle/audio track switching.** Grep at Phase A pre-flight confirms callsites.

---

## §11 Rollback

**Phase A rollback:**
- Single commit covers `handle_set_tracks` split + `set_tracks_worker` extraction + cancellation hook + audit comments.
- Revert: `git revert <phase-A-commit-sha>`. Restores synchronous `handle_set_tracks`. Sidecar rebuild + redeploy. Layer-1 StreamPage parity fixes (already shipped) remain in place — they're independent.

**Phase B rollback:**
- Env-gated behind `TANKOBAN_SIDECAR_SOURCE_V2=1`. Remove the env var → legacy `is_http` branches active.
- Hard rollback: `git revert <phase-B-commit-sha-range>`. Multiple commits expected (one per Phase B sub-step); revert in reverse chronological order.

**Phase A audit-revealed Phase A.3 (if surfaced):**
- Each blocker fix is its own commit. Revert independently.

---

## §12 Memory close-out tasks

**At Phase A close:**
- Update `project_sidecar_dispatcher_non_blocking_decision.md` — change Status from "Phase A+B pending fresh-chat summon" to "Phase A SHIPPED 2026-04-25 [+sha]; Phase B pending."
- If Phase A.2 audit surfaced a new pattern, write `feedback_sidecar_dispatcher_audit_pattern.md` documenting the audit recipe so future agents reproduce it.

**At Phase B close:**
- Update `project_sidecar_dispatcher_non_blocking_decision.md` Status → "Phase A+B SHIPPED [date+shas]; Phase C deferred indefinitely."
- Add `feedback_sidecar_source_abstraction.md` if pattern reuse is plausible (e.g., future codecs, future protocols).
- Narrow / archive `project_stream_server_pivot.md` cross-references if Phase B closes any cross-cutting questions there.

**At TODO close (post Phase B + soak):**
- Move `SIDECAR_DISPATCHER_NON_BLOCKING_FIX_TODO.md` to `agents/_archive/todos/`.
- CLAUDE.md row gets STATUS column updated to "CLOSED [date]".
- One closing chat.md line summarizing scope.

---

## §13 Cursor (this row maintained by Agent 4)

| Phase | Status | Date | Commit | Notes |
|---|---|---|---|---|
| Authoring | ✅ DONE | 2026-04-25 ~22:20 | (sweep-pending) | Plan-mode design + ground-truth exploration |
| §5 Ratification | ✅ LOCKED VIA RULE 14 | 2026-04-25 ~22:35 | (sweep-pending) | All 5 picks locked as Agent 4 calls per Hemanth's "I do not know shit" Rule 14 invocation |
| Phase A.1 — `set_tracks_worker` extraction | ✅ SHIPPED | 2026-04-26 ~01:05 | (this RTC) | Globals + cancellation hook + 3-phase mutex pattern + teardown drain. Sidecar BUILD GREEN; main app BUILD OK. Awaiting Hemanth smoke. |
| Phase A.2 — dispatch-table audit | ✅ SHIPPED | 2026-04-26 ~01:00 | (this RTC) | No second smoking-gun blocker. Latent: `handle_load_external_sub` has `avformat_open_input` (only HTTP-subtitle paths matter, fast on local); `audio_dec->stop()` inside set_tracks_worker Phase 1 still ≤5s under mutex on audio-track switch (not subtitle-toggle). Both documented in §10; Phase A.3 conditional. |
| Phase A.3 — audio-track switch async | ⏳ CONDITIONAL | — | — | Only if Hemanth smoke surfaces audio-track-switch lockup separately from subtitle-track |
| Phase B — Source abstraction | ⏳ QUEUED | — | — | After Phase A soak |
| Phase C — two-entity revisit | ⏳ DEFERRED | — | — | Probably never |

---

## §14 Honest scope note

This TODO solves **one specific class of bug**: dispatcher-blocking handlers in the native sidecar that cause IPC commands to queue up behind slow I/O. Phase A kills the user-visible symptoms (Pause/Close non-responsive on HTTP track-switch). Phase B prevents the next "stream mode does X weird" bug class from being possible by normalizing source-type dispatch.

This TODO does NOT solve:
- General sidecar latency improvements (audio glitch, frame pacing, HDR tone-mapping).
- Stream-server-side bugs (Stremio Node.js subprocess).
- Main-app player UX issues (HUD, controls, seek bar).
- Audio host API selection / Path A WASAPI work (Agent 3 lane).
- Anything outside `native_sidecar/src/`.

If Hemanth surfaces a different sidecar bug during Phase A smoke, it does NOT fold into this TODO unless it's clearly the same dispatcher-blocking class. Otherwise it's a separate fix-TODO.
