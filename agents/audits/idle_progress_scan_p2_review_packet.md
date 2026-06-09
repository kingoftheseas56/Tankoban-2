# REVIEW PACKET — IDLE_PROGRESS_SCAN_FIX P2 (off-thread per-file progress scan)

**Requester:** Agent 4 (Opus) · **Date:** 2026-06-09
**Producer:** prior Agent 4 instance (working-tree, uncommitted) · **Reviewer must be a DIFFERENT engine** (producer≠reviewer).
**Change class:** THREADING change → mandatory cross-model review before merge (TODO §6 + brotherhood norm).

## What the change is (plain)

The app went "Not Responding" at idle while downloading a big season pack (One Piece, hundreds of files),
with NO video player open. Root cause: `TorrentClient::processPieceFinishedProgress()` ran an O(files)
per-file progress scan + JSON re-parse **on the GUI thread** on every debounced piece-finished tick.

- **P1 (already in tree, committed concept):** cache the parsed pack (`m_parsedPackCache`) so `parsePack`
  runs once per torrent, not per tick.
- **P2 (this diff):** move the `torrentFileProgress()` gather + percentage math **off the GUI thread** via
  `QtConcurrent::run`, marshal only the small per-episode `{season, episode, pct}` result set back to the
  GUI thread via `QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection)` to call
  `m_streamDownloadIndex->updateEpisodeProgress(...)`.

## Definition of Done — verify each, adversarially

1. `parsePack` runs **once per torrent** (cache hit on subsequent ticks), invalidated only when the file
   list changes (`clearPieceProgressState` removes `m_parsedPackCache[hash]`).
2. The per-file gather + pct math runs **off the GUI thread**; no O(files) work left on the GUI thread per
   tick (acknowledged: the one-time cache-miss `torrentFiles()`+`parsePack()` still runs on GUI once per
   torrent — acceptable).
3. Only the small result set is marshaled back; `updateEpisodeProgress` is called on the GUI thread.
4. The `pieceFinished` signal / its emit site / the `Qt::QueuedConnection` wiring are **untouched**
   (Congress-6 frozen `022c4eb`).
5. No per-tick SQL (`getTorrent`) reintroduced — still uses `m_pieceMetaCache`.

## Threading-correctness questions (the reason this needs review)

A. **Off-GUI engine read safety.** The worker calls `engine->torrentFileProgress(hash)` off-thread.
   `TorrentEngine::torrentFileProgress` takes `QMutexLocker lock(&m_mutex)` and uses a libtorrent
   `torrent_handle` (handle methods are thread-safe by design). **Is this genuinely safe under concurrent
   AlertWorker mutation of `m_records`?** Look for any path that touches engine state WITHOUT the mutex.

B. **`this` lifetime / use-after-free.** The worker captures `this` and posts a queued lambda back to
   `this`. The destructor does `waitForFinished()` on all `m_pieceProgressWorkers` futures, then clears.
   `~QObject` drops pending posted events for `this`. **Is there any window where the worker dereferences
   `this` or `m_engine` after destruction, or where `waitForFinished` deadlocks (it must not — the
   marshal-back uses QueuedConnection which only posts, never blocks)?**

C. **Rerun coalescing / runaway.** While a worker runs, further ticks set `m_pieceProgressWorkerPending`.
   On completion the marshal-back re-invokes `processPieceFinishedProgress(hash)` exactly once if pending.
   **Can this become a tight CPU loop, or is it bounded to one extra run per burst?** Note the rerun path
   bypasses the `onPieceFinished` debounce — is that a problem?

D. **Data races on `m_*` members.** The worker body touches only captured-by-value data + the mutex-guarded
   engine. All `m_pieceProgressWorkers` / `m_pieceProgressWorkerPending` / `m_streamDownloadIndex` /
   `m_pieceMetaCache` access happens in the GUI-thread marshal-back. **Confirm no `m_*` member is read or
   written from the worker thread body.**

E. **`m_pieceProgressWorkers.insert(hash, QFuture<void>{})` then reassign** — any race? (All on GUI thread.)

## My (Agent 4) pre-assessment — confirm or refute

I believe A–E are all safe (mutex-guarded engine, destructor-wait + removePostedEvents, bounded single
rerun, value-captures only, single-threaded GUI mutation). I want an independent engine to try to REFUTE
that. Default to flagging if uncertain.

## Diff

See `agents/audits/idle_progress_scan_p2_review_diff.txt` (full working-tree diff of TorrentClient.cpp/.h).
