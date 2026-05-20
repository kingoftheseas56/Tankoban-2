# `hemanth-language` Skill — Design

**Date:** 2026-05-20
**Author:** Agent 0 (Coordinator)
**Status:** Approved by Hemanth via brainstorm (3 rounds of 4 questions). Ready for implementation plan.

---

## Problem

Six existing project memories address how brothers should talk to Hemanth — `feedback_hemanth_terms_or_skip.md`, `feedback_decision_authority.md` (Rule 14 supporting), `feedback_self_service_execution.md` (Rule 15 supporting), `feedback_hemanth_role_open_and_click.md`, `feedback_coordination_mechanics_not_hemanth.md`, `feedback_directive_lives_in_files.md`. Plus the `hemanth-rewrite` skill.

Despite all of that scaffolding, brothers continue to burn Hemanth's brain cells. Hemanth verbatim 2026-05-20: *"agents continue to burn my remaining few brain cells, I didn't have many to begin with."* The structural failure is not in any single rule's wording — it's that the lessons are **too scattered** to be internalized, and they miss a key dimension: **analogies to non-coding domains** which Hemanth explicitly named as the lever that helps him understand best.

## Goals

1. Single unified anchor that brothers see every wake, replacing six scattered references.
2. Codify the **analogy-first** discipline (which no existing memory captures).
3. Codify the **preview-per-task-group** discipline (silence-between-tool-calls is a top-3 brain-burn).
4. Refine the **menu rule** from "no menus" to "menus are OK if all four ingredients are present."
5. Keep Hemanth in the open-app / click / report lane (preserved from Rule 15).
6. Auto-loads every wake — no honor-system, no manual invocation.

## Non-Goals

- **No new governance rule.** Skill + memory is enough (Hemanth explicitly chose against the "all three layers" option in brainstorm round 2).
- **No pre-prompt hook scanning responses for jargon/menus.** Auto-load every wake IS the enforcement. Trade-off accepted: lighter machinery, no automated catch of violations.
- **No retroactive cleanup** of past chat.md or past brain-burns. Going-forward only.
- **No change to Rules 14 + 15** in `agents/GOVERNANCE.md` — those stay as governance. Their *supporting memories* consolidate into the new Field Manual; the rules themselves don't move.

## Design

### The Skill

| Property | Value |
|----------|-------|
| **Name** | `hemanth-language` |
| **Plugin / location** | `.claude/commands/hemanth-language.md` — matches Tankoban convention (verified: existing skills `brief`, `hemanth-rewrite`, `commit-sweep`, etc. all live in `.claude/commands/<name>.md`, plain markdown body, no SKILL.md frontmatter wrapper) |
| **Tier** | 1 (Core Mandatory) |
| **Loading** | Auto-loads at SessionStart for every wake. Same mechanism as `/brief`, `/session-recap`, `/superpowers:verification-before-completion`. |
| **Length target** | ~150 lines. Short enough that brothers re-read it cover-to-cover. |

### The 4 Disciplines

**Discipline 1 — Analogies first.** For any non-trivial explanation, the brother leads with a non-coding metaphor, then maps to the code reality. The analogy is the doorway — it gives Hemanth's brain a handle to grab before the technical detail lands.

Pattern: `[non-coding analogy that captures the structural truth] + [the code-reality mapping] + [why this matters for the current decision]`.

**Discipline 2 — Preview per task group, not per file.** Brother announces one 1-line "about to do X" before each grouped chunk of work. The preview describes the *logical goal* of the next chunk, not every micro-action.

Examples of good preview granularity:
- ✅ "About to land Rule 21 in three files: governance, versions, chat.md."
- ✅ "About to scan local suspects (hooks, MCPs, claude-mem health) in parallel."
- ❌ "About to read X. Done. About to read Y. Done. About to grep Z. Done." — too granular, becomes noise.

**Discipline 3 — No silence.** If a brother has been silent for more than ~30 seconds during tool work without a preview, that's a violation. Hemanth's stated preference is *previews-then-execute* (Q2 brainstorm round 1): he doesn't need mid-task narration or post-task summaries, but he must never be wondering "what is the brother doing right now."

**Discipline 4 — Menus only when all four ingredients are present.** Menus aren't categorically bad — Hemanth explicitly clarified that *well-explained* menus help him. A menu is permitted if and only if it has:

1. **Plain-language description per option** — beyond the label, a 1-line sentence in Hemanth's terms.
2. **An analogy or concrete example for any technical option** — Discipline 1 applied to the menu shape.
3. **A recommended pick *with the reason*** — not just `(Recommended)`, but *"I'd pick A because [specific reasoning]."*
4. **Honest cost framing per option** — every pick has a `this gets us X but costs us Y` line. No hidden downsides.

If a brother can't put all four together for a given question, that's a signal the question is technical/architectural and they should decide themselves per Rule 14. No menu.

### The 5 Paired Examples (the meat of the skill)

The skill body contains 5 paired examples (brain-burner version → Hemanth-friendly rewrite → discipline demonstrated). Examples are drawn from actual recent brain-burn moments to maximize pattern-match teaching. Each example has three parts:

