# Stream Engine Rebuild TODO — Tankoban 2

**Authored 2026-04-18 by Agent 0.** Derived from approved plan at `C:\Users\Suprabha\.claude\plans\i-want-you-to-cosmic-newell.md` + Agent 7 audit `agents/audits/player_stremio_mpv_parity_2026-04-17.md` + Agent 3 validation at `agents/chat.md:3159-3243` + Mode B repro telemetry from 2026-04-17 session. Gated on Congress 5 ratification.

---

## Context

Stream mode has consumed more hours than the rest of the app combined, including an FFmpeg player built from scratch. Current `StreamEngine.cpp` is a 1269-line monolith evolved via patches (Phase 2.6.1 → 2.6.2 → 2.6.3). The 2026-04-17 Mode B reproduction — seek-target pieces [21,22] stuck `ready=0 have=[0,0]` for 4 minutes despite head gate at 100%, 70+ peers, 11 MB/s — empirically falsified Agent 4's claim that Phase 2.6.3 closed Mode B. This TODO drives the demolition-and-rebuild against Stremio Reference as the semantic blueprint (Stremio = Node.js/WebTorrent — behavior parity, NOT API port).

Player-side parity work (PLAYER_STREMIO_PARITY Phase 1 — buffered-range surface end-to-end) landed in `973ff32` + `c510a3c`. Consumer-side IPC contract survives this rebuild untouched. PLAYER_STREMIO_PARITY Phases 2-8 continue in parallel.

---

## Objective

**Close Hemanth's 6-point MVP bar (2026-04-17):**
1. No wonky aspect ratios *(not in engine scope — see Non-Goals)*
2. No frame-stuck (Mode B seek-hang)
3. No infinite buffering at 1000+ seeds (Mode A cold-start 0%-buffering)
4. Subtitles work through streaming path
5. Playback tracking persists across streams
6. General "streams like Stremio"

**Success =** a stream opens in < 6s p50 on a healthy swarm, mid-file seek into unbuffered resumes in < 3s with per-peer `have_piece` telemetry proving which class of failure (scheduler vs swarm) when it doesn't, progress persists across app restart and episode switches, and the rebuild is git-revertible at any phase except P6.

---

## Non-Goals (explicitly out of scope for this plan)

- **UI page rewrites.** StreamPage, StreamHomeBoard, StreamContinueStrip, StreamDetailView, StreamLibraryLayout, cards, addon manager, calendar screen all untouched. Hemanth 2026-04-17 confirmed: UI is covered.
- **Source fetching.** MetaAggregator, StreamAggregator, AddonRegistry, AddonTransport, MetaItem, CalendarEngine untouched. Torrentio integration stays. Stremio addon protocol adoption deferred forever.
- **Torrent substrate refactor.** TorrentEngine + TorrentClient internals untouched except one new signal (`pieceFinished`) + one optional method (`peersWithPiece`).
- **Native player pipeline.** Sidecar, FrameCanvas, D3D11, SHM overlay, subtitle rendering untouched (probe-escalation at `video_decoder.cpp:221` is the single exception — surgical sidecar internal change).
- **`StreamHttpServer` rewrite.** Codified non-goal at `StreamEngine.h:43-50`. One surgical change in P2 (`waitForPieces → StreamPieceWaiter::await`).
- **`StreamProgress` schema change.** Additive-only: P0 adds `schema_version: 1` field; namespace signatures frozen for rebuild window.
- **HLS transcoding.** Stremio's `stream-server-master` has it; we don't port.
- **libmpv wrap.** We keep direct-FFmpeg sidecar. Parity is feature-level.
- **Aspect ratio correctness.** Sidecar (Agent 3) domain, not engine. Engine rebuild cannot regress it because engine doesn't touch aspect. MVP criterion 1 validates against current sidecar behavior.
- **Parallel `stream_v2/` folder.** Flat single-checkout per `feedback_no_worktrees`. Demolish-and-rebuild on master.
- **Literal port of Stremio Node.js code.** Stremio = WebTorrent, not libtorrent. Port the behavior contract; derive libtorrent calls from libtorrent 2.0 docs (`C:\tools\libtorrent-2.0-msvc`).

---

## Agent Ownership

**Primary:** Agent 4 (Stream) — owns P1 scaffold + P2 integration + P3 prioritizer + P5 stall detection + P6 demolition.

