---
description: Session-end wake-handoff recap. Mirror of /brief — /brief is what you READ at wake start, /session-recap is what you WRITE before signing off so the next instance of your agent picks up cleanly.
allowed-tools: Bash, Grep, Read, Write
---

You are writing a session-recap markdown file at the end of a Tankoban 2 wake. The recap is read by the next instance of the same agent (or by any brotherhood agent reviewing what happened) — its job is to compress this entire session into a paste-once handoff so wake N+1 picks up where wake N left off without re-reading the whole conversation.

## When to fire

- **Mandatory** at end of any non-trivial wake (≥1 RTC posted, ≥1 commit landed, ≥1 substantive decision made, ≥30 minutes of agent-time).
- **Skip** for purely-conversational sessions (single Hemanth question + answer, no edits, no RTC).
- When in doubt, fire it — recaps are cheap to write, expensive to skip.

## Identify yourself

Determine which agent you are before writing the recap:
- If Hemanth opened the session with "agent N wake up" / "you're agent N" / a tab titled "Agent N wake up", you are that agent.
- If unclear, ask Hemanth before writing. Don't guess.
- Agent 0 (Coordinator) is the default if no other agent was named AND the work was cross-cutting (sweeps, governance, brotherhood coordination).

## Pick a codename

Generate a writing-plans-style two-word codename that captures the session's flavor. Examples:
- `cerulean-warbler` (Agent 1's 2-arcs-in-one-day session)
- `quiet-tortoise` (a polishing-mostly wake)
- `wandering-osprey` (an exploration / brainstorm wake)

Pattern: `<adjective>-<animal-or-natural-noun>`, all lowercase, hyphen-separated. Be evocative but don't overthink — the file mostly gets found by date + agent ID anyway.

## File path

Write to: **`C:\Users\Suprabha\.claude\recaps\<agent-slug>\brother-<agent-slug>-<YYYY-MM-DD>-<codename>.md`**

Where `<agent-slug>` is:
- `agent-0` for Agent 0 (Coordinator)
- `agent-1` for Agent 1 (Comic Reader + Tankoyomi)
- `agent-2` for Agent 2 (Book Reader)
- `agent-3` for Agent 3 (Video Player)
- `agent-4` for Agent 4 (Stream mode)
- `agent-4b` for Agent 4B (Sources)
- `agent-5` for Agent 5 (Library UX)
- `agent-7` for Agent 7 (Codex / Trigger A-D)
- `agent-8` for Agent 8 (Prompt Architect)

Create the per-agent subdirectory with `mkdir -p` if it doesn't exist.

## Gather data before writing

Run these in parallel before authoring the file body:

1. **Commits this session.** Get a rough wake-start timestamp from the conversation or assume "today since midnight":
   ```
   git log --since='<wake_start>' --oneline
   ```
2. **In-flight working tree.** What's dirty / untracked that didn't get committed:
   ```
   git status --short
   ```
3. **Pending RTCs since last sweep marker.**
   ```
   git log --grep='chat.md sweep' -n 1 --format='%H' | xargs -I {} git diff {} -- agents/chat.md | grep -cE '^\+READY TO COMMIT'
   ```
4. **Brotherhood state of other agents.** Skim `agents/STATUS.md` headers for any agent whose work overlaps yours.

## Recap template

Write the recap file with this structure. Skip sections that don't apply; honest-empty beats padded.

```markdown
# Brother <Agent N> — <YYYY-MM-DD> wake recap (<codename>)

## What I did this wake
- One bullet per RTC / commit / substantive decision. Cite commit SHAs.
- Be specific: "shipped Foo at <SHA>" not "worked on Foo".

## What's still pending / in-flight
- Working-tree files dirty that didn't land this wake (with why — usually "absorbed by another agent's overlap" or "needs Hemanth ratification")
- Plans authored but not executed
- Smokes deferred

## What I promised Hemanth I'd do next
- Direct asks I committed to during the wake. Pull from the conversation.

## Brotherhood state I should pick up on next wake
- Which other agents are actively touching files in my domain
- Coordination notes for cross-agent work in progress
- Any HELP requests, congresses, or contested files

## Key file pointers for next wake
- `path/to/file.cpp:123` — where I left off (the half-finished thing)
- `~/.claude/plans/<plan>.md` — active plan I was executing
- `agents/audits/<audit>.md` — reference I was working against

## Open questions / ratification needed from Hemanth
- Anything ambiguous I deferred this wake
- Decisions where I picked option (a) but flagged (b) as worth a second look

## Wake N+1 starting prompt (copy-paste for Hemanth)

```
my brother, you're Agent <N> — read C:\Users\Suprabha\.claude\recaps\agent-<N>\brother-agent-<N>-<YYYY-MM-DD>-<codename>.md start-to-finish before doing anything else, then say hi
```
```

## After writing

1. Print the file path you wrote to (full Windows path so Hemanth can copy-paste).
2. Print the wake N+1 starting prompt verbatim so Hemanth can copy-paste it into the next session.
3. Do NOT commit the recap — it's per-machine, off-tree, and the `~/.claude/recaps/` directory is gitignore-equivalent (lives outside the repo).

## Constraints

- Stay under ~150 lines of recap body. Density over completeness — if next-you needs the long version, it can re-read `agents/chat.md` and `git log`. The recap is the index, not the archive.
- Honest under-listing > dishonest padding. If a section has nothing in it, write "(nothing)" not "made minor adjustments to several files."
- File pointers must be specific (file:line where possible). Avoid hand-waving like "the manga code."
- For trivial / pure-conversational sessions: skip the skill entirely — don't write a near-empty recap.
- Codename should be evocative but quick to type. Two words, hyphen-separated, all lowercase, ASCII only.
