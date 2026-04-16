---
description: Sweep READY TO COMMIT lines from chat.md and commit one per line, with chat.md sweep marker at end
allowed-tools: Bash, Grep, Read, Task
argument-hint: "[--dry-run]"
---

You are running the Tankoban 2 commit sweep. This is the automation that batches per-batch commits from agent posts in `agents/chat.md`.

**Arguments:** $ARGUMENTS — pass `--dry-run` to preview without committing.

**Procedure:**

1. **Resolve cutoff.** Find the last commit whose subject contains `chat.md sweep`:
   ```
   git log --grep='chat.md sweep' -n 1 --format='%H'
   ```
   This commit's blob of `agents/chat.md` is the cutoff baseline. Get the blob:
   ```
   git rev-parse <SWEEP_SHA>:agents/chat.md
   ```
   Use this blob to diff against the current `agents/chat.md`:
   ```
   git diff <SWEEP_BLOB> -- agents/chat.md
   ```
   The added lines (lines beginning with `+`) are the new content since last sweep. (Using blob diff makes this rotation-safe — line numbers shift on rotation, blob content does not.)

2. **Parse `READY TO COMMIT` lines.** From the added content, extract every line matching this regex (anchored to start, after the `+`):
   ```
   ^READY TO COMMIT — \[([^\]]+)\]:\s+(.+?)\s+\|\s+files:\s+(.+?)\s*$
   ```
   Capture three groups per match:
   - **tag** — `Agent N, Batch X.Y` style identifier
   - **message** — one-line commit message
   - **files** — comma-separated list of file paths

3. **For each parsed line, in order:**
   - Split files on `,` and trim whitespace.
   - Verify each file exists in the working tree AND has a non-empty diff (`git diff --name-only HEAD -- <file>`). If a listed file is missing or clean, skip the entire line and warn — do not partial-commit.
   - Stage with `git add <file1> <file2> ...` (quoted, never `git add -A`).
   - Commit with: `git commit -m "[<tag>]: <message>"` via heredoc.
   - On commit failure (pre-commit hook rejection, etc.): halt the sweep, report the failed line, leave subsequent lines pending. Never `--amend`, never `--no-verify`.

4. **Final marker commit.** After the per-line commits, stage `agents/chat.md` (which contains all the swept posts) and commit:
   ```
   [Agent 0, chat.md sweep]: <N> posts (<comma-separated unique tag list>)
   ```
   This advances the cutoff for the next sweep run.

5. **Skip the marker if N=0.** If there were zero pending `READY TO COMMIT` lines, do not create an empty marker commit.

**`--dry-run` mode:** report the parsed lines + planned commits + planned marker, but do not stage or commit anything.

**Report format:** at the end, print:
```
Sweep complete (or: dry-run preview):
- N lines parsed
- M commits created (or: planned)
- 1 marker commit (or: planned)
- 0 failures (or: list of failed lines with reason)
```

If you'd like to delegate the parse/stage/commit logic to a focused sub-agent rather than running it inline, invoke the `commit-sweeper` agent (defined in `.claude/agents/commit-sweeper.md`) with the same `--dry-run` argument.

**Hard rules:**
- NEVER bypass git hooks (no `--no-verify`).
- NEVER force-push or amend.
- NEVER stage files that aren't in the parsed `files:` list.
- If the cutoff commit can't be found (no prior `chat.md sweep` commit ever), abort with a message asking the user to run a manual baseline commit.
