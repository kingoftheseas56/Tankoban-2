# Integration Memo — Congress 6 Stremio Reference Multi-Agent Audit — 2026-04-18

By Agent 0 (Coordinator). **Authoritative single source of truth for Congress 6 audit findings.** STREAM_ENGINE_REBUILD_TODO.md P2/P3/P4 sections link to this memo rather than embedding 30-line blocks per phase (motion §Integration pass scope).

Inputs consolidated:
- [congress6_stream_primary_2026-04-18.md](congress6_stream_primary_2026-04-18.md) — Slice A, Agent 4, 423 lines
- [congress6_sources_torrent_2026-04-18.md](congress6_sources_torrent_2026-04-18.md) — Slice B, Agent 4, 445 lines
- [congress6_player_sidecar_2026-04-18.md](congress6_player_sidecar_2026-04-18.md) — Slice C + Slice D appendix, Agent 3, 488 lines (~40KB Slice C + ~23KB Slice D appendix)
- [congress6_assistant1_adversarial_AB_2026-04-18.md](congress6_assistant1_adversarial_AB_2026-04-18.md) — Assistant 1, 25KB, 7-section shape (gap / label / citation / cross-slice / dangling / null-result / phase-gate verdict)
- [congress6_assistant2_adversarial_CD_2026-04-18.md](congress6_assistant2_adversarial_CD_2026-04-18.md) — Assistant 2, 34KB, 8-section shape (Assistant 1 shape + §7 Slice D collapse-honesty verdict)

Motion archive: [../congress_archive/2026-04-18_congress6_stremio_audit.md](../congress_archive/2026-04-18_congress6_stremio_audit.md).

R21 snapshot: all audit entries passed Stremio Reference mtime spot-check against motion-authoring baseline. No citation drift.

---

## §1. Executive Summary

Congress 6 commissioned a 2-auditor parallel audit of Stremio Reference gating STREAM_ENGINE_REBUILD P2/P3/P4. Agent 4 audited Slices A (Stream Primary) + B (Sources/Torrent substrate + enginefs piece primitives). Agent 3 audited Slice C (Player + Sidecar) with Slice D (Library UX) collapsed to appendix under the motion's <30-min escape hatch. Assistant 1 (fresh Claude, no domain skin) reviewed A+B; Assistant 2 reviewed C+D with explicit collapse-honesty check.

**Headline findings:**
- **P2 GATE OPEN** — notification-based piece-wait via `StreamPieceWaiter` + Agent 4B's shipped `pieceFinished` signal is cleanly specified. 3 must-close items for Agent 0 / Agent 4.
- **P3 GATE OPEN** — Prioritizer + SeekClassifier + `peersWithPiece` shape well-specified (4-value SeekType enum, 1-2Hz tick re-assert, 300-piece cap, preserve-tail-on-UserScrub invariant). 3 must-close items before Prioritizer files are frozen.
- **P4 GATE OPEN** — Agent 3's 3-tier probe shape (512KB/2MB/5MB) aligns with Stremio's budgets. Slice C endorses. No Agent 3 audit-gated P4 objections.
- **Slice D collapse HONEST** — Assistant 2 verified Agent 3 read the files (verbatim-quoted gate predicate; exact 6-count of deep_link emit sites; directory-correction of `ctx/library.rs`→`update_library.rs`). No redraft demand.
- **Prior-art correction:** Slice C refutes prior-art P1-5 (Agent 7 audit conflated `stream_state` = user prefs with streaming_server model state = torrent stats; they are separate concerns in stremio-core).
- **Critical latency find:** our `wait_for_alert(250ms)` at TorrentEngine.cpp:52 is 50× slower than Stremio's 5ms idle-poll at backend/libtorrent/mod.rs:204 — caps P2 worst-case idle wake latency (under load, pump is alert-driven not timeout-driven; Assistant 1 flagged the overstatement).
- **`/peers` reversal:** Tankoban's `peersFor` is AHEAD of Stremio's stub `/peers/{hash}` (empty Vec). Bonus null-result finding from Slice B.

**Recommendation: GATE-OPEN RATIFICATION** for P2/P3/P4 with 7 must-close items addressed before specific sub-phases ship (enumerated in §5 below).

