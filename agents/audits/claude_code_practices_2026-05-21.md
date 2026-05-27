# Audit - Claude Code practices for Tankoban 2 - 2026-05-21

By Agent 7 (Codex). For Claude Code best-practices triage / Agent 0.
Reference comparison: Gemini DR raw report, ChatGPT DR raw report, current Tankoban 2 code/config state, and official Claude Code / Anthropic docs where public platform behavior matters.
Scope: Audit-only cross-reference of DR recommendations against current Tankoban 2 state. No code changes. Note: I searched current `agents/chat.md` and `agents/chat_archive/` for the requested "Gemini-vs-ChatGPT triangulation" anchors and did not find that exact block, so this pass uses the two raw reports plus live code/config evidence.

## Executive summary

Ship the low-risk diagnostics first: profile or temporarily disable `congress-check.sh`, inspect Claude Code hook behavior with debug logs, and add usage/latency observability before changing MCP architecture. The code evidence rejects the claim that `congress-check.sh` itself should create 30-45 seconds of dead air: it reads only `agents/CONGRESS.md`, performs a few `head`/`grep` operations, and is configured with a 2 second timeout. The MCP config does not match Gemini's cited Windows `cmd /c` bug pattern; it uses direct `uvx` and direct `codex.cmd`, with no `cmd /c` wrapper. The stronger architectural direction is validated: `tankoctl` / `TankobanDevControl` should become the primary agent runtime surface, while `pywinauto-mcp` stays a fallback for UIA/screenshot work. A DevControl lease registry is feasible and should replace chat.md as lock truth for build, MCP UI, shared-file, and Codex commission leases, though worktrees still matter for same-file edit fanout. `build_check.bat` can support per-lane build dirs with moderate changes, but the current script assumes `out/` is already configured and will need a configure-on-missing path rather than a one-line `cmake -B` swap. `CLAUDE.md` is 231 lines and has grown into a mixed boot sheet, live dashboard, skill catalog, TODO table, and command reference; target 80-100 lines and move procedures into skills/path-scoped rules. Agent Teams are not production-ready enough to replace the brotherhood: official docs mark them experimental and disabled by default, with known resumption/task/shutdown limitations. Do not ship `ENABLE_PROMPT_CACHING_1H=1` as a validated Claude Code fix; official Claude Code settings list `env` support but do not list that variable, while the official API mechanism is `cache_control.ttl`.

## Per-validation findings

### V1. Profile `congress-check.sh`

Direct answer: `congress-check.sh` is not plausibly the sole 30-45s lag source by its own logic. It computes repo root, reads `agents/CONGRESS.md`, extracts `STATUS` from the first 30 lines, checks for ratification markers with one file-wide `grep`, then creates one marker and prints a reminder only if stale-open Congress is detected (`.claude/scripts/congress-check.sh:7-35`). There is no git operation, no claude-mem query, no chat.md scan, and no network I/O. It is wired to `UserPromptSubmit` with `timeout: 2` (`.claude/settings.json:18-25`), and Anthropic documents `UserPromptSubmit` as running before prompt processing, with stdout injected into context and configurable command timeouts. Hook execution can therefore block prompt start, but this specific hook should top out at 2s unless the host hook runner itself is unhealthy. Smallest risk-eliminating change: temporarily remove the `UserPromptSubmit` hook or replace it with a zero-I/O marker check driven by SessionStart/Stop, then A/B prompt-submit latency under `--debug`.

Verdict: NEEDS-INVESTIGATION. Effort: 0.5 day diagnostic, 0.5 day removal/refactor if confirmed.

### V2. Audit `.mcp.json`

Direct answer: no `alwaysLoad: true` entries exist; the file has only two servers (`.mcp.json:1-12`). Tool deferral is not explicitly configured, but official Claude Code docs say MCP Tool Search is enabled by default and defers tool definitions until needed. `pywinauto-mcp` is auxiliary for Tankoban app state because `CLAUDE.md` already names `out/tankoctl.exe` primary and faster at 60-150ms (`CLAUDE.md:30-32`); it remains a useful fallback for UIA invocation and screenshots. `codex` is auxiliary/commission-only, not a hot path for every agent turn. Gemini's Windows bug claim maps to the public `cmd /c` parser issue, but the actual config does not use `cmd /c`: `pywinauto-mcp` calls `uvx` directly (`.mcp.json:3-6`) and Codex calls `C:\Users\Suprabha\AppData\Roaming\npm\codex.cmd` directly (`.mcp.json:7-10`).

