You are managing the Rule 19 MCP LANE LOCK for Tankoban 2.

**Arguments:**
- `<action>` — required, one of: `claim` / `release` / `peek`
- `<reason>` — required for `claim`, e.g. `Agent 4, TANKORENT Phase 11 smoke`

**State location:** the lock is tracked as plain text lines in `agents/chat.md`. There is no separate lock file. This avoids state drift and lets every agent see the lock by reading the chat tail.

**Procedure:**

1. **Read recent chat.md tail** (last 200 lines):
   ```
   tail -200 agents/chat.md
   ```

2. **Find the most recent MCP LOCK / MCP LOCK RELEASED pair.** Use ASCII protocol anchors per Rule 16:
   ```
   ^MCP LOCK - \[(?<holder>[^\]]+)\]:
   ^MCP LOCK RELEASED - \[(?<holder>[^\]]+)\]:
   ```
   If the most recent LOCK has no matching RELEASE after it, the lock is HELD.

3. **For `peek`:** report the current lock state. Format:
   - HELD: `MCP LOCK currently HELD by <holder> since <timestamp>` (with reason if available)
   - FREE: `MCP LOCK currently FREE — no active claim`

4. **For `claim`:**
   - If HELD: refuse with `MCP LOCK CLAIM REFUSED — already held by <holder>`. Exit without modifying chat.md.
   - If FREE: emit the claim line to stdout with current timestamp:
     ```
     MCP LOCK - [<reason>]: <current ISO timestamp>
     ```
   - Do NOT auto-append to chat.md — the agent decides when to post (typically in their own RTC block).

5. **For `release`:**
   - If FREE (no active claim): warn `MCP LOCK already FREE — emitting RELEASED line for audit anyway`. Continue.
   - If HELD: emit the release line to stdout:
     ```
     MCP LOCK RELEASED - [<holder>]: <current ISO timestamp>
     ```
   - The `holder` value should match the most recent active LOCK line exactly.

**Examples:**

For `/mcp-lock claim "Agent 4, TANKORENT Phase 11 smoke"`:
- If free, emit:
  ```
  MCP LOCK - [Agent 4, TANKORENT Phase 11 smoke]: 2026-05-19T19:30:00Z
  ```
- If held by Agent 1, emit:
  ```
  MCP LOCK CLAIM REFUSED — already held by [Agent 1, COMICS_SOURCES_SIDEBAR smoke] since 2026-05-19T19:25:00Z
  ```

For `/mcp-lock release`:
```
MCP LOCK RELEASED - [Agent 4, TANKORENT Phase 11 smoke]: 2026-05-19T19:45:00Z
```

For `/mcp-lock peek`:
```
MCP LOCK currently HELD by [Agent 4, TANKORENT Phase 11 smoke] since 2026-05-19T19:30:00Z
```

**Quality gates:**
- Emitted lines use ASCII ` - ` per Rule 16 (NOT em-dash)
- Holder tag preserved exactly across claim → release
- Timestamps in ISO 8601 UTC
- Refuse-claim path never modifies chat.md state suggestions
