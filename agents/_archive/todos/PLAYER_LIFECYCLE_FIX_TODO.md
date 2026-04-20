# Player Lifecycle Fix TODO — SidecarProcess sessionId + open/stop fence + VideoPlayer stop identity clear

**Owner:** Agent 3 (Video Player). Agent 0 coordinates phase gates and commit sweeps. Agent 6 reviews each phase against this doc + `agents/audits/tankostream_session_lifecycle_2026-04-15.md` as co-objective. Cross-agent coordination with Agent 4 via `STREAM_LIFECYCLE_FIX_TODO.md` flagged per phase.

**Created:** 2026-04-16 by Agent 0 after Agent 7's session-lifecycle audit + Agent 4's observation-only validation pass (chat.md:18768-18911).

## Context

Companion TODO to `STREAM_LIFECYCLE_FIX_TODO.md`. Audit + validation split cleanly: Stream side owns 8 findings, Player side owns 3. Same root cause pattern — session identity doesn't propagate across async boundaries — but the Player-side findings are concentrated in `VideoPlayer` and `SidecarProcess`, Agent 3's territory.

**Three Player-domain findings, all CONFIRMED:**
- **P0-2 — `VideoPlayer::openFile` sends stop+shutdown+open to a still-running sidecar process.** Same class as the just-closed double-open blank-player race, but at VideoPlayer level rather than StreamPage timer level. Any caller can hit the race without involving the seek-retry timer.
- **P0-3 — `SidecarProcess::processLine` ignores incoming event `sessionId`.** Native sidecar DOES emit `sessionId` on events and DOES guard incoming commands by session — the hole is entirely Qt-side. Stale events from a prior sidecar session rewrite new-session state.
- **P1-5 — `VideoPlayer::stopPlayback` leaves `m_currentFile` / `m_pendingFile` / `m_pendingStartSec` intact.** Any `onSidecarReady` event arriving post-stop hits the non-empty-`m_pendingFile` branch and re-opens a file the user just closed.

All three are local to Agent 3's domain files. None require cross-agent coordination beyond sequencing.

**Why this is a separate TODO, not a phase in STREAM_LIFECYCLE:** agent ownership. Domain alignment is the consistent repo pattern (BOOK_READER = Agent 2, VIDEO_PLAYER_FIX = Agent 3, COMIC_READER_FIX = Agent 1, etc.). Splitting by subsystem keeps commit sweeps clean + Agent 6 reviews scoped to one reviewer's mental model.

**Scope:** 3 phases, ~5 batches. Phase 1 is the cheap high-leverage foundation (sessionId filter — ~20 LOC). Phase 2 is the open/stop fence (highest implementation depth; process-lifecycle ordering). Phase 3 is the VideoPlayer stop-identity cleanup.

## Objective

After this plan ships, VideoPlayer + SidecarProcess session semantics are airtight under every process-lifecycle scenario the audit identified:

1. **User opens a new file while one is playing:** no stop+shutdown+open to a still-running sidecar. Fence ensures the new open either waits for the old to fully close, or uses a stop/open protocol without shutdown. No process race.
2. **Stale sidecar events (post-restart, post-shutdown):** filtered out by sessionId mismatch. `time_update` from an old session cannot drive `progressUpdated` under a new session. `first_frame` from an old session cannot reinitialize the canvas against a stale shm handle. `tracks_changed` from an old session cannot replace track lists.
3. **User closes player → unexpected `onSidecarReady` event:** doesn't trigger re-open of the just-closed file. `m_currentFile` / `m_pendingFile` cleared on stop; `sendOpen` gated on explicit pending-open token.
4. **Crash-recovery restart:** still works correctly — uses a distinct path that intentionally preserves `m_currentFile` for resume. Not affected by this TODO's changes.

## Non-Goals (explicitly out of scope for this plan)

- **Stream-side session-lifecycle fixes** — owned by `STREAM_LIFECYCLE_FIX_TODO.md` (Agent 4). Player side does not touch `StreamPage` / `StreamPlayerController` / `StreamEngine` / `StreamHttpServer`.
- **Refactoring the crash-recovery restart path** — `m_currentFile` preservation for crash recovery is intentional; this TODO doesn't touch it. The fix in Phase 3 distinguishes user-stop from crash-recovery via a flag.
- **Native sidecar protocol changes** — sessionId plumbing on the sidecar side already works correctly. This TODO only fixes the Qt side's consumption of it.
- **SidecarProcess event parsing / signal signatures** — the event-dispatch by `name` stays. Only adds a pre-dispatch sessionId check.
- **Videos-mode playlist / Comics-reader page identity** — these consumers of VideoPlayer have their own identity concerns but don't overlap with session races.
- **Any work outside `src/ui/player/VideoPlayer.*`, `src/ui/player/SidecarProcess.*`**.