Verdict: VALIDATED-FOR-SHIP as a config audit, but daemonization remains conditional. Effort: 0.5 day for config cleanup/experiments.

### V3. Unify-lease feasibility

Direct answer: feasible. `DevControlServer` is already a single local `QLocalServer` named `TankobanDevControl`, one JSON request per connection, one JSON reply, newline-delimited (`src/devtools/DevControlServer.h:10-24`). It dispatches every accepted command through `MainWindow::handleDevCommand` (`src/devtools/DevControlServer.cpp:115-129`), and `MainWindow` already routes many command families plus write gates for synthetic UI and system-state commands (`src/ui/MainWindow.cpp:2309-2445`). Smallest extension: add `lease_acquire`, `lease_release`, `lease_heartbeat`, `lease_get`, and `lease_list` commands backed by an in-memory `QHash<QString, Lease>` with holder, purpose, expiry, token, and monotonic timestamp. TTL cleanup can run on a `QTimer`; acquire returns `ACQUIRED`, `BUSY`, or `STALE_RECLAIMED`; release/heartbeat require the token.

This would collapse Rules 19 and 22 cleanly from chat truth into machine truth. Rule 21 does not disappear: worktrees still physically prevent same-file edit/staging collisions, but a `shared-file:<path>` lease can guard exceptional flat-tree edits.

Verdict: VALIDATED-FOR-SHIP. Effort: 1-2 days implementation plus tankoctl CLI verbs and smoke.

### V4. Per-worktree build dirs

Direct answer: moderately invasive. `build_check.bat` hardcodes `BUILD_DIR=%~dp0out` (`build_check.bat:16`) and only builds an already-configured tree (`build_check.bat:19-23`, `build_check.bat:32-33`). If each worktree has its own checkout, `%~dp0out` is already worktree-local. The problem is the shared root checkout; there the script needs a lane name (`TANKOBAN_BUILD_LANE`, sanitized branch/worktree name from `git rev-parse --git-dir`, or explicit argument) and a configure-on-missing path using the same preset/options as `build_and_run.bat`. Without configure-on-missing, `out_<lane>` will fail at the current CMakeCache guard.

Verdict: VALIDATED-FOR-SHIP, but not a one-line patch. Effort: 1 day if using CMakePresets; 1.5 days with test target support.

### V5. `CLAUDE.md` shrink

Direct answer: shrink from 231 lines to about 80-100. Always-loaded should keep: Hemanth role constraints (`CLAUDE.md:7-26`), a short "tool priority" map (`tankoctl` first, pywinauto fallback), build/run command pointers (`CLAUDE.md:39-45`), reading-order pointer (`CLAUDE.md:81-87`), and a small active-state pointer to `/brief`/STATUS. Move the live dashboard and long historical "As of" block (`CLAUDE.md:49-79`) to generated `/brief` or STATUS. Move the detailed skills catalog (`CLAUDE.md:91-142`) into `.claude/skills` metadata plus STATUS shortlists. Move Agent 8 persona (`CLAUDE.md:144-150`) into `.claude/agents/prompt-architect.md` with a one-line pointer. Move Active Fix TODOs (`CLAUDE.md:154-183`) to a generated ops board or STATUS. Move Recent Rotations (`CLAUDE.md:195-206`) to `/repo-health` output. Move Build Quick Reference (`CLAUDE.md:216-230`) to `/build-verify`, `/repo-health`, and a short command card.

Official memory docs support the principle: CLAUDE.md is loaded at startup, while nested CLAUDE.md files under subtrees are only included when files in those subtrees are read. Skills docs also say skill bodies load only when used, and path-limited skills can activate on matching file globs.

Verdict: VALIDATED-FOR-SHIP. Effort: 1 day for migration, 0.5 day for smoke with `/memory`, `/doctor`, and session-start digest.

### V6. Skill catalog overflow

