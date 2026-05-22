---
description: Archive oldest chat.md entries to agents/chat_archive/ preserving preamble + tail
allowed-tools: Bash, Read, Write
argument-hint: "[--keep=500]"
---

You are running the Tankoban 2 chat.md rotation. This is the periodic maintenance that keeps `agents/chat.md` from growing unbounded.

**Arguments:** $ARGUMENTS — pass `--keep=N` to override the default tail size (default 500).

**Procedure:**

1. **Sanity check size.** Run `wc -l agents/chat.md`. Save as `TOTAL`. If `TOTAL - KEEP < 1000`, abort with message: "chat.md is only <TOTAL> lines; rotating <TOTAL - KEEP> lines isn't worth a commit (target ≥ 1000 lines archived per rotation)."

2. **Critical pre-check — open threads in the about-to-be-archived range, post-last-sweep-marker only.** The brotherhood writes a sweep marker line into chat.md after each rotation (format: `[Agent 0, chat.md sweep marker — ...]`). RTC lines BELOW the marker are post-rotation open threads worth gating on; RTC lines ABOVE the marker are sealed history (already swept or already in HEAD per the post-hoc close-out pattern — they SHOULD archive freely). So the pre-check scans only from the latest sweep marker forward to `TOTAL - KEEP`:
   ```
   SCAN_FROM=$(grep -n '^\[Agent 0, chat\.md sweep marker' agents/chat.md | tail -1 | cut -d: -f1)
   # Fallback: if no marker exists yet (pre-rotation-7 legacy), scan from line 8.
   if [ -z "$SCAN_FROM" ]; then SCAN_FROM=8; fi
   sed -n "${SCAN_FROM},$((TOTAL - KEEP))p" agents/chat.md | grep -E '^(READY TO COMMIT|REQUEST PROTOTYPE|REQUEST AUDIT)'
   ```
   If any matches surface AND the matches are in the post-marker range, print them and **abort**. Tell the user: "Resolve these open threads first (commit the work, write the prototype, answer the audit), or shift the split point. Re-invoke `/rotate-chat --keep=<larger N>` to pull more lines back into live."

   **Why marker-aware:** the brotherhood's normal pattern is post-hoc narrative RTCs (the agent commits per-task, THEN writes a chat.md RTC line summarizing what shipped). After the next /commit-sweep, the work is verifiably in HEAD but the RTC lines REMAIN as historical record. A naive `grep -E '^READY TO COMMIT'` from line 8 trips on every historical RTC even months after sweep, hard-blocking rotation. The marker-aware scan trusts the marker as the "everything above is sealed" boundary, matching how the brotherhood actually operates. Introduced rotation 7 (2026-05-22) after the gate's strict semantics blocked routine rotation despite 421 RTCs being post-hoc narratives all behind shipped HEAD.

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
   - **Sweep marker line** (single line, grep-able by step-2 pre-check on next rotation): `[Agent 0, chat.md sweep marker — <ISO timestamp> — <N> lines archived into chat_archive/<archive filename>; everything below this line is post-rotation activity. /rotate-chat pre-check uses this marker as the open-thread scan cutoff.]`
   - One `---` separator.
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
