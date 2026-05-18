You are generating a mid-wake handoff brief for Tankoban 2.

**Arguments:**
- `<target-agent>` — required, identifier (e.g. `Agent 4`, `Agent 7`)
- `<scope>` — required, one-line description of what's being handed off

**Procedure:**

1. **Capture current session state:**
   - Active TODOs being worked: scan working-tree for `*_FIX_TODO.md` files modified this session OR uncommitted RTCs naming TODOs
   - Files currently dirty: `git status --short` filtered to relevant scope
   - Pending RTCs: scan `agents/chat.md` tail since last sweep marker (`git log --grep='chat.md sweep' -n 1 --format=%H`, then tail since)
   - Last 3 chat.md posts: tail -100 + extract last 3 `## Agent N - ...` blocks
   - Current MCP lock state: invoke `/mcp-lock peek` logic OR scan chat.md tail for unmatched `MCP LOCK` / `MCP LOCK RELEASED` pair
   - Active arc(s): scan recent chat.md for arc identifiers (e.g. `THEATRE_DOWNLOAD_OVERHAUL`, `MANGAUPDATES_FALLBACK`, `SKILL_AUGMENTATION_ARC`)
   - Recent commits in this session: `git log --since="$(date -d 'today 00:00')" --format='%h %s'`

2. **Construct the handoff brief block:**

```markdown
# Handoff brief → <target-agent>: <scope>

**From:** <originating agent (auto-detect)>
**Wake date:** <today YYYY-MM-DD>
**Reason for handoff:** <one-line context>

## Files dirty in working tree
<git status --short output, scoped>

## Active TODOs being worked
- <TODO name + current phase cursor>

## Uncommitted RTCs since last sweep marker
<list of recent RTCs in chat.md tail>

## MCP lock state
<HELD by [<holder>] since <ts> / FREE>

## Active arcs
<list of arc identifiers seen in recent chat.md>

## Recent commits this wake (last 10)
<git log output>

## What I need <target-agent> to do
<fill in: specific ask — verb + object + verification gate>

## Relevant memory pointers
- <list of memory slugs the target should read first>

## Specific files to read for context
- <list with file paths>
```

3. **Print the brief to stdout** as a pastable block. Target agent's next wake prompt should paste this brief alongside their own instructions.

**Quality gates:**
- Brief is self-contained (target agent doesn't need to ask for additional context)
- File lists are real (from `git status`, not fabricated)
- Memory pointers reference actual files in the memory dir
- "What I need <target-agent> to do" section is concrete (verb + object + verification gate)
- Single chat.md paste, not multiple back-and-forth

**Difference from /session-recap:**
- `/session-recap` = end-of-wake recap written by an agent to brief the NEXT instance of themselves (off-tree at `~/.claude/recaps/<agent-slug>/`)
- `/handoff-brief` = mid-wake handoff to a DIFFERENT agent within the same wake (inline output, not saved to disk)

**Examples:**

For `/handoff-brief "Agent 4" "TANKORENT_CINEMETA Task 12 smoke verification"`:
emits a brief covering current dirty files (Agent 0 sweep state), pending RTCs from Agent 4's prior tasks, MCP lock state (currently held by Agent 4), arc identifier `TANKORENT_CINEMETA_PACK_MAPPING`, and the specific Task 12 ask.
