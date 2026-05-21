---
description: Scaffold a contracts-v3 RTC line for agents/chat.md. Use after non-trivial work is ready for Agent 0 sweep.
---

You are scaffolding a contracts-v3 RTC line for the agent to paste into agents/chat.md.

**Arguments:**
- `<tag>` — required, e.g. `Agent 4, Bug X round-2 fix`
- `<message>` — required, one-line commit subject describing the work

**Procedure:**

1. **Detect dirty files since HEAD.** Run:
   ```
   git diff --name-only HEAD
   git status --porcelain
   ```
   Filter to files that have actual diffs OR are staged additions. Skip ignored paths.

2. **Detect skills invoked this session.** Run:
   ```
   bash .claude/scripts/skill-provenance-detect.sh --candidates-only
   ```
   Capture the comma-separated list. If empty or only whitespace, fall back to `/build-verify, /superpowers:verification-before-completion`.

3. **Classify trivial vs non-trivial.** Non-trivial = ≥1 file under `src/` or `native_sidecar/src/`, OR ≥30 LOC changed cumulative against HEAD. Trivial RTCs may omit the `Skills invoked:` field; non-trivial must include it.

4. **Build the scaffolded line.** Use ASCII delimiters per Rule 16:
   ```
   READY TO COMMIT - [<tag>]: <message> | Skills invoked: [<skill-list>] | files: <file1>, <file2>, ...
   ```

5. **Validate against sweeper regex.** The line must match:
   ```
   ^READY TO COMMIT [—-] \[([^\]]+)\]:\s+(.+?)(?:\s+\|\s+Skills invoked:\s+\[([^\]]*)\])?\s+\|\s+files:\s+(.+?)\s*$
   ```
   If it fails to match, fix it (most common cause: unescaped pipe in message body, or empty files list).

6. **Emit the line to stdout.** Print the scaffolded RTC line. Do NOT append to chat.md automatically — the agent decides when to post.

**Quality gates:**
- File list reflects actual working-tree changes (no fabrications)
- Skills field only present for non-trivial RTCs
- ASCII delimiters only
- No trailing whitespace
- Single line (no embedded newlines in the RTC itself)

**Examples:**

For `/rtc "Agent 4, Bug 5 fix" "stop button color regression resolved"`:
```
READY TO COMMIT - [Agent 4, Bug 5 fix]: stop button color regression resolved | Skills invoked: [/build-verify, /superpowers:systematic-debugging] | files: src/ui/pages/StreamPage.cpp, agents/chat.md
```

For trivial RTC like `/rtc "Agent 0, docs typo" "fix typo in CLAUDE.md dashboard"`:
```
READY TO COMMIT - [Agent 0, docs typo]: fix typo in CLAUDE.md dashboard | files: CLAUDE.md
```
