You are proposing MEMORY.md archive candidates for Tankoban 2.

**Arguments:** none (always interactive)

**Memory dir location:** `~/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/`

**Procedure:**

1. **Read MEMORY.md** (the index file in the memory dir).

2. **For each pointer entry in MEMORY.md, gather metrics:**
   - Pointer entry shape: `- [<slug>.md](<slug>.md) — <one-line hook>`
   - Read the target file's frontmatter to get `name` slug + `metadata.type`
   - Compute LAST-CITED frequency:
     ```
     grep -c <slug> agents/chat.md
     grep -rc <slug> agents/chat_archive/
     ```
     Sum the counts. Newer chat.md counts more (weight 2x) vs archived (weight 1x).
   - Compute AGE:
     ```
     git log -1 --format=%cs <memory-dir>/<slug>.md
     ```
     Days since last edit.

3. **Score each entry:**
   - Score = (age in days × 1) - (weighted citation count × 2)
   - Higher score = better archive candidate

4. **Sort by score descending** and propose the top 10 archive candidates. Present as a numbered list with:
   - Slug
   - Last-edit date
   - Citation count (chat + archive split)
   - Computed score
   - 1-line memory description from MEMORY.md

5. **Wait for user confirmation.** Format:
   ```
   Top archive candidates (highest score first):

   1. [12] feedback_foo.md — last edit 2026-04-15 (35d ago) — cited 1x chat / 0x archive — "Foo behavior pattern"
   2. [10] project_bar.md — last edit 2026-04-20 (30d ago) — cited 0x chat / 2x archive — "Bar shipped"
   ...

   Which to archive? Reply with comma-separated numbers (e.g. "1,3,5") or "none" to abort.
   ```

6. **On confirmation:**
   - `git mv` selected memory files to `memory/_archive/<original-name>.md`
   - Update MEMORY.md index: remove the moved entries
   - Append entry to `memory/_archive/INDEX.md`:
     ```
     - [<slug>.md](<slug>.md) — archived <YYYY-MM-DD>, last-active <last-cited-date>, score <score>
     ```
   - Print summary of moves

7. **Do NOT auto-archive without confirmation.** Per spec §5 Q1 default: propose-with-confirmation.

**Quality gates:**
- Never moves a file the user didn't confirm
- INDEX.md entry preserves the original 1-line description from MEMORY.md
- MEMORY.md index entries are removed cleanly (no leftover blank lines or section breaks broken)
- Recent (≤14 days since edit) entries are NEVER proposed regardless of citation count

**Examples:**

For a 28-entry MEMORY.md where 3 entries are aged + uncited:
```
Top archive candidates:
1. [22] feedback_old_xy_workaround.md — last edit 2026-04-12 (37d ago) — cited 0x chat / 0x archive
2. [18] project_closed_arc.md — last edit 2026-04-18 (31d ago) — cited 1x chat / 0x archive
3. [15] reference_unused_tool.md — last edit 2026-04-22 (27d ago) — cited 0x chat / 1x archive

Which to archive? Reply with comma-separated numbers (e.g. "1,3") or "none" to abort.
```

User responds `1,3` → script moves the two files, updates MEMORY.md + INDEX.md, prints confirmation.
