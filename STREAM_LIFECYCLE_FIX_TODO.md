# Stream Lifecycle Fix TODO — PlaybackSession + source-switch reentrancy + P1/P2 cleanup

**Owner:** Agent 4 (Stream mode). Agent 0 coordinates phase gates and commit sweeps. Agent 6 reviews each phase against this doc + `agents/audits/tankostream_session_lifecycle_2026-04-15.md` as co-objective. Cross-agent coordination with Agent 3 via `PLAYER_LIFECYCLE_FIX_TODO.md` flagged per phase.

**Created:** 2026-04-16 by Agent 0 after Agent 7's session-lifecycle audit + Agent 4's observation-only validation pass (chat.md:18768-18911).

## Context

Stream mode's intermittent playback class (cold-start races, flash-to-browse mid-buffering, progress-persistence-under-wrong-key) has been chased through multiple one-off fixes. The `feedback_session_lifecycle_pattern.md` memory (2026-04-15) concluded: "intermittent stream playback = session-lifecycle race 99% of the time. Check stale state / orphan timers / residual signal connections BEFORE blaming libtorrent or ffmpeg."

Agent 7's session-lifecycle audit at `agents/audits/tankostream_session_lifecycle_2026-04-15.md` delivered the itemized receipts: 3 P0s + 5 P1s + 3 P2s, all converging on one architectural gap — session identity doesn't propagate across async boundaries (Qt signal closures, QTimer::singleShot callbacks, sidecar process events, HTTP worker threads). Agent 4's validation (chat.md:18770-18906) confirmed all 11 findings with traced order-of-operations evidence.

The audit's closing advisory #1 is the real fix shape:
> Consider a single StreamPage/Controller `resetSessionState(reason)` or `PlaybackSession` object that owns `_currentEpKey`, pending play, next-episode state, seek retry state, deadline timestamp, and StreamPage-owned player connections.

The existing `m_seekRetryState` generation check (shipped as an orphan-timer fix earlier) is the local precedent. This TODO generalizes that pattern into a proper session-identity abstraction, then fixes the specific P0/P1/P2s that fall out of it.

**Domain split rationale:** audit findings split cleanly — Stream side owns P0-1 (source-switch reentrancy) + 4 P1s + all 3 P2s + the architectural refactor. Player side owns P0-2 + P0-3 + P1-5. Parallel execution via this TODO (Agent 4) + `PLAYER_LIFECYCLE_FIX_TODO.md` (Agent 3). The two are independent in execution but coupled in user experience — fixing only one may shift the dominant failure mode without eliminating user-visible flashing.

**Scope:** 5 phases, ~11 batches. Phase 1 establishes the `PlaybackSession` foundation — mandatory prerequisite for phases 2-4. Phase 5 (HTTP worker cancellation) has highest implementation depth but is independent of phases 1-4 — can ship in parallel or deferred.

## Objective

After this plan ships, Stream mode behaves correctly under every session-boundary scenario the audit identified:

