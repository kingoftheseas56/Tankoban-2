# Stream Engine Fix TODO — Substrate observability + startup reliability + tracker resilience (Slice A of 6)

**Owner:** Agent 4 (Stream mode). Agent 0 coordinates phase gates and commit sweeps. Agent 6 review gates retained in template but dormant per 2026-04-16 decommission — Hemanth approves phase exits directly via smoke. Cross-agent HELP from Agent 4B (Sources — TorrentEngine domain) required for Phase 2.3 + Phase 3 (explicit ack pending at TODO-authoring time; see Agent Ownership § and Phase Why blocks).

**Created:** 2026-04-16 by Agent 0 after Agent 7's Slice A audit (`agents/audits/stream_a_engine_2026-04-16.md`, 349 lines, committed `fdd6711`) + Agent 4's comprehensive validation pass (chat.md line 710 — "Validation pass on Agent 7's Slice A audit"). First fix TODO in the 6-slice stream-mode comparative audit programme ratified this session.

## Context

Stream mode's substrate layer — `StreamHttpServer` + `StreamEngine` + `TorrentEngine` — recently hardened by `STREAM_LIFECYCLE_FIX_TODO` (closed at `139c0bb` + `b488079`; Phase 1-5, 7 audit findings closed). That work fixed the session-identity race class but never audited the byte-serving / head-piece / buffer heuristics / tracker behaviour against a clean reference. Hemanth's symptoms — "opening an episode, progress tracking, stream loading all feel broken" — have a suspected root below the session-identity layer: startup reliability under real-world tracker-light swarms.

Agent 7's Slice A audit compared our substrate against perpetus `stream-server-master` (open-source Rust reimplementation claiming drop-in API compatibility with Stremio's proprietary `server.js`) across 11 comparison axes + Stremio consumer-side patterns. Agent 4's validation pass (chat.md:710-946) confirmed all 11 axis findings, surfaced one NEW gap (tail-metadata deadlines for non-faststart MP4 moov / MKV cues), and produced a ranked fix plan using the **D-then-B-sequenced strategy** — observability first, empirical data informs startup-reliability tuning second.

Agent 4's strategy rejection rationale (chat.md:884-892):
- **Option A (passive — do nothing):** rejected. Hemanth reported real symptoms; substrate change is acceptable.
- **Option C (full reference-tier — `/create`, memory-first cache, bare-hash routes, HLS, subtitle proxy):** rejected. Balloons Slice A into "rebuild the streaming server contract", cross-cuts Slice D/3a/3c, violates slicing-programme discipline rule 1 (slice boundaries lock at start).
- **Option B (startup-only — change gate size + dynamic priorities):** too narrow alone. Ship gate changes without empirical evidence = `feedback_evidence_before_analysis.md` violation.
- **Option D (observability-first) alone:** delays user-visible improvements while we instrument.
- **Picked: D-then-B sequenced + Axis 7 tracker-fallback addition.** Phase 1 observability lands first, short empirical window collects data, Phase 2 startup tuning fires informed by the data, Phase 3 tracker resilience (cheap independent win) ships in parallel.

Per `feedback_evidence_before_analysis.md`: the 5 MB gate, the 200ms poll cadence in `waitForPieces`, and the tail-metadata gap are all hypotheses until Phase 1 telemetry confirms each is the dominant bottleneck for real Hemanth-observed stalls. Phase 2.1 (gate reduction) explicitly conditional on Phase 1 data saying the gate is the problem; if telemetry says otherwise, re-rank.

**Audit cross-reference discipline:** Slice A findings explicitly cross-referenced `STREAM_LIFECYCLE_FIX` closed findings (P0-1, P1-1, P1-2, P1-4, P1-5, P2-2, P2-3). Axis 4 (cancellation propagation) is STRONG OVERLAP — validated-already-closed by lifecycle Phase 5's cancellation token. Axes 1/5/6/7/8/10/11 are NON-OVERLAP (different problem classes). Axes 2/3/9 are PARTIAL OVERLAP. No re-opening of lifecycle-closed findings in this TODO.

**Scope:** 4 committed phases + 2 conditional phases. ~9 committed batches across Phases 1-4 (observability + startup reliability + tracker resilience + diagnostics polish/non-goals); Phases 5 + 6 conditional on empirical triggers (re-open friction / silent codec failure incidence respectively), defer by default.

## Objective

After this plan ships, Stream mode substrate behaves correctly under every startup and runtime scenario Slice A identified:

1. **Structured telemetry available for current + future substrate audits.** `StreamEngine::statsSnapshot()` returns a typed struct consumable by player UI, future audits, and Agent 4's Rule-15 agent-side log reads. Gated structured log file (`stream_telemetry.log`) captures first-piece arrival, gate progress %, prioritized piece range, active file index, peer/dl rates, cancellation state, tracker source count — all grep-friendly.
2. **Cold-start startup tuned by data.** If Phase 1 telemetry confirms 5 MB gate → first-frame is the dominant stall component, gate reduces to empirically-justified size (proposed 1.5-2 MB, subject to data). If telemetry refutes, gate stays + a different Phase 2 bottleneck targets.
3. **Non-faststart MP4 / tail-metadata-requiring formats stream correctly from stream start.** New tail-deadline logic sets libtorrent priorities on the last 2-5 MB of file at stream start (mirrors perpetus `handle.rs:322-329`), unblocking `moov` atom fetches for MP4 encoded without `+faststart` and MKV cue-at-end files.
4. **Piece-wait latency halved + CPU reduced.** Event-driven piece waiter (libtorrent `piece_finished_alert` wakeup) replaces 200ms polling in `waitForPieces`. User-visible: faster mid-file seek recovery; subliminal: less CPU on a stalled stream.
5. **Tracker-light sources (add-on response carrying 0-2 trackers) stream reliably.** Default tracker pool of 20-30 high-quality public trackers concatenated into magnets below a threshold (proposed: <5 add-on trackers). No silent zero-peer stalls on thin-metadata add-on responses.
6. **Stream-A architectural non-goals documented in-code.** Comment block in `StreamEngine.h` codifies: no HLS / no subtitle VTT proxy / no `/create` endpoint / no archive/NZB / no bare-hash routes / no multi-range / no backend abstraction. Downstream slices (D / 3a / C / 3b / 3c) read the comment and do not re-flag as gaps.
7. **Temporary `[STREAM]` qDebug instrumentation is retired** after Phase 1.2's structured logs supersede. Source tree no longer carries diagnostic-only trace comments marked "temporary — Agent 4B".
8. **`statsSnapshot` is the stable surface** — if Slice D / 3a / 3c audits later need substrate-level observability, they consume this API rather than re-introducing ad-hoc `qDebug`.

