# Stream Player Diagnostic Fix TODO — Protocol event enrichment + post-probe/pre-first-frame state + first-frame watchdog + subtitle variant grouping (Slice D of 6)

**Owner:** Agent 4 (Stream mode) as TODO owner + validation-source. **Phase 1 + Phase 2 + Phase 3 primary execution: Agent 3** (Video Player — sidecar + VideoPlayer + LoadingOverlay + SubtitleMenu are their domain). Agent 4 consumer-side integration in Batch 1.3 (`StreamPlayerController` + stream-mode-specific event paths) after Phase 1.2 lands. Agent 0 coordinates phase gates and commit sweeps. Agent 6 review gates retained in template but dormant per 2026-04-16 decommission — Hemanth approves phase exits directly via smoke. Normal TODO dispatch (no HELP ACK ceremony — work is in Agent 3's domain, Agent 4 consumes signals rather than mutating Agent 3's surface).

**Created:** 2026-04-17 by Agent 0 after Agent 7's Slice D audit (`agents/audits/stream_d_player_2026-04-17.md`, 450 lines, committed `9d24161`) + Agent 4's comprehensive validation pass (chat.md:1929-2043 — "Validation pass on Agent 7's Slice D audit"). Second fix TODO in the 6-slice stream-mode comparative audit programme (Slice A fix TODO `STREAM_ENGINE_FIX_TODO.md` already in execution — Phases 1+2.2+2.4+2.5+2.6.3+1.3 SHIPPED, Phases 2.3+3 pending Agent 4B HELP ACK, Phase 4 ready).

## Context

Stream mode's player surface (`src/ui/player/*` + `native_sidecar/src/*` stream code paths) has the widget infrastructure (LoadingOverlay, TrackPopover, SubtitleMenu, HUD) needed for Stremio-parity UX but lacks **classified diagnostic signal during the critical open → first-frame interval**. Hemanth's lived symptoms — *"never loaded brother"*, *"took forever to load"*, *"frozen frame while time advances"* — are not missing-widget defects; they are **absence-of-classification** defects. The LoadingOverlay shows a generic "Loading filename" pill for the full 10-70 second silent window between `state_changed{opening}` and `first_frame`, with no visibility into whether the sidecar is probing, reopening for decoder, reading first packet, or hung.

Agent 7's Slice D audit (12 comparison axes + 16 findings + 22-item validation checklist, 450 lines) mapped the gap against Stremio's player stack (`stremio-web-development/src/routes/Player/`, `stremio-video-master/stremio-video-master/src/HTMLVideo/` + `withHTMLSubtitles/`, `stremio-core-development/src/models/player.rs`). Agent 4's validation pass (chat.md:1929-2043) confirmed 13 findings, marked 3 NEEDS-EMPIRICAL, zero REFUTED, and identified the precise mechanism behind D-13 "frozen frame while time advances" — `av_sync_clock.cpp:88-97 position_us()` is pure wall-clock interpolation from `anchor_time_ + rate_`, does NOT gate on frame delivery, ticks at wall-clock pace regardless of whether decoder is delivering frames / SHM is being written / FrameCanvas is consuming.

The Hemanth-lived anchor for the P0 diagnostic blind spot already exists in earlier-session log evidence: **sidecar reached `tracks_changed` at 10.7s post-open, then went silent 67+s with no `first_frame`, no `buffering`, no `decode_error`.** That is the D-11/D-12 scenario in the wild; no re-repro needed to validate the audit's framing. Agent 4 confirms no runtime data gap for authoring.

