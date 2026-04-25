# Tankoban 2 — Session Bootstrap & State Dashboard

This file auto-loads into every Claude Code session in this directory. The dashboard block is **state** (refreshed by Agent 0 at every phase-boundary commit per Rule 13). Rules and protocols live in `agents/GOVERNANCE.md` — this file does not duplicate them.

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

**UI smoke is NOT Hemanth's job anymore either (as of 2026-04-19).** Two MCP servers are registered in `.mcp.json` at repo root — both project-scoped, both auto-loaded for every Claude Code session in this project. Any agent (1/2/3/4/4B/5) can drive Tankoban's UI programmatically: launch via build_and_run.bat, click tabs, play torrents, read overlay text, screenshot, check widget state. If your domain needs a smoke and the thing being smoked is mechanical (does the button work? does the buffer fill? does the seek land at the right position?), **you do the smoke via MCP — do not ask Hemanth.** Hemanth's role further shrinks to visual-quality + taste judgment only (HDR tone-mapping, subtitle smoothness, frame pacing feel, AV sync feel, "does it look right").

**Which MCP, when** (added 2026-04-23):
- **`mcp__pywinauto-mcp__*`** — primary for Qt widget interaction. UIA-native: click/read/set by AutomationId or ControlType, no pixel coordinates needed. Qt publishes 100% AutomationId coverage via `objectName()` (verified by `scripts/uia-dump.ps1`). Use for: source-row clicks, button invokes, reading HUD text / progress / badge state, any widget that has a stable AutomationId. If a Qt custom widget has no AutomationId or no IInvokeProvider, that's an Agent 3 QAccessibleInterface follow-up — NOT a reason to fall back to pixel clicks silently.
- **`mcp__windows-mcp__*`** — secondary, for what pywinauto-mcp doesn't cover. Screenshots (visual confirmation), `Shortcut` for keyboard input (Ctrl+F, Esc, arrows, space-to-play), `PowerShell` for compound scripts, clipboard, process enumeration, general-purpose ops. Pixel `Click` is LAST resort; prefer UIA-invoke first.
- Both under Rule 19 MCP LANE LOCK — one agent drives the desktop at a time regardless of which server they're using.

**Build-command contract** (so no agent has to "invent" these for Hemanth):
- Run-the-app-with-telemetry: `build_and_run.bat` (env vars are baked in at lines 81 + 87, no manual `set` needed).
- Verify a .cpp compiles: `build_check.bat` (agent runs it, not Hemanth).
- Run main-app tests: `-DTANKOBAN_BUILD_TESTS=ON` + `ctest` (agent runs it).
- Dashboard drift / tracked junk / large files: `/repo-health` or `powershell -NoProfile -File scripts/repo-health.ps1` (agent runs it).

If you are tempted to give Hemanth a terminal command list longer than **one line** or a multi-step procedure that requires him to decide something technical, **stop and re-read this block.** Menu-ing Hemanth with coder steps has been flagged as a recurring brotherhood failure 2026-04-17, 2026-04-18, and 2026-04-19. Rule 14 + Rule 15 + multiple feedback memories codify this already — the block above is the always-loaded pointer.

---

## 30-Second State Dashboard