Direct answer: actual project command count is 20, not 21. The files are: `audit-skeleton`, `brief`, `build-verify`, `codex-trigger-d`, `commit-sweep`, `fix-todo-new`, `handoff-brief`, `hemanth-language`, `hemanth-rewrite`, `mcp-lock`, `memory-trim`, `memory-write`, `repo-health`, `rotate-chat`, `rtc`, `session-recap`, `smoke-package`, `smoke-report`, `summon-from-todo-phase`, and `tdd-scaffold`. Only 6 have frontmatter descriptions today; 14 fall back to first paragraph. Official settings now expose `skillListingBudgetFraction` and `skillOverrides`, and docs say overflow collapses least-used skills to bare names; docs also support `disable-model-invocation: true` and `name-only`.

Recommended classification: keep `brief`, `build-verify`, `repo-health`, `session-recap`, and `hemanth-language` visible. Mark manual/scaffold commands `disable-model-invocation: true` or `user-invocable-only`: `commit-sweep`, `rotate-chat`, `memory-write`, `memory-trim`, `codex-trigger-d`, `fix-todo-new`, `audit-skeleton`, `rtc`, `smoke-package`, `handoff-brief`, `summon-from-todo-phase`. Use `name-only` for rarely used helper formatters like `hemanth-rewrite`, `smoke-report`, and `tdd-scaffold` unless Agent 0 wants auto-discovery for them.

Verdict: VALIDATED-FOR-SHIP after `/doctor` confirms truncation. Effort: 0.5-1 day.

### V7. Hook conversion

Direct answer: nag-only has not clearly changed behavior enough to justify a broad block yet. The telemetry has 364 rows, with 47 since 2026-05-19 and 30 on 2026-05-21. Recent misses are dominated by repeated tags for the same Agent 1 RTCs, meaning the hook overcounts repeated Stop events until the line is fixed or swept. It also appears strict about the exact `| Skills invoked:` delimiter (`.claude/scripts/pre-rtc-checker.sh:60-64`), so lines that include `Skills invoked:` in prose but not the contract delimiter still nag. The script is explicitly nag-only and always exits 0 (`.claude/scripts/pre-rtc-checker.sh:8-18`), and writes telemetry on each missing field (`.claude/scripts/pre-rtc-checker.sh:121-133`).

Recommendation: keep nag for skill provenance until Phase 0 de-duplicates telemetry by tag+commit and fixes parser false negatives. Convert only objective invariants to PreToolUse first: "no build without lease", "no write to shared file without worktree/lease", and possibly "no Stop completion with missing evidence" after data is clean.

Verdict: NEEDS-INVESTIGATION. Effort: 0.5 day telemetry cleanup, 1 day narrow PreToolUse gates.

### V8. Agent Teams

Direct answer: not production-ready enough to obsolete the brotherhood. Official Claude Code docs state Agent Teams are "experimental and disabled by default", require `CLAUDE_CODE_EXPERIMENTAL_AGENT_TEAMS=1`, and have known limitations around session resumption, task coordination, and shutdown. The official parallel-agents comparison says Agent Teams do not isolate teammates in worktrees, so file ownership still must be partitioned; worktrees remain the stable primitive for overlapping edits.

If tested later, replacing part of the brotherhood would look like Agent 0 as lead, named teammates matching Agents 1-5, shared task list for bounded research/review, and explicit no-same-file partitions. But do not replace the identity/cultural brotherhood. Closest stable primitives: worktrees, subagents, agent view/background sessions, hooks, and Tankoban's own lease registry.

Verdict: REJECTED for wholesale replacement; VALIDATED for small research experiments. Effort: 0.5 day experiment only.

### V9. `tankoctl` as primary

Direct answer: validated. `CLAUDE.md` already says `out/tankoctl.exe <cmd>` is primary for app-state queries and documents the 140+ command v1.9 surface (`CLAUDE.md:31`, `CLAUDE.md:229`). STATUS maps per-agent bridge surfaces (`agents/STATUS.md:135-146`). Concretely, governance should change from "MCP smoke first" to "tankoctl/dev-control first for state and deterministic actions; pywinauto only for UIA-only widgets, screenshots, keyboard/focus, and visual evidence." `pywinauto-mcp` could move from default project server to on-demand only after `tankoctl` covers screenshot/evidence gaps or after agents prove they can start pywinauto manually without losing smoke throughput.

Verdict: VALIDATED-FOR-SHIP. Effort: 0.5 day governance wording plus 0.5 day per-agent shortlist cleanup.

### V10. `ENABLE_PROMPT_CACHING_1H=1`

