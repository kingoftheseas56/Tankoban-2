# REPO_HYGIENE_FIX_TODO

**Status:** Phase 1 ✅ CLOSED 2026-04-26 (foundation: untracked source backfill + ring buffer + path strip + dead-state cleanup + single-instance wire). Phase 3 (dev-control bridge) up next per Rule-14 sequencing change. 7-phase arc covering external-AI-audit findings + ChatGPT-tuned dev-control-bridge proposal + Hemanth's strategic intent ("anyone who downloads our source code would be able to build our entire app without any fuss").

**Owner:** Agent 0 (authoring + governance + cross-phase coordination). Per-phase execution split across Agent 0, Agent 3, Agent 4, Agent 4B, Agent 5, Agent 7 (Codex). See §6 phase-by-phase ownership.

**Source materials:**
1. **External AI audit** (provided by Hemanth 2026-04-26) — 19 findings across 11 critical / 7 high-severity / 1 medium categories. Audit was static-only (no build attempt); but its critical findings were verified by Agent 0 against working tree + git status. Key findings: missing files in CMakeLists (untracked source on local machine), hardcoded `C:/Users/Suprabha/...` paths in committed source, JsonStore read-after-write race, scanner thread ownership wrong Qt idiom, dropped rescans during in-progress scan, sidecar `waitForStarted` blocking UI thread, unguarded `std::stoi` in native sidecar, dead single-instance code, dead tray-quit path.
2. **ChatGPT-tuned dev-control-bridge proposal** (provided by Hemanth 2026-04-26 after feeding ChatGPT the `tankobangpt.zip` source). Code-aware proposal for a C++ Qt6 `QLocalServer`-based dev bridge inside Tankoban (`--dev-control` flag) + `tankoctl.exe` console client. Replaces UIA-tree-walk MCP smokes with direct field reads (~10x faster). Proposal coexists with pywinauto-mcp + windows-mcp as primary/fallback split.
3. **Hemanth's strategic intent** (2026-04-26): "create a github repo in such a way that anyone who downloads our source code would be able to build our entire app without any fuss. they shouldn't even need to do anything, we will provide all the bat files necessary to install dependencies, compile code and build our app and stuff and we will keep our repo ultra organised."

---

## §1 Goal

Close the gap between the brotherhood's actual codebase quality and what an external clean-clone reviewer can build/run. Two coupled goals:

**Goal A — Reproducible builds for developers/agents.** Anyone with a fresh Windows machine + Qt6 + MSVC2022 installed clones the repo, runs ONE setup script, and gets a clean build. No untracked files needed locally, no hardcoded path edits required, no manual dependency installs (vcpkg handles them).

**Goal B — One-click run for end users.** Pre-built `Tankoban-Setup.exe` published to GitHub Releases. Random users download + double-click → app installs to Program Files like any normal Windows app. Most users will never see source code.

Both achievable; Goal A unblocks Goal B (you can't auto-build releases if developers can't build manually first).

---

## §2 Why now

The external AI audit caught real, embarrassing issues:
- 6+ critical source files exist on Hemanth's machine but never committed to GitHub. Anyone cloning today gets a broken build.
- Production source contains `C:/Users/Suprabha/Desktop/Tankoban 2/...` and `C:/Users/Suprabha/Desktop/TankobanQTGroundWork/...` hardcoded fallbacks. Leaks developer machine paths into shipped code.
- Unconditional `_boot_debug.txt` + `_player_debug.txt` writes on every launch. Noise files appear in user's working directory.
- Real lifecycle bugs (JsonStore race, scanner ownership, sidecar UI-block) that any external reviewer will hit immediately.

Plus Hemanth wants the repo to be presentable. The brotherhood's chat.md culture is great for internal coordination but the public face needs to be clean. We just spent a wake fixing skill-discipline; the same energy applied to repo hygiene closes the gap with what the AI flagged.

The dev-control-bridge proposal lands in the same arc because it's a fundamental agent-tooling improvement that DEPENDS on the bounded log ring buffer (Phase 1) — sequencing them in one TODO keeps the narrative coherent.

---

## §3 Constraints (load-bearing)

