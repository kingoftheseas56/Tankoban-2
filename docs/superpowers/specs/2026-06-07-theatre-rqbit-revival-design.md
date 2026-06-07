# Theatre Revival on rqbit — Design Spec

- **Date:** 2026-06-07
- **Owner:** Agent 4 (Stream mode + Tankorent)
- **Status:** Design approved (Hemanth) — pending spec review, then implementation plan
- **Arc:** THEATRE_RQBIT_REVIVAL
- **Restore reference commit:** `eeedc92` (last fully-working old Theatre, immediately before the 2026-05-29 download-only cutover)

## 1. Motivation

Theatre was converted to **download-only** on 2026-05-29 (THEATRE_DOWNLOAD_ONLY + THEATRE_DOWNLOAD_SIMPLIFY). That conversion is the root of a stack of fragility surfaced over 2026-06-06/07 — addon-timeout aborts, a 1-at-a-time download queue, blank/stale episode rows, a 1127× `FOREIGN KEY` persistence flood, and Torrentio HTTP/2 failures. Each was a real bug fixed in isolation, but the underlying loss was architectural: the old Theatre — **a Sources pane offering both Torrentio and Tankorent sources, each clickable to stream (watch-while-download) or download** — was a more complete, more robust product.

This arc brings that back, with one deliberate substitution: the deleted Stremio Node stream-server is replaced by **rqbit** (a modern Rust torrent engine with a first-class HTTP streaming API). The download-only fixes already landed this week stay in place and keep today's Theatre working until this revival supersedes it.

## 2. Ground truth (archaeology)

**The 2026-05-29 cutover was surgical.** Files DELETED between `eeedc92` and HEAD (the *only* things gone):
- `src/core/stream/stremio/StreamServerEngine.{cpp,h}`, `StreamServerClient.{cpp,h}`, `StreamServerProcess.{cpp,h}` — the stream-server driver
- `src/ui/pages/stream/StreamPlayerController.{cpp,h}` — drove watch-while-download playback
- `src/core/stream/StreamTelemetryWriter.h`, `StreamTypes.h`
- `resources/stream_server/*` — the ~88MB Stremio Node runtime + ffmpeg DLLs

**Still in the tree, only unwired from `StreamDetailView`** (the cutover commits explicitly said "left intact for future streaming"):
- `src/ui/pages/stream/StreamSourceList.{cpp,h}`, `StreamSourceCard.*`, `StreamSourceChoice.*` — the inline Sources side-pane
- `src/ui/pages/stream/TorrentPackPicker.*`, `PackListItem.*` — the Tankorent pack picker
- `src/ui/pages/stream/TheatreDownloadPanel.*`
- `src/core/stream/UnifiedPackSearchEngine.*`, `src/core/TankorentSearchService.*` — the search engines
- `src/ui/pages/TankorentPage.*` + the standalone Tankorent sidebar tab (hidden by `3f152c4`, not deleted)

**Old wiring (at `eeedc92`, `StreamDetailView.cpp`):** built `m_sourcesList = new StreamSourceList(...)` and connected `sourceActivated` (→ play via `StreamPlayerController`), `directDownloadRequested`, `addToTankorentRequested`, `autoLaunchCancelRequested`.

**rqbit facts (verified):** Rust; ships prebuilt Win/Mac/Linux binaries; runs headless (`rqbit server start <dir>`); HTTP API on `127.0.0.1:3030` by default — `POST /torrents` (magnet/http/file), `GET /torrents/{id}/stream/{file_idx}` (**Range-seekable**, prioritizes streamed pieces), `GET /torrents/{id}/stats/v1`, `POST /torrents/{id}/pause|start|delete`. The standalone binary is "a small wrapper over `librqbit`," and its HTTP API is the same facade rqbit's own desktop app consumes.

## 3. Decision: Approach A — subprocess rqbit + existing libtorrent

