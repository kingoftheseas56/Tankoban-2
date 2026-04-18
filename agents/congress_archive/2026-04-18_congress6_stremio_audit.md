# Congress 6 — Stremio Reference Multi-Agent Audit (ARCHIVED)

**Archived:** 2026-04-18 by Agent 0. Ratified same-session by Hemanth via collapsed-position direct ratification (summon-chain framing — Hemanth structured the post-motion session as `Summon 1: Agent 0 authors motion; Summon 2: ...`, treating motion-shape as pre-approved and moving directly to audit execution). Positions from Agents 3 / 4B / 5 / 4 were not solicited — Hemanth exercised Rule 14 decision authority to approve the motion shape as drafted. The revised addendum at `C:\Users\Suprabha\.claude\plans\i-want-you-to-cosmic-newell.md` §ADDENDUM had already passed Assistant 1 + Assistant 2 adversarial review before motion authoring, which substituted for the usual domain-agent position pass.

---

## CONGRESS 6 — STATUS: RATIFIED 2026-04-18 (positions collapsed)
Opened by: Agent 0 (Coordinator)
Date opened + ratified: 2026-04-18 (same-session)

## Motion

**2-auditor parallel audit of Stremio Reference + assistant-adversarial review + redo of Agent 7 stream audits; gates STREAM_ENGINE_REBUILD P2/P3/P4.**

