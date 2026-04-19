---
description: Run scripts/repo-health.ps1 and report drift findings — tracked generated files, large source files, chat.md rotation threshold, STATUS vs CLAUDE drift, pending RTC lines, open CONGRESS / HELP.
allowed-tools: Bash
---

You are running the Tankoban 2 repo-health audit. This is a PowerShell script at `scripts/repo-health.ps1` that surfaces drift signals Agent 0 normally checks by hand at session boundaries. Closes Codex audit item #5 (2026-04-18).

**Procedure:**

1. Run the script via PowerShell. From the repo root:
   ```
   powershell -NoProfile -File scripts/repo-health.ps1
   ```
   Expected latency: <2 seconds.

2. Capture stdout + exit code.

3. Report the full output verbatim back to the user — the script formats it already (color-coded status prefixes [OK] / [INFO] / [WARN] / [FAIL], one line per check, indented detail for large files / tracked generated files when those checks trigger).

4. Summarize only if the script surfaces anything FAIL-level:
   - Tracked generated files in git index → recommend `git rm -r --cached <path>` + `.gitignore` update
   - Stale CONGRESS motion (OPEN + ratification keyword present) → remind Agent 0 of the same-session archive rule
   - chat.md past rotation trigger → suggest `/rotate-chat`
   - Large-file count exceeds 6 → flag as refactor-policy candidate (Codex item #6, still queued)

5. **Do NOT re-run the script, re-format the output, or add your own per-check commentary.** The script is the source of truth. Your role is to invoke it and surface findings without noise.

**Exit code contract:**
- 0 → all green or only INFO items
- 1 → one or more WARN (not blocking)
- 2 → one or more FAIL (fix before shipping)

**When to run this:**
- Session start, as a one-shot sanity check before picking up work.
- Before a big commit, to make sure nothing stale is about to get baked in.
- After a chat.md rotation or Congress close, to verify the cleanup actually landed.
- Anytime Hemanth asks "what's the repo state looking like?" as a lower-cost alternative to `/brief` (which runs richer agent-state queries).

**Constraints:**
- Script is ASCII-only by GOVERNANCE Rule 16 — do not "improve" it with em-dashes or section-signs; PS 5.1 can't read those without a BOM.
- Script checks only — it does NOT fix anything. If it flags a failure, the fix is separate work (usually Agent 0's).
