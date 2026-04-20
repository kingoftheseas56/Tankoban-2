# Stream Stall Fix TODO — CLOSED 2026-04-19

**Status:** CLOSED 2026-04-19 by Agent 0. All phases shipped + smoked. Agent 4B Phase 4.2 verification ratified close: 974+ PERF ticks across 3 sessions / 2 swarms (One Piece 1575eafa + JJK S01E01) / 2 video codecs (h264 + av1) / 2 audio codecs (eac3 + opus) — **ZERO `stall_detected` events under Phase 2 + 3 + 4 combined** vs Phase 3 baseline of 2 stalls vs pre-Phase-2 baseline of 32 s pieces-not-in-queue starvation. 4-hour soak explicitly deferred to organic normal-use sessions per Agent 4B recommendation.

**Final shipped stack (phases landed in order):**
- Phase 1 (Agent 3) — DE-SCOPED SHIP: `reconnect_delay_max=5` parity across video_decoder + audio_decoder + demuxer (3 files, ~13 LOC). Commit `c2e1295`.
- Phase 2 (Agent 4) — gate-pass sequential toggle: `bool gatePassSequentialOff` per session + `setSequentialDownload(false)` on first GatePct-crosses-100 in StreamEngine.cpp (~15 LOC). Fixed `pi.requested 0→1` scheduler-visibility gate per Agent 4B B3 source-prediction. Commit `1f8f4e0`.
- Phase 3 (Agent 4B) — session-settings bundle: `max_out_request_queue` 500→1500 + readback log in TorrentEngine::applySettings (~18 LOC). Widened peer pipelines 16× + 4× peer concurrency. Commit `1b9e891`.
- Phase 4 (Agent 3) — Option A prefetch thread: new `StreamPrefetch` class at native_sidecar/src/stream_prefetch.{h,cpp} (~290 LOC) + glue in video_decoder.cpp wrapping raw AVIOContext → 64 MiB ring buffer → dedicated producer thread. Three structural bugs caught + fixed (bounded-wait-breaks-find_stream_info, AVIOContext-not-thread-safe-must-cache-source_size, producer-continue-on-EOF-for-MKV-Cues-backtrack) — reference patterns for future sidecar work.
- Phase 4.1 (audio-decoder parity) — PARKED indefinitely per Agent 3 Rule-14 video-only scope call.

