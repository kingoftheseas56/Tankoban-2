# `.claude/rules/` migration plan — 2026-05-21

By Agent 0, overnight wake. Authored to close Codex audit Phase 1.3 — *"Create `.claude/rules/` exploration branch or path-scoped skill migration plan. Scope: new rules/skills only."*

This is a **plan document**, not the migration itself. No rule files are created until Hemanth ratifies the scope + sequencing.

## Goal

Shrink the always-loaded session-start surface (currently dominated by CLAUDE.md at 231 lines) by migrating procedural guidance that only applies to certain file paths into `.claude/rules/*.md` with path globs. Result: a fresh session in a stream-domain task does not load comic-domain guidance and vice versa.

## How `.claude/rules/` works (per official docs)

Claude Code's `.claude/rules/` directory holds path-scoped rule files. Each file may declare globs in its frontmatter (e.g. `paths: ["src/ui/pages/comics/**"]`); the rule body is included in context **only** when the agent reads a file matching the glob in the current turn. This is the same mechanism that powers nested CLAUDE.md files under subtrees (per Anthropic memory docs).

This contrasts with the top-level CLAUDE.md, which loads at every session start regardless of what the session touches.

## Current always-loaded surface inventory

(Line counts from `CLAUDE.md` after the Phase 1.5 wording edits this wake.)

| Section in CLAUDE.md | Approximate lines | Path-scopable? | Target rule file |
|---|---|---|---|
| HEMANTH'S ROLE block | ~20 | NO — applies universally | stays in CLAUDE.md |
| Which MCP, when (Tool priority) | ~10 | PARTIAL — tankoctl applies always; tool catalog could path-scope | stays in CLAUDE.md (concise after Phase 1.5 edit) |
| Build-command contract | ~5 | NO — applies whenever any agent runs a build | stays in CLAUDE.md |
| 30-Second State Dashboard ("As of:" block) | ~30 | NO — situational state, not rule-shaped; addressed by Phase 2 CLAUDE.md shrink (Codex V5) | stays / migrates to `/brief` per Phase 2 |
| Active agents list | ~15 | NO — universal identity / ownership map | stays |
| Required Skills & Protocols (tiered list) | ~50 | PARTIAL — Tier-1 universal; Tier-2/3 trigger-shaped, candidate for rule files | Tier 1 stays; Tier 2/3 → `.claude/rules/skill-triggers-<domain>.md` |
| Active Fix TODOs table | ~30 | NO — situational state | migrates to generated ops board per Phase 2 V5 |
| Recent Rotations & Maintenance | ~12 | NO — historical | migrates to `/repo-health` per V5 |
| Build Quick Reference | ~15 | YES — applies whenever an agent runs a build, but most contents are command pointers (Hemanth-irrelevant) | migrate to `.claude/rules/build-commands.md` activating on `**/CMakeLists.txt`, `**/*.cpp`, `**/*.h`, `build_*.bat` |

## Proposed migration matrix (concrete rule files)

| Rule file | Path globs | Body content (sourced from) |
|---|---|---|
| `.claude/rules/player-domain.md` | `src/ui/player/**`, `src/core/player/**`, `native_sidecar/**`, `MAKE_MPV_*.md` | Agent 3 ownership note + sidecar-IPC + libplacebo notes + memory pointers (project_native_d3d11, project_player_perf, ...) |
| `.claude/rules/stream-domain.md` | `src/ui/pages/videos/**`, `src/core/stream/**`, `src/core/torrent/**`, `TANKORENT_*.md`, `STREAM_*.md` | Agent 4 ownership + stream-server pivot context + Tankorent foundation vision pointer |
| `.claude/rules/comics-domain.md` | `src/ui/pages/comics/**`, `src/core/manga/**`, `COMIC_READER_*.md`, `COMICS_*.md` | Agent 1 ownership + Tankoyomi merger arc context + comic-reader Mihon/YACReader reference |
| `.claude/rules/books-domain.md` | `src/ui/pages/books/**`, `src/core/book/**`, `BOOK_*.md`, `TANKOLIBRARY_*.md` | Agent 2 ownership + BOOKS_STREMIO_PIVOT context + EdgeTTS notes |
| `.claude/rules/library-theme-domain.md` | `src/ui/library/**`, `src/ui/theme/**`, `Theme.*`, `THEME_*.md` | Agent 5 ownership + theme-system context + Tankoban-Max parity references |
| `.claude/rules/governance-edits.md` | `agents/GOVERNANCE.md`, `agents/CONTRACTS.md`, `agents/STATUS.md`, `agents/VERSIONS.md`, `CLAUDE.md` | Rule 12 header-touch discipline + Rule 13 phase-boundary commit discipline + sweep-marker convention |
| `.claude/rules/build-commands.md` | `**/CMakeLists.txt`, `**/*.cpp`, `**/*.h`, `build_*.bat`, `native_sidecar/**` | build_and_run vs build_check usage + Rule 1 (taskkill before rebuild) + Rule 17 (stop-tankoban after smoke) |
| `.claude/rules/mcp-smoke.md` | `.mcp.json`, `**/*smoke*.md`, `agents/audits/**` | Rule 19 MCP LANE LOCK + smoke discipline rules + tankoctl-first priority |
| `.claude/rules/chat-md-edits.md` | `agents/chat.md` | RTC contracts-v3 format + skill provenance requirement + pre-rtc-checker.sh nag behavior + commit-sweep flow |