**Cross-domain:**
- **Agent 4B (Sources — TorrentEngine owner):** P2 `pieceFinished` signal exposure (HARD dependency) + optional P3 `peersWithPiece` method + API-freeze commitment. See HELP.md request active this session.
- **Agent 3 (Video Player — sidecar owner):** P4 probe escalation at `native_sidecar/src/video_decoder.cpp:221` + `demuxer.cpp:65`. Single-file-pair sidecar change, no IPC impact. Consumes preserved `contiguousHaveRanges` post-rebuild.
- **Agent 5 (Library UX):** No code changes expected. Position in Congress 5 confirms `streamFailed` text / `bufferedRangesChanged` cadence / `streamStopped(StopReason)` vocabulary assumptions hold.

**Agent 0 (Coordinator):** P0 hardening + TODO authoring + dashboard/memory maintenance + commit sweeps per Rule 11. Congress 5 synthesis post-positions.

---

## Phase 0 — Pre-demolition hardening (Agent 0)

**Purpose:** freeze StreamProgress schema before rebuild window opens. Tag T0-baseline for rollback.

### Batch 0.1 — `schema_version` field + T0 tag

- Add `obj["schema_version"] = 1;` to `StreamProgress::makeWatchState()` in `StreamProgress.h:39-48`
- Git-tag current HEAD as `stream-rebuild/T0-baseline`
- Verify: existing Continue Watching entry loads correctly post-change (backward-compat — field is purely additive)

**Files:** `src/core/stream/StreamProgress.h`

**Exit criteria:**
- Green build, all current streams play exactly as pre-change
- Existing `stream:ttXX:sN:eE` QSettings entries load without loss
- Tag `stream-rebuild/T0-baseline` exists at HEAD

---

## Phase 1 — Scaffold + contract freeze (Agent 4)

**Purpose:** stand up new file structure with pass-through routing. Zero behavior change. Enables per-phase replacement of internals without disrupting callers.

### Batch 1.1 — New file shells

Create 4 new file pairs (empty-class shells that compile, stubbed methods return defaults):
- `src/core/stream/StreamSession.{h,cpp}` — per-hash FSM (states defined, no transitions yet)
- `src/core/stream/StreamPrioritizer.{h,cpp}` — deadline policy (API declared, bodies stubbed)
- `src/core/stream/StreamPieceWaiter.{h,cpp}` — async-wake registry (API declared, fallback poll-every-500ms mode as default)
- `src/core/stream/StreamSeekClassifier.{h,cpp}` — SeekType enum + position history (API declared)

### Batch 1.2 — StreamEngine facade rewrite (preserves 17-method public API)

Rewrite `StreamEngine.{h,cpp}` as facade. Current 1269-LOC body gets inlined temporarily (preserved verbatim behavior) into the new facade's `d_ptr` or private-impl block. All 17 public methods + 2 signals + **3** structs (StreamFileResult, StreamTorrentStatus, StreamEngineStats; `StreamRecord` is private-impl) + 1 enum frozen per Congress 5 Amendment 2.

Current internal helpers that move to private-impl temporarily (deleted in P6):
- `applyStreamPriorities` (current private method)
- `waitForPieces` (in StreamHttpServer, surgical change happens in P2)
- `StreamRecord` flag-state bookkeeping fields (metadataReady, registered bools)

### Batch 1.3 — CMake + build verification

- Update `CMakeLists.txt` with 4 new .cpp/.h file pairs
- Full clean build
- Smoke test: stream Sopranos S06E09, verify first-frame + mid-file seek work exactly as T0

**Files:** `src/core/stream/StreamSession.{h,cpp}`, `src/core/stream/StreamPrioritizer.{h,cpp}`, `src/core/stream/StreamPieceWaiter.{h,cpp}`, `src/core/stream/StreamSeekClassifier.{h,cpp}`, `src/core/stream/StreamEngine.{h,cpp}`, `CMakeLists.txt`

**Exit criteria:**
- Green full build + MOC regeneration for new Q_OBJECT classes
- Stream playback identical to T0 (no telemetry delta)
- Tag `stream-rebuild/phase-1-scaffold`

**Revertible:** YES — delete 4 new file pairs, revert `StreamEngine.cpp` from `stream-rebuild/T0-baseline`.

---

## Phase 2 — HTTP piece-waiter async (Agent 4 + Agent 4B)

