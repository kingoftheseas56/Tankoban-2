# Seamless Engine-Switching Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. This is governance/skill/memory documentation work (no `src/` code, no compile), so "verification" steps are grep/byte/read-back checks, not unit tests.

**Goal:** Make a brotherhood agent-slot (Agent N) switching between its Claude/Opus engine and the DeepSeek V4-Pro tab (and Codex) as frictionless as possible — so a switch is a routing lookup, not a context-loss event.

**Architecture:** The brotherhood already switches engines well because all load-bearing state lives in files (recaps, chat.md, memories, governance), never in an engine's conversational memory. This plan hardens the four observed seams from the 2026-05-28 Volume X experiment: (1) handoffs are engine-*agnostic* but not engine-*aware* — they don't tell the next leg which engine to run on; (2) the Agent 9 persona hedges on whether the DeepSeek harness exposes Tier-1 skills, which is itself friction; (3) the switch conventions (attribution, handoff, routing) are scattered across 4 files; (4) the quota-driven routing call ("Codex first, DeepSeek when Codex low") has no at-a-glance quota signal.

**Tech Stack:** Markdown only — `.claude/commands/*.md` (skill templates), `agents/GOVERNANCE.md` + `agents/VERSIONS.md` (rules), `CLAUDE.md` (dashboard), off-git memory files. Commits are doc commits; no build.

**Execution context:** Flat-on-master, Path A (Agent 0 owns governance/dashboard/skill docs; no concurrent editor on these files this wake). Off-git memory edits need no commit.

