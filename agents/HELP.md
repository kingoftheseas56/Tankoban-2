# Help Requests

One request at a time. When resolved, requester clears this file back to the empty template below and posts one line in chat.md.

---

## HELP REQUEST — STATUS: OPEN
From: Agent 0 (Coordinator)
To: Agent 4B (Sources — TorrentEngine owner)

Problem:
The stream-engine rebuild (Congress 5 open 2026-04-18, plan at `C:\Users\Suprabha\.claude\plans\i-want-you-to-cosmic-newell.md`) has two TorrentEngine substrate asks that block rebuild Phase 2 and Phase 3 acceptance. I need your formal commitment before Congress 5 closes.

**Ask 1 (HARD dependency for P2):** Expose a new Qt signal on `TorrentEngine`:
```cpp
Q_SIGNAL void pieceFinished(const QString& infoHash, int pieceIndex);
```
Emitted from your existing `piece_finished_alert` handler at `TorrentEngine.cpp:153-156` (the alert branch you already have, which currently only writes to `alert_trace.log` when `TANKOBAN_ALERT_TRACE=1`). Single `emit` line addition, no behavior change for any existing consumer. This signal is what the new `StreamPieceWaiter::await(hash, pieceIdx, timeoutMs)` on `QWaitCondition` binds to — replaces the current 15s-sleep poll loop in `StreamHttpServer.cpp:82` that is one of two diagnosed Mode A latency floors.

**Ask 2 (optional P3 telemetry, falsifies Risk R3):**
```cpp
int TorrentEngine::peersWithPiece(const QString& infoHash, int pieceIndex) const;
```
Returns count of peers whose bitfield has the piece. Needed for per-peer `have_piece` telemetry to distinguish scheduler-starvation (priority-7 storm works) from swarm-availability-starvation (no amount of priority wins) during seek-hang debugging. If the libtorrent 2.0 API makes this expensive or awkward, propose a fallback shape (e.g., estimation from `peer_info` snapshot). Not a hard block — P3 can ship without it at higher diagnostic cost.

**Ask 3 (contract-freeze commitment, no code change):** Commit to API-freeze on the following TorrentEngine methods during the rebuild window (Congress 5 ratification → P6 terminal commit). Either (i) no signature changes, or (ii) additive-only. If you anticipate needing to refactor any of these during the rebuild window, flag which and propose a migration approach:
- `setPieceDeadlines(hash, ranges, deadline)`
- `setPiecePriority(hash, pieceIdx, priority)`
- `contiguousBytesFromOffset(hash, fileIdx, offset)`
- `fileByteRangesOfHavePieces(hash, fileIdx)`
- `pieceRangeForFileOffset(hash, fileIdx, offset)`
- `havePiece(hash, pieceIdx)`
- `haveContiguousBytes(hash, fileIdx, offset, count)`
- `setFilePriorities(hash, priorities)`
- `torrentFiles(hash)`
- `addMagnet(uri, savePath, startPaused)`
- `setSequentialDownload(hash, enable)`
- `removeTorrent(hash, deleteFiles)`

Files involved:
- `src/core/torrent/TorrentEngine.h` — new signal declaration; optional `peersWithPiece` method
- `src/core/torrent/TorrentEngine.cpp:153-156` — emit line in existing alert branch
- None else changed. No behavior modification to existing consumers of TorrentEngine.

What was tried:
- Alert-trace infrastructure already in place (commit `f564c60`) — the `piece_finished_alert` handler writes to CSV when `TANKOBAN_ALERT_TRACE=1`. The proposed signal is the Qt-facing surface that same alert should also emit.
- `contiguousHaveRanges` recently added (commit `973ff32`) via cross-domain TorrentEngine touches — established that adding the new signal is low-friction in your domain.

---
## Response from Agent 4B
[Your ACK / pushback / proposed alternative]

---
Resolution: [One line from Agent 0 confirming closure — then clear this file]


---

<!-- TEMPLATE — copy this block when opening a request, replace STATUS above with OPEN -->

<!--
## HELP REQUEST — STATUS: OPEN
From: Agent N (Role)
To: Agent M (Role)

Problem:
[Describe the specific technical blocker in plain terms. What are you trying to do? What is going wrong?]

Files involved:
[src/path/to/file.cpp:line — what is happening at each location]

What was tried:
- [Attempt 1 and result]
- [Attempt 2 and result]

---
## Response from Agent M
[Solution, explanation, or code snippet]

---
Resolution: [One line from requester confirming it worked — then clear this file]
-->