Strategy rationale (Agent 4 endorses Agent 0's A+C-composable bias, chat.md:1937-1946):
- **Option A (diagnostic classification) — this TODO's Phase 1 + 2.** Direct hit on the two Hemanth-lived P0s (D-11 "never loaded brother" + D-12 10-second probe window). Event-protocol enrichment + LoadingOverlay text specialization + first-frame watchdog. Not UI polish re-opening.
- **Option C (subtitle variant grouping) — this TODO's Phase 3.** Properly isolated from decode/open work; can ship in parallel or sequenced without coupling risk. Addresses D-06 cleanly, no geometry ramifications (D-07 stays closed under Option A rollback posture).
- **Option B (buffered range in seekbar) — deferred.** Per D-03 cross-reference bucket (Depends on Slice A Phase 2.3/3/4 pending), substrate-side signal doesn't exist yet. `StreamEngineStats` exposes `gateProgressBytes` + `prioritizedPieceRange{First,Last}` but not a seekable-range byte map. Rendering Stremio-like `TimeRanges` without that substrate is a pretend-signal; wait for Phase 2.3 + 3 Slice A closure.
- **Option D (HLS/adaptive) — architectural non-goal.** STREAM_ENGINE_FIX Phase 4.2 codifies this (pending execution). No action here.

Ordering within A+C: **A first (Phase 1 + 2), C second (Phase 3).** A closes P0 diagnostic blind spots; C is P2 polish. If capacity tight, A alone is a complete fix — C can hold a slot later without losing anything (Agent 4's chat.md:1946 call, accepted).

**Audit cross-reference discipline:** Slice D findings explicitly cross-referenced against closed prior TODOs — PLAYER_UX_FIX (D-01, D-02, D-05), PLAYER_LIFECYCLE_FIX (D-08), cinemascope once-only-exception (D-07, D-14), STREAM_LIFECYCLE_FIX (D-04), PLAYER_PERF_FIX (D-16). Slice A cross-references — D-03 (pending Phase 2.3+3), D-15 (closed substrate). Agent 4 validation's D-16 bucket refinement noted: `[PERF]` log line is Phase 1.1 + Phase 3 Option B both, not Phase 3 alone — cosmetic refinement, doesn't change scope.

**Scope:** 3 phases, ~6-8 batches per Agent 4's proposal (chat.md:2009-2015). Phase 1 = protocol event enrichment (2-3 batches, sidecar + Qt-side plumbing). Phase 2 = classified LoadingOverlay state + 30s first-frame watchdog (2 batches). Phase 3 = subtitle variant grouping (2-3 batches).

## Objective

After this plan ships, Stream mode player behaves correctly under every diagnostic scenario Slice D identified:

1. **Classified open → first-frame visibility.** Sidecar emits `probe_start` / `probe_done` (with `analyze_duration_ms` + `probesize_bytes_read`) / `decoder_open_start` / `decoder_open_done` / `first_packet_read` / `first_decoder_receive` events with wall-clock delta from `open_start`. VideoPlayer + StreamPlayerController consume. Agent-readable via structured log (mirrors `stream_telemetry.log` convention); future audits / debugging have the signal for root-cause identification.
2. **No more 10-70s silent windows.** The LoadingOverlay shows classified progress text ("Probing source…" → "Opening decoder…" → "Decoding first frame…") driven by Phase 1 events, not a generic "Loading filename" pill for the entire opaque interval. User sees where the player actually is in the open pipeline.
3. **First-frame watchdog catches wedge states.** 30-second watchdog from `open_start`: if no `first_frame` within 30s, LoadingOverlay flips to "Taking longer than expected — close to retry" and sidecar emits `first_frame_timeout` diagnostic event. Matches existing sidecar `STREAM_TIMEOUT:no data for 30 seconds` cadence for internal consistency. No more indefinite user-visible wait on genuinely stuck opens.
4. **Frozen-frame-while-time-advances correlation surfaceable.** Sidecar `[PERF]` line extended with frame-advance counter cross-referenced against `positionSec`-delta per 1s window. Log self-surfaces any disagreement between "time advancing" and "frame delivered" — diagnostic signal for D-13 without user-facing state change this phase (user-facing recovery is future scope).
5. **Subtitle variant grouping parity with Stremio.** `SubtitleMenu` re-layout: language-first header rows + variant sub-items per Stremio shape, origin-priority sort (embedded → addon → local), active-variant highlight. Preserves existing delay/offset/size controls. Multi-variant same-language sources are discoverable without horizontal flat-list scanning.
6. **STREAM_UX_PARITY Batch 2.6 unlocks as additive polish.** Slice D D-04 confirms lifecycle/plumbing closed under STREAM_LIFECYCLE Phase 4; visible next-video affordance becomes pure key-binding + control-bar-button + menu-entry polish. Can land in parallel with Phase 1+2 execution of this TODO.

## Non-Goals (explicitly out of scope for this plan)

Derived from Agent 4 validation § "Strategy pick" (chat.md:1937-1946) + Slice D audit cross-reference buckets. Slice D player-only. Phase 4.2 of STREAM_ENGINE_FIX_TODO (pending execution) codifies architectural non-goals at substrate level; this TODO does not re-codify them.

- **Buffered range visibility in seekbar** (D-03, Option B). Deferred — re-open when Slice A Phase 2.3 event-driven waiter + statsSnapshot expansion land. Substrate doesn't expose byte-contiguous have-bitmap suitable for `TimeRanges` today; renderable substrate is the prerequisite. Not in scope of this TODO.
- **HLS / adaptive bitrate parity** (D-15, Option D). Architectural non-goal. Sidecar demuxes everything FFmpeg supports; HLS.js-equivalent ABR isn't our direction. No player-side action. Codified at substrate level via STREAM_ENGINE_FIX Phase 4.2 (pending).
- **Press-and-hold 2x speed gesture** (D-10). Hemanth UX call on scope. Default defer — not in scope unless explicitly greenlit.
- **Cinemascope/subtitle geometry runtime revalidation** (D-14 NEEDS-EMPIRICAL). No code batch unless a new defect surfaces in `_player_debug.txt` aspect-diag log. Validation-via-existing-diagnostics only, no new widget / geometry work.
- **Sidecar decoder reuse of probe-open's `AVFormatContext`** (Agent 4 validation chat.md:1981 refinement). Plausible Phase 2 optimization once Phase 1 instrumentation ranks decoder-reuse vs probe-shrink empirically. Not this TODO — queued for conditional future phase or separate TODO per empirical findings.
- **User-facing frozen-frame recovery state** (D-13 UX surface beyond diagnostic log). This TODO adds the `[PERF]` frame-advance correlation signal but does NOT classify "frozen frame" into a user-facing error state. Future polish once Hemanth has the log-evidence baseline.
- **Control-bar next-video visible button** (D-04 visible-affordance polish). Routed to STREAM_UX_PARITY Batch 2.6 (Agent 4 domain, separate TODO). This TODO only confirms Slice D unlocks that work — doesn't execute it.
- **Subtitle delay / offset / size persistence refactor** (validation checklist items 13-14). Existing controls preserved as-is per Phase 3. No persistence model changes; no cross-addon/embedded flow changes beyond what variant-grouping naturally clarifies.
- **`native_sidecar/src/*` beyond the 4 files flagged by Agent 4 scope-expansion** (main, demuxer, video_decoder, subtitle_renderer, av_sync_clock, state_machine). `overlay_shm.cpp`, `overlay_renderer.cpp`, `protocol.cpp`, `heartbeat.cpp` are out-of-scope for Phase 1/2 unless Phase 1 instrumentation work unavoidably crosses them — flag and scope-creep check if so.

## Agent Ownership

**TODO owner:** Agent 4 (Stream mode domain master + validation source). Agent 4 owns fix-direction authority, validates phase exits against Slice D audit evidence, picks technical strategy per Rule 14.

**Phase 1 + Phase 2 + Phase 3 execution primary:** Agent 3 (Video Player). Sidecar event emission + `SidecarProcess` plumbing + `VideoPlayer` + `LoadingOverlay` + `SubtitleMenu` are all Agent 3's domain. Agent 3 picks the sidecar event emit-site details + Qt-side consumption shape + LoadingOverlay `setStage` API + watchdog placement + SubtitleMenu UI primitive during implementation (see § "Open design questions" for the 8 technical calls Agent 3 makes). `SubtitleMenu.cpp` is in-player UI (Agent 3's domain, not Agent 5's library-UX domain per `feedback_agent5_scope`) — Phase 3 stays Agent 3 unless they explicitly flag capacity issue at Phase 3 entry.

**Phase 1.3 + Phase 2 consumer-side:** Agent 4. `StreamPlayerController` consumes Phase 1.2's new `VideoPlayer` signals + correlates with `StreamEngineStats` (Slice A Phase 1 artifact) for stream-mode-specific diagnostic cohesion. This is read-only consumption of Agent 3's signal surface — no mutation of Agent 3's domain. Agent 4 picks up Batch 1.3 after Phase 1.2 lands.

**Dispatch model:** Normal TODO dispatch (no HELP ACK ceremony). Work is in Agent 3's domain; Agent 4 consumes signals rather than mutating Agent 3's surface. The HELP ACK pattern is reserved for cross-domain *API-surface mutations* (Agent 4B's TorrentEngine case in STREAM_ENGINE_FIX) — not applicable here. Agent 3 acknowledges the dispatch via normal STATUS bump + first chat post, then executes per Rule 6 + Rule 11.

**Primary files in scope:**
- **Sidecar:** `native_sidecar/src/main.cpp`, `native_sidecar/src/demuxer.cpp`, `native_sidecar/src/video_decoder.cpp`, `native_sidecar/src/state_machine.{h,cpp}` (Phase 1 + 2)
- **Qt consumer:** `src/ui/player/SidecarProcess.{h,cpp}` (Phase 1), `src/ui/player/VideoPlayer.{h,cpp}` (Phase 1 + 2), `src/ui/player/LoadingOverlay.{h,cpp}` (Phase 2), `src/ui/player/SubtitleMenu.{h,cpp}` (Phase 3)
- **Stream-mode consumer:** `src/ui/pages/stream/StreamPlayerController.{h,cpp}` (Phase 1 + 2 stream-specific integration only)

**No CMakeLists.txt touches in this plan** (no new files introduced). Rule 7 does not apply.

**Interactions with other in-flight work:**
- STREAM_ENGINE_FIX Phase 1 (telemetry + statsSnapshot) is SHIPPED; this TODO's Phase 1 structured log convention mirrors `stream_telemetry.log` shape for cross-diagnosis consistency.
- STREAM_ENGINE_FIX Phase 2.3 (event-driven piece waiter) + Phase 3 (tracker pool) still pending Agent 4B HELP ACK. D-03 buffered-range UI (deferred in this TODO) re-opens when those land.
- STREAM_ENGINE_FIX Phase 4 (diagnostics polish + non-goals codification) ready; Slice D D-15 corroborates HLS non-goal for Phase 4.2 comment block.
- STREAM_UX_PARITY Batch 2.6 (Shift+N visible affordance) unlocked by Slice D D-04; can ship parallel with Phase 1+2 of this TODO (separate Agent 4 track, not bundled here).
- PLAYER_UX_FIX closed; LoadingOverlay is its artifact. Phase 2 of this TODO extends LoadingOverlay (setStage or equivalent) — consult `PLAYER_UX_FIX_TODO.md` Phase 2 Batch 2.3 for the widget's current contract before extending.
- PLAYER_PERF_FIX closed; `[PERF]` log at `video_decoder.cpp:969-978` is the target for Phase 1 D-13 frame-advance-counter extension.

## Phase 1 — Protocol event enrichment (P0 — closes D-11 + D-12 diagnostic blind spots)

**Why:** Agent 4 validation chat.md:1970-1981 identified the exact event-protocol gap. Between sidecar `tracks_changed` and `first_frame` (often 10-70+ seconds), the only events that can fire are `decode_error` (continues) or `error`/`eof` (terminal). No probe-milestone events, no decoder-open-complete event, no packet-read-progress event, no "waiting on first packet" tick. Hemanth's 10.7s-to-`tracks_changed`-then-67s-of-silence trace is exactly this gap. Phase 1 closes it at the protocol level before Phase 2 surfaces anything in the UI.

Agent 4 also identified (chat.md:1981): significant win may come from sidecar teaching decoder to reuse probe-open's `AVFormatContext` rather than re-probing — but that's a sidecar refactor, Agent 3's surface. Phase 1 instrumentation lands first and lets empirical data rank "decoder reuse vs probe shrink" before committing to either optimization.

**Phase 1 is Agent 3's domain work (sidecar + SidecarProcess + VideoPlayer).** Normal dispatch — no HELP ACK ceremony.

### Batch 1.1 — Sidecar event emission (primary batch)

- NEW session-scoped events emitted by `native_sidecar/src/main.cpp` around the open pipeline:
  - `probe_start` — emitted immediately before `probe_file()` call (around `main.cpp:705` `handle_open` region). Payload: session id + url hint + wall-clock delta from `open_start`.
  - `probe_done` — emitted after `probe_file()` returns successfully. Payload: `analyze_duration_ms` (actual time spent in `avformat_find_stream_info`), `probesize_bytes_read` (from `AVIOContext` stats if available, else `probe_file()` return metadata), stream-count, duration_ms if known, wall-clock delta.
  - `decoder_open_start` — emitted immediately before `VideoDecoder::open_input()` (or equivalent decoder init site around `main.cpp:373-388` post-probe hoist region). Payload: session id + wall-clock delta.
  - `decoder_open_done` — emitted after decoder initialization returns. Payload: codec_name, pixel_format, width, height, wall-clock delta.
  - `first_packet_read` — emitted on first successful `av_read_frame` at the decoder's read loop (`video_decoder.cpp:1076+` region or equivalent). Payload: pts_ms, stream_index, packet_size, wall-clock delta.
  - `first_decoder_receive` — emitted on first successful `avcodec_receive_frame`. Payload: pts_ms, decode_latency_ms, wall-clock delta.
- All events carry `sid` + wall-clock delta from `open_start` (Agent 4 validation chat.md:1979 explicit — events must be gated by session id to work with existing PLAYER_LIFECYCLE_FIX Phase 1 session-id-filter pattern).
- Wall-clock source: sidecar has an existing `AVSyncClock` instance but that starts at playback; Phase 1 needs a separate monotonic stopwatch anchored at `handle_open` entry. Agent 3 picks the clock source during implementation (`std::chrono::steady_clock`-based is my proposal; their call).
- Event-emission format: mirrors existing `write_event()` convention used by current events (`state_changed`, `tracks_changed`, `media_info`, `buffering`, etc.). Agent 3 picks exact key-value shape.
- `first_frame` event (already exists, `main.cpp:464` region per Agent 4 chat.md:1966) gains `wall_clock_delta_from_open_ms` field for consistency — backward compatible additive field, existing consumers ignore.

**Files:** [native_sidecar/src/main.cpp](native_sidecar/src/main.cpp), [native_sidecar/src/demuxer.cpp](native_sidecar/src/demuxer.cpp) (if emit sites require `probe_file` internal changes), [native_sidecar/src/video_decoder.cpp](native_sidecar/src/video_decoder.cpp) (for `first_packet_read` + `first_decoder_receive` + reuse of existing `[PERF]` log infrastructure).

**Success:** Building sidecar succeeds (Rule 7 sidecar build self-service per contracts-v2). Running sidecar emits the 6 new events in the correct order during an open; stderr log shows grep-friendly lines for each. Zero regressions on existing events. Agent 3 runs sidecar-only smoke (no Qt-side needed yet — structured log is self-contained for this batch).

**Isolate-commit:** yes. Protocol additions to sidecar; isolate so Batch 1.2 Qt-side plumbing lands against a known-stable event surface.

### Batch 1.2 — Qt-side event parsing + signal surface

- `SidecarProcess::processLine` ([src/ui/player/SidecarProcess.cpp:397+](src/ui/player/SidecarProcess.cpp#L397)) extended to parse the 6 new event types + the extended `first_frame` field. Session-id filter (PLAYER_LIFECYCLE_FIX Phase 1 pattern at `:410-434`) applies to all new events — stale-session events dropped.
- NEW Qt signals on `VideoPlayer`: `probeStarted(QString sidecarUrl)`, `probeDone(qint64 analyzeDurationMs, qint64 bytesRead)`, `decoderOpenStarted()`, `decoderOpenDone(QString codec, int width, int height)`, `firstPacketRead(qint64 ptsMs, int streamIndex)`, `firstDecoderReceive(qint64 ptsMs, qint64 decodeLatencyMs)`. Wall-clock-delta ridealong on each.
- Debug log integration via existing `debugLog()` (matches current `[VideoPlayer] state=opening file=...` convention, PLAYER_UX_FIX Phase 1 pattern). Each new signal produces a log line with delta_ms + payload.
- Optional: structured log file `player_events.log` in application-working-directory (mirrors `stream_telemetry.log` conventions — env-var-gated by `TANKOBAN_PLAYER_EVENTS=1` for symmetry with STREAM_ENGINE_FIX Phase 1.2, short-circuits cheaply when unset). Agent 3 + Agent 4's call whether to include in this batch or defer — adds maybe 40 LOC to the file. Recommendation: include; cross-diagnostic between `stream_telemetry.log` + `player_events.log` is useful for future audits.

**Files:** [src/ui/player/SidecarProcess.h](src/ui/player/SidecarProcess.h), [src/ui/player/SidecarProcess.cpp](src/ui/player/SidecarProcess.cpp), [src/ui/player/VideoPlayer.h](src/ui/player/VideoPlayer.h), [src/ui/player/VideoPlayer.cpp](src/ui/player/VideoPlayer.cpp).

**Success:** Main app builds (Rule 15 Hemanth build). Running with a stream in flight: sidecar `[event] probe_start sid=... delta=42ms` line in stderr + VideoPlayer debugLog line matching. Signal surface callable from consumers (Phase 2 will consume; this batch just wires). Zero regressions on existing `[VideoPlayer]` debug log lines.

### Batch 1.3 — StreamPlayerController consumer integration + frame-advance counter

- `StreamPlayerController` ([src/ui/pages/stream/StreamPlayerController.cpp](src/ui/pages/stream/StreamPlayerController.cpp)) connects to the 6 new signals. For stream-mode-specific context: consume `probeStarted` / `probeDone` / etc., correlate with `StreamEngine::statsSnapshot` (Phase 1 Slice A artifact) for cross-layer diagnostic cohesion. Example: if `probeDone` reports 9500ms analyze_duration BUT `StreamEngineStats.gateProgressBytes` was 5MB-contiguous at `probeStarted` — that's a sidecar-side bottleneck, not a substrate issue. Log both.
- Sidecar `[PERF]` log at [native_sidecar/src/video_decoder.cpp:969-978](native_sidecar/src/video_decoder.cpp#L969) extended with `frames_written_delta` counter cross-referenced against `position_us`-delta per 1s window. Agent 3 picks exact field — could be cumulative frame-delivery-count or per-window delta. Purpose: surface D-13 "frozen frame while time advances" discrepancy in the log itself without user-facing state change.
- `[PERF]` line format extension must be backward-compatible (additive fields only) — existing log parsers should ignore new fields.

**Files:** [src/ui/pages/stream/StreamPlayerController.h](src/ui/pages/stream/StreamPlayerController.h), [src/ui/pages/stream/StreamPlayerController.cpp](src/ui/pages/stream/StreamPlayerController.cpp), [native_sidecar/src/video_decoder.cpp](native_sidecar/src/video_decoder.cpp).

**Success:** Stream-mode open produces correlated log output showing the new events on both sides (sidecar stderr + Qt debugLog). `[PERF]` line shows frames_written_delta alongside existing p50/p99 metrics. Agent 4 can read stream-mode-specific failure traces end-to-end agent-side (Rule 15) without needing Hemanth to describe behavior.

### Phase 1 exit criteria
- 6 new events emitted by sidecar at correct lifecycle points.
- Qt-side signals + debugLog + optional structured log all functional.
- `StreamPlayerController` consumes stream-mode-specific diagnostic paths.
- `[PERF]` line extended with frame-advance counter for D-13 correlation.
- Agent 4 runs a slow-open stream (e.g., the One Piece hash that produced the 10.7s+67s trace) with Phase 1 instrumentation enabled + reads the resulting log agent-side; log must show every protocol milestone with wall-clock timing. If timeline has gaps Agent 7 audit didn't predict, flag for Phase 2 design refinement.
- `READY TO COMMIT — [Agent 3, STREAM_PLAYER_DIAGNOSTIC_FIX Phase 1]: Protocol event enrichment — 6 new session-scoped events + frame-advance counter | ...`

---

## Phase 2 — Post-probe/pre-first-frame user-facing state + first-frame watchdog (P1 — closes D-09 + D-11 user-visible half)

**Why:** Agent 4 validation chat.md:1966 (D-09) + chat.md:1950 (D-01 + D-11 Hypothesis). The LoadingOverlay widget is correct and wired; the missing piece is classified signal routing into it. State machine ([state_machine.h:7](native_sidecar/src/state_machine.h#L7)) currently has `{INIT, READY, OPEN_PENDING, PLAYING, PAUSED, IDLE}` with nothing between `OPEN_PENDING` and `PLAYING` — and `PLAYING` only sets in the first_frame branch ([main.cpp:464](native_sidecar/src/main.cpp#L464)). Phase 2 either extends the state machine OR keeps it and drives LoadingOverlay from the Phase 1 events directly (agent design call — state-machine extension is more architecturally honest, event-driven is less code).

Phase 2 also introduces a **30-second first-frame watchdog** to catch wedge states. Duration chosen to match sidecar's existing `STREAM_TIMEOUT:no data for 30 seconds` at [video_decoder.cpp:1087](native_sidecar/src/video_decoder.cpp#L1087) — internal consistency. 30s is empirically defensible: sidecar already treats 30s-no-data as cause to surface an event; user-facing watchdog firing at the same threshold produces coherent behavior.

**Phase 2 is Agent 3's domain work** (LoadingOverlay + VideoPlayer watchdog). Normal dispatch — no HELP ACK ceremony.

### Batch 2.1 — LoadingOverlay stage-classified text

- `LoadingOverlay` ([src/ui/player/LoadingOverlay.h](src/ui/player/LoadingOverlay.h), [src/ui/player/LoadingOverlay.cpp](src/ui/player/LoadingOverlay.cpp)) gains `setStage(Stage stage, QString filename)` API with enum `enum class Stage { Opening, Probing, OpeningDecoder, DecodingFirstFrame, Buffering, TakingLonger }`. Each stage has its own text; `setLoading(filename)` remaining as shortcut for `setStage(Stage::Opening, filename)` backward-compatible.
- Text copy — **Hemanth's Rule-14 UX call at Phase 2 exit;** TODO lists two proposals, agent picks one as default for initial ship, Hemanth flips at smoke if preferred:
  - **Proposal A (bracketed-progress, precise, matches sidecar vocabulary):** "Opening source…" / "Probing source…" / "Opening decoder…" / "Decoding first frame…" / "Buffering…" / "Taking longer than expected — close to retry"
  - **Proposal B (user-literal, smoother, less technical):** "Connecting…" / "Loading…" / "Almost ready…" / "Almost ready…" / "Buffering…" / "Still working — close to retry"
  - **Default ship:** Proposal A (bracketed-progress). Flip to Proposal B at Phase 2 exit smoke if Hemanth prefers.
- Agent 3 picks exact text-width + ellipsis-elision + animation (existing pill UI structure at `LoadingOverlay.cpp:22-32` preserves).
- `VideoPlayer::onStateChanged` (existing handler at `VideoPlayer.cpp:791+`) consumes Phase 1 signals to drive `setStage` transitions. Concrete mapping: `state=opening` → `setStage(Opening)`; `probeStarted` → `setStage(Probing)`; `probeDone` → stays `Probing` until `decoderOpenStarted` → `setStage(OpeningDecoder)`; `firstPacketRead` → `setStage(DecodingFirstFrame)` (OR `decoderOpenDone` if agent prefers — stage-transition design detail per Agent 3); `firstFrame` → hide overlay (existing path); `bufferingStarted` → `setStage(Buffering)`.

**Files:** [src/ui/player/LoadingOverlay.h](src/ui/player/LoadingOverlay.h), [src/ui/player/LoadingOverlay.cpp](src/ui/player/LoadingOverlay.cpp), [src/ui/player/VideoPlayer.h](src/ui/player/VideoPlayer.h), [src/ui/player/VideoPlayer.cpp](src/ui/player/VideoPlayer.cpp).

**Success:** User-visible stage text changes during an open that takes >2s. On slow opens, overlay text reflects actual pipeline position (probing, opening decoder, etc.) rather than frozen "Loading filename". Hemanth smoke confirms UX legibility.

### Batch 2.2 — First-frame watchdog + timeout state

- NEW `QTimer m_firstFrameWatchdog` on `VideoPlayer` started in `openFile` teardown-and-open sequence ([VideoPlayer.cpp:265-318](src/ui/player/VideoPlayer.cpp#L265)) or equivalent point. Fires at 30s if `firstFrame` hasn't been received. On fire:
  - `LoadingOverlay::setStage(TakingLonger)`.
  - Sidecar emits `first_frame_timeout` diagnostic event (additive, mirrors Phase 1 shape — Agent 3 coordinates with sidecar-side addition if separate from the LoadingOverlay UI flip, could be Qt-only for this batch and sidecar gets its own emit in a follow-up, Agent 3's call).
  - Watchdog cancelled on `firstFrame` signal reception (normal path) or on `openFile` teardown (new open).
- Watchdog duration declared as `constexpr int kFirstFrameWatchdogMs = 30 * 1000;` with inline comment citing empirical baseline (sidecar `STREAM_TIMEOUT:no data for 30 seconds` at video_decoder.cpp:1087).
- "Taking longer — close to retry" text + LoadingOverlay rendering changes must NOT block the player — user can still hit close button, escape, back navigation. Watchdog is UX signal, not a terminal state.
- Per-session watchdog identity (matches PLAYER_LIFECYCLE_FIX Phase 1 session-id-filter pattern) — watchdog from session N must not fire during session N+1.

**Files:** [src/ui/player/VideoPlayer.h](src/ui/player/VideoPlayer.h), [src/ui/player/VideoPlayer.cpp](src/ui/player/VideoPlayer.cpp); optionally [native_sidecar/src/main.cpp](native_sidecar/src/main.cpp) for `first_frame_timeout` diagnostic event emission if Agent 3 couples the sidecar-side signal with this batch.

**Success:** Slow-open scenario (force via a genuinely slow source or network throttle): watchdog fires at 30s, LoadingOverlay text flips, user can close cleanly. Regression: healthy-open scenario — watchdog cancels before firing; no false-positive UX flips on normal opens.

### Phase 2 exit criteria
- `LoadingOverlay::setStage` API live + 6 stages defined.
- VideoPlayer drives stage transitions from Phase 1 events.
- 30s first-frame watchdog functional with per-session identity.
- Hemanth confirms UX legibility via smoke on both healthy-open (no stage churn) and slow-open (stage transitions visible + watchdog fires if >30s).
- Hemanth approves or flips text copy proposal.
- `READY TO COMMIT — [Agent 3, STREAM_PLAYER_DIAGNOSTIC_FIX Phase 2]: Classified LoadingOverlay stages + 30s first-frame watchdog | ...`

---

## Phase 3 — Subtitle variant grouping (P2 — closes D-06 / Option C)

**Why:** Agent 4 validation chat.md:1960 (D-06 NEEDS-EMPIRICAL). `SubtitleMenu.cpp:19-60` + `refreshList` at `:318-339` produces a flat QListWidget with label `"<title>  (<LANG>)  — <addonId>"` — single-column, source-section ordering (embedded / addon / local blocks). No language grouping, no variant sub-list. Stremio's grouping by language-first with variant sub-list is a real UX delta. "More manual scanning" for multi-variant same-language sources is the observable pain. P2 polish; completely isolated from Phase 1 + Phase 2 (zero decode/open coupling).

NEEDS-EMPIRICAL tag on D-06 per Agent 4 chat.md:1960: multi-variant repro requires an addon-subtitled stream with ≥3 same-language variants. Hemanth weighs UX pain during Phase 3 exit validation. If pain is minor after variant grouping lands, confirm closed; if still painful, agent 3 iterates within Phase 3 scope.

### Batch 3.1 — SubtitleMenu re-layout: language groups + variant items

- `SubtitleMenu` UI re-layout. Current flat `QListWidget`: each row is a subtitle variant with origin prefix. New layout (Agent 3 picks Qt primitive — QTreeWidget, nested QListWidget with custom delegate, or QWidget-hosted grouping — based on existing menu architecture):
  - Top-level header rows per language (e.g., "English", "Japanese") — non-selectable + styled as headers.
  - Variant sub-items nested under each language header — selectable, show origin ("embedded" / "addon: opensubtitles" / "local") + any variant disambiguator (e.g., "English — forced-signs", "English — full + SDH").
  - Origin-priority sort WITHIN each language group: embedded first, then addon (by addon-id alphabetical), then local.
  - Active-variant highlight (existing selection mechanism preserved).
- Existing "Off" state preserved at top or bottom of the menu (Agent 3 picks placement — consistent with existing UX is correct default).
- Delay / offset / size controls unchanged — still present at the bottom of the menu as existing layout.

**Files:** [src/ui/player/SubtitleMenu.h](src/ui/player/SubtitleMenu.h), [src/ui/player/SubtitleMenu.cpp](src/ui/player/SubtitleMenu.cpp).

**Success:** Opening the subtitle menu on a multi-variant source shows language groups with nested variants. Selecting a variant changes subtitle display correctly. No regression on delay/offset/size controls.

### Batch 3.2 — Persistence + active-variant propagation consistency check

- Saved subtitle language/id preferences (via `VideoPlayer` existing save-progress path) must continue to survive across addon / embedded / local transitions — Phase 3 UI re-layout must not regress persistence.
- Active-variant highlight must reflect the currently-playing subtitle across menu re-opens (existing state pattern; Phase 3 verifies).
- If Batch 3.1 happens to introduce any storage-model changes (e.g., variant-grouping requires new variant-id canonicalization), the persistence path must accept both new and pre-existing saved preferences cleanly. Backward-compatibility shims if needed.

**Files:** [src/ui/player/SubtitleMenu.cpp](src/ui/player/SubtitleMenu.cpp), potentially [src/ui/player/VideoPlayer.cpp](src/ui/player/VideoPlayer.cpp) (if save-progress touchpoints need coordination).

**Success:** Close and re-open a file that played with a specific subtitle variant → variant restored correctly + highlighted in re-opened menu. Switch to a different addon-sourced variant mid-playback → next-open restores the new one. Zero persistence regression.

### Phase 3 exit criteria
- Subtitle menu shows language-grouped variants with origin priority.
- Active-variant highlight works.
- Persistence across menu re-opens + file re-opens holds.
- Hemanth validates UX improvement via multi-variant source smoke (addon-subtitled content with ≥3 same-language variants — Agent 3 notes if Hemanth needs a specific test source recommendation).
- `READY TO COMMIT — [Agent 3, STREAM_PLAYER_DIAGNOSTIC_FIX Phase 3]: SubtitleMenu language-variant grouping + origin-priority sort | ...`

---

## Scope decisions locked in

- **TODO name: `STREAM_PLAYER_DIAGNOSTIC_FIX_TODO.md`** per Agent 4's suggestion (chat.md:2007). Signals direction honestly — Option A is decode-open-diagnostic, Option C is UX-diagnostic (variant discoverability). "Diagnostic" encompasses both.
- **Strategy pick: Option A + Option C composable, Option B deferred, Option D codified non-goal** per Agent 4's endorsement of Agent 0's bias (chat.md:1937-1946).
- **Phase ordering: 1 → 2 → 3.** Agent 4's pick chat.md:1946. A first closes P0s; Phase 3 is isolated — can ship before/after/parallel if capacity demands, default sequence is 1 → 2 → 3.
- **Phase 1 + Phase 2 execution: Agent 3 (Video Player domain).** Consumer-side integration in `StreamPlayerController`: Agent 4. Phase 3 execution: Agent 3 (SubtitleMenu is in-player UI per `feedback_agent5_scope` — NOT Agent 5 routing). Resolution of Agent 4's open question #1 + #4.
- **LoadingOverlay text copy: Proposal A (bracketed-progress) default, Hemanth flips at smoke if prefers Proposal B (user-literal).** Single-line copy-change at phase-exit smoke per Rule 14. Resolution of Agent 4's open question #2.
- **First-frame watchdog duration: 30s.** Accept Agent 4's pick (matches sidecar's `STREAM_TIMEOUT:no data for 30 seconds` internal convention). Resolution of Agent 4's open question #3.
- **Phase 3 ordering: isolated — can ship any order.** Default 3rd in sequence per P0/P1/P2 ranking; if Agent 3 wants to ship Phase 3 first (trivially isolated, pure Qt UI work) that's acceptable. Do NOT pre-route to Agent 5.
- **Dispatch model: normal TODO dispatch, no HELP ACK ceremony.** Work is in Agent 3's domain (sidecar + SidecarProcess + VideoPlayer + LoadingOverlay + SubtitleMenu); Agent 4 consumes signals in Batch 1.3 rather than mutating Agent 3's surface. HELP ACK pattern reserved for cross-domain API-surface mutations (Agent 4B's STREAM_ENGINE_FIX case) — not applicable here.
- **No buffered-range UI in this TODO.** Deferred to post-Slice-A-Phase-2.3/3/4-close. Re-opens as own batch (probably additive to this TODO or separate micro-TODO) when substrate lands.
- **No sidecar decoder-reuse refactor in Phase 1.** Queued as conditional Phase 2+ scope or separate TODO per empirical findings. Phase 1 instrumentation lands first, Phase 2 observes, then decoder-reuse-vs-probe-shrink ranks accordingly.

## Isolate-commit candidates

Per Rule 11 + `feedback_commit_cadence`:
- **Batch 1.1** (sidecar event emission) — protocol surface introduction; isolate so Qt-side plumbing (1.2) lands against known-stable event contract.
- **Batch 2.1** (LoadingOverlay stage API) — widget API extension; isolate so watchdog (2.2) binds against stable stages.

Other batches commit at phase boundaries.

## Existing functions/utilities to reuse (not rebuild)

- [`write_event()` pattern in sidecar main.cpp](native_sidecar/src/main.cpp) — Phase 1 event emission mirrors this shape.
- [`SidecarProcess::processLine` + session-id filter at src/ui/player/SidecarProcess.cpp:410-434](src/ui/player/SidecarProcess.cpp#L410) — Phase 1.2 extends this parser; session-id filter applies to all new events.
- [`debugLog()` in VideoPlayer + LoadingOverlay setLoading/setBuffering at src/ui/player/LoadingOverlay.cpp:22-42](src/ui/player/LoadingOverlay.cpp#L22) — Phase 2.1 extends with `setStage` while preserving backward-compat.
- [`[PERF]` log at native_sidecar/src/video_decoder.cpp:969-978](native_sidecar/src/video_decoder.cpp#L969) — Phase 1.3 extends with frame-advance counter; additive fields only (backward-compat parsers).
- [`STREAM_TIMEOUT:no data for 30 seconds` at video_decoder.cpp:1087](native_sidecar/src/video_decoder.cpp#L1087) — Phase 2.2 watchdog duration baselines against this empirical threshold.
- [`StreamEngineStats + statsSnapshot` from STREAM_ENGINE_FIX Phase 1.1](src/core/stream/StreamEngine.h) — Phase 1.3 stream-consumer cross-correlates probe/decoder timing against substrate state.
- [`AVSyncClock::position_us()` at av_sync_clock.cpp:88-97](native_sidecar/src/av_sync_clock.cpp#L88) — Phase 1.3 frame-advance counter reads this to correlate position vs frame delivery (D-13 mechanism).
- [PLAYER_LIFECYCLE_FIX Phase 1 session-id-filter pattern](src/ui/player/SidecarProcess.cpp) — Phase 1.2 + Phase 2.2 watchdog identity applies the same discipline.
- [`QListWidget` SubtitleMenu architecture at src/ui/player/SubtitleMenu.cpp:19-60 + :318-339](src/ui/player/SubtitleMenu.cpp#L19) — Phase 3.1 evolves this (tree or grouped list); don't rebuild from scratch.

## Review gates

Agent 6 is DECOMMISSIONED 2026-04-16 per `project_agent6_decommission`; READY FOR REVIEW lines are retired. Hemanth approves phase exits directly via smoke per Rule 15. Template preserved for reactivation readiness:
```
READY FOR REVIEW — [Agent N, STREAM_PLAYER_DIAGNOSTIC_FIX Phase X]: <title> | Objective: Phase X per STREAM_PLAYER_DIAGNOSTIC_FIX_TODO.md + agents/audits/stream_d_player_2026-04-17.md. Files: ...
```

Per Rule 11 (commit protocol): READY TO COMMIT lines remain mandatory per phase; Agent 0 sweeps batch commits at phase boundaries.

## Open design questions Agent 3 (+ Agent 4 where cross-domain) decides as domain masters

- **Wall-clock source in sidecar (Phase 1.1).** `std::chrono::steady_clock` vs existing `AVSyncClock` vs new lightweight stopwatch. Agent 3's call.
- **`write_event` key-value shape for new events (Phase 1.1).** Consistent with existing event vocabulary; Agent 3 picks.
- **`first_packet_read` vs `first_decoder_receive` as the trigger for `setStage(DecodingFirstFrame)` (Phase 2.1).** Both Phase 1 events exist; Agent 3 picks based on which maps more cleanly to "decoder is actually processing."
- **State machine extension vs event-driven overlay (Phase 2.1).** Extend `state_machine.h` enum with new states (PROBE / DECODER_OPEN / DECODING_FIRST_FRAME) vs keep enum and drive LoadingOverlay from events directly. Architectural call — state-machine extension is more honest but more code; event-driven is lighter.
- **Optional `player_events.log` structured-log file in Batch 1.2.** Include vs defer. Recommendation include; Agent 3's call.
- **SubtitleMenu UI primitive (Phase 3.1).** QTreeWidget vs nested QListWidget with custom delegate vs QWidget-hosted grouping. Agent 3's call based on existing menu architecture + Qt styling consistency.
- **First-frame watchdog placement (Phase 2.2).** `QTimer` member on VideoPlayer vs `QTimer::singleShot` with generation check. Generation-check is safer per PLAYER_LIFECYCLE_FIX pattern; Agent 3's call.
- **`first_frame_timeout` diagnostic event — Qt-only (watchdog fires Qt-side) or sidecar-side (sidecar emits on decode-timeout)?** Either is defensible; Qt-only is simpler this phase, sidecar-side is more honest per event-protocol discipline. Agent 3's call — default Qt-only, sidecar-side in follow-up if empirical data shows sidecar actually hits its own 30s timeout before Qt does.
- **Phase 1.3 cross-correlation log format.** `StreamEngineStats` + Phase 1 event correlation — in `stream_telemetry.log`, `player_events.log`, or a new file? Agent 4's call since this is stream-mode-specific consumer integration.

## What NOT to include (explicit deferrals)

- **Buffered range / seekable range UI in seekbar** (D-03 / Option B). Deferred to post-Slice-A-Phase-2.3/3/4-close.
- **HLS / adaptive bitrate** (D-15 / Option D). Architectural non-goal — STREAM_ENGINE_FIX Phase 4.2 codifies.
- **Press-and-hold 2x speed gesture** (D-10). Default defer. Hemanth-UX-call-scope question for future.
- **Sidecar decoder reuse of probe `AVFormatContext`** (Agent 4 chat.md:1981 refinement). Queued; Phase 1 instrumentation lands first, decoder-reuse comes only if empirical data ranks it above other options.
- **User-facing frozen-frame recovery state** (D-13 UX beyond log). Phase 1.3's `[PERF]` frame-advance counter surfaces the log signal; user-facing recovery UI is future polish post Hemanth log-evidence baseline.
- **Cinemascope / aspect runtime revalidation** (D-14 NEEDS-EMPIRICAL). Validate against existing `_player_debug.txt` aspect-diag. No new widget / geometry batch unless a new defect surfaces.
- **Visible next-video control-bar button** (D-04 visible-affordance polish). Routed to STREAM_UX_PARITY Batch 2.6 (Agent 4 separate domain). Slice D unlocks that work but doesn't execute here.
- **Subtitle delay / offset / size persistence refactor.** Existing controls preserved.
- **Sidecar files beyond main, demuxer, video_decoder, subtitle_renderer, state_machine, av_sync_clock.** overlay_shm, overlay_renderer, protocol, heartbeat out-of-scope unless Phase 1 emit sites unavoidably cross them — scope-creep check if so.

## Rule 6 + Rule 11 application

- **Rule 6:** every batch builds + smokes on Hemanth's box (or agent-runnable smoke where Phase 1 structured log suffices) before `READY TO COMMIT`. Agent 3 does not declare done without verification.
- **Rule 11:** per-batch READY TO COMMIT lines; Agent 0 batches commits at phase boundaries (isolate-commit candidates ship individually).
- **Rule 7:** no new files in scope; no CMakeLists.txt touches.
- **Rule 14 (gov-v3):** technical decisions above (wall-clock source, event-shape, state-machine-extension-vs-event-driven, QtreeWidget vs nested list, watchdog placement, structured-log-file opt-in) are Agent 3's calls, not Hemanth's. LoadingOverlay text copy is product — Hemanth flips at smoke if prefers alternative.
- **Rule 15 (gov-v3):** Agent 3 + Agent 4 run agent-side log reads / sidecar smoke / grep themselves. `stream_telemetry.log` + `player_events.log` + `[PERF]` log + `_player_debug.txt` all agent-readable. Hemanth does UI-observable smoke only ("play file X, does the loading overlay text change as the open progresses?" / "close a mid-buffering stream, does the watchdog cancel?" / "open the subtitle menu on File Y, does it show language groups?").
- **Single-rebuild-per-batch per `feedback_one_fix_per_rebuild`.**
- **Evidence-before-analysis per `feedback_evidence_before_analysis.md`:** Phase 1 instrumentation lands first; Phase 2 user-facing state is driven by empirical signal from Phase 1 events; Phase 2.2 watchdog duration baselined against sidecar's existing 30s threshold (not speculative).

## Verification procedure (end-to-end once all 3 phases ship)

Agent 3 + Agent 4 run 1-3 + 7 (agent-side log reads); Hemanth runs 4-6 + 8-10 (UI smoke) per Rule 15 split:

1. **Phase 1 log coverage:** Agent 4 launches a stream, watches sidecar stderr + VideoPlayer debugLog → log shows `probe_start` → `probe_done` (with analyze_duration_ms) → `decoder_open_start` → `decoder_open_done` → `first_packet_read` → `first_decoder_receive` → `first_frame` with wall-clock deltas on each. Healthy stream: total delta ≤ few seconds. (Agent 4 reads.)
2. **Phase 1.3 cross-correlation:** stream-mode open shows correlated `StreamEngineStats` snapshot + Phase 1 event deltas in the same log window. Agent 4 can identify whether a slow open is substrate-side (gate/piece) or sidecar-side (probe/decoder). (Agent 4 reads.)
3. **Phase 1.3 `[PERF]` frame-advance counter:** during a healthy-playing stream, `frames_written_delta` per 1s window matches FPS. Agent 3 reads; if `frames_written_delta = 0` for 1s but `position_us` delta is ~1s, that's the D-13 repro condition being surfaced. (Agent 3 reads.)
4. **Phase 2 healthy-open smoke:** Hemanth opens a known-fast source → LoadingOverlay stages transition quickly (Opening → Probing → OpeningDecoder → DecodingFirstFrame → hide). No "TakingLonger" overlay. No watchdog fire. (Hemanth observable.)
5. **Phase 2 slow-open smoke:** Hemanth opens a known-slow source (or throttles network) → LoadingOverlay stays at its actual current stage (not stuck at "Loading"), eventually flips to "Taking longer — close to retry" at 30s if first frame not delivered. (Hemanth observable.)
6. **Phase 2 close-mid-open smoke:** Hemanth opens a stream, closes before first frame → watchdog cancels cleanly, no ghost overlay on next open. No crash. (Hemanth observable.)
7. **Phase 2 text copy approval:** Hemanth confirms Proposal A bracketed-progress text is clear OR flips to Proposal B. Single-line copy change if flipped. (Hemanth decision.)
8. **Phase 3 subtitle variant grouping smoke:** Hemanth opens a multi-variant same-language source (e.g., anime with 3+ English subtitle variants from different addons), opens subtitle menu → language header "English" with variants nested, selection works. (Hemanth observable.)
9. **Phase 3 persistence smoke:** Hemanth plays with a specific variant → close → re-open same file → variant restored + highlighted in menu. (Hemanth observable.)
10. **Regression scan:** close / open / seek / pause / subtitle delay / subtitle size / audio track switch → zero regression on existing behavior. (Hemanth observable.)

## Next steps post-approval

1. Agent 0 posts routing announcement in chat.md — Phase 1 + Phase 2 dispatch to Agent 3 (primary) + Agent 4 (consumer-side) + Agent 3 HELP ACK request for cross-domain sidecar + LoadingOverlay + SubtitleMenu touches.
2. Agent 3 posts HELP ACK (or scopes / declines) in chat.md; Phase 1 + 2 + 3 unblock on ACK.
3. Agent 3 executes Phase 1 batches per Rule 6 + Rule 11.
4. Agent 4 integrates consumer-side in `StreamPlayerController` after Phase 1.2 lands.
5. Agent 0 commits at phase boundaries per `feedback_commit_cadence` (isolate-commit exceptions per Rule 11 section).
6. MEMORY.md `Active repo-root fix TODOs` line updated to include this TODO. CLAUDE.md dashboard "Active Fix TODOs" table row added.
7. Post-Phase-2 close, evaluate whether Phase 1 empirical data re-ranks Phase-2+ scope or surfaces new conditional batches (decoder reuse, buffered-range re-open, user-facing frozen-frame recovery).

---

**End of plan.**