## Agent Ownership

All batches are **Agent 3's domain** (Video Player). Primary files:
- `src/ui/player/VideoPlayer.{h,cpp}`
- `src/ui/player/SidecarProcess.{h,cpp}`

**Cross-agent coordination:**

- **Agent 4** ships `STREAM_LIFECYCLE_FIX_TODO.md` in parallel. Agent 4's Phase 1 (PlaybackSession foundation) + Phase 2 (source-switch reentrancy split) close P0-1 + P1/P2 Stream findings. No direct file overlap with this TODO's scope.
- **User-visible interaction:** Per Agent 4's validation (chat.md:18893-18894): P0-1 (Stream Phase 2) and P0-2 (Player Phase 2) both reduce user-visible flashing. Fixing only one may shift the dominant failure mode. Ideal landing sequence per Agent 4's recommendation:
  1. **Player Phase 1 (sessionId filter)** first — foundation, unblocks understanding of all race paths.
  2. **Stream Phase 1 (PlaybackSession)** — Agent 4's foundation, similar architectural shape.
  3. **Stream Phase 2 (source-switch split) + Player Phase 2 (sidecar fence)** in parallel — the two P0 fixes close together.
  4. **Player Phase 3 + Stream Phase 3/4/5** close remaining findings in either order.
- **No shared file touches.** Rule 7 does not apply.

Agent 0 coordinates phase gates and commit sweeps. Agent 6 reviews each phase against this plan + the audit as co-objective.

---

## Phase 1 — SidecarProcess sessionId filter (P0-3)

**Why:** Agent 4 validation (chat.md:18820-18826): `SidecarProcess::processLine` dispatches by `obj["name"]` only. Never reads `obj["sessionId"]`, never compares against `m_sessionId`. Every event — `first_frame`, `time_update`, `tracks_changed`, `eof`, `state_changed`, `error` — emits blind.

Since `sendOpen` generates a new `m_sessionId` on every call, and native events carry the decoder worker's captured sid (per audit on `native_sidecar/src/main.cpp:367`), any event lingering in stdout buffer from an OLD session reaches VideoPlayer as if current-session. Impact: stale `time_update` drives `progressUpdated`, stale `tracks_changed` replaces track lists, stale `first_frame` reinitializes canvas against stale shm handle.

Native sidecar already treats session ID as a guard for incoming commands (`main.cpp:1266`). The fix is to mirror that discipline on event output — Qt side filters events whose `sessionId` doesn't match current. **Cheap, high-leverage.** Foundational — unblocks clean reasoning about phases 2-3.

### Batch 1.1 — Filter processLine by sessionId