1. **The brain-burner version** — verbatim shape of how a brother actually phrased something (or might).
2. **The Hemanth-friendly rewrite** — what the brother should have said.
3. **The discipline demonstrated** — which of the 4 disciplines it ties to (one example may demonstrate multiple).

**Example 1 — Analogy-first (Discipline 1):**
- Topic: explaining what worktrees do for parallel agents.
- Brain-burner: *"Worktrees create isolated git working directories that point at separate branches, eliminating Edit-races by physically separating filesystem state across N parallel subagent worktrees."*
- Hemanth-friendly: *"Worktrees are like giving each carpenter their own workshop instead of fighting over one workbench. In code terms, each brother gets their own copy of the repo so they can't accidentally erase each other's edits."*

**Example 2 — Preview per task group (Discipline 2):**
- Topic: brother lands a multi-file change.
- Brain-burner: [Brother goes silent. Runs 8 Bash + 4 Read + 2 Edit tools over 90 seconds. Suddenly produces a 600-line response.]
- Hemanth-friendly: *"About to land Rule 21 across four files — governance, versions, memory index, and chat.md announcement. Going to do all the writes in one batch, then verify."* [Then does the work and reports.]

**Example 3 — Good menu (Discipline 4):**
- Topic: catalog storage choice.
- Brain-burner: *"Do you want A or B? A = use SQLite for the catalog cache. B = use JSON files. Pick one."*
- Hemanth-friendly: *"How should we store the catalog cache? **A: SQLite (Recommended)** — like a filing cabinet with built-in search; readers can grab any record instantly. Costs us ~20MB more in disk and a build dependency. **B: JSON files** — like a stack of paper folders; simpler, no new dependency, but slower to look up. I'd pick A because the catalog will hit 50K+ entries by Phase 8 and JSON lookup gets sluggish past ~10K."*

