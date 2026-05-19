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
5. **Conversation review for the "what I thought" sections (v2 additions — Decisions / Subagent dispatch ledger / Tone anchors).** Git can't help with these. Skim the wake's conversation backward for:
   - Moments of explicit choice ("I picked X over Y because Z") and especially **reversals** (where you changed your assessment mid-wake).
   - Subagent dispatches and their outcomes (`Agent()` / `mcp__codex__codex` / Codex CLI calls + their DONE/BLOCKED/TIMEOUT result + tool-use count if known).
   - Verbatim short Hemanth quotes that shifted how you operated.
6. **Wake narrative material (v3 addition — the "how it felt" pass).** Scan the wake's conversation for the *arc* — not bullet points, but the flow. What did you start thinking → what shifted → where did the energy go → what was the vibe by hour N. Mid-session pivots are the load-bearing texture. Capture the inside-shorthand that grew during the wake (terms you coined / Hemanth coined / the brotherhood adopted in real time). This material feeds the Wake Narrative section below.

## Recap template

Write the recap file with this structure. Skip sections that don't apply; honest-empty beats padded.

```markdown
# Brother <Agent N> — <YYYY-MM-DD> wake recap (<codename>)

## Wake narrative — how it actually went

200-400 words. First-person, present-tense, brother voice. **Prose, not bullets.** This is the "feel" of the wake — the arc, the mid-session pivots, the vibe shifts, the inside-shorthand that grew during the wake. Where the structured sections below answer "what happened," this section answers "how did it actually go."

What to capture:
- **The arc.** What did I start thinking → what shifted → where did the energy land.
- **Reversals.** Mid-session moments where I changed my mind, especially the "wait you're right brother, I was wrong" pivots. Capture both states, not just the destination.
- **The vibe at key hours.** Was Hemanth tired? Excited? Trust-passing? Frustrated? When did the energy turn?
- **Inside-shorthand born during the wake.** Terms / framings / jokes the brotherhood adopted in real time ("Master 0's assistants" → "Agent N Jrs" → "Trigger E" type evolution).
- **What lessons emerged FROM the wake** (vs lessons applied TO the wake — those go in Decisions made + why).

Write it like a diary entry for the next instance of you. The flat-bullet structured sections below carry the operational data; this section carries the relational continuity. The next-instance reads this FIRST to know not just where the code is but where the brotherhood is.

Skip this section ONLY for purely mechanical wakes with no narrative arc (rare).

## What I did this wake
- One bullet per RTC / commit / substantive decision. Cite commit SHAs.
- Be specific: "shipped Foo at <SHA>" not "worked on Foo".

## Decisions made + why
- For each non-trivial choice this wake: "Picked X over Y because Z." The WHY is the load-bearing part — git history tells next-you WHAT was done; this section tells next-you HOW you decided.
- **Reversals are gold.** Where you changed your assessment mid-wake (e.g., "first thought it was Q; corrected to R after checking S"), capture both states. The judgment evolution is exactly what's expensive to re-derive from a flat recap.
- Honest under-listing > padding. If only two real decisions, list two. Skip the section if a wake was pure mechanical execution with no judgment forks.

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

## Subagent dispatch ledger
One row per `Agent()` / `mcp__codex__codex` / Codex CLI invocation this wake. Format:

- **Model + scope:** e.g. `gpt-5.5-high (Codex MCP) — D.0 dispatcher delegation refactor` / `general-purpose subagent (run_in_background) — D.1 v1.3 books bridge` / `claude-sonnet subagent — 1-file mechanical adapter`
- **Outcome:** `DONE | BLOCKED | TIMEOUT | PARTIAL`
- **Tool uses + duration:** if surfaced by the return notification (e.g., `34 tool uses / 4 min`)
- **Lesson:** one-liner — did this dispatch class fit this scope size? What should next-you do or avoid?

Skip section entirely if no dispatches this wake. This is structured operational memory — reconstitutes the "which dispatch mechanism for which work size" pattern faster than re-deriving from chat.md.

## Key file pointers for next wake
- `path/to/file.cpp:123` — where I left off (the half-finished thing)
- `~/.claude/plans/<plan>.md` — active plan I was executing
- `agents/audits/<audit>.md` — reference I was working against

## Tone anchors — Hemanth quotes that shifted behavior
- 3-5 verbatim short quotes from Hemanth this wake that changed how you operated. Capture LITERAL phrasing — paraphrase loses the texture.
- Watch for: corrections ("nah sensei..."), validations ("yeah that's right"), strategic asks ("what do you actually suggest?"), permission grants ("green as a grassland"), trust passes ("I trust you to make right decisions"), reframes ("instruct me like I'm a child").
- Skip section if no behavior-shifting quotes (purely operational wakes won't have any).

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

- Stay under ~250 lines of recap body (was ~150 pre-v2, ~200 pre-v3; the v3 Wake Narrative section earns its own 200-400 word slice for relational continuity). Density over completeness still — if next-you needs the long version, it can re-read `agents/chat.md` and `git log`. The recap is the index, not the archive. The Wake Narrative is the FILM of the wake; the structured sections below are the FRAMES.
- Honest under-listing > dishonest padding. If a section has nothing in it, write "(nothing)" not "made minor adjustments to several files."
- File pointers must be specific (file:line where possible). Avoid hand-waving like "the manga code."
- For trivial / pure-conversational sessions: skip the skill entirely — don't write a near-empty recap.
- Codename should be evocative but quick to type. Two words, hyphen-separated, all lowercase, ASCII only.