**Purpose:** kill Mode A latency floor from polling sleep. Replace `StreamHttpServer::waitForPieces` 15s poll-spin with `QWaitCondition`-driven async wake on `TorrentEngine::pieceFinished` signal.

**Dependency:** Agent 4B must have delivered `pieceFinished(hash, pieceIdx)` signal (see HELP.md). Block P2 start until this ships.

### Batch 2.1 — Agent 4B: `pieceFinished` signal emission

Single emit addition in `TorrentEngine.cpp:153-156` alert branch:
```cpp
emit pieceFinished(QString::fromStdString(pfa->handle.info_hash().to_string()),
                   static_cast<int>(pfa->piece_index));
```
No behavior change for existing consumers.

### Batch 2.2 — Agent 4: StreamPieceWaiter implementation

- Register subscribers keyed by `(hash, pieceIdx)` → `QWaitCondition`
- On `pieceFinished` signal: wake matching subscribers
- Timeout fallback (5s default, configurable per `await()` call)
- Fallback poll-every-500ms mode behind `STREAM_PIECE_WAITER_POLL=1` env flag for rollback safety

### Batch 2.3 — Agent 4: StreamHttpServer.cpp surgical replacement

Replace current `waitForPieces` poll-sleep loop at `StreamHttpServer.cpp:82` with `StreamPieceWaiter::await(hash, pieceIdx, timeoutMs)`. New `piece_wait` telemetry event captures actual wait duration.

**Files:** `src/core/torrent/TorrentEngine.{h,cpp}` (Agent 4B), `src/core/stream/StreamPieceWaiter.{h,cpp}` (Agent 4), `src/core/stream/StreamHttpServer.cpp` (Agent 4)

**Exit criteria:**
- Cold-start 1000-seed torrent: first byte to sidecar < 2s after `metadata_ready` event (T0: 15s+ polling sleeps visible)
- `piece_wait` telemetry event shows typical wait duration < 500ms
- Fallback poll mode verified (env flag flips to polling)
- Tag `stream-rebuild/phase-2-piece-waiter`

**Revertible:** YES — flip env flag to fallback poll mode.

---

## Phase 3 — Prioritizer + seek-type (Agent 4)

**Purpose:** kill Mode B seek-hang. Replace one-shot deadline set with ticking re-assertion on UserScrub. Per-peer `have_piece` telemetry falsifies Risk R3.

### Batch 3.1 — StreamSeekClassifier implementation

Classify seek intent based on `(delta_pos_sec, elapsed_ms_since_last_position)`:
- Delta > 30s ahead, < 2s elapsed → `UserScrub`
- Delta < 5s ahead, ≥ 2s elapsed → `Sequential` (steady-state playback)
- Starting near pos 0 → `InitialPlayback`
- End-of-file reads (`offset > filesize - 10MB`) → `ContainerMetadata`

### Batch 3.2 — StreamPrioritizer::onPlaybackTick + onSeek

Replaces `updatePlaybackWindow` + `prepareSeekTarget` internals (public signatures preserved — they now delegate to prioritizer).
- Tick every 500ms from StreamSession timer
- On `UserScrub`: if seek pieces haven't arrived in 1500ms, re-assert priority 7 + deadline escalation (500ms → 300ms → 200ms). Cap at 3 retries before falling to stall-recovery semantics (P5).
- On `Sequential`: maintain urgency window (15-60 pieces based on bitrate + EMA download speed)
- EMA download speed: alpha=0.2 on `torrentProgress` signal's `download_rate`

### Batch 3.3 — Per-peer have-piece telemetry (REQUIRED — hard P3 exit gate per Congress 5 Amendment 1)

New `seek_target_peer_have` field in `seek_target` telemetry event. Logs peers-with-piece count BEFORE priority-7 re-assertion fires. Distinguishes scheduler-starvation (priority 7 can win) from swarm-availability-starvation (no amount of priority wins — falls to stall-recovery + user-visible messaging).

**`peersWithPiece` is REQUIRED, not optional.** Congress 5 ratified 2026-04-18 adopted Agent 4B + Agent 4 consensus: the heuristic fallback from `peer_info.progress` measures aggregate swarm completeness, not per-piece availability, leaving R3 un-falsified by construction. Agent 4B ships `peersWithPiece(hash, pieceIdx) const` as part of P3 parallel with Agent 4's prioritizer work. If libtorrent 2.0 API makes it expensive on Agent 4B's side during implementation, Agent 0 reconvenes a scoped HELP — does NOT auto-fall-back.