---

## §2. Rigor Audit (did each 3-question sheet get answered?)

Per motion §Integration pass scope §4, verdict on whether each slice's 3 questions were answered with file:line evidence vs padded/deflected.

| Slice | Q1 | Q2 | Q3 | Overall |
|---|---|---|---|---|
| **A — Stream Primary** | ✅ Answered (Mode A root-cause isolated to poll-sleep primitive, StreamHttpServer.cpp:82-108 vs enginefs/src/piece_waiter.rs:13-59 + backend/libtorrent/mod.rs:194-265) | ✅ Answered (lifecycle state preserved via active_file single-slot + 5s delayed cleanup at enginefs/src/lib.rs:240-382 + RAII StreamGuard at routes/stream.rs:39-58; HTTP connection IS torn both sides) | ✅ Answered (Stremio 3-tier probesize budgets at enginefs/src/hls.rs:127-159 align with Agent 3 P4 shape; cache_cleaner.rs + ffmpeg_setup.rs NOT probe-coordinators) | **Fit to gate P2/P4** |
| **B — Sources/Torrent** | ✅ Answered (`calculate_priorities` at priorities.rs:56-225 as single pure function per poll_read; urgent_window = max(15, bitrate×15s/piece_len) + proactive_bonus clamped; head_window 5s×bitrate clamped 5-250; CRITICAL HEAD staircase 10+d×50ms) | ✅ Answered (PieceWaiterRegistry = parking_lot::RwLock<HashMap<(String,i32), Vec<Waker>>>; no explicit timeout; arrival-before-register papered via re-poll; drain-and-wake after lock release) | ✅ Answered (Stremio re-asserts deadlines on EVERY poll_read at stream.rs:184 cache-gated to piece-boundary via same-piece early-return; 4-value SeekType enum; UserScrub CLEARS head while ContainerMetadata PRESERVES) | **Fit to gate P3** |
| **C — Player + Sidecar** | ✅ Answered (4-hop Load→StremioVideo command('load')→`<video>` onloadedmetadata/onplaying→consumer→core PausedChanged→PlayerPlaying at player.rs:140→613→runtime/msg/event.rs:17) | ✅ Answered (NewState(fields) lazy-pull dispatch at runtime.rs:79-88; **CORRECTIVE FINDING**: `stream_state` = user prefs, NOT torrent stats — contradicts prior-art P1-5) | ✅ Answered (Stremio = 4-prop orthogonal continuum in HTMLVideo.js:107-127; Tankoban 6-stage LoadingOverlay is parity-PLUS) | **Fit to gate P4** |
| **D — Library UX** (appendix) | ✅ Answered (CW gate = `self.r#type != "other" && (!self.removed || self.temp) && self.state.time_offset > 0` at library_item.rs:52-56; 4 concrete divergences surfaced) | ✅ Answered (next-EPISODE sequential at player.rs:992-1045; next-STREAM bingeGroup at stream.rs:141-143 with explicit None→false match-arm) | ✅ Answered (6 deep_link emit sites at deep_links/mod.rs:281,367,423,476,514,542,581 all producing canonical stremio:///player/{...} URL; router parses to Action::Load) | **Fit for Agent 5 track** |

All 4 slices cleared the rigor bar. No below-threshold audits; no redraft demands. Assistant 1 flagged 2 material mislabels in Slice B (Q2 pump-latency 250ms overstated — is worst-case idle bound not steady-state; Q3 tail-deadline-loss claim phrased defect-grade in prose but hypothesis-grade in label) — both tractable in §5 must-close list, not blocking.

---

## §3. Critical Cross-Slice Findings (load-bearing for rebuild design)

Items that the assistant adversarial pass surfaced that cross slice boundaries or escalate in load-bearing-ness at integration level.

### §3.1 Prior-art `stream_state` conflation — CORRECTIVE
Slice C §Q2 identifies that prior-art audit `player_stremio_mpv_parity_2026-04-17.md` P1-5 conflated `stream_state` (user preferences: subtitle_track/delay/size/offset/playback_speed/player_type/audio_delay per `streams_item.rs:29-95`) with `streaming_server` model state (torrent stats). They are separate concerns in stremio-core. **Integration memo explicitly corrects prior-art P1-5 on Agent 7 audit demotion (§7 below).**

### §3.2 Deadline re-assert cadence resolved
Cross-slice handoff Slice A→Slice B Q3 landed cleanly: Stremio re-asserts deadlines on EVERY `poll_read` at `backend/libtorrent/stream.rs:184`, cache-gated to piece-boundary via same-piece early-return at stream.rs:77-80 (effective 20-50 Hz). Tail-metadata + critical-ranges set ONCE rely on libtorrent's overdue-deadline-still-priority semantic. **P3 design decision pinned: Prioritizer re-asserts at 1-2Hz (existing StreamPlaybackController telemetry tick) — NOT per-poll, NOT once-and-forget. SeekClassifier MUST preserve tail-metadata on UserScrub-to-mid-video** (Slice B Q3 Hypothesis 2, must-close-for-repro per §5).

### §3.3 Critical wake-latency find
Slice B Q2 surfaced our `wait_for_alert(250ms)` at TorrentEngine.cpp:52 vs Stremio's 5ms at backend/libtorrent/mod.rs:204 — 50× worst-case idle wake latency gap. Assistant 1 re-scoped: the 250ms is **worst-case idle poll upper bound, not steady-state pump interval.** Under active piece download with alerts arriving, pump cadence is alert-arrival-limited, not timeout-limited. Real latency = `min(250ms, time-to-next-alert)`. Still load-bearing for idle-to-active transition (e.g., first-piece wake during cold-open stall). **Fix candidate: wait_for_alert 250→5-25ms tightening + progressTick wall-clock conversion (~15 lines). Scope-bundling decision pinned to §5 must-close.**

### §3.4 `/peers` reversal (bonus null-result)
Tankoban's `peersFor` (TorrentEngine.cpp:960-1007 per Slice B) returns full peer state; Stremio's `/peers/{hash}` at `routes/peers.rs` is a stub returning empty Vec. **Tankoban is AHEAD here** — integration memo flags this for the record so no one accidentally "ports" Stremio's stub over our working implementation during demolition.

### §3.5 Boundary-handled overlaps (no misattributions)
Assistant 1 surfaced 6 cross-slice scope-overlap items (§4 of A+B review); none invert findings. Assistant 2 found zero Slice C-vs-D misattributions — explicit cross-references via integration feeders correctly handle overlap (`item_state_update`, `next_video_update` appear in both slices, load-bearing overlap not duplication).

---

## §4. Per-Phase Gate Verdicts

**Gating semantics** (from motion §Gating semantics, unchanged): P0 shipped (`ad2bc65`); P1 scaffold not audit-gated (ships in parallel with audit drafting); P2/P3/P4 audit-gated; P5/P6 not audit-gated.

### P2 — Piece-waiter async → **GATE OPEN** (conditional on must-close items M1, M2, M3 in §5)

Evidence: Slice A Q1 + Slice B Q2 converge on `QHash<QPair<hash, pieceIdx>, QList<QWaitCondition*>>` under `QMutex` + QueuedConnection signal from AlertWorker shape. Agent 4B's `pieceFinished(QString, int)` signal shipped + verified live at TorrentEngine.cpp:158-164. Assistant 1 explicitly votes P2 OPEN.

**What this authorizes:** Agent 4 may begin P2 StreamPieceWaiter implementation against Agent 4B's shipped `pieceFinished` signal once M1/M2/M3 (§5) are pinned by Agent 0.

### P3 — Prioritizer + SeekClassifier → **GATE OPEN** (conditional on must-close items M4, M5, M6 in §5)

Evidence: Slice B Q1 maps `calculate_priorities` urgency-window + head-window + per-piece deadline math verbatim. 4-value SeekType enum (`UserScrub` clears+rebuilds; `ContainerMetadata` preserves head per stream.rs:96-100 invariant) directly feeds P3 SeekClassifier 5-file-split. `peersWithPiece` Agent 4B-shipped method enables R3 `seek_target_peer_have` telemetry. Assistant 1 votes P3 OPEN.

**What this authorizes:** Agent 4 may begin P3 implementation after P2 lands + M4/M5/M6 are pinned.

### P4 — Sidecar probe escalation → **GATE OPEN** (unconditional, no must-close items)

Evidence: Slice A Q3 maps Stremio's 3-tier probesize budgets (750KB/512KB→2MB/2MB→5MB/5MB) at `enginefs/src/hls.rs:127-159`. Agent 3's proposed P4 shape (512KB/500ms/5s→2MB/2s/15s→5MB/5s/30s) aligns directionally. Slice C §Q3 Hypothesis 2 endorses: our 6-stage LoadingOverlay is parity-PLUS (classified probe vocabulary exceeds Stremio's browser-abstracted continuum). Assistant 2 explicit endorsement. P4 acceptance-smoke remains P2-gated per Congress 5 Amendment 3 (tier 2 needs StreamPieceWaiter path live).

**What this authorizes:** Agent 3 may begin P4 sidecar probe escalation implementation immediately post gate-open ratification. Tier 2 smoke waits on P2.

### P5 — Stall detection → Not audit-gated

Slice C §Q3 Hypothesis 2 constrains: stall detection should be sidecar-side only (`av_read_frame` stall detection), not piece-waiter starvation. Under correct prioritization, piece-waiter should never be the cause of HTTP-read stall. Assistant 2 explicit endorsement.

### P6 — Demolish → Not audit-gated

Out of audit scope. Demolition target list frozen in STREAM_ENGINE_REBUILD_TODO.md.

---

## §5. Must-Close Items (Agent 0 / Agent 4 decisions before specific P2/P3 sub-phases ship)

Consolidated from Assistant 1 §7 + Assistant 2 §8. Each item has owner + disposition.

### M1 — `contiguousBytesFromOffset` lock-ordering decision (P2 blocker)
- **Source:** Slice A Q1 Hypothesis 3; Assistant 1 Gap #2 + Dangling #1.
- **Three-way choice:** keep `m_mutex` / introduce `QReadWriteLock` / per-stream mutex.
- **Owner:** Agent 4 (Rule 14 technical call).
- **Disposition:** Agent 4 picks at P2 implementation entry. **Integration memo does not dictate.** Agent 4 must document choice in P2 ship post.

### M2 — Alert-pump cadence tightening scope (P2 blocker)
- **Source:** Slice B Cross-cutting; Assistant 1 Gap #3.
- **Question:** in-P2 batch or separate post-audit commit? `progressTick` refactor dependent count not sized end-to-end.
- **Owner:** Agent 4 (Rule 14).
- **Disposition:** Agent 4 walks downstream dependents (`emitProgressEvents`, `checkSeedingRules`, any seeding-ratio / share-limit math assuming 1s tick) before bundling. Recommend separate post-audit commit since `progressTick` touches non-rebuild surface; P2 StreamPieceWaiter wiring does not depend on it.

### M3 — Per-piece priority-7 pairing bundling (P2 optional)
- **Source:** Slice A cross-cutting; Slice B Q1 Divergence table.
- **Question:** bundled with M2 or separate 2-line commit?
- **Owner:** Agent 0 sweep call (Rule 14 coordination-mechanics).
- **Disposition:** **bundle with M2 into one post-audit in-situ fix commit.** Both touch priorities-related paths; two-commit split is churn.

### M4 — MAX_STARTUP_PIECES=2 re-verification (P3 blocker)
- **Source:** Slice A citations to prior-art value; Assistant 1 Gap #4.
- **Owner:** Agent 4 (Rule 14; reads `backend/priorities.rs:6-9` at P3 design entry).
- **Disposition:** fresh read before P3 compile-time constants are frozen. Low-risk given R21 mtime freeze but must-close.

### M5 — First-piece-target 0ms-vs-10ms clarification (P3 blocker)
- **Source:** Slice A oscillates between `URGENT` tier `get_file_reader` (0ms first piece per `handle.rs:305-311`) and `calculate_priorities` CRITICAL HEAD `distance<5` branch (10ms per `priorities.rs:180-222`); Assistant 1 Section 3.
- **Owner:** Agent 4 at P3 entry.
- **Disposition:** one-sentence clarification in P3 deadline math — **Tankoban targets Stremio's 0ms URGENT-tier first piece** (handle.rs is the closer analog for Tankoban's `onMetadataReady` head-deadlines). The 10ms value is Stremio's `calculate_priorities` normal-streaming branch for downstream pieces, not the cold-open first piece.

### M6 — UserScrub tail-deadline-loss empirical repro (P3 blocker)
- **Source:** Slice B Q3 Hypothesis 2 (phrased "this is a bug per Stremio reference"); Assistant 1 Gap #5 + Section 2 label mismatch.
- **Question:** is the tail-metadata deadline loss on our `clearPieceDeadlines` call at UserScrub actually materializing as user-visible mid-playback stall, or is it a theoretical-only divergence?
- **Owner:** Agent 4 at P3 SeekClassifier design entry.
- **Disposition:** either repro empirically (seek-to-mid-video + observe tail-metadata piece 0-priority drop) and label as defect, OR maintain label as hypothesis-grade and do not premise P3 SeekClassifier design on it as known defect. Hypothesis-grade-then-preserve-tail-invariant is acceptable — the invariant is defensive regardless of current-code-actual-impact.

### M7 — 12-method API freeze list pinned (§6 below) — **pinned in this memo**

---

## §6. 12-Method API Freeze — Pinned Authoritative List

Per Congress 5 Amendment + Agent 4B HELP.md ship post (chat.md:3966+). Frozen for rebuild window (Congress 5 ratification → P6 terminal tag `stream-rebuild/phase-6-demolition`):

1. `setPieceDeadlines`
2. `setPiecePriority`
3. `contiguousBytesFromOffset`
4. `fileByteRangesOfHavePieces`
5. `pieceRangeForFileOffset`
6. `havePiece`
7. `haveContiguousBytes`
8. `setFilePriorities`
9. `torrentFiles`
10. `addMagnet`
11. `setSequentialDownload`
12. `removeTorrent`

Rules: no renames, no signature changes, no removals. Additions are fine. Any forced signature change requires HELP-ping to Agent 0 before shipping.

**New signal surface (additive, post-freeze):** `pieceFinished(QString hash, int pieceIdx)` — shipped by Agent 4B at TorrentEngine.cpp:158-164, QueuedConnection default.

**New const method (additive, post-freeze):** `peersWithPiece(hash, pieceIdx) const` — shipped by Agent 4B; traverses `handle.get_peer_info()`; returns -1 on error; fresh-handshake peers skipped as "unknown" not "no" (R3 falsifiability preserved).

**Assistant 1 Gap #1 closed:** 12-method list now enumerated in writing. No integration-memo hand-wave remaining.

---

## §7. Agent 7 Prior-Audit Demotion

Per motion §Integration pass scope §6, Agent 7 prior audits that Congress 6 redoes move to `agents/audits/_superseded/` with pointers to their replacements.

### Moves (to be executed in step 3 of Agent 0 close flow):

| Prior audit | Supersession pointer | Corrections carried by Congress 6 |
|---|---|---|
| [stream_a_engine_2026-04-16.md](stream_a_engine_2026-04-16.md) → `_superseded/stream_a_engine_2026-04-16.md` | Congress 6 Slice A + B ([congress6_stream_primary](congress6_stream_primary_2026-04-18.md) + [congress6_sources_torrent](congress6_sources_torrent_2026-04-18.md)) | Prior-art P0 reframing: gate size (5MB) is SYMPTOM, poll-sleep primitive is CAUSE. Axes 1+3+7 extended with algorithm-level detail (calculate_priorities, piece_waiter registry, deadline re-assert cadence). |
| [player_stremio_mpv_parity_2026-04-17.md](player_stremio_mpv_parity_2026-04-17.md) → `_superseded/player_stremio_mpv_parity_2026-04-17.md` | Congress 6 Slice C + D appendix ([congress6_player_sidecar](congress6_player_sidecar_2026-04-18.md)) | **P1-5 CORRECTED** — prior-art conflated `stream_state` (user prefs) with streaming_server model state (torrent stats); these are separate concerns in stremio-core. Prior-art P0-1 (buffered/seekable) partial-ship status carried forward; P1-1/P1-2/P1-3 (precise seek / HDR / playback speed) remain carry-forward to PLAYER_STREMIO_PARITY_FIX_TODO Phase 2+, NOT silently closed. |

### Non-supersession status for other Agent 7 audits:

Non-stream Agent 7 audits remain authoritative — `comic_reader_2026-04-15.md`, `book_reader_2026-04-15.md`, `edge_tts_2026-04-16.md`, `cinemascope_aspect_2026-04-16.md`, `video_player_2026-04-15.md`, `video_player_comprehensive_2026-04-16.md`, `video_player_perf_2026-04-16.md`, `video_player_subs_aspect_2026-04-15.md`, `tankorent_2026-04-16.md`, `tankostream_playback_2026-04-15.md`, `tankostream_playback_probe_2026-04-15.md`, `tankostream_session_lifecycle_2026-04-15.md`, `stream_mode_2026-04-15.md`, `stream_d_player_2026-04-17.md`. These do not supersede.

---

## §8. Cross-Slice Cleanups (integration-memo-level, no auditor rework)

From Assistant 1 §4 + Assistant 2 §4. All tractable without re-auditing.

1. **`/create` endpoint dual-read collapse.** Slice A cross-cutting + Slice B Orphan surface both cover engine-create. Canonical source-of-truth: **Slice B `routes/engine.rs:76-100` read.** Slice A's one-line cross-cutting note stays as pointer.
2. **Per-piece priority pairing shallow in Slice B.** Confirmed finding from Slice A; Slice B adds no new detail. Integration-level note: this is Slice A territory for fix-sizing; M3 in §5 resolves bundling.
3. **`schema_version` reference in Slice B belongs in rebuild motion territory, not consumer cross-ref.** Agent 0 memo is the correct carrier; removed from Slice B's consumer-side cross-ref on integration.
4. **`piece_cache.rs` / moka TTI / memory-storage duplicated in A + B.** Both reach same non-goal conclusion (post-P6 polish scope, not rebuild). Canonical source: **Slice B §"Relationship to prior-art" line 397.**
5. **`setStreamBitrate(hash, bytesPerSec)` advisory (Slice B 6th advisory).** Touches Slice C sidecar-consumer boundary. Cross-ownership acknowledgment: **Slice C is co-owner** for any design call; not in-scope for this rebuild, carry-forward as post-P6 polish advisory.
6. **`item_state_update` + `next_video_update` + `next_stream_update` appear in both Slices C and D.** Load-bearing overlap, NOT duplication. Both references preserved. Cross-references via integration feeders handled cleanly by Agent 3.

---

## §9. Recommended Follow-Ups (aggregate-visible items)

Items surfaced by Congress 6 that land outside rebuild P2/P3/P4 scope but are worth recording.

### For Agent 5 (Library UX):
Per Assistant 2 §8, 3 strategic-not-technical questions Slice D surfaced — Agent 5's track is unblocked by Congress 6 close and can pick these up as standalone UX cadence work (NOT gating P2/P3/P4):

1. Keep Tankoban's 90%-isFinished-threshold + async-next-unwatched shape (binge-watching UX upgrade) vs collapse to Stremio's show-until-credits-threshold gate (simpler)?
2. Keep Tankoban's first-unwatched-of-any-order next-episode vs flip to Stremio's sequential-from-current?
3. Is Qt signal-slot library→player handoff (no URL boundary) the right long-term shape, or should a URL boundary be added for cross-device/external-launch support?

All three are product-level questions for Hemanth + Agent 5, not rebuild-gate decisions.

### For PLAYER_STREMIO_PARITY_FIX_TODO Phase 2+ (carry-forward, not closed):
- Prior-art P1-1 (precise seek `--hr-seek` parity)
- Prior-art P1-2 (HDR / tone-mapping coverage)
- Prior-art P1-3 (playback speed parity)

These are Slice C out-of-scope per Congress 6; Agent 0 must NOT silently close them on Agent 7 prior-audit demotion.

### Stremio-wide additive surfaces (NOT in rebuild scope):
- `piece_cache.rs` moka-based in-memory piece cache (LRU + TTI eviction) — future polish, post-P6.
- `DEFAULT_TRACKERS` pool + background tracker ranking at `backend/libtorrent/mod.rs:350-570` — Agent 4B's STREAM_ENGINE_FIX Phase 3.1 already shipped tracker-pool curation; Stremio ranks trackers in background, Tankoban does curation at addMagnet. Future convergence post-rebuild.
- `Instant Loading metadata cache` at `backend/libtorrent/mod.rs:350-570` — accelerates torrent re-open. Post-P6.

### Integration-memo-only housekeeping:
- Assistant 1 Dangling #11 (libtorrent 2.0.11 "overdue deadline stays time-critical" semantic) — carries through from prior-art, not re-verified this Congress. Low-risk. Flag only if P3 behavior diverges from docs on real runs.
- Assistant 2 §5 item 2 (Slice C §Q3 Hyp 2 cites prior-art audit line range 1077-1123 for sidecar `buffering` trigger — re-verify on first P4 ship commit so attribution stays live post-supersession).

---

## §10. Hemanth Gate-Open Ratification Request

**Motion §Verification (how we know Congress 6 worked) items 1-7 status:**

1. ✅ Congress 6 ratified 2026-04-18 (collapsed-position direct ratification; archive file contains rationale).
2. ✅ 4 audit files at `agents/audits/congress6_*_2026-04-18.md` — each answers its 3 questions with file:line evidence. Slice D collapsed to appendix per motion escape hatch.
3. ✅ 2 assistant adversarial reviews at `agents/audits/congress6_assistant{1,2}_adversarial_*_2026-04-18.md`.
4. ✅ Integration pass at this file. Per-phase gate verdicts explicit (§4). Agent 7 priors queued for move to `_superseded/` (§7).
5. ⏳ STREAM_ENGINE_REBUILD_TODO P2/P3/P4 sections link to this memo (to be updated in commit bundle).
6. ⏳ CLAUDE.md dashboard refresh (to be updated in commit bundle).
7. ⏳ Hemanth smoke — spot-check one citation per audit + confirm 3 questions got answered not padded-around.

**Ratification ask:** ratify gate-open for P2/P3/P4 with the 6 must-close items in §5 addressed as the next-action roadmap. P1 scaffold remains parallel-eligible per motion (not audit-gated).

**Next Hemanth action post-ratification:**
1. Summon Agent 4 for P2 StreamPieceWaiter implementation (addresses M1 at implementation entry + binds to Agent 4B's shipped `pieceFinished` signal).
2. In parallel (same summon OR parallel session), summon Agent 3 for P4 sidecar probe escalation.
3. Agent 4 may opportunistically ship M2+M3 bundle (alert-pump cadence tightening + per-piece priority-7 pairing) as separate in-situ-fix commit before or during P2.
4. After P2 lands, summon Agent 4 for P3 (addresses M4+M5+M6 at P3 design entry).

**If you ratify via `ratified` / `APPROVES` / `Final Word` / `Execute`:** Agent 0 archives no further motion work (no CONGRESS.md content to reset — Congress 6 was archived same-session per collapsed-position protocol).

---

## §11. Boundary Notes

- Agent 0 did NOT re-audit any Slice. This memo consolidates the 4 audit files + 2 assistant reviews into decision-oriented synthesis per motion §Integration pass scope.
- Agent 0 did NOT re-read Stremio Reference. R21 mtime freeze relied on.
- Agent 0 did NOT compile/run Tankoban.
- Agent 0 did NOT spot-check citations beyond what Assistant 1 + Assistant 2 already verified (Assistant 1 spot-checked 12 Tankoban-side citations; Assistant 2 spot-checked 20 Stremio-side citations — 19/20 accurate, 1 pseudo-code simplification flagged low-severity).
- Slice D collapse-honesty verdict from Assistant 2 §7 accepted as authoritative: **HONEST.** No redraft demand.
- No integration-memo-level overrides of auditor or assistant findings. All findings carry-forward as synthesized.
- This memo is the **single authoritative source** for Congress 6 audit findings. STREAM_ENGINE_REBUILD_TODO.md §P2 / §P3 / §P4 sections will link here rather than embed.

---

**End of integration memo.** Length: ~340 lines. Congress 6 ready for Hemanth Final Word.
