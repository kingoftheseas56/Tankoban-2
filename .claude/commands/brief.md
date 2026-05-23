---
description: Live brotherhood dashboard — STATUS per agent + chat tail + commit backlog + Congress state. On-demand verification of CLAUDE.md.
allowed-tools: Bash, Grep, Read
---

You are producing the live state dashboard for Tankoban 2. Output a markdown digest that mirrors the format of the `## 30-Second State Dashboard` block in `CLAUDE.md`, but computed from the actual current state of the repo. Use this when CLAUDE.md may be stale (Agent 0 forgot to bump after a phase) or for ground-truth verification.

**Token discipline (load-bearing — read FIRST):** Do NOT use the `Read` tool on `agents/STATUS.md` or `agents/chat.md`. Both files are 25-50k tokens and the brief only needs specific extracted lines. Use targeted `grep` / `head` / `tail` / `wc` / `git log` / `git diff` commands only. Reading either file fully wastes ~75k tokens per `/brief` wake. This is the chat.md tail-only + STATUS.md grep-only enforcement per `agents/audits/token_cost_audit_2026-05-22.md` findings #2 + #3 (Codex audit 2026-05-22).

**Procedure:**

1. **Active agents.** Extract from `agents/STATUS.md` via targeted `grep` — DO NOT Read the file (it's ~49k tokens, and you only need the structured field lines):
   - Agent list: `grep '^## Agent' agents/STATUS.md`
   - Status lines: `grep -n '^Status:' agents/STATUS.md` (use first sentence only when surfacing)
   - Blockers: `grep -n '^Blockers:' agents/STATUS.md` (any value other than "None" warrants surfacing)
   - Last session dates: `grep -n '^Last session:' agents/STATUS.md` — flag dates older than 7 days vs today.

   If you need the full body of one specific agent's section (rare — only for deep verification), use `awk '/^## Agent N/,/^## Agent /' agents/STATUS.md` to slice just that block rather than reading the whole file.

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

5. **Last build / smoke.** Run `tail -n 80 agents/chat.md | grep -iE '(smoke|green|pass|\[perf\])' | tail -1`. Surface the matched line as a one-liner. DO NOT Read `agents/chat.md` via the Read tool — it's ~26k tokens; the bash one-liner above returns exactly what you need (~50-150 tokens).

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
