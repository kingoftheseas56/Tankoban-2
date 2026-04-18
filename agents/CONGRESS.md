# Congress

One motion at a time. When resolved, Agent 0 archives to `congress_archive/YYYY-MM-DD_[topic].md` and resets this file to the empty template. Then posts one line in chat.md.

When Hemanth posts a ratification line (`ratified`, `APPROVES`, `Final Word`, or `Execute`), Agent 0 MUST archive and reset in the same session — not the next session. If Agent 0 is absent, the next agent to start a session becomes the archiver-of-record.

---

## CONGRESS 5 — STATUS: OPEN
Opened by: Agent 0 (Coordinator)
Date: 2026-04-18

## Motion

**Demolish and rebuild Tankoban 2's streaming engine from scratch against Stremio Reference as the semantic (behavior) blueprint.** Scope is narrow: `src/core/stream/StreamEngine.{h,cpp}` + `src/core/stream/StreamProgress.{h,cpp}` + `src/ui/pages/stream/StreamPlayerController.{h,cpp}`. Everything else — UI pages, source fetching (MetaAggregator/StreamAggregator/AddonRegistry/AddonTransport/MetaItem/CalendarEngine), torrent substrate (TorrentEngine/TorrentClient), native sidecar player — stays unchanged. The rebuild proceeds in 6 dependency-ordered phases (P0 pre-hardening → P1 scaffold pass-through → P2 piece-waiter async → P3 prioritizer + seek-type → P4 sidecar probe escalation → P5 stall detection → P6 demolition of dead paths), each revertible at its boundary except P6. MVP acceptance bar is Hemanth's 6-point list from 2026-04-17 (no wonky aspect, no Mode B seek-hang, no Mode A cold-start 0%-buffering at 1000+ seeds, subtitles work, progress persists, streams like Stremio).

Full plan at `C:\Users\Suprabha\.claude\plans\i-want-you-to-cosmic-newell.md` (approved 2026-04-18). Hemanth's direction 2026-04-17: stream mode has consumed more hours than the rest of the app combined; evolved-via-patches `StreamEngine` is at its local minimum; rebuild is the right move.

## Scope

**IN scope:**
- Demolish + rewrite `StreamEngine.{h,cpp}` with internals split across `StreamSession`, `StreamPrioritizer`, `StreamPieceWaiter`, `StreamSeekClassifier`, and the facade. Public API frozen (17 methods + 2 signals + 4 structs + 1 enum).
- Rewrite `StreamPlayerController.{h,cpp}` internals; public API frozen (ctor + 4 methods + 5 signals + `StopReason` enum).
- `StreamProgress.h` namespace additive-only (add `schema_version=1` field in P0; no renames/removals).
- New `TorrentEngine::pieceFinished(hash, pieceIdx)` signal exposure (Agent 4B, single-line emit addition).
- Sidecar probe escalation at `native_sidecar/src/video_decoder.cpp:221` + `demuxer.cpp:65` (Agent 3 domain; three-tier 512KB/750ms → 2MB/2s → 5MB/5s).
- `StreamHttpServer.cpp:82` surgical replacement of `waitForPieces` with `StreamPieceWaiter::await` (P2 only).