Phase 5 (cache rework) + Phase 6 (codec preflight) success criteria apply only if their conditional triggers fire; default is deferred.

## Non-Goals (explicitly out of scope for this plan)

Derived from Agent 4 validation § "Strategy-options pushback" + audit Axes 5/6/8/10/11. Slice A substrate-only. Codified in Phase 4.2 comment block.

- **HLS / adaptive transcoding routes** (Axis 6). Our native sidecar demuxes + decodes anything ffmpeg supports in-process; HTML-video constraint driving Stremio's HLS layer doesn't apply. No HLS endpoint, no hwaccel profile system, no `/transcode` route.
- **Subtitle VTT proxy routes** (Axis 5). Sidecar handles ASS/SSA/PGS/text subtitle decode + render directly. No `/subtitlesTracks` route, no server-side VTT extraction.
- **`/create` endpoint** (Axis 10). Tankoban pre-resolves fileIdx in `onMetadataReady` via `autoSelectVideoFile` + largest-video heuristic + `behaviorHints.filename`. Stremio's `/create` pushes fileIdx resolution to the server; our pre-resolution is functionally equivalent for the common case.
- **Archive / YouTube / NZB substrate** (Axis 11). Out of Stream-A scope. YouTube returns `UNSUPPORTED_SOURCE` today; archives may surface later as Sources/library features (Agent 4B's domain), not as Stream-A substrate concerns.
- **Bare-hash routes `/{infoHash}/{fileIdx}`** (Axis 2 Hypothesis 1). Our consumer is the in-process native sidecar, which only knows the URL `StreamEngine` builds at `StreamEngine.cpp:707-713`. Stremio's React shell consumer assumptions don't apply to a single-tenant native app. Route shape `/stream/{hash}/{file}` only.
- **Multi-range HTTP byte serving** (Axis 2). Single-range parser is RFC 9110 compliant + decoder-contract-sufficient. No multi-range bytes parser.
- **Backend abstraction — librqbit dual-backend** (Axis 8). Perpetus's trait boundary enables backend swap; we don't need it. Memory storage / piece waiters / tracker policy can all evolve WITHIN `TorrentEngine` without an abstraction layer.
- **Memory-first storage model** (Axis 11 Hypothesis 2). QFile-from-disk approach has valid properties (durable across restarts, simpler, larger-than-RAM possible, lower memory pressure on long sessions). Strategic choice, not a defect.
- **Stream-mode library writeback / progress tracking** — Slice 3a's territory. Not touched here.
- **Player-side buffering UI / loading skeleton / control bar** — Slice D's territory. Not touched here.
- **Source picker / `behaviorHints` display / multi-source ordering** — Slice C's territory.
- **Real Torrentio response-shape capture** — programme-level soft gap blocking Slice C dispatch, not Slice A execution.

## Agent Ownership

All batches are **Agent 4's domain** (Stream mode). Primary files:
- `src/core/stream/StreamEngine.{h,cpp}` — statsSnapshot API + tail-deadline + piece-waiter subscription
- `src/core/stream/StreamHttpServer.{h,cpp}` — piece-waiter consumer (Phase 2.3) + diagnostic-log cleanup (Phase 4.1)
- `src/core/torrent/TorrentEngine.{h,cpp}` — tracker pool constants + piece_finished_alert subscription API (Phase 2.3 + Phase 3, **cross-domain — Agent 4B**)

**Cross-agent coordination — HELP ACK REQUIRED BEFORE Phase 2.3 + Phase 3 EXECUTION:**

Agent 4B's STATUS-header pre-offered HELP (chat.md:555-561) covered **Axis 1** (`contiguousBytesFromOffset` semantics, `pieceRangeForFileOffset` boundary cases) and **Axis 3** (cache eviction observations). Agent 4's validation extends that ask to:
- **Axis 2 / Phase 2.3** — `piece_finished_alert` subscription API shape in `TorrentEngine` that `StreamHttpServer` can subscribe to per-connection. Replaces 200ms polling with wakeup-on-piece-available.
- **Axis 7 / Phase 3** — default tracker pool stored as `TorrentEngine` constants + injection hook into magnet URI construction below the `<5-tracker` threshold.

Agent 4B posts an explicit HELP ACK line in chat.md before Phase 2.3 + Phase 3 start. If ack doesn't arrive in-session, Phase 2.3 + Phase 3 hold at Phase 2.2 exit; Phase 4 (diagnostics polish + non-goals comment) can proceed in parallel because it has zero TorrentEngine surface area.

**No CMakeLists.txt touches in this plan** (no new files introduced). Rule 7 does not apply.

**Interactions with other in-flight work:**
- `STREAM_LIFECYCLE_FIX` is closed; cancellation token + source-switch Shape 2 split already live. Phase 2.3's piece-waiter integrates alongside the existing cancellation token (`cancelled && cancelled->load()`) — additive, no conflict.
- Agent 4B's temporary `[STREAM]` qDebugs at `StreamEngine.cpp:595/721` + `TorrentEngine.cpp:1205` are inputs to Phase 1.2 (promote to structured log) + outputs at Phase 4.1 (delete temp traces after structured logs cover). Coordinate the removal timing with Agent 4B.
- Slice D + Slice 3a will consume `statsSnapshot()` — design Phase 1.1's struct with those downstream consumers in mind (Player UI buffering state, progress tracking cadence).
- `STREAM_UX_PARITY` Batch 2.6 (Shift+N) remains held per programme-level decision — Slice D audit will reshape player keybinding surface; 2.6 unblocks post-Slice-D, not now.

## Phase 1 — Substrate observability (P0, no behavior change)

**Why:** Per `feedback_evidence_before_analysis.md`, substrate behavior changes without telemetry evidence are wrong-order work. Agent 4B's temporary `[STREAM]` qDebugs (shipped at `2a669d2` for 0%-buffering diagnosis) gave partial signal but lack (a) stable in-process consumer surface and (b) wall-clock timing data for gate-passed-from-metadata-ready. Phase 1 closes both gaps before Phase 2's behavior changes fire. No user-visible change. Agent 4 agent-side Rule-15 log reads feed Phase 2 decisions.

### Batch 1.1 — `StreamEngine::statsSnapshot()` in-process typed struct

- NEW struct `StreamEngineStats` in `StreamEngine.h` (or nested in `StreamEngine` class):
  ```cpp
  struct StreamEngineStats {
      QString infoHash;
      int activeFileIndex = -1;
      qint64 firstPieceArrivalMs = 0;    // wall-clock from metadata-ready to first-piece-have
      qint64 gateProgressBytes = 0;       // contiguousBytesFromHead
      qint64 gateSizeBytes = 0;           // kGateBytes effective (post sub-file clamp)
      double gateProgressPct = 0.0;       // 0..100
      int prioritizedPieceRangeFirst = -1;
      int prioritizedPieceRangeLast = -1;
      int peers = 0;
      qint64 dlSpeedBps = 0;
      bool cancelled = false;             // cancellation-token current state
      int trackerSourceCount = 0;         // tracker count in magnet URI
      // Reserved for Phase 3: int trackerPoolAugmented = 0;
  };
  ```
- NEW method `StreamEngineStats statsSnapshot(const QString& infoHash) const` — pure read. Consumes existing `torrentStatus` at `StreamEngine.cpp:347-356` for peers + dlSpeed, existing `contiguousBytesFromOffset` at `TorrentEngine.cpp:1141-1217` for gate progress, `m_streams` lookup for cancellation-token state + prioritized file index + tracker count. Falls back to sentinel values (–1 / empty-string) for unknown hash.
- NEW method `void recordFirstPieceArrival(const QString& infoHash)` — called once from whichever site detects first-piece-have for the active file (grep for first-piece-have signal; candidates include `applyStreamPriorities` post-first-piece or the sidecar-facing readiness check). Stores a `QElapsedTimer`-based wall-clock inside the `StreamRecord`. Subsequent calls are no-ops.
- Field added to `StreamRecord`: `qint64 metadataReadyMs = 0;` + `qint64 firstPieceArrivalMs = 0;`. Set in `onMetadataReady` (metadata-ready wall clock) and `recordFirstPieceArrival` respectively.

**Files:** [src/core/stream/StreamEngine.h](src/core/stream/StreamEngine.h), [src/core/stream/StreamEngine.cpp](src/core/stream/StreamEngine.cpp).

**Success:** `statsSnapshot` callable + returns populated struct for live stream; returns sentinel struct for unknown hash. No behavior change — existing flow is untouched.

**Isolate-commit:** yes. Observability-only surface; isolating this ensures Phase 1.2's log wiring lands against a known-stable API.

### Batch 1.2 — Structured telemetry log facility (gated by env var)

- NEW file-scope logger in `StreamEngine.cpp` (or helper class if warranted):
  - Env-var gate: `qgetenv("TANKOBAN_STREAM_TELEMETRY") == "1"`.
  - Output path: `stream_telemetry.log` in application-working-directory (next to `sidecar_debug_live.log`).
  - Format: grep-friendly key=value per line with timestamp prefix — `[2026-04-16T14:22:11.043] hash=ABC idx=0 firstPieceMs=N gatePct=X peers=P dlBps=Y state=gated/serving/cancelled tracker_sources=T`.
  - Writer must be thread-safe (called from engine-thread + HTTP worker threads).
- **Promote** Agent 4B's temporary `[STREAM]` qDebug sites to consume the same facility:
  - `StreamEngine.cpp:595-602` head-deadlines block → structured log.
  - `StreamEngine.cpp:721-725` applyStreamPriorities → structured log.
  - `TorrentEngine.cpp:1205-1214` contiguousBytesFromOffset → structured log.
  - Temporary qDebug lines **stay in place** through Phase 2-3 as redundant safety nets; Phase 4.1 removes them after Phase 2-3 stabilize.
- Log emission cadence: first-piece-arrival event (one-shot), every 5 s during gate-open phase, every 15 s during serving phase, on cancellation transition, on stream-stop. Never busy-logs.

**Files:** [src/core/stream/StreamEngine.cpp](src/core/stream/StreamEngine.cpp), [src/core/torrent/TorrentEngine.cpp](src/core/torrent/TorrentEngine.cpp).

**Success:** With `TANKOBAN_STREAM_TELEMETRY=1` set + stream launch + 60s play + stop: `stream_telemetry.log` exists with grep-parseable records covering metadata-ready → first-piece-arrival → gate-open → serving → stop. Without env var: no file created, no log emissions (current behavior preserved).

### Phase 1 exit criteria
- `StreamEngineStats` + `statsSnapshot()` live + returns populated data for live streams.
- Structured log file generated under env-var gate; emissions cover lifecycle events at appropriate cadence.
- Agent 4 runs 3-5 cold-start traces on Hemanth's box (Hemanth launches stream + stops; Agent 4 reads resulting log) to collect baseline substrate timing data informing Phase 2.1 gate-tuning decision.
- `READY TO COMMIT — [Agent 4, STREAM_ENGINE_FIX Phase 1]: Substrate observability — statsSnapshot struct + structured telemetry log facility (env-gated) | ...`

---

## Phase 2 — Startup reliability (P0, behavior changes informed by Phase 1 data)

**Why:** Agent 4 validation pass confirmed the audit's substrate hypotheses but ranked them NEEDS-EMPIRICAL-DATA for Axis 1 (gate conservatism) and PARTIALLY-CONFIRMED-WITH-NEW-GAP for tail metadata. Phase 2 executes the startup-reliability changes ONCE Phase 1 telemetry clarifies which change yields user-observable gain. Phase 2.1 explicitly conditional; 2.2 (tail deadline) and 2.3 (event-driven piece waiter) are unconditional wins.

**Phase 2.3 is cross-domain with Agent 4B — HELP ACK required before execution.**

### Batch 2.1 — Gate size tuning (CONDITIONAL on Phase 1 telemetry)

- **Trigger condition:** Phase 1 telemetry data shows gate-open-to-first-frame dominates cold-start stall time. Specifically: metadata-ready → gate-passed wall clock > 2× metadata-ready → first-piece-have wall clock across 3+ traces. If telemetry data refutes this (e.g., first-piece arrival IS the bottleneck, not gate size), SKIP 2.1 and re-rank — Phase 2 becomes 2.2 + 2.3 only.
- **If triggered:** reduce `kGateBytes` at `StreamEngine.cpp:209` from `5 MB` to empirically-justified value (proposed starting point: `1.5 MB` or `2 MB`, subject to Phase 1 data). Sidecar probe budget at `demuxer.cpp:15` is 5 MB — the head-deadline gradient (500-5000ms across first 5 MB, `StreamEngine.cpp:589-617`) already gets pieces prioritized up to 5 MB; the gate is purely the "allow streaming to start" threshold, which can be lower than the probe target.
- **Document the telemetry data** (whichever value gets chosen) inline with the constant so future substrate audits see the empirical justification: `// Gate at 1.5 MB — Phase 1 telemetry 2026-04-16 showed ...`. Agent 4's call on exact wording + exact value.
- Sub-file clamp at `StreamEngine.cpp:219` (`qMin(kGateBytes, fileSize)`) unchanged — tiny files still handled correctly.

**Files:** [src/core/stream/StreamEngine.cpp](src/core/stream/StreamEngine.cpp).

**Success (if triggered):** gate-open wall clock reduces proportionally (e.g., 5 MB → 1.5 MB = ~3× faster gate-pass on healthy-swarm cold starts). No regression on sub-file-clamp behavior or post-gate serving. Smoke: stream a 10 GB MKV on healthy swarm → gate passes in observably less time. Agent 4 collects one follow-up telemetry trace to confirm.

**Isolate-commit:** yes if triggered. Behavior-change batch touching startup-critical constant; isolate so any regression surfaces cleanly.

### Batch 2.2 — Tail-metadata head deadline (closes NEW gap from Axis 1 H2)

- **Problem (Agent 4 validation chat.md:732):** MP4 without `+faststart` keeps `moov` atom at file end. MKV may have cues at end. WebM similar. Current `StreamEngine.cpp:589-617` sets head deadlines across first N pieces only; zero tail-deadline logic. Sidecar probe stalls pulling `moov` from the slow-tail during startup — visible symptom is minute-scale "connecting" on non-faststart sources.
- **Fix:** at stream start (post-metadata-ready, same site as head deadlines), additionally set libtorrent priorities on the LAST 2-5 MB of the active file. Mirrors perpetus `stream-server-master/enginefs/src/backend/libtorrent/handle.rs:322-329` deferred-tail pattern.
- **Deadline gradient for tail:** slower than head (head is user-visible first-frame critical; tail is probe-completion important). Proposed: 3000-6000 ms linear across tail pieces. Agent 4's call on exact values.
- **Piece range calculation:** use existing `TorrentEngine::pieceRangeForFileOffset` helper with `offset = fileSize - tailBytes`, `length = tailBytes`. Same function already closes gate-logic calls; no new TorrentEngine API needed (confirming with Agent 4B as a lightweight read, not a HELP request — this batch stays domain-local).
- **Idempotency:** tail priorities set once per stream-start, never re-applied on seek (seek already has its own deadline logic).

**Files:** [src/core/stream/StreamEngine.cpp](src/core/stream/StreamEngine.cpp).

**Success:** non-faststart MP4 cold-start sidecar-probe completes without minute-scale stall. Agent 4 validates via Phase 1 telemetry `firstPieceArrivalMs` for tail pieces showing bounded timing + Hemanth UI smoke on a known non-faststart file (scenario framing per Rule 15 — "play this specific file and tell me if the 'connecting' stage completes in under 5s").

### Batch 2.3 — Event-driven piece waiter (CROSS-DOMAIN — Agent 4B HELP REQUIRED)

- **Problem (audit Axis 2 H2 + Axis 1):** `StreamHttpServer::waitForPieces` polls 200ms × 75 (up to 15s). On healthy swarms, the poll interval is the dominant bottleneck between piece-arrival and serve-loop-resumption — a piece arrives 30ms after poll, worker sleeps 170ms before noticing. Halving CPU cost + halving wait latency falls out of a wakeup pattern.
- **Fix (cross-domain):**
  - **Agent 4B (TorrentEngine):** add subscription API `TorrentEngine::subscribeToPieceFinished(const QString& infoHash, std::function<void(int pieceIdx)> callback) → SubscriptionHandle` + `unsubscribePieceFinished(SubscriptionHandle)`. Hook into existing `piece_finished_alert` pump (`TorrentEngine` already processes libtorrent alerts). Callback fires on engine thread; subscribers must be thread-safe or dispatch to their own thread.
  - **Agent 4 (StreamHttpServer):** `waitForPieces` swaps 200ms polling for: subscribe → wait-on-condvar-or-cancellation (timeout preserved at 15s as safety net) → on fire, re-check `haveContiguousBytes` → return. Unsubscribe on return path (both success + cancel + timeout).
- **Cancellation integration:** existing cancellation token at `StreamEngine.cpp:294-309` still short-circuits; condvar.wait predicate checks cancellation + piece-arrival-signal. No new cancellation semantics.
- **Fallback:** if Agent 4B's subscription API has scope concerns, Agent 4 can gate this batch on a simpler PR from 4B (e.g., a public `pieceFinished(hash, pieceIdx)` Qt signal on `TorrentEngine`) + use Qt-signal bridging.
- **Safety net:** 15s timeout preserved — event-driven path + timeout coexist, so missed-signal cases (subscription registration race) still terminate.

**Files:** [src/core/stream/StreamHttpServer.h](src/core/stream/StreamHttpServer.h), [src/core/stream/StreamHttpServer.cpp](src/core/stream/StreamHttpServer.cpp), [src/core/torrent/TorrentEngine.h](src/core/torrent/TorrentEngine.h), [src/core/torrent/TorrentEngine.cpp](src/core/torrent/TorrentEngine.cpp).

**Success:** CPU cost of a stalled `waitForPieces` drops by >50% (wall-clock-verified via Phase 1 telemetry `peers+dlBps` steady + idle piece-wait events). Mid-file seek recovery observably faster on slow-disk / slow-network scenarios. No regression on cancellation or 15s timeout behavior.

**Isolate-commit:** yes. Cross-domain touch + first subscription API in `TorrentEngine`; isolate for clean revert path if subscription pattern has issues.

### Phase 2 exit criteria
- Phase 2.1 either shipped with telemetry-justified gate value OR explicitly skipped with telemetry notes.
- Phase 2.2 tail deadline live; non-faststart cold-start stall closed.
- Phase 2.3 event-driven piece-waiter live post Agent 4B HELP ACK; CPU + latency gains observable.
- Agent 4 collects one post-Phase-2 telemetry trace for regression baseline.
- `READY TO COMMIT — [Agent 4, STREAM_ENGINE_FIX Phase 2]: Startup reliability | ...`

---

## Phase 3 — Tracker resilience (P1, CROSS-DOMAIN — Agent 4B HELP REQUIRED)

**Why:** Agent 4 validation (chat.md:807-814) CONFIRMED Axis 7 as real risk — no fallback tracker pool exists; tracker-light add-on responses (0-2 trackers) produce silent zero-peer stalls on swarms reachable only via DHT. Cheap independent win — 20-30 hard-coded high-quality public trackers + threshold-gated injection into magnet URIs below `<N` add-on trackers. Cross-domain with Agent 4B because magnet construction lives at `src/core/stream/addon/StreamSource.h:66-77` but the tracker pool constant and injection helper belong in `TorrentEngine` for reuse by Sources domain if needed.

### Batch 3.1 — Default tracker pool constant in TorrentEngine

- **Cross-domain Agent 4B ack required.** Propose: `TorrentEngine.h` exposes `static const QStringList& defaultTrackerPool()` returning 20-30 trackers. Names + URL list curated from publicly-known reliable trackers (examples: `udp://tracker.opentrackr.org:1337/announce`, `udp://tracker.openbittorrent.com:6969/announce`, etc.). Agent 4B's call on exact roster — they own tracker policy.
- Pool is compile-time-constant; no network fetch, no runtime mutation. Zero surface for external pollution.
- Rationale docs inline: "Fallback pool for add-on responses carrying <5 trackers. Engineered for one-shot magnet injection, not long-term swarm policy."

**Files:** [src/core/torrent/TorrentEngine.h](src/core/torrent/TorrentEngine.h), [src/core/torrent/TorrentEngine.cpp](src/core/torrent/TorrentEngine.cpp).

**Success:** `defaultTrackerPool()` returns list. Unit-like smoke (call returns non-empty + well-formed URIs).

### Batch 3.2 — Inject pool into magnets below threshold

- At magnet construction in `StreamSource::toMagnetUri` (or equivalent in `StreamSource.h:66-77` / its consumer path in `StreamEngine::streamFile` at `:67-112`): count add-on trackers; if `< kTrackerFallbackThreshold` (proposed: `5`, Agent 4's call), append pool URIs up to the threshold.
- **Augmentation log:** via Phase 1.2 structured logger — `[telemetry] hash=X addon_trackers=N pool_trackers_added=M`.
- **No deduplication against add-on trackers** — different announce URIs even for same tracker host are fine; libtorrent tolerates the redundancy.
- `statsSnapshot.trackerSourceCount` reflects total post-augmentation count (previously ungated Phase 1.1 field; this batch makes it meaningful).

**Files:** [src/core/stream/addon/StreamSource.h](src/core/stream/addon/StreamSource.h) (if magnet construction lives there; else `StreamEngine.cpp`).

**Success:** tracker-light add-on response (test-torrent with 0-2 trackers) — launching it now connects within 30s vs baseline "may never connect." Phase 1.2 structured log confirms pool injection fired with correct counts.

### Phase 3 exit criteria
- Default tracker pool callable from TorrentEngine.
- Magnets with `<5` add-on trackers get pool-augmentation.
- Structured log records augmentation events.
- `READY TO COMMIT — [Agent 4, STREAM_ENGINE_FIX Phase 3]: Tracker resilience — default pool + threshold-gated magnet injection | ...`

---

## Phase 4 — Diagnostics polish + architectural non-goals codified (P2)

**Why:** After Phase 2-3 ship, Agent 4B's temporary `[STREAM]` qDebugs are redundant with Phase 1.2's structured log facility. Clean up. Second batch codifies the 7 architectural non-goals (HLS / subtitle VTT / `/create` / archive / bare-hash / multi-range / backend abstraction) as an in-code comment block so Slice D / 3a / C / 3b / 3c don't re-flag them as gaps.

### Batch 4.1 — Remove temporary `[STREAM]` qDebugs

- Delete the 3 temporary qDebug sites Agent 4B added at `2a669d2`:
  - `StreamEngine.cpp:595-602` (head-deadlines block)
  - `StreamEngine.cpp:721-725` (applyStreamPriorities)
  - `TorrentEngine.cpp:1205-1214` (contiguousBytesFromOffset)
- Verify each site's observability is covered by Phase 1.2's structured log facility BEFORE deletion. If any signal isn't covered, extend the structured log first.
- Grep verify: `grep -n "\[STREAM\].*Agent 4B.*temporary" src/` returns zero hits post-batch.

**Files:** [src/core/stream/StreamEngine.cpp](src/core/stream/StreamEngine.cpp), [src/core/torrent/TorrentEngine.cpp](src/core/torrent/TorrentEngine.cpp).

**Success:** `[STREAM] ... temporary trace` comments gone; structured log at `TANKOBAN_STREAM_TELEMETRY=1` still captures equivalent signal.

### Batch 4.2 — Codify Stream-A architectural non-goals as in-code comment block

- Add a comment block at the top of `StreamEngine.h` (or a dedicated `doc/stream_a_non_goals.md` — Agent 4's call) documenting the 7 Slice A non-goals with rationale pointing to Slice A audit + validation chat.md citation for each:
  - No HLS / adaptive transcoding routes (Axis 6)
  - No subtitle VTT proxy routes (Axis 5)
  - No `/create` endpoint (Axis 10)
  - No archive / YouTube / NZB substrate (Axis 11)
  - No bare-hash `/{infoHash}/{fileIdx}` routes (Axis 2 H1)
  - No multi-range HTTP byte serving (Axis 2)
  - No backend abstraction / dual-backend support (Axis 8)
  - No memory-first storage model (Axis 11 H2)
- Format: one bullet per non-goal with a one-line rationale + `// See: agents/audits/stream_a_engine_2026-04-16.md Axis N` anchor.
- Purpose: downstream slice audits (D / 3a / C / 3b / 3c) read the block, do not re-flag these as gaps, do not file overlapping findings.

**Files:** [src/core/stream/StreamEngine.h](src/core/stream/StreamEngine.h).

**Success:** comment block present at top of `StreamEngine.h` citing Slice A audit path. Future audit agents land on the file and see the non-goals before analyzing.

### Phase 4 exit criteria
- Temporary `[STREAM]` qDebugs removed; structured log covers equivalent signal.
- Non-goals comment block codified in `StreamEngine.h`.
- `READY TO COMMIT — [Agent 4, STREAM_ENGINE_FIX Phase 4]: Diagnostics polish + architectural non-goals codification | ...`

---

## Phase 5 — CONDITIONAL: cache rework (P2-P3, defer by default)

**Why:** Agent 4 validation Axis 3 (chat.md:753-763) CONFIRMED delete-on-stop as deliberate Stream-A architecture (Stream = ephemeral, Library = persistent via Sources domain). Re-open friction is the plausible UX-visible cost. **Defer Phase 5 by default** — skip unless Hemanth flags re-open friction post-Phase-2 as visible UX pain.

**Trigger condition:** Hemanth reports "re-opening the same stream takes too long" (or equivalent) during post-Phase-2 smoke. Without the trigger, skip this phase entirely — the design choice stays "delete-on-stop + Library for persistence" and the UX routing goes to Slice 3a's Continue Watching surface (which can choose to promote-to-Sources on user action).

**If triggered:** 1-2 batches — bounded piece cache in `StreamEngine` (memory OR disk, Agent 4's call post-trigger). Mirror perpetus `piece_cache.rs` shape (5% disk / 512 MB mem / 5-min TTI) but smaller initial parameters. Do NOT rebuild toward the full perpetus architecture — keep the cache local + bounded.

**Deliberately undefined at TODO-authoring time.** Phase 5 scoping happens AFTER trigger fires; Agent 0 re-authors a delta-TODO post-trigger.

---

## Phase 6 — CONDITIONAL: codec preflight (P2, defer by default)

**Why:** Agent 4 validation Axis 6 (chat.md:800-801) PARTIAL CONFIRM on silent-codec-failure path. Rare (exotic VC-1 variants, some builds). **Defer by default** — skip unless Phase 1-2 telemetry OR Hemanth smoke shows actual silent-codec-failure incidence.

**Trigger condition:** structured log traces show `streamFile ok=true` followed by sidecar-side decode failure, OR Hemanth reports specific files that "connect but don't play" without error. Without the trigger, skip — current "hand URL to sidecar, let it succeed/fail" pattern suffices.

**If triggered:** 1 batch — preflight `ffprobe`-via-sidecar codec check in `streamFile` before returning `ok=true`. Simple codec-compatibility lookup against a known-compatible list; on incompatible codec, return a structured error the player UI can surface as "codec unsupported" rather than generic failure.

**Deliberately undefined at TODO-authoring time.** Same post-trigger re-authoring pattern as Phase 5.

---

## Scope decisions locked in

- **D-then-B sequenced strategy (Agent 4's pick).** Phase 1 observability lands first, telemetry collects, Phase 2 behavior changes fire informed by data. No substrate behavior change without evidence.
- **`statsSnapshot()` as in-process struct, not HTTP endpoint.** Single-tenant native consumer; no network-exposed stats route needed. Slice D / 3a / 3c consume the same struct through Qt signals or direct calls.
- **Env var `TANKOBAN_STREAM_TELEMETRY=1` + `stream_telemetry.log` in working dir.** Matches `sidecar_debug_live.log` conventions. Agent 4's recommendation accepted.
- **Phase 2.1 gate reduction is conditional on Phase 1 telemetry.** Don't ship behavior change against an unconfirmed hypothesis. `feedback_evidence_before_analysis.md` is the governing precedent (three analysis-driven fixes shipped in one session that didn't close the bug vs one trace-driven fix that did).
- **Phase 2.2 tail-metadata is unconditional.** New gap Agent 4 surfaced in validation; non-faststart files are a known-broken class today regardless of gate size.
- **Phase 2.3 + Phase 3 are gated on Agent 4B HELP ACK.** Cross-domain `TorrentEngine` touches require explicit acknowledgement. Agent 4B's pre-offered HELP covered Axes 1+3; need explicit extension to Axes 2+7.
- **Phase 5 (cache) deferred by default.** Keep Slice A bounded per slicing-programme discipline rule 1. Re-open friction routes to Slice 3a / Library-persistence if it surfaces.
- **Phase 6 (codec preflight) deferred by default.** Rare failure class; no pre-emptive spend.
- **Architectural non-goals codified in Phase 4.2.** In-code comment block, not just this TODO. Future audits see it.
- **No bare-hash routes, no multi-range, no backend abstraction, no HLS/VTT/create routes.** Codified as non-goals; not reconsidering mid-execution.

## Isolate-commit candidates

Per Rule 11 + `feedback_commit_cadence`:
- **Batch 1.1** (statsSnapshot API) — isolate so Phase 1.2 log wiring lands against a known-stable consumer surface.
- **Batch 2.1** (gate size tuning, if triggered) — isolate behavior change touching startup-critical constant.
- **Batch 2.3** (event-driven piece waiter) — cross-domain first-subscription-API-in-TorrentEngine; isolate for clean revert path.

Other batches commit at phase boundaries.

## Existing functions/utilities to reuse (not rebuild)

- [`TorrentEngine::contiguousBytesFromOffset` at TorrentEngine.cpp:1141-1217](src/core/torrent/TorrentEngine.cpp#L1141) — consumed by `statsSnapshot` for `gateProgressBytes`.
- [`TorrentEngine::pieceRangeForFileOffset`](src/core/torrent/TorrentEngine.cpp) — consumed by Phase 2.2 tail-deadline logic + Phase 1.1 `prioritizedPieceRange*` fields.
- [`torrentStatus` at StreamEngine.cpp:347-356](src/core/stream/StreamEngine.cpp#L347) — consumed by `statsSnapshot` for `peers + dlSpeedBps`.
- [Existing `QElapsedTimer` / `qElapsedTimerDefault` pattern in StreamEngine](src/core/stream/StreamEngine.cpp) — consumed by `metadataReadyMs` / `firstPieceArrivalMs` fields.
- [Cancellation token at StreamEngine.cpp:294-309](src/core/stream/StreamEngine.cpp#L294) — Phase 2.3 event-driven piece-waiter integrates alongside; no new cancellation semantics.
- [libtorrent `piece_finished_alert` pump in TorrentEngine alert loop](src/core/torrent/TorrentEngine.cpp) — Agent 4B hooks Phase 2.3 subscription API into it.
- [Existing `kGateBytes` constant + sub-file clamp at StreamEngine.cpp:209+219](src/core/stream/StreamEngine.cpp#L209) — Phase 2.1 adjusts the constant; sub-file clamp semantics preserved.
- [Agent 4B's `[STREAM]` qDebug sites at StreamEngine.cpp:595/721 + TorrentEngine.cpp:1205](src/core/stream/StreamEngine.cpp) — Phase 1.2 promotes; Phase 4.1 removes.

## Review gates

Agent 6 is DECOMMISSIONED 2026-04-16 per `project_agent6_decommission`; READY FOR REVIEW lines are retired. Hemanth approves phase exits directly via smoke per Rule 15. Template preserved for reactivation readiness:
```
READY FOR REVIEW — [Agent 4, STREAM_ENGINE_FIX Phase X]: <title> | Objective: Phase X per STREAM_ENGINE_FIX_TODO.md + agents/audits/stream_a_engine_2026-04-16.md. Files: ...
```

Per Rule 11 (commit protocol): READY TO COMMIT lines remain mandatory per phase; Agent 0 sweeps batch commits at phase boundaries.

## Open design questions Agent 4 decides as domain master

- **`StreamEngineStats` struct scope.** Nested in `StreamEngine` class vs free struct in a header. Free-struct is more consumer-friendly (Slice D + 3a will consume); nested is simpler for single-consumer era. Agent 4's call.
- **`recordFirstPieceArrival` invocation site.** Which signal in the flow most reliably corresponds to first-piece-have for the active file — `applyStreamPriorities` post-first-piece, or sidecar-facing readiness check, or libtorrent `piece_finished_alert` filtered by piece index matching active file's head range? Agent 4 picks during implementation.
- **Structured telemetry log format.** Proposed key=value with `[iso-timestamp]` prefix; Agent 4 can pick JSON-per-line if greppability isn't worse + future consumer tooling benefits.
- **Phase 2.1 exact gate value.** `1.5 MB` vs `2 MB` vs different; decide from Phase 1 telemetry data, not at TODO-authoring time.
- **Phase 2.2 tail window size.** `2 MB` vs `5 MB` — Agent 4's call informed by observed moov-atom sizes in test files (typical MP4 non-faststart moov atoms 100KB-2MB; MKV cues similar).
- **Phase 2.3 piece-waiter subscription API shape.** `std::function<void(int)>` callback vs Qt signal vs condvar notification object. Agent 4 + Agent 4B co-design post-HELP-ACK.
- **Phase 3.1 tracker list curation.** Which 20-30 URLs. Agent 4B's call — they own tracker policy.
- **Phase 3.2 threshold value.** `<5` add-on trackers trigger augmentation; Agent 4 can adjust based on observed swarm resilience.
- **Phase 4.2 non-goals comment location.** Top of `StreamEngine.h` vs dedicated `doc/stream_a_non_goals.md` vs README section. Agent 4's call.

## What NOT to include (explicit deferrals)

- **Bare-hash routes** — no. Single-tenant native sidecar consumer.
- **Multi-range byte serving** — no. RFC 9110 single-range compliant + decoder-contract sufficient.
- **HLS / hwaccel profile system / `/transcode` route** — no. Native sidecar demuxes everything ffmpeg supports.
- **Subtitle VTT proxy / `/subtitlesTracks` route** — no. Sidecar handles subtitle decode + render.
- **`/create` endpoint** — no. Pre-resolved fileIdx via `autoSelectVideoFile`.
- **Backend abstraction / librqbit** — no. Memory storage + piece waiters + tracker policy evolve within `TorrentEngine`.
- **Memory-first storage model** — no. QFile-from-disk stays.
- **Archive / YouTube / NZB substrate** — no. Future Sources/library concerns, not Stream-A.
- **Network-exposed `/stats.json` HTTP endpoint** — no. Single-tenant; `statsSnapshot()` in-process struct covers needs.
- **SSDP auto-discovery** — no. Single-tenant; server isn't network-visible as a service.
- **GeoIP / tracker ranking by latency** — no. Static pool + libtorrent's own policy handle ordering.
- **Piece cache warm across restarts** — no. Delete-on-stop is deliberate; persistence goes through Sources.
- **Cross-agent `STREAM_UX_PARITY` Batch 2.6 (Shift+N)** — holds per programme decision (land post-Slice-D).
- **Slice D/3a/C/3b/3c work** — later audits; not this TODO.
- **Real Torrentio `/stream/series/<id>.json` capture** — programme-level soft gap, blocks Slice C dispatch not Slice A execution.

## Rule 6 + Rule 11 application

- **Rule 6:** every batch builds + smokes on Hemanth's box (or agent-runnable smoke where Phase 1 structured log suffices) before `READY TO COMMIT`. Agent 4 does not declare done without verification.
- **Rule 11:** per-batch READY TO COMMIT lines; Agent 0 batches commits at phase boundaries (isolate-commit candidates above ship individually).
- **Rule 7:** no new files in scope (all changes edit existing files); no CMakeLists.txt touches.
- **Rule 14 (gov-v3):** technical decisions above (gate value, log format, subscription API shape, tracker roster, threshold value, non-goals comment location) are Agent 4 + Agent 4B's calls, not Hemanth's. Hemanth's smoke surfaces behavior observations; agents pick implementation shape.
- **Rule 15 (gov-v3):** Agent 4 reads `stream_telemetry.log` + runs agent-side diagnostic empirical work; Hemanth does UI-observable smoke only ("play this file and tell me if X", "close mid-buffer and tell me if it feels slow"). No grep-work-asks on Hemanth.
- **Single-rebuild-per-batch per `feedback_one_fix_per_rebuild`.**
- **Evidence-before-analysis per `feedback_evidence_before_analysis.md`:** Phase 2.1 explicitly conditional on Phase 1 telemetry; Phase 5 + Phase 6 conditional on empirical triggers. No speculative behavior changes.

## Verification procedure (end-to-end once all committed phases ship)

Agent 4 runs 1-3 (agent-side log reads); Hemanth runs 4-10 (UI smoke) per Rule 15 split:

1. **Phase 1 structured log produced:** Set `TANKOBAN_STREAM_TELEMETRY=1`, launch + play a stream for 60s, stop → `stream_telemetry.log` exists, contains metadata-ready / first-piece-arrival / gate-open / serving / stop events with parseable fields. (Agent 4 reads.)
2. **`statsSnapshot()` returns live data:** Launch stream → call statsSnapshot during playback → returns struct with non-sentinel values for peers + dlSpeedBps + gateProgressPct + trackerSourceCount. (Agent 4 validates via instrumentation or log emission.)
3. **Phase 2.1 gate-pass wall-clock drop (if 2.1 triggered):** Phase 1 baseline trace shows gate-pass at ~`T` ms; post-2.1 trace shows gate-pass at measurably lower time, ideally proportional to gate-size reduction. (Agent 4 reads.)
4. **Non-faststart MP4 cold-start doesn't stall:** Hemanth launches a known non-faststart MP4 → "connecting" stage completes within 5s instead of minute-scale stall. (Hemanth observable.)
5. **Tracker-light source connects:** Hemanth launches a test magnet carrying 0-2 add-on trackers → within 30s peer count reaches non-zero. (Hemanth observable.) Phase 1.2 structured log confirms pool augmentation fired at launch time.
6. **Mid-file seek recovery feels faster** (Phase 2.3 qualitative): Hemanth plays, seeks ahead past buffered range → video resumes with less perceived delay than pre-Phase-2.3 baseline. (Hemanth observable; Agent 4 cross-checks via Phase 1 log showing reduced piece-wait latency.)
7. **Cancellation still works:** Hemanth closes stream mid-buffering → socket release within 200ms (existing behavior preserved from `STREAM_LIFECYCLE_FIX` Phase 5); structured log shows `state=cancelled` transition. (Hemanth + Agent 4 split.)
8. **No regression on healthy-swarm streaming:** Hemanth plays a 2-hour healthy-swarm stream through → no stutters, no crashes, no new stalls. (Hemanth observable.)
9. **Temporary qDebugs gone:** `grep -n "Agent 4B.*temporary" src/` returns zero hits after Phase 4.1. (Agent 4 runs.)
10. **Non-goals comment block present:** Open `StreamEngine.h` → top comment documents 8 non-goals with Slice A audit citation per bullet. (Agent 4 or Agent 0 cross-checks at sweep time.)

## Next steps post-approval

1. Agent 0 posts routing announcement in chat.md with Phase 1 launch framing + Agent 4B HELP ACK request for Axes 2 + 7 (Phase 2.3 + Phase 3).
2. Agent 4 executes Phase 1 (observability) immediately; behavior-change phases 2-3 hold on telemetry data (2.1 conditional) + Agent 4B HELP ACK (2.3 + 3).
3. Agent 4 collects 3-5 cold-start telemetry traces on Hemanth's box before firing Phase 2.1 decision.
4. Agent 4B posts HELP ACK (or declines / scopes) in chat.md; Phase 2.3 + 3 unblock on ACK.
5. Agent 0 commits at phase boundaries per `feedback_commit_cadence` (isolate-commit exceptions per Rule 11 section).
6. MEMORY.md `Active repo-root fix TODOs` line updated to include this TODO. CLAUDE.md dashboard "Active Fix TODOs" table row added.
7. Post-Phase-4 close, Agent 0 evaluates Phase 5 / Phase 6 triggers; if fired, delta-TODO re-authored. If not, TODO closes at Phase 4.

---

**End of plan.**