**As of:** 2026-04-18 (Agent 0 — **CONGRESS 6 RATIFIED by Hemanth delegation** 2026-04-18 (`Execute` recorded in integration memo §12 by Agent 0 on Hemanth instruction "do it on my behalf"). Integration memo at [agents/audits/congress6_integration_2026-04-18.md](agents/audits/congress6_integration_2026-04-18.md). **P2/P3/P4 GATES OPEN FOR EXECUTION.** Agent 4 may begin P2 StreamPieceWaiter against Agent 4B's shipped `pieceFinished` signal (`022c4eb`), with M1/M2/M3 decisions at entry. Agent 3 may begin P4 sidecar probe escalation. Agent 4 P3 after P2 lands, with M4/M5/M6 at P3 design entry. Congress 5 + 6 both archived; Agent 7 prior stream audits demoted to [_superseded/](agents/audits/_superseded/). 12-method API freeze active through P6 terminal tag.)

**Active agents:**
- **Agent 1** (Comic Reader) — IDLE, polish mode (`COMIC_READER_FIX_TODO.md` Phase 6 closed)
- **Agent 2** (Book Reader) — IDLE, awaiting Hemanth smoke on 8 batches across `BOOK_READER_FIX_TODO.md` Phases 1+2+3+5
- **Agent 3** (Video Player) — **HEMANTH-DRIVEN MODE 2026-04-25.** All standing player fix-TODOs + audits archived to `agents/_archive/` per Hemanth directive "future video player work will involve Hemanth full hands on." No agent-initiated player audits / TODO authoring / direction-picking going forward. Agent 3 responds to explicit Hemanth change requests only. Shipped-and-live infrastructure preserved: `scripts/compare-mpv-tanko.ps1`, SidecarProcess IPC tracker, subtitle vertical-position slider, cursor auto-hide on canvas, popover wheel isolation. See memory `feedback_hemanth_driving_player_domain.md`.
- **Agent 2** (Book Reader) — IDLE, **`EDGE_TTS_FIX_TODO` CLOSED 2026-04-16** at `17a202b` (all 5 phases / ~9 batches squashed; Phase 4 streaming deferred conditionally per TODO Phase 4.3 gate). EdgeTtsClient (Qt direct WSS) + EdgeTtsWorker (QThread) shipped; static voice table + LRU cache + failure taxonomy + HUD collapse all in. Awaiting Hemanth main-app build + Listen-button smoke matrix.
- **Agent 4** (Stream mode) — ACTIVE, **STREAM_SERVER_PIVOT direction SET 2026-04-24** after 4 wakes of STREAM_HTTP_PREFER hit diminishing returns. Wake-4 libtorrent tuning reverted; LoadingOverlay simplified to indeterminate bar this wake. Standing by for Hemanth ratification of STREAM_SERVER_PIVOT_TODO + Agent 7 P0 prototype greenlight before P1 kickoff.
- **Agent 4B** (Sources) — IDLE, **STREAM_ENGINE_REBUILD P2/P3 substrate SHIPPED** 2026-04-18 (`pieceFinished(QString, int)` signal at TorrentEngine.cpp:158-164 + `peersWithPiece(hash, pieceIdx) const` method + 12-method API freeze on-record — all preserved under STREAM_SERVER_PIVOT; Congress 6 freeze still applies). Next summon: STREAM_SERVER_PIVOT P4 Tankorent-isolation sign-off (verify Agent 4's pivot didn't touch TorrentEngine). `TANKORENT_HYGIENE_FIX` Phases 1+2+3 SHIPPED + committed.
- **Agent 5** (Library UX + Theme) — IDLE, **THEME_SYSTEM_FIX_TODO.md authored 2026-04-25** from Agent 5's `qt_theme_feasibility_2026-04-25.md` audit; awaiting Hemanth ratification → P1 kickoff (Theme.h infrastructure + applyTheme + split-key QSettings persistence). 4 phases (~4-6 summons): P1 infra → P2 picker UI → P3 light-mode 55-override port → P4 Win11 Mica + tile hover polish.
- **Agent 6** (Reviewer) — DECOMMISSIONED 2026-04-16 (do not summon; READY FOR REVIEW lines retired)
- **Agent 7** (Codex prototypes + audits) — IDLE, last delivery `agents/prototypes/{player_lifecycle,stream_lifecycle}/` 2026-04-16
- **Agent 8** (Prompt Architect) — ON-DEMAND, woken by Hemanth in a new tab ("agent 8 wake up" / "you're agent 8"). Conversational prompt crafter + chat.md-on-behalf poster. Persona file at [.claude/agents/prompt-architect.md](.claude/agents/prompt-architect.md). No state between sessions.

**READY TO COMMIT backlog:** ~14 lines pending (Congress 6 bundle — audits + assistant reviews + integration memo + substrate ship + summon briefs; batch-sweep at session close)

**Open congresses:** none (Congress 6 archived same-session 2026-04-18 to `agents/congress_archive/2026-04-18_congress6_stremio_audit.md`; Congress 5 archived to `agents/congress_archive/2026-04-18_stream_engine_rebuild.md`)

**Open HELP requests:** none (Agent 0 → Agent 4B asks all SATISFIED by 4B's 2026-04-18 substrate ship)

**Blocked:** none. P2/P3/P4 unblocked by Hemanth delegated ratification 2026-04-18 — Agent 4 P2 + Agent 3 P4 may execute on summon; Agent 4 P3 follows P2 land.

**Last successful smoke:** PLAYER_PERF_FIX Phase 2 (D3D11_BOX) green on Sopranos S06E09 + The Boys S03E06 — 2026-04-16. Phase 3 Option B (SHM-routed overlay) SHIPPED 2026-04-16, awaiting smoke.

**Live governance versions:** `gov-v2` / `contracts-v1` (see `agents/VERSIONS.md`)

---

## For Claude Sessions — Reading Order

See `agents/GOVERNANCE.md` "Session Start — Reading Order" section. Slimmed 2026-04-16: VERSIONS.md + this file are always-required; everything else is conditional.

This file is **state** (who/what/where right now). `agents/GOVERNANCE.md` is **rules** (how anyone operates). Do not duplicate rules here.

For Codex (Agent 7): see `AGENTS.md` at this same root, which redirects you into the brotherhood's governance.

---

## Required Skills & Protocols — non-negotiable every session

Every agent honors the listed skill invocations at the listed triggers. `superpowers:using-superpowers` (auto-loaded at SessionStart) enforces the discipline: *"if even 1% chance a skill applies, invoke it."* 21 skills adopted across three plugin namespaces (`superpowers:` / `claude-mem:` / `example-skills:`) + built-in slash commands. Full rationale + NOT-adopted list: memory `feedback_plugin_skills_adopted.md`.

### Session bootstrap

- **Every wake — `/brief`** for live dashboard. SessionStart hook prints a pre-digest; `/brief` is the full state read.
- **Every wake — `superpowers:using-superpowers`** (auto-loaded) — reminder to invoke other skills aggressively.

### Before shipping work (pre-RTC)

- **`/superpowers:verification-before-completion`** — evidence-before-assertions checklist. Every RTC, every agent.
- **`/simplify`** — reuse + efficiency review (fixes issues found). When diff has non-trivial edits.
- **`/build-verify`** — pre-RTC build gate when `src/` or `native_sidecar/src/` touched. Runs `build_check.bat` or sidecar build; tail-captures last 30 lines on failure.
- **`/superpowers:requesting-code-review`** — self-review primer on your own diff.
- **`/security-review`** — security audit of pending changes. REQUIRED when touching `src/core/stream/*`, `src/core/torrent/*`, `native_sidecar/src/*`, or anything handling user-facing input / network-exposed surfaces.

### Debugging & bug hunts

- **`/superpowers:systematic-debugging`** — FIRST, before proposing fixes. Any bug / test failure / unexpected behavior. Aligns with `feedback_evidence_before_analysis.md`.

### Creative / design / feature work

- **`/superpowers:brainstorming`** — before scoping a new feature, fix-TODO, or refactor. Also before posting a Congress position block (reduces Agent 0 synthesis rework).
- **`/superpowers:writing-plans`** — authoring a standalone plan file at `~/.claude/plans/*.md`. Pairs with Rule 18's plan-execute-smoke-verify loop.
- **`/superpowers:executing-plans`** — executing a plan file. Structured checkpoint discipline.

### Correction handling

- **`/superpowers:receiving-code-review`** — Hemanth corrects your work / Agent 7 audit lands with findings for your domain. Stops performative agreement + blind re-implementation.

### Cross-session memory + codebase structure

- **`/claude-mem:mem-search`** — "Didn't we solve this before?" / "What was the conclusion on X?" / "How did we fix Y?" BEFORE re-deriving or chat_archive dig.
- **`/claude-mem:smart-explore`** — "What functions live in X?" / "Class structure of Y?" / "Find all callers of Z?" Tree-sitter AST, more token-efficient than Grep for structural questions.

### Parallel / sub-agent dispatch

- **`/superpowers:dispatching-parallel-agents`** — `Agent()` branching into 2+ independent tasks.
- **`/superpowers:subagent-driven-development`** — executing a fix-TODO phase via Agent() dispatch.

### Narrow-domain-only

- **`/superpowers:test-driven-development`** — opt-in ONLY for `tankoban_tests` pure-logic primitives (Codex #4 Stage 3a). Smoke-first everywhere else.

### Building new brotherhood tooling

- **`/example-skills:skill-creator` + `/superpowers:writing-skills`** — paired, when creating a new Tankoban skill.
- **`/example-skills:mcp-builder`** — authoring a new MCP server (e.g. Codex audit item #5 QTest debug bridge).

### Post-milestone + knowledge capture

- **`/claude-mem:timeline-report`** — post-big-ship narrative.
- **`/claude-mem:knowledge-agent`** — ripe corpus for focused mini-brain (Agent 0 decides when to commission).

### Agent 0 phase-boundary tools (scoped, not universal)

- **`/commit-sweep`** — end of session with pending RTCs.
- **`/rotate-chat`** — chat.md > 3000 lines or > 300 KB.
- **`/repo-health`** — drift audit (tracked junk / large files / stale STATUS).

### Agent 8 (Prompt Architect) — conversational persona

- **Agent 8** is a brotherhood member like the rest. Hemanth wakes Agent 8 by opening a new Claude Code tab and saying "agent 8 wake up" / "you're agent 8" / "hey agent 8" / similar, OR by titling the tab something that names Agent 8. When you detect this as your session identity, read [.claude/agents/prompt-architect.md](.claude/agents/prompt-architect.md) as your persona file and stay in character for the whole conversation. No modes, no subcommands, no menus — talk to Hemanth naturally.
- Agent 8's role: take Hemanth's rough intent, talk it out, hand him a polished prompt he can paste into another agent's tab. Can also post to `agents/chat.md` on Hemanth's behalf (preview-before-post mandatory — see persona file §Posting on behalf).
- Tool scope: Read / Grep / Glob / Edit only. No Write, no Bash, no MCP, no sub-agent dispatch.
- Agent 8 does NOT write code, run builds, drive MCP, or dispatch sub-agents. It produces prompts.
- No STATUS.md section, no GOVERNANCE.md row, no memory file — state is query-time (persona loads CLAUDE.md + STATUS.md + chat.md tail at wake, on-demand the rest).

---

## Active Fix TODOs (owner + phase cursor)

> **Archive note 2026-04-20:** 13 closed / superseded TODOs (STREAM_LIFECYCLE_FIX, PLAYER_LIFECYCLE_FIX, PLAYER_PERF_FIX, PLAYER_UX_FIX, EDGE_TTS_FIX, STREAM_STALL_FIX, CINEMASCOPE_FIX, STREAM_ENGINE_FIX, STREAM_UX_PARITY, STREAM_PARITY, STREAM_PLAYBACK_FIX, STREAM_UX_PARITY_ADD_LATER, NATIVE_D3D11_TODO) have been moved to `agents/_archive/todos/` to declutter repo root. Rows below preserved for historical phase-cursor context. File paths in rows are now relative to `agents/_archive/todos/` for the CLOSED/SUPERSEDED entries; active entries still live at repo root as before.

> **Archive note 2026-04-25:** All player-domain fix-TODOs + audits DECOMMISSIONED per Hemanth directive "future video player work will involve Hemanth full hands on." 8 TODOs moved to `agents/_archive/todos/` (PLAYER_COMPARATIVE_AUDIT_TODO, PLAYER_POLISH_TODO, PLAYER_STREMIO_PARITY_FIX_TODO, STREAM_PLAYER_DIAGNOSTIC_FIX_TODO, SUBTITLE_HEIGHT_POSITION_TODO, VIDEO_PLAYER_FIX_TODO, VIDEO_PLAYER_UI_POLISH_TODO, VLC_ASPECT_CROP_REFERENCE_TODO). 12 audit files + 3 player-specific evidence logs moved to new `agents/_archive/audits/player/` directory. Going forward Hemanth drives all player-direction decisions manually; no agent-initiated player audits or fix-TODO authoring until Hemanth explicitly re-opens the workflow. See memory `feedback_hemanth_driving_player_domain.md`. Player-domain CODE (scripts/compare-mpv-tanko.ps1, SidecarProcess IPC tracker, sub-position slider, cursor auto-hide, popover wheel fix) remains live — infrastructure is preserved, only the planning/analysis docs are archived. Agent 3 is now pure execution-on-demand (no standing fix-TODO queue).

| TODO file | Owner | Phase cursor | Status |
|-----------|-------|--------------|--------|
| **`TANKOLIBRARY_FIX_TODO.md`** | Agent 4B | **AUTHORED 2026-04-21**, M1 queued pending Agent 4B summon | Greenfield new Sources sub-app (sibling to Tankorent + Tankoyomi) for book discovery via shadow libraries. v1 = 2 stateless sources (Anna's Archive + LibGen); Z-Library deferred as future stateful-source phase. Two tracks: **Track A Main** (M1 scaffold + AA search-only / M2 AA detail + download / M3 LibGen + dual-source fan-out) + **Track B Polish** (filters + cover fetch+cache + detail cards + IndexerHealth). Based on Agent 7 audit [tankolibrary_2026-04-21.md](agents/audits/tankolibrary_2026-04-21.md) (committed `c8052ee`) + Agent 4B domain validation (5/5 hypotheses confirmed, chat.md 14:?? block). New `src/core/book/` tree parallel to `src/core/manga/`; `BookResult` richer than `MangaResult` (format/language/publisher/year/pages/ISBN/MD5/size). Downloads → existing BooksPage library path; `LibraryScanner` picks up on next scan. Openlib selectors NOT ported (pre-drift vs current AA templates); used for flow decomposition only. Reuses `CloudflareCookieHarvester` for AA stage-(a) CF; new `AaSlowDownloadWaitHandler` for stage-(b) countdown + `no_cloudflare` warning handling. |
| `BOOK_READER_FIX_TODO.md` | Agent 2 | 1+2+3+5 SHIPPED | awaiting Hemanth smoke; Phase 4 explicitly deferred |
| `COMIC_READER_FIX_TODO.md` | Agent 1 | Phase 6 closed | polish mode (no new UI/UX); 10 phases ~26 batches scoped |
| **`THEME_SYSTEM_FIX_TODO.md`** | Agent 5 | **AUTHORED 2026-04-25**, awaiting Hemanth ratification → P1 kickoff | 4-phase port of Tankoban-Max two-axis theme system to Qt6 + QSS + QGraphicsEffect + QPropertyAnimation. P1 Theme.h infrastructure (palette + preset registry + applyTheme + split-key QSettings). P2 picker UI (top-right topbar icon cluster: sun/moon for axis A dark↔light, paint-palette for axis B 7-swatch popover). P3 light-mode 55-effect-override port from Tankoban-Max `theme-light.css`. P4 Win11 Mica via DwmSetWindowAttribute (Win-only `#ifdef`) + tile hover-lift drop-shadow + memory `feedback_qt_vs_electron_aesthetic.md` narrowing-in-place to F-bucket compositing only. ~4-6 summons, ~400-700 LOC, no new Qt deps. Source audit: `agents/audits/qt_theme_feasibility_2026-04-25.md` PATH B with 6 RESOLVED ANSWERS locked Hemanth 2026-04-25 ~16:30. |
| **`STREAM_SERVER_PIVOT_TODO.md`** | Agent 0 (authored) + Agent 7 P0 + Agent 4 P1-P5 + Agent 4B P4 sign-off | **AUTHORED 2026-04-24**; awaiting Hemanth ratification → Agent 7 Trigger-B P0 prototype kickoff | 7-phase strategic pivot: stream mode off libtorrent C++ engine onto Stremio's own Rust `stream-server` binary (perpetus/stream-server, pre-built Windows exe) as subprocess + REST adapter. Tankorent stays on libtorrent. Pre-built binary download + SHA-256 pin; 127.0.0.1 scope via Windows Firewall inbound rule (no source patches). Legacy-flag rollback window through P4 (`TANKOBAN_STREAM_BACKEND={legacy,server}` CMake option). Agent 7 Trigger-B P0 is HARD GATE — must prove HOLY_GRAIL=1 on Invincible S01E01 + seek test on pre-built binary before any src/ changes. Hemanth decision 2026-04-24 15:34 after 4 wakes of STREAM_HTTP_PREFER tuning hit diminishing returns. **Supersedes STREAM_ENGINE_REBUILD_TODO scope on P5 deletion.** |
| `STREAM_ENGINE_REBUILD_TODO.md` | Agent 4 (primary) + 4B (substrate) + 3 (sidecar probe) | **SUPERSEDED-on-P5 by STREAM_SERVER_PIVOT_TODO 2026-04-24** — P0 StreamProgress schema_version=1 ship (`ad2bc65`) preserved; Phases 1-6 moot. Archive to `agents/_archive/todos/` at PIVOT P6. | (Historical) Stream engine rebuild against Stremio Reference (libtorrent-rasterbar via libtorrent-sys FFI — semantic port). Retired in favor of running Stremio's reference source compiled as a subprocess — strengthens Congress 8 discipline rather than breaking it. Agent 4B substrate ship (`pieceFinished` signal + `peersWithPiece` method + 12-method API freeze at `022c4eb`) remains part of TorrentEngine API contract; Congress 6 freeze honored throughout pivot. Agent 7 prior stream audits stay demoted at [agents/audits/_superseded/](agents/audits/_superseded/). |
| `STREAM_LIFECYCLE_FIX_TODO.md` | Agent 4 | **CLOSED 2026-04-16; SUPERSEDED-on-P6 by STREAM_ENGINE_REBUILD** | All 5 phases shipped (~9 batches); audit findings P0-1/P1-1/P1-2/P1-4/P2-2/P2-3 closed; awaiting behavioral smoke. Work rolls into rebuild's preserved lifecycle semantics (StopReason enum + cancellationToken frozen). |
| `STREAM_ENGINE_FIX_TODO.md` | Agent 4 | **SUPERSEDED by STREAM_ENGINE_REBUILD_TODO 2026-04-18** | Slice A audit fix TODO — some work shipped, rest moot under rebuild scope. Formally closed on P6 exit of rebuild. |
| Cinemascope architectural fix | Agent 7 (exception) | **SHIPPED 2026-04-16** at `ade3241` | Canvas-sized overlay plane + set_canvas_size protocol + PGS coord rescale + cinemascope viewport math centering (closes asymmetric-letterbox cosmetic bug as side-effect); SUBTITLE_GEOMETRY_FIX_TODO authoring SKIPPED — work shipped directly under Hemanth's once-only exception |
| Edge TTS audit | Agent 7 | **DELIVERED 2026-04-16** at `8f48b82` (`agents/audits/edge_tts_2026-04-16.md`) | 338-line comprehensive audit; Agent 2 validation pass complete at `0b18ab2`; TODO authored. |
| `EDGE_TTS_FIX_TODO.md` | Agent 2 | **CLOSED 2026-04-16** at `17a202b` | All 5 phases shipped (~9 batches squashed); EdgeTtsClient + EdgeTtsWorker live; static voice table + LRU 200-cap cache + failure taxonomy + HUD collapse + edgeDirect ghost deleted; Phase 4 streaming conditionally deferred per TODO gate. Awaiting Hemanth Listen-button smoke. |
| `STREAM_UX_PARITY_TODO.md` | Agent 4 | **SUPERSEDED-on-P6 by STREAM_ENGINE_REBUILD_TODO 2026-04-18** | Remaining Shift+N handler batch can roll into rebuild window or shipped independently pre-rebuild. |
| `TANKORENT_FIX_TODO.md` | Agent 4B | All 7 phases SHIPPED | smoke pending |
| `TANKORENT_HYGIENE_FIX` | Agent 4B | Phases 1+2+3 SHIPPED + committed | done; data-dir self-healing on next boot |
| `STREAM_PARITY_TODO.md` | Agent 4 | All 6 phases SHIPPED; SUPERSEDED-on-P6 by STREAM_ENGINE_REBUILD_TODO | Historical — closed |
| `STREAM_PLAYBACK_FIX_TODO.md` | Agent 4 | All 3 phases SHIPPED; SUPERSEDED-on-P6 by STREAM_ENGINE_REBUILD_TODO | Historical — closed |

---

## Memory Pointer

Long-term memory: `C:\Users\Suprabha\.claude\projects\c--Users-Suprabha-Desktop-Tankoban-2\memory\MEMORY.md` (off-git, per-machine).

Archived memories: `memory/_archive/INDEX.md`. Quarterly audit per File Hygiene section in `agents/GOVERNANCE.md`.

---

## Recent Rotations & Maintenance

- **chat.md rotated** 2026-04-16: lines 8–19467 → `agents/chat_archive/2026-04-16_chat_lines_8-19467.md`. Live chat.md ~532 lines (steady-state target 1500–2500). Next rotation trigger: 3000 lines OR 300 KB.
- **Congress 4 archived** 2026-04-16: `agents/congress_archive/2026-04-16_library-ux-1-1-parity.md`. CONGRESS.md reset to empty template with auto-close clause.
- **STATUS.md header fields introduced** 2026-04-16: `Last header touch` + `Last agent-section touch` two-field discipline (Rule 12).
- **Memory consolidation** 2026-04-16: 7 archive moves + 1 merge → 45 active entries, MEMORY.md ~46 lines.
- **Governance bumped** 2026-04-16: gov-v1 → gov-v2 (slim reading order + Rules 12, 13 + Maintenance section + Congress auto-close).
- **Automation surface live** 2026-04-16 (Track 4): `.claude/commands/{commit-sweep,brief,rotate-chat,build-verify}.md` slash commands; `.claude/scripts/{scan-pending-commits,session-brief,congress-check}.sh`; `.claude/agents/commit-sweeper.md` sub-agent; `.claude/settings.json` with SessionStart + UserPromptSubmit hooks.
- **Contracts bumped to contracts-v2** 2026-04-16: sidecar build unlocked for agents (`native_sidecar/build.ps1` + `build_qrhi.bat` now agent-runnable from bash); main app build stays honor-system.
- **Lifecycle TODOs both CLOSED** 2026-04-16: PLAYER_LIFECYCLE (Agent 3, 3 phases ~4 batches) + STREAM_LIFECYCLE (Agent 4, 5 phases ~9 batches); 7 audit findings closed (P0-1, P1-1, P1-2, P1-4, P1-5, P2-2, P2-3).
- **chat.md at 2833 lines** — very close to rotation trigger (3000 lines / 300 KB). Run `/rotate-chat` next session boundary.
- **PLAYER_UX_FIX closed 2026-04-16:** 6 phases / ~11 batches. Symptoms addressed: 30s blank startup (metadata hoist + LoadingOverlay), HUD stale data (teardownUi reset), HDR mapping (Path A shrink), Tracks/EQ/Filters polish (IINA parity + presets + chip state). Carry-forward: SUBTITLE_GEOMETRY_FIX_TODO for cinemascope subtitle architectural fix.

---

## New Agent / Consultant Onboarding

Path: see `agents/ONBOARDING.md` — 15-minute orientation track that gets a new contributor productive without reading 7 governance files + 50 memories.

---

## Build Quick Reference

- Main app (Release + asset deploy + run): `build_and_run.bat`
- Main app (Debug, MSVC2022 + Qt6.10.2): `build2.bat`
- Main app (compile-only, agent-safe): `build_check.bat` — `BUILD OK` / `BUILD FAILED exit=<n>` + 30-line cl.exe tail, no exe run, no GUI spawn (Codex #4 Stage 1)
- Sidecar (MinGW, installs to `resources/ffmpeg_sidecar/`): `powershell -File native_sidecar/build.ps1`
- Main-app tests (pure-logic primitives; opt in at configure with `-DTANKOBAN_BUILD_TESTS=ON`; fetches GoogleTest via FetchContent on first run, MSVC-built to match main app ABI): `cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release -DTANKOBAN_BUILD_TESTS=ON <flags>; cmake --build out --target tankoban_tests; cd out && ctest --output-on-failure -R tankoban_tests` (Codex #4 Stage 3a)
- Repo-health audit (drift surfaces — tracked generated files, large source files, chat.md rotation, STATUS vs CLAUDE drift, RTC backlog, CONGRESS/HELP state): `powershell -NoProfile -File scripts/repo-health.ps1` or `/repo-health` slash command (Codex #5)
- Runtime-health digest (post-smoke log-scan — process state, stream telemetry event counts, sidecar PERF, error-line scan with benign-chatter filter): `powershell -NoProfile -File scripts/runtime-health.ps1` (Agent 7 MCP audit item #1, 2026-04-19)
- Smoke cleanup (kill Tankoban + ffmpeg_sidecar post-smoke per Rule 17): `powershell -NoProfile -File scripts/stop-tankoban.ps1` (added 2026-04-20)
- Mpv-vs-Tankoban regression harness v1 (log-parser only — feed two already-recorded logs, get a one-line `verdict=CONVERGED/DIVERGED-WORSE` diff on dropped frames + stall cycles; exit 0/1/2): `powershell -NoProfile -File scripts/compare-mpv-tanko.ps1 -MpvLog <path> -SidecarLog <path> [-File "<label>"] [-DurationSec N]` (Agent 3, added 2026-04-24 post-VIDEO_QUALITY_DIP audit). v2 future-work: orchestrate fresh mpv + MCP-Tankoban runs and diff. Validated against `agents/audits/evidence_{mpv_baseline,sidecar_debug_dip_smoke}_20260424_132114.*` → reproduces 332 drops / 53 stall_pauses / 50 stall_resumes exactly.
- IPC round-trip latency tracker (runtime instrumentation in `SidecarProcess` — stamps send-time per seq on each sendCommand, matches against the sidecar's generic `ack` event by seqAck, accumulates per-command p50/p99/max/count; dumps to `out/ipc_latency.log` on destructor / clean shutdown; non-behavioral measurement-only): automatic on every Tankoban run — no flag needed. Append-only log, one session block per run (`## session_end=<ISO ts> total_commands=N distinct_cmd_types=M pending_unmatched=K` header then `cmd=<name> count=… p50=…ms p99=…ms max=…ms` rows sorted alphabetically). Companion to compare-mpv-tanko.ps1 — the harness says IF we regressed, this says WHERE the time goes (Agent 3, 2026-04-24 strategic thread #3 first piece).
- UIA enumeration (structural/state smoke alternative to pixel-based Windows-MCP): `powershell -NoProfile -File scripts/uia-dump.ps1 [-MaxDepth 6] [-TargetClass StreamPage]` — dumps Tankoban's UIA tree per-widget. Qt auto-publishes 100% AutomationId coverage via `objectName()`; see audit `agents/audits/uia_inspection_2026-04-22.md` + memory `feedback_pywinauto_when.md` for when to pick UIA vs pixel MCP. Interactive UIA exploration: `C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/inspect.exe` (already installed via Windows 11 SDK, no setup).
- Always: `taskkill //F //IM Tankoban.exe` before any rebuild (Rule 1). And `scripts/stop-tankoban.ps1` after any agent-driven smoke (Rule 17). And `MCP LOCK` / `MCP LOCK RELEASED` chat.md lines around any desktop-interacting MCP work (Rule 19).