**OUT of scope:**
- UI page rewrites (StreamPage, cards, layouts, detail view, home board, calendar, addon manager all untouched).
- Source fetching (Torrentio addon integration stays; addon protocol adoption deferred forever per Hemanth 2026-04-17).
- Torrent substrate refactor (TorrentEngine/TorrentClient internals untouched except the one new signal + optional method).
- Native player pipeline (sidecar, FrameCanvas, D3D11, SHM overlay, subtitle rendering all untouched).
- `StreamHttpServer` rewrite — codified non-goal at `StreamEngine.h:43-50`; one surgical change only.
- HLS transcoding (Stremio has it; we don't port).
- libmpv wrap — we keep direct-FFmpeg sidecar.
- Aspect ratio correctness — sidecar (Agent 3) concern, not engine. Engine rebuild cannot regress it because engine doesn't touch aspect.
- Parallel `stream_v2/` folder — flat single-checkout per `feedback_no_worktrees`.

## Pre-Brief

**Required reading before posting positions:**

1. **Approved plan:** `C:\Users\Suprabha\.claude\plans\i-want-you-to-cosmic-newell.md` — 6-phase rebuild, preserved contracts, cross-domain coordination matrix, risk register, rollback shape.
2. **Agent 7 audit:** `agents/audits/player_stremio_mpv_parity_2026-04-17.md` (97 lines) — frames architectural gap.
3. **Agent 3 validation of that audit:** `agents/chat.md:3159-3243` — confirms P0-1 buffered-range + P0-2 cache-pause + P0-3 property-graph IPC; plus factual corrections.
4. **Mode B repro telemetry excerpt:** `out/stream_telemetry.log` tail from 2026-04-17 14:01-14:05 UTC — seek-target pieces [21,22] `ready=0 have=[0,0]` storm despite head gate 100%, 70+ peers, 11 MB/s. This is the concrete failure the rebuild must close.
5. **Stremio semantic reference:** `C:\Users\Suprabha\Downloads\Stremio Reference\stream-server-master\` — includes vendored `mpv-master`. Stremio is Node.js/WebTorrent — behavior parity only, not API port. Every libtorrent call verified against libtorrent 2.0 docs at `C:\tools\libtorrent-2.0-msvc`, never derived from Stremio code.
6. **Risk R3 in the plan:** Mode B may survive as a peer-availability problem (not scheduler). Per-peer `have_piece` telemetry required BEFORE declaring Mode B fixed.

## How This Congress Works

**Motion requires positions from 4 agents. Agents 5, 3, 4B can draft in PARALLEL** — their concerns are non-overlapping (library UX consumer / sidecar probe / torrent substrate). Summon concurrently. **Agent 4 posts LAST** as domain master per GOVERNANCE Hierarchy — their position carries more weight and they address concerns raised by the other three. Agent 0 synthesizes after all four.

**Each agent's position must answer:**
- **Agent 5:** Concerns about (a) `streamFailed` text possibly becoming more specific (e.g., `"Probe failed at 5MB"`), (b) `bufferedRangesChanged` cadence possibly rising to ~3 Hz during stalls (T0 ~2 Hz; SeekSlider dedupes upstream — should be safe), (c) `streamStopped(StopReason)` vocabulary staying unchanged. Flag any consumer assumptions that would break.
- **Agent 3:** (a) Accept owning P4 sidecar probe escalation (three-tier 512KB/750ms → 2MB/2s → 5MB/5s at `video_decoder.cpp:221` + `demuxer.cpp:65`; `rw_timeout` escalating with tier). (b) Confirm preserved IPC contract (`buffered_ranges` event from PLAYER_STREMIO_PARITY Phase 1 survives rebuild consuming preserved `contiguousHaveRanges`). (c) Flag any sidecar-side risks from probe escalation (e.g., fast-swarm cases incurring extra latency — mitigation is first tier is 750ms, faster than current 30s worst-case).
- **Agent 4B:** (a) Commit to exposing new `TorrentEngine::pieceFinished(QString hash, int pieceIdx)` signal from alert worker's `piece_finished_alert` branch at `TorrentEngine.cpp:153-156` — single emit addition, no behavior change. HARD dependency for P2. (b) Commit to TorrentEngine API-freeze during rebuild window on these methods: `setPieceDeadlines`, `setPiecePriority`, `contiguousBytesFromOffset`, `fileByteRangesOfHavePieces`, `pieceRangeForFileOffset`, `havePiece`, `haveContiguousBytes`, `setFilePriorities`. Either (i) freeze, or (ii) version the shapes. (c) Feasibility of optional P3 `peersWithPiece(hash, pieceIdx) const` method for per-peer `have_piece` telemetry to falsify Risk R3 — or propose a fallback shape.
- **Agent 4 (domain master, last):** (a) Accept owning primary rebuild execution (P1 scaffold through P6 demolition). (b) Phase ordering concerns — the plan proposes P0 → P1 → (P2 ∥ P4) → P3 → P5 → P6; flag any dependencies I missed. (c) File split concern — plan proposes 5 files (facade + Session + Prioritizer + PieceWaiter + SeekClassifier) vs current monolith; push back if you think different shape fits better. (d) Preserved-contract completeness — plan enumerates 17 methods + 2 signals + 4 structs + 1 enum as frozen; verify nothing missing. (e) 4-hour soak gate for P6 (per Risk R7) — accept or propose alternative gate.

**After all four post, Agent 0 synthesizes (ratify, modify, or override with justification per GOVERNANCE Hierarchy). Hemanth has final word.**

---

## Positions

### Agent 1 (Comic Reader)
*Not summoned — stream mode is out of scope for comic reader domain. Position not required for this motion.*

### Agent 2 (Book Reader)
*Not summoned — stream mode is out of scope for book reader domain. Position not required for this motion.*

### Agent 3 (Video Player)
[position]

### Agent 4 (Stream)
[position]

### Agent 4B (Sources)
[position]

### Agent 5 (Library UX)
[position]

---

## Agent 0 Synthesis

[Synthesis after all positions in. Override justification if any. Final recommendation to Hemanth.]

---

## Hemanth's Final Word

[Hemanth ratifies, modifies, or rejects.]

---

<!-- TEMPLATE — copy this block when opening a new motion, replace STATUS above with the open motion -->

<!--
## CONGRESS N — STATUS: OPEN
Opened by: Agent 0 (Coordinator)
Date: YYYY-MM-DD

## Motion

[One-paragraph statement of what is being decided.]

## Scope

**IN scope:** [...]

**OUT of scope:** [...]

## Pre-Brief

[Required reading before posting positions, e.g., agents/congress_prep_*.md.]

## How This Congress Works

[Order of positions, who posts when, what each agent must confirm.]

---

## Positions

### Agent 1 (Comic Reader)
[position]

### Agent 2 (Book Reader)
[position]

### Agent 3 (Video Player)
[position]

### Agent 4 (Stream)
[position]

### Agent 4B (Sources)
[position]

### Agent 5 (Library UX)
[position]

---

## Agent 0 Synthesis

[Synthesis after all positions in. Override justification if any. Final recommendation to Hemanth.]

---

## Hemanth's Final Word

[Hemanth ratifies, modifies, or rejects.]
-->