- **No half-finished features in committed source.** Either fully implement or fully delete. Single-instance helpers, tray-quit branches, dead UI paths — all get either wired or removed.
- **No hardcoded developer paths in committed source.** Every `C:/Users/Suprabha/...` reference is a bug. Replace with Qt-resolved paths (`QStandardPaths::writableLocation` / `QCoreApplication::applicationDirPath`).
- **No unconditional debug logs.** Every `_boot_debug.txt` / `_player_debug.txt` / `sidecar_debug_live.log` / `sub_debug.log` write must be either gated behind an env var / build flag OR routed through a bounded in-memory ring buffer with structured query interface.
- **`vcpkg` for native deps, NOT manual installs.** Boost, libtorrent, FFmpeg, PortAudio all migrate to `vcpkg.json`. Qt + MSVC stay manual prereqs (too large to bundle, license-gated).
- **CI must catch future regression of these fixes.** GitHub Actions clean-machine build on every push. If anyone (us, future us, future agents) re-introduces an untracked-file-in-CMakeLists or hardcoded path, CI fails immediately.
- **Dev-control-bridge is dev-only.** Production builds must NOT expose the `QLocalServer` surface. Gate behind `--dev-control` flag or `TANKOBAN_DEV_CONTROL=1` env var.
- **Coexist with existing MCP setup.** pywinauto-mcp + windows-mcp stay registered in `.mcp.json`. Dev bridge becomes the primary path for app-state queries; MCP servers stay as fallbacks for UIA invocation + screenshots + visual judgment.
- **No human-day deadlines.** Per `feedback_no_human_days_in_agentic.md` — phases measured in summons, not weeks.

---

## §4 Out of scope

- **Refactoring file sizes** (audit finding 15). VideoPlayer.cpp at ~3,853 LOC and ComicReader.cpp at ~3,888 LOC are large but not bugs. Refactoring split-by-domain would be its own multi-summon arc with high regression risk; defer.
- **Stripping agent-history comments** (audit finding 17). `PLAYER_LIFECYCLE_FIX Phase 1 Batch 1.1` style comments are useful during coordination, noise post-ship. Defer to a future "comment-cleanup pass" wake; not blocking.
- **Test framework expansion** (audit finding 16). Adding tests for lifecycle/build/process boundary is valuable but out of scope for hygiene; covered piecemeal in P4 lifecycle fixes (e.g. JsonStore consistency test).
- **Stream/torrent boundary clarification** (audit finding 18). Already in flux per STREAM_SERVER_PIVOT_TODO; that's the right vehicle, not this TODO.
- **Restructuring `agents/` folder organization.** Internal coordination surface; not user-facing.
- **Cross-platform support (Linux/macOS).** Tankoban is Windows-only. Cross-platform is a separate strategic decision, not hygiene.
- **Replacing pywinauto-mcp / windows-mcp.** Dev bridge coexists, doesn't replace.
- **Backporting fixes to historical commits.** Forward-only. The repo cleans up from this point on.

---

## §5 Decisions — Hemanth ratifies before Phase 1 executes

Nine strategic calls, each with my recommended answer. Phases assume these are settled.

1. **Phase ordering — strict sequential or parallel-where-possible?** RECOMMEND: **parallel where possible.** P1 first (foundation). Then P2 (vcpkg) + P5 (CI) can run parallel — different agents, no shared files. Then P3 (dev bridge) + P4 (lifecycle fixes) parallel. Then P6 (installer). Then P7 (docs). 7 phases but ~4 sequential checkpoints.

2. **vcpkg vs Conan for dependency management?** RECOMMEND: **vcpkg.** Microsoft-blessed for Windows C++, integrates with CMake out of the box, well-documented, used by Qt itself. Conan is more flexible but more setup. Tankoban is Windows-only Qt — vcpkg is the right pick.

3. **Installer format — WiX (.msi) or NSIS (.exe)?** RECOMMEND: **NSIS.** WiX produces enterprise-grade .msi but is fiddly. NSIS produces a clean `Tankoban-Setup.exe` that 99% of Windows desktop apps use. Simpler, faster to ship.

4. **Log ring buffer scope — main app only OR main app + native sidecar?** RECOMMEND: **main app only for P1.** Native sidecar's `sidecar_debug_live.log` redirect is a separate problem — covered in P4 (lifecycle fixes for sidecar). Splitting keeps P1 small and shippable.

5. **Single-instance code (audit finding 2) — wire it or delete it?** RECOMMEND: **wire it.** Multiple-instance state corruption IS a real risk (shared sidecar processes, shared library DB). Audit's recommended fix is small (~5 lines in main.cpp). Wire it in P1 alongside the path-strip work.

