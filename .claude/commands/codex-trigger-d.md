---
description: Package a Codex Trigger D handoff prompt for scoped src/ implementation work. Use when commissioning Codex for a focused code change.
---

You are packaging a Codex Trigger D handoff for Tankoban 2.

**Arguments:**
- `<spec-file>` — required, path to a spec markdown file (relative to repo root)
- `<attribution-agent>` — required, the agent commissioning Codex (e.g. `Agent 4`)
- `<scope-summary>` — required, one-line summary for the RTC tag

**Procedure:**

1. **Read the spec file.** Validate it exists; abort if missing with `Spec file not found: <spec-file>`.

2. **Extract spec body context** — the first paragraph from the Strategic Intent section, plus the §Files list (Codex needs to know exactly what to touch).

3. **Identify relevant memory pointers** — scan the spec for `[[<slug>]]` links or explicit "Memory references:" sections. Include `project_codex_substrate_live.md` always (canonical Trigger D pattern).

4. **Construct the Codex prompt block** following the canonical Trigger D pattern (see `project_codex_substrate_live.md` memory + the v1.1/v1.2 bridge ships at chat.md for live examples):

```
From <attribution-agent> — Trigger D (scoped src/ implementation).

Target spec: <spec-file>

Context: <auto-extract first paragraph from the spec's Strategic Intent or top-of-file>

YOUR TASK:
1. Read the full spec.
2. Implement the changes per the spec body in the listed files.
3. Match the spec's §Files list exactly — no edits outside the listed paths.
4. build_check.bat after each major change; commit only when build_check.bat = BUILD OK.

CONSTRAINTS — DO NOT:
- Expand scope beyond the spec
- Touch memory files or CLAUDE.md (unless spec explicitly lists them)
- Use --no-verify or skip build verification
- Introduce non-ASCII characters in protocol-anchored lines (RTC, MCP LOCK; ASCII Rule 16)

CONSTRAINTS — DO:
- Preserve all existing code outside your diff
- Keep additive changes (schema versioning rule: additive within v1.x = non-breaking)
- Match the existing code style (C++ MSVC2022 + Qt6, indent + brace patterns from neighboring files)
- ASCII-only emissions in any chat.md or protocol-parsed output

MEMORY POINTERS (already loaded for any project Codex session):
- project_codex_substrate_live.md (Trigger A/B/C/D pattern + Rule 20)
- project_dev_control_bridge.md (if bridge work)
- feedback_fix_todo_authoring_shape.md (if fix-TODO work)
- <any spec-specific memory pointers extracted from spec body>

OUTPUT EXPECTATIONS:
1. Files modified per spec §Files list (no scope creep)
2. build_check.bat = BUILD OK before commit
3. scoped git diff --check clean (no whitespace warnings)
4. ASCII-clean check on any modified Markdown/text files
5. RTC posted to agents/chat.md with attribution "[<attribution-agent> (Codex), <scope-summary>]"

VERIFICATION CHECKLIST FOR AGENT POST-CODEX:
- Build green (build_check.bat = BUILD OK)
- Spec §Files list matches actual diff scope (no scope creep)
- No memory or CLAUDE.md touched (unless spec listed)
- ASCII protocol anchors in any emitted chat.md lines
- RTC posted with correct attribution

When done, post an RTC in agents/chat.md with attribution "[<attribution-agent> (Codex), <scope-summary>]" plus your standard Codex Trigger D verification block (build_check OK, scoped diff --check, ASCII-clean check on touched files).
```

5. **Print the prompt block to stdout** so the user can copy-paste into a Codex tab (or fire via `mcp__codex__codex` tool).

**Quality gates:**
- Spec file path is validated against working tree (abort if missing)
- Attribution agent and scope summary are non-empty
- Memory pointers section reflects actual spec content (not generic boilerplate)
- Output is self-contained (Codex doesn't need to ask follow-up questions to start work)

**Examples:**

For `/codex-trigger-d docs/superpowers/specs/2026-05-19-bridge-v1.3-books-commission.md "Agent 2" "v1.3 books-side bridge layer"`:
emits a complete Codex prompt block referencing the v1.3 spec, with Agent 2 attribution and "v1.3 books-side bridge layer" as the RTC scope.
