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

**As of:** 2026-04-26 ~11:45am (Agent 0 — **REPO_HYGIENE_FIX_TODO Phase 1 ✅ CLOSED, BUILD GREEN + double-launch smoke GREEN.** Phase 1 spanned two wakes: pre-pause (sweep + P1.1 backfill commits `bad1cea` + `d0bfee2` + P1.3 ring buffer + P1.2 path strip across 8 main-app files + P1.5 tray-quit cleanup) and fresh-chat resume (P1.4 single-instance wire + smoke + close-out). Single-instance smoke: first launch PID 840 binds `TankobanSingleInstance` socket; second launch attempt exit=0 in 1027ms; only PID 840 survives — confirms `signalExistingInstance()` round-trip ack + first-instance `bringToFront()` callback both fire correctly. Helper batch files `_p1_*_oneshot.bat` deleted at close-out. Honest carry-through: native_sidecar/src/{subtitle_renderer.cpp, main.cpp} pre-existing dirt NOT lifted into Phase 1 commit — only Phase-1-relevant lines (sub_log no-op + `freopen("sidecar_debug_live.log")` env-gated behind `TANKOBAN_SIDECAR_DEBUG=1`). One bundled commit covers all of Phase 1. Rule-14 sequencing locked: Phase 3 (dev-control bridge) PROMOTED ahead of Phase 2 (vcpkg) — Phase 3 only depends on Phase 1's ring buffer; Phase 2's vcpkg is heaviest-single-phase polish that lands later. **Pre-pause this wake:** REPO_HYGIENE_FIX_TODO authored off external-AI-audit + ChatGPT-tuned dev-bridge proposal + Hemanth's strategic intent. 7-phase arc: P1 ✅ / P2 vcpkg / P3 dev-control bridge / P4 lifecycle fixes / P5 CI / P6 installer + Releases / P7 docs. Phase 3 unblocked on Hemanth pacing; multi-agent execution, parallel-where-possible. **Prior wake (2026-04-25):** SKILL_DISCIPLINE_FIX ✅ ALL 6 PHASES CLOSED, single-wake six-phase arc spanning a machine reboot.** Phase 1 diagnosis (no claude.cmd on PATH) + Phase 2 remedy (Option A install + reboot; observations table now alive, /mem-search functional — see [diagnosis §10](agents/audits/claude_mem_persistence_diagnosis_2026-04-25.md)) + §5 ratification (8 strategic Decisions all answered) + Phase 3 contracts-v3 RTC provenance contract (GOVERNANCE.md Rule 11 + CONTRACTS.md new § + commit-sweeper.md regex + session-brief.sh card + VERSIONS.md) + Phase 4 pre-RTC nag hook on Stop event (.claude/scripts/pre-rtc-checker.sh + .claude/settings.json + telemetry JSONL; verified GREEN against live working tree) + Phase 5 memory-degraded sentinel (.claude/scripts/memory-health.sh; auto-demotes /mem-search rule when claude-mem state unhealthy; verified GREEN — currently DEGRADED status correctly fires banner since no built corpus yet) + Phase 6 tiered skill sheet (CLAUDE.md "Required Skills & Protocols" rewritten as Tier 1 Core Mandatory ~6 + Tier 2 Conditional ~13 + Tier 3 Milestone-only ~2; per-agent shortlists in STATUS.md; memory `feedback_plugin_skills_adopted.md` narrowed; new memory `feedback_skill_discipline_remeasurement.md` documents 30-day re-measurement criterion = telemetry ≥30 OR 2026-05-25). TODO archive-ready. Awaits Hemanth `/commit-sweep` to land the bundle (~14 files of substance + carry-through). Concurrent agent state same-wake: A5 shipped THEME_SYSTEM_FIX P3.1 + Light-mode REMOVAL + library polish (audiobooks header consistency + density slider extension to continue strips); A1 has 2 RTCs queued (Phase 1.1 + SinglePage-removal); A3 in HEMANTH-DRIVEN MODE shipped VIDEO_HUD_TIME_LABELS_FIX; A4 STREAM_SERVER_PIVOT P3 shipped earlier today. NOTE: agent state lines + last-smoke + congress fields below from prior wakes and partially stale; full refresh deferred to a future Agent 0 sweep.)

**Active agents:**
- **Agent 1** (Comic Reader) — IDLE, polish mode (`COMIC_READER_FIX_TODO.md` Phase 6 closed)
- **Agent 2** (Book Reader) — IDLE, awaiting Hemanth smoke on 8 batches across `BOOK_READER_FIX_TODO.md` Phases 1+2+3+5
- **Agent 3** (Video Player) — **HEMANTH-DRIVEN MODE 2026-04-25.** All standing player fix-TODOs + audits archived to `agents/_archive/` per Hemanth directive "future video player work will involve Hemanth full hands on." No agent-initiated player audits / TODO authoring / direction-picking going forward. Agent 3 responds to explicit Hemanth change requests only. Shipped-and-live infrastructure preserved: `scripts/compare-mpv-tanko.ps1`, SidecarProcess IPC tracker, subtitle vertical-position slider, cursor auto-hide on canvas, popover wheel isolation. See memory `feedback_hemanth_driving_player_domain.md`.
- **Agent 2** (Book Reader) — IDLE, **`EDGE_TTS_FIX_TODO` CLOSED 2026-04-16** at `17a202b` (all 5 phases / ~9 batches squashed; Phase 4 streaming deferred conditionally per TODO Phase 4.3 gate). EdgeTtsClient (Qt direct WSS) + EdgeTtsWorker (QThread) shipped; static voice table + LRU cache + failure taxonomy + HUD collapse all in. Awaiting Hemanth main-app build + Listen-button smoke matrix.
- **Agent 4** (Stream mode) — ACTIVE, **`SIDECAR_DISPATCHER_NON_BLOCKING_FIX_TODO.md` AUTHORED 2026-04-25 ~22:20** from prior wake's `project_sidecar_dispatcher_non_blocking_decision.md` memory. 2-phase plan: Phase A worker-thread split for `handle_set_tracks` + dispatch-table audit (~1-2 summons); Phase B `Source` abstraction env-gated (~3-5 summons). Awaiting Hemanth §5 ratification → Phase A.1 kickoff. Plan-mode design saved at `~/.claude/plans/so-create-a-comprehensive-effervescent-otter.md`. **Prior in-flight work:** STREAM_PLAYER_LIFECYCLE_FIXUPS layer-1 parity fixes shipped 2026-04-25 ~21:18 (`setFocus` + `stopPlayback` against MainWindow precedent) — they fix the easy path; this new TODO closes the deeper dispatcher-blocking class.
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

## Required Skills & Protocols — tiered (Core Mandatory + Conditional + Milestone-only)

**Restructured 2026-04-25 per SKILL_DISCIPLINE_FIX_TODO Phase 6 + §5 question 1+7+8 ratification.** The prior framing (21 skills, all "mandatory") produced near-zero observable use of long-tail skills (audit `agents/audits/skill_discipline_audit_2026-04-25.md`: 0 explicit `/mem-search`, `/smart-explore`, `/timeline-report`, `/knowledge-agent` mentions across 176-RTC sample). The new framing splits skills by trigger frequency. `superpowers:using-superpowers` (auto-loaded at SessionStart) preserves the meta-rule: *"if even 1% chance a skill applies, invoke it."* — but the actual mandatory floor is now smaller and more honest.

Full rationale + NOT-adopted list: memory `feedback_plugin_skills_adopted.md`.

### Tier 1 — Core Mandatory (~6 skills, every relevant wake)

These fire often enough that every active agent meets them most wakes. The new RTC contract (contracts-v3) requires these to be named in the `Skills invoked: [...]` field on non-trivial RTCs (≥1 src/ file or ≥30 LOC) — see `agents/CONTRACTS.md` § Skill Provenance in RTCs.

- **`/brief`** — every wake start. SessionStart hook prints a pre-digest; `/brief` is the full state read.
- **`/superpowers:verification-before-completion`** — every RTC, every agent. Evidence-before-assertions checklist. The single load-bearing skill of the brotherhood.
- **`/simplify`** — every non-trivial diff. Reuse + efficiency review (fixes issues found).
- **`/build-verify`** — whenever `src/` or `native_sidecar/src/` touched. Runs `build_check.bat` or sidecar build; tail-captures last 30 lines on failure.
- **`/superpowers:requesting-code-review`** — every non-trivial RTC. Self-review primer on your own diff.
- **`/superpowers:systematic-debugging`** — whenever the work is bug-shaped (test failure, unexpected behavior, log-grep, smoke iteration). FIRST, before proposing fixes.

### Tier 2 — Conditional (~13 skills, fire only on specific triggers)

NOT mandatory by default. Fire when the trigger matches; otherwise skip without guilt.

- **`/security-review`** — when touching `src/core/stream/*`, `src/core/torrent/*`, `native_sidecar/src/*`, or any user-facing input / network-exposed surfaces.
- **`/superpowers:brainstorming`** — before scoping a new feature, fix-TODO, refactor, or Congress position block.
- **`/superpowers:writing-plans`** — when authoring a standalone plan file at `~/.claude/plans/*.md`.
- **`/superpowers:executing-plans`** — when executing a plan file with structured checkpoint discipline.
- **`/superpowers:receiving-code-review`** — when Hemanth corrects your work or Agent 7 audit lands with findings for your domain.
- **`/claude-mem:mem-search`** — "Didn't we solve this before?" / "What was the conclusion on X?" BEFORE chat_archive dig. (Auto-demoted by SessionStart hook when claude-mem memory is degraded — see Phase 5 sentinel.)
- **`/claude-mem:smart-explore`** — structural code queries: "What functions live in X?" / "Class structure of Y?" / "Find all callers of Z?" Tree-sitter AST, more token-efficient than Grep.
- **`/superpowers:dispatching-parallel-agents`** — when branching into 2+ independent subagents via `Agent()`.
- **`/superpowers:subagent-driven-development`** — when executing a fix-TODO phase via `Agent()` dispatch.
- **`/superpowers:test-driven-development`** — opt-in ONLY for `tankoban_tests` pure-logic primitives (Codex #4 Stage 3a). Smoke-first everywhere else.
- **`/example-skills:skill-creator` + `/superpowers:writing-skills`** — paired, when creating a new Tankoban skill.
- **`/example-skills:mcp-builder`** — when authoring a new MCP server.

### Tier 3 — Milestone-only (~2 skills, post-big-ship narrative)

Fire post-milestone, NOT day-to-day. Demoted from prior "mandatory" framing per §5 question 7 — observed zero usage across the 176-RTC audit sample.

- **`/claude-mem:timeline-report`** — post-big-ship narrative. Agent 0 commissions when a TODO closes or a multi-week arc lands.
- **`/claude-mem:knowledge-agent`** — ripe corpus for focused mini-brain. Agent 0 commissions when a domain has accumulated enough observations to warrant a knowledge-agent.

### Agent 0 phase-boundary tools (scoped, not universal)

- **`/commit-sweep`** — end of session with pending RTCs.
- **`/rotate-chat`** — chat.md > 3000 lines or > 300 KB.
- **`/repo-health`** — drift audit (tracked junk / large files / stale STATUS).

### Re-measurement schedule

A 30-day re-measurement wake is on `/schedule` for ~2026-05-25 (Agent 0). It re-runs the audit's RTC-skill-mention count against the post-Phase-3+4+6 corpus to verify whether (a) the contracts-v3 provenance contract is being honored, (b) the pre-RTC nag hook is reducing missing-field rate, (c) the trim to ~6 core mandatory has not silently demoted any actually-load-bearing skill, and (d) any of the Tier-2 skills should promote to Tier-1 (or vice versa) based on actual usage data. Promote-to-block decision for the Phase 4 hook also depends on this re-measurement.

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
| **`REPO_HYGIENE_FIX_TODO.md`** | Agent 0 (author + cross-phase coordinator) + multi-agent (per-phase ownership in §6) | **AUTHORED 2026-04-26 + Phase 1 ✅ CLOSED 2026-04-26 ~11:45am** (BUILD GREEN + double-launch smoke GREEN; second launch exit=0 in 1027ms; bundled commit pending sweep). **Rule-14 sequencing locked**: Phase 3 (dev-control bridge) PROMOTED ahead of Phase 2 (vcpkg). Phase 3 unblocked on Hemanth pacing. | 7-phase arc covering external-AI-audit findings (19 items, 11 critical) + ChatGPT-tuned dev-control-bridge proposal + Hemanth's strategic intent ("anyone clones + builds without fuss"). **P1** commit untracked source + strip hardcoded `C:/Users/Suprabha/...` paths + bounded log ring buffer + wire dead single-instance code (Agent 0 + Agent 3). **P2** vcpkg dependency migration + CMakePresets + setup.bat (Agent 4 + 4B). **P3** dev-control bridge (`DevControlServer.{h,cpp}` + `tankoctl.cpp` + per-class `devSnapshot()`) for ~10x faster agent smokes via direct field reads vs UIA tree walks; coexists with pywinauto-mcp / windows-mcp (Agent 0 + 3 + 5). **P4** lifecycle bug fixes (audit 4-11: JsonStore race, scanner thread ownership, dropped rescans, sidecar UI-thread blocking, native-sidecar `std::stoi` hardening; multi-agent). **P5** GitHub Actions CI (catches future untracked-files-in-CMakeLists regressions; Agent 7 / Codex). **P6** Windows installer + GitHub Releases pipeline (NSIS, auto-build on tag push; Agent 7). **P7** documentation (README + BUILD + ARCHITECTURE + CONTRIBUTING + LICENSE; Agent 0 + 8). Sequencing: P1 first (foundation); P2+P5 parallel; P3+P4 parallel after P1; P6 after P2+P5; P7 after P6. |
| **`SKILL_DISCIPLINE_FIX_TODO.md`** | Agent 0 (author + first-time executor) | **✅ ALL 6 PHASES CLOSED 2026-04-25** — single-wake six-phase arc spanning a machine reboot. Archive-ready. | 6-phase fix on Agent 7's [skill_discipline_audit_2026-04-25.md](agents/audits/skill_discipline_audit_2026-04-25.md). Load-bearing finding: claude-mem repair is a prerequisite, not a result, of enforcing `/mem-search`. **Phase 1+2 same-wake arc 2026-04-25 — root cause: no `claude.cmd` on PATH (Hemanth uses Claude Code via VS Code extension not CLI); remedy: Option A `npm install -g @anthropic-ai/claude-code` + machine reboot to clear Windows zombie socket on port 37777; verification GREEN — observations table now has Tankoban hits, `/mem-search` works end-to-end (see [diagnosis §10](agents/audits/claude_mem_persistence_diagnosis_2026-04-25.md)).** §5 Decisions ratified 2026-04-25 ~22:30 wholesale per Hemanth — all 8 questions resolved with Agent 0 picks: split core+conditional / provenance required for non-trivial RTCs / nag-only first 30 days / serial memory-before-governance / accept 1-2s pre-RTC hook latency / Codex held to platform-gap'd standard / demote /timeline-report + /knowledge-agent to milestone-only / trim 21-sheet to ~6 core then re-measure 30 days. Phase 3 (RTC `Skills invoked:` provenance + commit-sweeper + contracts-v3 bump). Phase 4 (pre-RTC checker hook). Phase 5 (memory-degraded sentinel). Phase 6 (skill sheet trim + per-agent shortlists). 30-day re-measurement is `/schedule` candidate post-Phase-4. |
| **`THEME_SYSTEM_FIX_TODO.md`** | Agent 5 | **AUTHORED 2026-04-25**, awaiting Hemanth ratification → P1 kickoff | 4-phase port of Tankoban-Max two-axis theme system to Qt6 + QSS + QGraphicsEffect + QPropertyAnimation. P1 Theme.h infrastructure (palette + preset registry + applyTheme + split-key QSettings). P2 picker UI (top-right topbar icon cluster: sun/moon for axis A dark↔light, paint-palette for axis B 7-swatch popover). P3 light-mode 55-effect-override port from Tankoban-Max `theme-light.css`. P4 Win11 Mica via DwmSetWindowAttribute (Win-only `#ifdef`) + tile hover-lift drop-shadow + memory `feedback_qt_vs_electron_aesthetic.md` narrowing-in-place to F-bucket compositing only. ~4-6 summons, ~400-700 LOC, no new Qt deps. Source audit: `agents/audits/qt_theme_feasibility_2026-04-25.md` PATH B with 6 RESOLVED ANSWERS locked Hemanth 2026-04-25 ~16:30. |
| **`SIDECAR_DISPATCHER_NON_BLOCKING_FIX_TODO.md`** | Agent 4 (Stream mode) | **AUTHORED 2026-04-25 ~22:20**, awaiting Hemanth §5 ratification → Phase A.1 kickoff | 2-phase fix on the dispatcher-blocking bug class in `native_sidecar/src/`. Surfaced from prior wake (`ade60215` 2026-04-25 ~21:30) as the deeper root cause behind STREAM_PLAYER_LIFECYCLE_FIXUPS — layer-1 parity fixes (`setFocus`/`stopPlayback`) shipped but couldn't reach the wedge case where `handle_set_tracks` blocks the dispatcher inside `preload_subtitle_packets` on HTTP sources. **Phase A (~1-2 summons): worker-thread split for `handle_set_tracks` (mirrors existing `handle_open` → `open_worker` shape) + cooperative cancellation atomic + dispatch-table audit pass.** **Phase B (~3-5 summons, env-gated): `Source` abstraction (`LocalSource` + `HttpSource`) replacing scattered `is_http` branches; lazy subtitle preload for HTTP.** §5 PROPOSED picks: broad Phase A scope / abort-the-first cancellation / env-gated Phase B / author+start same-wake / Agent 4 owns. Hemanth-driven directive 2026-04-25 ~21:30 "I want no compromises on our app — what is the TRUEST fix?" + memory `project_sidecar_dispatcher_non_blocking_decision.md`. Plan-mode design saved at `~/.claude/plans/so-create-a-comprehensive-effervescent-otter.md`. |
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
- Main app + libplacebo for SDR (LIBPLACEBO_SINGLE_RENDERER_FIX P2 opt-in, 2026-04-26): `build_and_run_libplacebo_sdr.bat` — wrapper that sets `TANKOBAN_LIBPLACEBO_SDR=1` then invokes `build_and_run.bat`. SDR files run through the same libplacebo pipeline HDR uses today (Vulkan + ewa_lanczossharp + ICC). Drop the wrapper at P3 once the gate is removed from `native_sidecar/src/main.cpp`.
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