6. **Tray-quit behavior (audit finding 13) — implement close-to-tray or delete the dead path?** ASK Hemanth. The current code has tray icon + quit-from-tray method but `closeEvent` doesn't honor `m_quitRequested`. Either:
   - (a) Implement close-to-tray properly (close button hides to tray, quit-from-tray actually quits)
   - (b) Delete the unused tray-quit branch + `m_quitRequested` flag
   - (c) Delete the entire tray icon (no tray feature at all)
   - I lean (b) — keep tray for the "show running app" affordance but close button just exits. But this is a UX call, not technical.

7. **Dev-control-bridge socket name — `TankobanDevControl` or other?** RECOMMEND: **`TankobanDevControl` per ChatGPT's proposal.** Distinct from `TankobanSingleInstance` (which P1 wires) so they don't conflict. Schema-versioned in `ping` response (`"tankoban.dev.v1"`).

8. **CI matrix — Windows-only or also Linux/macOS dry-runs?** RECOMMEND: **Windows-only.** Tankoban is Windows-native (Qt6 + DirectX + libtorrent + ffmpeg sidecar). Cross-platform CI without cross-platform support produces noise. Add later if/when Tankoban gets ported.

9. **GitHub Releases pipeline — auto-build on tag push or manual?** RECOMMEND: **auto-build on tag push.** Push `v0.1.0` tag → GitHub Actions builds release → uploads `Tankoban-Setup.exe` to Releases. Manual triggering is fine for early iterations but auto is the right end-state.

---

## §6 Phases

### Phase 1 — Commit untracked source + strip hardcoded paths + bounded log ring buffer (Agent 0 + Agent 3)

**Goal:** make a fresh GitHub clone build against the SAME source tree the developer uses locally, without leaking developer paths or unconditional debug breadcrumbs.

**Steps:**
- **P1.1 (Agent 0):** Commit currently-untracked source files. Per session-start git status: `src/core/book/AaSlowDownloadWaitHandler.{h,cpp}`, `src/core/book/BookDownloader.{h,cpp}`, `src/core/book/LibGenScraper.{h,cpp}`, `src/core/stream/StreamTelemetryWriter.h`, `src/ui/widgets/ThemePicker.{h,cpp}`. Plus any others surfaced by `git status` at execution time. One commit per coherent group (TankoLibrary work group + theme work group + telemetry).
- **P1.2 (Agent 3):** Strip `C:/Users/Suprabha/Desktop/Tankoban 2/_player_debug.txt` + `C:/Users/Suprabha/Desktop/TankobanQTGroundWork/...` hardcoded paths from `src/ui/player/SidecarProcess.cpp` (lines ~22-27, ~63 per audit). Replace with Qt-resolved `QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)` for log directory, `QCoreApplication::applicationDirPath()` for sidecar exe lookup. Same treatment for `native_sidecar/src/subtitle_renderer.cpp:35-43` (`sub_debug.log`).
- **P1.3 (Agent 0):** Build a bounded in-memory log ring buffer in main app (~500 entries max, structured `{level, source, message, time, details}` schema). Replaces unconditional `_boot_debug.txt` + `_player_debug.txt` writes. Existing call sites route through new logger. Logger gated behind `TANKOBAN_DEBUG_LOG=1` env var OR debug build flag.
- **P1.4 (Agent 0):** Wire the dead single-instance helpers in `src/main.cpp` (lines 18-70 per audit finding 2). `signalExistingInstance()` + `createInstanceServer(MainWindow*)` are defined but never called — wire them between QApplication construction and MainWindow show.
- **P1.5 (Agent 0 + Hemanth):** Tray-quit decision per §5 question 6. Either implement properly OR delete dead branch.

**Files touched:** `src/core/book/*` (commit untracked), `src/ui/widgets/ThemePicker.*` (commit), `src/core/stream/StreamTelemetryWriter.h` (commit), `src/main.cpp` (single-instance wire + log buffer init), `src/ui/MainWindow.cpp` (tray-quit fix), `src/ui/player/SidecarProcess.cpp` (path strip), `native_sidecar/src/subtitle_renderer.cpp` (path strip), NEW `src/core/DebugLogBuffer.{h,cpp}` (~150 LOC), CMakeLists.txt (add DebugLogBuffer).