1. **Source-switch mid-play:** new session's state survives. No flash-to-browse. No empty `_currentEpKey`. Buffer overlay stays visible through the handoff.
2. **Stream failure → user navigates away:** 3s failure timer does NOT yank the user back to browse. Generation-check gates the timer's effect.
3. **Failed-open → user retries different stream:** stale `m_infoHash` does not leak into the new session's seek pre-gate / deadline retargeting.
4. **StreamEngine emits `streamError`:** controller observes it, terminates the session cleanly.
5. **User hits Shift+N during series playback:** it works. (Today it's a silent no-op because `m_pendingPlay.valid` is cleared before playback starts.)
6. **Fast close/reopen within 2s:** `m_lastDeadlineUpdateMs` reset tied to session boundary. First deadline update fires on schedule.
7. **Source-activation partial cleanup:** goes through `resetNextEpisodePrefetch()` — no residual Shift+N pending flag or stale aggregator connections.
8. **User closes stream while HTTP worker is sleeping in `waitForPieces()`:** worker observes cancellation token promptly (< 1s), releases socket + file cleanly. No up-to-15s hang.

All state that should be session-scoped IS session-scoped. Session boundary is one function call, not 7 scattered inline resets.

## Non-Goals (explicitly out of scope for this plan)

- **P0-2 / P0-3 / P1-5 fixes** — owned by `PLAYER_LIFECYCLE_FIX_TODO.md` (Agent 3). Stream side does not touch `VideoPlayer` / `SidecarProcess` / sidecar process ordering.
- **Session-lifecycle work outside stream mode** — videos-mode and comics-reader have their own lifecycle contracts (persistence modes, different teardown surfaces). Not touched here.
- **Refactoring non-session state in StreamPage** — this plan reorganizes `_currentEpKey` + `m_pendingPlay` + `m_nextPrefetch` + `m_nearEndCrossed` + `m_nextShortcutPending` + `m_lastDeadlineUpdateMs` + `m_seekRetryState` into a `PlaybackSession` object. Other StreamPage state (UI widget members, layout members, static config) stays where it is.
- **Changing the per-session signal connect/disconnect pattern** — the wildcard-disconnect-receivers pattern at `onReadyToPlay` stays. This plan does NOT rewrite into `QMetaObject::Connection`-handle-per-flow storage (audit advisory #7 — deferred as polish).
- **TorrentEngine / libtorrent changes** — the `haveContiguousBytes()` behavior under unknown-hash is engine-layer. Phase 5's HTTP worker cancellation adds a per-stream cancel token that checks BEFORE the engine call, so we don't depend on engine-layer safety.
- **Stream library / addon work** — separate STREAM_UX_PARITY_TODO.md already shipped.
- **Any work outside `src/ui/pages/StreamPage.*`, `src/ui/pages/stream/StreamPlayerController.*`, `src/core/stream/StreamHttpServer.*`, `src/core/stream/StreamEngine.*`**.

## Agent Ownership

All batches are **Agent 4's domain** (Stream mode). Primary files:
- `src/ui/pages/StreamPage.{h,cpp}`
- `src/ui/pages/stream/StreamPlayerController.{h,cpp}`
- `src/core/stream/StreamHttpServer.{h,cpp}` (Phase 5 only)
- `src/core/stream/StreamEngine.{h,cpp}` (Phase 3 wiring + Phase 5 cancellation token)

**Cross-agent coordination:**

- **Agent 3** ships `PLAYER_LIFECYCLE_FIX_TODO.md` in parallel. Agent 3's Phase 1 (SidecarProcess sessionId filter) + Phase 2 (sidecar open/stop fence) + Phase 3 (VideoPlayer stop identity clear) close P0-2 / P0-3 / P1-5. No direct file overlap with this TODO's scope. Agent 4 does not touch `VideoPlayer.*` or `SidecarProcess.*`.
- **User-visible interaction:** P0-1 (Stream Phase 2) and P0-2 (Player Phase 2) both reduce user-visible flashing. Per Agent 4's validation: fixing only one may shift the dominant failure mode. Commit sweep coordination: ideal landing order is Player Phase 1 (sessionId filter) → Stream Phase 1 (PlaybackSession) → Stream Phase 2 (source-switch split) + Player Phase 2 (sidecar fence) in parallel → Player Phase 3 + Stream Phase 3/4/5. Agent 0 brokers if execution cadence diverges.
- **No shared file touches.** CMakeLists.txt not touched by this plan (no new files added). Rule 7 does not apply.

Agent 0 coordinates phase gates and commit sweeps. Agent 6 reviews each phase against this plan + the audit as co-objective.

---

## Phase 1 — PlaybackSession foundation

**Why:** Audit advisory #1 + #2. The generation-check pattern already proven by `m_seekRetryState` (the orphan-timer fix) needs to generalize to every async closure / timer / signal callback that mutates session state. Without the foundation, P0-1 + P1-1 + P1-2 + P2-1 fixes are each a bespoke generation-check per site — that's 7+ sites duplicating the same logic. One abstraction collapses them.

**Foundational phase.** All subsequent phases depend on this ABI landing first.

### Batch 1.1 — Introduce PlaybackSession struct + generation counter

- NEW nested struct inside `StreamPage` (no separate file; keep co-located with consumer):
  ```cpp
  struct PlaybackSession {
      quint64 generation = 0;              // monotonic counter; 0 = no active session
      QString epKey;                        // replaces _currentEpKey dynamic property
      PendingPlay pending;                  // replaces m_pendingPlay
      std::optional<NextEpisodePrefetch> nextPrefetch;  // replaces m_nextPrefetch
      bool nearEndCrossed = false;          // replaces m_nearEndCrossed
      bool nextShortcutPending = false;     // replaces m_nextShortcutPending
      qint64 lastDeadlineUpdateMs = 0;      // replaces m_lastDeadlineUpdateMs
      std::shared_ptr<SeekRetryState> seekRetry;  // replaces m_seekRetryState

      bool isValid() const { return generation != 0 && !epKey.isEmpty(); }
  };

  PlaybackSession m_session;
  quint64 m_nextGeneration = 1;  // next-to-assign; never 0
  ```
- Add `quint64 currentGeneration() const { return m_session.generation; }` accessor — every async closure that mutates session state captures this value at creation time and checks at fire time.
- Add `void resetSession(QString reason)` — single boundary. Implementation:
  1. Stops `m_nextEpisodeCountdownTimer` (if running)
  2. Disconnects prefetch aggregator connections (`seriesMetaReady` + `streamsReady`)
  3. Clears `m_session = {}`
  4. qInfo logs `"[session] reset: reason=<reason>"` for debugging
- Add `void beginSession(QString epKey, PendingPlay pending)`:
  1. Calls `resetSession("beginSession")` first
  2. `m_session.generation = m_nextGeneration++;`
  3. `m_session.epKey = epKey;`
  4. `m_session.pending = pending;`
  5. Returns the generation for caller to capture in async closures
- Add `bool isCurrentGeneration(quint64 gen) const { return gen != 0 && gen == m_session.generation; }` — helper for closure guards.

This batch **introduces the struct + API only** — no consumers migrated yet. Existing `_currentEpKey` / `m_pendingPlay` / etc. stay in place. Next batches migrate call sites.

**Files:** [src/ui/pages/StreamPage.h](src/ui/pages/StreamPage.h), [src/ui/pages/StreamPage.cpp](src/ui/pages/StreamPage.cpp).

**Success:** `PlaybackSession` struct defined + API methods implemented + unit-like smoke (can call `beginSession` / `resetSession` / `isCurrentGeneration` without crash). No behavior change — old state members still drive the flow.

### Batch 1.2 — Migrate _currentEpKey + m_pendingPlay + deadline ms

- Replace every `setProperty("_currentEpKey", ...)` call site with `m_session.epKey = ...` equivalent (captured inside `beginSession`).
- Replace every `property("_currentEpKey").toString()` read with `m_session.epKey` read.
- Replace every `m_pendingPlay.X` access with `m_session.pending.X`.
- Replace `m_lastDeadlineUpdateMs` with `m_session.lastDeadlineUpdateMs`.
- Grep-assisted sweep: `grep -n "_currentEpKey\|m_pendingPlay\|m_lastDeadlineUpdateMs" src/ui/pages/StreamPage.cpp`. Migrate every hit.
- Drop `_currentEpKey` as a dynamic property. Drop `m_pendingPlay` and `m_lastDeadlineUpdateMs` as separate members (they're inside `m_session` now).

The migration is mechanical but cross-cuts the file. Careful diff — some sites read via dynamic-property helper, those go away.

**Files:** [src/ui/pages/StreamPage.h](src/ui/pages/StreamPage.h), [src/ui/pages/StreamPage.cpp](src/ui/pages/StreamPage.cpp).

**Success:** every session-field access routes through `m_session`. Build clean. Smoke: launch stream, play 30s, close, re-launch different stream. No crash, no visual regression.

**Isolate-commit:** yes. Mechanical migration touching many call sites. Isolate so any missed site surfaces in isolation before Phase 2's behavioral changes pile on.

### Batch 1.3 — Migrate remaining session fields + generation-check the seek retry

- Move `m_nextPrefetch`, `m_nearEndCrossed`, `m_nextShortcutPending`, `m_seekRetryState` into `m_session`.
- Update `resetNextEpisodePrefetch()` to call through `m_session.nextPrefetch.reset()` + `m_session.nearEndCrossed = false` + `m_session.nextShortcutPending = false`. Keep the prefetch-aggregator-disconnect logic inside — this helper is still useful as a sub-operation of `resetSession`.
- Update the seek-retry pattern: instead of comparing `retryState != m_seekRetryState`, compare captured generation at `QTimer::singleShot` creation against `currentGeneration()` at fire time. The `SeekRetryState` struct stays but carries the generation instead of being the identity itself.

**Files:** [src/ui/pages/StreamPage.h](src/ui/pages/StreamPage.h), [src/ui/pages/StreamPage.cpp](src/ui/pages/StreamPage.cpp).

**Success:** all 7 session fields live inside `m_session`. Seek-retry timer uses generation check. Behavior parity with Batch 1.2 smoke.

### Phase 1 exit criteria
- `PlaybackSession` + generation-counter foundation live.
- All 7 session fields migrated.
- Single `resetSession(reason)` boundary + single `beginSession(epKey, pending)` constructor.
- Seek-retry uses generation check (first consumer of the new pattern).
- Agent 6 review: foundation API + migration cleanliness against audit advisory #1 + #2.
- `READY FOR REVIEW — [Agent 4, STREAM_LIFECYCLE_FIX Phase 1]: PlaybackSession foundation | Objective: Phase 1 per STREAM_LIFECYCLE_FIX_TODO.md + agents/audits/tankostream_session_lifecycle_2026-04-15.md. Files: src/ui/pages/StreamPage.h, src/ui/pages/StreamPage.cpp.`

---

## Phase 2 — Source-switch reentrancy split (P0-1)

**Why:** Agent 4 validation (chat.md:18790-18807) confirmed: `onSourceActivated` installs new session state (new `_currentEpKey`, buffer overlay, mainStack → player layer) BEFORE calling `startStream`. `startStream` synchronously emits `streamStopped` for the old session. StreamPage's `onStreamStopped` handler — direct-connected — fires INSIDE `startStream`, clears the JUST-INSTALLED new session state, and calls `showBrowse()`. The new session's `readyToPlay` then fires against an empty `_currentEpKey` → progress writes no-op for the whole session.

Two possible fix shapes:
1. **Reorder `onSourceActivated`** — call `stopStream` BEFORE installing new session. Simple but scatters the "old session is ending" signal to a weird pre-session moment.
2. **Split `stopStream(reason)` semantics** — distinguish "stop to replace" (no `showBrowse`, no `_currentEpKey` clear) from "stop to end" (current user-facing end-of-session behavior). The `streamStopped` signal carries the reason; StreamPage's handler branches on it.

Shape 2 is the architecturally correct answer — `streamStopped` today conflates two distinct lifecycle events. Agent 4 owns both `StreamPlayerController` and `StreamPage`; implementation is local.

### Batch 2.1 — Add stop reason to StreamPlayerController

- Enum in `StreamPlayerController`:
  ```cpp
  enum class StopReason {
      UserEnd,          // user closed player / Escape / back button
      Replacement,      // about to start a new stream; preserve session UX state
      Failure,          // controller hit timeout / engine error (emits streamFailed too)
  };
  ```
- `stopStream()` gains an overload `void stopStream(StopReason reason)`. Default-arg keeps existing callers on `StopReason::UserEnd`.
- `startStream` at [StreamPlayerController.cpp:26](src/ui/pages/stream/StreamPlayerController.cpp#L26) calls `stopStream(StopReason::Replacement)` as its first line (instead of `stopStream()`).
- `streamStopped(StopReason reason)` signal signature extended. All emit sites pass the reason.

**Files:** [src/ui/pages/stream/StreamPlayerController.h](src/ui/pages/stream/StreamPlayerController.h), [src/ui/pages/stream/StreamPlayerController.cpp](src/ui/pages/stream/StreamPlayerController.cpp).

**Success:** controller compiles with new API. Emit sites tagged. StreamPage not migrated yet — will still receive old-shape signal (Qt queued-connection warnings expected and tolerable for one batch).

### Batch 2.2 — StreamPage::onStreamStopped branches on reason

- Update connection at [StreamPage.cpp:104-105](src/ui/pages/StreamPage.cpp#L104-L105) to match new signature.
- `onStreamStopped(StopReason reason)` at [:1913-1933](src/ui/pages/StreamPage.cpp#L1913-L1933) branches:
  ```cpp
  void StreamPage::onStreamStopped(StreamPlayerController::StopReason reason) {
      if (reason == StopReason::Replacement) {
          // NEW SESSION IS ABOUT TO START — do not touch session state, do not navigate
          // Hide the OLD buffer overlay context (the new session will re-show it if needed)
          // Disconnect OLD player signals; new session will reconnect in onReadyToPlay
          disconnect(m_player, nullptr, this, nullptr);  // wildcard — matches existing pattern
          return;
      }
      // UserEnd + Failure fall through to existing teardown logic:
      m_session.epKey.clear();
      m_player->setPersistenceMode(PersistenceMode::LibraryVideos);
      disconnect(m_player, nullptr, this, nullptr);
      m_bufferOverlay->hide();
      // Overlay-visible guard (next-episode) unchanged
      if (!m_nextEpisodeOverlay->isVisible()) showBrowse();
  }
  ```
- Also update failure-emit sites to call `stopStream(StopReason::Failure)` in the controller (vs `UserEnd`).

**Files:** [src/ui/pages/StreamPage.h](src/ui/pages/StreamPage.h), [src/ui/pages/StreamPage.cpp](src/ui/pages/StreamPage.cpp).

**Success:** source-switch mid-play: no flash-to-browse. Buffer overlay stays visible through handoff. New session's `_currentEpKey` survives. Progress writes correctly. User-end-stop behavior unchanged. Failure-stop behavior unchanged.

### Phase 2 exit criteria
- `StopReason` enum live.
- `streamStopped(StopReason)` signal reaches StreamPage with correct reason.
- Source-switch test: start stream A, play 30s, switch to stream B → no visual flash, buffer overlay holds, progress writes under stream B's epKey.
- Agent 6 review against audit P0-1 citation chain.
- `READY FOR REVIEW — [Agent 4, STREAM_LIFECYCLE_FIX Phase 2]: Source-switch reentrancy split | Objective: Phase 2 per TODO + audit. Files: ...`

---

## Phase 3 — P1 cleanup: m_infoHash + 3s failure timer + streamError wiring

**Why:** Three P1s cluster into one small phase. All local fixes; all consumers of Phase 1's generation-check pattern.

### Batch 3.1 — Clear m_infoHash on failure paths (P1-1)

- Audit confirmed (chat.md:18828-18837): failure paths at `StreamPlayerController.cpp:47 / :55 / :93 / :153` set `m_active = false` without clearing `m_infoHash`. Next `startStream`'s defensive `stopStream()` early-returns on `!m_active`, leaving stale hash.
- Fix: every failure path clears `m_infoHash` + `m_selectedStream` explicitly before emitting `streamFailed`. One helper:
  ```cpp
  void StreamPlayerController::clearSessionState() {
      m_infoHash.clear();
      m_selectedStream = {};
      m_pollCount = 0;
      m_lastError.clear();
  }
  ```
- Call `clearSessionState()` from all 4 failure sites + `stopStream(UserEnd)` + `stopStream(Replacement)` — the latter is technically redundant with the next `startStream` re-init, but explicit beats implicit here.

**Files:** [src/ui/pages/stream/StreamPlayerController.h](src/ui/pages/stream/StreamPlayerController.h), [src/ui/pages/stream/StreamPlayerController.cpp](src/ui/pages/stream/StreamPlayerController.cpp).

**Success:** failed-open → user retries different stream → seek pre-gate uses the NEW hash, not the stale one. `currentInfoHash()` returns empty between sessions.

### Batch 3.2 — Generation-check the 3s failure timer (P1-2)

- [StreamPage.cpp:1907-1910](src/ui/pages/StreamPage.cpp#L1907-L1910) — 3s `singleShot` guards only on `isActive()`. Audit scenario: failure at T → user navigates to AddonManager at T+0.5s → T+3s timer fires → `isActive()` still false → `showBrowse()` yanks user back.
- Fix: capture `currentGeneration()` at timer creation + check at fire time:
  ```cpp
  const quint64 gen = currentGeneration();
  QTimer::singleShot(3000, this, [this, gen]() {
      if (!isCurrentGeneration(gen)) return;                    // session changed
      if (m_mainStack->currentIndex() != 0 /*not browse*/) return;  // user navigated
      if (!m_playerController->isActive()) showBrowse();
  });
  ```
- The "user navigated" check guards against the navigation-in-same-session case. The generation check guards against new-session.

**Files:** [src/ui/pages/StreamPage.cpp](src/ui/pages/StreamPage.cpp).

**Success:** stream fails → user navigates to addon manager → 3s later addon manager is STILL shown. No silent yank-back.

### Batch 3.3 — Wire StreamEngine::streamError → controller (P1-4)

- Audit confirmed (chat.md:18858-18860): `StreamEngine::streamError` emitted at `StreamEngine.cpp:517 / :620` but grep returns zero connections. Records sit in `m_streams` until controller 120s HARD_TIMEOUT or explicit user stop.
- Fix: in `StreamPlayerController` constructor or `startStream` wire-up, connect `m_engine->streamError(hash, msg)` → `onEngineStreamError(hash, msg)`. Slot checks `hash == m_infoHash`; if match, `clearSessionState() + m_active = false + emit streamFailed(msg)`.
- Also make sure Phase 2's `stopStream(StopReason::Failure)` path fires here — reuse the same code path as poll-timeout failure.

**Files:** [src/ui/pages/stream/StreamPlayerController.h](src/ui/pages/stream/StreamPlayerController.h), [src/ui/pages/stream/StreamPlayerController.cpp](src/ui/pages/stream/StreamPlayerController.cpp).

**Success:** force a stream that hits `streamError` (no-video torrent or torrent-error scenario) → controller terminates within 1s instead of 120s. User sees failure UI promptly.

### Phase 3 exit criteria
- Stale `m_infoHash` impossible post-failure.
- 3s failure timer can't navigate a different session.
- StreamEngine::streamError → controller failure within 1s.
- Agent 6 review against audit P1-1 + P1-2 + P1-4 citation chain.

---

## Phase 4 — P2 cleanup: Shift+N guard reshape + partial cleanup migration

**Why:** Two P2s block current work (P2-3 means Batch 2.6 Shift+N is a silent no-op), and one P2 (P2-2) is a trivial consistency fix that falls out of Phase 1's `resetSession` boundary.

### Batch 4.1 — Reshape Shift+N guard to session identity (P2-3)

- Audit confirmed (chat.md:18870): Shift+N handler at `StreamPage.cpp:1500` early-returns on `!m_pendingPlay.valid`, but `onSourceActivated` at `:1545` clears that flag before playback starts. During actual playback, `m_pendingPlay.valid == false`. Shift+N is silently broken today.
- Fix: replace the guard with session-identity check. What Shift+N actually needs to know: "is there a series currently playing?" Two signals:
  - `m_session.isValid()` — active session exists
  - `m_session.pending.mediaType == "series"` — current session is a series (not a movie / trailer / ad-hoc URL)
- New guard:
  ```cpp
  void StreamPage::onStreamNextEpisodeShortcut() {
      if (!m_session.isValid()) return;
      if (m_session.pending.mediaType != "series") return;
      if (!m_playerController->isActive()) return;
      // ... existing logic
  }
  ```
- Unblocks STREAM_UX_PARITY Phase 2 Batch 2.6 which is currently shipping a broken Shift+N per the validation note at chat.md:18896-18898.

**Files:** [src/ui/pages/StreamPage.cpp](src/ui/pages/StreamPage.cpp).

**Success:** Shift+N during series playback works (plays next episode). Shift+N during movie playback no-ops silently. Shift+N with no active session no-ops silently.

### Batch 4.2 — Route onSourceActivated through resetNextEpisodePrefetch (P2-2)

- Audit confirmed (chat.md:18869): `onSourceActivated` at `:1550-1551` resets `m_nearEndCrossed` + `m_nextPrefetch` inline but does NOT call `resetNextEpisodePrefetch()`. So it skips `m_nextShortcutPending` clear + prefetch aggregator disconnect.
- Fix: replace the two inline lines with a single `resetNextEpisodePrefetch()` call. Phase 1 already updated this helper to route through `m_session`.
- After Phase 1 migration, `resetSession("sourceActivated")` would be the fully-correct choice, but that ALSO clears `epKey` and `pending` — wrong for source-activation flow where we IMMEDIATELY install new values. So `resetNextEpisodePrefetch()` (a narrower reset) is what's wanted.

**Files:** [src/ui/pages/StreamPage.cpp](src/ui/pages/StreamPage.cpp).

**Success:** source-activation fully cleans prefetch state including `nextShortcutPending` flag and aggregator connections.

### Phase 4 exit criteria
- Shift+N works for series playback.
- onSourceActivated uses the canonical reset helper.
- P2-1 (deadline ms session-reset) implicitly covered by Phase 1's `m_session.lastDeadlineUpdateMs` migration — explicit verification only.
- Unblocks Batch 2.6.
- Agent 6 review against audit P2-1 + P2-2 + P2-3 citation chain.

---

## Phase 5 — HTTP worker cancellation (P1-3)

**Why:** Agent 4 validation (chat.md:18852-18856): `waitForPieces` takes `TorrentEngine*` + `infoHash` only, sleeps 200ms × 75 (up to 15s), doesn't accept `StreamHttpServer*` or a cancellation token. `StreamEngine::stopStream` removes the torrent with `deleteFiles=true` but an active worker inside `waitForPieces` holds a copied `FileEntry` and continues polling `haveContiguousBytes()` against an unknown hash. Behavior depends on `TorrentEngine` internals — best case 15s hang, worst case crash on invalidated libtorrent state.

Deeper implementation than phases 1-4. Adds cancellation threading through `StreamHttpServer` → `waitForPieces` path. Can ship independently after Phase 2 if Agent 4 capacity tight.

### Batch 5.1 — Add per-stream cancellation token to StreamEngine

- Extend `StreamEngine::StreamRecord` at [src/core/stream/StreamEngine.h:136](src/core/stream/StreamEngine.h) with:
  ```cpp
  std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);
  ```
- `StreamEngine::stopStream` at [:279-303](src/core/stream/StreamEngine.cpp#L279-L303) sets `record->cancelled->store(true)` BEFORE removing the record + unregistering + removing the torrent. This ensures workers observe cancellation before the engine invalidates underlying state.
- New API `std::shared_ptr<std::atomic<bool>> cancellationToken(QString infoHash) const` — lookup helper for `StreamHttpServer` to grab during `handleConnection`.

**Files:** [src/core/stream/StreamEngine.h](src/core/stream/StreamEngine.h), [src/core/stream/StreamEngine.cpp](src/core/stream/StreamEngine.cpp).

**Success:** token lives in StreamRecord. `stopStream` sets it. Lookup returns valid token for active stream, nullptr for unknown.

### Batch 5.2 — Thread cancellation through StreamHttpServer + waitForPieces

- `StreamHttpServer::handleConnection` at [:179](src/core/stream/StreamHttpServer.cpp#L179) grabs the cancellation token alongside the `FileEntry` copy. Token stored in a local captured by the worker.
- `waitForPieces` signature extended: `bool waitForPieces(TorrentEngine* engine, const QString& infoHash, qint64 offset, qint64 length, std::shared_ptr<std::atomic<bool>> cancelled)`. The 200ms poll loop now checks `if (cancelled && cancelled->load()) return false;` before each `haveContiguousBytes` call.
- On cancellation-observed return, `handleConnection` writes a clean HTTP abort (or just closes the socket — caller already handles that path).
- Shutdown-flag check at [:262-264](src/core/stream/StreamHttpServer.cpp#L262-L264) stays — covers server-level shutdown (separate from per-stream).

**Files:** [src/core/stream/StreamHttpServer.h](src/core/stream/StreamHttpServer.h), [src/core/stream/StreamHttpServer.cpp](src/core/stream/StreamHttpServer.cpp).

**Success:** user closes stream → within 200ms, worker exits `waitForPieces`. No 15s hang. Socket released promptly. No crash when TorrentEngine::haveContiguousBytes would have been called against removed hash (token short-circuits before the engine call).

**Isolate-commit:** yes. First cancellation-threading change in the HTTP worker path. Isolate to validate under weak-swarm close/reopen stress.

### Phase 5 exit criteria
- Cancellation tokens live per stream.
- `waitForPieces` observes tokens.
- Close-while-buffering releases socket < 1s.
- Agent 6 review against audit P1-3 citation chain.

---

## Scope decisions locked in

- **PlaybackSession as struct (not class).** In-StreamPage struct, no separate file, no new CMake entry. Keeps scope tight. If future complexity warrants a class, refactor is mechanical.
- **Generation counter is monotonic, not UUID.** `quint64` counter starting at 1 (0 reserved for "no session"). Never wraps in practical lifetime. Simpler than UUID, same correctness.
- **`resetSession(reason)` is a boundary, not a hook.** It clears state but does NOT emit signals / navigate UI / touch player widgets. Callers decide what UI follows. Keeps the reset pure.
- **`streamStopped(StopReason)` signal SIG change.** All emit sites + the one connect site update together in Phase 2.1+2.2. Not a breaking change for external callers (none exist outside StreamPage).
- **Shift+N guard uses session identity, not `m_pendingPlay.valid`.** The audit's P2-3 finding predicted this; Phase 4.1 closes it. Unblocks STREAM_UX_PARITY Batch 2.6.
- **HTTP worker cancellation is per-stream, not server-level.** Distinct from `m_shuttingDown`. Per-stream matches `StreamEngine::stopStream` semantics.
- **No wildcard-disconnect rewrite.** Audit advisory #7 recommends storing `QMetaObject::Connection` handles; we defer that. Wildcard-disconnect pattern is correct for the flows we have.

## Isolate-commit candidates

Per the TODO's Rule 11 section:
- **Batch 1.2** (mechanical migration of 3 core session fields) — cross-cuts StreamPage.cpp; isolate so any missed call site surfaces in isolation.
- **Batch 5.2** (HTTP worker cancellation threading) — first cancellation threading in worker path; isolate to stress-test under weak-swarm.

Other batches commit at phase boundaries.

## Existing functions/utilities to reuse (not rebuild)

- [`m_seekRetryState` orphan-timer fix pattern](src/ui/pages/StreamPage.cpp) — current local generation-check. Phase 1.3 generalizes into `PlaybackSession::generation`.
- [`resetNextEpisodePrefetch()` helper](src/ui/pages/StreamPage.cpp) — Phase 1.3 refactors internals to route through `m_session`; Phase 4.2 makes it the canonical cleanup path.
- [Wildcard `disconnect(sender, nullptr, this, nullptr)` pattern](src/ui/pages/StreamPage.cpp) — established per-session signal cleanup; Phase 2.2's `onStreamStopped(Replacement)` reuses.
- [`ConnectionGuard` in StreamHttpServer](src/core/stream/StreamHttpServer.cpp) — RAII connection counter; Phase 5's cancellation token pairs cleanly alongside it.

## Review gates

Each phase exits with:
```
READY FOR REVIEW — [Agent 4, STREAM_LIFECYCLE_FIX Phase X]: <title> | Objective: Phase X per STREAM_LIFECYCLE_FIX_TODO.md + agents/audits/tankostream_session_lifecycle_2026-04-15.md. Files: ...
```
Agent 6 reviews against audit + TODO as co-objective.

## Open design questions Agent 4 decides as domain master

- **`beginSession` vs `installSession` naming.** Either works; Agent 4's call.
- **`PlaybackSession` struct scope.** Nested in StreamPage (proposed) vs free struct in a new header. Nested is simpler; free-struct is more reusable for future tests.
- **Phase 5 cancellation token type.** `std::shared_ptr<std::atomic<bool>>` is straightforward; alternatives (Qt `QFuture`-like cancellation, engine-wide cancel registry) are heavier. Agent 4's call during implementation.
- **Phase 2 stop reason: 3 values or 2?** `Failure` can be folded into `UserEnd` if downstream handler doesn't need to distinguish. Current audit implies 3 is right (different log-level + UI-feedback cadence). Agent 4's call.
- **Phase 5 timing of commit relative to Player TODO Phase 2.** Cancellation threading is independent of Player Phase 2, but smoke-testing is cleaner if both have landed — Player fence reduces the race between stopStream and in-flight worker.

## What NOT to include (explicit deferrals)

- `QMetaObject::Connection`-handle-per-flow rewrite (audit advisory #7). Wildcard-disconnect stays.
- `PlaybackSession` as a separate class / file. Struct nested in StreamPage stays.
- Engine-layer `haveContiguousBytes` hardening for unknown hash. Phase 5's cancellation token short-circuits before engine call; engine-level hardening is separate concern.
- Tankoyomi / manga-download lifecycle work. Different subsystem; different agent.
- Videos-mode progress / persistence lifecycle. Outside stream scope.
- Cross-agent session-identity propagation between StreamPage and VideoPlayer. VideoPlayer's own session identity (P1-5) is Player TODO Phase 3's concern. StreamPage identity stays internal.

## Rule 6 + Rule 11 application

- Rule 6: every batch compiles + smokes on Hemanth's box before `READY TO COMMIT`. Agent 4 does not declare done without build verification.
- Rule 11: per-batch READY TO COMMIT lines; Agent 0 batches commits at phase boundaries (isolate-commit candidates above ship individually).
- Single-rebuild-per-batch per `feedback_one_fix_per_rebuild`.
- **Evidence-before-analysis** per `feedback_evidence_before_analysis.md`: if a fix batch needs runtime-behavior confirmation (e.g., whether Phase 5.2's cancellation actually short-circuits before `haveContiguousBytes` on removed hash), Agent 4 ships instrumentation first.

## Verification procedure (end-to-end once all 5 phases ship)

1. **Source-switch mid-play:** start series S1E1 → play 30s → click a different source card for the same episode → expect smooth handoff, no flash-to-browse, buffer overlay visible through transition, progress writes under the new source's key. (P0-1 fix validated.)
2. **Cold-start + immediate close:** launch stream → close before first frame → no Qt warnings, no stale `m_infoHash` in next launch. (P1-1 + process cleanup validated.)
3. **Stream failure + user navigation:** launch a broken stream → on failure, navigate to Addon Manager → wait 5s → expect to STILL be in Addon Manager, no silent yank-back. (P1-2 fix validated.)
4. **Stream with no-video:** force a stream that hits `StreamEngine::streamError` (small test torrent with only a `.nfo`, or use a test helper) → expect failure within 1s, not 120s. (P1-4 fix validated.)
5. **Shift+N during series playback:** start a series → during playback, hit Shift+N → expect next episode to start. (P2-3 fix validated + Batch 2.6 unblocked.)
6. **Shift+N during movie playback:** start a movie → Shift+N → no-op. No crash. (P2-3 fix correctness.)
7. **Fast close/reopen within 2s:** launch → close within 1s → relaunch → first sliding-window deadline update fires correctly (check log trace). (P1-1 covers via session reset; verify.)
8. **Close-while-buffering under weak swarm:** launch a stream that takes 10s to buffer → click close at 5s → socket released within 200ms (lsof trace). No 15s hang. (P1-3 fix validated.)
9. **Regression:** user-end-stop (Escape from playing stream) still returns to browse correctly. (P0-1 split preserves UserEnd behavior.)
10. **Regression:** end-of-episode next-episode overlay (Batch 2.5) still works — overlay shows, countdown fires, Play Now works. (P2-2 fix doesn't break overlay flow.)

## Next steps post-approval

1. Agent 0 posts routing announcement in chat.md.
2. Agent 4 executes phased per Rule 6 + Rule 11.
3. Agent 6 gates each phase exit.
4. Agent 0 commits at phase boundaries (isolate-commit exceptions per Rule 11 section).
5. Coordinate with Agent 3 PLAYER_LIFECYCLE_FIX_TODO execution — ideal landing order per Cross-agent coordination section above.
6. MEMORY.md `Active repo-root fix TODOs` line updated to include this TODO.

---

**End of plan.**
