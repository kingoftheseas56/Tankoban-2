# REVIEW PACKET — THEATRE_STREAMING_RESTORE P1 (wire StreamPlayerController → Play)

**Requester:** Agent 4 (Opus) · **Date:** 2026-06-09 · **Reviewer: a DIFFERENT engine** (producer≠reviewer).
**Change class:** process-lifecycle + Qt signal wiring → mandatory cross-engine review before merge.

## What this does (plain)

Theatre was download-only. Hemanth: bring back streaming (hybrid). P0 restored the Stremio
stream-server engine (dormant). P1 wires it: an episode "watch" click now STREAMS the auto-picked
source via `StreamServerEngine` + the restored `StreamPlayerController`, instead of silently
downloading. Every explicit download action (single/season/selected/direct/bulk) still downloads
via libtorrent — download is KEPT.

## Key design point — shared code path, intent flag

`finishAutoDownloadPick()` is reached by BOTH the watch click (`onPlayRequested` → `startAutoDownload`)
AND the explicit single-episode download (`onSingleEpisodeDownloadRequested` → `startAutoDownload`).
To avoid breaking download, I threaded a `bool forStream` through `startAutoDownload` →
`PendingAutoDownload::forStream` and branch in `finishAutoDownloadPick`:
- `forStream == true` (watch click) → `m_playerController->startStream(imdbId, mediaType, season, episode, chosen.stream)` and return.
- `forStream == false` (explicit download; the DEFAULT) → existing `m_torrentClient->startDownload(...)`.

## Wiring

- Engine + controller constructed in `buildUI()` (guarded `if (m_bridge && !m_streamEngine)`):
  `StreamServerEngine(dataDir+"/stream_server_cache")` → start()/cleanupOrphans()/startPeriodicCleanup();
  `StreamPlayerController(m_bridge, m_streamEngine, this)`.
- 4 signal connects: `bufferUpdate`→onBufferUpdate (status to sources panel), `readyToPlay`→onReadyToPlay
  (open VideoPlayer on the httpUrl), `streamFailed`→onStreamFailed (sources error), `streamStopped`→
  onStreamStopped (zero-arg slot; StopReason dropped via Qt fewer-args rule).
- `onReadyToPlay`: find VideoPlayer via `window()->findChild<VideoPlayer*>()`; setPersistenceMode(None) +
  setStreamMode(true); connect closeRequested → stopStream + restore persistence/streamMode; openFile(url).
- NetSeam migration: `StreamServerClient::ensureNam()` now uses `NetSeam::instance()->createManager(this,
  "stream-server-client")` instead of a raw `new QNetworkAccessManager` (Congress 9 observability).

## Definition of Done — verify each, adversarially

1. The watch click streams; the explicit download actions STILL download (the `forStream` branch is correct
   and no download caller accidentally passes `forStream=true`). Only `onPlayRequested`'s call site passes true.
2. `onReadyToPlay`'s `closeRequested` lambda captures `player` + `this` safely — player is MainWindow-owned,
   StreamPage is the connection context (auto-disconnect on StreamPage death). No use-after-free.
3. `disconnect(player, &VideoPlayer::closeRequested, this, nullptr)` before connect prevents double-fire on
   re-stream (lambda can't be deduped by UniqueConnection).
4. No leak/double-construct: engine+controller guarded by `!m_streamEngine`; buildUI runs once.
5. `stopStream()` on close → engine tears down the session (no orphaned stremio-runtime stream / staging).
6. Lifecycle: I did NOT modify StreamPlayerController internals (its STREAM_LIFECYCLE_FIX invariants intact).
7. Deferred (NOT in scope, intentionally): stall recovery, buffered-range overlay, playback-window retarget
   (P1.x). Confirm nothing half-wired references those.

## Concerns to probe
- Does constructing StreamServerEngine in buildUI() spawn stremio-runtime.exe at Theatre-page build even if
  the user never streams? (Original did this too.) Acceptable, but flag if it's a problem.
- Any path where `m_playerController` is used before buildUI() runs? (finishAutoDownloadPick null-checks it.)

## Diff
See `agents/audits/theatre_restore_p1_diff.txt` (StreamPage.{cpp,h} + StreamServerClient.cpp + CMake).
The restored StreamPlayerController.{cpp,h} are verbatim from 64213b5^ (unchanged) — not re-reviewed here.