rqbit runs as a **headless subprocess**; we talk to its HTTP API. rqbit handles **streaming**; the existing libtorrent `TorrentClient` keeps handling **offline downloads**. This mirrors the old Theatre's two-engine shape (stream-server + libtorrent) and is fully additive.

**Rejected — embed `librqbit` as a linked Rust library (FFI):** there are no existing C/C++ bindings for `librqbit`, its API is async/Tokio (requiring a hand-written runtime bridge across the FFI boundary), and it would bolt a Rust/cargo toolchain onto our C++/Qt/CMake/vcpkg build — for little gain, since streaming is HTTP either way. The subprocess path is the proven pattern (the deleted `StreamServer*` did exactly this) and uses rqbit as intended for an external consumer.

**Rejected — rqbit as sole Theatre engine (retire libtorrent for Theatre):** large rework + risk, discards this week's libtorrent fixes, and not phase-friendly. Left as a possible future consolidation, not now.

## 4. Architecture

```
StreamDetailView (show detail)
  └─ StreamSourceList (Sources pane: Torrentio + Tankorent cards)
        ├─ sourceActivated ........→ RqbitEngine.startStream(magnet) → player on HTTP stream URL   [STREAM]
        ├─ directDownloadRequested →  TorrentClient.startDownload(...) (existing libtorrent path)   [DOWNLOAD/OFFLINE]
        └─ addToTankorentRequested →  Tankorent
  └─ TorrentPackPicker (Tankorent pack picker)  [Phase 3]

Sources data:
  Torrentio  ← StreamAggregator (+ AddonTransport, HTTP/1.1 fix already landed)
  Tankorent  ← UnifiedPackSearchEngine / TankorentSearchService

Engines:
  rqbit subprocess (127.0.0.1:<port>)  → streaming (watch-while-download)
  libtorrent TorrentClient             → offline downloads + in-library tracking (StreamDownloadIndex)
```

The frozen `TorrentEngine` API (Congress-6) and the Comics/Tankoyomi libtorrent paths are untouched.

## 5. Components

### 5.1 rqbit engine layer (NEW — `src/core/stream/rqbit/`)
Modeled on the deleted `StreamServer*` trio:
- **`RqbitProcess`** — owns the subprocess: spawn `rqbit` headless bound to `127.0.0.1:<port>` with a Theatre download/staging dir; health-check by polling the HTTP API after spawn; graceful shutdown on app exit; restart-on-crash with a clear surfaced error. Binary vendored at `resources/rqbit/rqbit.exe` (Windows now; Mac/Linux when those ports land), deployed beside `Tankoban.exe` by the build (same POST_BUILD copy pattern the ffmpeg sidecar uses).
- **`RqbitClient`** — thin REST client (reuses NetSeam/QNAM): `addTorrent(magnet) → id`, `streamUrl(id, fileIdx)`, `stats(id)`, `pause/start/delete(id)`. JSON in/out.
- **`RqbitEngine`** — orchestrates Process + Client and is the only surface the UI touches: `startStream(magnetOrInfoHash) → {streamUrl, torrentId, fileIndex}` (adds torrent, selects the largest video file), `stats(torrentId)`, `stop(torrentId)`. Emits progress/ready/error signals.

### 5.2 Streaming play flow
`sourceActivated(choice)` → `RqbitEngine.startStream(choice.magnet)` → returns `http://127.0.0.1:<port>/torrents/<id>/stream/<idx>` → open the player on that URL. The ffmpeg sidecar already plays HTTP URLs with range support (it served the old stream-server), so the player side is proven. **Restore `StreamPlayerController`** from `eeedc92`, trimmed to: take the rqbit stream URL, open the player, poll `RqbitEngine.stats` for buffering/progress, tear down (stop/keep the rqbit torrent) on close.

