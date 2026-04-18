# Audit — Congress 6 Slice A: Stream Primary — 2026-04-18

By Agent 4 (Stream mode, domain master + Slice A auditor). For Agent 0 integration memo and the P2/P3/P4 gate decisions on `STREAM_ENGINE_REBUILD_TODO.md`.

Reference comparison: `C:\Users\Suprabha\Downloads\Stremio Reference\stream-server-master\` (Rust reimplementation of the upstream Node.js `server.js`; depends on `libtorrent-sys` FFI to libtorrent-rasterbar, same library family as Tankoban's TorrentEngine — so semantic port is viable, not a WebTorrent framing).

Scope: three locked questions from the Congress 6 3-question sheet for Slice A — (Q1) Mode A root-cause / piece-wait mechanism, (Q2) Replacement lifecycle / source-switch semantics, (Q3) Probe-HLS coordination with HTTP Range serving. Plus the 6 orphan routes (`subtitles/system/archive/ftp/nzb/youtube`) revisited explicitly in-scope per revised addendum. Out of scope: any fix prescription (in-line trivial-fix notes may land in a separate post-audit commit per loosened Trigger C); running Tankoban; re-auditing Slices B / C / D.

Prior-art input: [agents/audits/stream_a_engine_2026-04-16.md](stream_a_engine_2026-04-16.md) — read, not treated as authority. This Slice A supersedes that file to `agents/audits/_superseded/` on integration close. Every overlap is re-derived from fresh reads here.

R21 snapshot: at 2026-04-18 audit session entry, all 7 Stremio Reference subdirs match the motion-authoring mtimes recorded in [agents/congress_archive/2026-04-18_congress6_stremio_audit.md](../congress_archive/2026-04-18_congress6_stremio_audit.md). No citation drift to flag.

---

## Methodology

Tankoban files read (fresh this session):
- [src/core/stream/StreamEngine.cpp](../../src/core/stream/StreamEngine.cpp) (1269 lines) + [.h](../../src/core/stream/StreamEngine.h) (320 lines)
- [src/core/stream/StreamHttpServer.cpp](../../src/core/stream/StreamHttpServer.cpp) (564 lines)
- [src/core/torrent/TorrentEngine.cpp](../../src/core/torrent/TorrentEngine.cpp) (lines 1130–1269 targeted: piece-deadline primitives + `contiguousBytesFromOffset` + `haveContiguousBytes` + `pieceRangeForFileOffset`)

Stremio Reference files read (fresh this session):
- `stream-server-master/server/src/routes/stream.rs` (405 lines — full read)
- `stream-server-master/server/src/routes/hls.rs` (569 lines — full read)
- `stream-server-master/server/src/routes/subtitles.rs` (227 lines — full read)
- `stream-server-master/server/src/routes/system.rs` (160 lines sampled — settings + stats surface)
- `stream-server-master/server/src/routes/{youtube,ftp,nzb,archive,casting,proxy}.rs` (targeted head-reads to establish surface boundary; line totals 106/154/116/406/160/220)
- `stream-server-master/server/src/state.rs` (186 lines — full read)
- `stream-server-master/server/src/main.rs` (lines 180–320 targeted: route table)
- `stream-server-master/server/src/cache_cleaner.rs` (229 lines — full read)
- `stream-server-master/server/src/ffmpeg_setup.rs` (174 lines — full read)
- `stream-server-master/enginefs/src/lib.rs` (487 lines — full read: `on_stream_start`, `on_stream_end`, `schedule_file_cleanup`, `focus_torrent`)
- `stream-server-master/enginefs/src/engine.rs` (544 lines — full read: `get_probe_result`, `get_file`, `find_subtitle_tracks`)
- `stream-server-master/enginefs/src/piece_waiter.rs` (66 lines — full read: registry + notify)
- `stream-server-master/enginefs/src/hls.rs` (280 lines sampled: `probe_video` + `probe_video_with_limits`)
- `stream-server-master/enginefs/src/backend/libtorrent/stream.rs` (494 lines — full read: `poll_read`, `set_priorities`, `SeekType`)
- `stream-server-master/enginefs/src/backend/libtorrent/handle.rs` (716 lines — full read: `get_file_reader`, `prepare_file_for_streaming`, `clear_file_streaming`)
- `stream-server-master/enginefs/src/backend/libtorrent/mod.rs` (lines 1–350 — alert pump + `piece_finished_alert` wake path)

Stremio consumer-side (`stremio-core-development/src/models/streaming_server.rs` + `stremio-core-development/src/models/ctx/update_streams.rs`) is explicitly delegated to Slice C / D in the Congress 6 question sheets; this audit notes the handoff boundary rather than duplicating Agent 3's read.

No web-search performed this pass. libtorrent-rasterbar behaviour assumed consistent with 2.0.11 reference docs already cited in the 2026-04-16 prior-art audit.

---

## Q1 — Mode A root-cause: piece-wait mechanism

**Q1 verbatim from Congress 6 Slice A sheet:** "On a 1000-seed torrent at `metadata_ready`, what exact call sequence does Stremio's `stream.rs` make between piece-0 request and first HTTP byte sent to client? Where does it wait on pieces, what mechanism (sync/async)? How does this differ from our `StreamHttpServer.cpp:82` waitForPieces 15s poll-sleep?"

### Q1 Observed (Tankoban)

Tankoban's piece wait at the HTTP serving layer is a **200 ms poll-sleep loop with a 15 s hard timeout per chunk**:

- [StreamHttpServer.cpp:77-78](../../src/core/stream/StreamHttpServer.cpp#L77-L78) declares `PIECE_WAIT_POLL_MS = 200` and `PIECE_WAIT_TIMEOUT_MS = 15000`.
- `waitForPieces` at [StreamHttpServer.cpp:82-108](../../src/core/stream/StreamHttpServer.cpp#L82-L108) loops `while (elapsed < PIECE_WAIT_TIMEOUT_MS)` calling `engine->haveContiguousBytes(...)` every 200 ms via `QThread::msleep`. No waker registration, no notification path.
- The loop is invoked per 256 KB chunk ([StreamHttpServer.cpp:76](../../src/core/stream/StreamHttpServer.cpp#L76) `CHUNK_SIZE`) inside the main serve loop at [StreamHttpServer.cpp:286-345](../../src/core/stream/StreamHttpServer.cpp#L286-L345). On timeout, the serve loop breaks and the socket closes ([StreamHttpServer.cpp:326-344](../../src/core/stream/StreamHttpServer.cpp#L326-L344)); the sidecar's `av_read_frame` sees `AVERROR(EIO)` and triggers its retry path ([comment at 319-322](../../src/core/stream/StreamHttpServer.cpp#L319-L322)).
- Cancellation short-circuit added post-STREAM_LIFECYCLE_FIX Phase 5.2 at [StreamHttpServer.cpp:99-100](../../src/core/stream/StreamHttpServer.cpp#L99-L100) — the `cancelled` atomic load happens before `haveContiguousBytes`, so `stopStream` unblocks waiters on the next poll-iteration (≤ 200 ms slop).

Upstream of that loop, Tankoban also runs a **gate-based readiness check** in `StreamEngine::streamFile`:

- [StreamEngine.cpp:361-362](../../src/core/stream/StreamEngine.cpp#L361-L362) polls `contiguousBytesFromOffset(hash, idx, 0)` from the caller (StreamPlayerController, ~1–2 Hz) and returns `FILE_NOT_READY` until the first `kGateBytes = 5 MB` ([StreamEngine.h:293](../../src/core/stream/StreamEngine.h#L293)) are contiguous ([StreamEngine.cpp:430-437](../../src/core/stream/StreamEngine.cpp#L430-L437)).
- At `onMetadataReady`, head deadlines are set across the first 5 MB with a 500 ms → 5000 ms gradient ([StreamEngine.cpp:988-1029](../../src/core/stream/StreamEngine.cpp#L988-L1029)), plus a 3 MB tail-metadata window at 6000 ms → 10000 ms ([StreamEngine.cpp:1075-1117](../../src/core/stream/StreamEngine.cpp#L1075-L1117); note the Phase 2.2 hotfix lifted tail-first from 3000 ms to 6000 ms at [StreamEngine.cpp:1076-1082](../../src/core/stream/StreamEngine.cpp#L1076-L1082) to preserve head-wins-over-tail ordering).
- `setSequentialDownload(hash, true)` at [StreamEngine.cpp:285](../../src/core/stream/StreamEngine.cpp#L285) stays enabled per Phase 2.6.3 decision (comment at [StreamEngine.cpp:262-285](../../src/core/stream/StreamEngine.cpp#L262-L285) captures the falsification arc — sequential was blamed for seek-storm, validation showed head-gate regression without it, sequential + per-piece priority-7 boost in `prepareSeekTarget` at [StreamEngine.cpp:763-765](../../src/core/stream/StreamEngine.cpp#L763-L765) was the landed fix).
- `contiguousBytesFromOffset` at [TorrentEngine.cpp:1206-1269](../../src/core/torrent/TorrentEngine.cpp#L1206) walks pieces forward under `QMutexLocker(m_mutex)`, calling `have_piece()` per piece. Lock is held for O(num_pieces_in_file) calls into libtorrent.

There is no piece-finished-alert subscription in `TorrentEngine` at the Stream-A boundary. The HELP.md ask from Agent 0 → Agent 4B for a `pieceFinished(hash, pieceIdx)` signal is precisely the missing primitive that would turn this into a notification-driven waiter. Agent 4B's ACK at [agents/HELP.md](../HELP.md) confirms feasibility but the signal has not shipped yet (it ships parallel with P1 scaffold per Congress 5 ratification).

**Net observed call path from `metadata_ready` to first HTTP byte under current Tankoban code:**

1. `onMetadataReady` ([StreamEngine.cpp:880-1138](../../src/core/stream/StreamEngine.cpp#L880-L1138)) selects file → sets file priorities ([applyStreamPriorities](../../src/core/stream/StreamEngine.cpp#L1215-L1269) priority 7 on selected, priority 1 fallback on others per Phase 2.4 peer-preservation fix) → sets head + tail deadlines.
2. Caller polls `streamFile` repeatedly. Each poll returns `FILE_NOT_READY` while `contiguousHead < 5 MB` ([StreamEngine.cpp:430-437](../../src/core/stream/StreamEngine.cpp#L430-L437)).
3. When contiguousHead ≥ 5 MB → `streamFile` returns `ok=true, url=http://127.0.0.1:{port}/stream/{hash}/{idx}` ([StreamEngine.cpp:441-444](../../src/core/stream/StreamEngine.cpp#L441-L444)).
4. Sidecar opens the URL → HTTP request lands in `handleConnection` ([StreamHttpServer.cpp:134-370](../../src/core/stream/StreamHttpServer.cpp#L134-L370)). Parse headers, parse Range, open `QFile`, seek to `start`.
5. Per-chunk loop at [StreamHttpServer.cpp:286-367](../../src/core/stream/StreamHttpServer.cpp#L286-L367): `waitForPieces(engine, hash, idx, offset, toRead, cancelled)` → if true → `QFile::read(toRead)` → `socket.write(chunk)` → advance. If false → break → socket close → sidecar retry.

### Q1 Reference (Stremio)

Stremio's piece-wait is **notification-based via `PieceWaiterRegistry`**, driven by `piece_finished_alert` from libtorrent, with poll_read returning `Poll::Pending` and registering a tokio `Waker`:

- `PieceWaiterRegistry` at `enginefs/src/piece_waiter.rs:13-59` is a `RwLock<HashMap<(info_hash, piece_idx), Vec<Waker>>>`. Three operations: `register(info_hash, piece, waker)` (line 27-30), `notify_piece_finished(info_hash, piece)` (line 33-49; drains the waker list and calls `waker.wake()` on each), `clear_torrent(info_hash)` (line 53-58).
- `poll_read` in `enginefs/src/backend/libtorrent/stream.rs:177-437` is the HTTP body stream's `AsyncRead::poll_read` implementation. Three-tiered memory-first read path:
  1. **Local cache** (line 193-232): `cached_piece_data` holds one piece buffer from a previous poll. If the request falls on that piece, serve from RAM and return `Poll::Ready(Ok(()))`.
  2. **Moka piece cache** (line 234-315): `futures::executor::block_on(piece_cache.get_piece(...))` sync-check. If hit, copy into `buf`, then spawn an adaptive prefetch task (count based on `download_speed_ema` thresholds at line 274-282: 8 if > 10 MB/s, 5 if > 5 MB/s, 3 if > 1 MB/s, 2 otherwise) that reads `memory_read_piece_direct(next_piece)` for each already-downloaded next piece.
  3. **Piece not in cache** (line 317-390): if `!handle.have_piece(piece)`, register this task's waker in `piece_waiter` (line 320-321) + spawn a 50 ms tokio sleep as belt-and-suspenders safety net (line 324-327) + return `Poll::Pending` (line 354). If `have_piece` is true but not in cache, call `memory_read_piece_direct(piece)` synchronously (line 359); if non-empty, spawn `cache.put_piece` + `waiter.notify_piece_finished` (line 371-374) and return `Poll::Pending` with a 10 ms safety wake (line 386); if empty, register waker + 15 ms safety wake.
- The wake path is driven by a **5 ms alert pump** in `enginefs/src/backend/libtorrent/mod.rs:194-265`. `start_monitor_task` spawns a tokio task with `interval(Duration::from_millis(5))`. Each tick drains libtorrent alerts via `s.pop_alerts()`. When `alert.alert_type == piece_finished_alert_type` (line 230), it calls `memory_read_piece_direct(alert.piece_index)` (line 237), spawns `cache.put_piece(info_hash, piece_idx, piece_data)` + `waiter.notify_piece_finished(info_hash, piece_idx)` (line 244-252). The `notify_piece_finished` drains `Vec<Waker>` for that key and `waker.wake()`s each (piece_waiter.rs:36-38), which schedules the suspended `poll_read` task back on the tokio runtime.
- **Startup priority shape** in `handle.rs:218-358` (`get_file_reader`) is materially different from ours: `SeekType::InitialPlayback` uses a URGENT staircase 0 ms → N × 10 ms over `MAX_STARTUP_PIECES` (value documented at `backend/priorities.rs:6-9` in the prior-art audit — 2 pieces); `SeekType::UserScrub` uses 300 ms base with 4-piece window; `SeekType::ContainerMetadata` uses 100 ms base with 2-piece window. A `speed_factor` multiplier (0.5 for > 5 MB/s, 1.0 for 1–5, 1.5 for 100 KB–1 MB, 2.0 for < 100 KB/s) scales non-URGENT deadlines dynamically. Tail metadata is set to `last_piece=1200 ms` + `last_piece-1=1250 ms` (line 324-333) **only after the head window is set**, so the first-frame path is not delayed by end-of-file work.
- The `set_piece_deadline` is **called in `poll_read`'s `set_priorities` method on every poll** (`stream.rs:184`), which means deadlines dynamically re-center as the reader advances through pieces. `calculate_priorities` (imported from `backend::priorities`) uses the reader's current position + download-speed EMA + bitrate hint to compute an urgency window. Multi-stream fair-sharing jitter of up to 50 ms is added per stream (line 155-169) to interleave multiple concurrent streams' deadlines.

### Q1 Reference call path — metadata_ready to first HTTP byte

1. `stream_video` route handler at `stream.rs:190-366` called on HTTP GET `/stream/{hash}/{idx}` or `/{hash}/{idx}`.
2. `state.engine.get_engine(info_hash)` (line 210) — or `add_torrent` auto-create (line 213-237) with default trackers + cached ranked trackers.
3. `state.engine.on_stream_start(info_hash, idx)` (line 240) — takes `active_file` write lock, clears priorities on previously-active different file, sets new active, increments `active_file_streams[(hash,idx)]` counter. See Q2.
4. `state.engine.focus_torrent(info_hash)` (line 241) — enables streaming mode (uploads limited) + pauses all OTHER torrents in session (`lib.rs:465-469` + `set_streaming_mode(true)` + `backend.focus_torrent(hash)`).
5. `engine.handle.get_files()` (line 243) — adaptive-polled metadata read in `handle.rs:410-461` with 10 ms → 100 ms exponential backoff + 30 s hard timeout. Fast path returns immediately if `has_metadata`.
6. `engine.get_file(idx, start_offset, priority)` (line 279) — calls `prepare_file_for_streaming` (`handle.rs:482-693`) which: waits for metadata (same adaptive poll) → sets file priorities (target=4, others=0) → clears ALL piece deadlines (line 547; MULTI-FILE FIX comment) → sets low-urgency safety-net deadlines on first MAX_STARTUP_PIECES with 3000 + i*25 ms staircase (line 566-572) → pre-warms memory piece cache if head is already complete (line 578-599) → spawns a background 250 ms-delayed metadata inspector task that primes tail pieces + calls `MetadataInspector::find_critical_ranges` + sets 150 ms deadlines on `moov`/`Cues` regions (line 627-686).
7. Then `get_file_reader(idx, start_offset, priority, bitrate)` (`handle.rs:121-408`) — clears piece deadlines (line 240-242; SKIP for ContainerMetadata), sets URGENT staircase on head window, sets tail = 1200/1250 ms, returns `LibtorrentFileStream`.
8. HTTP response headers written (`stream.rs:311-327`). Body `Body::from_stream(guarded_stream)` (line 348) where `guarded_stream` is `StreamGuard { inner: ReaderStream, ... }`.
9. Consumer pulls first bytes → axum polls `guarded_stream.poll_next` → tokio polls `LibtorrentFileStream::poll_read` → either cache-hit path returns `Poll::Ready` immediately, OR piece-miss path registers waker in `piece_waiter` + returns `Poll::Pending`.
10. When piece 0 finishes downloading → `piece_finished_alert` fires → 5 ms alert-pump task receives it → `memory_read_piece_direct(0)` → `cache.put_piece` + `waiter.notify_piece_finished(hash, 0)` → `waker.wake()` → tokio re-schedules `LibtorrentFileStream::poll_read` → second poll hits the moka-cache path → bytes copied into `buf` → first HTTP byte flushed to client.

### Q1 Divergence

| Axis | Tankoban | Stremio | Impact |
|---|---|---|---|
| **Wait mechanism** | Blocking poll-sleep every 200 ms per chunk ([StreamHttpServer.cpp:104](../../src/core/stream/StreamHttpServer.cpp#L104) `QThread::msleep`) | Async waker registration in `PieceWaiterRegistry`, wake via `piece_finished_alert` through 5 ms alert pump | Latency between piece-arrival-at-libtorrent and data-on-wire: ours ≤ 200 ms slop per chunk; Stremio's ≤ 5 ms alert-pump interval + negligible waker-schedule overhead |
| **Wait granularity** | Per-256 KB HTTP chunk (timeout 15 s) | Per-piece (no explicit timeout; depends on libtorrent deadline + caller timeout) | Ours amplifies latency for multi-chunk reads when pieces are at the edge of availability |
| **Startup gate** | 5 MB contiguous head required before HTTP URL is returned (`StreamEngine.cpp:430`) | No explicit gate — HTTP URL returned immediately, first poll_read blocks on piece 0 | Tankoban's gate ensures sidecar probe completes against downloaded bytes (audit 2026-04-16 Axis 1 H1 hypothesis still load-bearing); Stremio trusts the ffmpeg probe escalation + deadline staircase |
| **Head deadlines** | 500 ms → 5000 ms over 5 MB head (`StreamEngine.cpp:988-1029`) | 0 ms → N × 10 ms staircase over MAX_STARTUP_PIECES (= 2 pieces per prior-art) with `speed_factor` multiplier | Stremio's URGENT tier is materially tighter but covers fewer pieces; maps cleanly to its small-first-playable-piece policy, not a conservative whole-head-gate |
| **Deadline re-assertion** | Once per `onMetadataReady` for head + tail; `prepareSeekTarget` re-sets on every poll-retry (~300 ms cadence per [StreamEngine.cpp:754-765](../../src/core/stream/StreamEngine.cpp#L754-L765)) | On every `poll_read` via `set_priorities` (`stream.rs:184`) — deadlines dynamically re-center around the reader's advancing position | Answers Slice B Q3 for Stremio side: deadlines are re-asserted PER READ, not once — every poll updates. Concrete bearing on our P3 design |
| **File priority on non-selected** | Priority 1 (very-low, preserves peers; [StreamEngine.cpp:1249](../../src/core/stream/StreamEngine.cpp#L1249)) | Priority 0 (skip) in `get_file_reader` (`handle.rs:187`); previously-active different file explicitly cleared in `on_stream_start` (`lib.rs:245-259`) | Tankoban's priority-1 was the fix for multi-file peer-collapse symptom 2026-04-16 ([StreamEngine.cpp:1217-1248](../../src/core/stream/StreamEngine.cpp#L1217-L1248)). Stremio's approach is priority 0 + explicit `active_file` tracking + delayed cleanup. Two valid solutions to the same bandwidth-focus problem |
| **Cancellation model** | `std::shared_ptr<std::atomic<bool>>` token checked pre-poll ([StreamHttpServer.cpp:99](../../src/core/stream/StreamHttpServer.cpp#L99)); set before erase in `stopStream` ([StreamEngine.cpp:464-466](../../src/core/stream/StreamEngine.cpp#L464-L466)) | RAII Drop on `StreamGuard` (`stream.rs:39-58`) → spawns async `on_stream_end` → 5 s delayed `clear_file_streaming` in `schedule_file_cleanup` (`lib.rs:325-382`) | See Q2 |

### Q1 Hypotheses

- **Hypothesis — Mode A root-cause is the 200 ms poll-sleep × 15 s timeout interaction at HTTP-chunk granularity, compounded by the 5 MB gate.** When per-piece download succeeds inside 15 s but first-chunk becomes eligible only at the tail-end of that window, ffmpeg's probe timeline (typically < 30 s before `AVERROR(EIO)` becomes a visible "cannot open probe file") gets squeezed. Under 1000-seed-swarm conditions the pieces should arrive within seconds — in which case the poll-sleep granularity is the dominant wasted latency, not libtorrent's scheduling. The gate's 5 MB floor pushes the first HTTP read to a byte range that's materially ahead of the probe's real need (ffmpeg typically probes < 1 MB), and the gate's completion is subject to libtorrent's fairness algorithm across pieces 0..N_head. **Agent 4 to validate empirically post-P2; P2 switches to notification-based wakes via `StreamPieceWaiter` + Agent 4B's `pieceFinished` signal.**
- **Hypothesis — the 5 MB gate is not required semantically; it's a defense against ffmpeg probing unavailable bytes.** If notification-based wakes land in P2, HTTP chunk reads block cleanly on specific pieces rather than spinning 200 ms polls — so ffmpeg's 30 s rw_timeout stops being the binding constraint and the gate can be lowered toward Stremio's ~1 MB / MAX_STARTUP_PIECES=2 regime. **Agent 4 to validate after P2 lands; P3 sets the final gate size.**
- **Hypothesis — `contiguousBytesFromOffset` under `m_mutex` lock at [TorrentEngine.cpp:1210](../../src/core/torrent/TorrentEngine.cpp#L1210) is fine at the current 1–2 Hz streamFile-poll cadence, but will become contended if we move to a per-piece notification where many readers are active simultaneously.** Stremio's `have_piece()` calls inside `poll_read` go through libtorrent session `read().await` (an async read lock), not a global mutex. **Agent 4 to decide in P2 whether to (a) keep `m_mutex` and accept potential contention, (b) switch to `QReadWriteLock`, or (c) add a dedicated per-stream mutex for piece-coverage queries.**

---

## Q2 — Lifecycle / Replacement

**Q2 verbatim:** "How does Stremio's `state.rs` handle source-switch mid-stream (equivalent of our `stopStream(Replacement)`) without tearing the HTTP connection to an in-flight player? What state survives, what resets?"

### Q2 Observed (Tankoban)

Tankoban's source-switch is handled by `stopStream(hash, StopReason::Replacement)` from the consumer (StreamPlayerController) — the STREAM_LIFECYCLE_FIX Phase 2 split separated Replacement from UserEnd/Failure. The HTTP connection to the in-flight player IS torn, and the torrent is removed with `deleteFiles=true`.

- [StreamEngine.cpp:447-498](../../src/core/stream/StreamEngine.cpp#L447-L498) `stopStream(infoHash)`:
  1. Take `m_mutex` ([line 449](../../src/core/stream/StreamEngine.cpp#L449)).
  2. Set `it->cancelled->store(true)` BEFORE erase ([line 464-466](../../src/core/stream/StreamEngine.cpp#L464-L466)) — Phase 5.1 ordering invariant; ensures HTTP workers inside `waitForPieces` unblock on their next 200 ms poll.
  3. `m_streams.erase(it)` ([line 476](../../src/core/stream/StreamEngine.cpp#L476)).
  4. Release `m_mutex`.
  5. `clearPieceDeadlines(hash)` ([line 483](../../src/core/stream/StreamEngine.cpp#L483)).
  6. `m_httpServer->unregisterFile(hash, idx)` ([line 486-487](../../src/core/stream/StreamEngine.cpp#L486-L487)) — removes from registry so any new connection hitting `/stream/{hash}/{idx}` gets 404.
  7. `m_torrentEngine->removeTorrent(hash, deleteFiles=true)` ([line 491](../../src/core/stream/StreamEngine.cpp#L491)) — libtorrent drops the torrent + deletes cached bytes.
- HTTP serving layer: any worker currently inside `handleConnection` will observe `cancelled=true` within ≤ 200 ms ([StreamHttpServer.cpp:99-100](../../src/core/stream/StreamHttpServer.cpp#L99-L100)) and break; `ConnectionGuard`'s dtor closes the socket cleanly ([StreamHttpServer.cpp:119-130](../../src/core/stream/StreamHttpServer.cpp#L119-L130)).
- HTTP server shutdown path: `StreamHttpServer::stop()` at [StreamHttpServer.cpp:405-447](../../src/core/stream/StreamHttpServer.cpp#L405-L447) sets `m_shuttingDown` atomic + drains `m_activeConnections` with 2 s timeout. The flag is intentionally NOT reset on return ([comment at 441-446](../../src/core/stream/StreamHttpServer.cpp#L441-L446)) to close the race where a worker stuck in a 15 s `waitForPieces` wakes after drain and serves on a torn-down server.
- Consumer side: the player process gets EOF on the socket, sidecar returns `AVERROR(EIO)`, StreamPlayerController gets a stop signal. The new source's `streamFile` call kicks off a fresh `addMagnet` + new `StreamRecord` + new head deadlines → new gate-progress cycle → new HTTP URL.
- **Cache data is deleted** on every Replacement. There is no carry-over of the just-watched source's pieces to a possible re-open in the next N seconds.

### Q2 Reference (Stremio)

Stremio's source-switch (and close-while-buffering, and mid-stream user-scrub to a different file) is handled **emergently by the HTTP connection lifecycle + `active_file` tracking + delayed cleanup**, not by a "stopStream(Replacement)" surface. The HTTP connection itself IS torn when the player opens a new URL — Stremio cannot preserve the connection across a URL change either — but the server-side ENGINE state (memory-mapped pieces + moka piece cache + libtorrent session + torrent handle) survives across a 5-second grace window so that a fast re-open reuses downloaded bytes.

- `StreamGuard` at `stream.rs:18-58` wraps the body stream. Its `Drop` impl (line 39-57) spawns `engine.engine.on_stream_end(info_hash, file_idx)` as a tokio task. The Drop fires when the axum body stream is dropped (client disconnect, server shutdown, or body consumed-to-end).
- `on_stream_start(info_hash, file_idx)` at `enginefs/src/lib.rs:240-278` is called at the top of `stream_video` (`stream.rs:240`):
  1. Take write lock on `active_file` (line 245).
  2. If a DIFFERENT `(prev_hash, prev_idx)` was previously active (line 246-255), call `clear_file_priorities(prev_hash, prev_idx)` — which calls `handle.clear_file_streaming(prev_idx)` (`handle.rs:695-714`): `set_file_priority(prev_idx, 0)` + `clear_piece_deadlines()`.
  3. Set `*active = Some((info_hash, file_idx))`.
  4. Release `active_file` lock.
  5. Increment `active_streams[info_hash]` and `active_file_streams[(info_hash, file_idx)]` counters.
- `on_stream_end(info_hash, file_idx)` at `enginefs/src/lib.rs:281-323`:
  1. Decrement `active_streams[info_hash]`; if it hits 0, remove entry (line 286-293).
  2. Decrement `active_file_streams[(info_hash, file_idx)]`; capture remaining count after decrement (line 295-309).
  3. If remaining file-streams is 0 → `schedule_file_cleanup(info_hash, file_idx)` (line 311-313).
- `schedule_file_cleanup` at `enginefs/src/lib.rs:325-382`:
  1. `tokio::time::sleep(Duration::from_secs(5))` (line 331).
  2. Re-check `active_file_streams[(info_hash, file_idx)]` — if another stream started in the 5 s window, skip cleanup (`"Skipping delayed cleanup because a new stream started"` log at line 339-344).
  3. If `active_file` still points at this (hash, idx), clear it (line 347-358).
  4. Call `handle.clear_file_streaming(file_idx)` on the engine (line 365-380) — priority 0 + clear deadlines.
- The engine auto-cleanup is separate and longer-scoped: `BackendEngineFS::new_with_backend_and_storage` spawns a 60-s-tick task (`lib.rs:125-164`) that removes inactive engines after `ENGINE_TIMEOUT = 300 s` of zero `active_streams`. So an unwatched engine lives 5 minutes before torrent-removal.
- On replacement (user picks a different source → opens new HTTP URL):
  - Old connection's `StreamGuard::Drop` fires → `on_stream_end(old_hash, old_idx)` → schedules 5 s delayed cleanup on the old file.
  - Meanwhile new `GET /stream/{new_hash}/{new_idx}` lands → `on_stream_start(new_hash, new_idx)`. If `new_hash != old_hash`, the `active_file` write lock picks up `Some((old_hash, old_idx))` and clears priorities immediately (line 246-255).
  - **The OLD TORRENT IS NOT REMOVED.** Only its streaming priorities are reset. The torrent handle, memory storage, moka piece cache, and any completed pieces all persist.
  - **If the user switches BACK to the old source within ENGINE_TIMEOUT seconds**, the engine is still in `self.engines`, `get_engine(old_hash)` returns the cached handle, `prepare_file_for_streaming` re-sets head priorities. The download-already-in-memory pieces serve instantly from `memory_read_piece_direct`.
  - **If the user opens the SAME source and SAME file again within 5 s** (e.g., a transient click-through), the delayed cleanup scheduler at `lib.rs:338-345` sees the active_file_streams counter back at 1 and skips the cleanup entirely — no resets, no re-priority dance, the new stream just resumes.
- `focus_torrent(info_hash)` at `lib.rs:465-469` pauses all OTHER torrents in the session to concentrate bandwidth on the new active one. `resume_all_torrents` is the pair; called from the auto-cleanup scheduler.

### Q2 Divergence

| Axis | Tankoban | Stremio | Impact |
|---|---|---|---|
| **Source-switch entry point** | Explicit `stopStream(hash, StopReason::Replacement)` call from consumer | Implicit: old connection's body stream is dropped on player teardown → `StreamGuard::Drop` → `on_stream_end` | Ours is synchronous + explicit; Stremio's is async + RAII. Both end at the same place (tear HTTP, release priorities) |
| **Cache retention on switch** | Zero — `removeTorrent(hash, deleteFiles=true)` at [StreamEngine.cpp:491](../../src/core/stream/StreamEngine.cpp#L491) | Full — torrent handle + memory-backed pieces + moka cache all survive `ENGINE_TIMEOUT = 300 s` | Stremio's fast-reopen (re-watch last N minutes) hits cache; Tankoban re-downloads from scratch |
| **Cleanup timing** | Immediate on `stopStream` | 5 s delay + re-check of active-stream count at `lib.rs:325-382` | Stremio tolerates click-through + double-open; ours treats every stop as definitive |
| **Connection preservation across switch** | Not possible — URL changes, decoder re-opens | Not possible — same reason | Neither system preserves HTTP connection across source-switch (this was implicit in the Q2 framing; confirmed both sides operate at the URL-change-means-new-connection boundary) |
| **State that survives switch** | Only `StreamHttpServer::m_server` (the TCP server itself) + `TorrentEngine::m_records` for OTHER streams | Engine + torrent handle + piece cache + moka cache + `active_streams` counters for other torrents | Stremio's survival surface is materially larger |
| **Priority cleanup on previously-active different file** | Not applicable — previous stream is fully removed | `clear_file_priorities(prev_hash, prev_idx)` at lib.rs:252-255 → `set_file_priority(prev_idx, 0)` + `clear_piece_deadlines` | Stremio's design accepts N concurrent torrents per session, only ONE file active at a time; ours accepts N concurrent torrents but priorities are per-torrent not per-file |
| **Engine-level reuse across session** | None — `StreamEngine::m_streams[hash]` removed on stop | `BackendEngineFS::engines[hash]` kept for 5 min of inactivity | Rapid source-switch (common in discover-flow) is materially faster in Stremio |
| **Bandwidth focus** | File-priority 7 on selected / 1 on others within one torrent; no inter-torrent focus | `focus_torrent(info_hash)` pauses all other torrents in the session (`lib.rs:465-469`) | Under multi-concurrent-torrent scenarios Stremio prevents bandwidth-bleed across torrents; we don't encounter this because we remove on stop, but a long-lived cache model would need this |

### Q2 Hypotheses

- **Hypothesis — rapid source-switch in discover flow is materially slower in Tankoban than in Stremio because every switch is a full torrent add + metadata fetch + head-deadline cycle.** The missing capability is engine-level retention for N seconds after stream stop. This maps to the "Cache retention on switch" axis; not part of the current STREAM_ENGINE_REBUILD plan but could be added as a post-P6 polish track. **Agent 4 to decide post-P6 whether to adopt a 5 s delayed-cleanup + 5 min engine-retention model, or keep delete-on-stop.** The preserved-contract envelope does not forbid this — `stopStream(Replacement)` semantics could evolve to "schedule delayed cleanup" without changing the caller surface.
- **Hypothesis — the fact that both systems tear HTTP connection across source-switch means our STREAM_LIFECYCLE_FIX Phase 2 design (separate stopReasons) has the right SHAPE; what Stremio adds is engine-state-survival, not connection-preservation.** The Session instantiation at P3 per Congress 5 R12 should preserve this invariant: Session destruction sets cancellation token true BEFORE teardown; no new "partial-stop" state survives the destructor. **Agent 4 to confirm P3 Session destructor ordering matches.**
- **Hypothesis — the `active_file` single-slot-per-session model in `lib.rs:245-259` is tighter than Tankoban's current model, which tracks priority-1 fallback per torrent but doesn't enforce "only one active file across all torrents at a time".** If we port the cache-retention model, we should also port the single-active-file invariant to avoid unproductive parallel-download across retained engines. **Agent 4 to decide whether to carry this forward if engine-retention lands.**
- **Hypothesis — the 5 s grace window is empirically tuned to cover click-through and double-open UX but not user-initiated quick-cancel-retry.** `schedule_file_cleanup`'s re-check at `lib.rs:334-345` is the load-bearing anti-race primitive. Porting this to Qt requires either a `QTimer::singleShot(5000, ...)` + re-check of `m_streams[hash]`, or a "pending-removal" state in `StreamRecord`. **Agent 4 to flag the Qt-port shape for P3 Session design if engine-retention is adopted.**

---

## Q3 — Probe / HLS coordination

**Q3 verbatim:** "How does `hls.rs` + `cache_cleaner.rs` + `ffmpeg_setup.rs` coordinate probe escalation with HTTP Range serving? Does Stremio's probe reader block on pieces the same way our sidecar probe does, or partial-probe on already-available bytes only?"

### Q3 Observed (Tankoban)

Tankoban's probe is **sidecar-internal** and uses ffmpeg's HTTP reader (via `avio_open`) against the local `/stream/{hash}/{idx}` URL. The sidecar probe blocks on pieces because its reads flow through our HTTP server which then hits `waitForPieces`:

- Sidecar probe reads up to 20 MB with a 30 s `rw_timeout` (per [StreamEngine.cpp:396-403](../../src/core/stream/StreamEngine.cpp#L396-L403) comment; actual values live in `native_sidecar/src/video_decoder.cpp:175` and `native_sidecar/src/demuxer.cpp:15-21`, not read this session but cited from prior art).
- Agent 3's proposed P4 probe escalation is **three-tier** (Congress 5 position): 512 KB / 500 ms / rw 5 s (reconnect off) → 2 MB / 2 s / 15 s (reconnect off) → 5 MB / 5 s / 30 s (reconnect on). Only tier-3 failure emits `OPEN_FAILED`.
- Currently `StreamEngine::streamFile` at [StreamEngine.cpp:430-437](../../src/core/stream/StreamEngine.cpp#L430-L437) ensures the HTTP URL is returned only after 5 MB contiguous head is available, so by the time the sidecar probe starts reading, the first 5 MB are on disk. The Phase 3.1 regression revealed that without this gate, ffmpeg's 20 MB probe reads hit the 15 s `waitForPieces` wall and exceeded the 30 s `rw_timeout` before enough bytes arrived ([comment at StreamEngine.cpp:396-403](../../src/core/stream/StreamEngine.cpp#L396-L403)).
- No HLS route. No transcoding. No server-side probe endpoint. No `cache_cleaner.rs` equivalent beyond the cache-dir orphan cleanup at [StreamEngine.cpp:826-840](../../src/core/stream/StreamEngine.cpp#L826-L840) (5-minute-tick `cleanupOrphans` that removes subdirs not in `m_streams`).
- No `ffmpeg_setup.rs` equivalent — `resources/ffmpeg_sidecar/` is bundled in the app bundle at build time via `native_sidecar/build.ps1`.
- Slice A non-goals in [StreamEngine.h:15-62](../../src/core/stream/StreamEngine.h#L15-L62) (Phase 4.2 codified) explicitly call out: no HLS, no VTT proxy, no `/create`, no archive/YouTube/NZB, no bare-hash routes, no multi-range, no backend abstraction, no memory-first storage.

### Q3 Reference (Stremio)

Stremio's probe is **server-side in `HlsEngine::probe_video`** and runs ffmpeg as a subprocess reading a file path (either local-file if memory-storage has spilled to disk, OR the HTTP URL as fallback). Probe escalates through three budget tiers and coordinates with HLS routes only when transcoding is needed:

- `HlsEngine::probe_video` at `enginefs/src/hls.rs:127-159`: iterates three (`analyzeduration`, `probesize`) tiers — (750 KB, 512 KB) → (2 MB, 2 MB) → (5 MB, 5 MB). After each attempt, checks `has_streams && knows_container`; returns early on success. Final pass always returns even if inconclusive.
- `probe_video_with_limits` at `hls.rs:161-275` spawns `ffmpeg -analyzeduration N -probesize M -i <path>`, captures stderr, parses `Duration:`, `Input #0, <format>,`, `Stream #0:N` via regex. No stdin stream — ffmpeg reads the path natively, whether local file or HTTP URL.
- `Engine::get_probe_result` at `enginefs/src/engine.rs:46-99`:
  1. Check in-memory `probe_cache[file_idx]` (line 57-64). Return cached if present.
  2. Call `prepare_file_for_streaming(file_idx)` (line 68) — sets priorities, primes head pieces, spawns background metadata inspector.
  3. Ask `handle.get_file_path(file_idx)` (line 76) for a local file path. In memory-only mode this returns `None` (per `handle.rs:463-480`). Fall back to the HTTP URL passed in as `fallback_url` (the `http://127.0.0.1:11470/{hash}/{idx}` URL).
  4. Run `probe_video(path_or_url)` tiered escalation.
  5. Store in `probe_cache`, return.
- When probing via HTTP URL (memory-only), ffmpeg's HTTP client eventually hits `stream_video` → `get_file` → `LibtorrentFileStream::poll_read` → piece-miss path → `piece_waiter.register(waker)` → `Poll::Pending`. So **Stremio's probe reader blocks on pieces the same way our sidecar probe would block on `waitForPieces` — but with notification-based wakes instead of 200 ms polls.** The key difference is:
  - Stremio reads already-available pieces instantly from moka cache / memory storage (line 234-315 cache paths in `stream.rs`).
  - Stremio's probe tier 1 is only 512 KB of `probesize` — well within the MAX_STARTUP_PIECES=2 window that `get_file_reader` prioritized at URGENT deadline.
  - If tier 1 returns insufficient streams (e.g., because the container needs more early bytes), tier 2 / tier 3 escalate — but each escalation is still reading forward through pieces the head-deadline staircase has already prioritized.
- **Background metadata inspector** at `handle.rs:627-686` runs 250 ms after `prepare_file_for_streaming` is called. Inspects the file for `moov` atoms (MP4) or `Cues` (MKV/WebM/MOV) using `MetadataInspector::find_critical_ranges`, then sets 150 ms deadlines on those ranges. This is the equivalent of our tail-metadata head-deadline logic at [StreamEngine.cpp:1075-1117](../../src/core/stream/StreamEngine.cpp#L1075-L1117) but Stremio's is data-driven (actually reads the file to find moov position) not heuristic-driven (last 3 MB).
- The HLS probe endpoint `/hlsv2/probe?mediaURL=...` at `routes/hls.rs:58-107` is a **consumer-facing probe** used by the Stremio web player. Parses info_hash + file_idx from the `mediaURL` query, calls `engine.get_probe_result` (shared path), returns JSON `{ infoHash, fileIdx, format: {name}, duration, streams: [...] }` in Stremio-shaped form. NOT a coordination-with-Range-serving endpoint — it's just the probe output marshalled as JSON. Coordination with Range serving happens at `engine.get_probe_result` level, not at the route level.
- `routes/hls.rs:118-195` (`master_playlist_by_url`) returns the HLS `.m3u8` master playlist when Stremio's web player detects an unsupported codec via the probe. Segments are transcoded on demand at `handle_hls_resource` → `get_segment` (line 267-526), which spawns ffmpeg against `get_file_path(file_idx)` — prefers local file, falls back to stream URL. Hardware acceleration is picked via `routes/system.rs::probe_hwaccel` + `enginefs::hls::TranscodeConfig::with_hwaccel` (`hls.rs:466-495`).
- `cache_cleaner.rs` (229 lines) is NOT a probe-coordinator. It's a periodic + file-watcher-triggered download-directory cleaner: (a) protects files belonging to active engines (line 105-119); (b) deletes files older than 30 days regardless of size (line 121-167); (c) size-based LRU eviction until `settings.cache_size` is respected (line 189-226). Runs entirely async — no interaction with the probe path or the HTTP serving path.
- `ffmpeg_setup.rs` (174 lines) is a **Windows auto-installer**. Checks PATH → downloads `ffmpeg-release-essentials.zip` from gyan.dev → verifies SHA-256 → extracts `bin/ffmpeg.exe` + `bin/ffprobe.exe` → prepends to process PATH. Runs once at server boot (`main.rs:183-199`ish, not shown in this session's reads but referenced). Has zero runtime coordination with probe or Range serving.

### Q3 Divergence

| Axis | Tankoban | Stremio | Impact |
|---|---|---|---|
| **Probe location** | Sidecar-internal (client-side of HTTP) | Server-side in `HlsEngine::probe_video` (subprocess of server) | Stremio can share a probe result across HLS playlist + direct stream + subtitle track enumeration; ours can't because the sidecar is the only probe-consumer |
| **Probe escalation shape** | Agent 3's P4 proposal: 3 tiers (512 KB/500 ms/rw 5 s → 2 MB/2 s/15 s → 5 MB/5 s/30 s) | 3 tiers: (750 KB analyzeduration / 512 KB probesize) → (2 MB / 2 MB) → (5 MB / 5 MB) | Shapes are remarkably similar — Stremio's tier 1 probesize is 512 KB matches Agent 3's tier 1; tier 2/3 align at 2 MB + 5 MB. P4 design is directionally correct per this reference |
| **Probe wait path** | Sidecar HTTP read → our `waitForPieces` 200 ms poll-sleep (blocks ~200 ms slop per piece) | Ffmpeg HTTP read → server's `poll_read` → `piece_waiter` registration + 5 ms alert-pump wake OR memory-cache instant serve | Probe under Stremio is materially cheaper when pieces are already in cache; blocks only on missing pieces with notification-grade latency |
| **Probe budget** | 20 MB read ceiling with 30 s rw_timeout (sidecar-side; cited) | Budget is per-tier `probesize` parameter to ffmpeg (max 5 MB); no explicit rw_timeout at Stremio layer (inherits from ffmpeg defaults) | Stremio caps probe spend tighter; Tankoban's 20 MB reflects a worst-case-tolerance philosophy |
| **HLS transcoding** | None | Full: master.m3u8 + 4 s segments + H.264/AAC targets + HW-accel detection + ffprobe-driven probe-cache | Out of Stream-A scope per Slice A non-goals at StreamEngine.h:22-27 — native sidecar demuxes anything ffmpeg can demux; no HLS need |
| **Probe-cache** | None (sidecar probes on every new stream) | `Engine::probe_cache: Mutex<HashMap<usize, ProbeResult>>` per-engine (`engine.rs:25, 38, 57-64`); probe output reused across probe / HLS master / stream-playlist / subtitle-tracks routes | Not relevant until we add server-side probe; note for P4 sidecar probe: repeat-open-same-stream could skip probe if sidecar also cached probe result |
| **Background metadata inspector** | Heuristic last-3-MB tail-metadata deadline at [StreamEngine.cpp:1075-1117](../../src/core/stream/StreamEngine.cpp#L1075-L1117) | Data-driven: `MetadataInspector::find_critical_ranges` reads the file to locate moov/Cues, then sets 150 ms deadlines on actual ranges (`handle.rs:651-686`) | Stremio's is more surgical; ours is cheaper + wider-net. For MP4 `+faststart` Ours wastes tail-deadline bandwidth on non-moov bytes. For MKV Cues at arbitrary positions Stremio wins |
| **Cache cleaner** | `cleanupOrphans` at [StreamEngine.cpp:826-840](../../src/core/stream/StreamEngine.cpp#L826-L840) runs every 5 min; removes cache subdirs not in `m_streams` | `cache_cleaner.rs`: file-watcher + 1-hour fallback + 60 s debounce; 30-day age eviction + size-based LRU | Different models. Stremio retains completed files as user-visible cache; we treat cache as transient per-session |
| **Ffmpeg availability** | Bundled at build time in `resources/ffmpeg_sidecar/` | Auto-downloaded on first run if missing from PATH | Non-concern for Tankoban (Qt desktop app with bundled resources); relevant only if we ever needed runtime transcoding |

### Q3 Hypotheses

- **Hypothesis — Agent 3's P4 probe escalation design is correctly shaped relative to Stremio's reference.** The tier budgets (512 KB → 2 MB → 5 MB) land within 20% of Stremio's (512 KB → 2 MB → 5 MB; exact match on probesize). The rw_timeout escalation (5 s → 15 s → 30 s) is tighter at tier 1/2 than Stremio's ffmpeg-default (approximately 30 s hard), which is correct for our stream-side readiness gate being upstream. **Agent 3 to confirm in Slice C audit; no action this Slice A.**
- **Hypothesis — `cache_cleaner.rs` is not load-bearing for Slice A because Tankoban's cache model is transient-per-session.** Adopting Stremio-style retention would be a strategic parity track (Slice B / post-P6 polish), not a Slice A gap. **Agent 4 to note in integration memo; no P4 block.**
- **Hypothesis — `ffmpeg_setup.rs` is entirely out of Slice A.** Bundled ffmpeg at build time is the correct shape for Qt desktop apps.
- **Hypothesis — the data-driven metadata inspector is a quality improvement over heuristic tail-deadline.** Porting it requires a piece-reader that speaks MP4 atom parsing + MKV EBML parsing; that's a moderate Agent 7 prototype target rather than a P2/P3/P4 gate-blocking item. **Agent 4 to defer to post-P6 polish list.**
- **Hypothesis — the probe-cache `Mutex<HashMap<usize, ProbeResult>>` model would give us a meaningful win on Replacement-to-same-source scenarios.** Tracking as a P4 follow-on: sidecar could stash probe output per (hash, fileIdx); next open skips probe entirely. **Agent 3 to flag in Slice C if relevant.**

---

## 6 orphan routes (subtitles / system / archive / ftp / nzb / youtube)

Revised addendum §7 moved these from background into Slice A explicitly. Summary findings — all observation-grade:

### subtitles.rs (227 lines)

**Observed:** Stremio exposes `/subtitles.vtt?from=URL` (proxy external VTT), `/{hash}/{idx}/subtitles.vtt` (extract from torrent file), `/opensubHash` + `/opensubHash/{hash}/{idx}` (OpenSubtitles hash for match), `/subtitlesTracks?subsUrl=URL` (enumerate tracks).

- `opensub_hash` (line 17-52): heuristic parses `(hash, fileIdx)` from URL, calls `engine.get_opensub_hash(file_idx)` which reads first 64 KB + last 64 KB of the file and computes the OpenSubtitles u64 hash (implementation at `enginefs/src/engine.rs:283-324`).
- `subtitles_tracks` (line 74-109): calls `engine.find_subtitle_tracks()` which enumerates external subtitle files in the torrent + probes embedded subtitle streams via `ffprobe -show_streams -select_streams s` (implementation at `engine.rs:326-424`). Returns `{ id, lang, label, url }` list.
- `get_subtitles_vtt` (line 111-171): two paths — embedded track (id ≥ 1000) uses `engine.extract_embedded_subtitle(file_idx, track_id)` which spawns `ffmpeg -map 0:s:N -c:s webvtt -f webvtt -` (implementation at `engine.rs:432-484`); external subtitle file (id < 1000) reads the file bytes and runs `enginefs::subtitles::convert_to_vtt(content)` to normalize ASS/SRT/VTT.

**Reference to Tankoban:** Slice A non-goal (StreamEngine.h:30-32 codified 2026-04-17). Sidecar decodes subtitles via libass + `SubtitleRenderer` directly. Stream-A deliberately does not proxy subtitle bytes.

**Divergence:** Tankoban does not carry an OpenSubtitles-hash API on the stream substrate — that's a library-UX track (Agent 5 domain) if adopted. Embedded-subtitle extraction is sidecar-side, not stream-substrate.

**Hypothesis — the OpenSubtitles hash capability may become load-bearing if Tankoban ever adds external subtitle fetching via OpenSubtitles addon.** Currently out of scope. **Agent 5 to own; not Stream-A.**

### system.rs (160 lines sampled)

**Observed:** `/heartbeat` (returns `{success: true}`), `/stats.json` (aggregates `get_all_statistics` across all engines + optional `sys` field for loadavg/cpus), `/network-info` (enumerates non-loopback IPv4 interfaces), `/settings` (GET/POST with `baseUrl` + `values`), `/device-info`, `/hwaccel-profiler`, `/get-https`, `/samples/{filename}`.

- `ServerSettings` struct (line 62-101) contains cache-root + cache-size + proxy-streams-enabled + BT max-connections/timeouts + download-speed-soft/hard limits + transcode-profile + remote-https + cached-trackers.
- The settings surface exists because Stremio is a stand-alone daemon consumed by multiple clients (web, mobile, Android TV, shell). Tankoban is single-tenant in-process.

**Reference to Tankoban:** Slice A non-goal pattern. Tankoban has minimal stats exposure via `StreamEngineStats` struct ([StreamEngine.h:107-121](../../src/core/stream/StreamEngine.h#L107-L121)) emitted via the 5 s telemetry timer; no HTTP endpoint, no client consumption. The 2026-04-16 prior-art audit flagged stats observability as a P1 gap; this Slice A confirms the gap is intentional relative to Stremio's architecture and a strategic choice rather than a defect.

**Hypothesis — a minimal internal stats surface (Agent-4-readable) would reduce diagnostic reliance on qDebug traces.** Already partially addressed by `TANKOBAN_STREAM_TELEMETRY=1` env-var gated structured log at [StreamEngine.cpp:44-98](../../src/core/stream/StreamEngine.cpp#L44-L98). Expansion (e.g., a JSON dump endpoint on the existing StreamHttpServer) would be cheap. **Agent 4 to note in integration memo; consider post-P6 polish.**

### archive.rs (406 lines)

**Observed:** ZIP / RAR / 7Z / TAR / TGZ archive streaming. `/{rar,zip,7zip,tar,tgz}/create` posts an array of `{url}` → fetches each → creates an `ArchiveSession` keyed by UUID in `state.archive_cache` (a `DashMap<String, ArchiveSession>`). `/stream/{key}/{file}` streams an individual file from the archive. Redirection + query-based streaming also supported.

**Reference to Tankoban:** Entirely out of Slice A domain. Archive support in Tankoban is Agent 4B's Sources domain (Tankorent/Tankoyomi); never handled by Stream-A.

**Hypothesis — no Stream-A action needed.** Strategic parity: not required.

### ftp.rs (154 lines)

**Observed:** `/{filename}?lz=<base64-lz-compressed-json>` — decompresses lz-string param → parses `{ftp_url}` → either fetches via reqwest (HTTP/HTTPS scheme) or via `curl` subprocess (FTP scheme). Proxies response body to client.

**Reference to Tankoban:** Stream-A addon protocol supports `StreamSource::Kind::{Http, Url}` at [addon/StreamSource.h:21-22](../../src/core/stream/addon/StreamSource.h#L21-L22) already. FTP is not in our addon's source kind enum. No FTP-capable addons in the Tankoban ecosystem.

**Hypothesis — no Stream-A action needed.** Stream-A's `streamFile(addon::Stream)` dispatch at [StreamEngine.cpp:174-219](../../src/core/stream/StreamEngine.cpp#L174-L219) with its `Url/Http/Magnet/YouTube` switch is the correct boundary.

### nzb.rs (116 lines)

**Observed:** `/nzb/create` (or `/nzb/create/{key}`) posts `{servers: [...], nzbUrl}` → fetches NZB file → creates `NzbSession` in `state.nzb_sessions` (DashMap). `/nzb/stream/{key}/{file}` streams via the Usenet session.

**Reference to Tankoban:** Out of Stream-A domain entirely. Usenet support is not in any Tankoban addon.

**Hypothesis — no Stream-A action needed.**

### youtube.rs (106 lines)

**Observed:** `/yt/{id}` handler — if ID ends `.json` → returns metadata; else redirects to the video URL via `rusty_ytdl::Video::get_info` → filters for MP4 format with audio+video → HTTP 302 Redirect.

**Reference to Tankoban:** [StreamEngine.cpp:207-213](../../src/core/stream/StreamEngine.cpp#L207-L213) returns `UNSUPPORTED_SOURCE` for `StreamSource::Kind::YouTube`. Explicit non-goal per StreamEngine.h:38-41.

**Hypothesis — deliberate boundary.** Not adding YouTube would require either (a) a sidecar-embedded yt-dlp binary, or (b) an addon-layer yt-dlp call that resolves to a direct URL which then hits `streamFile(Kind::Url)`. Either is a strategic parity track, not a Stream-A gap. **Agent 5 / Sources domain to own if Hemanth prioritises.**

### Summary — 6 orphan routes

None of the 6 orphan routes surfaces a Slice A defect or a P2/P3/P4 gate-blocker. All are either (a) Agent 4B / Sources domain (archive, nzb), (b) out-of-scope protocols (ftp), (c) sidecar-side work already codified as non-goals (subtitles, youtube), or (d) multi-tenant daemon architecture not applicable to Tankoban (system settings endpoint).

The stats endpoint (`/stats.json`) is the only one with a plausible future value — an Agent-readable stream-substrate observability surface. Already partially present via env-var structured log + statsSnapshot struct; HTTP exposition would be trivial. Not a gate.

---

## Cross-cutting observations

### Architectural shape

- **Tankoban's Stream-A is a narrow local HTTP bridge** between `TorrentEngine` and the native sidecar — by design, per Slice A non-goals codified at [StreamEngine.h:15-62](../../src/core/stream/StreamEngine.h#L15-L62). The 2026-04-16 prior-art audit's characterisation ("narrow local HTTP bridge" vs Stremio's "broad contract surface") remains accurate and is further reinforced by the 6 orphan route findings.
- **Stremio's stream-server is a multi-tenant daemon** serving web / mobile / TV clients across ZIP/RAR/FTP/NZB/YouTube/torrent/local-addon protocols with HLS transcoding fallback. The contract surface breadth reflects "one server, many clients" not a technical must-have.
- **Mode A root-cause (Q1) isolates to one primitive**: notification-based piece wakeup via `PieceWaiterRegistry` + `piece_finished_alert` alert pump. STREAM_ENGINE_REBUILD P2 lands this primitive in Tankoban (`StreamPieceWaiter` + Agent 4B's `pieceFinished` signal per Congress 5 Amendment). Every other piece-scheduling detail (head deadlines, tail deadlines, file priorities, sequential mode) is secondary tuning on top of that primitive.

### Deadline-per-poll vs deadline-once

- **Stremio re-asserts deadlines on every `poll_read`** via `set_priorities(pos)` at `stream.rs:184`. Every HTTP chunk read triggers recomputation of the priority window based on current position + download-speed EMA + bitrate hint.
- **Tankoban re-asserts head deadlines ONCE in `onMetadataReady`** ([StreamEngine.cpp:988-1029](../../src/core/stream/StreamEngine.cpp#L988-L1029)); the sliding-window deadline re-assertion happens at a caller-driven 1–2 Hz cadence from StreamPlayerController ([updatePlaybackWindow](../../src/core/stream/StreamEngine.cpp#L637-L695)) rather than per-poll.
- **Hypothesis — per-poll re-assertion is a cost we don't need to pay** at Tankoban's HTTP-chunk granularity of 256 KB. 1–2 Hz sliding-window re-assertion should be semantically equivalent if deadlines cover a wide-enough window (20 MB per `updatePlaybackWindow`'s default) to absorb multiple chunks. This directly answers Slice B Q3 (`set_piece_deadline` once per seek vs on a tick) with a concrete reference data point: **Stremio re-asserts per-read; Tankoban's design intends per-sliding-window-tick**. Both are valid; the choice is a CPU-vs-bandwidth trade. **Agent 4 to decide in P3 Prioritizer design; audit position is that per-sliding-window-tick is the right shape for Tankoban, and R12 Session-migration-atomic-batch prevents interleaving hazards.**

### `set_piece_deadline` + `set_piece_priority` pairing

- **Stremio pairs `set_piece_priority(p, 7)` with `set_piece_deadline(p, ms)` always** in `handle.rs:305-311` and `stream.rs:170-173`. Comment at `handle.rs:303-304`: "Both are needed! Priority 7 = highest, deadline = time constraint."
- **Tankoban's onMetadataReady sets head DEADLINES without re-calling priority** — file priority is max-7 via `applyStreamPriorities` but per-piece priority is not explicitly set. **`prepareSeekTarget` at [StreamEngine.cpp:763-765](../../src/core/stream/StreamEngine.cpp#L763-L765) does pair priority-7 + deadline** explicitly per Phase 2.6.3 (post-validation that deadline alone wasn't strong enough to override scheduler).
- **Hypothesis — head-deadlines at onMetadataReady may be under-setting priority by not re-asserting priority-7 per head piece.** File priority 7 is a coarse hint; per-piece priority is the fine-grained dial. **Agent 4 to validate at P2 integration — likely a small safety-net addition to `onMetadataReady`'s head-deadline block to explicitly call `setPiecePriority(p, 7)` per head piece. Qualifies as a trivial in-situ fix (loosened Trigger C eligible) and could land in a post-audit separate commit.**

### Memory-storage vs disk-backed storage

- **Stremio uses libtorrent's memory-only custom storage via `bindings/libtorrent-sys/cpp/memory_storage.cpp`** (per prior-art at `backend/libtorrent/mod.rs:72-74`); pieces live in RAM, optionally spilled to disk cache after completion.
- **Tankoban uses libtorrent's default disk-backed storage**; pieces go straight to the save path. Stream-A non-goal per StreamEngine.h:57-60.
- **Hypothesis — adopting memory-storage is NOT within STREAM_ENGINE_REBUILD scope** and should stay out per Hemanth's 6-point MVP bar. Disk-backed is architecturally simpler + durable across restart + larger-than-RAM-possible. Stremio's memory-first is optimized for a different constraint (browser-based playback with no direct disk access).

### Alert-pump cadence

- **Stremio polls libtorrent alerts every 5 ms** in a tokio task (`backend/libtorrent/mod.rs:204`). This is the load-bearing primitive for `piece_finished_alert` wake latency.
- **Tankoban already has an alert-pump** (from prior-art Axis 8 — `TorrentEngine::m_alertTimer` polls alerts, though the file wasn't re-read this session; the HELP.md ask to Agent 4B for `pieceFinished` signal implies pump exists). The cadence was not confirmed this session.
- **Hypothesis — if our alert pump is currently at a cadence > 5 ms (e.g., 50 ms or 100 ms, typical for Qt default-interval timers), the notification-latency benefit of P2's `StreamPieceWaiter` is capped by our pump cadence, not by waker-overhead.** **Agent 4B to flag pump cadence in the `pieceFinished` signal PR; if > 5 ms, tighten to 5 ms during the PR.**

### Absence of a `/create` endpoint

- Stremio exposes `/create` + `/{hash}/create` routes (`main.rs:214-218`) that handle file-index resolution + peerSearch semantics server-side.
- Tankoban resolves fileIdx at addon-layer (`StreamSource.fileIndex`) + falls back to `autoSelectVideoFile` heuristic at [StreamEngine.cpp:1164-1199](../../src/core/stream/StreamEngine.cpp#L1164-L1199).
- **Non-goal per Slice A** (StreamEngine.h:33-36). Not a gap.

---

## Relationship to prior-art audit (2026-04-16)

The prior-art audit's Slice A is at [agents/audits/stream_a_engine_2026-04-16.md](stream_a_engine_2026-04-16.md). Cross-check per its 11 axes:

| Prior-art axis | 2026-04-18 Slice A verdict |
|---|---|
| **1. Head-piece prefetch + contiguousBytesFromOffset** | Confirmed divergence. Q1 root-caused more precisely: the wait mechanism (poll-sleep vs notification) is the load-bearing primitive, not the gate size. Gate size is a defensive secondary (H2 in prior art still valid — gate may relax post-P2). |
| **2. Byte-range 206 shape** | Confirmed non-divergence on range semantics; route shape divergence already codified as non-goal (StreamEngine.h:43-46). |
| **3. Cache eviction** | Confirmed material divergence (Stremio's cache_cleaner + 5-min engine retention + memory-first). Out of Slice A rebuild scope; adopted as post-P6 strategic parity track if at all. |
| **4. Cancellation propagation** | Confirmed overlap with STREAM_LIFECYCLE_FIX Phase 5; no new Slice A defect surfaced. 2026-04-18 adds: Stremio's RAII-Drop model achieves equivalent safety via `StreamGuard::Drop` + `on_stream_end` + 5 s delayed cleanup. |
| **5. Subtitle extraction** | Confirmed non-goal for Stream-A (StreamEngine.h:30-32 codifies). No action. |
| **6. HLS / transcoding** | Confirmed non-goal (StreamEngine.h:22-27 codifies). Agent 3's P4 3-tier probe shape aligns with Stremio's 3-tier probe budgets — directionally correct. |
| **7. Tracker / DHT** | Confirmed partial divergence; Stremio's ranked-cache + default-tracker list could be post-P6 polish. Not a Slice A gate. |
| **8. Backend abstraction** | Confirmed non-goal (StreamEngine.h:52-55). No action. |
| **9. Stats / health** | Confirmed partial divergence; env-var gated structured log closes the agent-side need. No user-visible stats endpoint. |
| **10. Add-on protocol** | Confirmed narrower surface; `/create` + archive + ftp + nzb + youtube routes re-classified as non-Stream-A in 6-orphan-route review. |
| **11. Memory-only storage** | Confirmed non-goal (StreamEngine.h:57-60). |

The prior art's P0 ("Startup can remain blocked behind 5 MB contiguous head target") stands confirmed as the Mode A root-cause area, but the 2026-04-18 Slice A reframes it: **the gate size is a symptom; the poll-sleep wait mechanism is the primary cause.** P2's notification-based wake primitive is the load-bearing fix; gate tuning is a follow-on.

---

## Cross-slice handoff notes

- **Slice B (Agent 4's next wake)**: answers the Slice B Q3 (`set_piece_deadline` once vs tick) with a concrete reference: Stremio re-asserts per-read; Tankoban's design intent is per-sliding-window-tick. This Slice A audit's "Deadline-per-poll vs deadline-once" finding feeds directly into Slice B's prioritizer-algorithm question.
- **Slice C (Agent 3's upcoming wake)**: the probe/HLS coordination finding in Q3 — Stremio's `get_probe_result` + `probe_cache` pattern + HLS endpoint sharing — is Slice C's territory when it audits the consumer-side `Action::Load` flow. Q3 Hypothesis on probe-cache may influence Slice C's IPC-surface question.
- **Slice D (Agent 3, possibly collapsed)**: no Library-UX findings surfaced in Slice A. The `active_file` single-slot-per-session pattern is Slice D-adjacent in that it reflects a consumer-side constraint (Stremio assumes one active file at a time), but doesn't feed into Library UX semantics.
- **Integration memo (Agent 0)**: the three Q1/Q2/Q3 findings each yield a P2/P3/P4 gate-opener vote:
  - **P2 gate (notification-based piece wait via Agent 4B's `pieceFinished`)**: Q1 → OPEN. Evidence complete; primitive shape well-specified.
  - **P3 gate (prioritizer + SeekClassifier + peersWithPiece)**: Q1 + Q2 → OPEN. Stremio's per-poll re-assertion + SeekType enum maps cleanly to our proposed Prioritizer + SeekClassifier shape; R12 atomic-batch Session migration remains the load-bearing risk.
  - **P4 gate (sidecar probe escalation)**: Q3 → OPEN. Agent 3's 3-tier shape aligns with Stremio's 3-tier probe budgets.
- **HELP.md**: no new asks from Slice A. Agent 4B's existing asks (pieceFinished signal, peersWithPiece, 12-method API freeze) stand.

---

## Implementation notes (advisory; not prescriptions)

Not fix prescriptions — pointers to hand to Agent 0 / P2 / P3 / P4 for shape decisions:

1. **P2 — StreamPieceWaiter design**: `QHash<QPair<QString, int>, QList<QPointer<QObject>>>` registry + Qt signal-on-pieceFinished from TorrentEngine's alert pump. Parallel to Stremio's `RwLock<HashMap<(String, i32), Vec<Waker>>>`. Tankoban's `QWaitCondition` + `QMutex` is a natural Qt idiom; Rust's tokio waker is the same primitive in async-clothing.
2. **P2 safety-net wake**: Stremio has 50 ms / 10 ms / 15 ms tokio sleep safety nets per miss branch (`stream.rs:324-327, 386-388, 424-427`). Our Qt equivalent would be a `QTimer::singleShot(50, ...)` pre-arm. Not strictly necessary if alert-pump is tight (5 ms) but belt-and-suspenders shape.
3. **P3 — per-piece priority + deadline pairing in onMetadataReady** (not just per-file priority): trivial addition, candidate for loosened Trigger C in-situ fix in a post-audit separate commit. See "set_piece_deadline + set_piece_priority pairing" finding.
4. **P4 — probe-cache at sidecar**: not a gate-blocker but a meaningful repeat-open win. See Q3 hypothesis.

Per Rule 14 these are choices for Agent 4 (P2/P3 implementation) / Agent 3 (P4 sidecar) / Agent 0 (integration memo synthesis). Not Hemanth calls.

---

## Audit boundary notes

- I did not compile or run Tankoban.
- I did not modify `src/` or any non-audit file while producing this report. Loosened Trigger C allows in-situ trivial fixes in a separate post-audit commit; one candidate (per-piece priority pairing in onMetadataReady — 2-line addition inside the existing head-deadlines loop) is flagged above but deferred.
- I did not read stremio-core consumer-side (`models/streaming_server.rs`, `models/ctx/update_streams.rs`, `runtime/msg/event.rs`) beyond cross-slice-handoff note. That surface is Slice C's territory.
- I did not read the full 716-line `handle.rs` line-by-line; `prepare_file_for_streaming` + `get_file_reader` + `clear_file_streaming` were full-read, the file-listing + stats helpers were sampled at method boundaries.
- I did not exhaustively read the 6 orphan routes line-by-line beyond router declarations + surface-establishing body samples. For each orphan route the full read was unnecessary given Slice A's non-goal codification at StreamEngine.h.
- I did not perform web-search this pass. Prior-art audit's web citations (libtorrent 2.0.11 reference, RFC 9110, webtor docs) were not re-verified; they carry forward.
- I did not re-audit Tankoban sidecar source (`native_sidecar/src/*.cpp`). Prior-art cites to demuxer.cpp:15-21 + video_decoder.cpp:175 are taken on-face.
- I did not inspect Agent 7's decommissioned prototype directory (`agents/prototypes/`) — outside Slice A scope.
- I did not assert Mode A root-cause as a single-variable fact. The poll-sleep primitive is the load-bearing hypothesis; validation is post-P2 empirical, not this audit.
- I did not prescribe any changes to `STREAM_ENGINE_REBUILD_TODO.md` — that's Agent 0's integration memo.
