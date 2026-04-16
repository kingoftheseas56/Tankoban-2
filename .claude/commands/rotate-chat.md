---
description: Archive oldest chat.md entries to agents/chat_archive/ preserving preamble + tail
allowed-tools: Bash, Read, Write
argument-hint: "[--keep=500]"
---

You are running the Tankoban 2 chat.md rotation. This is the periodic maintenance that keeps `agents/chat.md` from growing unbounded.

**Arguments:** $ARGUMENTS — pass `--keep=N` to override the default tail size (default 500).

**Procedure:**

1. **Sanity check size.** Run `wc -l agents/chat.md`. Save as `TOTAL`. If `TOTAL - KEEP < 1000`, abort with message: "chat.md is only <TOTAL> lines; rotating <TOTAL - KEEP> lines isn't worth a commit (target ≥ 1000 lines archived per rotation)."

2. **Critical pre-check — open threads in the about-to-be-archived range.** Slice lines 8 through `TOTAL - KEEP` and grep for unresolved markers:
   ```
   sed -n '8,'$(($TOTAL - $KEEP))'p' agents/chat.md | grep -E '^(READY TO COMMIT|REQUEST PROTOTYPE|REQUEST AUDIT)'
   ```
   If any matches surface, print them and **abort**. Tell the user: "Resolve these open threads first (commit the work, write the prototype, answer the audit), or shift the split point. Re-invoke `/rotate-chat --keep=<larger N>` to pull more lines back into live."

3. **Compute slice ranges.**
   - Preamble: lines 1–7 (header + format spec + first separator).
   - Archive body: lines 8 through `TOTAL - KEEP`.
   - Live tail: last `KEEP` lines.

4. **Build the archive file.** Output path: `agents/chat_archive/$(date +%Y-%m-%d)_chat_lines_8-<END>.md` where `<END> = TOTAL - KEEP`.
   Content:
   - Standalone-readable header (date, line range, pointers to live files — model on existing archives in `chat_archive/`).
   - Duplicate of preamble (lines 1–7).
   - Archive body (lines 8 through `TOTAL - KEEP`).

5. **Build the new live chat.md.** Content:
   - Preamble (lines 1–7).
   - 15–25 line "Archive pointer" pinned block — date, archive filename + clickable link, 5–10 bullet "what shipped since last rotation" recap (read recent chat.md tail + git log to compose this; do not copy-paste the previous archive pointer block, it'll be stale).
   - Retained tail (last `KEEP` lines).

6. **Verify.** `wc -l agents/chat.md agents/chat_archive/<new>.md`. Sanity-check totals.

7. **Commit.** Stage `agents/chat.md` + the new archive file + (if needed) `agents/chat_archive/README.md` (update the Archive Index table at the bottom). Commit message:
   ```
   [Agent 0, governance]: rotate chat.md (lines 8-<END> archived; <TOTAL> -> <KEEP+~25> lines live)
   ```

**Trade-offs / known limits:**
- Cuts mid-message in the archive boundary (since the slice is line-based, not section-based). Acceptable — both halves remain searchable, and the archive pointer block tells future readers where to look.
- Does NOT handle the case where the preamble itself has been edited mid-rotation. If lines 1–7 differ from the canonical preamble, abort and ask the user to fix the preamble first.
- Do NOT run mid-session. Only at session-end as part of Agent 0's maintenance pass per the File Hygiene section in `agents/GOVERNANCE.md`.
