# Archive: Player Polish Phase 6 — Error Recovery

**Agent:** 3 (Video Player)
**Reviewer:** Agent 6 (Objective Compliance Reviewer)
**Objective source:**
- Kodi `xbmc/cores/VideoPlayer/VideoPlayer.cpp` / `ProcessVideo.cpp` error-loop pattern (non-fatal decode errors skip + log; fatal break)
- OBS `libobs-d3d11/d3d11-subsystem.cpp` device-lost pattern (DXGI_ERROR_DEVICE_REMOVED detection + `GetDeviceRemovedReason` logging + device recreation)
- `PLAYER_POLISH_TODO.md:277-304` (Phase 6 batch definitions + exit criteria)

**Outcome:** REVIEW PASSED 2026-04-15 — first-read pass, no P0 or P1 raised.
**Shape:** 0 P0, 0 P1, 7 P2 (all non-blocking observations). No questions requiring Agent 3 response.

---

## Scope

Three shipped batches: (6.1) sidecar crash auto-restart with 3-retry exponential backoff (250/500/1000ms) and resume-at-last-PTS; (6.2) D3D11 device-lost recovery with full teardown + reinit + zero-copy resignal; (6.3) codec-error skip signal from sidecar, strictly distinct from the terminal `error` event, with throttled user toast.

Out of scope: functional verification (crash via `taskkill`, TDR-triggered device-lost, corrupted-media playback) — Hemanth's `build_qrhi.bat` rebuild + smoke tests are his job. Static review only.

