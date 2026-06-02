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

**Build-command contract:** agents run all builds, never Hemanth — command list in Build Quick Reference (below) / `agents/BUILD.md`.

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

See `agents/GOVERNANCE.md` "Session Start — Reading Order". Slimmed 2026-04-16: VERSIONS.md + this kernel are always-required; everything else is conditional (route via `agents/routes.yml`).

At wake read only YOUR latest recap + its trimmed transcript — not the recap archive. Older recaps + chat history are searchable on demand (grep / claude-mem), not auto-read.

For Codex (Agent 7): see `AGENTS.md` at this same root, which redirects you into the brotherhood's governance.

---

## Required Skills & Protocols

Tier-1 core skills below (every relevant wake). Full tiered catalog (Tier-2 conditional,
Tier-3 milestone, Agent-0 tools) + rationale: `agents/GOVERNANCE.md` Skills section +
memory `feedback_plugin_skills_adopted`. RTC `Skills invoked: [...]` provenance required
for non-trivial RTCs (contracts-v3).

- **`/hemanth-language`** — every wake (user-end terms, preview per task group, no silence, menus-off).
- **`/brief`** — full state read at wake start.
- **`/session-recap`** — wake END for non-trivial sessions.
- **`/superpowers:verification-before-completion`** — every RTC: evidence before assertions.
- **`/simplify`** — every non-trivial diff.
- **`/build-verify`** — whenever `src/` or `native_sidecar/src/` touched.
- **`/superpowers:requesting-code-review`** — every non-trivial RTC.
- **`/superpowers:systematic-debugging`** — FIRST, whenever the work is bug-shaped.

---

## Active work

Active fix-TODOs + phase cursors: **`agents/ACTIVE_TODOS.md`**. Per-domain detail
auto-loads from the subtree `CLAUDE.md` when you read that domain; task->files map is
`agents/routes.yml`.

---

## Memory Pointer

Long-term memory: `C:\Users\Suprabha\.claude\projects\c--Users-Suprabha-Desktop-Tankoban-2\memory\MEMORY.md` (off-git, per-machine).

Archived memories: `memory/_archive/INDEX.md`. Quarterly audit per File Hygiene section in `agents/GOVERNANCE.md`.

Memory freshness: each fact carries `status` (active/superseded) + `last_verified` (YYYY-MM-DD) so a recalled memory shows its own age before you act on it. Audit staleness with `python scripts/memory_lint.py` (flags missing fields + facts >120d unverified).

---

## New Agent / Consultant Onboarding

Path: see `agents/ONBOARDING.md` — 15-minute orientation track that gets a new contributor productive without reading 7 governance files + 50 memories.

---

## Build Quick Reference

- Run the app (Release + telemetry): `build_and_run.bat` · compile-check (agent-safe): `build_check.bat` · tests: `-DTANKOBAN_BUILD_TESTS=ON` + `ctest`.
- **Always:** `taskkill //F //IM Tankoban.exe` before any rebuild (Rule 1); `scripts/stop-tankoban.ps1` after any smoke (Rule 17); claim a lane lease before desktop/build/file-shared work (Rules 19+22).
- Full tooling list (sidecar build, /repo-health, log-scan, UIA dump, tankoctl, multi-engine helper): **`agents/BUILD.md`**.