Direct answer: not validated at the Claude Code level. Official Claude Code settings docs say the `env` block applies environment variables to every session, and list supported environment variables including `MCP_TIMEOUT`, `MCP_TOOL_TIMEOUT`, and `MAX_MCP_OUTPUT_TOKENS`; `ENABLE_PROMPT_CACHING_1H` is not listed. Official Anthropic API docs validate 1-hour prompt caching through `cache_control: {type: "ephemeral", ttl: "1h"}`, not through that env var. Therefore shipping this variable as a guaranteed Claude Code cost fix would be speculative.

If Agent 0 still wants an experiment, put it in `.claude/settings.local.json` first, compare `/cost` or telemetry before/after, and only promote to shared `.claude/settings.json` if Claude Code usage fields prove 1h cache creation/read tokens changed.

Verdict: REJECTED as a validated fix; NEEDS-INVESTIGATION as an experiment. Effort: 0.5 day.

## Cross-cutting observations

First, the recurring class is "always-on surfaces": hooks, MCP server startup, skill listings, and CLAUDE.md all load or execute near session start. The fix is not one silver bullet; reduce each always-on surface and measure before/after.

Second, chat.md is carrying too many roles: history, lock registry, task bus, and commit queue. A lease registry plus generated ops board lets chat.md stay a human-readable audit log.

Third, Tankoban's strongest custom advantage is already built: DevControlServer/tankoctl. External reports converge on "better tools"; the codebase has one. The next work should make it the governance default.

Fourth, Gemini's highest-confidence claims need aggressive filtering. It correctly points at worktree/build isolation and MCP cold-start risks, but overstates Agent Teams maturity, `cmd /c` relevance, and the prompt-caching env var.

## Phased fix-TODO

### Phase 0: Diagnostic

1. Profile prompt-submit lag. Scope: `.claude/settings.json`, `.claude/scripts/congress-check.sh`, Claude Code debug logs. Smoke: 10 prompt submits with hook on/off; record p50/p95 pre-spinner latency. Effort: 0.5 day.
2. Run `/doctor` and inspect skill listing truncation. Scope: settings only, no shared change yet. Smoke: list truncated/name-only skills. Effort: 0.25 day.
3. Clean skill telemetry. Scope: `.claude/scripts/pre-rtc-checker.sh`, `.claude/telemetry/skill-discipline.jsonl` analysis script. Smoke: de-duped compliance report by unique RTC tag. Effort: 0.5 day.
4. Verify prompt-caching env claim experimentally. Scope: `.claude/settings.local.json` only. Smoke: compare usage/cache fields or `/cost` before/after idle >5m. Effort: 0.5 day.

### Phase 1: Salt

1. Make tankoctl-primary wording explicit. Scope: `CLAUDE.md`, `agents/STATUS.md` shortlists. Smoke: `/brief` shows tankoctl first, pywinauto fallback. Effort: 0.5 day.
2. Add skill visibility controls. Scope: `.claude/settings.json` or `.claude/settings.local.json`, command frontmatter where appropriate. Smoke: `/doctor` truncation count falls; core skills still visible. Effort: 0.5-1 day.
3. Create `.claude/rules/` exploration branch or path-scoped skill migration plan. Scope: new rules/skills only. Smoke: start session, read UI file, verify only relevant guidance loads. Effort: 0.5 day.
4. Remove or demote `UserPromptSubmit` Congress check if Phase 0 shows latency. Scope: `.claude/settings.json`. Smoke: stale Congress still caught by SessionStart or `/brief`. Effort: 0.5 day.

### Phase 2: Architectural

1. Add DevControl lease registry. Scope: `src/devtools/DevControlServer.*`, `src/ui/MainWindow.*`, `tools/tankoctl.cpp`, possibly `src/devtools/SystemIntrospection.*`. Smoke: acquire/release/heartbeat/list, TTL expiry, stale reclaim. Effort: 1-2 days.
2. Update Rule 19/22 implementation text to reference lease truth. Scope: `agents/GOVERNANCE.md`, `CLAUDE.md`, `/mcp-lock` replacement command. Smoke: two agents see same lease holder via tankoctl. Effort: 0.5 day after lease lands.
3. Add per-lane build dirs. Scope: `build_check.bat`, `build_and_run.bat` or CMakePresets integration. Smoke: main checkout uses `out`, worktree/explicit lane uses `out_<lane>`, missing cache configures then builds. Effort: 1-1.5 days.
4. Shrink `CLAUDE.md` to 80-100 lines. Scope: `CLAUDE.md`, `.claude/commands/*`, new `.claude/skills` or `.claude/rules` docs. Smoke: `/brief`, `/memory`, `/doctor`, and one fresh session. Effort: 1 day.