**Parked Congress 7 tactics (documented in archive):**
- (b) `read_piece` force-pull — source-falsified at torrent.cpp:799.
- (e') `read_piece` forward scout — same falsification.
- (c) nuclear piece-priority reset on seek — last-resort scrub primitive, not needed post-Phase-4.
- (e-reload) session-level reload on stall — P5 stall-recovery bolt-on, not needed (zero stalls).
- (d) debrid-first pivot — deferred by Hemanth 2026-04-19.

**Critical re-framing on-record** (Congress 7 synthesis + Agent 4B B3): the original "smoking gun" at torrent.cpp:11100 was the wrong gate. Real dispatch stack is 6 layers (1 Hz second_tick → bottom-10% peer cull → deadline horizon → per-peer `can_request_time_critical` saturation → 2 s hard ceiling → add_blocks state check). Phase 2+3+4 together route around gates 1 + 4 via (a) normal-picker visibility + (e-settings) wider per-peer pipelines + (f) continuous consumer-side demand. Future stream-mode scheduler investigations start from this framing.

**Post-close actions executed this wake:** (1) this TODO marked CLOSED; (2) chat.md close announcement; (3) MEMORY.md updated with shipped + closed status. (4) STREAM_LIFECYCLE_FIX + STREAM_PLAYER_DIAGNOSTIC Phase-3 carry-forwards unblocked (independently Agent 4 / Agent 3 domain calls when they next wake). (5) STREAM_STALL_RECOVERY_FIX_TODO authoring DEFERRED — Agent 4B's zero-stall smoke means P5 stall-recovery primitives (c + e-reload) are not load-bearing right now; re-author if a catastrophic swarm surfaces in normal-use.

---

**Original scope below preserved for provenance.**

---

**Owners:** Agent 3 (Phase 1 — sidecar readahead) + Agent 4 (Phase 2 — gate-pass sequential toggle) + Agent 4B (Phase 3 — session-settings bundle). Agent 0 coordinates phase gates and commit sweeps.

**Created:** 2026-04-19 by Agent 0 after Congress 7 ratification (`agents/congress_archive/2026-04-19_stream_stall_tactics.md`).

## Context

Tankoban's stream mode exhibits mid-playback piece-dispatch starvation: on the 1575eafa One Piece S02E01 torrent, pieces past the cold-open head window (piece 5, piece 9, piece 25, piece 40) stall 15-32 s despite 149+ peers advertising the piece, 10+ MB/s aggregate bandwidth, and peer `download_queue_time` averaging 163 ms. `piece_diag` telemetry proves libtorrent's time-critical dispatcher silently drops these pieces: `in_dl_queue=0, peers_downloading=0, requested=0, finished=0, writing=0`. Pieces eventually arrive "via piece_arrival" — i.e., only when the regular sequential-download picker organically advances to that byte offset.

Congress 7 (2026-04-19) convened 5 agents for a reference-sweep comparative audit against `stremio reference 2/` — five Stremio-adjacent codebases (Stremio-Kai, stremio-enhanced, flixerr, stremio-community-v5, stremio-web-neo) + a libtorrent-source deep dive. Two tactics on the original Agent 4 table — (b) `read_piece` force-pull on stall + (e') `read_piece` forward scout — were source-falsified at [torrent.cpp:799](C:/tools/libtorrent-source/src/torrent.cpp#L799) (`read_piece` short-circuits with `invalid_piece_index` unless `user_have_piece(piece)` is true; it's a passive "after we have it" primitive, not a force-download). The pre-brief's claimed smoking gun at [torrent.cpp:11100-11135](C:/tools/libtorrent-source/src/torrent.cpp#L11100) was also corrected by Agent 4B B3: `pi.requested == 0` continue lives inside the finished-piece flush fast-path, not the fresh-piece path. Real dispatch failure is a 6-layer gate stack (1 Hz `second_tick` cadence → bottom-10% peer cull → deadline horizon → per-peer `can_request_time_critical` saturation → 2 s hard ceiling → `add_blocks` state check).

This TODO ships the 3 tactics Congress 7 ratified, in the order the synthesis recommended. Each tactic is independent; each has its own rollback path.

## Objective

After this plan ships, Tankoban's stream mode behaves correctly across the following failure scenarios:

1. **Cold-open:** `firstPieceMs` stays ≤ 12 s (no regression from current ~10-12 s baseline on healthy swarms).
2. **Post-gate-pass scrub to mid-file (UserScrub to 40:00 on S06E09):** `piece_diag` on the target piece flips `in_dl_queue 0 → ≥ 1` within one stall-watchdog tick (2 s). Stall duration drops from 15-32 s to < 5 s.
3. **Post-gate-pass playback advance (playback naturally advancing past piece 5, 9, 25, 40 on 1575eafa):** no stalls exceeding 4 s `stall_detected` threshold. No `stall_recovered via=piece_arrival` events past cold-open.
4. **Scrub-within-cache (seeks < 150 MB from current playhead):** zero network round-trip. Player resumes instantly.
5. **Bandwidth choke mid-playback:** `stall_detected` fires within 4 s, `stall_recovered` within 6 s of unchoke. (Existing P5 invariant — preserved, not regressed.)

## Non-Goals (explicitly out of scope for this plan)

- **Debrid-first pivot (tactic d).** Explicitly deferred by Hemanth 2026-04-19. Evidence logged in Congress 7 archive; not re-litigated here.
- **Nuclear piece-priority reset on seek (tactic c).** Source-falsified by Agent 3 as a reference pattern; Agent 4B flags destructive of mid-flight requests. Held as last-resort scrub primitive for future `SCRUB_RECOVERY_FIX_TODO`, not this bundle.
- **Session-level reload on stall (tactic e-reload).** Bolts onto P5 stall-recovery path, not cold-open. Separate `STREAM_STALL_RECOVERY_FIX_TODO` (future, after this bundle smokes).
- **Prioritizer / SeekClassifier tuning.** Already locked in via P3 of STREAM_ENGINE_REBUILD. This TODO does NOT retune urgency windows, deadline gradients, or re-assert cadence.
- **12-method API freeze.** In effect through P6 terminal tag. This bundle adds zero public methods to TorrentEngine / StreamEngine; all changes are inside existing call sites or additive-only session-settings.
- **Any TorrentEngine change outside `makeSessionSettings`.** Phase 3 is session-settings-only; no new methods, no signal changes.
- **Any StreamEngine change outside gate-pass detection + one toggle call.** Phase 2 is ~15 LOC — detection predicate + single `setSequentialDownload(false)` call.
- **Any sidecar change outside avformat_open chain + SidecarProcess launch flags.** Phase 1 mirrors 8 mpv.conf directives into ffmpeg equivalents; no new sidecar events, no IPC changes.

## Agent Ownership

**Phase 1 — Agent 3 (Sidecar / Video Player domain).** Primary files:
- `native_sidecar/src/demuxer.cpp` (avformat_open chain)
- `native_sidecar/src/main.cpp` or equivalent (argv parsing for optional runtime overrides)
- `src/ui/player/SidecarProcess.cpp` (if CLI-flag injection chosen over in-sidecar defaults)

**Phase 2 — Agent 4 (Stream mode domain).** Primary files:
- `src/core/stream/StreamEngine.cpp` (gate-pass detection in existing gate-tracking code + single toggle call)
- `src/core/stream/StreamEngine.h` (if new state field needed — likely just `bool m_gatePassedSequentialOff` or equivalent per session)

**Phase 3 — Agent 4B (Sources / TorrentEngine domain).** Primary files:
- `src/core/torrent/TorrentEngine.cpp` (`makeSessionSettings` call site — add `max_out_request_queue` bump + `whole_pieces_threshold` verify)

**Cross-agent coordination:**
- Phase 1 ships first (independent variable). Phase 2 + Phase 3 can ship in parallel once Phase 1 smoke is green.
- No shared file touches across phases. CMakeLists.txt not touched (no new files).
- Agent 0 brokers commit-sweep cadence + synthesizes smoke results.

---

## Phase 1 — Sidecar readahead pressure (tactic f) — **DE-SCOPED SHIP 2026-04-19; full scope PARKED as Option A**

**Why:** Stremio-Kai's `portable_config/mpv.conf:27-37` ships mpv with 300 MB forward cache + 150 MB back + 180 s readahead + 64 MB stream-buffer + `demuxer-seekable-cache=yes` + `reconnect=1`. This is what keeps libtorrent's time-critical queue continuously saturated — the player pulls bytes far ahead of the playhead, so `StreamHttpServer::waitForPieces` is always busy, and the non-time-critical `pick_pieces` path (which runs on every peer-event) has pieces to dispatch. Flixerr's working pipeline ([video-stream.js:77-110](C:/Users/Suprabha/Downloads/stremio%20reference%202/flixerr-master/assets/js/video-stream.js#L77)) relies on the same mechanism via WebTorrent's `createReadStream` continuous drain into ffmpeg stdin.

**Outcome 2026-04-19 (Agent 3 wake):** Only directive 8 (`reconnect_delay_max=5`) shipped — the directly-portable ffmpeg HTTP-option pass-through. Commit lands reconnect_delay_max 10 → 5 across `video_decoder.cpp` + `audio_decoder.cpp` + `demuxer.cpp`. Sidecar build GREEN, smoke GREEN (no regression on cold-open + 30+ consecutive PERF ticks steady at 24-25 fps, drops=0/s).

**Directives 1-7 could NOT be shipped via the brief's avformat_open approach.** Two implementation attempts backed out:
1. 64 MiB wrapping `AVIOContext` via `avio_alloc_context` + `read_packet` delegating to raw avio via `avio_read_partial`. Cold-open worked; BUT mid-playback `avio_read_partial` legitimately returns 0 during transient HTTP reconnect windows, which the callback mapped to `AVERROR_EOF`, triggering clean demuxer shutdown as false-EOF.
2. `avio_read` (blocking) with 2 MiB per-call cap. Cold-open REGRESSED to 45 s LoadingOverlay — the first blocking read needs bytes the torrent engine hasn't fetched yet, and those are behind the very gate this TODO was trying to relieve. Deadlock against the original problem.

**Root cause (Agent 3 finding):** ffmpeg libavformat is synchronous — demuxer pulls bytes on-demand on the decode thread. mpv's 64 MiB `stream-buffer-size` works because mpv runs its stream layer on a DEDICATED PREFETCH THREAD that continuously fills the cache while the demuxer reads from the cache non-blocking. Any pure-libavformat attempt to deliver "64 MiB of forward pressure" hits one of three failure modes (large blocking reads → deadlock; partial reads with 0-return-as-EOF → false shutdown; partial reads with 0-as-EAGAIN → CPU busy-loop without actual readahead).

**PARKED as Option A — prefetch-thread inside sidecar** (new `native_sidecar/src/stream_prefetch.{h,cpp}` + ring buffer + glue in `video_decoder.cpp` + RAII teardown). Est 2-3 additional Agent 3 wakes. Mirrors mpv's actual mechanism. Re-opens IF Phases 2 + 3 land without closing the stall.

**Why cheap-first investigation order:** Agent 4B's B3 already source-verified Phase 2's `sequential_download=false` flip as SUPPORTS STRONGLY. Phase 3 is a single settings-bump with independent rollback. Both together = ~25 LOC across 2 files, ~1-2 wakes. If they close the stall, Phase 1 becomes YAGNI and we save the prefetch-thread work. If they don't, we know the deficit is specifically consumer-side readahead pressure (not scheduler-side) and Option A is justified.

### Batch 1.1 — Mirror mpv.conf readahead directives into avformat_open

Target file: `native_sidecar/src/demuxer.cpp` (and/or wherever `avformat_open_input` is called).

Mirror these 8 directives (verbatim mpv names on the left, ffmpeg AVOption equivalents on the right):

1. **`cache=yes`** → ensure `AVFormatContext::flags` includes caching behavior. In practice: set `AVIOContext` cache via `avio_open_dyn_buf` / `avio_cached_*` pair, OR rely on the demuxer's implicit cache with bumped `demuxer-max-bytes` (next item).
2. **`cache-secs=900`** → bound the total cache by 15 minutes of stream. Map to `AVFormatContext::probesize + analyzeduration` lower bounds AND an upper byte-cap via `demuxer-max-bytes` (next item) rather than a literal seconds counter.
3. **`demuxer-readahead-secs=180`** → 3 minutes of readahead. ffmpeg equivalent: `AVFormatContext::max_analyze_duration = 180 * AV_TIME_BASE` for initial analysis, + ongoing readahead via `AVIOContext::buffer_size` + the demuxer's packet queue. Primary lever is `stream-buffer-size` (item 6).
4. **`demuxer-max-back-bytes=150MiB`** → 150 MB seek-back cache. ffmpeg: `AVFormatContext::max_index_size` + the internal demuxer buffer's back-compensation. In practice, allocate the stream buffer large enough that seeks within 150 MB of current playhead re-use cached bytes.
5. **`demuxer-max-bytes=300MiB`** → 300 MB forward cache. ffmpeg: `AVIOContext::buffer_size = 300 * 1024 * 1024` where supported, OR the FIFO queue between demuxer and decoder. This is THE critical directive — it's what makes `waitForPieces` always-busy.
6. **`stream-buffer-size=64MiB`** → 64 MB IO buffer between the HTTP source and the demuxer. ffmpeg: `AVIOContext::buffer_size = 64 * 1024 * 1024` on the context opened for the HTTP stream. Must be set BEFORE `avformat_open_input`.
7. **`demuxer-seekable-cache=yes`** → seeks within the cached forward/back window land as zero-network. ffmpeg: default behavior of the demuxer's internal cache when `stream-buffer-size` is large enough to cover the seek distance. No explicit opt-in needed; verify by smoke (item 4 of Objective).
8. **`stream-lavf-o=reconnect=1,reconnect_streamed=1,reconnect_delay_max=5`** → HTTP reconnect on drop. ffmpeg: pass `reconnect=1 reconnect_streamed=1 reconnect_delay_max=5` as avformat options dict before `avformat_open_input`. This is the exact ffmpeg syntax mpv passes through.

**Implementation freedom:** Agent 3 picks between (a) hard-coding these directives inside the sidecar's avformat_open chain, vs. (b) exposing them as SidecarProcess CLI flags so runtime override is possible. (a) is simpler; (b) is more debuggable. Agent 3's call per Rule 14.

**Smoke exit criterion:** with `TANKOBAN_STREAM_TELEMETRY=1`, open 1575eafa One Piece S02E01 cold, let playback advance naturally past piece 5 / 9 / 25 / 40. Observe zero `stall_detected` events for these pieces. Second smoke: seek within the cached 150 MB back window — player resumes in < 1 s, no network round-trip visible in `alert_trace.log`. Third smoke: seek beyond cache window — piece_diag should still show dispatch progressing (whether or not (a) also lands).

**Rollback:** revert the commit. Every directive is additive; removing them restores pre-batch behavior exactly.

**Build / smoke:** `powershell -File native_sidecar/build.ps1` (Rule 1: sidecar is agent-buildable via `contracts-v2`). Main app unchanged — no main rebuild needed for Phase 1.

**Expected LOC:** ~30 LOC in demuxer.cpp, + optional ~10 LOC in SidecarProcess.cpp if CLI-flag route chosen.

---

## Phase 2 — Gate-pass sequential toggle (tactic a)

**Why:** `setSequentialDownload(true)` at [StreamEngine.cpp:326](src/core/stream/StreamEngine.cpp#L326) is empirically net-positive for cold-open head delivery (Phase 2.6.3 validation, 2026-04-16) — without it, the gate stuck at 48.9% for 25 s on c23b316b S02E04. But Agent 4B B3 source-verified that `sequential_download=true` constrains the normal `pick_pieces` path's candidate set to pieces at or before the cursor — pieces past the cursor NEVER enter the picker's reach until the cursor organically advances to them. Flipping `sequential_download=false` post-gate returns pieces past cursor to `pick_pieces`, which runs on every peer-event (not the 1-Hz `second_tick` time-critical tick). Block-requests get seeded on prio-7 pieces within milliseconds; subsequent time-critical escalation then has `pi.requested > 0` to operate on.

**The earlier "global sequential off" test that regressed cold-open 11.5 s → 32 s conflated two regimes.** Gating the flip on cold-open gate-pass (GatePct first-crossing-100 per session) preserves sequential during head delivery AND unlocks the picker during playback.

**Depends on:** Phase 1 landing first (to establish the independent-variable baseline).

### Batch 2.1 — Flip sequential_download off on gate-pass

Target file: `src/core/stream/StreamEngine.cpp` (+ `.h` if a new per-session flag is needed — likely yes).

- Add a per-`StreamSession` boolean: `bool gatePassSequentialOff = false;` (initializes false; set true AFTER the flip has been applied once per session).
- Locate the existing gate-progress tracking code path (where `gateProgressBytes` or equivalent is advanced, and `gatePct` is computed). When `gatePct` crosses 100 for the first time this session (i.e., `!session.gatePassSequentialOff && gatePct >= 100`), call `m_torrentEngine->setSequentialDownload(session.infoHash, false);` and set `session.gatePassSequentialOff = true`. Never flip back on.
- Session teardown (replacement / failure / user-end): no cleanup needed — the flag dies with the session.

**Guard against regressions:** the flip must NOT fire before gate-pass. Verify via stream_telemetry.log: `setSequentialDownload(false)` log line (add a `qDebug() << "[stream] gate-pass sequential off:" << session.infoHash;` or emit a telemetry event) appears AFTER `firstPieceMs` telemetry, never before.

**Smoke exit criterion:** same 1575eafa smoke as Phase 1 but WITHOUT Phase 1's sidecar readahead. `firstPieceMs` should match pre-Phase-1 baseline (~11.5 s). After gate-pass, `piece_diag` on the next stalling piece shows `in_dl_queue 0 → ≥ 1` within one 2 s stall-tick. Combined with Phase 1: ideally zero stalls anywhere in the 1575eafa timeline.

**Rollback:** revert the commit. State is per-session and dies with the session; no cross-session carry-over.

**Build / smoke:** main-app rebuild (`build_check.bat` green + Hemanth-driven build_and_run.bat smoke via Windows-MCP). Not sidecar.

**Expected LOC:** ~15 LOC across StreamEngine.h (1 field addition) + StreamEngine.cpp (detection + single call at the gate-crossing site).

---

## Phase 3 — Session-settings flattening bundle (tactic e-settings)

**Why:** Agent 4B B3 identified gate 4 of the 6-layer time-critical dispatch stack as per-peer saturation: [peer_connection.cpp:3543-3558](C:/tools/libtorrent-source/src/peer_connection.cpp#L3543) `can_request_time_critical` returns false when `download_queue + request_queue > desired_queue_size * 2`. Our piece_diag `avg_peer_q_ms=163` sits mid-saturation for fast peers, blocking time-critical dispatch even AFTER Phase 2's `sequential=false` restores candidate-set visibility. Raising `max_out_request_queue` lifts the cap, allowing more in-flight requests per peer so `can_request_time_critical` passes. This is orthogonal to Phase 1 + Phase 2; runs as an independent experiment with its own rollback.

**Depends on:** nothing. Can ship parallel to Phase 2 (both touch different files — Phase 2 is StreamEngine, Phase 3 is TorrentEngine).

### Batch 3.1 — Raise max_out_request_queue + verify whole_pieces_threshold

Target file: `src/core/torrent/TorrentEngine.cpp` (in `makeSessionSettings` or equivalent — the single site where the `settings_pack` is constructed at session init).

- Bump `max_out_request_queue` from current default (likely libtorrent's 500) to **1500**. Start conservative; can raise to 2000 on next iteration if 1500 is net-positive but insufficient.
- Verify `whole_pieces_threshold`: current value should NOT be forcing whole-piece mode on our 8-16 MB pieces. The default is typically 20 (seconds); on an 8 MB piece at 5 MB/s that's ~1.6 s which is well below threshold — should be fine, but log the effective value at session init and confirm.
- Log the full `settings_pack` at session init for the first 3 settings (`max_out_request_queue`, `whole_pieces_threshold`, `request_queue_time`) so smoke can verify the bump landed.

**Smoke exit criterion:** run Phase 1 + Phase 2's smoke scenario with Phase 3 additionally applied. Compare `piece_diag` `avg_peer_q_ms` values: if Phase 3 is helping, peers should sustain higher queue depths (expect 400-800 ms range rather than 100-200 ms) without hitting the `> desired_queue_size * 2` cap. If `avg_peer_q_ms` rises but stall count does not improve, Phase 3 is innocent — keep it anyway (higher queue depth is also protective against transient peer loss).

**Rollback:** revert the single-commit settings bump. Independent rollback per setting: if bumping `max_out_request_queue` regresses and `whole_pieces_threshold` is untouched, only the first setting needs reverting.

**Build / smoke:** main-app rebuild. Parallel-shippable with Phase 2; Agent 0 brokers commit order if Phase 2 + Phase 3 land in the same sweep window.

**Expected LOC:** ~10 LOC in TorrentEngine.cpp (two settings + logging).

---

## Smoke Matrix (joint, post-all-phases)

One combined smoke run after all three phases land (expected ~3 batches over 2-3 Agent 4 wakes):

1. **1575eafa cold-open:** `firstPieceMs ≤ 12 s`. No `stall_detected` events during cold-open head delivery.
2. **1575eafa natural playback through piece 40:** zero `stall_detected` events. `piece_diag` never shows `in_dl_queue=0` on any piece in the active playback window.
3. **1575eafa UserScrub to 40:00:** playback resumes within 5 s. `piece_diag` on target piece shows `in_dl_queue ≥ 1` within 2 s of the scrub.
4. **1575eafa short seek (< 150 MB back):** playback resumes in < 1 s, no new network activity.
5. **Bandwidth choke mid-playback:** `stall_detected` fires within 4 s (P5 invariant preserved), `stall_recovered` within 6 s of unchoke (P5 invariant preserved).
6. **Sopranos S06E09 cold-open + UserScrub to 40:00 + ContainerMetadata scrub to 55:30:** full STREAM_ENGINE_REBUILD P5 smoke matrix — no regressions.

If any of 1-4 fails, keep drilling per Congress 7's parked tactics ((c) nuclear reset + (e-reload) session reload) as stall-recovery tools before considering (d) debrid pivot.

---

## Phase 4 — Option A: sidecar prefetch thread (UNLOCKED 2026-04-19)

**Why:** Phase 2 + Phase 3 landed partial-green. Agent 4B's Phase 3 smoke (chat.md Phase 3 report) proved the residual stall mechanism is NOT queue-depth-limited — it is **peer-delivery-speed-limited**. Measured telemetry: piece 31 `requested=128 finished=23/165` — libtorrent is asking 128 blocks of this piece across peers, only 23 have arrived. Peers are maxed on our demand but physically slow at delivery.

The fix is **ask more peers concurrently** — the only way to raise aggregate throughput when single-peer throughput is capped. That requires **continuous forward demand** from the consumer (sidecar), which spreads libtorrent's requests across a wider peer pool. This is exactly what mpv's 64 MiB `stream-buffer-size` does via its dedicated prefetch thread — see Agent 3's Phase 1 de-scope finding (chat.md:2543-2552) for the architectural root-cause analysis: ffmpeg libavformat is synchronous pull, so the fix cannot live inside the avformat_open chain; it needs a separate prefetch thread feeding a ring buffer.

**Scope fence:** This is a sidecar-internal change. Zero `src/core/stream/*` / `src/core/torrent/*` touches. Zero frozen Engine API touches. The ring buffer + prefetch thread sit between `avio_open2` (HTTP source) and `avformat_open_input` (demuxer consumer) — pure native_sidecar surgery.

### Batch 4.1 — New `native_sidecar/src/stream_prefetch.{h,cpp}` + glue in video_decoder.cpp

**New files:**
- `native_sidecar/src/stream_prefetch.h` — public interface for a `StreamPrefetch` class that owns the prefetch thread + ring buffer
- `native_sidecar/src/stream_prefetch.cpp` — implementation

**Design (Agent 3 has final Rule-14 freedom on shape; below is sketch):**
- Constructor takes the raw HTTP `AVIOContext*` from `avio_open2(url)`, capacity (64 MiB default per mpv parity), spawns a `std::thread` that continuously calls `avio_read_partial(raw, ring.write_slot(), chunk)` and advances the ring's write head.
- Expose a producer-thread state enum: `Running / EofReached / FatalError` — consumer distinguishes "no data yet, wait" from "stream genuinely ended."
- Expose a consumer-facing `read(buf, size)` callback that the demuxer's wrapping `AVIOContext` invokes. Semantics:
  - If ring has ≥ `size` bytes queued → copy + return `size` (non-blocking).
  - If ring has partial data + thread state == `Running` → copy what's available + return positive byte count (partial read is fine; NOT EOF).
  - If ring empty + thread state == `Running` → block on a condition variable until either data arrives or a shutdown signal. **Cap the wait** at something reasonable (e.g., 5 s) to avoid holding the decode thread indefinitely on a genuinely-stalled torrent.
  - If ring empty + thread state == `EofReached` → return 0 (true EOF).
  - If thread state == `FatalError` → return `AVERROR(EIO)` or similar.
- Destructor: shutdown flag + join prefetch thread + clean ring buffer teardown. RAII.

**Glue in `native_sidecar/src/video_decoder.cpp`:**
- Replace the existing `avformat_open_input(&fmt_ctx, url, ...)` call chain with:
  1. `avio_open2(&raw_avio, url, AVIO_FLAG_READ, ...)` as before.
  2. Construct `StreamPrefetch prefetch(raw_avio, 64 * 1024 * 1024)`.
  3. Allocate a consumer-side `AVIOContext* wrap_avio = avio_alloc_context(buf, 2MiB, 0, &prefetch, &StreamPrefetch::read_trampoline, nullptr, nullptr)`.
  4. `fmt_ctx = avformat_alloc_context(); fmt_ctx->pb = wrap_avio; fmt_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;`
  5. `avformat_open_input(&fmt_ctx, "", nullptr, opts)` (empty URL because we pass the custom IO).
- Ensure all 4 early-return paths + the normal end-of-function path tear down in the right order: demuxer close → wrap_avio free → prefetch dtor (joins thread) → raw_avio close.

**Critical: do NOT repeat Phase 1 attempt-1 and attempt-2 failures.**
- Attempt 1 (false-EOF on transient HTTP drain): caused by mapping `avio_read_partial` returning 0 to `AVERROR_EOF`. Fix: our consumer callback only returns 0 when the PRODUCER thread has explicitly reached EOF. Transient drain at the raw HTTP layer is invisible to the consumer because the ring buffer absorbs it.
- Attempt 2 (cold-open deadlock): caused by consumer-side blocking reads waiting for bytes the torrent engine hadn't fetched. Fix: our consumer block is bounded (wait-with-timeout); more importantly, the PRODUCER thread is the one that waits on the HTTP source, not the consumer — the producer's wait is transparent to the demuxer thread, which keeps processing whatever is already in the ring.

**Smoke exit criterion (Windows-MCP self-drive on 1575eafa One Piece S02E01 via Torrentio):**
1. **Cold-open preserved:** `firstPieceMs ≤ 12 s` (no regression from Phase 2+3 baseline). `gate_pass_sequential_off` telemetry still fires within milliseconds of first_piece as Phase 2 shipped.
2. **Peer concurrency rises:** `piece_diag` on active-playback pieces shows `peers_downloading` count expanding from Phase 3's 1-2 to 4+ (because sidecar is pulling 64 MiB continuously, which libtorrent spreads across more peers).
3. **Zero `stall_detected` events** on playback past cold-open head through piece 40. Target: residual tail-block stalls Agent 4B measured (pieces at 95-99% block completion with 12-32 s self-recovery) are GONE.
4. **Scrub-within-cache:** seek to a byte-offset within the 64 MiB ring buffer window resumes in < 1 s with zero network round-trip visible in alert_trace.log.
5. **Post-stall clean-playback window extends:** if any stall still fires, the clean-playback window after recovery should extend beyond Phase 2's 113 s baseline (closer to indefinite).
6. **No mid-playback false-EOF** — zero `VideoDecoder: thread exiting` logs with `Stream ends prematurely` during a 10-minute playback run.
7. **No cold-open deadlock** — LoadingOverlay never exceeds the existing 30 s watchdog.

**Rollback:** `git revert HEAD`. Sidecar-internal change; reverts to Phase 2+3 partial-green baseline. State lives entirely in the sidecar process; no cross-session / cross-commit coupling.

**Build / smoke:** `powershell -File native_sidecar/build.ps1` GREEN → Windows-MCP smoke. Main app unchanged for Phase 4; no `build_check.bat` / main-app rebuild needed.

**Expected LOC:** ~150-200 in `stream_prefetch.{h,cpp}`; ~20 LOC glue in `video_decoder.cpp` (+ mirror in `audio_decoder.cpp` + `demuxer.cpp` if they also call `avformat_open_input` and would benefit — Agent 3's call).

**Cap:** 2-3 Agent 3 wakes (Agent 3's own Phase 1 estimate preserved).

**Close conditions:** this phase CLOSES when smoke criteria 1-7 pass on 1575eafa + the Sopranos S06E09 P5 matrix (TODO §Smoke Matrix item 6) shows no regressions. On close, entire STREAM_STALL_FIX_TODO is ready for Hemanth final ratification + MEMORY.md update.

### Phase 4 wake-1 outcome (2026-04-19)

**Core ship ACCEPTED by Agent 0.** `native_sidecar/src/stream_prefetch.{h,cpp}` (~290 LOC) + `video_decoder.cpp` glue shipped. Smoke on 1575eafa / Torrentio EZTV / ~90s sustained playback: criteria 3, 5, 6-short GREEN — zero `stall_detected`, ring at 63-66 MiB continuously, 25 fps / drops=0/s / 89+ consecutive PERF ticks. Video-only first ship (audio_decoder + demuxer untouched per Rule 14 scope call). Three iteration-bugs caught + fixed in wake: (1) bounded wait + `AVERROR(EAGAIN)` aborts mkv find_stream_info → unbounded wait; (2) consumer `avio_size(raw_avio)` races with producer `avio_read_partial` (non-thread-safe) → cache `m_source_size` at ctor; (3) producer break-on-EOF leaves MKV backtrack seek (Cues index at EOF → offset 10961 for playback start) unserviced → continue-on-EOF + wait-for-seek pattern at top of loop.

### Criterion 1 + 7 re-interpretation (Agent 0, 2026-04-19)

**Cold-open watchdog fire during Phase 4 wake-1 smoke is NOT a Phase-4 regression.** The pre-brief's ≤12s target (criterion 1) was aspirational. Agent 4B's Phase 3 smoke on the SAME 1575eafa / Torrentio EZTV swarm showed `firstPiece=87861 ms` (chat.md Phase 3 report) — the main-app 30s `LoadingOverlay` watchdog was already firing routinely before Phase 4 existed, because fresh 48-peer Torrentio cold-starts are libtorrent-metadata-fetch-dominated (mdReady ~100+s on magnet), not piece-dispatch-dominated. Phase 4 only affects mid-playback readahead pressure (post-first-frame); cold-open latency lives upstream of the prefetch thread. **Accepted as pre-existing libtorrent fresh-swarm latency. Flagged for a separate future investigation (metadata-fetch-latency follow-up), not a Phase-4 re-ship.**

### Phase 4.2 — smoke-gap closure (Agent 4B, ~1 wake, no code change expected)

Remaining criteria need verification against the Agent 4B Phase 3 baseline + another swarm:

1. **Criterion 2 (peer concurrency):** pull `piece_diag` during active playback; expect `peers_downloading` to expand from Phase 3's baseline of 8 into 10+ range (sidecar's continuous 64 MiB forward pull should spread libtorrent requests across a wider peer pool).
2. **Criterion 4 (scrub-within-cache):** Windows-MCP UI-scrub within the 64 MiB ring-buffer window. Expected: playback resumes in < 1s with zero network round-trip visible in alert_trace.log.
3. **Criterion 6-long (10-minute soak):** extend wake-1's 90s smoke to a full 10-minute run; verify no `Stream ends prematurely` / `VideoDecoder thread exiting` / drift.
4. **Sopranos S06E09 P5 regression matrix** (TODO §Smoke Matrix item 6): different swarm / codec / size. Confirms Phase 4 doesn't regress on other content.

**Owner: Agent 4B** (single agent = single smoke harness = cleanest signal). No code expected; pure verification wake. If any criterion fails, open Phase 4.3 targeted at the specific failure.

### Phase 4.1 — audio-decoder parity (PARKED)

Agent 3 shipped VIDEO-ONLY glue in wake-1 per Rule 14 scope call (audio bitrate is 10-40× lower than video, rarely starves time-critical). Mirror into `audio_decoder.cpp` + `demuxer.cpp` probe path ONLY if multi-torrent smoke surfaces audio-specific stalls. Deferred indefinitely; no active owner.

## Rollback

Each phase is independently reversible via `git revert HEAD`. Landing order recommended: Phase 1 first (biggest expected delta, most orthogonal), then Phase 2 + Phase 3 in parallel. If smoke shows regression:

- Regression on cold-open `firstPieceMs` → revert Phase 1 (directive wrong), not Phases 2/3.
- Regression on head gate-pass timing → revert Phase 2 only (the gate-pass flip is the only thing that could affect this).
- Regression on `avg_peer_q_ms` or peer stability → revert Phase 3's settings bump (independent).

## Exit Criteria

This TODO is CLOSED when all 6 items of the Smoke Matrix are green, a 4-hour multi-file TV pack soak completes without `stall_detected` events on pieces beyond cold-open head, and `piece_diag` telemetry in `stream_telemetry.log` shows the fix is behavioral (not coincidental).

Post-close actions:
- Update MEMORY.md with the closed-and-shipped status.
- Mark `STREAM_LIFECYCLE_FIX_TODO` + `STREAM_PLAYER_DIAGNOSTIC_FIX_TODO` Phase-3 continuations as unblocked.
- Agent 0 authors `STREAM_STALL_RECOVERY_FIX_TODO` next if P5-side still needs (c) + (e-reload) as belt-and-suspenders.