**Exit criterion:** fresh GitHub clone has all source files; `grep -rn "C:/Users/Suprabha" src/ native_sidecar/src/` returns zero hits in committed code; `_boot_debug.txt` no longer appears in CWD on launch unless env-gated.

### Phase 2 — vcpkg dependency migration + setup.bat (Agent 4 + Agent 4B)

**Goal:** developers install Qt + MSVC manually (one-time, well-documented), then `vcpkg` handles everything else automatically on first cmake configure.

**Steps:**
- **P2.1 (Agent 4):** Add `vcpkg.json` at repo root listing native deps: `libtorrent`, `boost-system`, `boost-filesystem`, `boost-program-options`, `ffmpeg[avcodec,avformat,avutil,swresample,swscale]`, `portaudio`. Verify each builds clean via vcpkg.
- **P2.2 (Agent 4B):** Add `CMakePresets.json` with default preset using `vcpkg.cmake` toolchain. One preset for Release, one for Debug + tests. Removes need for users to remember 8 `-D` flags from `build_and_run.bat`.
- **P2.3 (Agent 0):** Author `setup.bat` at repo root that prereq-checks Qt6 + MSVC2022 + vcpkg + Bun. If anything missing, prints clear "install X from <url>" message. If everything present, runs `cmake --preset default` + `cmake --build out`. Single-script onboarding.
- **P2.4 (Agent 4):** Update `CMakeLists.txt` to source deps from vcpkg-resolved targets instead of hardcoded `third_party/` paths or system-PATH lookups. Verify `third_party/sherpa-onnx/` is still needed or migrate to vcpkg if available.
- **P2.5 (Agent 0):** Update `build_and_run.bat` to use the new `cmake --preset` invocation. Keep the dev-control flag from P3.

**Files touched:** NEW `vcpkg.json`, NEW `CMakePresets.json`, NEW `setup.bat`, MODIFIED `CMakeLists.txt`, MODIFIED `build_and_run.bat`, MODIFIED `build_check.bat` (preset-aware).

**Exit criterion:** fresh clone + Qt6 + MSVC2022 + vcpkg installed → `setup.bat` succeeds → `Tankoban.exe` builds. Time-to-build measured + documented.

### Phase 3 — Dev control bridge + tankoctl.exe (Agent 0 + Agent 3 + Agent 5)

**Goal:** agents drive Tankoban via direct C++ Qt LocalServer instead of UIA tree walks. ~10x faster for state queries; coexists with pywinauto-mcp.

**Depends on:** Phase 1 ring buffer (so `tankoctl logs` reads the buffer, not deprecated `_player_debug.txt`).

**Steps:**
- **P3.1 (Agent 0):** Author `src/devtools/DevControlServer.{h,cpp}` per ChatGPT proposal. `QLocalServer` listening on `TankobanDevControl` socket (separate from `TankobanSingleInstance`). JSON request/response. Schema-versioned (`tankoban.dev.v1` in ping).
- **P3.2 (Agent 0):** Author `MainWindow::handleDevCommand()` dispatcher + `MainWindow::devSnapshot()`. Dev API surfaces: `ping`, `get_state`, `open_page`, `scan_videos`, `get_videos`, `play_file`, `close_player`, `get_player`, `logs`, `run_video_smoke`. 10 commands max for v1. `logs` reads Phase 1 ring buffer.
- **P3.3 (Agent 3):** Add `VideoPlayer::devSnapshot()` reading existing fields (`m_currentFile`, `m_pendingFile`, `m_openPending`, `m_paused`, `m_streamMode`, `m_persistenceMode`, `m_streamStalled`, `m_currentAspect`, `m_currentCrop`, `m_durationSec`, `m_lastKnownPosSec`, `m_sidecarRetryCount`, `m_firstFrameWatchdog`, stats). Read-only; no behavior change.
- **P3.4 (Agent 5):** Add `VideosPage::devSnapshot()` + `ShowView::devSnapshot()` reading tile/episode state. Read-only; no behavior change.
- **P3.5 (Agent 0):** Author `tools/tankoctl.cpp` console exe + add `tankoctl` target to CMakeLists. Subcommands map to dev-bridge actions.
- **P3.6 (Agent 0):** Update `build_and_run.bat` to launch with `--dev-control`. Wire `enableDevControl()` call in `src/main.cpp` after window construction.
- **P3.7 (Agent 0):** Update `CLAUDE.md` "Which MCP, when" section to add `tankoctl` as the new primary for app-state queries. New priority order: `tankoctl.exe` (app truth via DevControlServer) → `pywinauto-mcp` (UIA invocation) → `windows-mcp` (screenshots / PowerShell / last-resort). Agents discover via SessionStart auto-load. Add per-agent shortlist updates in `agents/STATUS.md` for domains that benefit most (Agent 3 video player smokes, Agent 4 + 4B stream/sources lifecycle, Agent 5 library UX verification). Optional polish: extend `.claude/scripts/session-brief.sh` to probe `tankoctl ping` at SessionStart and surface "DevControl bridge: ALIVE/DEAD" in the digest banner — silent when alive, warning when Tankoban not running with `--dev-control`.