**Example 4 — Rule 15 cross-reference (don't ask Hemanth coder things):**
- Topic: brother needs log data.
- Brain-burner: *"Hemanth, can you open `out/sidecar_debug_live.log` and paste lines 200-220? I need to see the [PERF] timing values around that timestamp."*
- Hemanth-friendly: [Brother reads the log themselves.] *"Found the issue — the [PERF] traces show video_decoder.cpp blocking for 230ms on every frame around the timestamp. That's about 8x the budget. About to fix the queue depth and re-test."*

**Example 5 — Disciplines 1 + 2 + 3 combined (analogies + preview + no silence):**
- Topic: brother is about to investigate something complex.
- Brain-burner: [Long silence, brother runs many tool calls, finally produces a finding.]
- Hemanth-friendly: *"Quick — about to check three suspects in parallel: the hooks (the bouncers checking IDs at the door), the MCP subprocesses (the helpers in the back room), and claude-mem health (the memory file cabinet). Want to know which one's slow. Back in a moment."*

(Example 5's analogy density is high to demonstrate the **maximum** end of the discipline. Lower-density use is also fine — the principle is "reach for the analogy when the concept is non-trivial.")

### Skill File Structure

```
.claude/skills/hemanth-language/SKILL.md
```

Sections:
1. **Frontmatter** — name, description (the auto-load signal), tier.
2. **The principle** — 1-paragraph why-this-skill-exists.
3. **The 4 disciplines** — each in ~3-5 lines, with the pattern formula.
4. **The 5 paired examples** — formatted as a side-by-side table or sequential blocks.
5. **The failure-shape taxonomy** — the 4 brain-burn patterns Hemanth named (jargon paragraphs, going silent, asking him coder things, bare/deep-architecture menus).
6. **Cross-reference** — pointer to `feedback_hemanth_language_field_manual.md` for the deep content + the 6 archived originals for historical context.

### Supporting Memory: `feedback_hemanth_language_field_manual.md`

The "Field Manual" is the deeper companion to the skill. Skill is the **always-loaded summary**; memory is the **deep reference** brothers consult when they want full context.

Content of the Field Manual:
- Full text content from the 6 consolidated original memories (preserved verbatim with header attribution).
- Cross-references to Rules 14 + 15 in `agents/GOVERNANCE.md` (which stay).
- Pointer to the `hemanth-rewrite` skill (which stays — it's the *rewrite tool*, separate from the *anchor doctrine*).
- Estimated length: ~400-600 lines.

### Migration Plan

| Step | Action |
|------|--------|
| 1 | Author `.claude/skills/hemanth-language/SKILL.md` per the structure above. |
| 2 | Author the consolidated `feedback_hemanth_language_field_manual.md` in the project memory dir. |
| 3 | Archive 6 originals to `memory/_archive/2026-05/` with breadcrumb in `memory/_archive/INDEX.md`. |
| 4 | Update `memory/MEMORY.md` index: replace the 6 individual entries with one Field Manual entry. |
| 5 | Update `CLAUDE.md` "Required Skills & Protocols" Tier 1 list to add `hemanth-language`. |
| 6 | Update `agents/STATUS.md` per-agent shortlists to reference the new skill where relevant. |
| 7 | Announce in `agents/chat.md` so all agents see the new discipline before their next wake. |
| 8 | (Optional, deferred) Re-measurement in 30 days — same shape as `feedback_skill_discipline_remeasurement.md` — does the brain-burn frequency drop? |

The 6 memories Hemanth named at brainstorm time to consolidate + archive (originals preserved in `git log` and `memory/_archive/`):
1. `feedback_hemanth_terms_or_skip.md`
2. `feedback_decision_authority.md` (Rule 14 supporting; rule stays in GOVERNANCE.md)
3. `feedback_self_service_execution.md` (Rule 15 supporting; rule stays in GOVERNANCE.md)
4. `feedback_hemanth_role_open_and_click.md`
5. `feedback_coordination_mechanics_not_hemanth.md`
6. `feedback_directive_lives_in_files.md`

**Discovered during self-review — additional Hemanth-language memories referenced by `hemanth-rewrite.md` that should also be considered for consolidation:** `feedback_simple_language.md`, `feedback_no_tables_simple_lists.md`. Plus the `hemanth-rewrite.md` skill itself has 8 concrete prose rules embedded (lead with answer / short sentences / translate jargon / no markdown tables / etc.) that overlap with the new skill's scope. Implementation phase MUST verify these exist and consolidate any additional in-scope memories, but `hemanth-rewrite.md` itself **stays as a separate skill** — it is the *rewrite tool* for in-flight paragraphs, while `hemanth-language` is the *anchor doctrine*. The two complement each other rather than overlap.

## Acceptance Criteria

- `hemanth-language` skill exists at `.claude/skills/hemanth-language/SKILL.md`, loads in the SessionStart skills list for every new wake, and is verified to fire by checking that it appears in the available-skills list on session start.
- `feedback_hemanth_language_field_manual.md` exists in the project memory dir, indexed in MEMORY.md as the THE entry on Hemanth-language.
- The 6 originals are physically moved to `memory/_archive/2026-05/` with INDEX.md breadcrumb. (NOT deleted — `git log` history preserved either way, but the archive is the canonical landing place.)
- CLAUDE.md Tier 1 list includes `hemanth-language`.
- Brotherhood-wide announcement in `agents/chat.md` matches the tone register of the Rule 21 announcement (clear, structured, brotherhood-affectionate where appropriate).
- Verification on next wake: brother (any brother, any wake) sees `hemanth-language` in their available-skills list at session start; reads it; can articulate the 4 disciplines back.

## Honest Tradeoffs (the design's own cost-frame)

- **No automated enforcement.** Brothers can ignore the skill at runtime. The bet is that loading-every-wake + good examples + Hemanth's escalating frustration creates enough pressure. If 30-day re-measurement shows continued burn, escalate to a pre-prompt hook (we have precedent: the pre-RTC checker from Phase 4 of SKILL_DISCIPLINE_FIX).
- **Consolidation loses some specificity.** The 6 original memories had context-specific framings (e.g., `feedback_coordination_mechanics_not_hemanth` was scoped to *coordination* mechanics specifically). Merging into one Field Manual flattens nuance. Mitigation: preserve original headings + attribution within the Field Manual.
- **One more Tier-1 skill to load every wake.** Cost: ~150 lines × N agents × every wake = real prompt-cache work. But the existing Tier-1 list is already 6 entries (`/brief`, `/session-recap`, `/superpowers:verification-before-completion`, `/simplify`, `/build-verify`, `/superpowers:requesting-code-review`) — adding a 7th is marginal.
- **No governance rule means brothers can claim "I didn't know."** Mitigated by skill auto-load + Tier-1 status + cross-reference from CLAUDE.md.

## Implementation Sequencing

Pre-planned task chunks for the writing-plans skill to break down:

1. Author the skill file (the spine — 4 disciplines + 5 examples + failure taxonomy + structure).
2. Author the Field Manual memory (consolidate 6 originals verbatim with attribution).
3. Archive the 6 originals + update MEMORY.md index + write INDEX.md breadcrumb.
4. Update CLAUDE.md Tier 1 list.
5. Update STATUS.md per-agent shortlists.
6. Verify SessionStart skill list shows `hemanth-language` (live test against a fresh wake).
7. Author the chat.md announcement.
8. (Defer to 30-day mark) Re-measurement audit candidate.

## References

- Brainstorm rounds: 3 rounds of 4 questions via `AskUserQuestion`, Hemanth picked recommended on all decisive questions. Round 1 surfaced the pain shape + diagnosis + priority. Round 2 surfaced cadence + analogies + menu ingredients + anchor format. Round 3 surfaced trigger + name + migration shape + examples format.
- Pattern references: `feedback_skill_discipline_remeasurement.md` (re-measurement criterion), `feedback_fix_todo_authoring_shape.md` (TODO structure for skill-shipping arcs), `feedback_plan_first_zero_errors.md` (plan-first discipline that brought us here).
- Related skill family: `hemanth-rewrite` (paragraph-rewriting tool, complementary to this skill's doctrine).

---

**Next step:** invoke `/superpowers:writing-plans` to break this design into ordered implementation tasks.
