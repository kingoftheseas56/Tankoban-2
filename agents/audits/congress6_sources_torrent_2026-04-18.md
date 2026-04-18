# Audit — Congress 6 Slice B: Sources/Torrent substrate + enginefs piece primitives — 2026-04-18

By Agent 4 (Stream mode, domain master + Slice A+B auditor). For Agent 0 integration memo and the P2/P3 gate decisions on `STREAM_ENGINE_REBUILD_TODO.md`.

Reference comparison: `C:\Users\Suprabha\Downloads\Stremio Reference\stream-server-master\` enginefs + backend/libtorrent + bindings/libtorrent-sys (Rust reimplementation of upstream Node.js `server.js`; depends on `libtorrent-sys` FFI to libtorrent-rasterbar — same library family as Tankoban's TorrentEngine).

Scope per Congress 6 Slice B brief: three locked questions — (Q1) Mode B core / priorities.rs algorithm (urgency window sizing + per-piece priority value assignment + window-slide-on-seek semantics), (Q2) P2 piece-waiter / timeout + arrival-before-register + multiple-waiters + lock-ordering, (Q3) Mode B structural / `set_piece_deadline` once-per-seek vs re-assert-on-tick. Out of scope: any fix prescription inside the audit file (loosened Trigger C — trivial in-situ fix notes allowed in a separate post-audit commit); running Tankoban; re-auditing Slices A / C / D.

Prior-art input: [agents/audits/stream_a_engine_2026-04-16.md](stream_a_engine_2026-04-16.md) Axes 1 + 3 + 7 overlap Slice B scope — read, not treated as authority. Already flagged for `_superseded/` on integration close by my Slice A; Slice B extends that supersession.

R21 snapshot: at Slice B audit session entry (2026-04-18), all 7 Stremio Reference subdirs still match the motion-authoring mtimes recorded in [agents/congress_archive/2026-04-18_congress6_stremio_audit.md](../congress_archive/2026-04-18_congress6_stremio_audit.md). No citation drift to flag.

Cross-slice handoff from my Slice A audit [congress6_stream_primary_2026-04-18.md](congress6_stream_primary_2026-04-18.md): Q3 pre-answer flagged — "Stremio re-asserts deadlines on EVERY `poll_read` at `backend/libtorrent/stream.rs:184`; P3 Prioritizer should pick per-sliding-window-tick." Slice B extends that with full deadline-reassert flow from `priorities.rs`.

---

## Methodology

Tankoban files read (fresh this session for Slice B):
- [src/core/torrent/TorrentEngine.h](../../src/core/torrent/TorrentEngine.h) (311 lines — full) — public surface + `pieceFinished` signal declaration + 12-method API freeze
- [src/core/torrent/TorrentEngine.cpp](../../src/core/torrent/TorrentEngine.cpp) (targeted: AlertWorker class 30-217; applySettings 237-326; piece-primitives block 1109-1269; peersWithPiece/peersFor 960-1007) — alert worker architecture + `pieceFinished` emit site + `peersWithPiece` implementation
- [src/core/stream/addon/StreamSource.h](../../src/core/stream/addon/StreamSource.h) (81 lines — full) — addon source-kind enum surface

Stremio Reference files read (fresh this session for Slice B):
- `stream-server-master/enginefs/src/backend/priorities.rs` (328 lines — full including tests) — `calculate_priorities()` function + tests
- `stream-server-master/enginefs/src/piece_cache.rs` (253 lines — full) — moka-based in-memory piece cache + bounded vs unbounded capacity modes + TTI eviction
- `stream-server-master/enginefs/src/backend/libtorrent/constants.rs` (26 lines — full) — DEFAULT_TRACKERS pool
- `stream-server-master/enginefs/src/backend/libtorrent/helpers.rs` (200 lines — full) — CachedPieceData + read_piece_from_disk + default_stats + make_engine_stats
- `stream-server-master/enginefs/src/backend/libtorrent/mod.rs` (lines 350–570 targeted this session; 1–350 read in Slice A) — focus_torrent + resume_all_torrents + pause_all_torrents + add_torrent + Instant Loading metadata cache + tracker-injection + background tracker ranking
- `stream-server-master/server/src/routes/engine.rs` (100 lines sampled — full surface: create_engine + list/remove/remove_all + create_magnet for stremio-core compat)
- `stream-server-master/server/src/routes/peers.rs` (22 lines — full)
- `stream-server-master/server/src/local_addon/mod.rs` (60 lines sampled — router declaration + handler stubs)
- `stream-server-master/bindings/libtorrent-sys/src/lib.rs` (120 lines sampled — cxx::bridge SessionSettings + AddTorrentParams + TorrentStatus struct heads; spot-check per brief scope)
- `stremio-core-development/stremio-core-development/src/types/streams/streams_item.rs` (120 lines — full) — StreamsItem + StreamItemState + `adjusted_state` for source/binge transitions

Prior-art cross-read: `enginefs/src/piece_waiter.rs` (66 lines, read in Slice A), `backend/libtorrent/stream.rs` (494 lines, read in Slice A), `backend/libtorrent/handle.rs` (716 lines, read in Slice A). Re-surfaced here at Q2/Q3 anchor points, not re-read line-by-line.

No compile/run of Tankoban. No web-search this pass.

---

## Q1 — Mode B core: priorities.rs algorithm (line-by-line flow)

**Q1 verbatim:** "In `enginefs/src/backend/priorities.rs`, what is the EXACT algorithm for (a) urgency window sizing, (b) per-piece priority value assignment within the window, (c) window-slide-on-seek semantics? Line-by-line function flow."

### Q1 Observed (Tankoban)

Tankoban has **no `calculate_priorities`-equivalent function**. Priority assignment is scattered across three call-sites:

- **`StreamEngine::applyStreamPriorities(hash, fileIdx, totalFiles)`** at [StreamEngine.cpp:1215-1269](../../src/core/stream/StreamEngine.cpp#L1215-L1269): file-level priority only. Selected file = 7 (max), all others = 1 (very-low per Phase 2.4 peer-preservation fix — was 0/skip pre-2.4, which triggered peer-disconnect cascade on multi-file torrents per [StreamEngine.cpp:1217-1248](../../src/core/stream/StreamEngine.cpp#L1217-L1248) comment). No per-piece priority setting in this function.
- **Head/tail deadline pass in `onMetadataReady`** at [StreamEngine.cpp:988-1117](../../src/core/stream/StreamEngine.cpp#L988-L1117): sets piece deadlines ONLY (not per-piece priority). Head = 500 ms → 5000 ms gradient over first 5 MB. Tail = 6000 ms → 10000 ms gradient over last 3 MB (Phase 2.2 hotfix). Deadlines are set once per stream start.
- **`StreamEngine::prepareSeekTarget(hash, positionSec, durationSec, prefetchBytes=3MB)`** at [StreamEngine.cpp:704-824](../../src/core/stream/StreamEngine.cpp#L704-L824): sets both deadline + priority for seek pieces. Deadline gradient 200 ms → 500 ms over prefetch window. Per-piece priority 7 applied individually at [StreamEngine.cpp:763-765](../../src/core/stream/StreamEngine.cpp#L763-L765) — the only place per-piece priority is combined with deadline in our current code. Called on every seek poll-retry (~300 ms cadence) so deadlines + priorities are re-affirmed on each retry.
- **`StreamEngine::updatePlaybackWindow(hash, positionSec, durationSec, windowBytes=20MB)`** at [StreamEngine.cpp:637-695](../../src/core/stream/StreamEngine.cpp#L637-L695): sets deadline gradient 1000 ms → 8000 ms over the next 20 MB window. Called from StreamPage progressUpdated at ~1 Hz (caller-side rate-limited to 2 s). NO per-piece priority — deadlines only.

### Q1 Reference (Stremio)

Stremio's `calculate_priorities` at `enginefs/src/backend/priorities.rs:56-225` is a **single pure function** that takes seven inputs (`current_piece`, `total_pieces`, `piece_length`, `config`, `priority`, `download_speed`, `bitrate`) and returns `Vec<PriorityItem { piece_idx, deadline }>`. Called from `LibtorrentFileStream::set_priorities` at `backend/libtorrent/stream.rs:142-174` on every `poll_read`.

**(a) Urgency window sizing** — `priorities.rs:72-122`:

1. **urgent_base_pieces**: defaults to 15 (`priorities.rs:78`). If `bitrate` known, `max(15, (bitrate × 15s) / piece_length)` (`priorities.rs:79-82`) — i.e., at least 15 seconds of content.
2. **proactive_bonus_pieces** (`priorities.rs:87-100`): 
   - If bitrate known AND `download_speed > bitrate × 1.5`: `(bitrate × 45s) / piece_length` (45-second lookahead budget — "Expand window up to 60s ahead" per comment at line 91 though code computes 45s).
   - If bitrate unknown AND `download_speed > 5 MB/s`: `+20` pieces (~20 MB).
   - Otherwise: `0`.
3. **urgent_window** (`priorities.rs:103-122`):
   - Cache-enabled branch: `max_pieces = config.size / piece_length`. `urgent = (urgent_base + proactive_bonus).min(max_pieces).min(300)` (hard cap 300 pieces absolute). `buffer = 15.min(max_pieces - urgent)`. Returns `(urgent, buffer, max_pieces)`.
   - Cache-disabled "strict streaming" branch: `urgent = (urgent_base + proactive_bonus).min(50)`. `buffer = 0`. Returns `(urgent, 0, urgent)`.
4. **head_window** (`priorities.rs:131-147`): "Head Window" is the CRITICAL IMMEDIATE FUTURE, target 5 seconds of playback.
   - `target_head_bytes`: if bitrate known, `(bitrate × 5s).max(5 MB).min(50 MB)`. Else `(download_speed × 5s).max(5 MB).min(50 MB)`.
   - `head_window = (target_head_bytes / piece_length).clamp(5, 250)`.
5. **urgent_window re-clamp** (`priorities.rs:150`): `urgent_window = urgent_window.max(head_window + 15)` — urgent must be AT LEAST head plus 15 pieces of body.
6. **Final range** (`priorities.rs:152-167`): `total_window = urgent + buffer`. `end_piece = (start + total_window - 1).min(total_pieces - 1)`. Cache-size clamp: `allowed_end_piece = (start + max_buffer_pieces - 1).min(total_pieces - 1)`. Effective end = min of the two.

Concrete examples from test cases at `priorities.rs:227-328`:
- **Cache-disabled streaming @ 10 MB/s**: 35 pieces total (urgent_base 15 + proactive 20, buffer 0).
- **Cache-enabled @ 10 MB/s, 1 MB pieces, 1 GB cache**: 80 pieces total (urgent 65 = max(35, head 50+15), buffer 15).
- **Cache-enabled @ 20 MB/s, bitrate 10 MB/s, 2 GB cache**: 315 pieces total (urgent hits 300-piece hard cap; buffer 15).
- **Small 200 MB cache, 10 MB pieces**: 20 pieces total (fully clamped by cache limit).

**(b) Per-piece priority value assignment** — `priorities.rs:180-222`, priority-to-deadline by priority LEVEL + distance-from-start:

| Priority level | Formula | Interpretation |
|---|---|---|
| `priority >= 250` (internal probes / metadata) | `50` (flat) | Absolute priority — used by MetadataInspector at `handle.rs:654` |
| `priority >= 100` (seeking / UserScrub-ish) | `10 + distance × 10` | Tight staircase for seek operations |
| `priority == 0` (background pre-caching) | `20000 + distance × 200` | Lazy deadlines |
| Normal streaming (`priority == 1` typical) | Branched by distance: | |
| — `distance < 5` (CRITICAL HEAD) | `10 + distance × 50` → 10/60/110/160/210 ms | Very strict staircase |
| — `distance < head_window` | `250 + (distance - 5) × 50` | Linear staircase post-head |
| — `distance > urgent_base_pieces` (PROACTIVE AREA) | `10000 + distance × 50` | Relaxed 10s+ lookahead |
| — else (BODY) | `5000 + distance × 20` | Standard body |

Note: the function returns only `(piece_idx, deadline)` tuples — it does NOT set libtorrent's `priority` field (0–7). That's done separately in `handle.rs:305-311` (`set_piece_priority(p, 7)` paired with `set_piece_deadline(p, deadline)`) and `stream.rs:170-173` (`handle.set_piece_deadline(item.piece_idx, shared_deadline)` — priority-7 not re-asserted per poll; set once in `get_file_reader`).

**Fair-sharing jitter** at `stream.rs:155-169`: per-stream `jitter = (stream_id % 10) * 5` ms (0–45 ms), added to each deadline to interleave multiple concurrent streams' earliest-first pieces. Not applied to zero-deadline or ≥100000ms background pieces.

**(c) Window-slide-on-seek semantics** — distributed across `stream.rs:62-174` (`set_priorities`) and `stream.rs:440-473` (`start_seek`):

1. **Seek detection at `start_seek` (`stream.rs:440-473`)**: client calls `AsyncSeek::start_seek(SeekFrom::Start(new_pos))` when issuing a new HTTP Range read. Function computes `end_threshold = container_metadata_start(file_size)` (`priorities.rs:16-20`: `file_size.saturating_sub(10 MB).min(file_size × 95 / 100)` — last 10 MB or last 5%, whichever starts earlier).
2. **SeekType assignment** at `stream.rs:455-459`: if `new_pos >= end_threshold` → `SeekType::ContainerMetadata`; else → `SeekType::UserScrub`. Position updated unconditionally. `cached_piece_data` invalidated.
3. **Slide-on-seek at next `poll_read`** → `set_priorities(pos)` at `stream.rs:62-174`:
   - **Cache check** at `stream.rs:77-80`: if `current_piece == last_priorities_piece` → early-return (no slide).
   - **SeekType branch** at `stream.rs:82-109`:
     - `Sequential | InitialPlayback` → just extend window, no cleanup (tracing::trace log only).
     - `ContainerMetadata` → ADD priorities, PRESERVE head pieces (do NOT call `clear_piece_deadlines`). Critical fix — comment at line 96 says "don't wipe out piece 0-7 priorities when reading moov/Cues".
     - `UserScrub` → FULL RESET: `self.handle.clear_piece_deadlines()` at `stream.rs:107`.
   - **SeekType reset** at `stream.rs:112`: after handling → `self.seek_type = SeekType::Sequential` for subsequent reads.
4. **Priority computation** at `stream.rs:129-151`: status read, download_speed EMA update (α=0.2, 5-sample convergence), `calculate_priorities(current_piece, last_piece+1, piece_length, cache_config, priority, ema, bitrate)` returns new `PriorityItem` list. Note `last_piece + 1` not `total_pieces` — scoped to file's piece range, not whole torrent.
5. **Deadline application** at `stream.rs:157-173`: per-piece check that `piece_idx ∈ [first_piece, last_piece]` AND `!handle.have_piece(piece_idx)` (skip already-downloaded pieces). Jitter applied to non-zero non-background deadlines.
6. **InitialPlayback one-shot tail metadata** at `handle.rs:324-333`: if `last_piece > actual_start_piece + window_size`, set `last_piece=1200ms` + `last_piece-1=1250ms` — **once** at `get_file_reader`, not per-poll.

### Q1 Divergence

| Axis | Tankoban | Stremio | Impact |
|---|---|---|---|
| **Function locus** | Split across 3 sites (applyStreamPriorities for files, onMetadataReady head/tail, prepareSeekTarget for seeks, updatePlaybackWindow for sliding window) | Single `calculate_priorities` function returning `Vec<PriorityItem>`; called from one site (`set_priorities` at `stream.rs:184`) | Stremio's single-site shape is the reference for P3 Prioritizer file-split (5-file split in Congress 5 plan) |
| **Urgency window sizing** | Not computed; fixed 5 MB head + 3 MB tail + 20 MB sliding window | Dynamic: `urgent_base (15 or bitrate-scaled 15s) + proactive_bonus (up to 45s-at-bitrate if speed>1.5×bitrate)` clamped to min(cache_max_pieces, 300) | Stremio adapts to bandwidth — high-bandwidth streams get wider window; low-bandwidth narrower. Ours is fixed — over-allocates on slow links, under-allocates on fast links. |
| **Head window shape** | 500 ms → 5000 ms gradient over first 5 MB (pieceCount pieces) | 10 ms / 60 ms / 110 ms / 160 ms / 210 ms strict staircase for first 5 pieces, then 250 + (d-5)×50 ms through head_window | Stremio's URGENT tier is materially tighter. First-playable-piece latency is the load-bearing concern; our 500 ms for piece 0 is 50× Stremio's 10 ms for same position |
| **Bitrate-awareness** | None | `head_window` sized as 5s of bitrate (or download_speed when bitrate unknown), clamped 5-250 pieces | Direct feeder into P3 Prioritizer design; bitrate available via sidecar probe → telemetry on StreamEngineStats extension |
| **Cache-size awareness** | Not modeled | Every window clamped to `config.size / piece_length` (in pieces); separate cache-enabled vs streaming-mode math | Our cache model is disk-backed + delete-on-stop — cache.size isn't a dial. If we add engine-retention (Slice A post-P6 hypothesis) this becomes relevant |
| **Per-piece priority** | Set at head deadline via `applyStreamPriorities` → priority 7 on file, but not per-piece explicitly in head loop. Set per-piece 7 in `prepareSeekTarget` at StreamEngine.cpp:763-765. Not set per-piece in `updatePlaybackWindow` | `handle.set_piece_priority(p, 7)` paired with `handle.set_piece_deadline(p, deadline)` in `get_file_reader` URGENT tier at `handle.rs:305-311`. Not re-asserted per `poll_read` (only deadlines are). | Confirms Slice A cross-cutting finding: head pieces at stream start miss explicit per-piece priority-7. In-situ fix candidate at [StreamEngine.cpp:1016-1028](../../src/core/stream/StreamEngine.cpp#L1016-L1028) stands. |
| **Window-slide on scrub** | `updatePlaybackWindow` re-sets deadlines at 1-2 Hz from StreamPage caller; `prepareSeekTarget` is explicit pre-gate for resume/seek | SeekType enum at `stream.rs:11-20` classifies Sequential / InitialPlayback / UserScrub / ContainerMetadata at seek-time; full-reset (`clear_piece_deadlines`) only on UserScrub, preserve-on-ContainerMetadata | Stremio's SeekClassifier invariant ("preserve head on container-metadata seeks") is a sharp correctness boundary we don't currently honor — our `updatePlaybackWindow` doesn't distinguish seek types. **Hypothesis — Agent 4 to encode SeekType in P3 SeekClassifier**. |
| **Fair-sharing jitter for multi-stream** | None | `stream_id % 10 * 5` ms per-stream jitter | Not relevant today (single stream typical) but would matter if we ever support concurrent stream playback |
| **ContainerMetadata preservation** | Not modeled as a state. Tail-metadata deadlines set once at `onMetadataReady` (6000-10000 ms gradient over last 3 MB) for all seeks regardless | On UserScrub seek: `clear_piece_deadlines()` then rebuild. On ContainerMetadata seek (to last 10 MB / 5 %): NEVER clear — ADD priorities preserving head | Our current code's tail-metadata deadline WILL be cleared on user-scrub in our `clearPieceDeadlines` call — if the user scrubs to mid-video, tail deadline is gone. Stremio never loses tail deadline. **Hypothesis — Agent 4 to preserve tail-metadata deadlines across seek class (except UserScrub-to-tail which is natural)**. |

### Q1 Hypotheses

- **Hypothesis — our head deadline gradient (500 ms → 5000 ms) is ~50× slower than Stremio's first-piece target (10 ms).** libtorrent deadlines are relative; the scheduler's first-frame candidate is the piece with lowest deadline. A 500 ms head-piece-0 deadline means piece-0 is the MOST urgent piece in the torrent — but only by 500 ms vs other pieces' default ~infinite deadline. A 10 ms deadline signals "emergency, schedule RIGHT NOW to the fastest peer". The practical effect depends on libtorrent's scheduling granularity — **Agent 4 to validate empirically at P3**: tighten head-piece-0 to 10 ms and measure first-byte latency delta.
- **Hypothesis — the proactive-bonus expansion (when download_speed > 1.5× bitrate) is Stremio's adaptive buffering equivalent of Netflix-style ABR.** It widens the safety window when the client is demonstrably outrunning the content bitrate. Tankoban has no bitrate detection today (sidecar probe yields bitrate but we don't thread it back to StreamEngine). **Agent 4 to flag for P3 Prioritizer: add `setStreamBitrate(hash, bitrate)` surface or fold into `StreamPlaybackController` telemetry if cross-domain visible**. Not a hard P3 gate; feasible P3 polish.
- **Hypothesis — the SeekType enum is the load-bearing structural primitive P3 must preserve verbatim.** Specifically the ContainerMetadata-preserve invariant. Our current `updatePlaybackWindow` would violate it if the caller passes a position > end_threshold: we'd recompute a 20 MB window starting from near the tail, potentially clearing head-deadlines that were never cleared before on scrub. **Agent 4 to validate at P3: StreamSeekClassifier enum matches Stremio's 4-value exactly; ContainerMetadata branch preserves head + tail deadlines, UserScrub branch clears deadlines (not file priority)**. This directly supports Congress 5 Amendment proposing SeekClassifier as a 5-file-split component.
- **Hypothesis — the 300-piece hard cap on urgent_window is empirically tuned to prevent scheduler saturation.** libtorrent's deadline table supports arbitrary entries but beyond ~300 active deadlines the scheduler's sort-and-dispatch overhead starts costing measurable latency. Below-cap testing would require Stremio authors empirical data we don't have; adopt the 300-cap as a safety constant in P3. **Agent 4 to propose as a Prioritizer compile-time constant**.

---

## Q2 — P2 piece-waiter: timeout + arrival-before-register + multiple-waiters + lock-ordering

**Q2 verbatim:** "In `enginefs/src/piece_waiter.rs`, how does the registry handle (a) a waiter timing out, (b) a piece arriving before the waiter registers, (c) multiple waiters on the same piece? Are there lock-ordering subtleties we would miss in our Qt port?"

### Q2 Observed (Tankoban)

Tankoban **has no piece-waiter registry yet** — the P2 primitive is the load-bearing NEW surface per Congress 5 Amendment 1. Agent 4B has shipped the substrate:

- [TorrentEngine.h:280](../../src/core/torrent/TorrentEngine.h#L280) declares `Q_SIGNAL void pieceFinished(const QString& infoHash, int pieceIndex)`.
- [TorrentEngine.cpp:158-164](../../src/core/torrent/TorrentEngine.cpp#L158-L164) emits the signal from the AlertWorker thread inside `drainAlerts`:
  ```cpp
  else if (auto* pfa = lt::alert_cast<lt::piece_finished_alert>(a)) {
      auto hash = TorrentEngine::hashToHex(pfa->handle);
      const int pieceIdx = static_cast<int>(pfa->piece_index);
      emit m_engine->pieceFinished(hash, pieceIdx);
      if (m_traceActive)
          writeAlertTrace("piece_finished", hash, pieceIdx, -1);
  }
  ```
- [TorrentEngine.cpp:262-269](../../src/core/torrent/TorrentEngine.cpp#L262-L269) unconditionally enables `lt::alert_category::piece_progress` in the alert_mask (previously env-var-gated) — piece-finished alerts always pumped regardless of `TANKOBAN_ALERT_TRACE=1`.
- [TorrentEngine.cpp:52](../../src/core/torrent/TorrentEngine.cpp#L52) the alert pump cadence is `m_session.wait_for_alert(std::chrono::milliseconds(250))` — **250 ms pump interval** (not the 5 ms Stremio uses).
- Current HTTP serving at [StreamHttpServer.cpp:82-108](../../src/core/stream/StreamHttpServer.cpp#L82-L108) uses `waitForPieces` 200 ms poll-sleep — consumer side of the OLD primitive; to be replaced by a `StreamPieceWaiter` that binds to `pieceFinished` signal in P2 Batch 2.2 per Congress 5 plan.
- Agent 4B's HELP.md response notes the cross-thread subtlety at [HELP.md:60](../HELP.md#L60): "`piece_finished_alert` fires on my `AlertWorker` thread; the `pieceFinished` Qt signal crosses threads into wherever `StreamPieceWaiter` lives. Default `AutoConnection` resolves to `QueuedConnection` (safe, one event-loop-tick latency). `DirectConnection` is viable for sub-tick wake if `StreamPieceWaiter::onPieceFinished` is pure `QWaitCondition::wakeAll()` with no shared-state mutation."

No `StreamPieceWaiter` class exists yet — it's a P2 Batch 2.2 deliverable from Congress 5 plan.

### Q2 Reference (Stremio)

Stremio's piece-waiter is **60 lines** at `enginefs/src/piece_waiter.rs:1-66` with three public operations + one internal clear. Full code re-summarised:

```rust
type PieceKey = (String, i32);