**Files touched:** NEW `src/devtools/DevControlServer.{h,cpp}` (~300 LOC), NEW `tools/tankoctl.cpp` (~150 LOC), MODIFIED `src/ui/MainWindow.{h,cpp}` (handleDevCommand + devSnapshot + enableDevControl), MODIFIED `src/ui/player/VideoPlayer.{h,cpp}` (devSnapshot), MODIFIED `src/ui/pages/VideosPage.{h,cpp}` (devSnapshot), MODIFIED `src/ui/pages/ShowView.{h,cpp}` (devSnapshot), MODIFIED `CMakeLists.txt` (DevControlServer + tankoctl target), MODIFIED `src/main.cpp` (devControl flag check + enableDevControl), MODIFIED `build_and_run.bat` (--dev-control flag).

**Exit criterion:** `out\tankoctl.exe ping` returns valid JSON. `out\tankoctl.exe get-player` returns structured state ~10x faster than current pywinauto-mcp UIA walk. Existing MCP smokes still work (coexist).

### Phase 4 — Lifecycle bug fixes from external audit (multi-agent)

**Goal:** close audit critical findings 4-11. Each is a real bug that the dev bridge inherits if not fixed.

**Steps:**
- **P4.1 (Agent 4):** JsonStore race fix (audit finding 4). Separate "latest in-memory value" from "pending disk queue" — read consults latest map, not just pending. Make `commitToDisk()` return success/failure + log/emit failures.
- **P4.2 (Agent 5 + domain agents):** Scanner thread ownership (audit finding 5). Replace `delete m_scanner` with `connect(thread, finished, scanner, deleteLater)` pattern in VideosPage, BooksPage, ComicsPage.
- **P4.3 (Agent 5 + domain agents):** Dropped rescans (audit finding 6). Add `m_rescanPending` flag; if scan in progress, set pending; on scan finish, fire pending if set.
- **P4.4 (Agent 4 + Agent 4B):** Scanner cancellation tokens (audit finding 7). Plumb cancellation through `ScannerUtils.cpp` walk. Add depth bound + symlink loop guard.
- **P4.5 (Agent 3):** Sidecar process management non-blocking (audit finding 8). Replace `waitForStarted(5000)` blocking calls in `SidecarProcess.cpp` with async patterns (signal-based readyness). Same for stop + restart.
- **P4.6 (Agent 3):** Sidecar session-id strict mode (audit finding 9). Add version handshake; once sidecar declares session-id support, REQUIRE session-id on all session-scoped events. Reject unsigned events with structured error.
- **P4.7 (Agent 3):** Native sidecar parse hardening (audit finding 10). Replace unguarded `std::stoi` calls in `native_sidecar/src/main.cpp` (lines 816, 869, 896, 900, 1207, 1233 per audit) with safe parser helper that returns `std::optional<int>` + emits `process_error` event on parse failure.

**Files touched:** `src/core/JsonStore.{h,cpp}`, `src/ui/pages/VideosPage.{h,cpp}` + `BooksPage.{h,cpp}` + `ComicsPage.{h,cpp}` (scanner ownership + rescan-pending), `src/core/ScannerUtils.cpp` (cancellation), `src/ui/player/SidecarProcess.{h,cpp}` (async lifecycle), `native_sidecar/src/main.cpp` (parse hardening), tests for each fix.

**Exit criterion:** each audit finding 4-11 closes with a unit test or smoke verification proving the fix. Dev bridge inherits stable substrate.

### Phase 5 — GitHub Actions CI (Agent 7 / Codex or Agent 0)

**Goal:** every push triggers a clean-machine build. Catches future regressions of P1+P2 hygiene fixes automatically.

