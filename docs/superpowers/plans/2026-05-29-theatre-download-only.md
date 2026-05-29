# THEATRE_DOWNLOAD_ONLY Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.
>
> **Lifecycle:** active. **Spec:** `docs/superpowers/specs/2026-05-29-theatre-download-only-design.md`.

**Goal:** Make Theatre mode download-only — playing a not-downloaded title routes to the existing download flow; the Stremio stream-server subprocess and all live-streaming code are removed.

**Architecture:** Two phases. **Phase 1 (behavior cutover):** stop creating/driving the stream-server + `StreamPlayerController`; reroute the play entry points so "play" means *play-local-if-owned, else download*. This alone kills the "Resolving metadata" hang and stops any `stremio-runtime` subprocess from spawning. **Phase 2 (dead-code removal):** delete the now-unreferenced stream-server layer, the controller, the ~22 MB bundled binary, and their CMake/resource wiring. The download + local-file-play paths already exist and are untouched.

**Tech Stack:** C++20, Qt6, CMake/Ninja, libtorrent (`TorrentClient`). No new dependencies — this arc only removes + rewires.

---

## Verification model (read first)

- This is Qt UI/subprocess plumbing — there are **no unit tests** for it (`tankoban_tests` is pure-logic only). Each task gates on **`build_check.bat` BUILD OK + a targeted smoke**, not on a unit test.
- **Build in an isolated lane**, never shared `out/`: run `build_check.bat` with `$env:TANKOBAN_BUILD_LANE='agent4'` via the **PowerShell** tool with the **absolute path** (`& "c:\Users\Suprabha\Desktop\Tankoban 2\build_check.bat"`). The Bash-tool `cmd /c build_check.bat` silently no-ops (MSYS mangles `/c`), and shared `out/` collides with other agents. (Lessons: `feedback_no_concurrent_builds_same_out_dir`, `feedback_build_check_invocation`.)
- **Verify a build actually ran** by checking `out_agent4\Tankoban.exe` mtime advanced — not just exit 0.
- **Commit per phase** (gov-v9, Hemanth-authorized), bracketed format `[Agent 4 (Opus), THEATRE_DOWNLOAD_ONLY]: …`. Stage only this arc's files (shared tree has other agents' dirty work).

## File structure

| File | Action | Responsibility after |
|------|--------|----------------------|
| `src/ui/pages/StreamPage.{cpp,h}` | **Modify** | Owns the Theatre page; play entry points reroute to download-or-local; no longer creates the engine/controller |
| `src/ui/pages/stream/StreamPlayerController.{h,cpp}` | **Delete** (Phase 2) | (gone — it was purely the streaming controller) |
| `src/core/stream/stremio/StreamServer{Process,Client,Engine}.{h,cpp}` | **Delete** (Phase 2) | (gone) |
| `resources/stream_server/` | **Delete** (Phase 2) | (gone — ~22 MB binary bundle) |
| `src/core/stream/StreamTypes.h` | **Modify** (Phase 2) | Collapse playback mode to local-file only |
| `src/core/stream/StreamTelemetryWriter.{h,cpp}` | **Modify** (Phase 2) | Drop engine-lifecycle events |
| `CMakeLists.txt` | **Modify** (Phase 2) | Remove stream-server + controller sources/headers + the `stream_server/` resource-copy block |
| **Untouched (verified independent):** `MetaAggregator`, `AddonRegistry`, `CatalogAggregator`, `StreamAggregator`, `SubtitlesAggregator` (discovery); `TorrentClient`, `BulkSourceCollector`, `StreamBulkPlan`, `TheatreDownloadPanel`, `TorrentPackPicker`, `StreamDownloadIndex`, `TransferQueue` (download); `MainWindow::onPlayLocalFileFromStreamRequested` + `VideoPlayer::openFile` (local play). | — | — |

---

## PHASE 1 — Behavior cutover (kills the hang; no deletions yet)

### Task 1.1: Reroute the play entry points to play-local-or-download

**Files:**
- Modify: `src/ui/pages/StreamPage.cpp` — the three `m_playerController->startStream(...)` call sites (currently ~lines 1037, 1669, 4062) + the `StreamSourceChoice` dispatch (~3127-3130).
- Modify: `src/ui/pages/StreamPage.h` if a small helper is added.

