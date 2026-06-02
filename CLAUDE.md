# Tankoban 2 — Session Bootstrap (Kernel)

This file auto-loads into every Claude Code session in this directory. It is the **kernel** — stable role / rules-pointers / routing only. Mutable state lives in `agents/STATUS.md`; rules + protocols in `agents/GOVERNANCE.md`; the owner-per-domain + task→files router in `agents/routes.yml`. This file does not duplicate them.

---

## HEMANTH'S ROLE (READ THIS FIRST, EVERY WAKE)

**Hemanth is NOT a coder.** His role is strictly limited to three actions, total:

1. **Open the app.** Either closes + double-clicks `build_and_run.bat` (which auto-sets `TANKOBAN_STREAM_TELEMETRY=1` + `TANKOBAN_ALERT_TRACE=1`), or clicks the already-running Tankoban window.
2. **Click something in the UI.** Play a torrent. Seek. Open a book. Whatever the agent asked him to smoke.
3. **Report what he saw.** "Worked" / "Still stuck" / "Buffer never filled" / screenshot.

**That is the entire Hemanth role.** Do NOT ask him to:
- Open a terminal
- Set environment variables manually (they are already set by `build_and_run.bat`)
- Run `cmake`, `ctest`, `git`, `taskkill`, or any CLI command
- Read log files (`sidecar_debug_live.log`, `stream_telemetry.log`, `alert_trace.log`, `_player_debug.txt`, etc.)
- Grep, tail, diff, or parse anything
- Decide between technical options (Option A vs B vs C — that's Rule 14 agent-call)
- "Wait for the build" — the build IS the run; `build_and_run.bat` builds then launches
- Copy-paste output from terminals
- Know whether tests linked, what phase shipped, what env var does what

**Logs live under `out/` not repo root.** Before asking Hemanth anything diagnostic, `find . -maxdepth 3 -name "*.log"` and `cat` whatever you need. The answer is almost always already on disk.

**UI smoke is NOT Hemanth's job anymore either (as of 2026-04-19).** Two MCP servers are registered in `.mcp.json` at repo root (pywinauto-mcp + codex) — both project-scoped, both auto-loaded for every Claude Code session in this project. **But MCP is the FALLBACK, not the default surface.** The default surface for Tankoban app state + deterministic actions is `out\tankoctl.exe` (dev-control bridge, ~60-150ms per call, ~140+ commands across 14 domain prefixes). Any agent (1/2/3/4/5) can drive Tankoban programmatically: launch via build_and_run.bat, query state via tankoctl, fall back to pywinauto only for UIA-only widgets + screenshots + keyboard/focus + visual evidence. If your domain needs a smoke and the thing being smoked is mechanical (does the button work? does the buffer fill? does the seek land at the right position?), **you do the smoke yourself — do not ask Hemanth.** Hemanth's role further shrinks to visual-quality + taste judgment only (HDR tone-mapping, subtitle smoothness, frame pacing feel, AV sync feel, "does it look right").

**Tool priority** (`tankoctl` primary, `pywinauto-mcp` fallback):

- **`out/tankoctl.exe <cmd>`** — **primary for Tankoban app-state queries** via `QLocalServer` named pipe `TankobanDevControl`; typically 60–150ms per call (~5–10× faster than UIA tree walks). Schema `tankoban.dev.v1.10` with ~140+ commands across 14 prefix domains (`get-*` / `open-page` / `play-file` / `comics-*` / `books-*` / `stream-*` / `sources-*` / `library-*` / `player-*` / `sidecar-*` / `subs-*` / `ui-*` / `app-*` / `lease-*` etc.). Full live catalog: `out\tankoctl.exe ping`. Per-agent surface catalog + ship history + extension procedure: `project_dev_control_bridge.md` memory. Gated dev-only behind `--dev-control` flag (auto-set by `build_and_run.bat`) or `TANKOBAN_DEV_CONTROL=1`. Standing gates: `TANKOBAN_DEV_UI_SIM=1` (v1.8 synthetic UI writes) + `TANKOBAN_DEV_WRITE=1` (v1.9 destructive writes). Headline unlock: **`log-mark <label>`** writes correlation marker across all 4 log streams (`sidecar_debug_live.log` / `stream_telemetry.log` / `events.jsonl` / `ipc_latency.log`).
- **`mcp__pywinauto-mcp__*`** — **fallback** for UIA-only widgets + screenshots + keyboard/focus + visual evidence. Use ONLY when tankoctl can't reach the surface: Qt custom widgets without dev-bridge snapshot, visual-capture for smoke evidence (`automation_visual`), keyboard sends the bridge's `ui-keypress` doesn't cover, focus changes outside the synthetic UI gate. UIA-native: click/read/set by AutomationId or ControlType, no pixel coordinates needed. Qt publishes 100% AutomationId coverage via `objectName()` (verified by `scripts/uia-dump.ps1`). **Default question before reaching for pywinauto: is there a `tankoctl` command that already returns this state?** Most of the time, there is.
- **Built-in `PowerShell` / `Bash` tools** — for shell / CLI / clipboard / registry / process-enumeration / file-IO work.
- All MCP desktop interactions remain under Rule 19 MCP LANE LOCK (gov-v7 lease registry primary) — one agent drives the desktop at a time.

**Build-command contract** (so no agent has to "invent" these for Hemanth):
- Run-the-app-with-telemetry: `build_and_run.bat` (env vars baked in, no manual `set` needed).
- Verify a .cpp compiles: `build_check.bat` (agent runs it, not Hemanth).
- Run main-app tests: `-DTANKOBAN_BUILD_TESTS=ON` + `ctest` (agent runs it).
- Dashboard drift / tracked junk / large files: `/repo-health` or `powershell -NoProfile -File scripts/repo-health.ps1` (agent runs it).

If you are tempted to give Hemanth a terminal command list longer than **one line** or a multi-step procedure that requires him to decide something technical, **stop and re-read this block.** Menu-ing Hemanth with coder steps has been flagged as a recurring brotherhood failure 2026-04-17, 2026-04-18, 2026-04-19, and 2026-05-21. Rule 14 + Rule 15 + multiple feedback memories codify this already — the block above is the always-loaded pointer.

---

## Live state

Current who/what/where — the dashboard, per-agent activity, RTC backlog, open
congresses/HELP/blocked, last smoke, governance versions, engine/quota status —
lives in **`agents/STATUS.md`** (mutable; refreshed by Agent 0 at phase boundaries,
Rule 13). This kernel stays stable; STATUS churns. Read STATUS when you need the
current picture; the canonical owner-per-domain map is `agents/routes.yml`.

---

## For Claude Sessions — Reading Order

See `agents/GOVERNANCE.md` "Session Start — Reading Order" section. Slimmed 2026-04-16: VERSIONS.md + this file are always-required; everything else is conditional.

This file is **state** (who/what/where right now). `agents/GOVERNANCE.md` is **rules** (how anyone operates). Do not duplicate rules here.

For Codex (Agent 7): see `AGENTS.md` at this same root, which redirects you into the brotherhood's governance.

---

## Required Skills & Protocols — tiered

Tiered per SKILL_DISCIPLINE_FIX_TODO Phase 6 ratification 2026-04-25. Tier 1 = core mandatory (every relevant wake). Tier 2 = conditional (fire on trigger). Tier 3 = milestone-only. Full rationale + NOT-adopted list: memory `feedback_plugin_skills_adopted.md`. Re-measurement pacing: memory `feedback_skill_discipline_remeasurement.md`. RTC `Skills invoked: [...]` provenance required for non-trivial RTCs (contracts-v3, see `agents/CONTRACTS.md`).

### Tier 1 — Core Mandatory (~7 skills, every relevant wake)

- **`/hemanth-language`** — every wake, auto-loaded at SessionStart. 4 disciplines (user-end terms first, preview per task group, no silence, well-explained menus only) + paired examples + failure-shape taxonomy. Consolidated in `feedback_hemanth_language_field_manual.md`. Foundation builds on Rules 14 + 15. Discipline 1 updated 2026-05-27 from "analogies first" to "user-end terms first" — analogy is the fallback for internal-only work with no user manifestation. Re-read cover-to-cover every wake.
- **`/brief`** — every wake start. SessionStart hook prints a pre-digest; `/brief` is the full state read.
- **`/session-recap`** — every wake END for non-trivial sessions (≥1 RTC, ≥1 commit, ≥1 substantive decision, ≥30 min). v4 (2026-05-22) makes the trimmed `.cc-history/*.trimmed.md` transcript primary reading at next wake; the recap is the structured INDEX. Output lands at `~/.claude/recaps/<agent-slug>/brother-<agent-slug>-<YYYY-MM-DD>-<codename>.md`.
- **`/superpowers:verification-before-completion`** — every RTC, every agent. Evidence-before-assertions checklist.
- **`/simplify`** — every non-trivial diff. Reuse + efficiency review (fixes issues found).
- **`/build-verify`** — whenever `src/` or `native_sidecar/src/` touched. Runs `build_check.bat` or sidecar build.
- **`/superpowers:requesting-code-review`** — every non-trivial RTC. Self-review primer on your own diff.
- **`/superpowers:systematic-debugging`** — whenever the work is bug-shaped (test failure, unexpected behavior, log-grep, smoke iteration). FIRST, before proposing fixes.

### Tier 2 — Conditional (fire on trigger)

- **`/security-review`** — touching `src/core/stream/*`, `src/core/torrent/*`, `native_sidecar/src/*`, or any user-facing input / network-exposed surface.
- **`/superpowers:brainstorming`** — before scoping a new feature, fix-TODO, refactor, or Congress position block.
- **`/superpowers:writing-plans`** — when authoring a standalone plan file at `~/.claude/plans/*.md` or `docs/superpowers/plans/*.md`.
- **`/superpowers:executing-plans`** — when executing a plan file with structured checkpoint discipline.
- **`/superpowers:receiving-code-review`** — when Hemanth corrects your work or Agent 7 audit lands with findings for your domain.
- **`/claude-mem:mem-search`** — "Didn't we solve this before?" BEFORE chat_archive dig. Auto-demoted by SessionStart hook when claude-mem state is degraded.
- **`/claude-mem:smart-explore`** — structural code queries via tree-sitter AST.
- **`/superpowers:dispatching-parallel-agents`** — branching into 2+ independent subagents.
- **`/superpowers:subagent-driven-development`** — executing a fix-TODO phase via `Agent()` dispatch.
- **`/superpowers:test-driven-development`** — opt-in ONLY for `tankoban_tests` pure-logic primitives.
- **`/example-skills:skill-creator` + `/superpowers:writing-skills`** — paired, when creating a new Tankoban skill.
- **`/example-skills:mcp-builder`** — when authoring a new MCP server.

### Tier 3 — Milestone-only

- **`/claude-mem:timeline-report`** — post-big-ship narrative. Agent 0 commissions when a TODO closes or multi-week arc lands.
- **`/claude-mem:knowledge-agent`** — ripe corpus for focused mini-brain.

### Agent 0 phase-boundary tools (scoped, not universal)

- **`/commit-sweep`** — end of session with pending RTCs.
- **`/rotate-chat`** — chat.md > 3000 lines or > 300 KB.
- **`/repo-health`** — drift audit (tracked junk / large files / stale STATUS).

---

## Active Fix TODOs (owner + phase cursor)

> Closed / superseded TODOs (~20+ entries since project start) live in `agents/_archive/todos/`. Only currently-active rows below.

| TODO file | Owner | Phase cursor | One-line scope |
|-----------|-------|--------------|----------------|
| **`agents/todos/TANKOCTL_TEST_HARNESS_FIX_TODO.md`** | Agent 0 (author/sequence) + Codex/Agent 9/Jrs (exec) | **P1 SHIPPED 2026-05-30** (expect/run/wait-for/record). **P2 in progress:** cache-get-stats SHIPPED 2026-05-31 (schema v1.11). **Network layer RATIFIED via Congress 9 (2026-05-31) + NetSeam CORE merged `9e8f010`** (Agent 9/DeepSeek built, Agent 0 reviewer-passed) — **NetSeam MIGRATION COMPLETE `bb7e4ed`** — 21 creation sites (Stream 12 / Books 3 / manga 3 / player 1 / comics+videos 2) all routed through NetSeam::createManager, zero raw QNAM left, CI grep-gate live. **NetSeam CONTROL HALF DONE `4b1f82d`** (Agent 9 built, Agent 0 review+fix+merge): net-block-host/list-rules + durationMs(QElapsedTimer) + reliable bytes + qgetenv all live; net-throttle-set is honest record-only (real latency-injection = A9 follow-on, needs deferred-dispatch proxy reply). So the layer now SEEs + BLOCKs traffic. Remaining = enhancement only: real throttle latency, test-harness P3(visual)/P4(ergonomics)/P5(CI runner), perf/scanner/signal gauges, sidecar media observability = Phase 2 (A3). See `agents/congress_archive/2026-05-31_observable_network_layer.md`. | tankoctl: remote-control → self-checking test harness. 5 tiers: P1 assert+scenario engine · P2 v1.9 debt closure (network/perf/scanner/cache/sidecar-queue) · P3 visual golden-diff · P4 REPL/describe/watch · P5 headless CI gate. All additive within v1.x. |
| **`COMICS_TANKOYOMI_STREAM_MERGER` (placeholder — not yet authored)** | Agent 1 | **VISION LOCKED 2026-05-14**; brainstorm → Codex review-and-expand in place (Rule 20) → plan, all pending Hemanth's next Agent 1 wake | Comics mode absorbs Tankoyomi; Stream-show-view-style series page lives inside the Comics library; Netflix-style in-library downloads; Tankoyomi-sourced series carry a badge. |
| **`agents/todos/TANKOLIBRARY_FIX_TODO.md`** | Agent 2 (inherited from Agent 4B 2026-05-20) | **AUTHORED 2026-04-21**, M1 queued | Greenfield Sources sub-app for book discovery via shadow libraries (Anna's Archive + LibGen v1; Z-Library deferred). Folds into BOOKS_STREMIO_PIVOT scope. |
| `agents/todos/BOOK_READER_FIX_TODO.md` | Agent 2 | Phases 1+2+3+5 SHIPPED | Awaiting Hemanth smoke; Phase 4 explicitly deferred. |
| `agents/todos/COMIC_READER_FIX_TODO.md` | Agent 1 | Phase 6 closed | Polish mode (no new UI/UX). |
| **`agents/todos/THEME_SYSTEM_FIX_TODO.md`** | Agent 5 | **P1+P2 SHIPPED**, P3+P4 pending (light-mode re-add Summon 1 shipped — Dawn Gradient B; texture pick pending) | Two-axis theme port to Qt6 + QSS + QGraphicsEffect. |
| **`agents/todos/SIDECAR_DISPATCHER_NON_BLOCKING_FIX_TODO.md`** | Agent 4 | **AUTHORED 2026-04-25 ~22:20**, awaiting §5 ratification → Phase A.1 kickoff | Sidecar dispatcher non-blocking via worker-thread split for `handle_set_tracks` + Source abstraction; closes the wedge case where HTTP sources block dispatcher inside `preload_subtitle_packets`. |
| **`agents/todos/STREAM_SERVER_PIVOT_TODO.md`** | Agent 0 (authored) + Agent 7 P0 + Agent 4 P1-P5 | **P0 + P1 + P2A + P2B + P3 GREEN** as of 2026-05-19; P4 next | Strategic pivot: stream mode off libtorrent → Stremio's `stream-server` Rust binary as subprocess + REST adapter. Tankorent stays on libtorrent. |
| **`TANKOYOMI_VOLUME_PIVOT` arc** | Agent 1 | **13-phase plan written 2026-05-16**; subagent execution in flight | Stremio-for-manga; volume-only first-class UI unit; chapters become buried implementation detail. Plan + memory `project_tankoyomi_volume_pivot_arc_2026-05-16.md`. |
| **`THEATRE_DOWNLOAD_OVERHAUL` arc** | Agent 4 | **Phases A+B+C (9 of 22) SHIPPED 2026-05-16**; D1 EpisodeTile next | PackList state model overhaul; subagent-driven plan execution. Memory `project_theatre_download_overhaul_*.md`. |
| **`agents/todos/TANKORENT_QUALITY_AND_QUEUE_TODO.md`** | Agent 4 | **AUTHORED 2026-05-27**; spec + plan landed; P1 lane queue infra kickoff next | Per-show download lanes (parallel across shows, sequential inside), Nyaa parity restore, Tankorent as source-addon in Theatre series view, season pack badges + filter chip, Netflix-clean Downloads page. |
| **`agents/todos/REPO_STRUCTURE_CLEANUP_FIX_TODO.md`** | Agent 0 | **P1+P2+P3+P4 SHIPPED 2026-05-29**; effectively complete (pending Hemanth GitHub-landing eyeball for formal close) | Safe repo-org cleanup (audit passes 1-4): README→3-mode + stale-CI/release fix + `docs/README.md` map; untrack stray `out/` csv + archive closed root TODOs; **CMake split DONE — root `CMakeLists.txt` 922→290 lines across `cmake/TankobanSources`+`TankobanTests`+`TankobanRuntimeAssets` (`9c1f4bd`/`db8c0c8`/`3f99455`)**; sort `docs/superpowers/`. Risky source-moves (5-7) + macOS `platform/` seam DEFERRED + gated. From Codex audit `9239031` + Opus review. |
| **`CROSS_PLATFORM_BACKEND` (placeholder — not yet authored)** | Agent 3-led (video/sidecar) + Agent 0 | **VISION LOCKED 2026-05-29**; brainstorm → spec → plan pending kickoff | Windows + macOS + Linux equal first-class, **brotherhood builds the ports** (brother = Mac user/taste-judge, not porter). Sequence: Windows (now) → macOS (next) → Linux (after). ONE repo, shared Qt core + per-platform video backend (build-selected, `platform/{windows,mac,linux}` seam), N-platform-generic abstraction. NOT separate repos, NOT an intensive in-place port. Bounded to the video-playback layer. See `project_macos_target_end_user` memory. |

---

## Memory Pointer

Long-term memory: `C:\Users\Suprabha\.claude\projects\c--Users-Suprabha-Desktop-Tankoban-2\memory\MEMORY.md` (off-git, per-machine).

Archived memories: `memory/_archive/INDEX.md`. Quarterly audit per File Hygiene section in `agents/GOVERNANCE.md`.

---

## New Agent / Consultant Onboarding

Path: see `agents/ONBOARDING.md` — 15-minute orientation track that gets a new contributor productive without reading 7 governance files + 50 memories.

---

## Build Quick Reference

- Main app (Release + asset deploy + run): `build_and_run.bat`
- Main app (Debug, MSVC2022 + Qt6.10.2): `build2.bat`
- Main app (compile-only, agent-safe): `build_check.bat` — `BUILD OK` / `BUILD FAILED exit=<n>` + 30-line cl.exe tail, no exe run, no GUI spawn.
- Sidecar (MinGW, installs to `resources/ffmpeg_sidecar/`): `powershell -File native_sidecar/build.ps1`
- Main-app tests (opt in with `-DTANKOBAN_BUILD_TESTS=ON`): `cmake --build out --target tankoban_tests && cd out && ctest --output-on-failure -R tankoban_tests`
- Drift audit (tracked junk / large files / chat.md rotation / STATUS drift / RTC backlog / CONGRESS/HELP): `/repo-health`
- Post-smoke log-scan (process state / telemetry / PERF / error-line scan): `powershell -NoProfile -File scripts/runtime-health.ps1`
- Smoke cleanup (Rule 17 — kill Tankoban + ffmpeg_sidecar): `powershell -NoProfile -File scripts/stop-tankoban.ps1`
- Mpv-vs-Tankoban log-diff harness: `powershell -NoProfile -File scripts/compare-mpv-tanko.ps1 -MpvLog <path> -SidecarLog <path>` — verdict=CONVERGED/DIVERGED-WORSE on drops + stalls.
- IPC round-trip latency: automatic on every Tankoban run; per-session block appended to `out/ipc_latency.log` with `cmd=<name> count p50 p99 max` rows.
- UIA enumeration (Qt 100% AutomationId via `objectName()`): `powershell -NoProfile -File scripts/uia-dump.ps1 [-MaxDepth 6] [-TargetClass StreamPage]`
- Dev-control bridge client (primary for app-state queries): `out\tankoctl.exe <subcommand>` — see HEMANTH'S ROLE § Tool priority above for the schema + gates + standing flags. Full catalog: `out\tankoctl.exe ping`. Memory `project_dev_control_bridge.md` for ship history + extension procedure.
- **Multi-engine brother helper — OPTIONAL, NOT MANDATORY (de-mandated 2026-06-02).** The 2026-06-01 "default working mode for every brother" rule is **REVOKED** — no brother is required to route work through `scripts/engines/engine.py`. It remains an *available* experimental tool you MAY use for your own lane (grunt→DeepSeek, review→Codex, search→Gemini), but it is not required and not a default. The whole multi-engine approach is **under reconsideration** — likely superseded by a "mini-congress" group-conversation medium (research done 2026-06-02; brainstorm pending). Until that lands, work as you normally would; reach for the helper only if it genuinely helps. Spec/research: `docs/superpowers/{specs,plans}/2026-06-01-multi-engine-brother*`. Memory: `project_multi_engine_brother_shipped`.
- **Always:** `taskkill //F //IM Tankoban.exe` before any rebuild (Rule 1); `scripts/stop-tankoban.ps1` after any agent-driven smoke (Rule 17); claim the appropriate lane lease (`out\tankoctl.exe lease-acquire mcp|build|shared-file:<path>`) before desktop / build / file-shared work per Rules 19 + 22 (gov-v7).