**Steps:**
- **P5.1:** `.github/workflows/build.yml` — Windows runner, install Qt + MSVC + vcpkg, run `setup.bat`, build, run `tankoban_tests`.
- **P5.2:** `.github/workflows/repo-consistency.yml` — fail if any file in CMakeLists doesn't exist on disk, fail if any quoted include unresolvable, fail on grep hit for `C:/Users/` in committed source.
- **P5.3:** Status badges in README.

**Files touched:** NEW `.github/workflows/build.yml`, NEW `.github/workflows/repo-consistency.yml`.

**Exit criterion:** push to a branch triggers green build on a fresh runner; intentional commit of an untracked-in-CMakeLists file triggers red CI.

### Phase 6 — Windows installer + GitHub Releases (Agent 7 / Codex)

**Goal:** non-developers download `Tankoban-Setup.exe` and double-click. App installs to Program Files like any normal Windows app.

**Depends on:** Phase 2 (build pipeline) + Phase 5 (CI green).

**Steps:**
- **P6.1:** NSIS installer config (per §5 question 3 ratification). Bundles `Tankoban.exe` + ffmpeg_sidecar + Qt runtime DLLs + stream_server (Stremio runtime) + required resources. Excludes: dev-control-bridge code path, debug symbols (`.pdb`), test binaries.
- **P6.2:** `.github/workflows/release.yml` — on tag push (`v*.*.*`), builds release, packages with NSIS, uploads `Tankoban-Setup.exe` to GitHub Release.
- **P6.3:** Release notes template + version-bump script.

**Files touched:** NEW `installer/tankoban.nsi`, NEW `.github/workflows/release.yml`, NEW `scripts/version-bump.ps1`.

**Exit criterion:** push tag `v0.1.0` → workflow builds → `Tankoban-Setup.exe` published to Releases → fresh-machine user downloads + installs → app runs.

### Phase 7 — Documentation (Agent 0 + Agent 8)

**Goal:** repo presents itself well to outside readers. Brotherhood docs stay internal in `agents/`; external-face docs are clean.

**Steps:**
- **P7.1 (Agent 0 + Agent 8):** `README.md` at repo root. What is Tankoban? Screenshots. Feature list. Download link to latest Release. Link to BUILD.md for developers. License.
- **P7.2 (Agent 0):** `BUILD.md` — full developer build instructions. Prereqs, setup.bat usage, troubleshooting, manual build path if setup.bat fails.
- **P7.3 (Agent 0):** `ARCHITECTURE.md` — high-level pieces (Qt UI / native sidecar / libtorrent / stream-server / domain pages). Not exhaustive; orientation map.
- **P7.4 (Agent 0):** `CONTRIBUTING.md` — when/if outside contributors arrive. Brotherhood-coordination internals stay in `agents/` (gitignored from public docs).
- **P7.5 (Agent 0):** `LICENSE` — pick + commit.

**Files touched:** NEW `README.md`, NEW `BUILD.md`, NEW `ARCHITECTURE.md`, NEW `CONTRIBUTING.md`, NEW `LICENSE`.

**Exit criterion:** GitHub repo landing page reads as a real software project, not a coordination scratchpad.

---

## §7 Risk surface

- **R1: Phase 2 vcpkg migration breaks the build.** Mitigation: P2 in worktree first, verify clean build, then merge. Keep prior `third_party/` checkout as fallback for one phase.
- **R2: Phase 4 lifecycle fixes regress in-flight features.** Mitigation: each fix gets a smoke test before merge. Domain agents own their own fixes, not Agent 0.
- **R3: Phase 6 installer bundles too much (size bloat) or too little (missing runtime DLL).** Mitigation: smoke-test installer on fresh Windows VM before publishing first Release. Document inclusion list explicitly.
- **R4: Phase 5 CI false-fails on flaky network (vcpkg downloads).** Mitigation: cache vcpkg packages in CI; add retry-once on transient failure.
- **R5: Phase 3 dev bridge couples to internal field names that change.** Mitigation: `devSnapshot()` on each class is a single pinch point — when fields change, the snapshot updates. Schema versioning lets old `tankoctl` commands fail loud rather than return wrong data.
- **R6: Phase 1 single-instance wire breaks Hemanth's multi-tab dev workflow.** Mitigation: Hemanth currently doesn't run multiple Tankoban instances. If single-instance becomes annoying during dev, gate behind a build flag.
- **R7: Phase 7 docs say things that aren't true.** Mitigation: every claim in BUILD.md / ARCHITECTURE.md verified against working code, not vibes.