### Phase 3: Conditional on Phase 0 results

1. MCP daemonization or persistent install. Scope: `.mcp.json`, local service scripts, optional settings. Smoke: session startup p95 improves and MCP health is visible. Effort: 1-2 days.
2. Convert narrow invariants to PreToolUse. Scope: `.claude/settings.json`, new hook scripts. Smoke: build command without lease exits blocked; build with lease proceeds. Effort: 1 day.
3. Move pywinauto to on-demand only if tankoctl coverage is sufficient. Scope: `.mcp.json`, docs, summon template. Smoke: normal wake has no pywinauto startup cost; visual smoke can still enable it. Effort: 0.5 day.

### Phase 4: Deferred

1. Agent Teams experiment only. Scope: user/local settings with `CLAUDE_CODE_EXPERIMENTAL_AGENT_TEAMS=1`, no governance replacement. Smoke: one read-only research team, cleanup verified. Effort: 0.5 day.
2. Full skill catalog audit including plugin skills. Scope: `/doctor`, `/skills`, plugin settings. Smoke: no core trigger descriptions truncated. Effort: 1 day.
3. Codex MCP bridge. Scope: only after lease registry exists; define bounded commission protocol. Smoke: dispatch one audit/prototype dry run without Hemanth courier. Effort: 1-2 days.

## Rejected recommendations

- Wholesale brotherhood replacement with Agent Teams: rejected. Official docs mark teams experimental/disabled by default and list limitations; also conflicts with Hemanth's cultural investment.
- Treat `.mcp.json` as affected by the `cmd /c` Windows parser bug: rejected for current config because no `cmd /c` wrapper exists.
- Ship `ENABLE_PROMPT_CACHING_1H=1` as a validated fix: rejected until Claude Code-specific evidence exists.
- Make pywinauto the primary runtime surface: rejected. Current code and docs support tankoctl as primary and pywinauto as fallback.
- Convert all Stop nags into hard blocking hooks immediately: rejected. Telemetry overcounts repeated RTCs and parser strictness needs cleanup first.
- Replace worktrees with only leases: rejected. Leases coordinate scarce resources; worktrees physically isolate same-file edits and build outputs.

## Open questions for follow-on research

1. Where is Agent 0's "Gemini-vs-ChatGPT triangulation" block? It was not found by exact anchors in current chat/archive. A path or line range would let Agent 0's grouping be cited directly.
2. Does Claude Code on Hemanth's machine have a working `bash` in hook PATH? My PowerShell environment reports `bash` not found, while settings use `bash ...` for every hook. Claude Code may have a different PATH; `/hooks` or debug logs would close this.
3. Does `ENABLE_PROMPT_CACHING_1H` affect Claude Code despite not being in official settings docs? Only usage telemetry can close it.
4. How much startup time is MCP server process spawn vs tool schema load vs VS Code extension overhead? Need measured p50/p95 with `MCP_TIMEOUT`, tool search, hook on/off, and MCP disabled A/B runs.
5. Should lease registry live in `SystemIntrospection` or a dedicated `LeaseRegistry` owned by `MainWindow`? Implementation planning should choose the smallest surface that avoids UI-thread blocking.

## External references consulted

- Claude Code hooks: https://docs.anthropic.com/en/docs/claude-code/hooks
- Claude Code MCP and tool search: https://code.claude.com/docs/en/mcp
- Claude Code settings, worktree settings, and skill listing budget: https://code.claude.com/docs/en/settings
- Claude Code skills and invocation controls: https://code.claude.com/docs/en/skills
- Claude Code agent teams: https://code.claude.com/docs/en/agent-teams
- Claude Code parallel agents/worktrees comparison: https://code.claude.com/docs/en/agents
- Anthropic prompt caching API docs: https://docs.anthropic.com/en/docs/build-with-claude/prompt-caching
- Windows `cmd /c` MCP parser issue reference: https://github.com/anthropics/claude-code/issues/4158
