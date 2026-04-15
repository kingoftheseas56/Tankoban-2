# Stream Playback Fix TODO

Opened 2026-04-15 in response to two user-reported playback bugs in torrent-backed streaming. Driven by [agents/audits/tankostream_playback_2026-04-15.md](agents/audits/tankostream_playback_2026-04-15.md) — Agent 7 comparative audit vs Stremio / Peerflix / torrent-stream / Webtor.

Owner: Agent 4 (Stream & Sources).
Review: Agent 6 per phase, objective = audit P0/P1 citations.
Workflow: one batch per rebuild per `feedback_one_fix_per_rebuild.md`.
Pre-existing work paused: STREAM_UX_PARITY_TODO.md Phase 4.2+ (history + URL paste). Resumes after this TODO closes.

## Bugs addressed

1. **15-20% of the video buffers before playback starts.** Stremio / Peerflix play after a few MB of sequential head. Root cause (per audit): byte-gate of 2 MB is smaller than the sidecar's own probe size (5 MB local / 20 MB HTTP); `sequential_download` is a soft preference libtorrent itself documents as sub-optimal for streaming; no `set_piece_deadline()` retargeting.

2. **Mid-playback jumps from 0:05 → 5:16.** Root cause (per audit): `StreamEngine` returns a local sparse file path to ffmpeg after the 2 MB gate. ffmpeg reads past the downloaded frontier, hits zero-filled holes, resyncs forward to the next keyframe. All reference apps keep a torrent-aware userspace layer between decoder and incomplete file.

## Architectural decision