- [ ] **Step 1: Read the current play sites.** Read `StreamPage.cpp` around lines 1000-1060, 1620-1680, 4040-4100, and 3120-3175. Identify, for each `startStream(...)` call, the `imdbId / mediaType / season / episode` in scope and the selected `Stream`.

- [ ] **Step 2: Add a single reroute helper** `StreamPage::beginPlayOrDownload(imdbId, mediaType, season, episode)` (declare in `.h`, define in `.cpp`). Logic:

```cpp
void StreamPage::beginPlayOrDownload(const QString& imdbId, const QString& mediaType,
                                     int season, int episode)
{
    // Download-only Theatre: if we already own the file, play it locally;
    // otherwise open the source picker + download flow (no streaming).
    if (m_streamPage_downloadIndex /* StreamDownloadIndex* */) {
        const auto path = m_streamPage_downloadIndex->filePathFor(imdbId, season, episode);
        if (path.has_value() && QFileInfo::exists(*path)) {
            emit playLocalFileFromStreamRequested(*path, imdbId, /*showTitle*/ QString(),
                                                  season, episode);
            return;
        }
    }
    openDownloadFlowForEpisode(imdbId, mediaType, season, episode);  // Step 3
}
```
  Use the StreamDownloadIndex pointer already available to StreamPage (the same one passed to the Downloads page / used for the "Downloaded" badge — grep `isStreamOwned`/`StreamDownloadIndex` in StreamPage to find the member name; the snippet's `m_streamPage_downloadIndex` is a placeholder for that real member). Reuse the existing local-play emit (`playLocalFileFromStreamRequested`, already emitted at ~3950).

- [ ] **Step 3: Wire `openDownloadFlowForEpisode` to the existing download path.** It must do exactly what the current `[Download]` affordance does for one episode — open `TheatreDownloadPanel` / the source picker (`TorrentPackPicker`) scoped to this episode so the user picks a source, then it downloads via `TorrentClient`. Read how the existing download UI is triggered (the `TheatreDownloadPanel` creation ~827 and its `downloadRequested` wiring ~891, and any "download this episode" entry in `StreamDetailView`) and call into that same code. Do not duplicate the download logic — route to it.

- [ ] **Step 4: Replace the three `startStream(...)` call sites** with `beginPlayOrDownload(imdbId, mediaType, season, episode)`. Also point the `StreamSourceChoice` dispatch (~3127) at the download flow rather than `startStream`.

- [ ] **Step 5: Remove the engine stall-signal wiring** at `StreamPage.cpp` ~4127-4169 (the `m_streamEngine->stallDetected/stallRecovered` connects) — these reference the engine we're about to stop creating. Leave `m_streamEngine` itself for Task 1.2.

- [ ] **Step 6: Build (isolated lane) + smoke.**
  Run: `& "c:\Users\Suprabha\Desktop\Tankoban 2\build_check.bat"` with `$env:TANKOBAN_BUILD_LANE='agent4'`. Expected: `BUILD OK`.
  Smoke (Hemanth or agent via `build_and_run.bat`): open a show → click an episode you have NOT downloaded → the **source picker opens** (no "Resolving metadata"); click an episode you HAVE downloaded → it **plays from disk**.

- [ ] **Step 7: Commit.** `[Agent 4 (Opus), THEATRE_DOWNLOAD_ONLY]: P1.1 reroute play -> download-or-local; drop engine stall wiring`

### Task 1.2: Stop creating the stream-server engine + the streaming controller

**Files:** Modify `src/ui/pages/StreamPage.cpp` (~12, ~335, ~346-353) + `src/ui/pages/StreamPage.h`.

- [ ] **Step 1: Remove engine + controller construction.** Delete `m_streamEngine = new StreamServerEngine(...)` (~335) and `m_playerController = new StreamPlayerController(...)` (~346). Remove the four `connect(m_playerController, …)` signal hookups (~347-353).

- [ ] **Step 2: Remove the now-dead controller-signal handler slots** in StreamPage (the `bufferUpdate` / `readyToPlay` / `streamFailed` / `streamStopped` handlers, and any `currentInfoHash`/buffer-overlay code that only existed for streaming). Grep `m_playerController` and `m_streamEngine` in `StreamPage.{cpp,h}` and remove every remaining reference; remove the members + the `#include "core/stream/stremio/StreamServerEngine.h"` (~12) and the `StreamPlayerController` include.

- [ ] **Step 3: Confirm the local-play path is intact.** `m_detailView`'s `playLocalFileFromStreamRequested` connect (~1050) and the `emit playLocalFileFromStreamRequested` (~3950) must remain — that's how downloaded files play.

- [ ] **Step 4: Build (isolated lane) + smoke.** `BUILD OK`. Then `build_and_run.bat` → **launch and confirm NO `stremio-runtime.exe` process spawns** (`Get-Process stremio-runtime` empty), and the play/download/local-play flows from Task 1.1 still work.

- [ ] **Step 5: Commit.** `[Agent 4 (Opus), THEATRE_DOWNLOAD_ONLY]: P1.2 stop creating StreamServerEngine + StreamPlayerController (no subprocess spawns)`

**End of Phase 1: Theatre is download-only in behavior; the hang and the subprocess are gone. The stremio/ + controller files still exist on disk but are fully unreferenced — verified by Task 2.1/2.2 greps.**

---

## PHASE 2 — Dead-code + binary removal

### Task 2.1: Delete StreamPlayerController

**Files:** Delete `src/ui/pages/stream/StreamPlayerController.{h,cpp}`; Modify `CMakeLists.txt`.

- [ ] **Step 1: Grep for references.** `grep -rn "StreamPlayerController" src/ CMakeLists.txt`. Expected after Phase 1: only the CMake source/header entries + the file's own definition. If any live code reference remains, STOP — Phase 1 missed a site; fix it first.
- [ ] **Step 2: Delete the two files.** `git rm src/ui/pages/stream/StreamPlayerController.h src/ui/pages/stream/StreamPlayerController.cpp`.
- [ ] **Step 3: Remove its CMake entries** (source + header lists).
- [ ] **Step 4: Build (isolated lane).** `BUILD OK`.
- [ ] **Step 5: Commit.** `[Agent 4 (Opus), THEATRE_DOWNLOAD_ONLY]: P2.1 delete StreamPlayerController`

### Task 2.2: Delete the stream-server layer

**Files:** Delete `src/core/stream/stremio/` (StreamServerProcess/Client/Engine `.h`+`.cpp`); Modify `CMakeLists.txt`.

- [ ] **Step 1: Grep for references.** `grep -rn "StreamServerEngine\|StreamServerProcess\|StreamServerClient\|stremio/StreamServer" src/ CMakeLists.txt`. Expected: only the CMake entries + the files' own definitions. Any live reference → STOP and resolve.
- [ ] **Step 2: Delete the directory.** `git rm -r src/core/stream/stremio/`.
- [ ] **Step 3: Remove the 3 source + 3 header CMake entries** (~lines 227-233, 430-436).
- [ ] **Step 4: Build (isolated lane).** `BUILD OK`.
- [ ] **Step 5: Commit.** `[Agent 4 (Opus), THEATRE_DOWNLOAD_ONLY]: P2.2 delete stremio stream-server layer`

### Task 2.3: Delete the bundled stream-server binary + CMake resource block

**Files:** Delete `resources/stream_server/`; Modify `CMakeLists.txt` (~686-728 resource-copy block).

- [ ] **Step 1: Delete the binary bundle.** `git rm -r resources/stream_server/` (the ~22 MB `stremio-runtime.exe`, `server.js`, bundled ffmpeg `.dll`s, `LICENSE.stream-server.txt`).
- [ ] **Step 2: Remove the `add_custom_command` resource-copy block** that copied `stream_server/` into the build output (~686-728).
- [ ] **Step 3: Clean-from-scratch build (isolated lane).** Delete `out_agent4\.ninja_log` first to force a full rebuild, then `build_check.bat`. Expected `BUILD OK` and a much smaller output tree (no `stream_server/` under `out_agent4`).
- [ ] **Step 4: Commit.** `[Agent 4 (Opus), THEATRE_DOWNLOAD_ONLY]: P2.3 remove bundled stremio-runtime binary + resource-copy block`

### Task 2.4: Simplify types + telemetry + drop the vestigial flag

**Files:** Modify `src/core/stream/StreamTypes.h`, `src/core/stream/StreamTelemetryWriter.{h,cpp}`, and any `TANKOBAN_STREAM_BACKEND` site.

- [ ] **Step 1: Collapse the playback-mode enum** in `StreamTypes.h` to local-file only (remove the stream-server/HTTP modes). Grep `StreamPlaybackMode` (or the actual enum name) and fix every consumer to the single remaining mode.
- [ ] **Step 2: Trim `StreamTelemetryWriter`** — remove the `engine_started`/`stopped`/`metadata_ready`/`file_selected`/`first_piece` event emitters (the stream-server lifecycle events). Keep any download/completion telemetry if present.
- [ ] **Step 3: Remove `TANKOBAN_STREAM_BACKEND`** — `grep -rn "TANKOBAN_STREAM_BACKEND" .`; delete the CMake option + any `#ifdef`/env read. (Expected vestigial per the spec.)
- [ ] **Step 4: Build (isolated lane).** `BUILD OK`.
- [ ] **Step 5: Commit.** `[Agent 4 (Opus), THEATRE_DOWNLOAD_ONLY]: P2.4 collapse playback mode + trim stream-server telemetry + drop TANKOBAN_STREAM_BACKEND`

### Task 2.5: Final clean build + full smoke (gov-v11 close gate)

- [ ] **Step 1: Clean-from-scratch build in an isolated lane/worktree.** Delete `out_agent4\.ninja_log` + `.ninja_deps`, then `build_check.bat`. Expected `BUILD OK` (gov-v11 hard gate).
- [ ] **Step 2: Full smoke** (`build_and_run.bat`): browse a show → pick an undownloaded episode → source picker → download (visible on Downloads page) → completes → plays from disk; pick a downloaded episode → plays instantly; **no `stremio-runtime` ever spawns**; **no "Resolving metadata" anywhere**; movie path works the same.
- [ ] **Step 3: `/simplify` + reviewer pass** on the full arc diff (gov-v11 reviewer-before-master).
- [ ] **Step 4: Commit (if review yields fixes) + arc close.** Move the spec/plan to lifecycle as appropriate; update CLAUDE.md Active Fix TODOs.

---

## Self-Review (plan vs spec)

- **Spec §2 goal 1 (play only local):** Task 1.1. ✓
- **Goal 2 (stream-server gone):** Tasks 1.2 (stop creating) + 2.1-2.3 (delete). ✓
- **Goal 3 (hang impossible):** Task 1.2 Step 4 verifies no subprocess spawns. ✓
- **Goal 4 (browsing untouched):** discovery layer is in the "untouched" table; no task modifies it. ✓
- **Goal 5 (reuse download/local-play):** Task 1.1 Steps 2-3 route to existing flow; "untouched" table. ✓
- **Spec §4 non-goals:** no task adds ephemeral/progressive play, direct-URL play, or UI polish. ✓
- **Spec §5 sequencing:** Phase 1 behavior-first, Phase 2 deletion. ✓
- **Spec §7 blast radius:** every row maps to a Task 2.x. ✓
- **Placeholder scan:** one intentional placeholder — `m_streamPage_downloadIndex` in the Task 1.1 snippet is flagged as "the real StreamDownloadIndex member, grep to confirm the name" (the exact member name must be read at execution; the logic is concrete). The reroute's `openDownloadFlowForEpisode` routes to existing code rather than restating it (Step 3 says read-and-call, not reimplement). These are deliberate "read current code" steps for a removal/rewire arc, not hand-waves.
- **Type consistency:** `beginPlayOrDownload` / `openDownloadFlowForEpisode` / `playLocalFileFromStreamRequested` / `filePathFor` used consistently across tasks. ✓

## Coordination

Post a heads-up in `agents/chat.md` to **Agent 0** (their `REPO_STRUCTURE_CLEANUP` defers relocating `StreamPage` until the stream files are quiet — this arc keeps that move parked) and **Agent 1** (Stream mode is their COMICS_TANKOYOMI_STREAM_MERGER blueprint — the download-and-local-play shape is the new reference) **before starting Phase 1**.