- [SidecarProcess::processLine](src/ui/player/SidecarProcess.cpp#L339-L377) gets a sessionId check at the top:
  ```cpp
  void SidecarProcess::processLine(const QByteArray& line) {
      const QJsonDocument doc = QJsonDocument::fromJson(line);
      if (!doc.isObject()) return;
      const QJsonObject obj = doc.object();
      const QString name = obj["name"].toString();

      // Process-global events that don't belong to a session — let through.
      static const QSet<QString> kProcessGlobal = {
          "ready", "closed", "shutdown_ack", "version", "process_error"
      };
      if (!kProcessGlobal.contains(name)) {
          const QString eventSid = obj["sessionId"].toString();
          if (!eventSid.isEmpty() && eventSid != m_sessionId) {
              qCDebug(sidecarProcess) << "[session] drop stale event" << name
                                      << "eventSid=" << eventSid
                                      << "currentSid=" << m_sessionId;
              return;
          }
      }

      // existing dispatch logic
      if (name == "first_frame") { /* ... */ }
      else if (name == "time_update") { /* ... */ }
      // etc.
  }
  ```
- **Whitelist rationale:** `ready` fires at process start before any session exists. `closed` fires at process end after sessions are done. `shutdown_ack` / `version` / `process_error` are process-global. These 5 are the known exceptions; other events are session-scoped.
- `qCDebug` logging on drops lets us see at runtime whether stale events are actually being filtered (validates the fix empirically, matches `feedback_evidence_before_analysis.md`).
- Missing `sessionId` on a non-whitelist event: `if (eventSid.isEmpty())` tolerates — legacy sidecar builds may not include the field on every event. Don't regress compatibility.

**Files:** [src/ui/player/SidecarProcess.cpp](src/ui/player/SidecarProcess.cpp).

**Success:**
1. Normal playback: zero session-ID drops in qCDebug log during a single clean open/play/close cycle.
2. Rapid file-switch smoke: open file A, immediately open file B (fast double-click in playlist) → qCDebug log shows stale `time_update` / `first_frame` from session A dropped AFTER `sendOpen` for session B. No incorrect progress writes, no canvas flash.
3. Process crash recovery: qCDebug log shows no unnecessary drops during restart (session remains same during restart since `m_sessionId` is regenerated on `sendOpen` only).
4. Legacy sidecar without sessionId on events: playback still works (eventSid.isEmpty() branch tolerates).

**Isolate-commit:** yes. Foundation for P0-2 + P1-5 reasoning. Isolate to validate the drop-log empirically before Phase 2 piles on.

### Phase 1 exit criteria
- `processLine` filters by sessionId with explicit process-global whitelist.
- qCDebug logging on drops (debug-only — no production log spam).
- Smoke against normal playback + rapid file-switch confirms drops happen correctly.
- Agent 6 review against audit P0-3 citation chain.
- `READY FOR REVIEW — [Agent 3, PLAYER_LIFECYCLE_FIX Phase 1]: SidecarProcess sessionId filter | Objective: Phase 1 per PLAYER_LIFECYCLE_FIX_TODO.md + agents/audits/tankostream_session_lifecycle_2026-04-15.md. Files: src/ui/player/SidecarProcess.cpp.`

---

## Phase 2 — Sidecar open/stop fence (P0-2)

**Why:** Agent 4 validation (chat.md:18809-18818): `VideoPlayer::openFile` at `:276` calls `stopPlayback()`, which in `:362-366` does `sendStop()` + immediate `sendShutdown()` — no wait on `finished` or `processClosed`. Then `openFile` at `:335` checks `m_sidecar->isRunning()`; since shutdown is async, the check returns true and `:342` sends `open` directly to the same still-running process.

Command order at sidecar is race-dependent:
- Shutdown first → sidecar exits → open lost to dying stdin → blank player.
- Open first → new session starts → shutdown tears it down.

Same class as the just-closed seek-retry double-open, but at the VideoPlayer level. Any caller can hit the race.

**Two fix shapes:**

1. **Wait-for-closed.** After `sendShutdown()`, wait for `QProcess::finished` signal before any new `sendOpen`. Simple but high-latency on every file switch (~50-200ms process respawn).

2. **Drop shutdown for same-process file switch.** Use a stop/open protocol with explicit sequence-number acks. The sidecar stays alive across the file switch. New `open` command is accepted after `stop_ack` returns. Lower latency; more protocol work.

Shape 2 is the architecturally better answer (saves process respawn cost on every seek-to-different-file / source-switch). Shape 1 is the safer starting point. **Agent 3's call** which shape ships; recommend Shape 2 unless protocol complexity proves prohibitive. Phase 1's sessionId filter is the enabler for Shape 2 — old session's stale events get dropped by sessionId mismatch during the transition.

### Batch 2.1 — Open/stop fence implementation

**Recommended: Shape 2 (same-process stop/open protocol)**

- `SidecarProcess` gains:
  ```cpp
  // Sequence numbers for stop/open handshake
  quint32 m_nextSeq = 1;
  struct PendingStop {
      quint32 seq;
      std::function<void()> onComplete;  // fires on matching stop_ack
  };
  std::optional<PendingStop> m_pendingStop;
  ```
- `sendStop()` stamps a new `seq`, includes it in the JSON payload, stores `PendingStop{seq, onComplete}`. If `onComplete` is null, existing behavior (fire-and-forget stop).
- New event `stop_ack` from sidecar carries the stop's `seq`. Native sidecar main.cpp emits `stop_ack` after decoder teardown completes.
- `processLine` handles `stop_ack`: if matches `m_pendingStop->seq`, invokes `onComplete` and clears the pending.
- `VideoPlayer::openFile` rewrites:
  ```cpp
  void VideoPlayer::openFile(const QString& file, ...) {
      if (m_sidecar->isRunning()) {
          // Same-process file switch: sendStop + wait for stop_ack + sendOpen
          m_sidecar->sendStopWithCallback([this, file, ...]() {
              // stop_ack received — safe to open
              m_sidecar->sendOpen(file, ...);
          });
      } else {
          // Process not running — start + open as before
          m_sidecar->start();
          m_sidecar->sendOpen(file, ...);
      }
  }
  ```
- Crash-recovery / user-quit path (`stopPlayback`) uses `sendStop + sendShutdown` for full tear-down. Distinguished by callers.
- If sidecar doesn't respond to `stop_ack` within 2s (sidecar hang), fall back to `sendShutdown` + wait-for-`finished` + restart.

**Alternative: Shape 1 (wait-for-closed)**

- `stopPlayback` stays as-is (sendStop + sendShutdown).
- `openFile` after `stopPlayback`: if `m_sidecar->isRunning()`, install one-shot `QMetaObject::Connection` on `m_sidecar->finished()` → callback sends `sendOpen` after process closed.
- 2s timeout fallback if `finished` never fires (sidecar hang).
- Simpler implementation; higher latency on every file switch.

**Files:** [src/ui/player/VideoPlayer.cpp](src/ui/player/VideoPlayer.cpp) + [.h](src/ui/player/VideoPlayer.h), [src/ui/player/SidecarProcess.cpp](src/ui/player/SidecarProcess.cpp) + [.h](src/ui/player/SidecarProcess.h). Shape 2 also requires `native_sidecar/src/main.cpp` sidecar-side changes for `stop_ack` emission.

**Success:**
- Open file A → play 10s → open file B → no blank-player race, no sidecar death, playback starts on B cleanly.
- Rapid A/B/A/B file switches (5x in 2s) → no crashes, no stuck-in-buffering states.
- User Escape during playback → normal full tear-down path still works (sendStop + sendShutdown, not new fence).

**Isolate-commit:** yes. Highest-impact Player-side fix. Isolate for stress testing before Phase 3 piles on.

### Phase 2 exit criteria
- Open/stop fence live, shape agreed with Hemanth (Shape 1 or Shape 2).
- No stop+shutdown+open to running process.
- Rapid file-switch stress test clean.
- Sidecar rebuild only required if Shape 2 chosen (for `stop_ack` emission).
- Agent 6 review against audit P0-2 citation chain.

---

## Phase 3 — VideoPlayer stop identity clear (P1-5)

**Why:** Agent 4 validation (chat.md:18862-18864): `VideoPlayer::stopPlayback` clears canvas, detaches shm, stops sidecar restart timer, sends stop/shutdown, clears audio/sub track arrays. Does NOT touch `m_currentFile`, `m_pendingFile`, `m_pendingStartSec`, `m_playlist`, `m_playlistIdx`, `m_lastKnownPosSec`.

Any `onSidecarReady` event arriving post-stop hits `:411-413`: sends `sendOpen(m_pendingFile, m_pendingStartSec)` if non-empty — potentially re-opening a file the user just closed.

Crash-recovery path intentionally preserves `m_currentFile` / `m_lastKnownPosSec` for resume. This TODO's fix must distinguish user-stop from crash-recovery-stop.

### Batch 3.1 — Distinguish intentional stop from crash-recovery

- `VideoPlayer` already has `m_intentionalShutdown` flag. Audit-side check (per chat.md:18832-18835 / [VideoPlayer.cpp:712](src/ui/player/VideoPlayer.cpp#L712)) — flag guards crash-recovery path.
- Fix: extend `stopPlayback()` signature with a `bool isIntentional = true` default arg (existing callers retain current behavior). Intentional stops clear identity state:
  ```cpp
  void VideoPlayer::stopPlayback(bool isIntentional = true) {
      // existing teardown (canvas, shm, timers, tracks)
      // ...
      if (isIntentional) {
          m_currentFile.clear();
          m_pendingFile.clear();
          m_pendingStartSec = 0;
          m_playlist.clear();
          m_playlistIdx = 0;
          m_lastKnownPosSec = 0;
      }
      // Crash-recovery path calls stopPlayback(false) to preserve resume state.
  }
  ```
- Crash-recovery restart at [VideoPlayer.cpp:712](src/ui/player/VideoPlayer.cpp#L712) (`restartSidecar`) — audit-confirmed it reuses `m_currentFile` / `m_lastKnownPosSec`. Update the one crash-recovery call path to pass `isIntentional=false`.
- User-facing close paths (`closeRequested`, Escape, context back, media stop) pass `isIntentional=true` (default) — clears identity.

### Batch 3.2 — Gate onSidecarReady re-open on pending-open token

- [VideoPlayer::onSidecarReady at :411-413](src/ui/player/VideoPlayer.cpp#L411-L413): sends `sendOpen(m_pendingFile, m_pendingStartSec)` if `m_pendingFile` non-empty. After Phase 3.1, `m_pendingFile` cleared on intentional stop — but a post-stop `onSidecarReady` event during a weird restart window could still race.
- Fix: add `bool m_openPending = false` — one-shot token. Set true when `openFile` queues a pending open. Checked and cleared in `onSidecarReady`:
  ```cpp
  void VideoPlayer::onSidecarReady() {
      // existing restart-retry logic
      // ...
      if (m_openPending && !m_pendingFile.isEmpty()) {
          m_openPending = false;
          m_sidecar->sendOpen(m_pendingFile, m_pendingStartSec);
      }
  }
  ```
- Alongside Phase 2's fence: the token ensures only ONE `sendOpen` fires per explicit user action. Even if two `onSidecarReady` events race (crash restart + explicit open in quick succession), only one opens.

**Files:** [src/ui/player/VideoPlayer.cpp](src/ui/player/VideoPlayer.cpp) + [.h](src/ui/player/VideoPlayer.h).

**Success:**
- User-Escape during playback → `m_currentFile` / `m_pendingFile` cleared. Any subsequent unexpected `onSidecarReady` event → no re-open.
- Crash-recovery restart → `m_currentFile` preserved across `restartSidecar`. Resume works with the right position.
- Rapid Escape→reopen smoke test → no double-open, no re-open of closed file.

### Phase 3 exit criteria
- Intentional-stop vs crash-recovery-stop distinguished.
- `onSidecarReady` gated on one-shot pending-open token.
- Escape-close-no-reopen smoke clean.
- Crash-recovery smoke clean.
- Agent 6 review against audit P1-5 citation chain.

---

## Scope decisions locked in

- **Phase 1 sessionId filter uses `qCDebug` not `qWarning`.** Drops are expected under normal rapid-switch conditions; only abnormal if none fire during rapid switches. Verbose-debug category only.
- **Phase 1 process-global whitelist = `{ready, closed, shutdown_ack, version, process_error}`.** Can extend if more process-lifecycle events surface.
- **Phase 2 recommended shape = Shape 2 (same-process stop/open protocol).** Shape 1 is fallback if protocol complexity bites. Agent 3's final call during implementation.
- **Phase 2 sidecar `stop_ack` emission** (only for Shape 2) = new event on `native_sidecar/src/main.cpp` side. Minimal change; emits after decoder teardown completes.
- **Phase 3 `stopPlayback(bool isIntentional = true)` default-arg.** All existing callers are intentional stops; no breaking change. One crash-recovery caller updated explicitly.
- **`m_openPending` token is a single bool, not a counter.** Multiple rapid opens collapse to the last one — matches existing `m_pendingFile` semantics.

## Isolate-commit candidates

Per the TODO's Rule 11 section:
- **Batch 1.1** (sessionId filter) — foundation. Isolate to validate empirically before Phase 2.
- **Batch 2.1** (open/stop fence) — highest-impact fix. Isolate for stress testing before Phase 3.

Phase 3 batches commit at phase boundary.

## Existing functions/utilities to reuse (not rebuild)

- [`m_intentionalShutdown` flag](src/ui/player/VideoPlayer.cpp) — Phase 3.1 extends this discipline with `isIntentional` parameter on `stopPlayback`.
- [`m_sessionId` regeneration on `sendOpen`](src/ui/player/SidecarProcess.cpp#L138-L147) — Phase 1 consumes the existing per-open sessionId.
- [`restartSidecar` crash-recovery path](src/ui/player/VideoPlayer.cpp#L711) — Phase 3.1 updates this one call site to pass `isIntentional=false`.
- [Native sidecar session-guard logic](native_sidecar/src/main.cpp) — Phase 1's Qt-side filter mirrors this existing sidecar-side discipline.

## Review gates

Each phase exits with:
```
READY FOR REVIEW — [Agent 3, PLAYER_LIFECYCLE_FIX Phase X]: <title> | Objective: Phase X per PLAYER_LIFECYCLE_FIX_TODO.md + agents/audits/tankostream_session_lifecycle_2026-04-15.md. Files: ...
```
Agent 6 reviews against audit + TODO as co-objective.

## Open design questions Agent 3 decides as domain master

- **Phase 2 shape: Shape 1 (wait-for-closed) vs Shape 2 (same-process stop/open protocol).** Shape 2 is architecturally better (no process respawn cost per file switch) but requires sidecar-side `stop_ack` work. Agent 3 weighs complexity vs performance.
- **Phase 2 stop_ack timeout value.** 2s suggested; Agent 3 may tune based on observed sidecar teardown latency.
- **Phase 3 default arg `bool isIntentional = true`.** Alternative: split into two methods (`stopPlayback()` + `stopPlaybackForCrashRecovery()`). Default-arg is simpler; split-method is more explicit. Agent 3's call.
- **Phase 1 extended whitelist.** Beyond `ready / closed / shutdown_ack / version / process_error`, other process-global events may emerge. Agent 3 adds to the whitelist as they surface.

## What NOT to include (explicit deferrals)

- Session-generation pattern on VideoPlayer side (Agent 4's PlaybackSession is Stream-side — VideoPlayer's own analog isn't urgent).
- Videos-mode persistence lifecycle (PersistenceMode enum) changes. Agent 4's STREAM_LIFECYCLE TODO handles that. Player side stays.
- Full rewrite of `SidecarProcess::processLine` dispatch. Phase 1 is an additive filter, not a dispatch refactor.
- Shader / frame-pacing / audio internal session concerns. Player Polish Phases 1-6 closed engine-correctness already.
- Any work on `native_sidecar/src/` beyond Phase 2 Shape 2's `stop_ack` emission. Native sidecar changes trigger `build_qrhi.bat` sidecar rebuild — Hemanth runs that.

## Rule 6 + Rule 11 application

- Rule 6: every batch compiles + smokes on Hemanth's box before `READY TO COMMIT`. Agent 3 does not declare done without build verification.
- Rule 11: per-batch READY TO COMMIT lines; Agent 0 batches commits at phase boundaries (isolate-commit candidates above ship individually).
- Single-rebuild-per-batch per `feedback_one_fix_per_rebuild`.
- **Evidence-before-analysis** per `feedback_evidence_before_analysis.md`: Phase 1's qCDebug log is the evidence-gathering tool. Before declaring Phase 1 fixed, confirm in Hemanth's trace that stale events actually fire AND are dropped during rapid-switch scenarios.

## Verification procedure (end-to-end once all 3 phases ship)

1. **Normal playback:** open file → play → close. qCDebug log has zero session-ID drops. (Phase 1 baseline.)
2. **Rapid file switch:** open A → within 500ms open B → open B again within 500ms. qCDebug log shows stale `time_update` / `first_frame` / `tracks_changed` from A dropped. No blank player on B. No stop+shutdown+open race. (Phases 1 + 2 together.)
3. **Escape during playback:** Escape closes cleanly. No subsequent re-open. `m_currentFile` cleared. (Phase 3.1.)
4. **Crash recovery:** force sidecar crash (kill process) → VideoPlayer restart-recovery kicks in → resumes at last position. Correct behavior preserved. (Phase 3.1 distinguishes paths.)
5. **Post-close unexpected ready:** simulate sidecar-ready event after user closed → `m_openPending=false` blocks re-open. (Phase 3.2.)
6. **Stream-source switch stress (depends on Stream Phase 2):** start stream A → switch to stream B → verify Player-side no process race even under Stream-side reentrancy. (Cross-validates with Stream TODO.)
7. **Regression:** Player Polish features (EqualizerPopover, FilterPopover, TrackPopover, subtitle menu) all still function correctly. No session-filter false-drops on the popover-driven commands.

## Next steps post-approval

1. Agent 0 posts routing announcement in chat.md.
2. Agent 3 executes phased per Rule 6 + Rule 11.
3. Agent 6 gates each phase exit.
4. Agent 0 commits at phase boundaries (isolate-commit exceptions per Rule 11 section).
5. Coordinate with Agent 4 STREAM_LIFECYCLE_FIX execution — ideal landing order per Cross-agent coordination section above.
6. MEMORY.md `Active repo-root fix TODOs` line updated to include this TODO.

---

**End of plan.**