---

## §8 Files touched (cumulative across phases)

**New:**
- `src/core/DebugLogBuffer.{h,cpp}` (P1)
- `vcpkg.json` (P2)
- `CMakePresets.json` (P2)
- `setup.bat` (P2)
- `src/devtools/DevControlServer.{h,cpp}` (P3)
- `tools/tankoctl.cpp` (P3)
- `.github/workflows/build.yml` (P5)
- `.github/workflows/repo-consistency.yml` (P5)
- `installer/tankoban.nsi` (P6)
- `.github/workflows/release.yml` (P6)
- `scripts/version-bump.ps1` (P6)
- `README.md` (P7)
- `BUILD.md` (P7)
- `ARCHITECTURE.md` (P7)
- `CONTRIBUTING.md` (P7)
- `LICENSE` (P7)
- All currently-untracked source files (P1)

**Modified:**
- `src/main.cpp` (P1: single-instance wire + log buffer init; P3: dev-control flag)
- `src/ui/MainWindow.{h,cpp}` (P1: tray-quit fix; P3: handleDevCommand + devSnapshot + enableDevControl)
- `src/ui/player/SidecarProcess.{h,cpp}` (P1: path strip; P4: async lifecycle)
- `src/ui/player/VideoPlayer.{h,cpp}` (P3: devSnapshot)
- `src/ui/pages/VideosPage.{h,cpp}` + `ShowView.{h,cpp}` (P3: devSnapshot; P4: scanner ownership + rescan)
- `src/ui/pages/BooksPage.{h,cpp}` + `ComicsPage.{h,cpp}` (P4: scanner ownership + rescan)
- `src/core/JsonStore.{h,cpp}` (P4: race fix)
- `src/core/ScannerUtils.cpp` (P4: cancellation)
- `native_sidecar/src/main.cpp` (P4: parse hardening)
- `native_sidecar/src/subtitle_renderer.cpp` (P1: path strip)
- `CMakeLists.txt` (P1+P2+P3: targets + vcpkg toolchain)
- `build_and_run.bat` (P2: presets; P3: dev-control flag)
- `build_check.bat` (P2: preset-aware)
- `CLAUDE.md` (P3+P7: dev bridge note + skill sheet refresh)
- `agents/STATUS.md` (cursor maintenance throughout)
- `agents/chat.md` (RTC bundles per phase)

**Memory updates (Phase 7 close-out):**
- New memory `feedback_repo_hygiene_lessons.md` capturing the audit-driven rebuild discipline.

---

## §9 Smoke / verification per phase

- **P1:** fresh `git clone` → `git status` shows no new untracked source files; `grep -rn "C:/Users/Suprabha" src/ native_sidecar/src/` returns 0 hits; launch app → no `_boot_debug.txt` / `_player_debug.txt` in CWD; second launch → original instance focuses, second exits cleanly.
- **P2:** fresh Windows VM with Qt + MSVC installed → `setup.bat` succeeds → `cmake --preset default` succeeds → `cmake --build` succeeds → `Tankoban.exe` launches.
- **P3:** `out\tankoctl.exe ping` returns valid JSON < 200ms; `out\tankoctl.exe play-file <known-video>` opens player; `out\tankoctl.exe get-player` returns `firstFrameSeen: true` after ~3s.
- **P4:** each lifecycle bug has a paired test; all green. Audit re-run produces ≤ N - 8 critical findings (where N = original 11).
- **P5:** push to a branch → CI workflow runs → green. Intentionally break (commit untracked-in-CMakeLists file) → red. Revert → green.
- **P6:** push tag `v0.1.0-test` → release workflow runs → `Tankoban-Setup.exe` published. Fresh Windows VM → download installer → run → app installed in Program Files → launches.
- **P7:** outside reader (Hemanth or external agent or chatGPT with the new repo zip) reads README + BUILD + ARCHITECTURE → understands the project + can build it.

---

## §10 Honest unknowns