pub struct PieceWaiterRegistry {
    waiters: RwLock<HashMap<PieceKey, Vec<Waker>>>,
}

impl PieceWaiterRegistry {
    pub fn register(&self, info_hash: &str, piece: i32, waker: Waker) {
        let key = (info_hash.to_lowercase(), piece);
        self.waiters.write().entry(key).or_default().push(waker);
    }

    pub fn notify_piece_finished(&self, info_hash: &str, piece: i32) {
        let key = (info_hash.to_lowercase(), piece);
        if let Some(waker_list) = self.waiters.write().remove(&key) {
            let count = waker_list.len();
            for waker in waker_list { waker.wake(); }
            if count > 0 { tracing::trace!(...); }
        }
    }

    pub fn clear_torrent(&self, info_hash: &str) {
        let info_hash_lower = info_hash.to_lowercase();
        self.waiters.write().retain(|(hash, _), _| hash != &info_hash_lower);
    }
}
```

This is a `parking_lot::RwLock<HashMap<(String, i32), Vec<Waker>>>` keyed by `(info_hash.lowercase, piece_idx)`.

**(a) Waiter timeout:** Stremio's registry has **NO explicit timeout mechanism**. The registered `Waker` just sits in the list indefinitely until either (i) `notify_piece_finished` wakes it, or (ii) `clear_torrent` drops the entry. Timeout is implicit — the caller in `poll_read` adds a **safety-net tokio sleep** that schedules `waker.wake()` after 10-50 ms regardless, so the read task wakes up and re-enters `poll_read` to re-check state:
- `stream.rs:323-327`: after `register` on piece-not-yet-have → spawn `tokio::time::sleep(50 ms).await; waker.wake();`
- `stream.rs:384-388`: after `register` on downloaded-but-not-in-cache path → 10 ms sleep + wake.
- `stream.rs:423-427`: after `register` on requested-but-pending path → 15 ms sleep + wake.
No libtorrent-level deadline timeout is propagated into the waiter; if libtorrent never delivers the piece, the task re-polls every 50 ms-ish forever (or until the HTTP body stream is dropped, which cascades through `StreamGuard::Drop`).

**(b) Piece arrives before waiter registers:** Stremio handles this **via the check-register-check-wake sequence in `poll_read`**:
1. `stream.rs:318` checks `!self.handle.have_piece(piece)` — if already-downloaded, skips wait path entirely.
2. If not have_piece → `piece_waiter.register(hash, piece, cx.waker().clone())` at `stream.rs:320-321`.
3. Returns `Poll::Pending` with safety-net 50 ms wake at `stream.rs:324-327`.
4. Between step 1 (have_piece check) and step 2 (register), libtorrent's alert pump thread could call `notify_piece_finished` — the Vec<Waker> is empty at that moment, so the notify is a no-op.
5. On the 50 ms safety-net wake → `poll_read` re-enters → step 1 finds `have_piece` = true → serve from cache.

So the "arrival before register" race is **papered over by the 50 ms safety-net timer**. The race window (check-then-register) is ≤ microseconds in practice; the safety net covers it at worst-case 50 ms re-poll.

Note the `notify_piece_finished` at `piece_waiter.rs:33-49` uses `waiters.write().remove(&key)` — it atomically takes the Vec<Waker> out of the map under write-lock, then wakes each waker AFTER releasing the lock (Vec is moved out of the map, then the loop runs on the detached vec). This is correct: new waiters registered AFTER the notify would not see this batch's notification (by design — they'd start their own wait cycle).

**(c) Multiple waiters on the same piece:** The registry **deduplicates by piece-key but not by waker**. Multiple `register(hash, piece, w1); register(hash, piece, w2)` calls both push into the Vec. `notify_piece_finished` drains the entire Vec and wakes all wakers. So multiple concurrent HTTP streams (e.g., HLS segment transcoder + direct stream) reading the same piece would each register a waker; both get woken on piece-finish.

**Lock-ordering subtleties:**
1. **Single lock**: there's only one `RwLock<HashMap>`. No nested-lock ordering issue.
2. **Write-lock on register**: `register` acquires `waiters.write()` even though it's conceptually a "read" — because `entry().or_default().push(...)` mutates the map.
3. **Write-lock on notify**: `notify_piece_finished` acquires `waiters.write()` for `remove(&key)`. Lock is released BEFORE wakers are woken (the Vec is moved out of the map via `Option` unwrap at line 35).
4. **Thread boundary**: `notify_piece_finished` is called from the tokio `alert_pump` task at `backend/libtorrent/mod.rs:244-252` — same-thread family (tokio runtime) as the `poll_read` task that registered. `Waker::wake()` is safe to call from any thread.
5. **Alert pump → notify path**: `alert_pump` holds the `alert_session.write().await` lock (`backend/libtorrent/mod.rs:213`) for the entire loop body including the spawned `put_piece + notify_piece_finished` tasks. Those tasks are spawned — they run after the write-lock releases, so there's no nested lock between session-write-lock and piece_waiter-write-lock.

### Q2 Divergence and Qt-port translation

| Axis | Tankoban (planned P2) | Stremio | Impact |
|---|---|---|---|
| **Registry primitive** | TBD — per Agent 4 Congress 5 position: `QHash<QPair<QString, int>, QList<QPointer<...>>` + `QWaitCondition` per piece, under `QMutex` | `parking_lot::RwLock<HashMap<(String, i32), Vec<Waker>>>` | Qt port uses `QWaitCondition` because Qt doesn't have a `Waker`-equivalent async primitive natively; consumer blocks on `QWaitCondition::wait(mutex, timeout_ms)`. Different shape, equivalent safety class. |
| **Timeout shape** | `StreamPieceWaiter::await(hash, pieceIdx, timeoutMs)` per Congress 5 plan — explicit per-call timeout | None; safety-net tokio::sleep 10/50/15 ms per miss branch | Our Qt port can use `QWaitCondition::wait(mutex, timeoutMs)` which returns `false` on timeout — natural Qt idiom; no need to mirror Stremio's safety-net pattern. Simpler than Rust model. |
| **Arrival-before-register race** | TBD — P2 design must include the check-register-check-wake sequence | Paper-over via 50 ms safety-net tokio sleep | We can use a cleaner pattern: `await` method internally does `if have_piece return true; mutex_lock; wait_cond.wait(mutex, timeout); if have_piece return true; return false;` — the mutex bounds atomicity between check and wait, so no race window. Requires `pieceFinished` signal to be delivered via QueuedConnection so it fires AFTER our mutex unlock. |
| **Multiple waiters** | TBD; likely `QMultiHash` or `QHash<key, QList<QWaitCondition*>>` | `Vec<Waker>` per key | Equivalent; `QWaitCondition::wakeAll()` naturally handles this. |
| **Lock nesting** | `QMutex` held during `await`'s `QWaitCondition::wait` (released during wait per Qt semantics) + alert-thread emits `pieceFinished` which enters QueuedConnection slot → slot acquires same `QMutex` to call `wakeAll` | Single write-lock on `waiters`, no nesting with session-lock (alert-pump releases session-lock before spawning notify task) | **Potential hazard**: if our `StreamPieceWaiter::onPieceFinished` slot runs on the main thread (QueuedConnection) and tries to acquire a mutex held by another main-thread code path, we deadlock. Mitigation: slot body should be pure `wakeAll` with minimal critical-section — Agent 4B's HELP.md advice at line 60 anticipates this. |
| **Cross-thread wake latency** | **Qt signal path: 250 ms alert-pump interval (wait_for_alert timeout at TorrentEngine.cpp:52) + QueuedConnection tick (< 5 ms typically)** | 5 ms alert-pump interval + waker.wake() overhead (sub-ms) | **Our pump is 50× slower than Stremio's.** This caps P2 notification-wake latency at ~250 ms worst-case regardless of how fast `StreamPieceWaiter` responds. **Agent 4B HELP opportunity: tighten wait_for_alert timeout from 250 ms to 5-25 ms to match Stremio-grade wake latency.** |
| **clear_torrent semantics** | TBD | `retain(|(hash, _), _| hash != &info_hash_lower)` drops all keys with matching prefix | Our Qt port needs `QHash` iteration with key-prefix drop. Cheap (torrent-removal is rare). |

### Q2 Hypotheses

- **Hypothesis — 250 ms wait_for_alert interval at [TorrentEngine.cpp:52](../../src/core/torrent/TorrentEngine.cpp#L52) caps P2 wake latency at 50× Stremio's reference.** This is the load-bearing latency primitive for Mode A — the piece-finished alert from libtorrent only leaves the session at the next wait_for_alert tick. 250 ms means the worst-case time from piece-ready-at-libtorrent to Qt signal-emit is 250 ms + alert-pump body overhead. Stremio's 5 ms interval puts this at ~5 ms. **Agent 4B to flag as Slice B gap; mitigate in a separate commit by reducing the interval. Candidate: 5-25 ms.** Safe change (pump-body is light; alert_mask limits noise; resume-save still rate-limited via `RESUME_SAVE_INTERVAL_S = 30s`).
- **Hypothesis — our P2 Qt port can use a simpler wait-with-timeout than Stremio's safety-net pattern.** `QWaitCondition::wait(mutex, timeoutMs)` returns `false` on timeout and releases/reacquires mutex around the wait; combined with a pre-wait `havePiece()` check under mutex and a post-wait `havePiece()` check, the arrival-before-register race is closed at mutex-level. No need for Stremio's 10/50/15 ms re-wake safety nets.
- **Hypothesis — the `DirectConnection` vs `QueuedConnection` decision (per Agent 4B HELP.md:60) should land on QueuedConnection for P2.** `StreamPieceWaiter::onPieceFinished` SHOULD mutate the registry state (at minimum flag "piece arrived" for the waiter to pick up on post-wait check). Mutation off alert thread is the safe default. Stremio's same-thread-tokio model doesn't have a direct analog; our Qt cross-thread model maps cleaner to QueuedConnection. One event-loop-tick latency is negligible relative to the 250 ms pump interval.
- **Hypothesis — `clear_torrent` call-site for our Qt port is `removeTorrent(hash, deleteFiles)` at [TorrentEngine.cpp:1138-1144](../../src/core/torrent/TorrentEngine.cpp#L1138-L1144) (or wherever `StreamPieceWaiter` is told a torrent is gone).** Cheap iteration; called infrequently. **Agent 4 P2 design: add `StreamPieceWaiter::clearTorrent(hash)` call into `StreamEngine::stopStream` at [StreamEngine.cpp:483](../../src/core/stream/StreamEngine.cpp#L483) (just after `clearPieceDeadlines`) to free waiter state synchronously with torrent removal.**

---

## Q3 — Mode B structural: set_piece_deadline once vs re-assert-on-tick

**Q3 verbatim:** "Does Stremio call `set_piece_deadline` ONCE per seek (like our Phase 2.6.3), or does it re-assert on a tick? If once, how does it handle deadline expiry without libtorrent dropping the piece from time-critical tracking? This decides our P3 design between tick-re-assert and long-deadline-once."

### Q3 Observed (Tankoban)

Tankoban re-asserts deadlines **at multiple cadences** depending on call-site:

- **Head deadlines at `onMetadataReady`** ([StreamEngine.cpp:988-1117](../../src/core/stream/StreamEngine.cpp#L988-L1117)): set ONCE per stream start. Values 500 ms → 5000 ms (head) + 6000 ms → 10000 ms (tail). Never re-asserted by StreamEngine after this.
- **Sliding window at `updatePlaybackWindow`** ([StreamEngine.cpp:637-695](../../src/core/stream/StreamEngine.cpp#L637-L695)): re-asserted at 1-2 Hz from StreamPage progressUpdated lambda (rate-limited caller-side to once per 2 s). Values 1000 ms → 8000 ms over 20 MB.
- **Seek pieces at `prepareSeekTarget`** ([StreamEngine.cpp:704-824](../../src/core/stream/StreamEngine.cpp#L704-L824)): re-asserted on every poll-retry while gate is open (~300 ms cadence). Values 200 ms → 500 ms over 3 MB prefetch. Paired with per-piece priority 7 at [StreamEngine.cpp:763-765](../../src/core/stream/StreamEngine.cpp#L763-L765).
- **Clear on stop** ([StreamEngine.cpp:483](../../src/core/stream/StreamEngine.cpp#L483)): `clearPieceDeadlines(hash)` called from `stopStream`.

### Q3 Reference (Stremio)

Stremio **re-asserts deadlines on EVERY `poll_read`** — this was my Slice A cross-slice handoff pre-answer, and Slice B confirms + extends with full flow:

- **Per-poll re-assertion at `stream.rs:183-184`**: `poll_read` body starts with `self.set_priorities(pos)` before the memory-cache read. `set_priorities` at `stream.rs:62-174` computes and sets deadlines for the current-piece window on every poll.
- **Cache-gate at `stream.rs:77-80`**: `if current_piece == last_priorities_piece { return; }` — the ONLY cache break. Same-piece polls skip recomputation. This means re-assertion happens **per-piece-boundary crossing**, not per-byte — which is typically every 1 MB-ish (piece_length) of serve.
- **Per-poll path cost**: `handle.status()` (one libtorrent API call) + `calculate_priorities()` (pure Rust, < 1 ms) + loop over N pieces calling `handle.set_piece_deadline` (O(window) API calls, N ≈ 15-300 pieces). Each `set_piece_deadline` is a libtorrent write-lock acquire. At 1 MB piece size and 50 MB/s serve rate, poll-boundary crossings happen ~50/sec; with 15-piece window that's ~750 `set_piece_deadline` calls/sec — bounded.
- **One-shot tail metadata** at `handle.rs:324-333`: `InitialPlayback` ONLY, the tail two pieces are deadlined 1200 ms / 1250 ms ONCE in `get_file_reader`. Never re-asserted. Since Stremio's urgent tier uses 0 ms deadline for piece 0 + 10 ms staircase, head always outranks tail (1200 >> 210).
- **Metadata inspector critical ranges** at `handle.rs:651-682`: background task ~250 ms after stream start. Finds moov/Cues → sets 150 ms deadlines on those specific pieces ONCE. Since critical-metadata deadlines are SET ONCE, they risk libtorrent expiry/drop if the piece doesn't finish quickly. No re-assert in the 250 ms window — relies on libtorrent's scheduler holding the time-critical flag.
- **On UserScrub**: `stream.rs:107` calls `self.handle.clear_piece_deadlines()`, then the next `set_priorities` rebuilds from scratch. Tail-metadata + critical-ranges are LOST on UserScrub (per code read) unless re-established. There's no code path to re-run `MetadataInspector::find_critical_ranges` on seek — it's `prepare_file_for_streaming`-only.
- **Deadline-expiry behavior of libtorrent**: per upstream docs (cited in our [StreamEngine.cpp:969-976](../../src/core/stream/StreamEngine.cpp#L969-L976) Phase 2.2 comment, not re-fetched this session), `set_piece_deadline(piece, 0)` requests piece ASAP. Once the deadline passes, libtorrent still considers the piece time-critical but with the deadline treated as "overdue" — scheduler priority effectively saturates. Calling `set_piece_deadline` again with a fresh positive value resets the timer. There's no libtorrent-level "piece dropped from time-critical tracking" unless explicitly cleared via `clear_piece_deadlines` or the torrent is removed.

### Q3 Divergence

| Axis | Tankoban | Stremio | Impact |
|---|---|---|---|
| **Head deadline re-assertion** | Once at `onMetadataReady` | Once at `get_file_reader`, then re-asserted on every `poll_read` as the window slides with current position | Our head deadlines age as the reader consumes them; Stremio's stay fresh because `calculate_priorities` always anchors at current reader position |
| **Sliding window re-assertion** | 1-2 Hz caller-driven from StreamPage | Per-poll boundary in `poll_read` — effectively every piece-size-cross, typically 20-50 Hz at serving bandwidth | Stremio's cadence is ~10-50× higher than ours. CPU cost is bounded by pure-Rust `calculate_priorities` + N libtorrent calls; acceptable |
| **Seek pieces re-assertion** | Every poll-retry (~300 ms while seek gate open) | Once per SeekType transition in `start_seek` → reset of `last_priorities_piece` → next `poll_read` rebuilds | Different mechanism, similar effect. Both ensure seek pieces stay live under libtorrent |
| **Tail metadata re-assertion** | Never (once at `onMetadataReady`) | Never (once at `get_file_reader` InitialPlayback branch) — but lost on UserScrub `clear_piece_deadlines` | Both vulnerable to tail-deadline loss on seek. Stremio's ContainerMetadata seek-class PRESERVES deadlines (no clear); UserScrub CLEARS. Our `prepareSeekTarget` calls `clearPieceDeadlines` as part of its Phase 2.6.3 pattern — we lose tail AND head deadlines on every seek, then rebuild head via `updatePlaybackWindow`'s next tick but NEVER rebuild tail |
| **Critical-metadata preservation on seek** | Not modeled — `clearPieceDeadlines` wipes all | UserScrub wipes all; ContainerMetadata preserves; Sequential/InitialPlayback just extends | Direct feeder into P3 SeekClassifier: our Prioritizer needs to preserve tail-metadata deadlines when user seeks mid-video (current position < end_threshold) |
| **libtorrent deadline expiry handling** | Implicit — we rely on libtorrent's "overdue deadlines are still priority" semantic | Same — Stremio also relies on libtorrent's "re-assert" tolerance | Neither system relies on deadline being "current"; both treat deadlines as persistent priority markers that libtorrent re-dispatches against |

### Q3 Answer to the question directly

**Stremio calls `set_piece_deadline` on every `poll_read`** — not once per seek, not on a tick, but on every byte-serve cycle (gated by same-piece cache at `stream.rs:77-80`). The deadline-expiry question doesn't arise in Stremio's model because deadlines are continually refreshed by the advancing reader. For pieces SET ONCE (tail-metadata, critical ranges): Stremio relies on libtorrent's scheduler treating overdue deadlines as "still time-critical, still priority" — which the library does.

**For our P3 design, this decides:**
- **Prioritizer re-assertion cadence = per-sliding-window-tick (1-2 Hz)** from the StreamPlaybackController's existing telemetry tick. NOT per-poll (too high CPU for our serving model which doesn't have an async `poll_read` to hook into). NOT once (we lose the adaptive benefit).
- **SeekClassifier preserves tail-metadata deadlines on UserScrub-to-mid-video** (position < end_threshold). Current `prepareSeekTarget` wipes all — this is a bug per Stremio reference. Fix at P3.
- **Critical-range deadlines**: not applicable to Tankoban today (no MetadataInspector); would only matter if we adopt Stremio's data-driven metadata inspection as post-P6 polish.

### Q3 Hypotheses

- **Hypothesis — P3 Prioritizer should re-assert deadlines at 1-2 Hz (the existing StreamPlaybackController telemetry tick), NOT per-poll.** Tankoban's HTTP serving loop at [StreamHttpServer.cpp:286-367](../../src/core/stream/StreamHttpServer.cpp#L286-L367) reads 256 KB chunks; per-chunk re-assertion would be 50-200 Hz at typical bandwidth — excessive. The existing 1-2 Hz tick from StreamPage `progressUpdated` lambda is the natural hook point, already rate-limited to 2 s in the current code. **Agent 4 to wire in P3**: `StreamPlaybackController::pollStreamStatus` emits existing `bufferedRangesChanged` at 3 Hz; P3 Prioritizer uses the same timer to re-run calculate-priorities-equivalent and call `setPieceDeadlines` batched.
- **Hypothesis — P3 SeekClassifier must preserve tail-metadata deadlines on UserScrub.** Current `prepareSeekTarget` calls `clearPieceDeadlines` before setting new seek deadlines — this wipes the tail-metadata deadlines from `onMetadataReady`. Stremio's `ContainerMetadata` seek-type is the direct reference shape. **Agent 4 P3 design**: after `clearPieceDeadlines` in UserScrub path, re-apply tail-metadata deadlines (3 MB tail, 6000-10000 ms gradient) BEFORE returning. Alternative: split `clearPieceDeadlines` into `clearSeekDeadlines` (scoped to seek window) and `clearAllDeadlines` (full wipe, only on stream stop).
- **Hypothesis — our 300 ms seek poll-retry cadence at `prepareSeekTarget` is load-bearing correct and should be preserved in P3.** Stremio re-asserts on every poll_read which at seek time effectively means "every new Range request from client". Our 300 ms poll matches the same "continuously press the priority+deadline on the target pieces" semantic. **Agent 4 P3 design**: keep the 300 ms retry in SeekClassifier's user-scrub path; don't regress to once-per-seek.
- **Hypothesis — no separate "deadline expiry refresh" mechanism is needed.** libtorrent's scheduler treats overdue deadlines as remaining time-critical. Stremio doesn't re-refresh tail-metadata deadlines post-initial-set, confirming this semantic is safe. Our Phase 2.2 tail-metadata deadlines set ONCE at `onMetadataReady` are correct; the risk is only if they're cleared by `clearPieceDeadlines` on seek (Hypothesis above).

---

## Bindings spot-check (`bindings/libtorrent-sys/`)

Per Slice B brief: **spot-check only — autogenerated FFI, low semantic density**.

Confirmed via head-read at `bindings/libtorrent-sys/src/lib.rs:1-120`: this is a `cxx::bridge`-generated FFI module wrapping libtorrent-rasterbar's C++ API. Structs visible include `SessionSettings`, `AddTorrentParams`, `TorrentStatus`, `FileInfo`, `PeerInfo` — all one-to-one mirrors of the underlying C++ types. No Rust-side policy decisions; pure transport layer.

**Reference to Tankoban**: we use libtorrent-rasterbar directly via C++ headers (no FFI bridge needed — we ARE C++). Our equivalent boundary is `#include <libtorrent/...>` at [TorrentEngine.cpp:1-15](../../src/core/torrent/TorrentEngine.cpp#L1-L15) and the `TorrentEngine::applySettings` (at [TorrentEngine.cpp:237-326](../../src/core/torrent/TorrentEngine.cpp#L237-L326)) which configures the session in native C++.