Files reviewed:
- [src/ui/player/SidecarProcess.h/.cpp](src/ui/player/SidecarProcess.cpp) — `m_intentionalShutdown` flag + `processCrashed` signal + `decodeError` signal + `onProcessFinished` crash distinction
- [src/ui/player/VideoPlayer.h/.cpp](src/ui/player/VideoPlayer.cpp) — `onSidecarCrashed` handler + `m_sidecarRestartTimer` backoff ladder + `m_lastKnownPosSec` mirror + decodeError throttled toast + deviceReconnecting toast
- [src/ui/player/FrameCanvas.h/.cpp](src/ui/player/FrameCanvas.cpp) — `initializeD3D` + `tearDownD3D` extraction, `isDeviceLost` helper, `recoverFromDeviceLost` + `m_recovering` re-entry guard
- Sidecar `native_sidecar/src/main.cpp` — `decode_error` event branch at :448-461 (serialize with `recoverable:true`, don't mutate state)
- Sidecar `native_sidecar/src/video_decoder.cpp` — `DECODE_SKIP_PACKET` emit at :878, `DECODE_SKIP_FRAME` emit at :892

## Parity (Present)

**Batch 6.1 — Sidecar crash auto-restart**

- **`m_intentionalShutdown` flag distinguishes shutdown from crash.** [SidecarProcess.cpp:81](src/ui/player/SidecarProcess.cpp#L81) clears on `start`; :128-131 sets on `sendShutdown`; `onProcessFinished` at :390-399 captures the flag + clears it + emits `processCrashed` ONLY on unintentional termination. Clean single-point state ownership. ✓
- **Backoff ladder 250/500/1000ms via `kBackoffMs[3]`** at [VideoPlayer.cpp:455](src/ui/player/VideoPlayer.cpp#L455). Doubling pattern (effectively exponential for 3 retries). Matches chat.md narrative at :285. ✓
- **`m_lastKnownPosSec` mirror from every clean `timeUpdate`** at [VideoPlayer.cpp:374](src/ui/player/VideoPlayer.cpp#L374). Seeking skips the mirror (:371) so a mid-seek crash resumes from pre-seek position (correct — the seek PTS is lost anyway because sidecar hadn't reached it). ✓
- **3-retry cap** at [VideoPlayer.cpp:448-452](src/ui/player/VideoPlayer.cpp#L448-L452): "Player stopped — reconnection failed" toast + counter reset. ✓
- **Counter clear on successful recovery** at [:346-349](src/ui/player/VideoPlayer.cpp#L346-L349) — first `onFirstFrame` post-restart clears the retry count. ✓
- **Counter clear on user-driven openFile** at [:250](src/ui/player/VideoPlayer.cpp#L250). ✓
- **Restart timer cancelled on user-driven close/stop** at [:289](src/ui/player/VideoPlayer.cpp#L289) — user intent supersedes pending respawn. ✓
- **Resume flow via existing `m_pendingFile`/`m_pendingStartSec`** at [:466-470](src/ui/player/VideoPlayer.cpp#L466-L470): restart triggers `m_sidecar->start()`; when sidecar reports ready, the existing sidecar-ready handler at :338-340 sends `sendOpen(m_pendingFile, m_pendingStartSec)`. Reuses the standard open flow, no new codepath. ✓

**Batch 6.2 — D3D device-lost recovery**

- **`initializeD3D()` + `tearDownD3D()` extraction from `showEvent` / destructor.** [FrameCanvas.cpp:62 + :359](src/ui/player/FrameCanvas.cpp#L62). `tearDownD3D` releases in reverse creation order, idempotent, safe on partially-initialized state. Comment at :359-362 documents the invariant. ✓
- **`isDeviceLost(HRESULT)` matches DXGI_ERROR_DEVICE_REMOVED + DXGI_ERROR_DEVICE_RESET** at [:352-356](src/ui/player/FrameCanvas.cpp#L352-L356). Both are the standard D3D11 lost-device signals per MSDN `IDXGISwapChain::Present` docs. ✓ Pattern reference comment at :349-350 cites OBS d3d11-subsystem.cpp line 44; that citation is accurate for the `GetDeviceRemovedReason` logging pattern (Tankoban's version at :397-402). Full device recreation semantics are Agent 3's synthesis appropriate for Tankoban's single-canvas model (OBS's distributed recovery is specific to their multi-output architecture).
- **`recoverFromDeviceLost()` sequence** at [:386-433](src/ui/player/FrameCanvas.cpp#L386-L433):
  1. Re-entry guard via `m_recovering` (:392-393) — one recovery attempt per tick.
  2. Log `GetDeviceRemovedReason` for post-mortem (:397-402).
  3. Emit `deviceReconnecting` signal BEFORE teardown (:404) so listeners toast first.
  4. Stop render timer (:408) — teardown isn't racing `renderFrame` pointer reads.
  5. Zero-copy resignal: emit `zeroCopyActivated(false)` + clear `m_pendingD3DHandle/Width/Height` (:415-421). Correct — the sidecar-published NT handle is scoped to the sidecar's D3D device; the CPU-side imported SRV dies with the torn-down device.
  6. `tearDownD3D` (:423).
  7. `initializeD3D` (:425). On failure, `m_recovering = false` + stay-stopped; user close+reopen triggers `showEvent` re-dispatch.
- **Present failure at renderFrame** at [:524-534](src/ui/player/FrameCanvas.cpp#L524-L534) consults `isDeviceLost`; on match calls `recoverFromDeviceLost` + early return (rest of tick skips stale pointers). Non-lost Present failures log but don't trigger recovery. ✓
- **VideoPlayer toast wire** at [:1071-1074](src/ui/player/VideoPlayer.cpp#L1071-L1074) — "Reconnecting display…" on `deviceReconnecting`. ✓
- **Lazy SHM texture recreate** — `consumeShmFrame`'s `m_videoTexW=0` check triggers a fresh texture creation after teardown. Zero-copy stays OFF until sidecar re-publishes `d3d11_texture`. Matches narrative.

**Batch 6.3 — Codec error skip signal**

- **`decode_error` distinct from terminal `error`.** Sidecar `main.cpp:448-461` routes `decode_error` into a JSON event with `code/message/recoverable:true`; state is NOT mutated (no `set_state(IDLE)`). The terminal `error` path at :440-446 stays untouched for fatal cases. ✓
- **`recoverable` bool plumbed end-to-end** — sidecar sets true, `SidecarProcess::processLine` at :349-355 parses into `emit decodeError(code, message, recoverable)`, VideoPlayer lambda at :192 gates on it (fatal-with-decode-error-shape falls through without toast). Extensible for future non-recoverable decode errors. ✓
- **Throttled toast via static lambda local** at [VideoPlayer.cpp:193-197](src/ui/player/VideoPlayer.cpp#L193-L197) — 3-second minimum interval. Corrupted media fires dozens/sec; UI sees one toast every 3s. Throttle lives at consumer layer (not SidecarProcess), so future diagnostic consumers get every event unthrottled. ✓
- **Sidecar emit sites at the two avcodec failure points:**
  - `DECODE_SKIP_PACKET` at [video_decoder.cpp:874-879](file:///C:/Users/Suprabha/Desktop/TankobanQTGroundWork/native_sidecar/src/video_decoder.cpp#L874-L879) — `avcodec_send_packet` failure + `continue` (skip packet).
  - `DECODE_SKIP_FRAME` at [video_decoder.cpp:885-893](file:///C:/Users/Suprabha/Desktop/TankobanQTGroundWork/native_sidecar/src/video_decoder.cpp#L885-L893) — `avcodec_receive_frame` failure + `break` (stop inner drain loop, resume outer decode).
  - `av_strerror` output carried in the message. Matches Kodi's ProcessVideo pattern (log + skip). ✓
- **Existing fatal paths unchanged** — `DECODE_INIT_FAILED` at [video_decoder.cpp:220](file:///C:/Users/Suprabha/Desktop/TankobanQTGroundWork/native_sidecar/src/video_decoder.cpp#L220) + audio-side parallels still emit terminal `error`. ✓

## Gaps (Missing or Simplified)

**P0:** none.

**P1:** none.

**P2** (all non-blocking polish observations):

1. **`m_sidecarRetryCount` not cleared on user-driven close/stop.** [VideoPlayer.cpp:286-289](src/ui/player/VideoPlayer.cpp#L286-L289) stops the restart timer but doesn't zero the counter. If user stops mid-retry-sequence (counter=2) and later opens a new file, the new `openFile` at :250 clears it. Eventual consistency, no user-visible bug. Noting for clean-state hygiene — could fold into the stop path alongside the timer.stop.

2. **`m_sidecarRetryCount = 0` on `onFirstFrame` fires even for non-restart opens.** [VideoPlayer.cpp:346-349](src/ui/player/VideoPlayer.cpp#L346-L349). Intended to clear on successful crash-recovery respawn; also fires on every normal open. Harmless (counter was already 0), just slightly broad semantically. A `if (m_sidecarRetryCount > 0)` gate would read more precisely but is clutter — accept as shipped.

3. **`decode_error` throttle uses static lambda local.** [VideoPlayer.cpp:193](src/ui/player/VideoPlayer.cpp#L193). Shared across all VideoPlayer instances; only one ever exists, so fine. Minor stylistic preference — a `qint64 m_lastDecodeErrorToastMs = 0` member would be more idiomatic and survives if multiple VideoPlayers are ever added. No behavior change; accept.

4. **Backoff schedule fixed at `{250, 500, 1000}` ms, 3-retry cap.** Covers the TDR-timeout window (~2s) + most subprocess-launch transients. Could miss a genuine "Windows Update mid-restart" scenario where recovery takes >1.75s across all retries. Current user recourse: close+reopen player to trigger fresh 3-retry budget. Acceptable; noting for future if Hemanth reports "reconnection failed" on real-world incidents.

5. **No backoff jitter.** Classic concurrent-crash thundering-herd concern doesn't apply here (single player instance per process), so trivial. Noting only for completeness.

6. **OBS d3d11-subsystem citation at [FrameCanvas.cpp:349-350](src/ui/player/FrameCanvas.cpp#L349-L350) says "~line 44".** That line is specifically `LogD3D11ErrorDetails`'s `GetDeviceRemovedReason` call, which Tankoban's recovery mirrors at :397-402. But the full in-place device-recreation logic is Agent 3's synthesis — OBS's own recovery is distributed across `gs_device::device_lost` + application-level callbacks in `obs-display.c` etc., specific to OBS's multi-canvas model. Comment is accurate for what it cites; could add "(recovery synthesis is local to Tankoban's single-canvas model)" for future-reader clarity. Non-blocking.

7. **`recoverFromDeviceLost` re-entry pattern handles recursive device-lost correctly, but the narrow window is slightly subtle.** If `initializeD3D()` at :425 itself triggers a fresh device-lost signal (e.g., on an actively-unstable GPU), the `m_recovering` flag guards. On the next Present failure tick, `recoverFromDeviceLost` fires again, tears down the partial state, retries init. No infinite recursion — the guard + renderTimer.stop() at :408 ensures Present isn't firing during teardown. Clean but relies on the reader understanding the Present-failure → recoverFromDeviceLost → (possibly fails → m_recovering=false → next tick re-fires) loop. Could add a tiny comment at :392-393 spelling out "next-tick retry on failed reinit." Non-blocking.

## Parity summary vs objective source

- **Kodi ProcessVideo error loop pattern**: non-fatal decode errors log + skip + continue; fatal errors surface + terminate. Tankoban Batch 6.3 matches: DECODE_SKIP_PACKET/FRAME continue/break-inner, DECODE_INIT_FAILED stays terminal. ✓
- **OBS d3d11-subsystem device-lost pattern**: detect via Present HRESULT, log removal reason, tear down + recreate. Tankoban Batch 6.2 matches + adds explicit `DEVICE_RESET` handling (broader than OBS's `DEVICE_REMOVED`-only logged case, correct per MSDN). ✓
- **Crash auto-restart** is not in the cited objective sources but follows standard subprocess-recovery patterns (mpv subprocess error handling, Chromium per-tab recovery). Implementation is sound: intent flag, exponential backoff, retry cap, position-mirror resume. ✓

### Verdict

- [x] All P0 closed (n/a — none raised)
- [x] All P1 closed or justified (n/a — none raised)
- [x] Ready for commit (Rule 11)

**REVIEW PASSED — [Agent 3, Player Polish Phase 6]** 2026-04-15.

Player Polish track closes with Phases 1–6 all PASSED. Phase 7 (Deferred Cleanup) remains per TODO. Batch 6.3 sidecar rebuild via `build_qrhi.bat` needed before main-app can observe `decode_error` events; Batches 6.1 + 6.2 main-app-only ship on the existing build. `READY TO COMMIT -- [Agent 3, Batch 6.1/6.2/6.3]` lines stand.
