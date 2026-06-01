# SKILL_DISCIPLINE_FIX_TODO

**Status:** ✅ ALL PHASES CLOSED 2026-04-25 — Phase 1+2+3+4+5+6 + §5 RATIFIED + re-measurement criterion documented. Single-wake six-phase arc spanning a machine reboot. TODO archive-ready. Phase 1 diagnosis green (root cause = no `claude.cmd` on PATH; Hemanth uses Claude Code via VS Code extension not CLI). Phase 2 remedy green (Option A install + machine reboot; observations table now non-empty; `/mem-search?query=tankoban` returns 2 hits — see [diagnosis §10](agents/audits/claude_mem_persistence_diagnosis_2026-04-25.md)). §5 Decisions ratified wholesale by Hemanth ("yeah do these two next steps too") — all 8 strategic questions resolved with Agent 0's recommended answers (locked verbatim in §5 below). Phase 3 RTC provenance contract green: GOVERNANCE.md Rule 11 + CONTRACTS.md new § + commit-sweeper.md parser extension + session-brief.sh card + VERSIONS.md contracts-v3 bump. Phase 4 (pre-RTC checker hook) unblocked; Phases 5+6 sequenced behind it.

**Owner:** Agent 0 (Coordinator) — both authoring and first-time execution per Hemanth ratification 2026-04-25 ~21:30 ("Yes and you will be the one executing the TODO for the very first time"). Honest scope note: Agent 0 normally authors and a domain agent executes; this TODO crosses both lanes because the subject IS coordination/governance/telemetry — there is no other domain owner.

**Source:** [agents/audits/skill_discipline_audit_2026-04-25.md](agents/audits/skill_discipline_audit_2026-04-25.md) (Agent 7 / Codex). Evidence-first audit with 176-RTC sample, 275-commit sample since 2026-04-15, claude-mem SQLite + HTTP API ground-truth, upstream claude-mem issue references, 4 hypotheses + 8 open questions. Brainstorming step is therefore evidence-loaded already; formal `/superpowers:brainstorming` skipped as a Rule-14 call (the audit IS a stronger scoping artifact than a fresh brainstorm would produce).

---

## §1 Goal

Close the gap between the brotherhood's designed skill discipline (21 mandatory skills with hard trigger map per [CLAUDE.md:85-138](CLAUDE.md#L85-L138)) and observed behavior:

- 1-of-116 explicit `/build-verify` mentions on code-touch RTCs.
- 2-of-168 explicit `/superpowers:systematic-debugging` mentions on debug-shaped RTCs.
- 0 explicit `/mem-search` / `/smart-explore` / `/timeline-report` / `/knowledge-agent` mentions across all chat history examined (live + 3 archives).
- 56 Tankoban claude-mem sessions, **0 observations, 0 summaries, 0 corpora**.
- 39 `summaryStored=null` events on a single day (2026-04-25).

The audit's headline framing — and this TODO's load-bearing constraint — is that **claude-mem repair is a prerequisite, not a result, of enforcing `/mem-search` discipline.** Tightening governance against a hollow memory store will produce cynicism, not compliance. The phases are sequenced accordingly: memory rehab first, telemetry/provenance second, enforcement hooks third, governance trim last.

---

## §2 Why now

Hemanth has explicitly asked for the powerup ("how do we power up our agents with claude-mem and other stuff" — 2026-04-25 ~21:25). The honest answer is on Page 1 of the audit: we can't, until memory persistence is repaired. Every wake we ship without this fix is another wake of "use mem-search!" rules pointed at an empty box, which actively trains the brotherhood to ignore memory tooling.

Secondary urgency: governance versions are stable (gov-v3 / contracts-v2), no Congress is open, no domain TODO is competing for Agent 0's bandwidth. This is the right moment.

---

## §3 Constraints (load-bearing per audit + memory)

