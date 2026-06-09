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
- **P1 — Re-wire `StreamPlayerController` into current StreamDetailView/StreamPage → "Play" streams via Stremio.**
  - Add a Play action distinct from Download. Smoke: click episode → instant play (no full download).
  - Threading/process-lifecycle touch → **mandatory cross-engine review** before merge.
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