**Option A — harden the existing `StreamHttpServer` as the hot path, layered with `set_piece_deadline()` streaming primitives on `TorrentEngine`.** Reuses the Peerflix / torrent-stream / Webtor model; the HTTP server is already written (just needs hardening); inherits sidecar HTTP retry/stall logic at [native_sidecar/src/video_decoder.cpp:807,834,850](native_sidecar/src/video_decoder.cpp#L807).

Option B (custom ffmpeg `AVIOContext` in the sidecar with a new sync-IPC piece-availability channel) was considered and deferred. ~2-5 days work including new IPC; current JSON-over-stdin/stdout isn't the right shape for per-read piece queries. Revisit only if HTTP latency proves a bottleneck after Phase 2 deadlines land.

---

## Phase 1 — Torrent-aware read boundary (closes Bug 2 — random jumps)

### Batch 1.1 — Route magnet playback through HTTP server, not direct file path

- Remove the direct-file-return branch at [src/core/stream/StreamEngine.cpp:219-244](src/core/stream/StreamEngine.cpp#L219). Let `buildStreamUrl()` at [src/core/stream/StreamEngine.cpp:446](src/core/stream/StreamEngine.cpp#L446) be the sole return path for torrent playback.
- Closes the P2 header/impl disagreement: [StreamEngine.h:21,54](src/core/stream/StreamEngine.h#L21) documents magnet playback as HTTP-URL-based; the implementation silently contradicted that.
- StreamPage's consumer already names the variable `httpUrl` at [src/ui/pages/StreamPage.cpp:1162](src/ui/pages/StreamPage.cpp#L1162) — no caller changes needed.
- Side effect: VideoPlayer's sidecar HTTP-retry logic at [native_sidecar/src/video_decoder.cpp:166-180](native_sidecar/src/video_decoder.cpp#L166) now activates for torrent playback — reconnect, 30s retry loop, stall-buffering events work for free.
- **Success:** torrent playback URL printed by StreamEngine is `http://127.0.0.1:{port}/stream/{hash}/{fileIndex}`; no sparse-file jumps in a 10-minute continuous playback test.

### Batch 1.2 — Fix `waitForPieces` ignored-return bug

- Single-block fix at [src/core/stream/StreamHttpServer.cpp:203-218](src/core/stream/StreamHttpServer.cpp#L203). Capture and check the `waitForPieces` return.
- On timeout: break the serving loop cleanly with an explicit `Connection: close` and no further `file.read` — ffmpeg surfaces `AVERROR(EIO)` which routes through the existing sidecar retry path at [video_decoder.cpp:834](native_sidecar/src/video_decoder.cpp#L834).
- Add `qWarning` log on piece timeout — current silent failure makes production debugging impossible.
- Add `Content-Type: text/plain` on error responses (minor ffmpeg compatibility; some decoders warn on type-less 4xx bodies).
- **Success:** a simulated slow-seed torrent no longer produces silent garbled playback; the sidecar emits a buffering event instead; playback resumes when pieces arrive.

### Batch 1.3 — Per-connection deadline + graceful shutdown

- Add 30s idle-timeout on each `handleConnection` thread at [src/core/stream/StreamHttpServer.cpp:88](src/core/stream/StreamHttpServer.cpp#L88). Prevents hung sockets on genuinely stuck reads.
- On `stop()` at [StreamHttpServer.cpp:252-261](src/core/stream/StreamHttpServer.cpp#L252), track in-flight connection descriptors so shutdown force-closes them rather than leaving them dangling.
- Graceful error propagation: socket write failures explicitly flush + close with `Connection: close` trailer (currently silent break at line 213).
- **Success:** closing the player while mid-stream releases the file handle within 1s; restarting a stream immediately after close doesn't hit a locked-file error.

### Phase 1 exit

- Three batches shipped and smoke-passed.
- Agent 6 review against audit P0 #1 (sparse-file exposure) and P1 #2 (HTTP server safety).
- `READY FOR REVIEW — [Agent 4, STREAM_PLAYBACK_FIX Phase 1]` line in chat.md.

---

## Phase 2 — Piece-priority streaming model (closes Bug 1 bulk)

### Batch 2.1 — `set_piece_deadline` primitive on TorrentEngine

- Add `TorrentEngine::setPieceDeadlines(infoHash, QList<QPair<int piece, int msFromNow>>)` wrapping `handle.set_piece_deadline(piece, ms)`. Reuses lock pattern at [src/core/torrent/TorrentEngine.cpp:472-482](src/core/torrent/TorrentEngine.cpp#L472).
- Add `TorrentEngine::clearPieceDeadlines(infoHash)` wrapping `handle.clear_piece_deadlines()`.
- Add `TorrentEngine::pieceRangeForFileOffset(infoHash, fileIndex, offset, length) → QPair<int,int>` wrapping `ti->map_file()` logic already proven in [haveContiguousBytes at line 753](src/core/torrent/TorrentEngine.cpp#L753).
- Add `TorrentEngine::contiguousBytesFromOffset(infoHash, fileIndex, offset) → qint64` — counts bytes from `offset` in the selected file until the first non-`have_piece()` piece is hit. Used by Phase 3's buffering % fix too.
- No streaming behavior change in this batch — primitives only, cheap to test in isolation.
- **Success:** a small exercise test (or manual sidecar-off invocation) confirms deadlines are accepted by libtorrent; no existing path regresses.

### Batch 2.2 — Head deadline on stream start

- In `StreamEngine::onMetadataReady` at [StreamEngine.cpp:354](src/core/stream/StreamEngine.cpp#L354), after `applyStreamPriorities`, call `setPieceDeadlines` on the first ~5 MB of pieces from offset 0 with an aggressive gradient deadline (500ms → 5000ms across the window).
- Keep the `sequential_download` flag set (cheap, complementary).
- Keep `applyStreamPriorities` (file-level skip of non-selected files) — deadlines are additive to priorities.
- **Success:** time-from-click-to-first-frame drops noticeably on well-seeded magnets (compare before/after with one fixed public-domain magnet).

### Batch 2.3 — Sliding-window deadline retargeting

- Listen to VideoPlayer's existing `progressUpdated(path, positionSec, durationSec)` signal at [VideoPlayer.h:90](src/ui/player/VideoPlayer.h#L90) — StreamPage already connects at [StreamPage.cpp:1043](src/ui/pages/StreamPage.cpp#L1043). Add one more lambda.
- On each progress tick: convert `positionSec` → byte offset (via duration + file size) → piece range → call `setPieceDeadlines` on the next ~20 MB window of pieces.
- Rate-limit to once per 2s to avoid thrashing libtorrent.
- Clear stale deadlines on playback stop / back-to-browse (hook into `resetNextEpisodePrefetch` cleanup paths already in place).
- **Success:** during long continuous playback, the download reader-frontier stays ~20 MB ahead of the playback-frontier; no mid-stream stalls on bandwidth-adequate networks.

### Batch 2.4 — Seek / resume target pre-gate

- On `onSourceActivated` with `startPositionSec > 0` (resume) OR on any mid-playback seek routed through StreamPlayerController, convert `positionSec` → byte offset → piece range → call `setPieceDeadlines` with 200-500ms deadlines BEFORE handing to the player.
- Block player launch (or surface a short "Seeking..." toast) until first ~3 MB of the target window is contiguous via existing `haveContiguousBytes`. Matches Webtor's seek-restart pattern at [content-transcoder/services/transcode_run.go:117](https://github.com/webtor-io/content-transcoder/blob/90b9c9f2a700a4a78325137b815bab7fc45532f2/services/transcode_run.go#L117).
- Side-carry: fix the warm-sidecar bug Agent 7 flagged at [VideoPlayer.cpp:325](src/ui/player/VideoPlayer.cpp#L325) — `sendOpen(filePath)` must pass `m_pendingStartSec` in both the warm and cold sidecar branches, not only the cold one at line 397.
- **Success:** resume-at-47:00 pre-buffers ~3MB of the target window before the player opens; no "lands at 0:00 instead of 47:00" regressions.

### Phase 2 exit

- Four batches shipped and smoke-passed.
- Agent 6 review against audit P0 #2 (deadline streaming primitives absent) and P1 #3 (resume pre-gate).
- `READY FOR REVIEW — [Agent 4, STREAM_PLAYBACK_FIX Phase 2]` line in chat.md.

---

## Phase 3 — Startup gate + UX alignment (P1 fixes)

### Batch 3.1 — Align startup gate with sidecar probe size (or remove entirely)

- Current `MIN_HEADER_BYTES = 2 MB` at [StreamEngine.cpp:209](src/core/stream/StreamEngine.cpp#L209). Sidecar probes 5 MB for local paths ([demuxer.cpp:15,21](native_sidecar/src/demuxer.cpp#L15)) and larger for HTTP.
- With Phase 1 + 2 landed, HTTP + deadlines already enforce the torrent-frontier boundary. The byte-gate in StreamEngine becomes a double-gate — remove it entirely, or keep it minimal (512 KB just to avoid the "stream has literally zero bytes" edge case).
- **Decision point in this batch:** drop the gate vs lower it to 512 KB. Ship whichever measures faster click-to-play on the same test magnet used in Batch 2.2.
- **Success:** click-to-play latency reduced further; no regression in sparse-read symptoms (Phase 1 guarantees prevent it).

### Batch 3.2 — Buffering % shows gate progress, not whole-file progress

- At [StreamEngine.cpp:212-215](src/core/stream/StreamEngine.cpp#L212), change `pct = 100 * downloaded / totalSize` to `pct = 100 * contiguousBytesFromOffset(…, 0) / gateSize`. Reaches 100% exactly when playback starts; no more misleading "Buffering 15%" while real gate progress is 80%.
- Reuses the `TorrentEngine::contiguousBytesFromOffset` primitive added in Batch 2.1.
- Ripple into [StreamPlayerController.cpp:125-155](src/ui/pages/stream/StreamPlayerController.cpp#L125) — emit the gate-progress percentage via the existing `bufferUpdate` signal; no downstream UI changes needed.
- **Success:** buffering bar visibly reaches 100% at the moment playback begins (not stuck at some fraction for several seconds while the gate hasn't crossed).

### Batch 3.3 — Session-settings tuning pass

- Read Stremio's reference values at `C:/Users/Suprabha/Downloads/Stremio Reference/stremio-core-development/stremio-core-development/src/types/streaming_server/settings.rs` (specifically `bt_max_connections`, `bt_min_peers_for_playback`, `bt_request_timeout`, soft/hard speed limits).
- Tune at [TorrentEngine.cpp:169-199](src/core/torrent/TorrentEngine.cpp#L169): raise `max_queued_disk_bytes`, `read_cache_line_size`, consider increased `connections_limit` / `active_downloads`. Comment at line 194 admits current settings are "less aggressive than streaming" — this fixes that.
- **Conservative approach:** change one setting per micro-batch, measure time-to-play impact on the same test magnet. May land as 2-3 micro-batches (3.3a, 3.3b, 3.3c) if the diffs are independently measurable.
- **Success:** a measurable click-to-play improvement compared to post-Phase-2 baseline; no regression in non-streaming torrent-download throughput.

### Phase 3 exit

- Three batches (possibly more if 3.3 splits) shipped and smoke-passed.
- Agent 6 review against audit P1 #1 (buffering % misalignment), P2 #2 (session-settings tuning).
- `READY FOR REVIEW — [Agent 4, STREAM_PLAYBACK_FIX Phase 3]` line in chat.md.

---

## TODO exit criteria

- Both user-reported bugs closed: click-to-play under ~10s for well-seeded titles; no mid-stream jumps in 10-minute continuous playback.
- All three Agent 6 phase reviews PASSED.
- Hemanth green on functional verification.
- STREAM_UX_PARITY_TODO.md Phase 4.2 resumes.

## Files

**Modified:**
- `src/core/stream/StreamEngine.h` (documentation pass; possibly public helper additions)
- `src/core/stream/StreamEngine.cpp` (direct-file-return removal, deadline wiring, gate change, buffering % source)
- `src/core/stream/StreamHttpServer.cpp` (waitForPieces return fix, timeouts, logging, graceful shutdown)
- `src/core/torrent/TorrentEngine.h/.cpp` (new deadline primitives, contiguousBytesFromOffset, session-settings tune)
- `src/ui/pages/StreamPage.cpp` (progressUpdated → deadline sliding window)
- `src/ui/pages/stream/StreamPlayerController.cpp` (new buffering % source)
- `src/ui/player/VideoPlayer.cpp` (warm-sidecar startPositionSec side-carry)

**New:** none.

**Explicitly not touched:**
- `native_sidecar/` — sidecar stays as-is; its HTTP retry logic is exactly what we're inheriting.
- `src/ui/pages/stream/Stream*View.cpp` / `Stream*Widget.cpp` — Phase 4/5 UI surface, unrelated.
- Phase 3 detail-view density work just shipped — do not disturb.

## Risks + mitigations

1. **HTTP latency on sync piece waits could stutter playback.** Mitigation: Phase 2's deadlines keep pieces arriving ahead of the reader; HTTP wait only on genuine stalls (which sparse-file reads "solved" by returning zeros — strictly worse). If measurable stutter appears post-Phase-2, Option B (custom AVIO) remains available as a follow-up track.
2. **`set_piece_deadline` may conflict with `sequential_download` flag.** Reference: libtorrent docs suggest deadlines supersede. Keep both enabled; deadlines take effect for flagged pieces, sequential acts as a tiebreaker elsewhere.
3. **Progress-signal rate could thrash deadline API.** Mitigation: 2s rate-limit on deadline retargeting in Batch 2.3.
4. **Session-settings tune could regress non-streaming torrent performance.** Mitigation: measure before/after on non-streaming paths; scope per-torrent if needed.