- **vcpkg build time on first cmake configure.** Estimated 30-60 minutes for libtorrent + ffmpeg + boost first build. Documented in BUILD.md so users know to wait.
- **Whether `third_party/sherpa-onnx` migrates cleanly to vcpkg.** May not be available; might stay as third-party download with a `scripts/setup-sherpa.bat` helper.
- **Installer size budget.** Tankoban.exe (~26 MB) + Qt runtime (~50 MB) + ffmpeg (~50 MB) + stream_server (~88 MB) + sherpa (~75 MB) = ~290 MB installer. Acceptable for a media app but document.
- **Dev bridge schema evolution policy.** v1 is fine; what's the plan for v2? Recommend: additive changes are non-breaking (`tankoban.dev.v1.x`); breaking changes bump major (`tankoban.dev.v2`). Document at Phase 3 ship.
- **GitHub Actions Windows runner cost.** Free tier limits at 2000 minutes/month for private repos; should be fine, but if Tankoban repo goes public the limits change.

---

## §11 Rollback

Each phase independently revertable:
- P1: revert path-strip + ring buffer commits; untracked-source commits stay (no harm in committing).
- P2: revert vcpkg.json + CMakePresets; restore prior `third_party/` paths in CMakeLists.
- P3: revert DevControlServer + tankoctl; existing pywinauto-mcp / windows-mcp still work.
- P4: per-fix revert; each is independent.
- P5: delete `.github/workflows/*.yml`.
- P6: delete `installer/*` + release.yml; existing builds still work locally.
- P7: delete README + BUILD + ARCHITECTURE + CONTRIBUTING + LICENSE.

The one phase whose changes touch external state is P6 (publishes to GitHub Releases). Rollback = delete the bad release manually via GitHub UI.

---

## §12 Memory close-out tasks (Phase 7 exit)

- New memory `feedback_repo_hygiene_lessons.md` capturing what we learned: audit-driven rebuilds need committed source first, hygiene gates governance, vcpkg saves real time on Windows C++ projects.
- Update `MEMORY.md` index.
- Audit `MEMORY.md` for stale "Tankoban not yet public-ready" assertions if any exist.

---

## §13 Cursor (this row maintained by Agent 0)

- Phase 1: ✅ **CLOSED 2026-04-26 ~11:45am.** All sub-steps shipped. Pre-step `/commit-sweep` (13 commits + marker `bd14e5e`). P1.1 (commits `bad1cea` + `d0bfee2`, 7 untracked source files committed). P1.3 (DebugLogBuffer.{h,cpp}, ~150 LOC ring buffer, BUILD OK). P1.2 (13 hardcoded paths stripped across 8 files + 2 native_sidecar files no-op'd, BUILD OK). P1.5 (m_quitRequested deleted, quitFromTray simplified). P1.4 (single-instance wire in src/main.cpp at two insertion points; double-launch smoke GREEN — second instance exit=0 in 1027ms, only one Tankoban.exe alive post-smoke). One bundled commit covering P1.2 + P1.3 + P1.4 + P1.5 + dashboard refresh. Helper batch files `_p1_configure_oneshot.bat` + `_p1_build_oneshot.bat` deleted at close-out per cookbook.
- **Phase 3 PROMOTED ahead of Phase 2** (Rule-14 call this wake per Hemanth's "when are we going to get to this?" nudge re: dev-control bridge). Phase 3 only depends on Phase 1's ring buffer; Phase 2's vcpkg is heaviest-single-phase build-pipeline polish that lands later.
- Phase 2 (vcpkg): moved to land near the end of the arc.
- Phase 3 (dev-control bridge + tankoctl): unblocks immediately after Phase 1 closes; primary next-major work.
- Phase 4 (lifecycle bug fixes): parallel-eligible with Phase 3 post-Phase-1.
- Phase 5 (CI): parallel-eligible.
- Phase 6 (NSIS installer): blocked behind Phase 2 + Phase 5.
- Phase 7 (docs): blocked behind Phase 6.

---

## §14 Honest scope note

This TODO does not solve "Tankoban is production-ready." It solves the specific failure modes the external audit surfaced + the strategic intent Hemanth ratified ("anyone clones + builds without fuss"). Other quality vectors — UX polish, performance work, feature completeness — remain separate scope.

Audit findings 15 (file size refactor), 17 (comment cleanup), 18 (stream/torrent boundary), 19 (packaged ffprobe path) are explicitly deferred per §4. They're real but not blocking the strategic intent.

If during execution any phase surfaces evidence the audit's priority ordering was wrong (e.g. a "high-severity" finding turns out to be load-bearing for Phase 2), reorder with explicit chat.md note + §13 Cursor update.