### 5.3 Sources-pane rewire (`StreamDetailView`)
Restore the `eeedc92` wiring into today's `StreamDetailView`: rebuild `m_sourcesList` (`StreamSourceList`), populate from `StreamAggregator` (Torrentio) + `UnifiedPackSearchEngine`/`TankorentSearchService` (Tankorent), and connect the three actions per §4. Today's auto-download episode-click behavior is replaced by the source-pane flow (click episode → sources → pick → stream/download).

### 5.4 Tankorent surfaces (Phase 3)
Restore `TorrentPackPicker`/`PackListItem` in the show detail and un-hide the standalone Tankorent sidebar tab (revert `3f152c4`'s hide). Machinery is intact; this is re-wiring + un-hiding.

### 5.5 Offline downloads (keep both)
"Download for offline" routes to the existing libtorrent path (`TorrentClient` + `TransferQueue` + `StreamDownloadIndex` + this week's fixes) — unchanged. **Promotion (Phase 2):** when a title currently streaming via rqbit is marked "keep offline," promote rqbit's already-downloaded file into the library instead of re-fetching via libtorrent. Phase 1 treats stream and download as independent actions.

## 6. Phasing

- **Phase 1 — rqbit engine + streaming.** Build `RqbitProcess/Client/Engine`, vendor + deploy the rqbit binary, restore a trimmed `StreamPlayerController`. For the trigger, reuse the **existing auto-pick** (top Torrentio source via `AutoSourcePicker`) — an episode click streams the auto-picked source through rqbit instead of auto-downloading it. (Manual source selection via the Sources pane arrives in Phase 2; Phase 1 proves the engine + play loop end-to-end without UI rework.) *Ships:* click → plays while downloading. *Smoke:* One Piece episode streams, seeks, no stall.
- **Phase 2 — Sources pane.** Re-wire `StreamSourceList` into `StreamDetailView`; both Torrentio + Tankorent cards; pick a card to stream; offline-promotion. *Ships:* the Sources pane is back.
- **Phase 3 — Tankorent surfaces.** Pack picker + standalone tab. *Ships:* full old Theatre.

Each phase ends with a Hemanth running-app smoke (the only "done" gate) and a different-engine review of the diff against this spec before merge.

## 7. Error handling
- **rqbit lifecycle:** spawn-fail / port-in-use (pick a free port or retry) / crash → `RqbitProcess` restart + clear surfaced error (mirror `StreamServerProcess`).
- **Firewall:** rqbit needs outbound for peers — same Windows-firewall foot-gun as libtorrent; tell = peers>0 + 0 B/s for 30s.
- **No seeders / stream stall:** surface in the player/loading overlay, never a silent hang.
- **Source fetch failures:** already handled — per-addon errors are non-fatal (amatsu-abort fix) and addon HTTP forces HTTP/1.1 (HTTP/2 fix).

## 8. Testing
- Unit: `RqbitClient` against a mocked HTTP API (add/stream-url/stats/delete); `RqbitEngine` lifecycle + file selection; stream-URL construction.
- Live smoke per phase (the real gate): stream a well-seeded title, confirm playback starts before full download + seeking works.
- Regression guard: the existing `tankoban_tests` suite stays green (pre-existing `PickBestBookFileTest.FileInSubdir_MovedToRoot` failure is unrelated/known).

## 9. Constraints / do-not-break
- `TorrentEngine.h` API + `pieceFinished` signal are Congress-6 FROZEN — untouched (rqbit is separate).
- Comics/Tankoyomi libtorrent download paths — untouched.
- The week's landed fixes (queue parallelism, amatsu-abort, FK persistence, HTTP/2, UI-display, Tankorent poll-gating) — preserved; they keep today's download-only Theatre working until this revival lands.
- libtorrent Windows path separators (`[\\/]`).

## 10. Deferred / out of scope
- Mac/Linux rqbit binaries (Windows first; cross-platform when those ports land).
- Consolidating to rqbit-as-sole-Theatre-engine (Approach B) — possible future, not now.
- Audio-dub track selection, advanced pack heuristics — as the old arcs had them; not new here.
