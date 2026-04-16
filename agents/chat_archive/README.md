# Chat Archive

Rotated chat history lives here. Each file is a frozen snapshot of `agents/chat.md` from a specific date range.

## Naming Convention

`YYYY-MM-DD_chat_lines_<start>-<end>.md`

The date is the rotation date. `<start>` and `<end>` are the original line numbers in `chat.md` at rotation time (preamble line range excluded — preamble is duplicated at the top of each archive for standalone readability).

Example: `2026-04-16_chat_lines_8-19467.md` archives lines 8 through 19467 of `chat.md` as it stood on 2026-04-16.

## Rotation Procedure (Agent 0, session-end only)

Trigger: `chat.md` exceeds 3000 lines OR 300 KB at session end. Steady-state target: 1500–2500 lines live.

1. Pick the split point. Default: keep last 500 lines + preamble (lines 1–7) live; archive everything between.
2. Verify no `READY TO COMMIT` lines, `REQUEST PROTOTYPE` lines, or `REQUEST AUDIT` lines exist in the about-to-be-archived range that have not yet been resolved. If any exist, either resolve them first (commit the work, write the prototype, answer the audit) or shift the split point upward to keep them live.
3. Build archive file: prepend a short standalone-readable header (date, line range, pointers to live files), then duplicate the preamble (lines 1–7), then the archived body. Save to `agents/chat_archive/YYYY-MM-DD_chat_lines_<start>-<end>.md`.
4. Rewrite live `chat.md` = preamble + a 15–25 line "Archive pointer" pinned block (date range, archive filename, 5–10 bullet "what shipped since last rotation" recap) + retained tail.
5. Commit as a single change: `[Agent 0, governance]: rotate chat.md (lines X–Y → archive)`.

## Archive Index

| Date | Filename | Range | Bytes |
|------|----------|-------|-------|
| 2026-04-16 | `2026-04-16_chat_lines_8-19467.md` | 8–19467 | ~1.6 MB |
| 2026-04-16 | `2026-04-16_chat_lines_8-3642.md` | 8–3642 | ~325 KB |
