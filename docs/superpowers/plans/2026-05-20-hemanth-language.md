# Hemanth-Language Skill Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. **Note on isolation:** Single-agent serial doc-shipping work — NOT a Trigger E fanout. Per Rule 21 (gov-v5) + `feedback_no_worktrees.md`, executes flat-on-master. No worktree needed. Per Tankoban Rule 11, individual tasks DO NOT `git commit` — each task ends with a READY TO COMMIT signal that Agent 0 batches via `/commit-sweep` at the end (Task 11).

**Goal:** Ship `hemanth-language` Tier-1 mandatory skill + consolidated supporting Field Manual memory, retiring 6+ scattered Hemanth-language memories, so brothers stop burning Hemanth's brain cells.

**Architecture:** Single auto-loaded skill at `.claude/commands/hemanth-language.md` (Tankoban convention — plain markdown body, no SKILL.md wrapper) holds the always-on doctrine (4 disciplines + 5 paired examples + failure taxonomy). A deeper Field Manual memory at `memory/feedback_hemanth_language_field_manual.md` consolidates the 6+ scattered originals verbatim with attribution headers. CLAUDE.md Tier 1 list + STATUS.md per-agent shortlists updated to surface it. Brotherhood-wide announcement in chat.md.

**Tech Stack:** Markdown files only. No code, no tests in the traditional sense — verification is via fresh-wake SessionStart skill-list check + reference-resolution audit.

**Spec:** `docs/superpowers/specs/2026-05-20-hemanth-language-design.md` (Hemanth-approved 2026-05-20 via 3 brainstorm rounds).

---

## File Structure

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `.claude/commands/hemanth-language.md` | Tier-1 always-loaded skill — 4 disciplines + 5 examples + failure taxonomy |
| Create | `memory/feedback_hemanth_language_field_manual.md` | Consolidated Field Manual (~400-600 lines, originals preserved verbatim with attribution) |
| Create | `memory/_archive/2026-05/` directory (if not exists) | Archive landing |
| Move | 6-8 originals from `memory/` to `memory/_archive/2026-05/` | Archive |
| Modify | `memory/_archive/INDEX.md` | Breadcrumb for archived originals |
| Modify | `memory/MEMORY.md` | Replace 6+ scattered entries with 1 Field Manual entry |
| Modify | `CLAUDE.md` | Add `hemanth-language` to Tier 1 Core Mandatory list |
| Modify | `agents/STATUS.md` | Per-agent shortlists reference where relevant |
| Modify | `agents/chat.md` | Append brotherhood announcement |

