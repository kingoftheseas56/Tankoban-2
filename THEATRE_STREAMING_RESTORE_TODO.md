# THEATRE_STREAMING_RESTORE — FIX TODO

**Author:** Agent 4 · **Date:** 2026-06-09 · **Owner:** Agent 4 (Stream / Theatre)
**Directive:** Hemanth, 2026-06-09: *"bring back sources, tankorent and stream server js."*
**Shape chosen by Hemanth (AskUserQuestion 2026-06-09):**
- **Hybrid** — bring back instant-play **streaming** + the manual **Sources picker** + the **Tankorent tab**, AND **keep** today's one-click download-only auto-pick. All options live on a show.
- **Engine = the original Stremio stream-server** (the one proven on Hemanth's Windows), NOT rqbit (folded 2026-06-07, Windows tracker bug) and NOT a new libtorrent streamer.

---

## §1 — Strategic intent

Theatre was collapsed to **download-only** on 2026-05-29 (Hemanth-approved simplification, NOT a
failure of streaming). That arc removed three things at three different depths. This TODO restores all
three in **hybrid** form — streaming returns *alongside* the current download flow, not replacing it.

The Stremio server is the right engine: it was removed for simplicity, it **worked** on Hemanth's
machine (STREAM_SERVER_PIVOT P0/1/2A/2B GREEN; tuning A/B = 65% stall↓), and it is independent of the
rqbit path that folded on a Windows tracker bug ([[project_rqbit_folded_windows_tracker_bug]]).

---

## §2 — What was removed, and where it lives in history

| # | Thing | Removal commit | Depth | Restore difficulty |
|---|-------|----------------|-------|--------------------|
| 1 | **Stremio stream-server** (server.js + stremio-runtime.exe + ffmpeg, ~88 MB; C++ `StreamServerEngine/Client/Process`; `StreamPlayerController`; `StreamTelemetryWriter.h`) | `64213b5` (THEATRE_DOWNLOAD_ONLY P2, 2026-05-29) | **Full delete** — 25 files, 114 405 deletions | **High** — restore files from `64213b5^`, re-wire CMake (refactored since), re-connect controller into drifted StreamPage/StreamDetailView |
| 2 | **Sources picker side-pane** (manual source choice in show-detail) | `318c08f` (THEATRE_DOWNLOAD_SIMPLIFY P4, 2026-05-29) — `StreamDetailView.cpp` 67/35 | Single-file edit | **Moderate** — StreamDetailView.cpp drifted 10+ commits since; re-introduce manually, do NOT blind-revert |
| 3 | **Tankorent tab** (standalone sidebar entry) | `3f152c4` (THEATRE_DOWNLOAD_SIMPLIFY P4.3) — `SidebarDrawer.cpp` 3 lines | 3-line hide | **Trivial** — `SidebarDrawer.cpp` has NO commits since; clean revert. `TankorentPage.{cpp,h}` fully intact, still in MainWindow |

Related show-detail removals (decide per §5 whether in scope): `f7bf977` / `7f20dba` P4.2 (Pack-downloads
button), `e8d19c4` P4.1 (season-pack UI right-pane).

**Retrievability confirmed:** `git cat-file -s 64213b5^:resources/stream_server/stremio-runtime.exe` →
21 796 232 bytes present; `server.js` blob present. All restorable from history.

---

## §3 — Phase breakdown

- **P0 — Restore Stremio files + CMake re-wire → clean BUILD OK (engine present, not yet UI-wired).** ✅ **DONE 2026-06-09.** Restored stremio/{Process,Client,Engine}.{cpp,h} + StreamTypes.h + StreamTelemetryWriter.h + resources/stream_server (88M) from 64213b5^; wired into TankobanSources.cmake + TankobanRuntimeAssets.cmake (copy_directory deploy). Faithful restore, ZERO code edits. BUILD OK (agent4 lane, full reconfigure), 3 stremio .obj confirmed compiled, 88M bundle confirmed deployed next to exe. StreamPlayerController.{cpp,h} restored on disk but held for P1 (where it gets UI-wired). NetSeam migration of StreamServerClient's raw QNAM deferred to P1 (no CI gate today; cheap, do it while editing for wiring).
  - `git checkout 64213b5^ -- src/core/stream/stremio/ src/ui/pages/stream/StreamPlayerController.{cpp,h} src/core/stream/StreamTelemetryWriter.h resources/stream_server/`
  - **StreamTypes.h:** merge the stremio types back into the CURRENT file (it was further touched by `b453e84`); do NOT blind-restore.
  - **CMake:** add the restored sources to `cmake/TankobanSources.cmake` and the `resources/stream_server/*` deploy step to `cmake/TankobanRuntimeAssets.cmake` (the old 70-line CMakeLists.txt block was refactored away — hand-wire, don't revert).
  - Gate: clean-from-scratch `build_check.bat` BUILD OK + `tankoban_tests` green.
- **P1 — Re-wire `StreamPlayerController` into current StreamPage → "Play" streams via Stremio.**
  - Add a Play action distinct from Download. Smoke: click episode → instant play (no full download).
  - Threading/process-lifecycle touch → **mandatory cross-engine review** before merge.
  - **Implementation map (built from git archaeology 2026-06-09):**
    - **Integration surface = `src/ui/pages/StreamPage.{cpp,h}`** (the controller was always owned/wired here, not StreamDetailView).
    - **Reference A — original Stremio wiring:** `git show 85ad939^:src/ui/pages/StreamPage.cpp`. `85ad939` is the THEATRE_DOWNLOAD_ONLY P1 cutover that STOPPED constructing StreamServerEngine + StreamPlayerController; its parent has the live construction + all signal connects.
    - **Reference B — current-tree play→stream routing:** `git show 38a26c9` (reverted rqbit T8, StreamPage.cpp 66 / StreamPage.h 17). Same shape as what we want (episode click → auto-pick source → start stream → on-ready open VideoPlayer), written against the CURRENT drifted StreamPage — the most directly reusable scaffold; just swap the rqbit engine for StreamServerEngine.
    - **Controller API:** ctor `(CoreBridge*, StreamServerEngine*, parent)`; `startStream(imdbId, mediaType, season, episode, const tankostream::addon::Stream& selected)`; `stopStream(StopReason)`. Signals: `readyToPlay(httpUrl)` → open VideoPlayer on the URL; `bufferUpdate(text, pct)` → loading overlay; `streamFailed(msg)` → toast; `streamStopped(reason)` → return to browse.
    - **Steps:** (1) construct StreamServerEngine + StreamPlayerController in StreamPage ctor (CoreBridge is already a ctor arg); (2) connect the 4 signals to the existing VideoPlayer-open / overlay / toast / back paths (mirror 38a26c9); (3) add a Play action alongside Download (do NOT replace the disk-first episode painter / download flow — hybrid); (4) auto-pick a Stream for Play (reuse AutoSourcePicker, same as download auto-pick); (5) NetSeam-migrate StreamServerClient.cpp:36 raw QNAM → `NetSeam::instance()->createManager(this, "stream-server-client")`; (6) add StreamPlayerController.{cpp,h} to TankobanSources.cmake; commit + build + cross-engine review + Hemanth smoke.
    - **Lifecycle caution:** StreamPlayerController.h is dense with STREAM_LIFECYCLE_FIX invariants (StopReason routing, clearSessionState, do-not-mutate-lifecycle). Wire to its public surface; do NOT alter its internal lifecycle. [[feedback_session_lifecycle_pattern]].
  - **DEEPER FINDINGS (2026-06-09 recon, before any code):**
    - **The restored controller is the STREMIO one, not rqbit's.** The rqbit revival (38a26c9) wrote its OWN thin `StreamPlayerController(QObject*)` with `playUrl()`/`closed` — deleted on revert. What P0 restored is the original `(CoreBridge*, StreamServerEngine*)` controller whose `startStream(imdbId,mediaType,season,episode,Stream)` drives the Node subprocess and emits `readyToPlay(httpUrl)` only after buffering. So 38a26c9 gives the HOOK POINTS; 85ad939^ gives the real API usage.
    - **Original Stremio wiring (85ad939^ StreamPage.cpp) is LARGE** — not a 66-line hook. It carries: engine construct+start+cleanup (335-338), controller + 4 connects (346-354), startStream call sites (1037/1669/4062), `onReadyToPlay` opening the player + `_imdbId` property (4073+), stall detect/recover (4166-4169), playback-window deadline retarget per position tick (4279), buffered-range overlay, full `m_session` lifecycle. Re-creating ALL of it against the drifted download-only StreamPage is the gnarliest part of the whole arc.
    - **Scope fork — Option B (minimal viable) recommended for P1 core:** construct StreamServerEngine + StreamPlayerController; connect the 4 core signals (bufferUpdate→overlay, readyToPlay→open VideoPlayer on httpUrl, streamFailed→sources toast, streamStopped→back); trigger `startStream(ctx.imdbId, ctx.mediaType, ctx.season, ctx.episode, chosen.stream)` (the picked `StreamPickerChoice.stream` IS the `addon::Stream` the API needs). DEFER the session-lifecycle polish (stall handling, buffered-range overlay, playback-window retarget) to **P1.x**. This matches the (Hemanth-accepted) rqbit T8 scope.
    - **UX fork (HEMANTH'S CALL — blocks clean impl):** how does Play get triggered? (a) MIRROR rqbit T8 — episode click STREAMS (watch-while-download); the explicit "download for offline" action stays on libtorrent. Click default changes from download→stream. OR (b) ADD a distinct Play button alongside the existing download click (more additive, but needs a StreamDetailView affordance + placement decision). Recommended (a) — Hemanth already accepted that shape for rqbit; it's the proven, lowest-risk default and keeps download via the explicit action.
    - **VideoPlayer open pattern (current):** StreamPage finds the floating player via `window()->findChild<VideoPlayer*>()` (sites at 327/1021/2680/2797) — `onReadyToPlay(httpUrl)` reuses that to open on the stream URL.
    - **Why not auto-executed in the 2026-06-09 autonomous wake:** P1 is lifecycle-critical + carries a genuine UX fork that is Hemanth's to decide, and needs iterative on-device smoke (his streaming feature). Implementing it blind in an unattended window with 40-min build cycles is high-risk/low-reward. Paused at a fully-mapped, ready-to-execute state pending the UX answer.
- **P2 — Restore the Sources picker side-pane in StreamDetailView (manual source choice), alongside auto-download.**
  - Re-introduce against the drifted StreamDetailView; keep the disk-first episode-state painter intact.
- **P3 — Un-hide the Tankorent tab** (`SidebarDrawer.cpp` revert) + decide P4.1/P4.2 pack-button restore (§5).
- **P4 — Hemanth end-to-end smoke:** stream a show; pick a source manually; open Tankorent tab; confirm one-click download still works.

---

## §4 — Acceptance criteria

- **P0:** all restored files compile + link in the current tree; `resources/stream_server/*` deploys next
  to the exe (verify on disk); tests green; no behavior change yet (engine dormant).
- **P1:** clicking an episode's Play streams via the Stremio server (sidecar process spawns, playback
  starts without a completed download); download path untouched.
- **P2:** show-detail offers a manual Sources picker; auto-download still works; episode-state display
  (downloaded/0%/Play) unaffected.
- **P3:** Tankorent tab visible + functional (search → add → download); no regression to Comics/Books.
- **P4:** Hemanth verdict — streaming + manual sources + Tankorent + download all work on his machine.

---

## §5 — Hemanth ratification questions

1. **Pack-download buttons in show-detail** (`P4.1`/`P4.2`, the season-pack right-pane + Packs button) —
   bring those back too, or is the new Sources picker enough? — *Recommended: Sources picker is enough;
   leave the old pack buttons out unless missed.*
2. **88 MB returns to the repo** — server.js + stremio-runtime.exe + ffmpeg come back as tracked deploy
   assets (they're already in git history, so no new bloat to the remote). Confirm OK. — *Recommended: YES,
   same as before the deletion.*

---

## §6 — Risks

1. **CMake drift** — old 70-line block is gone; must hand-wire into TankobanSources.cmake +
   TankobanRuntimeAssets.cmake. Mitigation: clean-from-scratch build gate at P0.
2. **StreamDetailView drift** (10+ commits since removal: episode-state model, disk-first painter, NetSeam,
   progress display). Mitigation: re-introduce Sources pane manually; never blind-revert 318c08f.
3. **Node SEA** — do NOT rcedit `stremio-runtime.exe` ([[feedback_nodejs_sea_rcedit_trap]] — breaks offset → segfault).
4. **Firewall** — Win Firewall Cancel = silent block ([[feedback_stream_server_firewall_gotcha]]).
5. **Shared-tree git** — commit each phase early; never reset --hard ([[feedback_shared_tree_git_is_cross_agent_destructive]]).
6. **Sequencing vs the in-flight IDLE_PROGRESS_SCAN fix** — that fix (TorrentClient.cpp/.h, uncommitted)
   must land FIRST so this arc starts from a clean tree. Different files, no overlap.

---

## §7 — Dependencies / references

- Removal arc specs: `ba87350` (THEATRE_DOWNLOAD_ONLY design), `b8596b9` (THEATRE_DOWNLOAD_SIMPLIFY design).
- Memories: [[project_stream_server_pivot]], [[project_stremio_tuning_ab_result]],
  [[project_rqbit_folded_windows_tracker_bug]], [[feedback_nodejs_sea_rcedit_trap]],
  [[feedback_stream_server_firewall_gotcha]], [[feedback_node_sea_rcedit_trap]], [[project_three_modes]].
- Reference: `project_sidecar_dispatcher_non_blocking_decision` (Source abstraction Phase B was the planned
  seam for multiple stream engines).