**Decisions locked (Agent 0, can be vetoed on review):**
- The consolidated switch protocol is a **governance addition → gov-v10** (comparable to gov-v8 worktrees / gov-v9 self-commit). Keeps versioning honest.
- The quota signal is a **single dashboard field in CLAUDE.md** that Hemanth/Agent 0 update — lightest mechanism that still makes the switch a lookup. No automation.
- Harness skill-parity is documented from **observed RTM evidence** (this wake's DeepSeek RTM listed `/brief`, `/hemanth-language`, `/superpowers:executing-plans`, `/build-verify`, `/superpowers:verification-before-completion`, `/simplify`) plus a standing per-wake provenance check — Agent 0 (Opus) cannot live-run the DeepSeek harness, so this is evidence-documentation, not a live test.

---

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `.claude/commands/session-recap.md` | Wake-end recap template (what the next instance reads) | Add **Engine for next leg** field to template + gather/instructions |
| `.claude/commands/handoff-brief.md` | Mid-wake handoff brief template | Add same **Engine for next leg** line |
| `agents/GOVERNANCE.md` | Brotherhood rules | New `## Engine Switching Protocol` section |
| `agents/VERSIONS.md` | Governance version ledger | gov-v10 row |
| `CLAUDE.md` | State dashboard | One-line **Engine/quota status** field |
| `memory/project_agent9.md` (off-git) | Agent 9 definition | Replace "don't assume skills exist" hedge with confirmed surface + standing check |

---

## Task 1: Engine-aware field in the session-recap skill

**Files:**
- Modify: `.claude/commands/session-recap.md` (template section ~line 124 "Brotherhood state" + ~line 153 "Wake N+1 starting prompt"; gather section ~line 47)

- [ ] **Step 1: Add the Engine-routing line to the recap template's "Brotherhood state" section**

In `.claude/commands/session-recap.md`, inside the `## Brotherhood state I should pick up on next wake` template block, add as the first bullet:

```markdown
- **Engine for next leg:** <which engine the NEXT leg of this work wants, and why> — e.g. "next leg is the design/reversal pass → Opus (not yet tested on DeepSeek)"; "next leg is execution of the now-locked plan → DeepSeek or Codex, quota decides"; "audit/long-context → DeepSeek". Default if unsure: same engine as this wake. Routing rule: `agents/audits/deepseek_engine_experiment_2026-05-28.md`.
```

- [ ] **Step 2: Add the engine line to the "Wake N+1 starting prompt" template**

In the `## Wake N+1 starting prompt (copy-paste for Hemanth)` fenced block, add a line after the reading-order pointers:

```markdown
  Engine for this wake: <Opus | DeepSeek (Agent 9 tab) | Codex> — <one-line why, per the prior recap's "Engine for next leg">.
```

- [ ] **Step 3: Add a gather prompt so the writer fills it honestly**

In the `## Gather data before writing` section, append a bullet:

```markdown
- **Engine-for-next-leg call:** decide whether the next leg of your in-flight work is a design/deliberation pass (→ Opus), clean execution of a locked plan (→ DeepSeek/Codex by quota), or audit/long-context (→ DeepSeek). This becomes the "Engine for next leg" line — the routing decision is made HERE, by the agent who just did the work and knows what's next, not deferred to whoever opens the next tab.
```

- [ ] **Step 4: Verify the edits landed**

Run: `grep -n "Engine for next leg" .claude/commands/session-recap.md`
Expected: 2 matches (template bullet + gather bullet referencing it).

- [ ] **Step 5: Commit**

```bash
git add .claude/commands/session-recap.md
git commit -m "[Agent 0, ENGINE_SWITCH]: recap skill gains Engine-for-next-leg routing field"
```

---

## Task 2: Engine-aware field in the handoff-brief skill

**Files:**
- Modify: `.claude/commands/handoff-brief.md`

- [ ] **Step 1: Read the handoff-brief template to find its field list**

Run: `grep -n "^#\|^-\|^\*\*" .claude/commands/handoff-brief.md`
Identify the brief's field block (the list of what a mid-wake handoff carries).

- [ ] **Step 2: Add the engine line to the handoff-brief template**

Add to the brief's field block (adjacent to the "what's next / pending" field):

```markdown
- **Engine for the continuation:** <Opus | DeepSeek (Agent 9 tab) | Codex> + one-line why. A mid-wake handoff to a fresh tab should name the engine the remaining work wants (design pass → Opus; locked-plan execution → DeepSeek/Codex by quota). Default: same engine as the current session unless the remaining work changes shape.
```

- [ ] **Step 3: Verify**

Run: `grep -n "Engine for the continuation" .claude/commands/handoff-brief.md`
Expected: 1 match.

- [ ] **Step 4: Commit**

```bash
git add .claude/commands/handoff-brief.md
git commit -m "[Agent 0, ENGINE_SWITCH]: handoff-brief skill names the engine for the continuation"
```

---

## Task 3: Kill the skill-parity hedge in the Agent 9 persona memory (off-git)

**Files:**
- Modify: `memory/project_agent9.md` (off-git — NO commit)

- [ ] **Step 1: Replace the "don't assume skills exist" hedge with the confirmed surface**

In `C:\Users\Suprabha\.claude\projects\c--Users-Suprabha-Desktop-Tankoban-2\memory\project_agent9.md`, find the launch paragraph containing *"Do not assume Claude Code-only skills, hooks, or MCP servers exist unless the active harness exposes them."* Replace that sentence with:

```markdown
**Confirmed harness surface (DeepSeek VS Code tab, observed 2026-05-28 via Volume X RTM provenance):** `/brief`, `/hemanth-language`, `/superpowers:executing-plans`, `/build-verify`, `/superpowers:verification-before-completion`, `/simplify` all invoked successfully. Treat the Tier-1 skill set + `build_check.bat` lane auto-detection + gov-v9 Path B worktrees as AVAILABLE in the DeepSeek tab unless a specific invocation fails. Standing check: each DeepSeek wake's RTM should list its skills-invoked provenance (contracts-v3) — if a Tier-1 skill is ever unavailable, note it in the RTC and flag for Agent 0 so this confirmed-surface list is corrected. (Supersedes the prior blanket "don't assume skills exist" hedge, which was itself friction — uncertainty made DeepSeek-run agents second-guess whether discipline applied.)
```

- [ ] **Step 2: Verify**

Run: `grep -n "Confirmed harness surface" "C:/Users/Suprabha/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/project_agent9.md"`
Expected: 1 match. And confirm the old hedge string is gone:
Run: `grep -c "Do not assume Claude Code-only skills" "C:/Users/Suprabha/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/project_agent9.md"`
Expected: 0.

- [ ] **Step 3: No commit** (off-git per-machine memory). Note in the executing summary that this edit is done but uncommitted-by-design.

---

## Task 4: Consolidated Engine Switching Protocol in GOVERNANCE.md

**Files:**
- Modify: `agents/GOVERNANCE.md` (new section, insert after the `## PROTOTYPE + AUDIT + IMPLEMENTATION Protocol` block ends and before `## Build Rules`, i.e. around line 317)

- [ ] **Step 1: Read the insertion boundary**

Run: `sed -n '310,320p' agents/GOVERNANCE.md`
Confirm the line where the Agent-7 protocol section ends and `## Build Rules` begins; insert the new section immediately before `## Build Rules`.

- [ ] **Step 2: Insert the new section**

Insert this complete section before `## Build Rules`:

```markdown
## Engine Switching Protocol (added 2026-05-28 — gov-v10)

A brotherhood **agent-slot** (Agent 1, Agent 2, …) is an identity, not an engine. The same slot may run on Claude/Opus one wake, DeepSeek V4-Pro (the Agent 9 tab) the next, or hand a leg to Codex. This protocol makes that switch seamless. It consolidates conventions previously scattered across `project_agent9.md`, the routing report, and the commit/handoff rules.

**1. State-in-files is the enabler — protect it.** A switch is only seamless because the incoming engine reads the same disk the outgoing one wrote: recaps (`~/.claude/recaps/`), trimmed transcripts (`.cc-history/*.trimmed.md`), `agents/chat.md`, governance, and off-git memory. NEVER let load-bearing state live only in an engine's conversational memory. If it matters across a wake or a switch, it is written to a file first.

**2. Attribution carries the engine.** Sign every RTC / RTM / commit / recap with the engine in the parenthetical: `[Agent 1 (Opus), TAG]`, `[Agent 1 (DeepSeek V4-Pro), TAG]`, `[Agent 7 (Codex), TAG]`. This lets `git log` / chat archive show which engine produced which work without ambiguity.

**3. The routing call is made at handoff, by the agent who just did the work.** Recaps and handoff-briefs carry an **Engine for next leg** line (see the `session-recap` + `handoff-brief` skills). The agent who just finished knows what the next leg is (design pass vs locked-plan execution vs audit) and names the engine for it. Routing table: `agents/audits/deepseek_engine_experiment_2026-05-28.md`. Summary:
   - **Execute a locked / fully-specified plan** → DeepSeek (Agent 9) or Codex (Agent 7), quota decides. *Proven 2026-05-28.*
   - **Design / deliberation pass** (reversal-heavy archaeology that produces the locked plan) → Opus, until DeepSeek is tested there.
   - **First-pass audit / research / long-context / parser-bulk logic** → DeepSeek's natural strength.
   - **Gnarly production-C++ / novel architecture / long agentic loop** → prefer Codex.
   - **Anything an execution engine ships** → reviewer pass (Opus/Codex) before master, mandatory — same as Codex Trigger-D.

**4. Mid-arc handoff mechanics = gov-v9 Path B.** To switch engines mid-arc without losing work: the outgoing engine self-commits in its worktree (or flags RTC on flat checkout), posts `READY TO MERGE` / a handoff-brief naming the next engine, and the incoming engine picks up from the committed base. No work is stranded in an unmergeable in-flight state across a switch.

**5. Quota is a routing input, never a replacement argument.** The Codex↔DeepSeek default is "Codex first, DeepSeek when Codex quota is low." Quota state is read from the **Engine/quota status** field in `CLAUDE.md`'s dashboard (Hemanth or Agent 0 keeps it current). Brothers are not swappable slots — cost/quota decides *which available brother takes a switch-eligible task*, it never argues for removing one ([[feedback_brotherhood_is_not_swappable]]).
```

- [ ] **Step 3: Verify**

Run: `grep -n "## Engine Switching Protocol" agents/GOVERNANCE.md`
Expected: 1 match, positioned before `## Build Rules`.

- [ ] **Step 4: Commit** (bundled with Task 5 VERSIONS bump — commit together so the gov-v10 ledger and the section land atomically; see Task 5 Step 3.)

---

## Task 5: gov-v10 row in VERSIONS.md

**Files:**
- Modify: `agents/VERSIONS.md`

- [ ] **Step 1: Read VERSIONS.md to match the existing row format**

Run: `sed -n '1,40p' agents/VERSIONS.md`
Identify the governance-version table/list format and the current latest row (gov-v9).

- [ ] **Step 2: Append the gov-v10 row in the file's existing format**

Add a gov-v10 entry mirroring the format of the gov-v8/gov-v9 rows, with content:

```
gov-v10 (2026-05-28) — Engine Switching Protocol added to GOVERNANCE.md. Codifies state-in-files enabler, engine-in-attribution, handoff-time routing call (Engine-for-next-leg in recap + handoff-brief skills), gov-v9 Path B mid-arc switch mechanics, and quota-as-routing-input read from the CLAUDE.md dashboard field. Driven by the 2026-05-28 DeepSeek Volume X experiment (agents/audits/deepseek_engine_experiment_2026-05-28.md).
```

- [ ] **Step 3: Verify + commit GOVERNANCE.md (Task 4) and VERSIONS.md together**

Run: `grep -n "gov-v10" agents/VERSIONS.md`
Expected: ≥1 match.

```bash
git add agents/GOVERNANCE.md agents/VERSIONS.md
git commit -m "[Agent 0, ENGINE_SWITCH]: gov-v10 — Engine Switching Protocol (state-in-files, engine attribution, handoff-time routing, quota signal)"
```

---

## Task 6: Engine/quota status field in the CLAUDE.md dashboard

**Files:**
- Modify: `CLAUDE.md` (the "30-Second State Dashboard" block — add near "Live governance versions" / "Blocked" lines)

- [ ] **Step 1: Locate the dashboard status lines**

Run: `grep -n "Live governance versions\|\*\*Blocked:\*\*\|Open HELP" CLAUDE.md`
Pick the insertion point adjacent to the other one-line status fields.

- [ ] **Step 2: Add the field**

Add this line alongside the other dashboard status one-liners:

```markdown
**Engine/quota status:** Codex quota OK (default Codex for execution work; switch execution-shaped tasks to DeepSeek/Agent 9 when this reads "Codex LOW"). Opus = design/deliberation pass. Updated by Hemanth or Agent 0. Routing: gov-v10 Engine Switching Protocol.
```

- [ ] **Step 3: Verify**

Run: `grep -n "Engine/quota status" CLAUDE.md`
Expected: 1 match.

- [ ] **Step 4: Commit**

```bash
git add CLAUDE.md
git commit -m "[Agent 0, ENGINE_SWITCH]: dashboard gains Engine/quota status field (the at-a-glance switch signal)"
```

---

## Task 7: Cross-link the routing memory (off-git)

**Files:**
- Modify: `memory/feedback_deepseek_execution_engine_proven.md` (off-git — NO commit)

- [ ] **Step 1: Add a pointer to the new governance protocol**

In `feedback_deepseek_execution_engine_proven.md`, append to the final "Routing report / companions" line:

```markdown
Governance home: `agents/GOVERNANCE.md` § Engine Switching Protocol (gov-v10). Handoff carries the engine via the `session-recap` + `handoff-brief` skills' "Engine for next leg" field.
```

- [ ] **Step 2: Verify**

Run: `grep -n "Engine Switching Protocol" "C:/Users/Suprabha/.claude/projects/c--Users-Suprabha-Desktop-Tankoban-2/memory/feedback_deepseek_execution_engine_proven.md"`
Expected: 1 match.

- [ ] **Step 3: No commit** (off-git memory).

---

## Self-Review (run after all tasks)

1. **Seam coverage:** Seam 1 (engine-aware handoff) → Tasks 1+2. Seam 2 (harness parity hedge) → Task 3. Seam 3 (scattered conventions) → Tasks 4+5. Seam 4 (quota signal) → Task 6. Cross-linking → Task 7. All four seams covered.
2. **Placeholder scan:** every insert step carries the literal markdown to add — no "TBD"/"add appropriate"/"similar to".
3. **Consistency:** the field is named **"Engine for next leg"** in the recap (Task 1) and **"Engine for the continuation"** in the handoff-brief (Task 2) — intentionally distinct (wake-boundary vs mid-wake) but both point at the same gov-v10 routing table; GOVERNANCE.md §3 references both skills by that framing. The dashboard field is **"Engine/quota status"** (Task 6), referenced by GOVERNANCE.md §5. Consistent.
4. **Commit grouping:** Tasks 4+5 commit together (atomic gov section + version row). Tasks 1, 2, 6 commit independently. Tasks 3, 7 are off-git (no commit).
```
