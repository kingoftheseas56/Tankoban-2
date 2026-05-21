---
description: Scaffold a new memory file (user / feedback / project / reference) with proper frontmatter. Use when capturing a load-bearing learning from the current session.
---

You are scaffolding a new memory file for Tankoban 2.

**Arguments:**
- `<type>` — required, one of: `user` / `feedback` / `project` / `reference`
- `<name>` — required, kebab-case slug (e.g. `agent4-stream-ipc-pattern`)

**Memory dir location:** `~/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/`

**Procedure:**

1. **Validate type:** must be one of the 4 types per CLAUDE.md auto-memory section. If invalid, abort with the valid list.

2. **Construct filename:**
   - Pattern: `<type>_<name>.md`
   - Example: `feedback_<name>.md`, `project_<name>.md`, `reference_<name>.md`, `user_<name>.md`
   - Absolute path: `~/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/<filename>`

3. **Check for existing file:** if a file with this name already exists, abort with `File already exists — use Edit to update, not /memory-write to create.`

4. **Scaffold the file content with correct frontmatter:**

   For type=`user`:
   ```markdown
   ---
   name: <name>
   description: <ONE-LINE description — fill in>
   metadata:
     type: user
   ---

   <Memory body — what does Claude need to know about the user? See CLAUDE.md auto-memory section for user-memory guidance.>
   ```

   For type=`feedback`:
   ```markdown
   ---
   name: <name>
   description: <ONE-LINE description — fill in>
   metadata:
     type: feedback
   ---

   <Rule or principle — lead with the rule itself.>

   **Why:** <reason — often a past incident, strong preference, or empirical pattern>

   **How to apply:** <when/where this guidance kicks in; which agents/domains affected>

   Related: [[related-memory-slug]] (link liberally; broken links are OK markers).
   ```

   For type=`project`:
   ```markdown
   ---
   name: <name>
   description: <ONE-LINE description — fill in>
   metadata:
     type: project
   ---

   <Fact or decision — lead with what changed/happened.>

   **Why:** <motivation — often a deadline, stakeholder ask, or empirical finding>

   **How to apply:** <how this should shape future agent decisions>

   Related: [[related-memory-slug]]
   ```

   For type=`reference`:
   ```markdown
   ---
   name: <name>
   description: <ONE-LINE description — fill in>
   metadata:
     type: reference
   ---

   <External resource pointer — what's at the URL/path, how to use it.>

   Use when: <conditions that should trigger this reference>
   ```

5. **Write the file** to the memory dir.

6. **Add MEMORY.md index entry.** Read MEMORY.md, find the right section (Workflow / Player domain / Stream domain / Sources / MCP / etc), and insert:
   ```
   - [<filename>.md](<filename>.md) — <ONE-LINE summary>
   ```
   Section placement heuristic:
   - `feedback_*` → typically `Workflow / governance` section (or domain-specific if obvious)
   - `project_*` → domain-specific section based on the body content
   - `reference_*` → typically near top with other reference entries
   - `user_*` → near top with other user entries

7. **Print confirmation** with the file path + which MEMORY.md section the index entry landed in.

**Quality gates:**
- Frontmatter is valid YAML (no tabs, name/description/metadata fields present)
- Slug matches `<type>_<name>.md` exactly
- MEMORY.md index entry stays under ~150 chars total
- File body has the **Why:** + **How to apply:** lines for feedback/project types
- Related: line included (even if empty) to remind future-self to link

**Examples:**

For `/memory-write feedback "stream-ipc-batch-pattern"`:
- Creates `feedback_stream-ipc-batch-pattern.md` with the feedback template
- Adds MEMORY.md entry under `## Stream domain` (or `## Workflow / governance` if cross-cutting)
- Prints `Created: ~/.claude/projects/.../memory/feedback_stream-ipc-batch-pattern.md`