## Estimated CLAUDE.md size after migration

Pre-migration (post-Phase-1.5): 231 lines.
After moving the 6 path-scopable bands above: estimated **~140-150 lines** (still above Codex's 80-100 target — full shrink also requires Phase 2 V5 work: dashboard → `/brief`, TODO table → ops board, rotations → `/repo-health`, skill catalog → metadata, Agent 8 persona → its own file).

The rules migration is a **prerequisite enabler** for the larger Phase 2 V5 CLAUDE.md shrink, not a replacement for it.

## Risks

1. **Rule activation discovery** — agents may not realize a path-scoped rule loaded mid-turn. Smoke procedure must confirm `.claude/rules/<file>` content actually appears in context when the path glob matches.
2. **Glob drift** — adding new files outside the existing path patterns could leave them unscoped. Mitigation: maintain `_default.md` rule with broad fallback for truly cross-cutting guidance, plus document the glob conventions in `agents/GOVERNANCE.md`.
3. **Existing nested CLAUDE.md files** — if any subdirectory has its own CLAUDE.md (none currently, but check `agents/`), interaction with `.claude/rules/*.md` needs validation.
4. **Agent identity confusion** — Agent X reading a file owned by Agent Y is normal (cross-domain audits). Rule bodies must say "Agent Y owns this domain; if you're not Agent Y, your edit is a cross-domain change requiring..." not assume the reader IS that domain's agent.

## Smoke procedure (for the actual migration, post-ratification)

1. Author one rule file (`.claude/rules/comics-domain.md`) as MVP.
2. Move the Agent 1 + comic-reader content out of CLAUDE.md into that file.
3. Start a fresh Claude Code session.
4. Confirm session-start context does NOT include Agent 1 / comics guidance.
5. Have the agent Read `src/ui/pages/comics/ComicsSeriesView.cpp`.
6. Verify a system-reminder or context-injection appears that loads `.claude/rules/comics-domain.md` body.
7. If verified GREEN, migrate the remaining 8 rule files in parallel (each is independent — Trigger E candidate).
8. Re-measure CLAUDE.md line count + post a smoke evidence row.

## Sequence for Hemanth's morning ratification

| Step | Decision needed | Effort |
|---|---|---|
| 1 | Approve the path-glob → rule-file mapping above, or ask for adjustments | ~5 min review |
| 2 | Ratify MVP rule pick (default: `comics-domain.md` since Agent 1 is most-modified file owner this week) | ~1 min |
| 3 | Greenlight the smoke-procedure run on the live CLAUDE.md | ~1 min |
| 4 | After GREEN smoke, dispatch remaining 8 rule files (Trigger E worktree fanout per Rule 21) | ~3-5 hours wall-clock under parent supervision |
| 5 | Re-measure CLAUDE.md + post sweep marker | ~10 min |

## Honest caveats

- This plan assumes `.claude/rules/` is available in the Hemanth-installed Claude Code version. Need to confirm via `/doctor` or version check that path-scoped rules work as documented; if not, defer until Claude Code release lands.
- Phase 2 V5 (CLAUDE.md shrink to 80-100 lines) is the bigger ship; this rules migration is a **structural prerequisite**, not a substitute. Even with perfect rule-file path-scoping, the dashboard / TODO table / rotations / skill catalog all need to move per V5 to hit the 80-100 target.
- I am not creating ANY rule files in this overnight wake — only this plan doc. The first rule file ships only after Hemanth's morning ratification.

## What's already done

- Codex V5 + V6 + V9 audit findings consumed; this plan is the V5 + 1.3 deliverable.
- CLAUDE.md tool-priority wording tightened this wake (Phase 1.5).

## What's blocked on Hemanth

- Migration matrix sign-off
- MVP rule pick + smoke greenlight
- Trigger E worktree fanout dispatch
- Sequencing relative to Phase 2 V5 CLAUDE.md shrink