- **Memory before governance.** Audit §3 + §7 + Bottom Line. Don't tighten the mandatory framing until `/mem-search` returns Tankoban hits. Reversing this order guarantees more resentment than compliance.
- **Provenance over honor system.** Audit §3 cause F + I. Add `Skills invoked:` field to RTC contract; let commit-sweeper preserve it. The system currently can't distinguish "skill skipped" from "skill ran but unrecorded" — that's a defect, not just an inconvenience.
- **Cross-platform honesty.** Audit §8 + memory `feedback_codex_mcp_nested_approval_trap.md`. Codex (Agent 7) lacks Claude Code's `PostToolUse` / `SessionStart` skill hooks. The brotherhood writes one contract that names what each platform can and can't enforce; we don't pretend Codex meets the same bar.
- **Reminder fatigue is real.** Audit §3 cause C. Replace dense SessionStart card surface with a contextual / on-demand surface. Don't add a third reminder on top of two existing ones.
- **No human-day deadlines.** Per `feedback_no_human_days_in_agentic.md` — phases measured in summons.
- **Honest sequencing.** Phase ordering encodes the audit's load-bearing finding. Skipping straight to Phase 6 (governance trim) without Phase 1+2 (memory rehab) defeats the TODO.

---

## §4 Out of scope

- Patching upstream claude-mem source (`C:\Users\Suprabha\.claude\plugins\marketplaces\thedotmack\src\`). If Phase 1 surfaces an upstream bug, file an issue and apply local workaround if one exists; do not fork.
- Codex (Agent 7) platform parity. Codex's lack of `PostToolUse` hooks is a Codex problem; this TODO documents the gap, doesn't close it.
- Adding new skills. Audit recommends trimming, not expanding.
- Backfilling historical observations. The 56 already-empty Tankoban sessions stay empty; rehab applies to new sessions onward.
- "Agent powerup" as a category. This TODO solves the specific failure modes the audit surfaced; other powerup vectors (better build-verify ergonomics, faster MCP smokes, smarter audit-validation flows) remain separate scope.

---

## §5 Decisions — RATIFIED 2026-04-25 ~22:30 by Hemanth

Eight strategic calls from audit §9, ratified wholesale ("yeah do these two next steps too" 2026-04-25 ~22:30, where "next steps" included §5 ratification with my recommended picks). All 8 questions answered with Agent 0's recommendations as locked positions; subsequent phases proceed under these terms.

1. **Core mandatory vs conditional split?** ✅ **RATIFIED — SPLIT.** ~6 core mandatory + ~15 conditional. Long-tail skills with near-zero observable use stop being framed "mandatory" — that framing produces guilt instead of behavior change.
2. **Is provenance itself a requirement, or is work-shape proof enough?** ✅ **RATIFIED — PROVENANCE AS REQUIREMENT** for non-trivial RTCs (defined as ≥1 `src/` file or ≥30 LOC). Trivial RTCs (doc-only / governance-only / single-line edits) exempt.
3. **Missing `Skills invoked:` field — block RTC or nag?** ✅ **RATIFIED — NAG-ONLY for first 30 days** post-Phase-3 ship. Promote to block only if compliance plateaus below 80% after 30 days. Don't punish habit-formation.
4. **claude-mem repair before governance tightening, or parallel?** ✅ **RATIFIED — SERIAL.** Load-bearing call of the whole TODO. Phase 1+2 (memory rehab) shipped first 2026-04-25 BEFORE Phase 3+ (governance tightening) begins. See §3 first constraint.
5. **Accept 1-2s pre-RTC hook latency?** ✅ **RATIFIED — YES.** Trivial cost for materially better discipline. Phase 4 hook implementation may exceed 2s in pathological cases (very large pending RTCs); add a hard 5s timeout fallback to prevent shell-hang.
6. **Codex same literal standard as Claude Code?** ✅ **RATIFIED — NO.** Codex contract names what Codex can do (read claude-mem via MCP, name skills explicitly in audit text) and accepts the rest as documented platform-gap. Cross-platform asymmetry written into the new contracts-v3 governance text in Phase 3.
7. **Keep zero-usage memory skills mandatory?** ✅ **RATIFIED — DEMOTE** `/timeline-report` + `/knowledge-agent` to milestone-only governance. `/mem-search` becomes mandatory ONLY because memory rehab now demonstrably works (per [diagnosis §10](agents/audits/claude_mem_persistence_diagnosis_2026-04-25.md)) — would have stayed demoted otherwise.
8. **Trim 21-skill sheet first, then expand?** ✅ **RATIFIED — YES.** Trim to ~6 core in Phase 6, observe for 30 days under Phase 4 telemetry, expand only if telemetry proves additions are actually used. 30-day re-measurement is a `/schedule` candidate (Agent 0 future wake).

---

## §6 Phases

### Phase 1 — Diagnose claude-mem persistence failure (Agent 0, this wake)

**Goal:** explain why 39 `summaryStored=null` events fired on 2026-04-25 with zero observations persisting. No governance changes. No code changes outside diagnostic scripts.

**Steps:**
- Reproduce empty state locally (SQLite query + HTTP API probe matching audit §4 evidence).
- Inspect claude-mem worker source: observation write path (`PostToolUse → queue → worker SDK → <observation> → observations table → Chroma sync`) and summary write path.
- Trace `summaryStored=null` through the failing path. Identify whether failure is: (a) Bun/worker startup (issue #565 shape), (b) Chroma sync timeout, (c) session wall-clock-age abort, (d) SDK-side observation generation failure, (e) something else.
- Decide remedy: upstream bug → file issue + apply config workaround if one exists; local config → patch settings; missing prerequisite → document the prerequisite and add to onboarding.
- **Output:** `agents/audits/claude_mem_persistence_diagnosis_2026-04-25.md` with evidence + remedy decision + Phase 2 unblock criteria.

**Files touched:** `agents/audits/claude_mem_persistence_diagnosis_2026-04-25.md` (new). No src/. No governance.

**Exit criterion:** Hemanth has a one-paragraph explanation he can read of "this is why mem-search returns nothing for Tankoban" + a remedy proposal he can ratify.

### Phase 2 — Restore claude-mem observations + summaries (Agent 0)

**Depends on:** Phase 1 remedy decision + Hemanth ratification of §5 question 4 (serial sequencing).

**Steps:**
- Apply Phase 1's recommended remedy.
- Verify `/api/search/observations?query=tankoban&limit=5` returns Tankoban-specific hits (not just prompt hits).
- Run one Tankoban session under the fixed configuration; verify the new session lands as ≥1 observation and ≥1 summary in SQLite.
- Build one Tankoban corpus via `/claude-mem:build_corpus` or HTTP equivalent.
- Prime + query it; verify results are coherent.

**Exit criterion:** `/mem-search` invocation by any brotherhood agent returns Tankoban-relevant results.

### Phase 3 — Add `Skills invoked:` provenance to RTC contract (Agent 0)

**Depends on:** Hemanth ratification of §5 questions 2 + 3.

**Steps:**
- Update `agents/GOVERNANCE.md` RTC contract section: non-trivial RTCs include `Skills invoked: [list]` line.
- Update `.claude/agents/commit-sweeper.md` regex/parser: extract the field, preserve in sweep summary, flag missing on non-trivial RTCs.
- Update `.claude/scripts/session-brief.sh` SessionStart card: surface the new field.
- Bump `agents/VERSIONS.md` from contracts-v2 → contracts-v3.

**Exit criterion:** next chat.md sweep produces a summary that names which skills the brotherhood actually invoked.

### Phase 4 — Pre-RTC checker hook (Agent 0)

**Depends on:** Phase 3 shipped; Hemanth ratification of §5 question 5.

**Steps:**
- New `.claude/scripts/pre-rtc-checker.sh`: scans staged RTC text for `Skills invoked:` field on non-trivial RTCs; matches build/security/debug heuristics from audit §3 cause A.
- Wire to `Stop` hook (or appropriate decision-point per Anthropic hooks docs).
- Mode: **nag-only** for first 30 days per §5 question 3 recommendation. Print warning + RTC tag suggestion; do not block.
- Telemetry: append nag-fire counts to `.claude/telemetry/skill-discipline.jsonl` so we can re-measure compliance at the 30-day mark.

**Exit criterion:** any non-trivial RTC missing required skill provenance triggers a visible warning; warning rate is measurable.

### Phase 5 — Memory-degraded sentinel hook (Agent 0)

**Depends on:** Phase 2 shipped (so the sentinel has a healthy state to compare against).

**Steps:**
- New `.claude/scripts/memory-health.sh`: probes `/api/corpus` + observation count at SessionStart.
- If `observations==0 OR corpora==[]`: print prominent "memory degraded" warning, suppress `/mem-search` rule from the SessionStart card.
- Wire to existing `SessionStart` hook in `.claude/settings.json`.

**Exit criterion:** when memory is healthy, sentinel is silent; when memory is empty, sentinel is loud and the mandatory-mem-search rule is auto-demoted.

### Phase 6 — Trim 21-skill sheet + per-agent shortlists (Agent 0)

**Depends on:** Phase 3 + Phase 4 shipped (so post-trim measurement is possible); Hemanth ratification of §5 questions 1 + 7 + 8.

**Steps:**
- Update `CLAUDE.md` "Required Skills & Protocols" section: split into Core Mandatory (~6) + Conditional (~15).
- Update memory `feedback_plugin_skills_adopted.md`: revise tier framing.
- Author per-agent shortlists in `agents/STATUS.md` per-agent sections (1-2 line "your hot triggers" callout) per audit §6.
- Demote `/timeline-report` + `/knowledge-agent` to milestone-only.
- Plan a 30-day re-measurement wake (Agent 0 future summon, not this TODO — `/schedule` candidate).

**Exit criterion:** every brotherhood agent reading CLAUDE.md or their STATUS section sees a tractable list, not a 21-row table.

---

## §7 Risk surface

- **R1: Phase 1 surfaces an upstream bug we can't fix.** Mitigation: file claude-mem issue with our log evidence; document a manual workaround (e.g., explicit observation flush); flag as carry-forward to a separate dependency-update wake. Phase 2 may pause until upstream lands.
- **R2: Phase 4 nag hook produces false positives that erode trust.** Mitigation: 30-day nag-only window before block-promotion; tune heuristics from real RTC data, not invented edge cases.
- **R3: Phase 6 trim leaves a real load-bearing skill behind.** Mitigation: 30-day re-measurement gate before declaring trim final; brotherhood may argue any demoted skill back into core via Congress motion.
- **R4: claude-mem repair fails entirely.** Mitigation: §5 question 4 already settled — this TODO's serial sequencing means failure here halts Phase 6, but Phases 3-5 still ship and provide useful telemetry independently. `/mem-search` just stays demoted in the trim.
- **R5: Cross-platform divergence widens (Codex falls further behind).** Mitigation: §8 audit framing — name the gap honestly in governance; don't pretend Codex meets the same bar.
- **R6: Agent 0 is both author and executor; loses neutrality on Decisions.** Mitigation: §5 recommendations are explicit; Hemanth ratifies before Phase 2+ executes. Phase 1 is diagnosis-only so neutrality risk is minimal there.
- **R7: Telemetry log (`skill-discipline.jsonl`) becomes its own discipline burden.** Mitigation: append-only, no parsing required by agents during normal flow; only Agent 0 reads it at re-measurement.

---

## §8 Files touched (cumulative across phases)

**New:**
- `agents/audits/claude_mem_persistence_diagnosis_2026-04-25.md` (Phase 1)
- `.claude/scripts/pre-rtc-checker.sh` (Phase 4)
- `.claude/scripts/memory-health.sh` (Phase 5)
- `.claude/telemetry/skill-discipline.jsonl` (Phase 4, append-only)

**Modified:**
- `agents/GOVERNANCE.md` (Phase 3, RTC contract section)
- `.claude/agents/commit-sweeper.md` (Phase 3)
- `.claude/scripts/session-brief.sh` (Phase 3 + 5)
- `.claude/settings.json` (Phase 4 + 5, hook wiring)
- `agents/VERSIONS.md` (Phase 3, contracts bump)
- `CLAUDE.md` (Phase 6, skill sheet trim + Active Fix TODOs row maintenance)
- `agents/STATUS.md` (Phase 6, per-agent shortlists; Agent 0 cursor maintenance throughout)
- claude-mem local config or settings (Phase 2, per Phase 1's remedy)

**Memory updates (Phase 6 close-out):**
- `feedback_plugin_skills_adopted.md` — narrow in place to reflect new tiered framing.
- New memory `feedback_memory_before_governance.md` capturing the audit's load-bearing sequencing rule.

---

## §9 Smoke / verification per phase

- **Phase 1:** read-only diagnosis; verification = peer-readable diagnosis doc cited from Phase 1 evidence.
- **Phase 2:** end-to-end query proof — `mem-search "stream stall"` returns Tankoban hits sourced from Tankoban observations.
- **Phase 3:** next sweep produces summary with skill provenance per RTC.
- **Phase 4:** synthetic RTC missing `Skills invoked:` triggers nag warning visibly; telemetry log records the fire.
- **Phase 5:** corrupting the local claude-mem state (e.g., empty observations) makes the SessionStart sentinel print degraded warning; restoring makes it silent.
- **Phase 6:** 30-day re-measurement Agent 0 future wake counts skill mentions vs the audit's baseline; reports compliance delta.

---

## §10 Honest unknowns

- Whether claude-mem's Bun worker startup failure (issue #565) is the actual root cause or a downstream symptom.
- Whether the `summaryStored=null` failure mode is reproducible on demand or session-state-dependent.
- Whether Anthropic's `Stop` hook supports the inspection surface needed for Phase 4 (audit cites the docs but I haven't verified the implementation against our use case).
- Whether trimming to 6 core skills preserves the "even 1% chance" discipline that the `superpowers:using-superpowers` skill demands. Phase 6 may force a sub-decision.
- Whether Phase 5's degraded-mode sentinel will see false-positives from claude-mem worker not-yet-warm states immediately post-startup.

---

## §11 Rollback

Each phase is independently revertable:
- Phase 3 governance reverts via VERSIONS.md decrement + commit-sweeper revert.
- Phase 4/5 hooks revert via `.claude/settings.json` removal.
- Phase 6 reverts by un-trimming CLAUDE.md.

The one phase whose changes touch external state is Phase 2 (claude-mem config). Rollback = restore prior `~/.claude-mem/settings.json` snapshot (taken in Phase 1).

---

## §12 Memory close-out tasks (Phase 6 exit)

- Narrow `feedback_plugin_skills_adopted.md` in-place: tier framing, not skill list (skill list lives in CLAUDE.md).
- Add new memory `feedback_memory_before_governance.md` capturing the audit's load-bearing sequencing rule.
- Audit `MEMORY.md` for stale "always run mem-search" assertions if any exist post-trim.

---

## §13 Cursor (this row maintained by Agent 0)

- Phase 1 (diagnose): ✅ **CLOSED 2026-04-25 ~21:35** — diagnosis audit at `agents/audits/claude_mem_persistence_diagnosis_2026-04-25.md`.
- Phase 2 (restore memory): ✅ **CLOSED 2026-04-25 ~22:30** — Option A install + reboot; observations table now non-empty; `/api/search/observations?query=tankoban` returns 2 hits. See [diagnosis §10](agents/audits/claude_mem_persistence_diagnosis_2026-04-25.md).
- Phase 3 (RTC provenance contract): ✅ **CLOSED 2026-04-25 ~23:00** — 5 files shipped: GOVERNANCE.md Rule 11 amended with `Skills invoked:` field + non-trivial threshold; CONTRACTS.md gains new "Skill Provenance in RTCs" §; commit-sweeper.md regex extended for the new field + non-trivial detection + final-report missing-field tracker; session-brief.sh REQUIRED SKILL TRIGGERS card gets RTC PROVENANCE block; VERSIONS.md bumped contracts-v2 → contracts-v3 with changelog row. Trivial vs non-trivial threshold codified: ≥1 file under `src/` or `native_sidecar/src/` OR ≥30 lines changed cumulative.
- Phase 4 (pre-RTC checker hook): ✅ **CLOSED 2026-04-25 ~23:30** — `.claude/scripts/pre-rtc-checker.sh` (~150 LOC bash) wired to `Stop` hook in `.claude/settings.json` with 5s timeout; nag-only mode (always exits 0); detects new RTC lines in working-tree chat.md via `git diff HEAD`; classifies non-trivial via src/ touch OR ≥30 LOC; emits single system-reminder warning per turn listing all offending tags; appends one JSONL row per offense to `.claude/telemetry/skill-discipline.jsonl` for 30-day re-measurement. End-to-end verification GREEN — fired against live working tree, caught all 12 pre-existing non-trivial RTCs missing the field, wrote 12 telemetry rows, did NOT false-nag the Phase 3 RTC (positive case implicit-verified). Failure modes: any error path exits 0 silently; hook never blocks a turn.
- Phase 5 (memory-degraded sentinel hook): ✅ **CLOSED 2026-04-25 ~23:45** — `.claude/scripts/memory-health.sh` (~50 LOC bash) probes claude-mem `/api/stats` + `/api/corpus` endpoints; exits 0 healthy / 1 degraded; reports one-line state to stdout. Wired into `.claude/scripts/session-brief.sh` so SessionStart card auto-demotes the `/mem-search` rule line + prints a "MEMORY DEGRADED" banner when worker absent / observations:0 / corpora empty. Verified GREEN against live worker — currently DEGRADED (corpora empty), banner + auto-demote both fire correctly. Healthy-path silent (will verify on next wake post-corpus-build). Self-healing: when claude-mem state recovers, sentinel goes silent and rule re-promotes automatically.
- Phase 6 (trim 21-skill sheet + per-agent shortlists): ✅ **CLOSED 2026-04-25 ~23:50** — `CLAUDE.md` "Required Skills & Protocols" rewritten in tiered format: **Tier 1 Core Mandatory (6)** = `/brief`, `/superpowers:verification-before-completion`, `/simplify`, `/build-verify`, `/superpowers:requesting-code-review`, `/superpowers:systematic-debugging`. **Tier 2 Conditional (13)** = `/security-review`, `/superpowers:brainstorming`, `/superpowers:writing-plans`, `/superpowers:executing-plans`, `/superpowers:receiving-code-review`, `/claude-mem:mem-search`, `/claude-mem:smart-explore`, `/superpowers:dispatching-parallel-agents`, `/superpowers:subagent-driven-development`, `/superpowers:test-driven-development`, `/example-skills:skill-creator`, `/superpowers:writing-skills`, `/example-skills:mcp-builder`. **Tier 3 Milestone-only (2)** = `/claude-mem:timeline-report`, `/claude-mem:knowledge-agent`. Memory `feedback_plugin_skills_adopted.md` narrowed in-place to tier framing. New "Per-agent skill-trigger shortlists" section in `agents/STATUS.md` covers all 8 agents. Re-measurement criterion in new memory `feedback_skill_discipline_remeasurement.md` — trigger = telemetry JSONL ≥30 entries OR calendar 2026-05-25, whichever first.

---

## §14 Honest scope note

This TODO does not solve "agent powerup" as a category. It solves the specific failure modes the audit surfaced: missing memory persistence + missing provenance + reminder fatigue + over-broad mandatory framing. Other powerup vectors remain separate scope.

If Phase 1 surfaces that claude-mem is structurally unfit for Tankoban's use case, this TODO is honest about replacing it. The brotherhood is not married to any specific tool; we're committed to working memory.
