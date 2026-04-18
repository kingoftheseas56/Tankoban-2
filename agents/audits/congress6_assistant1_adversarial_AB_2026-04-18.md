# Adversarial Review — Congress 6 Slices A + B — 2026-04-18

By Assistant 1 (Agent 0's adversarial reviewer under the revised Congress 6 plan addendum §ADDENDUM). Reads:

- [congress6_stream_primary_2026-04-18.md](congress6_stream_primary_2026-04-18.md) — Slice A (Agent 4, Stream primary, 423 lines)
- [congress6_sources_torrent_2026-04-18.md](congress6_sources_torrent_2026-04-18.md) — Slice B (Agent 4, Sources/Torrent substrate + piece primitives, 445 lines)

Scope per plan addendum: 7-section adversarial sweep. Not a re-audit — no line-by-line Stremio Reference re-read; I cross-check Tankoban-side citations empirically where load-bearing and flag internal consistency hazards. Assistant 2 owns Slices C+D; not my territory.

Methodology: fresh reads of Slice A + B end-to-end; empirical spot-checks of the load-bearing Tankoban citations that gate P2 / P3 / P4 votes (TorrentEngine alert pump, HTTP server piece-wait, stopStream ordering); cross-reference of [agents/HELP.md](../HELP.md) for API-freeze Ask 3 list. No Tankoban compile/run. No Stremio Reference re-read.

---

## 1. Gap check

Items either audit should have covered but either skipped or only glanced at:

1. **12-method API freeze list is referenced but not enumerated.** Slice B line 20 says `TorrentEngine.h (311 lines — full) — public surface + pieceFinished signal declaration + 12-method API freeze`. Neither audit lists the 12 methods nor checks them against [agents/HELP.md](../HELP.md#L26) Ask 3's enumerated list. Integration memo (Agent 0) needs this list frozen in writing before P2 scaffold lands — the audit was a natural checkpoint to produce it. **Gap: enumerate and pin the 12-method surface in the integration memo; do not carry forward as hand-wave.**

2. **`contiguousBytesFromOffset` lock-ordering decision left dangling.** Slice A Q1 Hypothesis 3 (line 118) flags a 3-way decision — keep `m_mutex` / `QReadWriteLock` / per-stream mutex — for P2. Slice B's remit covers piece primitives and had the closest read of TorrentEngine (lines 1109–1269 targeted), yet does not weigh in. This is the exact Slice B sweet spot (Qt-port lock subtleties, Q2's lock-ordering lens) and it went unexamined. **Gap: either Slice B should have called this decision, or Agent 0's integration memo must make the 3-way call before P2 ships — otherwise P2 lands without a locking posture.**

3. **Alert-pump cadence tightening refactor not sized end-to-end.** Slice B line 362 flags the `progressTick >= 4` wall-clock conversion if `wait_for_alert(250ms)` is tightened to 5–25 ms. Slice B sizes the conversion as "~15-line refactor" but does not list all downstream dependents of the 1-second cadence — `emitProgressEvents`, `checkSeedingRules`, any callers that assume 1s tick frequency (e.g., seeding-ratio / share-limit math). A mis-scoped refactor under the "trivial in-situ fix" exception is exactly the trap Trigger C loosening was warned about. **Gap: either rule the cadence tightening OUT of the trivial-fix bucket (proper P2 batch with full dependent audit), or expand the 15-line sizing in the integration memo.**

4. **MAX_STARTUP_PIECES = 2 is unverified in this session.** Slice A line 86 and line 109 cite the value as "2 pieces per prior-art" — specifically the 2026-04-16 audit. Neither audit re-opens `backend/priorities.rs:6-9` this session to confirm. This value is load-bearing for Q1 Divergence's "head-window shape" row and the "first-piece target (10 ms)" hypothesis. Prior-art drift — however low-risk given R21 mtime freeze — is a recognized R-series risk. **Gap: re-verify MAX_STARTUP_PIECES = 2 before P3 Prioritizer compile-time constants are frozen.**

5. **Empirical repro missing for the tail-deadline-lost-on-UserScrub claim.** Slice B Q3 states "our current code's tail-metadata deadline WILL be cleared on user-scrub in our `clearPieceDeadlines` call" (line 125–126) and later "this is a bug per Stremio reference" (line 286). The code path is read correctly — `prepareSeekTarget` at [StreamEngine.cpp:704-824](../../src/core/stream/StreamEngine.cpp#L704-L824) does call `clearPieceDeadlines` — but there's no smoke evidence or trace that the tail-metadata loss materializes as user-visible mid-playback stall. Hypothesis-grade, not verified-grade. **Gap: label as hypothesis until empirically repro'd; do not premise P3 SeekClassifier design on it as a known defect.**

6. **Slice A's 6-orphan sweep and Slice B's `routes/engine.rs` overlap on `/create`.** Slice A cross-cutting line 356–361 and Slice B's "Orphan surface" section both cover engine-create semantics. Slice B's read is fuller (`create_magnet` at `engine.rs:76-100`), Slice A's is a one-line cross-cutting note. Not a contradiction, but redundancy that suggests the scope split between "Slice A orphan routes" and "Slice B orphan surface" was fuzzy. **Gap: integration memo should consolidate so both `/create` reads collapse into one source of truth.**

7. **`findSubtitleTracks`-style probe dispatch is a consumer-side concern flagged to Slice D without cross-check.** Slice A's subtitles.rs observation (lines 259-271) concludes non-goal for Stream-A. Fine — but the hand-off to Agent 5 library-UX is asserted without any chat with Slice D's question sheet. **Low-severity gap — flag for Assistant 2 cross-check against Slice D when Slice D lands.**

---

## 2. Label check (hypothesis vs fact discipline)

Claims I test against the evidence presented:

| Claim | Slice | Label stamped | My verdict |
|---|---|---|---|
| Mode A root-cause is poll-sleep primitive | A Q1 Hyp. 1 | Hypothesis | **OK** — properly labelled, validation gated post-P2 |
| 5 MB gate not semantically required | A Q1 Hyp. 2 | Hypothesis | **OK** — properly gated on P2 landing |
| `contiguousBytesFromOffset` lock contention at P2 | A Q1 Hyp. 3 | Hypothesis + open decision | **OK but dangling** (see Gap #2) |
| Rapid source-switch slower in Tankoban | A Q2 Hyp. 1 | Hypothesis | **OK** — post-P6 scoped |
| `StreamGuard::Drop` shape correct for lifecycle | A Q2 Hyp. 2 | Hypothesis | **OK** |
| 5s grace window is tuned for click-through | A Q2 Hyp. 4 | Hypothesis | **OK** |
| Agent 3's P4 3-tier probe shape correct | A Q3 Hyp. 1 | Hypothesis, handed to Slice C | **OK** |
| Head deadline 50× slower than Stremio URGENT | B Q1 Hyp. 1 | Hypothesis | **MIXED** — the 50× is arithmetic on cited values, not hypothesis; the empirical delta on first-byte latency is hypothesis. Split the fact from the claim. |
| Proactive-bonus equivalent of Netflix ABR | B Q1 Hyp. 2 | Hypothesis | **OK** |
| SeekType enum load-bearing for P3 | B Q1 Hyp. 3 | Hypothesis | **OK** |
| 300-piece hard cap empirically tuned | B Q1 Hyp. 4 | Hypothesis | **OK — adopt as constant** |
| 250 ms wait_for_alert caps P2 wake latency | B Q2 Hyp. 1 | Hypothesis | **Overstated** — 250 ms is the `wait_for_alert` _timeout_, not the steady-state pump interval. When alerts arrive, `drainAlerts` runs immediately; the 250 ms is only the idle-poll upper bound. Confirmed at [TorrentEngine.cpp:52](../../src/core/torrent/TorrentEngine.cpp#L52) — this is `wait_for_alert(timeout)` which returns immediately on first alert. Latency is `min(250ms, time-to-next-alert)`. Under active piece download with alerts arriving constantly, actual pump cadence is alert-arrival-limited, not timeout-limited. **Re-label: "250 ms caps worst-case idle wake latency"; under load pump is alert-driven.** |
| P2 Qt port uses simpler wait-with-timeout | B Q2 Hyp. 2 | Hypothesis | **OK** |
| QueuedConnection over DirectConnection for pieceFinished | B Q2 Hyp. 3 | Hypothesis | **OK** |
| `clear_torrent` call-site is stopStream | B Q2 Hyp. 4 | Hypothesis / design proposal | **OK** |
| P3 re-assert at 1–2 Hz not per-poll | B Q3 Hyp. 1 | Hypothesis | **OK** |
| P3 SeekClassifier must preserve tail on UserScrub | B Q3 Hyp. 2 | Hypothesis, but prose says "this is a bug" (line 286) | **Mislabelled in prose** — section header says hypothesis but body phrases as defect. See Gap #5. Bring the two into agreement. |
| 300 ms seek-retry cadence load-bearing | B Q3 Hyp. 3 | Hypothesis | **OK** |
| No separate deadline-expiry refresh needed | B Q3 Hyp. 4 | Hypothesis | **OK, but relies on libtorrent 2.0.11 doc claim cited from prior-art** (not re-verified this session) |

Net: most hypotheses properly labelled. Two material mislabels: Slice B Q2 pump-latency overstatement (timeout ≠ steady-state), and Slice B Q3 tail-deadline claim oscillating between hypothesis and defect prose.

---

## 3. Citation spot-check

Load-bearing Tankoban-side citations I verified against the repo this session:

| Citation | Slice | Verified? |
|---|---|---|
| `wait_for_alert(std::chrono::milliseconds(250))` at [TorrentEngine.cpp:52](../../src/core/torrent/TorrentEngine.cpp#L52) | B line 156 | **✓ exact** |
| `RESUME_SAVE_INTERVAL_S = 30` at [TorrentEngine.cpp:70](../../src/core/torrent/TorrentEngine.cpp#L70) | B line 362 | **✓ exact** |
| `progressTick >= 4` at [TorrentEngine.cpp:56](../../src/core/torrent/TorrentEngine.cpp#L56) | B line 362 | **✓ exact** |
| `piece_finished_alert` emit at [TorrentEngine.cpp:158-164](../../src/core/torrent/TorrentEngine.cpp#L158-L164) | B line 145 | **✓ exact** (code at 158–164 matches verbatim to quoted snippet) |
| `alert_category::piece_progress` in mask at [TorrentEngine.cpp:262-269](../../src/core/torrent/TorrentEngine.cpp#L262-L269) | B line 155 | **✓** — actual block is lines 262–269 (inclusive of `set_int` commit); Slice B range is correct |
| `PIECE_WAIT_POLL_MS = 200; PIECE_WAIT_TIMEOUT_MS = 15000` at [StreamHttpServer.cpp:77-78](../../src/core/stream/StreamHttpServer.cpp#L77-L78) | A line 54 | **✓ exact** |
| `QThread::msleep(PIECE_WAIT_POLL_MS)` at [StreamHttpServer.cpp:104](../../src/core/stream/StreamHttpServer.cpp#L104) | A line 55 (cited at :104) | **✓ exact** |
| Cancellation check at [StreamHttpServer.cpp:99-100](../../src/core/stream/StreamHttpServer.cpp#L99-L100) | A line 57 | **✓ exact** |
| `it->cancelled->store(true)` at [StreamEngine.cpp:464-466](../../src/core/stream/StreamEngine.cpp#L464-L466) | A line 132 | **✓ exact** |
| `m_streams.erase(it)` at [StreamEngine.cpp:476](../../src/core/stream/StreamEngine.cpp#L476) | A line 133 | **✓ exact** |
| `clearPieceDeadlines(hash)` at [StreamEngine.cpp:483](../../src/core/stream/StreamEngine.cpp#L483) | A line 135 | **✓ exact** |
| `removeTorrent(infoHash, true)` at [StreamEngine.cpp:491](../../src/core/stream/StreamEngine.cpp#L491) | A line 137 | **✓ exact** |

Internal inconsistencies found in Slice A (not raw misquotes, but contradictions with itself):

- **Slice A "first-piece target" oscillates between 0 ms and 10 ms.** Line 86 says Stremio's URGENT staircase is `0 ms → N × 10 ms` (piece 0 = 0 ms). Line 109 table restates `0 ms → N × 10 ms`. Then line 129 (Q1 Hypothesis 1) says "Stremio's first-piece target (10 ms)" and "A 10 ms deadline signals emergency". The two refer to different Stremio code paths (`get_file_reader` URGENT tier at `handle.rs:305-311` vs `calculate_priorities` CRITICAL HEAD branch at `priorities.rs:180-222`), but Slice A conflates them in the same hypothesis without stating which path it's anchored to. Slice B Q1(b) clarifies: the 10-ms-first-piece is the `calculate_priorities` `distance<5` branch for `priority==1` normal streaming. Slice A's URGENT-tier citation gives first piece = 0 ms. **Slice A needs one-sentence clarification that the hypothesis compares Tankoban's 500 ms head-piece-0 against the 0 ms URGENT-tier deadline (handle.rs), not the 10 ms calculate_priorities value.**

- **Alert-pump cadence: Slice A (line 352-354) asks Agent 4B to confirm; Slice B (line 359) confirms it was 250 ms and flags tightening.** Cross-audit resolution is fine — but Slice A's "Tankoban already has an alert-pump ... the cadence was not confirmed this session" leaves a stale claim dangling in the frozen Slice A file. Integration memo should note Slice B supersedes on this point.

No other citation issues found among the 12 load-bearing Tankoban cites. Stremio Reference citations not re-verified (Slice A / B explicitly note they rely on R21 mtime freeze; fair).

---

## 4. Cross-slice misattributions

Overlaps, mis-routes, and territory issues between Slice A and Slice B:

1. **`/create` duplication.** Slice A cross-cutting (line 356–361) notes "Absence of a /create endpoint" as a Stream-A non-goal. Slice B Orphan surface (line 316–322) re-audits `routes/engine.rs` including `/create` + `create_magnet`. The overlap is mild but should collapse in integration memo — two sources of truth for the same endpoint is cruft waiting to happen.

2. **`set_piece_deadline + set_piece_priority` pairing hypothesis surfaces in both slices with different framing.** Slice A Cross-cutting (line 338–342) flags per-piece priority-7 not re-asserted in `onMetadataReady` head-deadline block; proposes 2-line in-situ fix. Slice B Q1 Divergence table (line 122) says the same thing in passing ("Confirms Slice A cross-cutting finding: head pieces at stream start miss explicit per-piece priority-7"). Slice B correctly cites Slice A but does not deepen — this is the piece-primitive auditor's natural territory and deserved more than a confirming nod. No strict misattribution; just shallow follow-through.

3. **Slice A Q1 Divergence table reaches into Slice B Q3 territory.** Line 110 ("Answers Slice B Q3 for Stremio side: deadlines are re-asserted PER READ, not once — every poll updates"). This is Slice A pre-answering Slice B's central question. Properly flagged as handoff in both slice audits (Slice B line 13 picks it up explicitly). **No misattribution — clean handoff** but worth calling out that if Slice A's read were wrong, Slice B would inherit the error. Slice B confirms independently.

4. **`StreamsItem::adjusted_state` (Slice B line 344-351) is correctly flagged Slice D territory.** But Slice B also notes `StreamProgress.h` + STREAM_ENGINE_REBUILD P0 `schema_version=1` hardening cross-ref — that's Agent 0 motion territory, not Slice D. **Minor mis-route: the schema_version reference belongs in the integration memo, not in Slice B's consumer-side cross-ref.**

5. **`piece_cache.rs` / moka TTI / memory-storage references appear in both slices.** Slice A Cross-cutting "Memory-storage vs disk-backed storage" (line 344-348) and Slice B "Relationship to prior-art" (line 397) reach the same non-goal conclusion. Consistent but duplicated; integration memo should collapse.

6. **Slice B 6th advisory ("bitrate feedback from sidecar")** proposes `setStreamBitrate(hash, bytesPerSec)` on StreamPlaybackController. This touches Slice C (sidecar probe → consumer handoff) semantics — Slice C should co-own this decision. Slice B notes it as "a choice for Agent 4" (per Rule 14) but doesn't explicitly hand off. **Minor: integration memo or Slice C should pick up cross-ownership.**

No misattributions that invert a finding or blame the wrong subsystem. All six above are tractable scope-overlap cleanups for the integration memo.

---

## 5. Dangling questions

Open items that either audit raised and did not close, nor handed off with a clear owner:

1. **`contiguousBytesFromOffset` mutex posture** (Slice A Q1 Hyp. 3) — 3-way decision: `m_mutex` / `QReadWriteLock` / per-stream. No owner assigned; no P2 batch pinned. **Must-close before P2 merges.**

2. **Alert-pump cadence tightening: in-scope for P2 or separate?** (Slice B Cross-cutting line 358–362) — called out as "candidate for separate post-audit commit" but whose commit, when, and bundled with per-piece priority pairing or separate? The progressTick refactor sizing is questioned by me in Gap #3.

3. **Engine-retention post-P6 adoption decision** (Slice A Q2 Hyp. 1) — hypothesis-level; no Hemanth-facing decision teed up. OK per Rule 14 (technical scope), but Agent 0 should decide whether the integration memo mentions it or not.

4. **Single-active-file invariant port** (Slice A Q2 Hyp. 3) — conditional on engine-retention adoption. Chained dangling.

5. **SeekClassifier 5-file split validation** (Slice A Q1 Hyp. 3) — proposed as Congress 5 amendment, not re-confirmed in audit against Slice B's read. Slice B Q1 proposes 4-value enum; the split count (5 files) is not a Slice B cross-check.

6. **300-piece hard cap as P3 compile-time constant** (Slice B Q1 Hyp. 4) — proposed; no cross-check against our libtorrent 2.0.11's own scheduler capacity.

7. **`setStreamBitrate` surface crossing the 12-method API freeze** (see Gap #1). Is this a TorrentEngine surface or StreamPlaybackController surface? Slice B advisory #6 implies the latter; if so, not freeze-breaking. Needs explicit confirmation.

8. **In-situ fix bundling decision** — Slice A flags one fix (per-piece priority-7 pairing, 2-line); Slice B flags two (the above + alert-pump cadence + progressTick refactor, 15–20 lines). Both defer to Agent 0 / Hemanth. Need a bundled-vs-separate call before `/commit-sweep`.

9. **UserScrub tail-deadline-loss repro** (Gap #5). Hypothesis-grade — should either be empirically repro'd or tagged as unverified assumption feeding P3 design.

10. **MAX_STARTUP_PIECES=2 re-verification** (Gap #4) — prior-art pass-through; fresh-read needed.

11. **libtorrent "overdue deadline stays time-critical" semantic** (Slice B Q3 Hyp. 4) — relied on from prior-art cite of libtorrent 2.0.11 docs, not re-verified. Low-risk but noted.

---

## 6. Null-result confirmation

Claims of "no finding" / "no gap" that the adversarial review should cross-check:

- **6 orphan routes in Slice A (`subtitles/system/archive/ftp/nzb/youtube`) surface no Slice-A defect** → confirmed against [StreamEngine.h:15-62](../../src/core/stream/StreamEngine.h#L15-L62) non-goal codification. All six either fall under Agent 4B (Sources) domain, sidecar-side non-goals, or multi-tenant-daemon architecture not applicable to single-tenant Qt desktop. **Null-result stands.**

- **`routes/engine.rs`, `routes/peers.rs`, `local_addon/` surface no substrate gap in Slice B.** `/peers/{hash}` is a Stremio stub (empty Vec) while Tankoban's `peersFor` returns full peer state — **Tankoban is AHEAD here**. `/create` and `local_addon` are multi-tenant concerns. **Null-result stands. `/peers` reversal is a bonus finding — Slice B understates it. Worth an integration memo sentence.**

- **R21 snapshot check-in at both audit entries passed** (Slice A line 11, Slice B line 11) — both audits entered with Stremio Reference mtimes frozen to the motion-authoring baseline. **Stands; neither audit re-checked at audit close (Slice B line 444 explicitly waives). Low-risk in a single-session audit window.**

- **No new HELP.md asks from Slice A or B** — confirmed. Agent 4B's existing asks (pieceFinished signal, peersWithPiece, 12-method API freeze) cover Slice A+B's needs.

- **Slice B surfaces no new axes beyond prior-art's 11** (line 400) — confirmed against the cross-check table at line 395–399. The alert-pump-cadence finding is a deepening of prior-art Axis 1 + Axis 8, not a new axis. **Stands.**

- **Both audits vote P2 / P3 / P4 OPEN with aligned gate-opener rationale.** Slice A line 392-395, Slice B line 409-412 — votes match on P2 (primitive well-specified) and P3 (prioritizer + SeekClassifier shape clear). P4 is only voted in Slice A (deferred to Slice C by Slice B, proper). **Stands — see Section 7 for my concurrence.**

- **No sidecar re-reads this pass** — Slice A line 420 and Slice B line 442 both acknowledge. Prior-art sidecar cites (demuxer.cpp:15-21, video_decoder.cpp:175) carried forward. **Acceptable for Slices A+B; Slice C must re-read sidecar fresh.**

- **No Tankoban compile/run / no web-search this pass** — both audits disclose. **Acceptable for audit-grade work.**

Null-result confirmation: six out of six stands. One upward revision (`/peers` reversal) is an understated finding that should surface in the integration memo.

---

## 7. Phase-gate verdict

Per addendum §ADDENDUM, Congress 6 audits vote P2 / P3 / P4 gate-open. Both Slice A and Slice B vote OPEN on P2 + P3; Slice A also votes OPEN on P4; Slice B properly defers P4 to Slice C.

My adversarial verdict:

### P2 gate (notification-based piece-wait via `StreamPieceWaiter` + Agent 4B's `pieceFinished`):
**OPEN — with two must-close items before scaffold lands.**
- **Must-close 1:** Lock-ordering decision on `contiguousBytesFromOffset` (3-way per Slice A Q1 Hyp. 3) — Agent 4 call under Rule 14. Integration memo must pin.
- **Must-close 2:** Alert-pump cadence tightening scope — in-P2 batch or separate post-audit commit, and the `progressTick` refactor sizing re-examined. Agent 0 call.
- **May-close (not blocking):** Per-piece priority-7 pairing 2-line fix bundled or separate. Agent 0 call.

Primitive shape (`QHash<QPair<hash, pieceIdx>, QList<QWaitCondition*>>` under `QMutex` + QueuedConnection signal from AlertWorker) is cleanly specified across both audits. `pieceFinished` signal verified live at [TorrentEngine.cpp:158-164](../../src/core/torrent/TorrentEngine.cpp#L158-L164).

### P3 gate (Prioritizer + SeekClassifier + peersWithPiece):
**OPEN — with three must-close items before Prioritizer file-split lands.**
- **Must-close 1:** MAX_STARTUP_PIECES value re-verification (Gap #4) — fresh read of `backend/priorities.rs:6-9` at P3 design entry.
- **Must-close 2:** First-piece-target inconsistency in Slice A (0 ms vs 10 ms, see Section 3) — one-sentence clarification before P3 deadline math is frozen.
- **Must-close 3:** UserScrub tail-deadline-loss empirical repro (Gap #5) — either repro it OR re-label Slice B Q3 claim as unverified hypothesis before P3 SeekClassifier design is committed.

Prioritizer shape (single `computePriorities(...)` → `QList<QPair<int, int>>`) + 4-value SeekType enum + 1–2 Hz tick re-assertion + 300-piece cap are well-specified and cross-consistent between Slices A and B.

### P4 gate (sidecar probe escalation):
**OPEN per Slice A (Q3 Hyp. 1) — Slice B properly defers to Slice C.** Agent 3's proposed 3-tier budgets (512 KB / 2 MB / 5 MB) align with Stremio's 3-tier (512 KB / 2 MB / 5 MB). Slice C will re-audit from sidecar side. **My vote is CONTINGENT on Slice C re-confirmation.**

### Overall:
Slices A + B together meet the Congress 6 gate-opening bar with six must-close / may-close items to Agent 0 integration memo. No adversarial-review red flags that would warrant re-opening either audit or rotating auditor. Two mislabels (Section 2), one internal inconsistency (Section 3), and six tractable cross-slice cleanups (Section 4) are all addressable in the integration memo itself without re-auditing.

**Recommendation to Agent 0:** Land the integration memo with the six must-close / may-close items from this review explicitly enumerated and resolved. Do not ratify Congress 6 until:
(a) 12-method API freeze list is pinned in writing against HELP.md Ask 3;
(b) MAX_STARTUP_PIECES and first-piece-target inconsistency are fresh-verified;
(c) `contiguousBytesFromOffset` lock posture is decided;
(d) In-situ fix bundling (per-piece priority, alert-pump cadence, progressTick refactor) is bundled-or-separate-committed with explicit Agent 0 call.

Assistant 2 to review Slices C + D under the same 7-section shape; integration memo consolidates all four slice reviews into a single gate-open proposal for Hemanth Final Word.

---

## Review boundary notes

- I did not re-audit Stremio Reference against Slices A+B's citations — R21 mtime freeze is relied on. Stremio Reference file reads are taken at face value.
- I did not compile or run Tankoban.
- I did not cross-check Slice A or B against the 2026-04-16 prior-art audit line-by-line beyond what both Slices already cross-check.
- I did not produce any fix prescriptions; the review closes with items for Agent 0 integration memo.
- I did not inspect Slice C or D (Assistant 2's scope; not landed yet at review time).
- I did not re-open [agents/congress_archive/2026-04-18_congress6_stremio_audit.md](../congress_archive/2026-04-18_congress6_stremio_audit.md) beyond what both Slices cite.
- I did not verify the 12-method API freeze list against TorrentEngine.h's public surface myself — that's the Agent 0 integration memo's job, and my review flags it as Gap #1.
- I did not perform web-search this pass.
- I did not validate libtorrent 2.0.11 "overdue deadlines stay time-critical" semantic independently; carries through from prior art.
- This review is adversarial but bounded — I looked for holes, not for strict factual errors to overturn the audits' core findings. Both Slice A and B stand as gate-opening deliverables.