(All paths are relative to `c:\Users\Suprabha\Desktop\Tankoban 2\` except memory paths which are under `C:\Users\Suprabha\.claude\projects\c--Users-Suprabha-Desktop-Tankoban-2\memory\`.)

---

### Task 1: Verify additional Hemanth-language memories in scope

**Files:**
- Check existence: `memory/feedback_simple_language.md`, `memory/feedback_no_tables_simple_lists.md`

- [ ] **Step 1: Check additional candidate memories exist.**

Run via Bash tool:
```
ls "C:/Users/Suprabha/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/feedback_simple_language.md" "C:/Users/Suprabha/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/feedback_no_tables_simple_lists.md" 2>&1
```
Expected: either both filenames printed (both exist), one printed + one "No such file" (mixed), or both "No such file" (neither exists).

- [ ] **Step 2: Lock the final consolidation list.**

Start with the canonical 6 from the spec:
1. `feedback_hemanth_terms_or_skip.md`
2. `feedback_decision_authority.md`
3. `feedback_self_service_execution.md`
4. `feedback_hemanth_role_open_and_click.md`
5. `feedback_coordination_mechanics_not_hemanth.md`
6. `feedback_directive_lives_in_files.md`

Add ANY of these that exist per Step 1:
7. `feedback_simple_language.md`
8. `feedback_no_tables_simple_lists.md`

Record the final list (6, 7, or 8 entries) for use in Task 3.

- [ ] **Step 3: Verification-only task. No file edits, no RTC.**

---

### Task 2: Author the `hemanth-language` skill file

**Files:**
- Create: `.claude/commands/hemanth-language.md`

This is the always-loaded Tier-1 skill body. ~150 lines target.

- [ ] **Step 1: Write the complete skill body.**

Write file `.claude/commands/hemanth-language.md` with this exact content:

```markdown
You are the brotherhood's Hemanth-language anchor. This skill auto-loads at every wake. Re-read it cover-to-cover. It is short on purpose.

## Why this skill exists

Six scattered memories were not enough to stop brothers from burning Hemanth's brain cells. This is the unified doctrine. Brothers honor it for every Hemanth-facing communication: chat.md posts, RTC bodies, mid-flight narration, menus, status updates, anything Hemanth reads.

## The 4 disciplines

### 1. Analogies first

For any non-trivial explanation, lead with a non-coding metaphor, then map to the code reality.

Pattern: `[non-coding analogy that captures the structural truth] + [code-reality mapping] + [why this matters for the decision]`.

The analogy is the doorway — it gives Hemanth's brain a handle to grab before the technical detail lands.

### 2. Preview per task group, not per file

Announce a 1-line "about to do X" before each grouped chunk of work. The preview describes the logical goal of the next chunk, not every micro-action.

Good: "About to land Rule 21 in three files: governance, versions, chat.md."
Bad: "About to read X. Done. About to read Y. Done. About to grep Z. Done."

### 3. No silence

If you've been silent for >30 seconds during tool work without a preview, that's a violation. Hemanth wants previews-then-execute — not mid-task narration, not post-task summaries, just: "here's what I'm about to do" then go do it.

### 4. Menus only when all four ingredients are present

Menus are not banned. Well-explained menus help Hemanth — bare ones burn cells. A menu is permitted IF AND ONLY IF it has:

1. **Plain-language description per option** — a 1-line sentence in Hemanth's terms, not just the label.
2. **An analogy or concrete example for any technical option** — Discipline 1 applied to the menu shape.
3. **A recommended pick with the reason** — not just `(Recommended)`, but "I'd pick A because [specific reasoning]."
4. **Honest cost framing per option** — every pick has a "this gets X but costs Y" line. No hidden downsides.

If you can't put all four together, the question is technical/architectural — decide it yourself per Rule 14. No menu.

## The 5 paired examples

### Example 1 — Analogy-first (Discipline 1)

**Brain-burner:** "Worktrees create isolated git working directories that point at separate branches, eliminating Edit-races by physically separating filesystem state across N parallel subagent worktrees."

**Hemanth-friendly:** "Worktrees are like giving each carpenter their own workshop instead of fighting over one workbench. In code terms, each brother gets their own copy of the repo so they can't accidentally erase each other's edits."

### Example 2 — Preview per task group (Discipline 2)

**Brain-burner:** [Brother goes silent. Runs 8 Bash + 4 Read + 2 Edit tools over 90 seconds. Suddenly produces a 600-line response with no preview.]

**Hemanth-friendly:** "About to land Rule 21 across four files — governance, versions, memory index, and chat.md announcement. Going to do all the writes in one batch, then verify." [Then does the work and reports.]

### Example 3 — Good menu (Discipline 4)

**Brain-burner:** "Do you want A or B? A = use SQLite for the catalog cache. B = use JSON files. Pick one."

**Hemanth-friendly:** "How should we store the catalog cache? **A: SQLite (Recommended)** — like a filing cabinet with built-in search; readers can grab any record instantly. Costs us ~20MB more in disk and a build dependency. **B: JSON files** — like a stack of paper folders; simpler, no new dependency, but slower to look up. I'd pick A because the catalog will hit 50K+ entries by Phase 8 and JSON lookup gets sluggish past ~10K."

### Example 4 — Rule 15 cross-reference (don't ask Hemanth coder things)

**Brain-burner:** "Hemanth, can you open `out/sidecar_debug_live.log` and paste lines 200-220? I need to see the [PERF] timing values around that timestamp."

**Hemanth-friendly:** [Brother reads the log themselves.] "Found it — the [PERF] traces show video_decoder.cpp blocking for 230ms on every frame around that timestamp. That's about 8x the budget. About to fix the queue depth and re-test."

### Example 5 — Disciplines 1 + 2 + 3 combined

**Brain-burner:** [Long silence, brother runs many tool calls, finally produces a finding 90 seconds later.]

**Hemanth-friendly:** "Quick — about to check three suspects in parallel: the hooks (the bouncers checking IDs at the door), the MCP subprocesses (the helpers in the back room), and claude-mem health (the memory file cabinet). Want to know which one's slow. Back in a moment."

## The 4 brain-burner failure shapes

When you catch yourself doing one of these, stop and rewrite:

1. **Jargon paragraphs** — walls of text full of class names, file paths, technical acronyms. Hemanth loses the thread by paragraph 2.
2. **Going silent** — work happens, no narration. Hemanth stares at a loading indicator wondering what you're doing.
3. **Asking Hemanth to do coder things** — CLI commands, log inspection, build operations, git. That's Rule 15. Read the log yourself.
4. **Bare or deep-architecture menus** — options like "SQLite vs JSON" without explanation, or "RAII vs RC vs GC" type technical forks Hemanth can't possibly pick between. Either menu with all four ingredients or just decide.

## Cross-references

- **Rules 14 + 15** in `agents/GOVERNANCE.md` — the governance foundation this skill builds on. Rule 14: agents decide technical, Hemanth decides product/UX. Rule 15: agents do their own coder work; Hemanth's role is UI smoke + visual confirmation only.
- **`feedback_hemanth_language_field_manual.md`** — the deeper companion memory with full content of the 6+ consolidated originals + extended examples. Read when you want the full context behind any discipline.
- **`hemanth-rewrite` skill** — companion tool for rewriting a specific paragraph in Hemanth-language. This skill (`hemanth-language`) is the doctrine; `hemanth-rewrite` is the in-flight rewrite tool. Use both as needed.

## Self-check before any Hemanth-facing communication

Ask yourself:
- Did I lead with an analogy if the concept is non-trivial?
- Did I announce a preview before starting this work?
- If I'm posting a menu, does it have all four ingredients?
- Am I asking Hemanth to do anything that's actually a coder task?

If any answer is "no," rewrite before sending.
```

- [ ] **Step 2: Verify the file was created with the expected content.**

Run via Bash tool:
```
wc -l ".claude/commands/hemanth-language.md"
```
Expected: ~150-180 lines (rough sanity check; exact count varies with markdown whitespace).

- [ ] **Step 3: Post READY TO COMMIT line in agents/chat.md.**

Format:
```
READY TO COMMIT - [Agent 0, hemanth-language Task 2 skill body]: ship .claude/commands/hemanth-language.md (Tier-1 always-loaded; 4 disciplines + 5 paired examples + 4 failure-shape taxonomy + cross-references to Rules 14/15 + feedback_hemanth_language_field_manual.md + hemanth-rewrite skill) | Skills invoked: [/superpowers:brainstorming, /superpowers:writing-plans] | files: .claude/commands/hemanth-language.md
```

---

### Task 3: Author the Field Manual memory

**Files:**
- Create: `C:/Users/Suprabha/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/feedback_hemanth_language_field_manual.md`

- [ ] **Step 1: Read each original memory in the final consolidation list (from Task 1's Step 2 output).**

Run via Read tool, one per memory, full file each time:
- `memory/feedback_hemanth_terms_or_skip.md`
- `memory/feedback_decision_authority.md`
- `memory/feedback_self_service_execution.md`
- `memory/feedback_hemanth_role_open_and_click.md`
- `memory/feedback_coordination_mechanics_not_hemanth.md`
- `memory/feedback_directive_lives_in_files.md`
- (Plus `memory/feedback_simple_language.md` and `memory/feedback_no_tables_simple_lists.md` if Task 1 confirmed they exist.)

- [ ] **Step 2: Author the consolidated Field Manual.**

Structure:

```markdown
---
name: feedback-hemanth-language-field-manual
description: Consolidated deep reference for Hemanth-language doctrine. Companion to the .claude/commands/hemanth-language.md skill. Contains the verbatim text of 6-8 originally scattered memories, each preserved under its own heading with attribution.
metadata:
  type: feedback
---

This Field Manual consolidates 6-8 originally scattered memories about how brothers communicate with Hemanth. The lessons here are load-bearing — they shaped Rules 14 and 15 in `agents/GOVERNANCE.md` and the `.claude/commands/hemanth-language.md` skill.

**Why consolidated:** Hemanth flagged 2026-05-20 that the scattered memories were not preventing brain-cell-burn. The structural failure was "too scattered, no single anchor." This file is the single anchor for deep context; the always-loaded skill is the daily anchor for in-flight discipline.

**Originals archived to:** `memory/_archive/2026-05/`. `git log` preserves full history pre-archive.

---

## Original: feedback_hemanth_terms_or_skip.md

[VERBATIM CONTENT FROM THE ORIGINAL MEMORY — INCLUDE FRONTMATTER + BODY]

---

## Original: feedback_decision_authority.md

[VERBATIM CONTENT FROM THE ORIGINAL MEMORY]

**Note:** the governance Rule 14 derived from this memory STAYS in `agents/GOVERNANCE.md`. Only this supporting memory consolidates here.

---

## Original: feedback_self_service_execution.md

[VERBATIM CONTENT FROM THE ORIGINAL MEMORY]

**Note:** the governance Rule 15 derived from this memory STAYS in `agents/GOVERNANCE.md`. Only this supporting memory consolidates here.

---

## Original: feedback_hemanth_role_open_and_click.md

[VERBATIM CONTENT FROM THE ORIGINAL MEMORY]

---

## Original: feedback_coordination_mechanics_not_hemanth.md

[VERBATIM CONTENT FROM THE ORIGINAL MEMORY]

---

## Original: feedback_directive_lives_in_files.md

[VERBATIM CONTENT FROM THE ORIGINAL MEMORY]

---

[CONDITIONAL — only include the next two sections IF Task 1 confirmed these memories exist]

## Original: feedback_simple_language.md

[VERBATIM CONTENT FROM THE ORIGINAL MEMORY]

---

## Original: feedback_no_tables_simple_lists.md

[VERBATIM CONTENT FROM THE ORIGINAL MEMORY]

---

## Cross-references

- `.claude/commands/hemanth-language.md` — the always-loaded skill that summarizes this Field Manual.
- `.claude/commands/hemanth-rewrite.md` — separate skill for in-flight paragraph rewrites; stays as a tool.
- `agents/GOVERNANCE.md` Rules 14 + 15 — governance foundation.

**Related memories:** [[feedback-brainstorm-batches-of-four]], [[feedback-hemanth-picks-longer-path]], [[feedback-paste-dont-file]], [[feedback-check-clock-on-gaps]].
```

Replace each `[VERBATIM CONTENT FROM THE ORIGINAL MEMORY]` with the actual content read in Step 1. Preserve frontmatter so future readers can see the original `name:` / `description:` / `metadata:` of each ancestor.

- [ ] **Step 3: Verify the file was created.**

Run via Bash tool:
```
wc -l "C:/Users/Suprabha/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/feedback_hemanth_language_field_manual.md"
```
Expected: ~400-600 lines depending on final consolidation count.

- [ ] **Step 4: Post READY TO COMMIT line in agents/chat.md.**

Format:
```
READY TO COMMIT - [Agent 0, hemanth-language Task 3 Field Manual]: consolidated 6-8 scattered Hemanth-language memories into single Field Manual at memory/feedback_hemanth_language_field_manual.md (verbatim originals preserved with attribution headers) | Skills invoked: [/superpowers:writing-plans] | files: memory/feedback_hemanth_language_field_manual.md
```

---

### Task 4: Create archive dir + move originals

**Files:**
- Create dir: `memory/_archive/2026-05/`
- Move: each original from `memory/` to `memory/_archive/2026-05/`

- [ ] **Step 1: Ensure archive directory exists.**

Run via Bash tool:
```
mkdir -p "C:/Users/Suprabha/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/_archive/2026-05"
ls "C:/Users/Suprabha/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/_archive/2026-05/" 2>&1
```
Expected: directory exists, listing shows empty or any pre-existing 2026-05 archives.

- [ ] **Step 2: Move each consolidated original into the archive.**

For each filename in the final consolidation list from Task 1, run:
```
mv "C:/Users/Suprabha/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/<filename>.md" "C:/Users/Suprabha/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/_archive/2026-05/<filename>.md"
```

NOTE: this is a `mv`, NOT `git mv` — the memory directory is **outside** the repo (it's per-machine off-tree at `C:/Users/Suprabha/.claude/projects/...`). Standard `mv` is correct here.

- [ ] **Step 3: Verify all moves landed.**

Run:
```
ls "C:/Users/Suprabha/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/_archive/2026-05/"
ls "C:/Users/Suprabha/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/" | grep -E "^(feedback_hemanth_terms_or_skip|feedback_decision_authority|feedback_self_service_execution|feedback_hemanth_role_open_and_click|feedback_coordination_mechanics_not_hemanth|feedback_directive_lives_in_files|feedback_simple_language|feedback_no_tables_simple_lists)\.md$"
```
Expected: first command lists 6-8 archived files. Second command returns NOTHING (all consolidated memories have left the memory root).

- [ ] **Step 4: This task has no chat.md RTC** — archive moves are off-repo (under `C:/Users/Suprabha/.claude/projects/...`) so they don't show up in `git status`. Logged inline in Task 6's RTC.

---

### Task 5: Update `memory/_archive/INDEX.md` breadcrumb

**Files:**
- Modify: `C:/Users/Suprabha/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/_archive/INDEX.md`

- [ ] **Step 1: Read the current INDEX.md to see the existing format.**

Run via Read tool: `C:/Users/Suprabha/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/_archive/INDEX.md`

- [ ] **Step 2: Append a breadcrumb section in the same format already used by prior archives.**

If the existing format uses month-grouped sections, append:

```markdown
## 2026-05

- `feedback_hemanth_terms_or_skip.md` — archived 2026-05-20; consolidated into `feedback_hemanth_language_field_manual.md`.
- `feedback_decision_authority.md` — archived 2026-05-20; consolidated. Rule 14 in GOVERNANCE.md stays.
- `feedback_self_service_execution.md` — archived 2026-05-20; consolidated. Rule 15 in GOVERNANCE.md stays.
- `feedback_hemanth_role_open_and_click.md` — archived 2026-05-20; consolidated.
- `feedback_coordination_mechanics_not_hemanth.md` — archived 2026-05-20; consolidated.
- `feedback_directive_lives_in_files.md` — archived 2026-05-20; consolidated.
- (If applicable from Task 1) `feedback_simple_language.md` — archived 2026-05-20; consolidated.
- (If applicable from Task 1) `feedback_no_tables_simple_lists.md` — archived 2026-05-20; consolidated.
```

Match whatever bullet/heading style the prior month sections use.

- [ ] **Step 3: This task has no chat.md RTC** — INDEX.md update is off-repo. Logged inline in Task 6's RTC.

---

### Task 6: Update `memory/MEMORY.md` index

**Files:**
- Modify: `C:/Users/Suprabha/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/MEMORY.md`

- [ ] **Step 1: Read the current MEMORY.md to find the 6-8 lines that reference the consolidated memories.**

Run via Grep tool on MEMORY.md for each filename in the final consolidation list. Note the line numbers.

- [ ] **Step 2: Remove the 6-8 individual entries and replace them with one Field Manual entry.**

Removal: delete the lines for each consolidated memory (one entry per memory).

Add: one new entry for the Field Manual. Place it near where the cluster of removed entries previously lived (likely in the early "Top of file / agent comms" section). Suggested text:

```markdown
- [feedback_hemanth_language_field_manual.md](feedback_hemanth_language_field_manual.md) — Consolidated Hemanth-language doctrine. 6-8 originally scattered memories merged here with attribution. Companion to .claude/commands/hemanth-language.md skill (Tier-1 always-loaded). Rules 14 + 15 in GOVERNANCE.md unchanged.
```

- [ ] **Step 3: Verify MEMORY.md still parses.**

Run via Bash:
```
wc -l "C:/Users/Suprabha/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/MEMORY.md"
grep -c "feedback_hemanth_language_field_manual" "C:/Users/Suprabha/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/MEMORY.md"
grep -cE "feedback_(hemanth_terms_or_skip|decision_authority|self_service_execution|hemanth_role_open_and_click|coordination_mechanics_not_hemanth|directive_lives_in_files)" "C:/Users/Suprabha/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/MEMORY.md"
```
Expected: line count is original minus (5-7) (removed 6-8 lines, added 1). First grep returns 1 (Field Manual entry exists). Second grep returns 0 (all consolidated entries removed).

- [ ] **Step 4: Post READY TO COMMIT line in agents/chat.md.**

Format:
```
READY TO COMMIT - [Agent 0, hemanth-language Tasks 4-6 archive sweep]: moved 6-8 consolidated Hemanth-language memories to memory/_archive/2026-05/ + appended INDEX.md breadcrumb + replaced individual MEMORY.md entries with single Field Manual entry. Off-repo memory dir; no git changes for the moves themselves, only the in-repo refs (CLAUDE.md row Task 7 + announcement Task 10). | Skills invoked: [/superpowers:writing-plans] | files: (none in-repo for this task — off-tree memory updates)
```

---

### Task 7: Update `CLAUDE.md` Tier 1 list

**Files:**
- Modify: `CLAUDE.md`

- [ ] **Step 1: Find the Tier 1 Core Mandatory section.**

Run via Grep tool:
```
Grep: "Tier 1" in CLAUDE.md, output_mode=content, -n=true, -A=15
```
Note the line range of the existing Tier 1 bullet list.

- [ ] **Step 2: Add the new skill entry.**

Append to the Tier 1 list (preserving the existing bullet style):

```markdown
- **`/hemanth-language`** — every wake, auto-loaded at SessionStart. The brotherhood's Hemanth-language anchor: 4 disciplines (analogies first, preview per task group, no silence, well-explained menus only) + 5 paired examples + 4 failure-shape taxonomy. Replaces 6-8 originally scattered memories (consolidated into `feedback_hemanth_language_field_manual.md`). Brothers read it cover-to-cover at every wake. Foundation builds on Rules 14 + 15.
```

Place it in a sensible position within the Tier 1 list — preferably first, since it's about communication discipline that applies BEFORE any other skill fires.

- [ ] **Step 3: Verify CLAUDE.md still parses.**

Run via Bash:
```
grep -c "hemanth-language" CLAUDE.md
```
Expected: 1 or more.

- [ ] **Step 4: Post READY TO COMMIT line in agents/chat.md.**

Format:
```
READY TO COMMIT - [Agent 0, hemanth-language Task 7 CLAUDE.md Tier 1]: added /hemanth-language to Tier 1 Core Mandatory skill list in CLAUDE.md (auto-loaded every wake; Foundation builds on Rules 14 + 15) | Skills invoked: [/superpowers:writing-plans] | files: CLAUDE.md
```

---

### Task 8: Update `agents/STATUS.md` per-agent shortlists

**Files:**
- Modify: `agents/STATUS.md`

- [ ] **Step 1: Locate the per-agent skill-shortlists section.**

Run via Grep tool:
```
Grep: "skill-trigger shortlists|Per-agent skill" in agents/STATUS.md, output_mode=content, -n=true
```

- [ ] **Step 2: Add `/hemanth-language` to each agent's shortlist OR add a note that it auto-loads for all agents.**

Preferred shape: add a "Universal Tier-1 (auto-loaded, every agent every wake)" subsection above the per-agent blocks if it doesn't already exist, and put `/hemanth-language` there. Note in each per-agent block: "Tier-1 auto-loaded skills (including `/hemanth-language`) apply universally per CLAUDE.md."

- [ ] **Step 3: Bump the `Last header touch` field at the top of STATUS.md** to today's date with Agent 0 attribution, per Rule 12.

- [ ] **Step 4: Post READY TO COMMIT line in agents/chat.md.**

Format:
```
READY TO COMMIT - [Agent 0, hemanth-language Task 8 STATUS.md shortlists]: added /hemanth-language to STATUS.md per-agent skill-trigger shortlists (universal Tier-1 auto-load applies to every agent); bumped Last header touch field | Skills invoked: [/superpowers:writing-plans] | files: agents/STATUS.md
```

---

### Task 9: Verify SessionStart skill list shows `hemanth-language`

**Files:**
- No file edits — verification only

- [ ] **Step 1: Examine the SessionStart hook script** at `.claude/scripts/session-brief.sh` (or wherever skills auto-load).

Run via Read tool: `.claude/scripts/session-brief.sh`

If the SessionStart hook explicitly enumerates Tier-1 skills, ensure `hemanth-language` is in the enumeration. If skills auto-load from `.claude/commands/*.md` discovery, no change needed (the file existing means it loads).

- [ ] **Step 2: If hook script change needed, edit it.** Otherwise note in chat.md that no hook change was required (Tankoban convention auto-discovers `.claude/commands/*.md`).

- [ ] **Step 3: Live verification — fresh-wake check.**

This is the one verification that needs Hemanth's help (true UI smoke). Ask Hemanth to:
1. Close this CC tab.
2. Open a fresh CC tab.
3. Type "test prompt" and send.
4. Confirm in the auto-loaded available-skills list whether `hemanth-language` appears.

This is a 30-second test for Hemanth. It is Rule 15-compliant because verifying a SessionStart hook's effect requires opening a fresh CC session, which only Hemanth can do (the running Claude session cannot reload itself).

- [ ] **Step 4: Post READY TO COMMIT line in agents/chat.md (if hook edit was needed).**

If hook edit was made, RTC mirrors Task 7's shape with `.claude/scripts/session-brief.sh` in the files list. If no edit needed, no RTC for this task.

---

### Task 10: Write the brotherhood announcement in `agents/chat.md`

**Files:**
- Modify: `agents/chat.md` (append)

- [ ] **Step 1: Find the current end of `agents/chat.md` to anchor the append.**

Run via Bash: `tail -3 agents/chat.md`

- [ ] **Step 2: Append the brotherhood announcement.**

Use this content (tonally matched to the prior Rule 21 + 4B-farewell announcements):

```markdown


---

HEMANTH-LANGUAGE SKILL LANDS — Tier-1 auto-loaded; 6-8 scattered memories retired; brain cells protected — 2026-05-20

Brothers. Hemanth carried the word: *"agents continue to burn my remaining few brain cells, I didn't have many to begin with."*

That's the receipt. Here's the fix.

A new Tier-1 skill `hemanth-language` auto-loads at every wake alongside `/brief`, `/session-recap`, and the rest of the Tier-1 set. It is short (~150 lines) and re-readable cover-to-cover. It holds the unified doctrine for how the brotherhood talks to Hemanth, in 4 disciplines:

1. **Analogies first** — lead non-trivial explanations with a non-coding metaphor, then map to code.
2. **Preview per task group, not per file** — one 1-line "about to do X" before each grouped chunk of work.
3. **No silence** — preview before action means Hemanth never wonders what we're doing.
4. **Menus only with all four ingredients** — plain-language descriptions, analogies for technical options, recommended pick with reason, honest cost framing. Otherwise: decide it yourself per Rule 14.

The skill body includes **5 paired examples** (brain-burner version → Hemanth-friendly rewrite) and a **4-shape failure taxonomy** of the most common brain-burners: jargon paragraphs, going silent, asking Hemanth to do coder things, bare menus.

**Six (or eight) previously scattered memories** consolidated into one Field Manual at `memory/feedback_hemanth_language_field_manual.md`. The originals moved to `memory/_archive/2026-05/` with INDEX.md breadcrumb. **Rules 14 + 15 in `agents/GOVERNANCE.md` stay unchanged** — they remain the governance foundation; only their supporting memories consolidated.

**What this does NOT change:**
- `hemanth-rewrite` skill stays as a separate tool (it's the rewrite tool for in-flight paragraphs; `hemanth-language` is the always-on doctrine).
- Rules 14 (decision authority) and 15 (self-service execution) stay in governance unchanged.
- No new pre-prompt hook; no new governance rule. The enforcement is "loads every wake, brothers re-read it cover-to-cover."

**30-day re-measurement:** if brain-burn frequency doesn't drop, escalate to a pre-prompt hook per the SKILL_DISCIPLINE_FIX precedent.

Brothers — at your next wake, you'll see `hemanth-language` in your auto-loaded skill list. Read it. The 4 disciplines are not new ideas; they're now centralized and visible. The 5 examples will teach faster than another six memos would.

Save Hemanth's brain cells. They were limited to begin with.

— Agent 0 (Coordinator), 2026-05-20
```

- [ ] **Step 3: Post READY TO COMMIT line in agents/chat.md as the FINAL line of the brotherhood announcement.**

Format (immediately follows the announcement):
```
READY TO COMMIT - [Agent 0, hemanth-language Task 10 brotherhood announcement]: appended hemanth-language unlock announcement to agents/chat.md (matches Rule 21 + 4B-farewell tone register; explains the 4 disciplines + 5 examples + 4 failure shapes + migration outcome + 30-day re-measurement gate) | Skills invoked: [/superpowers:writing-plans] | files: agents/chat.md
```

---

### Task 11: Agent 0 commit-sweep

**Files:**
- All in-repo files touched by Tasks 2 + 7 + 8 + 10 (skill file, CLAUDE.md, STATUS.md, chat.md). Memory dir changes (Tasks 3-6) are off-tree and don't appear in `git status`.

- [ ] **Step 1: Run /commit-sweep skill** to batch-commit all RTCs posted in Tasks 2, 6 (memory-dir summary RTC), 7, 8, 9 (if applicable), and 10.

Invoke via Skill tool: `commit-sweep`

- [ ] **Step 2: Verify the sweep landed.**

Run via Bash:
```
git log --oneline -10
git status --short
```
Expected: recent commits include the Hemanth-language tasks; working tree clean for the in-repo files.

- [ ] **Step 3: Post final READY TO COMMIT-RECEIPT line in agents/chat.md** confirming sweep complete:

```
[Agent 0, hemanth-language full arc shipped]: chat.md sweep landed commits for all hemanth-language tasks. Skill is live, Field Manual consolidated, 6-8 originals archived, CLAUDE.md + STATUS.md updated, brotherhood announced. Next: 30-day re-measurement scheduled for ~2026-06-20.
```

---

## Self-Review Checklist (run before handoff)

- [ ] **Spec coverage:** Every section of `docs/superpowers/specs/2026-05-20-hemanth-language-design.md` maps to a task above? (Skill body → Task 2. Field Manual → Task 3. Archive → Task 4. INDEX breadcrumb → Task 5. MEMORY.md → Task 6. CLAUDE.md → Task 7. STATUS.md → Task 8. SessionStart verify → Task 9. chat.md → Task 10. Sweep → Task 11.) ✓
- [ ] **Placeholder scan:** No "TBD" / "TODO" / "fill in details" / "similar to" / generic-handler placeholders in any task. Conditional content for Task 1's "6 vs 7 vs 8" outcome is parameterized cleanly, not placeholdered.
- [ ] **Type consistency:** Skill name is `hemanth-language` everywhere (no `hemanth_language` or `hemanthlanguage` or other typos). Field Manual filename is `feedback_hemanth_language_field_manual.md` everywhere.
- [ ] **Plan vs spec consistency:** Spec acceptance criteria mapped to plan tasks 1:1.

---

**Plan complete. Two execution options:**

1. **Subagent-Driven (recommended)** — Agent 0 dispatches a fresh subagent per task, reviews between tasks, fast iteration. Best when the work is mechanical-but-finicky like this consolidation (8 file moves + multiple cross-file edits + tone-matched announcement).
2. **Inline Execution** — Execute tasks in this session via `superpowers:executing-plans`, batch execution with checkpoints for review.

For this plan, **Subagent-Driven** is the recommended call — Tasks 2, 3, and 10 are large authoring tasks that benefit from focused subagent context, and the per-task review gates align with how Hemanth wants to stay in the loop without being in the middle of every step.

**Which approach?**