**Files:** `src/core/stream/StreamSeekClassifier.{h,cpp}`, `src/core/stream/StreamPrioritizer.{h,cpp}`, `src/core/stream/StreamSession.{h,cpp}`, `src/core/torrent/TorrentEngine.{h,cpp}` (REQUIRED, Agent 4B ships `peersWithPiece`)

**Exit criteria:**
- Scrub to 60%-mark on 90%-complete healthy torrent: playback resumes < 3s
- `seek_target` event: `ready=1` within ≤ 4 retries (≤ 1.2s)
- `seek_target_peer_have` > 0 before any priority-7 fires (else: swarm-starvation, P5 handles)
- Stop→start→stop loop 50× without regression
- Tag `stream-rebuild/phase-3-prioritizer`

**Revertible:** YES — revert to single-shot deadline strategy by reverting this phase's commits.

---

## Phase 4 — Sidecar probe escalation (Agent 3)

**Purpose:** kill Mode A cold-start 0%-buffering. Current single-shot 20MB/30s probe at `video_decoder.cpp:221` is the second diagnosed Mode A latency floor.

### Batch 4.1 — Three-tier probe sequence

At `native_sidecar/src/video_decoder.cpp:221` and `demuxer.cpp:65`:
- **Tier 1:** `probesize=512KB`, `analyzeduration=750ms`, `rw_timeout=5s`
- **Tier 2** (if Tier 1 insufficient): `probesize=2MB`, `analyzeduration=2s`, `rw_timeout=15s`
- **Tier 3** (last resort): `probesize=5MB`, `analyzeduration=5s`, `rw_timeout=30s`
- Tier 3 failure = actual probe failure; surface as `streamError` with tier-specific message

New sidecar telemetry event: `probe_tier_passed { tier: 1|2|3, elapsed_ms, probesize, analyzeduration }`.

### Batch 4.2 — Engine gate reduction