**Confirmation that Stremio = libtorrent-rasterbar (R11 reframing finalized)**: `bindings/libtorrent-sys/Cargo.toml` (not re-read this session) + `bindings/libtorrent-sys/cpp/memory_storage.cpp` (prior art audit 2026-04-16 Axis 11) confirm the dependency is to libtorrent-rasterbar 2.x — same library family as our libtorrent 2.0.11 build. **No adjustment to Congress 5 R11 reframing**.

No hypothesis; no divergence worth flagging. Surface breadth is naturally identical because both wrap the same C++ library.

---

## Orphan surface — `server/src/routes/{engine,peers}.rs` + `local_addon/`

Per Slice B brief, these are part of scope for "Sources/Torrent substrate" but distinct from the piece-primitive trio. Findings:

### `routes/engine.rs` (100 lines sampled)

**Observed (Stremio)**: `/create` (either `?from=<url>` or `{torrent: <hex>}` payload), `/list`, `/{hash}/remove`, `/removeAll`, `/{hash}/create` (Stremio-core consumer compatibility — accepts `{stream: {infoHash}}` JSON, builds magnet URL, adds torrent). `create_magnet` at `engine.rs:76-100` is the Stremio-core-web protocol endpoint.

**Reference to Tankoban**: Slice A non-goal per [StreamEngine.h:33-36](../../src/core/stream/StreamEngine.h#L33-L36) — `/create` equivalent is the `StreamEngine::streamFile(addon::Stream)` dispatch at [StreamEngine.cpp:174-219](../../src/core/stream/StreamEngine.cpp#L174-L219) + `autoSelectVideoFile` heuristic. Single-tenant native app has no need for HTTP-addressable engine-create.

**No hypothesis.** Intentional boundary.

### `routes/peers.rs` (22 lines — trivially thin)

**Observed**: `/peers/{infoHash}` → `engine.get_peer_stats()`. Current implementation at `enginefs/src/engine.rs:426-429` returns empty Vec ("Placeholder for now"). Stub.

**Reference to Tankoban**: [TorrentEngine::peersFor](../../src/core/torrent/TorrentEngine.cpp#L974-L1007) returns full `QList<PeerInfo>` with ip/port/client/flags/progress/speeds. Consumed by the Peers tab of Tankorent details view. **Tankoban is materially more complete than Stremio here** — Stremio's peers endpoint is a stub.

**Hypothesis — no action needed**. We already have better peer observability; Slice B gap is in Stremio's direction.

### `local_addon/` (6-file module)

**Observed**: `/local-addon/manifest.json` + `/local-addon/catalog/{type}/{id}` + `/local-addon/meta/{type}/{id}` + `/local-addon/stream/{type}/{id}`. Implements Stremio addon protocol backed by a local filesystem scan (`scanner.rs` + `parser.rs` + `resolver.rs`). Serves as a built-in "addon" that exposes local video files as Stremio streams.

**Reference to Tankoban**: we have a Videos library page (Agent 5 domain — Library UX) that enumerates local files directly. We don't expose them via an HTTP-addon protocol because our consumer (our own Qt app) doesn't need the transport indirection.

**No hypothesis**. Not a substrate gap.

---

## `streams_item.rs` — consumer-side cross-reference

**Observed**: `stremio-core-development/stremio-core-development/src/types/streams/streams_item.rs` defines `StreamsItem` + `StreamItemState` + `adjusted_state` logic. Fields: stream + meta_id + video_id + meta_transport_url + stream_transport_url + state (optional subtitle/audio track IDs, delays, offset, playback_speed, player_type). `adjusted_state` at line 97-118 decides what state carries forward:
- **Source match** (same magnet/URL): retain full state.
- **Binge match** (same bingeGroup): retain everything EXCEPT subtitle_delay + audio_delay (reset; new episode may need different sync).
- **Otherwise**: retain only playback_speed + player_type.

**Reference to Tankoban**: this is consumer-side persistence; corresponds to our [src/core/stream/StreamProgress.h](../../src/core/stream/StreamProgress.h) + STREAM_ENGINE_REBUILD P0 `schema_version=1` hardening at `ad2bc65`. Not substrate territory per se; it's the bingeGroup/source-match decision tree for state rollover.

**Hypothesis — StreamProgress.h + StreamLibrary.cpp handoff to next-episode should model `adjusted_state` rules**. Slice D territory (Agent 3 Library UX audit) — not a Slice B gap. Flagged here for cross-slice handoff.

---

## Cross-cutting observations

### Alert-pump cadence is the under-examined P2 latency primitive

Slice A flagged "alert-pump cadence unknown" as a hypothesis. Slice B confirms: `wait_for_alert(250 ms)` at [TorrentEngine.cpp:52](../../src/core/torrent/TorrentEngine.cpp#L52) is 50× slower than Stremio's 5 ms `interval` at `backend/libtorrent/mod.rs:204`. This caps P2 `StreamPieceWaiter` wake-latency floor at ~250 ms regardless of how fast the consumer side responds.

- **In-situ fix candidate (deferred per loosened Trigger C)**: change `250` to `5` (or `10-25` for conservative) at [TorrentEngine.cpp:52](../../src/core/torrent/TorrentEngine.cpp#L52). One-line change. No behavior-change risk beyond higher CPU under idle (alert pump becomes busier). **Candidate for separate post-audit commit** — flagged alongside Slice A's per-piece-priority-pairing in-situ fix. Bundled or separate-commit per Agent 0 / Hemanth call.
- **Caveat**: resume-save rate is `RESUME_SAVE_INTERVAL_S = 30s` at [TorrentEngine.cpp:70](../../src/core/torrent/TorrentEngine.cpp#L70) and is elapsed-checked inside `run()` at [TorrentEngine.cpp:54](../../src/core/torrent/TorrentEngine.cpp#L54), so pump cadence doesn't affect it. `emitProgressEvents` and `checkSeedingRules` are `progressTick >= 4`-gated at [TorrentEngine.cpp:56](../../src/core/torrent/TorrentEngine.cpp#L56) — currently 1s (250ms × 4). If we tighten to 5ms, `progressTick >= 4` means 20ms progress emission — way too fast. Need to convert the tick gate to wall-clock (e.g., `if (progressTick >= 200) emit; reset;` for 1s at 5ms pump interval). Small refactor, not trivial-in-situ.

### Tankoban's P2 design needs explicit cancellation via the waiter registry

Stremio doesn't have an explicit cancellation mechanism for pending waiters — the `StreamGuard::Drop` cascade at `stream.rs:39-58` relies on the HTTP body stream being dropped by the client, which propagates through tokio's `Waker` machinery into the `poll_read` task being cancelled. Any registered wakers become orphans but don't block anything.

Our Qt model doesn't have that automatic cancellation. `StreamPieceWaiter::await` would block on `QWaitCondition::wait` indefinitely if nothing wakes it. Congress 5 plan's cancellation-token pattern (per Session destructor setting token BEFORE teardown, per my Slice A Q2 answer) + an explicit `StreamPieceWaiter::cancel(hash)` call in `stopStream` should cover this. **Agent 4 P2 design**: cancellation token checked under mutex at wait-entry AND slot-side; `stopStream` calls `waiter.cancel(hash)` which `wakeAll` on every waiter for that hash, returning `false` to the caller. Integrates cleanly with existing Phase 5.1 token pattern — no new primitive.

### Stremio's `focus_torrent` / `pause_all_torrents` pattern is post-P6 polish

`focus_torrent` at `backend/libtorrent/mod.rs:386-400` pauses all torrents EXCEPT the streaming one. `resume_all_torrents` at `mod.rs:411-426` unpauses all on stream end. This is a bandwidth-focus primitive for multi-torrent sessions.

Our single-user Tankoban model has ~1-3 concurrent torrents typical and doesn't implement this. Out of P2/P3 scope; post-P6 polish if multi-stream becomes common.

### Instant Loading metadata cache is an independent win

`add_torrent` at `backend/libtorrent/mod.rs:458-495` (Instant Loading Part 1-3): reads metadata from `.metadata/<hash>.torrent` cache directory, bypasses magnet resolution on second open. Cache populated in the slow-monitor task at `mod.rs:293-315` (Instant Loading Part 3 — saves metadata when `has_metadata` goes true).

Our equivalent is fastresume at [TorrentEngine.cpp:136-149](../../src/core/torrent/TorrentEngine.cpp#L136-L149) — we save `.fastresume` on `save_resume_data_alert`. This is a different shape (libtorrent's own resume-data binary format) but similar intent. No direct parity gap.

### `helpers.rs::read_piece_from_disk` is unused / dead-code-annotated

At `backend/libtorrent/helpers.rs:21-93` (`read_piece_from_disk`) and line 8-16 (`CachedPieceData`) — both carry `#[allow(dead_code)]`. Memory-only storage (per Stremio's streaming architecture) means pieces aren't on disk; the function exists for a disk-backed path that isn't active.

Our Tankoban model is disk-backed (pieces on disk under savePath) — the concept of `read_piece_from_disk` maps to `QFile::seek + read` at [StreamHttpServer.cpp:347](../../src/core/stream/StreamHttpServer.cpp#L347). Different architectures; both correct in their domain. No action.

---

## Relationship to prior-art audit (2026-04-16) — extended from Slice A

Slice A cross-checked prior-art's 11 axes and reframed P0. Slice B extends:

| Prior-art axis | Slice A verdict | Slice B extension |
|---|---|---|
| **1. Head-piece prefetch + contiguousBytesFromOffset** | Q1 root-cause reframed: poll-sleep primitive, not gate size | Q1 extended: head-piece deadlines are 50× looser than Stremio's URGENT tier; proactive-bonus expansion logic is entirely absent from Tankoban. |
| **3. Cache eviction** | Out of rebuild scope; post-P6 | Q3 extended: piece_cache.rs moka-TTI-5min is the direct reference if we ever adopt memory-first caching. `from_engine_config` sizing (5 % of disk cache, capped at 512 MB) is the tuning data point. |
| **7. Tracker / DHT** | Partial divergence; post-P6 polish | Constants.rs + mod.rs:518-545 confirm Stremio's default-tracker + rank-and-replace pattern. Our equivalent is `defaultTrackerPool()` at [TorrentEngine.cpp:1150](../../src/core/torrent/TorrentEngine.cpp#L1150)-ish (not re-read this session) + StreamEngine Phase 3.1 fallback pool. Still partial; still post-P6. |

Slice B surfaces no new axes beyond prior-art's 11. Prior art stays superseded to `_superseded/` on Agent 0 integration close.

---

## Cross-slice handoff notes

- **Slice A (Agent 4's Slice A audit)**: Q3 pre-answer confirmed + extended. The Slice A statement "Stremio re-asserts deadlines on EVERY poll_read at backend/libtorrent/stream.rs:184" is correct; Slice B adds that re-assertion is cache-gated to piece-boundary crossings (`stream.rs:77-80` same-piece short-circuit), so effective re-assertion rate is 20-50 Hz at serving bandwidth — not per-byte.
- **Slice C (Agent 3's audit)**: `streams_item.rs::adjusted_state` is the consumer-side source-match / binge-match logic. Slice C may overlap if Agent 3 covered `Action::Load` + StreamItemState persistence; my read here is surface-level, flagged for Slice C cross-reference.
- **Slice D (Agent 3, possibly collapsed to appendix)**: `local_addon/` is a library-UX-adjacent addon surface — meta + catalog + stream endpoints for filesystem discovery. Not substrate; noted for Slice D.
- **Integration memo (Agent 0)**: per my gate votes, unchanged from Slice A:
  - **P2 gate**: Slice B confirms OPEN. Adds nuance: alert-pump cadence (250 ms) is sub-optimal but not blocking; tightening is a separate commit.
  - **P3 gate**: Slice B ANSWERS the central P3 question. `calculate_priorities`-equivalent shape + SeekClassifier preserving ContainerMetadata seeks + per-sliding-window-tick re-assertion at 1-2 Hz. OPEN with clear shape.
  - **P4 gate**: no Slice B touch; Agent 3's Slice C is authoritative. OPEN per Slice A.

---

## Implementation notes (advisory; not prescriptions)

Not fix prescriptions — pointers for Agent 0 integration memo + P2 / P3 shape decisions:

1. **P2 — StreamPieceWaiter registry**: `QHash<QPair<QString, int>, QList<QWaitCondition*>>` (or equivalent) under `QMutex`. `await(hash, pieceIdx, timeoutMs)` does check-wait-check under mutex. `onPieceFinished(hash, pieceIdx)` slot (QueuedConnection from Agent 4B's shipped signal) does `wakeAll` under mutex. `cancel(hash)` iterates + wakes all waiters for that hash. Simpler than Stremio's Vec<Waker> + safety-net tokio-sleep pattern.
2. **P2 — alert-pump cadence tightening**: 250 ms → 5-25 ms. Candidate separate post-audit commit. Needs the progressTick gate converted from tick-count to wall-clock (~20-line change, not trivial-in-situ).
3. **P3 — Prioritizer**: single `computePriorities(currentPiece, fileLastPiece, pieceLength, cacheConfig, priority, dlSpeed, bitrate)` function returning `QList<QPair<int, int>>` (pieceIdx, deadlineMs). Port `calculate_priorities` algorithm verbatim modulo Qt idioms. Call from a 1-2 Hz timer in StreamPlaybackController (not per-HTTP-chunk).
4. **P3 — SeekClassifier**: 4-value enum `{Sequential, InitialPlayback, UserScrub, ContainerMetadata}` with `classify(newPos, fileSize) -> SeekType`. Threshold function matches `container_metadata_start` at `priorities.rs:16-20`.
5. **P3 — tail-metadata preservation on UserScrub**: split `clearPieceDeadlines` into `clearSeekDeadlines(seekRange)` and `clearAllDeadlines()`; UserScrub calls the former (scoped), only `stopStream` calls the latter.
6. **P3 — bitrate feedback from sidecar**: probe result contains bitrate; expose via `StreamPlaybackController::setStreamBitrate(hash, bytesPerSec)` for Prioritizer consumption. Adaptive head_window sizing then matches Stremio.
7. **Post-P6 polish**: bandwidth-focus (`focus_torrent`/`pause_all_torrents`), memory-first piece cache (`piece_cache.rs` moka TTI), data-driven metadata inspector (`MetadataInspector::find_critical_ranges`). Strategic parity tracks; not rebuild-gated.

Per Rule 14 these are choices for Agent 4 (P2/P3 implementation) / Agent 0 (integration memo synthesis). Not Hemanth calls.

---

## Audit boundary notes

- I did not compile or run Tankoban.
- I did not modify `src/` or any non-audit file while producing this report. Loosened Trigger C allows in-situ trivial fixes in a separate post-audit commit; **two** candidates are now flagged across Slices A+B:
  - Slice A: per-piece priority-7 pairing in `onMetadataReady` head-deadlines loop (~2-line addition).
  - Slice B: `wait_for_alert(250 ms)` → `wait_for_alert(5-25 ms)` tightening + progressTick gate conversion from 4-tick-count to wall-clock-elapsed (~15-line refactor).
  Both deferred to a bundled or separate post-audit commit per Agent 0 / Hemanth call.
- I did not re-read every Stremio `backend/libtorrent/` file line-by-line this session. `stream.rs`, `handle.rs`, `mod.rs` top-half were read in Slice A. `constants.rs`, `helpers.rs`, `mod.rs` 350-570 were read fresh this session. `helpers.rs` is ~200 lines sampled via full read; `mod.rs` balance (571+) not re-consumed.
- I did not exhaustively read `local_addon/` beyond `mod.rs` head — per brief scope ("spot-check only" for ancillary routes). `index.rs`, `parser.rs`, `resolver.rs`, `scanner.rs`, `manifest.rs`, `torrent.rs` not read line-by-line.
- I did not perform web-search this pass. libtorrent deadline-semantics assumptions carry forward from prior-art + our Phase 2.2 comments; not independently re-verified.
- I did not inspect Tankoban sidecar source — out of Slice B scope (Slice C territory).
- I did not validate P3 SeekClassifier design by simulating user-scrub + tail-metadata + head-deadline interaction empirically. All such claims are labeled Hypothesis / Agent 4 to validate.
- I did not re-verify the R21 snapshot at audit close (only at audit start). Stremio Reference folder mtimes can theoretically change mid-session; low probability event.
- Assistant 1 adversarial review of Slice A + B is on a later wake — not my hat. My job closes with Slice B landing.