Commission a four-slice audit of Stremio Reference (`C:\Users\Suprabha\Downloads\Stremio Reference\`) distributed across 2 Claude domain-agent sessions (Agent 4 × Slices A+B, Agent 3 × Slices C+D), followed by 2 Claude-assistant adversarial reviews (no domain skin in the game), followed by an Agent 0 integration memo that is the single authoritative source of audit findings. Output opens the P2 / P3 / P4 gates on `STREAM_ENGINE_REBUILD_TODO.md`. Agent 7's prior stream audits (`stream_a_engine_2026-04-16.md` substrate + `player_stremio_mpv_parity_2026-04-17.md` player) are redone fresh by the domain-agent auditors (as prior-art input, not as authority) and are demoted to `agents/audits/_superseded/` on integration close.

The motion collapses what the original addendum drafted as a 4-auditor + cross-review + integration ceremony (10-11 day calendar, 10 Hemanth summons) into a 2-auditor + assistant-adversarial + integration shape (4-5 day calendar, ~6 Hemanth summons). The structural changes reflected in this motion came out of Assistant 1 + Assistant 2 adversarial review of the original draft and are listed in the Pre-Brief revision banner.

## Scope

**IN scope:**
- 4 slices against Stremio Reference (A = Stream Primary routes + lifecycle + probe HTTP surface; B = Sources/Torrent substrate + enginefs piece primitives including `priorities.rs` + `piece_waiter.rs` + `piece_cache.rs` + `engine.rs` + `backend/libtorrent/*.rs` + `bindings/libtorrent-sys/`; C = Player state machine + Sidecar with `player.rs` focused on `Action::Load:140`, `Action::Unload:317`, `PausedChanged:613`, `NextVideo:649`, `Ended:690` plus `item_state_update:941` + `stream_state_update:967` binding context; D = Library UX consumer with explicit collapse-to-C-appendix escape hatch if 3-question sheet answers in < 30 min).
- Redo of 2 existing Agent 7 stream audits: `stream_a_engine_2026-04-16.md` (folded into Slice A) + `player_stremio_mpv_parity_2026-04-17.md` (folded into Slice C). Read as prior-art input, not as authority.
- 3-question pre-specification sheets per slice (below) as the rigor floor. Audit length is whatever each sheet's questions require — honest "no gap found" beats padded walls-of-text.
- 2 assistant-adversarial reviews replacing reciprocal domain-agent cross-review.
- Agent 0 integration memo at `agents/audits/congress6_integration_2026-04-XX.md` — authoritative; STREAM_ENGINE_REBUILD_TODO P2/P3/P4 sections link to this memo rather than embedding 30-line blocks.
- Orphan routes from Slice A re-scoped explicitly in: `subtitles.rs`, `system.rs`, `archive.rs`, `ftp.rs`, `nzb.rs`, `youtube.rs` (6 route files previously unassigned in the original draft).

**OUT of scope:**
- Fix prescription in any audit file. Audits remain observation-grade per GOVERNANCE Trigger C. In-situ trivial fixes (1-line obvious mistakes found during read) MAY be landed in a separate commit after the audit drops, tagged accordingly — commit-boundary separation, not tool-access prohibition.
- Compiling or running Tankoban. Audits are read-only.
- Re-auditing non-stream Agent 7 work (`comic_reader_2026-04-15.md`, `book_reader_2026-04-15.md`, `edge_tts_2026-04-16.md`, etc. stay authoritative).
- P0 / P1 / P5 / P6 rebuild phases (not audit-gated — see Gating section of Pre-Brief).
- Library UX independent audit IF Slice D's 3 questions answer in < 30 min (then D collapses into a 1-paragraph Slice C appendix; Assistant 2 checks the collapse was honest not lazy).
- Codex / Agent 7 invocation during the audit window. Agent 7 is unavailable until next week per Hemanth; return-timing handoff contract covered in Pre-Brief.
- Duplicated audit-finding blocks in STREAM_ENGINE_REBUILD_TODO.md (memo is authoritative; TODO links to memo §P2 / §P3 / §P4).

## Pre-Brief

**Required reading before posting a position:**

1. **Primary plan + addendum:** `C:\Users\Suprabha\.claude\plans\i-want-you-to-cosmic-newell.md` — full rebuild plan. The addendum block starting line 269 (`# ADDENDUM — Congress 6 Multi-Agent Audit Plan`) is the authoritative source for this motion. Revision banner at line 271 lists what changed from the original draft after Assistant 1/2 adversarial review surfaced: cross-review rotation was reciprocal not rotated (replaced with assistant-adversarial pass); 300-450 line audit floor was a Codex-shaped quota (dropped; replaced with 3-question sheets); Slice C event names were wrong (corrected to real events at `runtime/msg/event.rs:17-29`); Slice B under-covered its domain by ~2.3K LOC (rescoped); 6 orphan routes unassigned (routed); 4-auditor ceremony over-engineered (collapsed to 2-auditor); TODO/memo duplication removed.
2. **Post-Congress-5 state:** `agents/congress_archive/2026-04-18_stream_engine_rebuild.md` — the rebuild plan as ratified. Amendments 1-5 applied to TODO. R5 falsified (enginefs accessible). R11 reframed (Stremio = libtorrent-rasterbar via `libtorrent-sys`, not WebTorrent — direct semantic port viable).
3. **Master TODO:** `STREAM_ENGINE_REBUILD_TODO.md` at repo root — 6 phases P0-P6. P0 shipped `ad2bc65` (StreamProgress schema_version=1). P1 scaffold runs in parallel with audit drafting (not audit-gated). P2 / P3 / P4 are audit-gated by this Congress.
4. **Audit template + shape exemplars:**
   - `agents/audits/README.md` — audit template and Trigger C rules (loosened per revision: trivial in-situ fix notes allowed at commit-boundary separation).
   - `agents/audits/stream_a_engine_2026-04-16.md` — Slice A shape exemplar + redo target.
   - `agents/audits/player_stremio_mpv_parity_2026-04-17.md` — Slice C shape exemplar + redo target.
5. **HELP.md** — Agent 0 → Agent 4B asks are still open (pieceFinished signal hard-required per Amendment 1; peersWithPiece hard-required; 12-method API freeze). Not gated by this Congress; 4B proceeds independently.
6. **Governance:** `agents/GOVERNANCE.md` Rule 14 (domain agents decide technical questions; Hemanth decides product/UX/strategic) + Trigger C rules for audit/implementation separation.

**3-question pre-specification sheets (written here, locked before any auditor wakes):**

### Slice A — Stream Primary (Agent 4)
1. **Mode A root-cause question:** On a 1000-seed torrent at `metadata_ready`, what exact call sequence does Stremio's `stream.rs` make between piece-0 request and first HTTP byte sent to client? Where does it wait on pieces, and what mechanism (sync/async) does the wait use? How does this differ from our `StreamHttpServer.cpp:82 waitForPieces` 15s poll-sleep?
2. **Lifecycle/Replacement question:** How does Stremio's `state.rs` handle source-switch mid-stream (equivalent of our `stopStream(Replacement)`) without tearing down the HTTP connection to an in-flight player? What state survives, what resets?
3. **Probe/HLS coordination question:** How does `hls.rs` + `cache_cleaner.rs` + `ffmpeg_setup.rs` coordinate probe escalation with HTTP Range serving? Does Stremio's probe reader block on pieces the same way our sidecar probe does, or does it partial-probe on already-available bytes only?

### Slice B — Sources/Torrent + enginefs piece primitives (Agent 4)
1. **Mode B core question:** In `enginefs/src/backend/priorities.rs`, what is the EXACT algorithm for (a) urgency window sizing, (b) per-piece priority value assignment within the window, (c) window-slide-on-seek semantics? Line-by-line function flow.
2. **P2 piece-waiter question:** In `enginefs/src/piece_waiter.rs`, how does the registry handle (a) a waiter timing out, (b) a piece arriving before the waiter registers, (c) multiple waiters on the same piece? Are there lock-ordering subtleties we would miss in our Qt port?
3. **Mode B structural question:** Does Stremio call `set_piece_deadline` ONCE per seek (like our Phase 2.6.3), or does it re-assert on a tick? If once, how does it handle deadline expiry without libtorrent dropping the piece from time-critical tracking? This decides our P3 design between tick-re-assert and long-deadline-once.

### Slice C — Player + Sidecar (Agent 3)
1. **Probe → play flow question:** Trace `Action::Load` at `player.rs:140` through to the first `PlayerPlaying` event emit at `runtime/msg/event.rs:17`. What state transitions and side-effects happen, and where is the sidecar-probe-equivalent triggered? Does Stremio's equivalent of our LoadingOverlay stages have a state machine we should match?
2. **IPC surface question:** In `stremio-core-web/src/model/serialize_player.rs`, how is `stream_state` surfaced to the consumer — eagerly serialized on every change, lazily on request, or delta-only? This tells us whether our `contiguousHaveRanges` polling cadence matches Stremio's semantic cadence.
3. **State classification question:** In `stremio-video-master/src/StremioVideo/StremioVideo.js` + `HTMLVideo.js`, how does the consumer distinguish mid-probe vs paused-for-cache vs playing? Three discrete states or a continuum? Informs our classified LoadingOverlay.

### Slice D — Library UX (Agent 3; collapses to Slice C appendix if low-signal)
1. **Continue Watching computation question:** In `stremio-core/src/models/ctx/library.rs` (or equivalent), how is the "continue watching" list computed — watched-percentage threshold, last-position recency, bingeGroup affinity, or a combination? Line anchors.
2. **Next-episode detection question:** In `addon_transport/`, how does the library consume addon-provided streams for next-episode detection? Is this the `bingeGroup` mechanism, or a separate path?
3. **Library → player handoff question:** Is there a single library-card → stream-selection → player-load flow we should match, or multiple entry paths? Does selection flow through `Action::Load` at `player.rs:140` or through a separate `ctx/library.rs` dispatcher?

**Collapse rule:** if Slice D questions answer in < 30 min of reading, Slice D becomes a 1-paragraph appendix of the Slice C audit file. Assistant 2 verifies the collapse was honest (not laziness). "No gap found" is a valid audit outcome.

## How This Congress Works

**Position order:** 3 / 4B / 5 in any order (non-overlapping domains — consumer / substrate-upstream / library UX consumer); **Agent 4 last** as domain master synthesizing others' concerns. Same last-position convention as Congress 5.

**What each agent must confirm in their position:**

| Agent | Required confirmations |
|---|---|
| **Agent 3** (Slice C + D auditor) | (a) Will write Slice C + D audits against the 3-question sheets above; (b) understands Slice D collapse-to-appendix escape hatch; (c) will read `player_stremio_mpv_parity_2026-04-17.md` as prior-art input, not authority; (d) commits to observation-grade audit shape (Observed / Reference / Hypothesis separation, dual file:line citations, no fix prescription); (e) any concerns about the 3-question sheets or scope as drafted. |
| **Agent 4B** (substrate-upstream perspective) | (a) Status of HELP.md asks (pieceFinished signal + peersWithPiece + 12-method API freeze) — can 4B ship these on the Congress 6 timeline?; (b) review of Slice B scope — any gaps in the `priorities.rs` + `piece_waiter.rs` + `piece_cache.rs` + `engine.rs` + `backend/libtorrent/*.rs` + `bindings/libtorrent-sys/` file list from your TorrentEngine-domain perspective; (c) any concerns about the 3-question sheets. |
| **Agent 5** (library UX consumer perspective) | (a) Review of Slice D scope — does the 3-question sheet §D reasonably capture library UX concerns? Anything missing from the library-consumer perspective?; (b) confirm the collapse-to-appendix escape hatch is acceptable if Library UX gap is genuinely ~zero (Assistant 1's original flag); (c) any concerns. |
| **Agent 4** (Slice A + B auditor, domain master, posts LAST) | (a) Will write Slice A + B audits against the 3-question sheets above; (b) will read `stream_a_engine_2026-04-16.md` as prior-art input, not authority; (c) commits to observation-grade audit shape; (d) synthesis of 3/4B/5's concerns; (e) any amendment requests to 3-question sheets / scope / slice boundaries before audit execution begins. |

**Auditor role scoping:** Agent 4 as Slice A + B auditor retains P1-P6 executor role on the rebuild itself. Assistant-adversarial review pass (rather than domain-agent cross-review) keeps the 5-hat problem from recurring — assistant has no domain skin in the game, so catches blind-spots by lack of shared mental model.

**Positions do NOT need to draft in strict sequence.** 3/4B/5 domains are non-overlapping for this motion; parallel drafting is fine. Agent 4 must post last.

**Amendment mechanism:** any position may propose amendments to the 3-question sheets, slice scope, or agent assignments. Amendments with explicit file:line citation supporting the change carry more weight than generic pushback. Agent 0 synthesizes. Hemanth ratifies in Final Word.

**Gating semantics (repeat from addendum for on-page reference):**

| Phase | Audit-gated? | Gated by |
|---|---|---|
| P0 (shipped `ad2bc65`) | No | — |
| P1 Scaffold | No | — ships in parallel with audit-drafting |
| P2 Piece-waiter async | YES | Slice A + B (via Agent 4's audits + Assistant 1 review) |
| P3 Prioritizer + seek-type | YES | Slice B primarily + Slice C indirect |
| P4 Sidecar probe escalation | YES | Slice C (Stremio probe-retry patterns) |
| P5 Stall detection | No | — |
| P6 Demolish | No | — |

**Timeline (4-5 working days):**
- Day 1 AM: Agent 0 Congress 6 motion (this file)
- Day 1 PM: 4 positions drafted (3/4B/5 parallel-ish; Agent 4 last)
- Day 2 AM: Agent 0 synthesis + Hemanth Final Word → archive + reset
- Day 2-3: Agent 4 Slice A audit, Agent 4 Slice B audit, Agent 3 Slice C audit, Agent 3 Slice D audit (30min-4hrs each). P1 scaffold ships in parallel.
- Day 4: Assistant 1 adversarial review (A+B) + Assistant 2 adversarial review (C+D) — parallel Claude Code sessions.
- Day 4 PM / Day 5 AM: Agent 0 integration pass at `agents/audits/congress6_integration_2026-04-XX.md`. Agent 7 priors moved to `agents/audits/_superseded/`.
- Day 5: Hemanth gate-open ratification → P2/P3/P4 execution begins. Agent 4B ships `pieceFinished` + `peersWithPiece`. Agent 4 starts P2. Agent 3 starts P4. Agent 4 → P3 after P2 lands.

**Agent 7 return handoff contract:**
- Before Congress 6 opens → cancel Congress 6 as designed; rescope to single Agent-7 Trigger-C audit.
- During audit execution → Hemanth pauses next domain-agent summon; Agent 7 redirected to unreached slice (likely Slice D if it is last) or integration gap.
- During integration pass or later → Agent 7 reads integration memo + all audits; writes supplementary Trigger-C memo flagging below-bar audits or missed gaps. Advisory to Hemanth; may trigger targeted re-audit of one slice, not full re-run.

## R21 snapshot record — Stremio Reference folder state at motion authoring

Per addendum Risk Register R21 (snapshot drift mitigation), folder timestamps recorded 2026-04-18 at motion-authoring time via `ls -la "C:/Users/Suprabha/Downloads/Stremio Reference/"`:

| Subdir | Last-modified timestamp |
|---|---|
| `mpv-master/` | 2026-04-17 20:59 |
| `stream-server-master/` | 2026-04-16 22:38 |
| `stremio-core-development/` | 2026-04-14 16:59 |
| `stremio-docker-main/` | 2026-04-16 22:39 |
| `stremio-service-master/` | 2026-04-16 22:26 |
| `stremio-video-master/` | 2026-04-14 16:58 |
| `stremio-web-development/` | 2026-04-16 22:25 |

If any subdir mtime changes between motion-authoring (2026-04-18) and integration-pass close, Agent 0 integration memo flags affected citations as potentially stale and spot-checks the changed subdir. Folder has been stable across the Congress-5 and pre-Congress-6 window per file-mtime checks — low probability event, but the record is the mitigation.

## Verification (how we know Congress 6 worked)

1. Congress 6 ratified — 4 positions (3/4B/5 any order, Agent 4 last) + Agent 0 synthesis + Hemanth Final Word + archive + reset.
2. 4 audit files at `agents/audits/congress6_*_2026-04-XX.md` — each answers its 3 questions with file:line evidence. Length variable. Slice D may be an appendix of the Slice C audit.
3. 2 assistant adversarial reviews at `agents/audits/congress6_assistant{1,2}_adversarial_*_2026-04-XX.md`.
4. Integration pass at `agents/audits/congress6_integration_2026-04-XX.md` — per-phase gate verdicts explicit (GATE OPEN / GATE HELD PENDING for P2, P3, P4); `_superseded/` directory populated with Agent 7 priors.
5. STREAM_ENGINE_REBUILD_TODO P2/P3/P4 sections link to integration memo (no embedded 30-line blocks).
6. CLAUDE.md dashboard refreshed — Congress 6 closed, Agent 7 prior stream audits marked historical, P2/P3/P4 gates open.
7. Hemanth smoke — spot-check one citation per audit; verify 3 questions got answered, not padded-around.

---

## Positions

### Agent 3 (Video Player — Slice C + D auditor)
_COLLAPSED — not solicited. Agent 3 proceeds directly to Slice C audit when summoned per addendum step 5. Confirmations from the position-required-confirmations table are deferred to the audit session's pre-write commitment in chat.md._

### Agent 4B (Sources — substrate-upstream perspective)
_COLLAPSED — not solicited. 4B HELP.md asks (pieceFinished signal + peersWithPiece + 12-method API freeze) already tracked via HELP.md + landed in chat.md ACK post-Congress-5; no separate Congress-6 position needed._

### Agent 5 (Library UX — library-consumer perspective)
_COLLAPSED — not solicited. Slice D's collapse-to-appendix escape hatch covers the Library UX gap-may-be-zero scenario; Assistant 2 verifies honesty of the collapse during adversarial review. No Agent 5 pre-audit concerns to solicit._

### Agent 4 (Stream — Slice A + B auditor, domain master, posts LAST)
_COLLAPSED — not solicited. Agent 4 proceeds directly to Slice A audit when summoned per addendum step 4. Confirmations deferred to audit session's pre-write commitment._

---

## Agent 0 Synthesis

_Collapsed — Hemanth chose direct ratification over position-synthesis cycle. The revised addendum itself represents the synthesis-equivalent output: it reflects Assistant 1 + 2 adversarial review catching 7 structural flaws (cross-review reciprocity, LOC floor, Slice C event naming, Slice B under-coverage, orphan routes, ceremony bloat, TODO/memo duplication) plus 6 new risks post-revision (R17-R22). The domain-agent position pass would have added: (a) Agent 3 pre-commitment to Slice C+D shape → low marginal value since 3-question sheets lock the scope; (b) Agent 4B status on HELP.md asks → already ACK'd; (c) Agent 5 library gap scoping → already escape-hatched via Slice D collapse rule; (d) Agent 4 amendment requests → deferable to audit-session pre-write chat.md post. Net: positions were pre-empted by the assistant-adversarial pass and the Rule-14 call to move._

---

## Hemanth's Final Word

_Implicit ratification via summon-chain framing 2026-04-18. Hemanth's session prompt structured the post-motion flow as a sequential summon chain ("Summon 1: Agent 0 authors motion; Summon 2: ..."), treating the motion as pre-approved in its drafted shape. Explicit verbatim text not captured in chat.md at archive time — inferred ratification via hook-asserted state change (SessionStart hook flagged Congress 6 as RATIFIED after motion authored)._

---

## Post-ratification execution plan (unchanged from Pre-Brief Timeline)

Per addendum §"Addendum next steps post-approval (REVISED)" steps 4-9:

1. **Next summon: Agent 4 for Slice A audit session** (addendum step 4 — first of Agent 4's two back-to-back audits).
2. **Agent 4 for Slice B audit session** (same-day back-to-back or Day 2/Day 3).
3. **Agent 3 for Slice C audit session.**
4. **Agent 3 for Slice D audit session** (or appendix-collapse decision).
5. **Assistants 1 and 2 in parallel Claude Code sessions** for adversarial reviews.
6. **Agent 0 integration pass** at `agents/audits/congress6_integration_2026-04-XX.md`.
7. **Hemanth gate-open ratification** → P2/P3/P4 execution begins.

P1 scaffold ships in parallel with audit drafting (P1 is not audit-gated).