`kGateBytes` in `StreamEngine.h:293` drops 5MB → 1MB to match sidecar Tier 1 probe size. If sidecar needs Tier 2/3, engine gate can grow dynamically (or stay at 1MB and let sidecar's Tier 2 probe wait for more pieces — design call during implementation).

**Files:** `native_sidecar/src/video_decoder.cpp`, `native_sidecar/src/demuxer.cpp`, `src/core/stream/StreamEngine.h` (gate constant only)

**Exit criteria:**
- Cold-start 1000-seed torrent: first frame visible < 6s p50 **(requires P2 landed — Congress 5 Amendment 3; P4 code can commit before P2 but acceptance-smoke waits on StreamPieceWaiter::await replacing the 15s poll-floor)**
- `probe_tier_passed` event shows Tier 1 success on fast swarms (majority case)
- Tier 2/3 escalation verified on slow swarm (manually throttled)
- Tag `stream-rebuild/phase-4-probe-escalation`

**Revertible:** YES — revert sidecar file pair + gate constant.

---

## Phase 5 — Stall detection + recovery (Agent 4)

**Purpose:** when StreamPieceWaiter waits > 4s on in-window piece, re-prioritize + telemetry-log + emit user-visible `bufferUpdate` text. Handles the peer-starvation case P3 can't solve with priority alone.

### Batch 5.1 — StreamSession stall watchdog

Timer ticks every 2s during active playback. If `StreamPieceWaiter::longestActiveWaitMs()` > 4000 for an in-window piece:
- Re-assert priority 7 on the stuck piece (one more retry beyond P3's escalation)
- Emit new telemetry event `stall_detected { hash, piece_idx, wait_ms, peer_have_count }`
- Emit `bufferUpdate("Reconnecting peers…", percent)` to UI

### Batch 5.2 — Stall-recovery semantics

When stall clears (piece arrives OR user cancels OR source-switch):
- Emit `stall_recovered { elapsed_ms, via: piece_arrival|cancelled|replacement }` telemetry event
- Clear `bufferUpdate` UI text back to normal progress display

**Files:** `src/core/stream/StreamSession.{h,cpp}`, `src/core/stream/StreamPieceWaiter.{h,cpp}` (add `longestActiveWaitMs()` accessor)

**Exit criteria:**
- Manual bandwidth choke mid-playback: buffer overlay reappears within 4s, clears within 6s of unchoke
- `stall_detected` + `stall_recovered` event pair observed
- No false-positive stalls on healthy streams
- Tag `stream-rebuild/phase-5-stall-recovery`

**Revertible:** YES — wrap stall watchdog in enable flag `STREAM_STALL_WATCHDOG=0`.

---

## Phase 6 — Demolish old code paths (Agent 4 + Agent 0)

**Purpose:** terminal commit. Remove dead branches kept during P1-P5 for rollback safety.

### Batch 6.1 — Dead-code removal

Remove:
- `applyStreamPriorities` (old private method in StreamEngine.cpp — replaced by Prioritizer)
- Old `waitForPieces` poll-sleep loop (already surgically replaced in P2, but code path may remain behind env flag — delete the flag + fallback)
- `StreamRecord` flag-state fields that Session FSM supersedes (metadataReady bool, registered bool — Session state enum replaces)
- `[[deprecated]]` shims that accumulated during earlier phases
- `STREAM_PIECE_WAITER_POLL` env fallback (P2 rollback safety — no longer needed)
- `STREAM_STALL_WATCHDOG` env fallback (P5 rollback safety — no longer needed)

### Batch 6.2 — Soak gate (4 hours)

Before P6 commit lands:
- Overnight wall-clock playback of **multi-file TV pack** (Sopranos S06E09 → E10 → E11 natural rollover, NOT single-file — per Congress 5 Amendment 5 to exercise `stopStream(Replacement)` → `startStream(...)` lifecycle 2-3 times naturally)
- Soak runs with `TANKOBAN_STREAM_TELEMETRY=1`; post-soak grep for anomalies: `stall_detected` without matching `stall_recovered`, `seek_target` with `peer_have=0`, `gateProgressBytes` monotonicity violations, unexpected `streamError` emits on paths T0 completed cleanly
- No crashes, no new `streamError`, no telemetry anomalies
- All 6 MVP criteria pass
- All 12 runtime scenario matrix cells pass

### Batch 6.3 — TODO closure + dashboard cleanup

- Close this TODO — mark "CLOSED 2026-04-XX" in CLAUDE.md dashboard row
- Close superseded TODOs: `STREAM_ENGINE_FIX_TODO`, `STREAM_PLAYER_DIAGNOSTIC_FIX_TODO`, `STREAM_UX_PARITY_TODO`, `STREAM_PARITY_TODO`, `STREAM_PLAYBACK_FIX_TODO` (all superseded by this rebuild — Agent 0 marks SUPERSEDED with pointer to this TODO's closing commit)
- Remove `TANKOBAN_STREAM_TELEMETRY` / `TANKOBAN_ALERT_TRACE` env-var gated diagnostic code if no longer needed (Agent 4 + 4B judgment)

**Files:** `src/core/stream/StreamEngine.{h,cpp}` (dead-code removal), `src/core/stream/StreamHttpServer.cpp` (flag removal), `CLAUDE.md`, `MEMORY.md`

**Exit criteria:**
- Green build with all dead paths removed
- 4-hour soak test passes
- All 6 MVP + 12 scenario cells pass
- Tag `stream-rebuild/phase-6-complete`

**Revertible:** **NO** — this is the point-of-no-return terminal commit.

---

## Scope decisions locked in

- **File split** — 5 files (facade + Session + Prioritizer + PieceWaiter + SeekClassifier) vs current monolith. Rationale: current 1269-LOC has priority/gate/FSM/telemetry all interleaved; Stremio's equivalent is separated. Per-hash `StreamSession` corresponds to Stremio's `StreamGuard` Drop semantics — session destructor = natural `on_stream_end` hook.
- **Public API frozen** (Congress 5 Amendment 2 corrections). StreamEngine = 17 methods + 2 signals + **3** structs (StreamFileResult, StreamTorrentStatus, StreamEngineStats — `StreamRecord` is private-impl) + 1 enum. StreamPlayerController = ctor + **5** methods (incl. `pollBufferedRangesOnce` shipped at `c510a3c`) + 5 signals + StopReason enum. Any attempt to change a signature during the rebuild window requires Agent 4 → Congress pushback, not unilateral change.
- **Probe escalation tiers fixed at 512KB/750ms → 2MB/2s → 5MB/5s.** `rw_timeout` escalates 5s → 15s → 30s. Tier 1 faster than current best-case (which stalls); Tier 3 slower than nothing (current 30s single-shot IS the problem).
- **Seek-type escalation capped at 3 retries.** After that, falls to stall-recovery (P5 watchdog). Prevents infinite priority-7 storm on genuinely unavailable pieces.
- **Per-peer `have_piece` telemetry is a Risk R3 falsification gate.** Must land in P3 (or with fallback heuristic) before declaring Mode B fixed.
- **4-hour soak is the P6 gate.** Wall-clock playback, no fast-forward. Per Risk R7 (hours-of-playback latent bugs invisible to 2-min smoke). Multi-file TV pack + TELEMETRY=1 + post-soak anomaly grep per Congress 5 Amendment 5.

## Post-Congress-5-ratification corrections (2026-04-18)

- **R5 (piece prioritization inaccessibility in enginefs) — FALSIFIED.** Post-synthesis verification confirmed `stream-server-master/enginefs/src/backend/priorities.rs` + `piece_waiter.rs` + `backend/libtorrent/{handle,stream,constants,helpers}.rs` all present in the reference folder. Phase 1 explore flagged this as "possibly inaccessible" without verifying. R5 removed from active risk register. Integration pass no longer needs fallback plan for unreachable prioritizer source.
- **R1 + R11 (Stremio = WebTorrent / libtorrent-vs-WebTorrent mismatch) — REFRAMED.** Post-synthesis verification of `stream-server-master/Cargo.toml` + `enginefs/Cargo.toml` confirmed: Stremio streaming-server depends on **`libtorrent-sys` (FFI bindings to libtorrent-rasterbar via cxx)** as its default feature, with `librqbit` as optional alternative. Stremio IS libtorrent-based — same library family as our C++ libtorrent 2.0 stack. The "re-derive libtorrent calls from first principles" R1 mitigation collapses into direct port of Stremio's libtorrent-sys wrappings to our C++ calls. R11 removed; R1 downgraded from High to Medium (differences in wrapping layer semantics between Rust cxx and our direct C++ calls remain, but the library itself is identical).
- **R12 (atomic Session migration at P3) — ADDED** (Composite 12) per Agent 4's Congress 5 position. Half-state corrupts under concurrent source-switch. Mitigation: migration lands in one P3 commit; no interleaving with P2 piece-waiter work; shared-file heads-up in chat.md at each `TorrentEngine.{h,cpp}` touch window across P2/P3.
- **R13 (plan-text amendment required) — ADDED** (Composite 10) per Agent 4's Congress 5 position. Mitigation: this TODO commit is the amendment trigger.

## External references worth open during implementation (supplementary)

Beyond the Stremio Reference folder (sufficient on its own), these external docs aid P2-P4 implementation:

- [libtorrent streaming.html](http://libtorrent.org/streaming.html) — canonical API reference for `set_piece_deadline` + `request_time_critical_pieces` algorithm
- [libtorrent streaming.rst on GitHub](https://github.com/arvidn/libtorrent/blob/master/docs/streaming.rst) — same doc, source of truth
- [libtorrent issue #84 — set_piece_deadline not honored](https://github.com/arvidn/libtorrent/issues/84) — documented cases of Mode-B-like behavior + maintainer resolutions
- [libtorrent discussion #6272 — set_piece_deadline vs sequential_download](https://github.com/arvidn/libtorrent/discussions/6272) — semantic distinctions P3 needs
- [peerflix source](https://github.com/mafintosh/peerflix) — Node.js streaming reference for HTTP-Range sanity-check

**Strategic insight from libtorrent docs (P3 design note for Agent 4):** `request_time_critical_pieces()` runs every 1s and has built-in timeout-and-retry: "if any time-critical piece takes more than the average piece download time plus a half average deviation, the piece is considered to have timed out, allowing double-request of blocks." This only works if the piece has a LIVE deadline when the next pass runs. Phase 2.6.3 set deadline ONCE — if it expired, libtorrent dropped the piece from time-critical tracking. **Alternative to P3's 500ms tick re-assertion:** call `set_piece_deadline` with a **longer** deadline (10-30s) so libtorrent's own retry runs several cycles naturally. Agent 4's call in P3 design — evaluate both.

---

## Isolate-commit candidates

- Batch 0.1 (schema_version field) — pure additive JSON field
- Batch 2.1 (TorrentEngine `pieceFinished` emit) — single-line emit addition, Agent 4B owns
- Batch 4.1 (sidecar probe escalation) — Agent 3 domain, no IPC impact
- Batch 5.1 (stall watchdog timer) — behind env flag for rollback
- Each phase's tag commit (reversible boundary)

---

## Existing functions / utilities to reuse (not rebuild)

- **`StreamHttpServer`** — RFC 9110 Range parser at `StreamHttpServer.cpp:43` + 206 emit at `:242`. Preserved verbatim except P2 surgical waitForPieces swap.
- **`StreamProgress` helpers** — all 8 inline functions preserved + `schema_version` added.
- **`TorrentEngine` piece primitives** — `setPieceDeadlines`, `setPiecePriority`, `contiguousBytesFromOffset`, `fileByteRangesOfHavePieces`, `pieceRangeForFileOffset`, `havePiece`, `haveContiguousBytes`, `setFilePriorities`, `torrentFiles`, `addMagnet`, `setSequentialDownload`, `removeTorrent`.
- **`contiguousHaveRanges`** — freshly shipped in `973ff32`; feeds PLAYER_STREMIO_PARITY Phase 1's SeekSlider gray-bar. Preserved verbatim.
- **`StreamEngineStats`** + `StreamTorrentStatus` + `StreamFileResult` structs — field shapes preserved.
- **`TANKOBAN_STREAM_TELEMETRY=1` + `stream_telemetry.log`** — grep-friendly ISO8601-kv-pair format. Preserved; new events append (never rename).
- **`MetaAggregator`, `StreamAggregator`, `AddonRegistry`, `AddonTransport`, `MetaItem`, `CalendarEngine`** — source-fetching path untouched.
- **`CoreBridge::saveProgress`** + QSettings storage — progress persistence path unchanged.

---

## Review gates

**Review protocol is SUSPENDED** (Agent 6 decommissioned 2026-04-16). Phase-exit approval is Hemanth smoke directly.

- **P0:** Hemanth verifies Continue Watching loads correctly post-schema_version addition.
- **P1:** Hemanth smokes Sopranos S06E09 stream; behavior exactly as T0.
- **P2:** Hemanth smokes cold-start high-seed torrent; first byte < 2s after metadata ready.
- **P3:** Hemanth smokes mid-file scrub + 50× stop-start loop; no regression.
- **P4:** Hemanth smokes cold-start healthy swarm; first frame < 6s.
- **P5:** Hemanth manually chokes bandwidth; verifies stall UI + recovery.
- **P6:** Hemanth runs overnight soak; all 6 MVP + 12 runtime cells pass.

Rule 11 mandatory on every batch: `READY TO COMMIT` line → Agent 0 sweep.

---

## Open design questions (domain masters decide per Rule 14)

1. **Agent 4 (P3.2):** UserScrub escalation 500ms → 300ms → 200ms — fixed cadence or adaptive to observed download rate? Default fixed; override if adaptive shows empirical win.
2. **Agent 4 (P4.2):** Gate drop 5MB → 1MB — static or dynamic with Tier escalation? Default static 1MB; dynamic if Tier 2 probes exhibit starvation.
3. **Agent 4B (P3.3):** `peersWithPiece` — expose or propose fallback heuristic? HELP.md request active.
4. **Agent 3 (P4.1):** Three-tier probe handler organization — one function with tier parameter or three separate attempt functions? Agent 3's judgment call.
5. **Agent 4 (P6.1):** Remove `TANKOBAN_STREAM_TELEMETRY` env-gated diagnostic code post-P6 or preserve as permanent observability? Default preserve (costs nothing when off); delete only if it interferes with normal code paths.

---

## What NOT to include (explicit deferrals beyond Non-Goals)

- **PLAYER_STREMIO_PARITY Phases 2-8** — continues in parallel TODO. Not this one.
- **Agent 4B tracker pool curation (STREAM_ENGINE_FIX Phase 3.1)** already shipped in `973ff32`. Not duplicated here.
- **Addon protocol subscription API shape** — future scope if Stremio addon ingestion is ever reconsidered.
- **HLS ABR / adaptive bitrate switching.** Not in scope.
- **Chromecast / DLNA / TV broadcast.** Not in scope forever.
- **Per-stream bandwidth caps + QoS.** Future polish TODO.
- **Pre-cache "next N seconds ahead" policy.** Risk R3 fallback only; ships only if per-peer `have_piece` telemetry shows scheduler is innocent + we still want aggressive prefetch.

---

## Rule 6 + Rule 11 application

**Rule 6:** Agent 4 runs `build_and_run.bat` (main app build — honor-system per contracts-v2) AND smokes the feature before declaring any batch done. Agent 3 runs `native_sidecar/build.ps1` for sidecar-only batches (agent-runnable per contracts-v2).

**Rule 11:** Every batch that verifies (compiles + feature works) gets a `READY TO COMMIT — [Agent N, REBUILD Phase X.Y]: <msg> | files: a, b, c` line in chat.md. Agent 0 sweeps. No git from agents.

**Rule 10 shared-file coordination:** `TorrentEngine.{h,cpp}` touches (P2, optional P3) cross Agent 4B domain; coordinate via HELP.md (active this session) + chat.md heads-up. `StreamHttpServer.cpp` touches (P2) are in Agent 4 domain but declared non-goal for major rewrite — surgical only.

**Rule 13:** Agent 0 maintains CLAUDE.md dashboard at every phase-boundary commit. This TODO row gets a phase-cursor update on every P0→P6 exit.

---

## Verification procedure (end-to-end once all phases ship)

### MVP smokes:
1. Cold-start Big Buck Bunny 1080p (high seed): first frame < 6s p50 → **MVP criterion 3 ✓**
2. Stream Sopranos S06E09 to 50%, scrub to 80%, resume < 3s → **MVP criterion 2 ✓**
3. MKV file with embedded sub track: toggle sub, verify render → **MVP criterion 4 ✓**
4. Watch 5 min, Esc, reopen, resume within ±2s → **MVP criterion 5 ✓**
5. Source-switch mid-stream + stop→start loop + app-restart→resume → **MVP criterion 6 ✓**
6. Agent 3 verifies sidecar aspect behavior unchanged (criterion 1 is sidecar domain, not regression from this rebuild) → **MVP criterion 1 ✓**

### 12-cell runtime scenario matrix:
All must pass before P6 exit: single-file movie, multi-file TV pack, high-seed (1000+), low-seed (< 10), seek unbuffered, seek buffered, source-switch mid-stream, stop + restart same stream, embedded subtitle, external subtitle, progress across app restart, progress across episode switches.

### Objective rollback criteria (worse-vs-different from plan):
Rebuild is **worse** (rollback required) if any regress vs T0 baseline:
1. Head gate open time > 50% regression (T0: ~5-15s)
2. `seek_target ready=1` time > 3s median on healthy BW (T0 target: 1.5s)
3. Post-gate peer crash > 30% within 30s on multi-file pack
4. Any new crash in 2-hour soak that T0 didn't produce
5. Progress-schema key drift losing any Continue Watching entry
6. `streamError` emitted in flow T0 completed cleanly

---

## Next steps post-approval

1. **Congress 5 ratifies motion** (positions from Agents 5, 3, 4B, 4 in that order; Agent 0 synthesis; Hemanth final word).
2. **Agent 4B responds to HELP.md** — commits to `pieceFinished` signal + API freeze + feasibility on optional `peersWithPiece`.
3. **Agent 0 ships P0.1** (schema_version field + T0 tag). Single commit. Green build.
4. **Agent 4 executes P1 scaffold** when Agents 4B + 3 signal readiness.
5. **P2 ∥ P4 parallel** — Agent 4B's `pieceFinished` signal + Agent 3's probe escalation are independent; ship together or separately.
6. **P3 follows P2** (needs piece-waiter infrastructure for escalation math).
7. **P5 follows P3** (builds on Prioritizer's observability).
8. **P6 follows P5** + 4-hour soak gate. Terminal commit.

**Superseded TODOs on P6 closure:**
- `STREAM_ENGINE_FIX_TODO.md` (Slice A audit work — now legacy)
- `STREAM_PLAYER_DIAGNOSTIC_FIX_TODO.md` Phase 3 carry-forward (subtitle variant grouping — if not yet closed, re-author under this rebuild's Phase 5 or a follow-up TODO)
- `STREAM_UX_PARITY_TODO.md` (remaining batches)
- `STREAM_PARITY_TODO.md` (closed but dashboard stub)
- `STREAM_PLAYBACK_FIX_TODO.md` (closed but dashboard stub)
