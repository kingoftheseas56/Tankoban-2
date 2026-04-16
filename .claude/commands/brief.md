---
description: Live brotherhood dashboard — STATUS per agent + chat tail + commit backlog + Congress state. On-demand verification of CLAUDE.md.
allowed-tools: Bash, Grep, Read
---

You are producing the live state dashboard for Tankoban 2. Output a markdown digest that mirrors the format of the `## 30-Second State Dashboard` block in `CLAUDE.md`, but computed from the actual current state of the repo. Use this when CLAUDE.md may be stale (Agent 0 forgot to bump after a phase) or for ground-truth verification.

**Procedure:**

1. **Active agents.** For each `## Agent N` block in `agents/STATUS.md`, extract:
   - The `Status:` line (first sentence only).
   - Whether `Blockers:` line is non-empty (anything other than "None").
   - The `Last session:` date — flag if older than 7 days from today.

2. **READY TO COMMIT backlog.** Find the last `chat.md sweep` commit:
   ```
   git log --grep='chat.md sweep' -n 1 --format='%H'
   ```
   Get its blob of `agents/chat.md`:
   ```
   git rev-parse <SWEEP_SHA>:agents/chat.md
   ```
   Diff against current chat.md and count `^READY TO COMMIT —` lines among the additions:
   ```
   git diff <SWEEP_BLOB> -- agents/chat.md | grep -cE '^\+READY TO COMMIT —'
   ```
   Show the count + the last 3 tag prefixes (e.g. `Agent 4B HYGIENE 2.1`, `Agent 3 PERF 3.A`).

3. **Open congresses.** Read first 30 lines of `agents/CONGRESS.md`. Surface the `STATUS:` value. If body contains `ratified`, `APPROVES`, `Final Word`, or `Execute` AND status is still `OPEN`, print a `[STALE — needs archive]` warning.

4. **Open HELP.** Read first 30 lines of `agents/HELP.md`. Surface the STATUS line.

5. **Last build / smoke.** Skim chat.md tail for the most recent line containing `smoke`, `green`, `PASS`, or `[PERF]`. Surface a one-liner.

6. **Chat.md size.** `wc -l agents/chat.md`. If > 3000, flag with `[ROTATION DUE]` per File Hygiene rule.

7. **TODO heartbeats.** `git log --since='7 days ago' --name-only -- '*_TODO.md'` to surface which TODO files saw recent activity.

8. **Recent commits.** Last 8 entries of `git log --oneline -8`.

**Output format:** ~50 lines of markdown. Headers + bullets only — no narrative paragraphs. Match this skeleton:

```
# Brotherhood Live Brief — <today's date>

## Agents
- A0 (Coordinator) — <status> — last session <date>
- A1 (Comic Reader) — <status> — ...
[etc.]

## Pending commits
<N> READY TO COMMIT lines uncommitted (last 3 tags: ...)

## Congress
STATUS: <value> [warn if stale]

## HELP
STATUS: <value>

## Last activity
- Last smoke: <one-liner>
- Chat.md: <N> lines [warn if > 3000]
- TODO heartbeats (last 7d): <list>

## Recent commits
<git log oneline output>
```

**Constraints:**
- Stay under ~50 lines of output.
- Compute from filesystem + git state — do not read CLAUDE.md (you're verifying against it, not echoing it).
- Run all the cheap commands. If any fails, note it inline rather than aborting.
