# Audit - skill discipline - 2026-04-25

By Agent 7 (Codex). For Hemanth and Agent 0.
Reference comparison: Anthropic Claude Code skills/hooks docs, claude-mem docs/releases/issues, LangChain/CrewAI/AutoGen guardrail patterns.
Scope: Why the brotherhood's adopted skill/plugin discipline is not behaving as a 24/7 always-on system, with claude-mem as the priority case study. Out of scope: changing governance, patching plugin code, rebuilding corpora, or editing `src/`.

This audit is evidence-first. Observations are backed by repo files, chat history, git history, claude-mem local state, local claude-mem HTTP/API responses, or cited web docs. Hypotheses are explicitly labeled and left for Agent 0 / Hemanth to ratify or reject.

## §0 Methodology + scope

### Observation

- Policy sources read in full:
  - `CLAUDE.md:85-138`
  - `agents/GOVERNANCE.md:284-310`
  - `agents/audits/README.md:1-80`
  - `C:\Users\Suprabha\.claude\projects\c--Users-Suprabha-Desktop-Tankoban-2\memory\feedback_plugin_skills_adopted.md:1-91`
- Enforcement/config sources read:
  - `.claude/settings.json:1-34`
  - `.claude/scripts/session-brief.sh:60-79`
  - `.claude/agents/commit-sweeper.md:2-106`
  - `.claude/agents/prompt-architect.md`
  - `.claude/commands/{brief,build-verify,commit-sweep,repo-health,rotate-chat}.md`
- Empirical corpus read:
  - `agents/chat.md` plus `agents/chat_archive/2026-04-24_chat_lines_8-5253.md`, `agents/chat_archive/2026-04-20_chat_lines_8-3978.md`, `agents/chat_archive/2026-04-18_chat_lines_8-4038.md`
  - `git log --since=2026-04-15`
- Memory/meta-discipline sources read:
- `C:\Users\Suprabha\.claude\projects\c--Users-Suprabha-Desktop-Tankoban-2\memory\MEMORY.md:67-84,116`
  - `feedback_evidence_before_analysis.md`
  - `feedback_directive_lives_in_files.md`
  - `feedback_self_service_execution.md`
  - `feedback_audit_validation_same_turn.md`
  - `feedback_decision_authority.md`
  - `feedback_plan_discipline.md`
  - `feedback_no_human_days_in_agentic.md`
- claude-mem local state inspected:
  - `C:\Users\Suprabha\.claude-mem\settings.json:2-48`
  - `C:\Users\Suprabha\.claude-mem\claude-mem.db` via SQLite queries
  - `C:\Users\Suprabha\.claude-mem\logs\claude-mem-2026-04-23.log`
  - `C:\Users\Suprabha\.claude-mem\logs\claude-mem-2026-04-25.log`
  - `http://127.0.0.1:37777/api/corpus`
  - `http://127.0.0.1:37777/api/search/observations?query=tankoban&limit=5`
  - `http://127.0.0.1:37777/api/search/prompts?query=tankoban&limit=5`
- External references kept to a minority share of the work:
  - Anthropic Claude Code skills docs
  - Anthropic Claude Code hooks docs
  - claude-mem README/releases/issues
  - LangChain/CrewAI/AutoGen enforcement docs

### Observation

- Sample size for the quantitative pass:
  - 176 `READY TO COMMIT` lines across live chat + 3 archives
  - 116 code-touch RTCs
  - 275 commits since 2026-04-15
- Measurement rule:
  - "Explicit fire" means the skill name is literally present in the RTC line or adjacent ship prose.
  - "Opportunity" means the work shape matched the rule as written in `CLAUDE.md`.
- This undercounts true usage when agents run a skill but do not record it.
- That undercount is not noise to ignore. It is part of the system defect, because the repo currently cannot distinguish "skill ran but was unrecorded" from "skill was skipped."

### Observation

- Cross-platform constraint for this wake:
  - I am in Codex, not Claude Code.
  - I could not literally invoke `Skill`-tool slash skills from this environment.
  - For the claude-mem case study I used the running claude-mem worker's local HTTP surface instead: `/api/search/*` and `/api/corpus`.
- This is itself evidence for §8: the brotherhood wants one discipline system across Claude Code and Codex, but the available tool surface is not identical.

## §1 Skill-trigger matrix (the system as designed)

### Observation

Meta-rule outside the 21-count:

| Meta-skill | Trigger | Stated purpose | Expected evidence |
|---|---|---|---|
| `superpowers:using-superpowers` | Every wake | "if even 1% chance a skill applies, invoke it" (`CLAUDE.md:87,92`) | Either explicit ship-post mention or reliable downstream skill usage |

The adopted 21-skill system from `feedback_plugin_skills_adopted.md:7-87` and `CLAUDE.md:85-138`:

| # | Skill | Trigger as written | Expected evidence in RTC / ship post | Current observability risk |
|---|---|---|---|---|
| 1 | `/brief` | Every wake (`CLAUDE.md:92`, `session-brief.sh:67-78`) | Session bootstrap note or explicit `/brief` mention | High: usually omitted from RTCs |
| 2 | `/superpowers:verification-before-completion` | Every RTC (`CLAUDE.md:96`) | Explicit skill name plus evidence references | Medium |
| 3 | `/simplify` | After non-trivial edit, before RTC (`feedback_plugin_skills_adopted.md:47-49`) | Explicit skill name or simplification note | Medium |
| 4 | `/build-verify` | Before RTC when `src/` or `native_sidecar/src/` touched (`CLAUDE.md:98`) | Explicit skill name plus build result | Low for build outcome, high for skill provenance |
| 5 | `/superpowers:requesting-code-review` | Self-review before RTC (`CLAUDE.md:99`) | Explicit skill name or self-review note | Medium |
| 6 | `/security-review` | Required on stream/torrent/sidecar/network/user-input paths (`CLAUDE.md:100`) | Explicit skill name or security note | Medium |
| 7 | `/superpowers:systematic-debugging` | First on bug/test failure/unexpected behavior (`CLAUDE.md:104`) | Explicit skill name before fix narrative | Low |
| 8 | `/superpowers:brainstorming` | Feature scoping / Congress / multi-tradeoff design (`session-brief.sh:72`, `feedback_plugin_skills_adopted.md:31-32`) | Explicit skill name or brainstorm artifact | Low |
| 9 | `/superpowers:writing-plans` | When authoring `~/.claude/plans/*.md` (`CLAUDE.md:109`) | Explicit skill name or plan-file note | Medium |
| 10 | `/superpowers:executing-plans` | When executing plan files (`CLAUDE.md:110`) | Explicit skill name or checkpoint note | Medium |
| 11 | `/superpowers:receiving-code-review` | On correction from Hemanth or Agent 7 audit (`feedback_plugin_skills_adopted.md:35-36`) | Explicit skill name or structured correction receipt | Very high |
| 12 | `/claude-mem:mem-search` | Before re-deriving prior work or digging chat archive (`CLAUDE.md:118`) | Explicit skill name or recalled prior result | Very high |
| 13 | `/claude-mem:smart-explore` | Structural code queries (`CLAUDE.md:119`) | Explicit skill name or AST-based exploration note | Very high |
| 14 | `/superpowers:dispatching-parallel-agents` | When branching into 2+ subagents (`feedback_plugin_skills_adopted.md:37-38`) | Explicit skill name or dispatch rationale | Very high |
| 15 | `/superpowers:subagent-driven-development` | When executing via Agent() dispatch (`feedback_plugin_skills_adopted.md:39-40`) | Explicit skill name | Very high |
| 16 | `/superpowers:writing-skills` | When authoring a Tankoban skill (`feedback_plugin_skills_adopted.md:41`) | Explicit skill name | Very high |
| 17 | `example-skills:skill-creator` | When authoring a Tankoban skill (`feedback_plugin_skills_adopted.md:21-23`) | Explicit skill name | Very high |
| 18 | `example-skills:mcp-builder` | When building an MCP server (`feedback_plugin_skills_adopted.md:21-22`) | Explicit skill name | Very high |
| 19 | `/claude-mem:timeline-report` | Post big ship (`CLAUDE.md:137`) | Explicit skill name or generated report | Very high |
| 20 | `/claude-mem:knowledge-agent` | When a corpus is ripe (`CLAUDE.md:138`) | Explicit skill name or corpus/brain commission | Very high |
| 21 | `/superpowers:test-driven-development` | Narrowly for `tankoban_tests` pure-logic work (`feedback_plugin_skills_adopted.md:59,83`) | Explicit skill name or test-first narrative | Medium |

### Observation

- The repo's designed system is not merely "encourage good habits."
- It is a hard trigger map:
  - `CLAUDE.md:87` says every agent honors the listed skill invocations at the listed triggers.
  - `session-brief.sh:67-77` repeats the high-frequency trigger card at SessionStart.
- The design goal is already explicit 24/7 invocation. The audit question is therefore not "should the brotherhood use skills more?" but "why is the designed enforcement surface failing to produce consistent behavior?"

## §2 Empirical fire-rate per skill per agent

### Observation

Topline numbers from chat history:

| Metric | Count |
|---|---:|
| RTC lines reviewed | 176 |
| Code-touch RTCs | 116 |
| Code-touch RTCs with build evidence in RTC text | 96 |
| Code-touch RTCs explicitly naming `/build-verify` in the RTC line itself | 1 |
| Security-review-required RTCs | 57 |
| Security-review-required RTCs explicitly naming `/security-review` | 9 |
| Debug-like RTCs | 168 |
| Debug-like RTCs explicitly naming `/superpowers:systematic-debugging` | 2 |

Interpretation:

- The gap is real.
- It is large.
- It is not uniform.
- Build outcomes are often preserved.
- Skill provenance is usually not preserved.

### Observation

Explicit skill mentions across all chat text examined, not only RTC lines:

| Skill | Explicit mentions in live chat + 3 archives |
|---|---:|
| `/simplify` | 21 |
| `/security-review` | 19 |
| `/superpowers:requesting-code-review` | 16 |
| `/superpowers:verification-before-completion` | 14 |
| `/superpowers:systematic-debugging` | 6 |
| `/build-verify` | 5 |
| `/brief` | 3 |
| `/superpowers:writing-plans` | 2 |
| `/superpowers:executing-plans` | 2 |
| `/superpowers:using-superpowers` | 2 |
| `/claude-mem:mem-search` | 0 |
| `/claude-mem:smart-explore` | 0 |
| `/superpowers:brainstorming` | 0 |
| `/claude-mem:timeline-report` | 0 |
| `/claude-mem:knowledge-agent` | 0 |

### Observation

Explicit skill mentions in commit messages since 2026-04-15 are even rarer:

| Skill / marker | Count in 275 commits |
|---|---:|
| `/simplify` | 6 |
| `/security-review` | 5 |
| `/superpowers:requesting-code-review` | 4 |
| `/superpowers:verification-before-completion` | 3 |
| `/build-verify` | 3 |
| `/brief` | 3 |
| `/superpowers:systematic-debugging` | 1 |
| `/claude-mem:mem-search` | 0 |
| `/claude-mem:smart-explore` | 0 |
| `build ok` | 45 |
| `build green` | 11 |
| `build_check.bat` | 42 |

Interpretation:

- The git log preserves "work was built" much more often than "the required skill was invoked."
- This is a direct provenance problem, not just a usage problem.

### Observation

Per-agent RTC pattern, reduced to the most decision-relevant fields:

| Agent | RTC total | Code-touch RTCs | Build evidence | Security-required RTCs | Explicit `/security-review` | Explicit `/verification-before-completion` | Explicit `/simplify` | Explicit `/requesting-code-review` | Explicit `/systematic-debugging` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Agent 0 | 38 | 5 | 1 | 0 | 0 | 0 | 0 | 0 | 0 |
| Agent 1 | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 3 | 0 |
| Agent 2 | 9 | 8 | 7 | 0 | 1 | 0 | 1 | 1 | 1 |
| Agent 3 | 39 | 30 | 27 | 12 | 6 | 4 | 6 | 5 | 1 |
| Agent 4 | 44 | 39 | 34 | 29 | 0 | 0 | 0 | 0 | 0 |
| Agent 4B | 23 | 19 | 12 | 9 | 0 | 0 | 0 | 0 | 0 |
| Agent 5 | 9 | 6 | 6 | 1 | 1 | 1 | 1 | 1 | 0 |
| Agent 7 / Agent 7 (Codex) combined | 9 | 4 | 2 | 3 | 0 | 0 | 0 | 0 | 0 |

Read carefully:

- This is not a leaderboard.
- It is a visibility map.
- A low explicit count can mean "skill was skipped," "skill was run but not recorded," or both.
- The system currently has no way to tell which one happened.

### Observation

The positive case exists. When agents choose to make skill usage legible, the ship post becomes dramatically clearer.

Representative example 1:

- `agents/chat.md:1038-1050` contains a clean "Rules honored this wake" block and a full "Skills invoked" list:
  - `/superpowers:using-superpowers`
  - `/brief`
  - `/superpowers:systematic-debugging`
  - `/superpowers:writing-plans`
  - `/superpowers:executing-plans`
  - `/simplify`
  - `/build-verify`
  - `/security-review`
  - `/superpowers:verification-before-completion`

Representative example 2:

- `agents/chat.md:1251-1253` records:
  - `/superpowers:writing-plans`
  - `/superpowers:executing-plans`
  - `/build-verify`
  - `/simplify`
  - `/superpowers:verification-before-completion`

These posts prove the problem is not that the brotherhood lacks a language for skill evidence.
The problem is that the language is optional and therefore sporadic.

### Observation

The gap is largest on the memory side:

- Zero explicit `mem-search` mentions across all chat history examined.
- Zero explicit `smart-explore` mentions across all chat history examined.
- Zero explicit `timeline-report` mentions.
- Zero explicit `knowledge-agent` mentions.

Given that the brotherhood repeatedly revisits old bugs, chat archives, prior audits, and prior design decisions, those zeros are too clean to be dismissed as chance.

## §3 Misfire root-cause taxonomy

### Observation

Root-cause buckets requested by Hemanth were A-H. The evidence pass supports keeping that taxonomy, with one addition:

- I. Telemetry blindness

This ninth bucket is needed because a large share of the observed failure is not purely "didn't run" but "could have run and the system still cannot prove it."

### Observation

Taxonomy summary:

| Cause | What it means here | Evidence strength | Minimum evidenced footprint |
|---|---|---|---|
| A. Trigger ambiguity | Agents do not consistently map real work shapes onto the trigger phrases | High | 95 code-touch RTCs with build evidence but no `/build-verify` naming; 166 debug-like RTCs without `/systematic-debugging` naming |
| B. Skill heaviness perception | Skill feels too heavy for the case | Medium | Strongly suggested by near-zero use of heavy process skills outside a few ritualized posts |
| C. No memory of the requirement | Session-start reminder is forgotten mid-wake | High | Only SessionStart/UserPromptSubmit hooks configured; no mid-turn or pre-RTC enforcement |
| D. No corpus / empty results | Memory cannot pay off because little or nothing is stored | Very high | 56 Tankoban sessions in DB, 0 observations, 0 session summaries, 0 corpora |
| E. Tool-discovery friction | 21-skill map is too large to scan in motion | Medium-high | Dense SessionStart card plus no contextual recommender |
| F. No feedback loop | System does not reward or punish skill use consistently | Very high | RTC format does not require a `Skills invoked:` field; commit-sweeper ignores skill provenance |
| G. Plugin-environment mismatch | Codex and Claude Code do not share the same skill surface | Very high | claude-mem issue #1270 plus this wake's actual Codex surface |
| H. Cost/latency aversion | Tools have earned a reputation for delay or fragility | Medium-high | claude-mem log failures/timeouts and recent upstream regressions |
| I. Telemetry blindness | System cannot tell whether a skill fired | Very high | `CLAUDE_MEM_SKIP_TOOLS` excludes `Skill` and `SlashCommand`; chat/git rarely record usage |

### Observation

Cause A: Trigger ambiguity is real.

Evidence:

- `CLAUDE.md:96-104` expresses high-level triggers in natural language:
  - "Every RTC"
  - "Before RTC when `src/` or `native_sidecar/src/` touched"
  - "Any bug / test failure / unexpected behavior"
- These are understandable to a human but still classification-heavy in the moment.
- Example of the ambiguity consequence:
  - 96 code-touch RTCs included build evidence.
  - Only 1 code-touch RTC explicitly named `/build-verify` in the RTC line.
  - Either the skill ran but was not recorded, or the work got built without passing through the formal skill ritual.
  - The current system cannot distinguish those cases.
- The same pattern is sharper for debugging:
  - 168 RTCs were clearly bugfix/diagnostic/debug/smoke shaped.
  - 2 explicitly named `/superpowers:systematic-debugging`.

### Observation

Cause C: reminder fatigue is designed in.

Evidence:

- `.claude/settings.json:6-28` configures only two hooks:
  - `SessionStart`
  - `UserPromptSubmit`
- The SessionStart hook only prints a reminder card:
  - `.claude/scripts/session-brief.sh:67-77`
- There is no:
  - `PreToolUse` policy gate
  - `PostToolUse` verifier that nudges skill use
  - `Stop`/pre-RTC checker
  - agent hook that inspects the pending RTC body
- Anthropic's hooks docs explicitly support blocking/decision hooks and agent hooks:
  - hooks can deny or block actions ([Claude Code hooks docs](https://code.claude.com/docs/en/hooks), lines 436, 617-618)
  - agent hooks can investigate files or outputs before deciding ([Claude Code hooks docs](https://code.claude.com/docs/en/hooks), lines 2111-2119)
- Tankoban is using the weakest possible enforcement surface: a startup reminder plus honor system.

### Observation

Cause D: claude-mem currently has almost no chance to reinforce itself.

Evidence from local state:

- `C:\Users\Suprabha\.claude-mem\settings.json:20-24,47-48`
  - data dir present
  - mode set to `code`
  - Chroma enabled
- SQLite state on 2026-04-25:
  - `sdk_sessions_total = 71`
  - `user_prompts_total = 653`
  - `observations_total = 0`
  - `session_summaries_total = 0`
  - `Tankoban 2 sessions = 56`
  - `Tankoban 2 observations = 0`
  - `Tankoban 2 summaries = 0`
- Recent Tankoban sessions in `sdk_sessions` have:
  - `memory_session_id = null`
  - `prompt_counter = 0`
  - `worker_port = null`
- Local corpus state:
  - `C:\Users\Suprabha\.claude-mem\corpora` contains 0 files
  - `GET http://127.0.0.1:37777/api/corpus` returned `[]`
- Local search behavior:
  - `GET /api/search/observations?query=tankoban&limit=5` returned `No observations found matching "tankoban"`
  - `GET /api/search/prompts?query=tankoban&limit=5` returned prompt hits immediately

Interpretation:

- claude-mem is storing prompts.
- It is not storing the memory objects that make `mem-search`, `timeline-report`, or corpus features pay off.
- This is not a perception problem. It is a concrete data absence problem.

### Observation

Cause H: claude-mem has recent reliability scars that would rationally reduce trust.

Repo-local evidence:

- `C:\Users\Suprabha\.claude-mem\logs\claude-mem-2026-04-23.log:289-293`
  - worker unavailable
  - tools fail until worker starts
- `...2026-04-23.log:353-354`
  - Chroma connection timeout
  - user prompt sync failed
- `...2026-04-23.log:348-364`
  - repeated session wall-clock-age aborts
  - summary completes with `summaryStored=null`
- `C:\Users\Suprabha\.claude-mem\logs\claude-mem-2026-04-25.log`
  - `summaryStored=null` appears 39 times on the day
  - prompt/session init keeps happening, but summaries still do not persist

Upstream evidence:

- claude-mem release `v12.3.4` says `v12.3.3` "broke SessionStart context injection" and was rolled back ([releases page](https://github.com/thedotmack/claude-mem/releases), lines 535-544).
- claude-mem issue #565 documents Bun/worker startup failure producing unusable hooks ([issue #565](https://github.com/thedotmack/claude-mem/issues/565), lines 233-279).

This does not prove every brotherhood agent consciously thinks "claude-mem is flaky."
It does prove the environment has recently produced exactly the kinds of failures that teach that lesson.

### Observation

Cause I: telemetry blindness is severe.

Evidence:

- `C:\Users\Suprabha\.claude-mem\settings.json:6`
  - `CLAUDE_MEM_SKIP_TOOLS = ListMcpResourcesTool,SlashCommand,Skill,TodoWrite,AskUserQuestion`
- Yet `claude-mem-2026-04-25.log` clearly records PostToolUse events such as:
  - `Skill(brief)` at lines 20, 873, 974, 3687, 5792
  - `AskUserQuestion` at lines 1630, 6322
  - `TodoWrite` dozens of times
- Because `Skill` and `SlashCommand` are in the skip list, claude-mem is structurally prevented from turning some of the most relevant brotherhood-discipline actions into memory observations.

Result:

- Even when a skill fires, claude-mem may be unable to preserve that as searchable evidence.
- This makes mem-search a poor auditor of skill discipline.
- It also means the repo currently lacks trustworthy telemetry for the exact surface it wants enforced.
- The mismatch is visible right now:
  - chat history records `/brief` only 3 times across the full sample
  - `claude-mem-2026-04-25.log` records `PostToolUse: Skill(brief)` 5 times in one day
  - even a known-running skill is being undercounted by the repo-facing evidence layer

### Hypothesized root causes (Agent 0 to validate)

- **Hypothesis -** The brotherhood's biggest failure is not agents consciously refusing the 21-skill system; it is that the system asks humans to remember a dense trigger map while providing almost no in-flow enforcement and almost no trustworthy telemetry. **Agent 0 to validate.**
- **Hypothesis -** `/superpowers:systematic-debugging`, `/superpowers:brainstorming`, and claude-mem memory tools are skipped more often than quality-gate skills because they feel like "extra ceremony" rather than "the thing that lets me ship." **Agent 0 to validate.**
- **Hypothesis -** Once claude-mem stopped returning useful memory, the brotherhood shifted back to grep/chat-archive/manual re-derivation, and the discipline lost its reinforcement loop. **Agent 0 to validate.**
- **Hypothesis -** The current 21-skill list is above the brotherhood's working-memory budget for everyday wakes; a smaller "core 8" would likely achieve higher real compliance than the current full mandatory framing. **Agent 0 to validate.**

## §4 Deep-dive: claude-mem (Hemanth-flagged priority)

### Observation

What claude-mem claims to provide:

- GitHub README:
  - "captures everything Claude does during your coding sessions" ([README](https://github.com/thedotmack/claude-mem), lines 513-517)
- Release notes:
  - corpus tools build a queryable brain from observation history ([releases](https://github.com/thedotmack/claude-mem/releases), turn1view1 lines 789-804 and turn1search1 snippet)
  - validated flow is `PostToolUse` -> queue -> worker SDK call -> `<observation>` XML -> `observations` table -> Chroma sync ([releases](https://github.com/thedotmack/claude-mem/releases), lines 797-798)

### Observation

What Tankoban's local claude-mem instance currently provides:

- Prompts: yes
- Sessions: yes
- Observations: no
- Session summaries: no
- Corpora: no
- Observation search results: no

That is the entire case study in one line:

- The brotherhood adopted `mem-search` as a recall primitive.
- The local memory store contains none of the records that make recall possible.

### Observation

Direct local evidence:

1. Database state

- `sdk_sessions_total = 71`
- `user_prompts_total = 653`
- `observations_total = 0`
- `session_summaries_total = 0`
- `Tankoban 2 sessions = 56`
- `Tankoban 2 observations = 0`
- `Tankoban 2 summaries = 0`

2. Corpus state

- `C:\Users\Suprabha\.claude-mem\corpora` is empty.
- `GET /api/corpus` returns `[]`.
- Upstream code stores corpora as files under `~/.claude-mem/corpora`:
  - `CorpusStore.ts:14,20,57`

3. Search state

- `GET /api/search/observations?query=tankoban&limit=5` returns no results.
- `GET /api/search/sessions?query=tankoban&limit=5` returns no results.
- `GET /api/search/prompts?query=tankoban&limit=5` returns 5 prompt hits immediately.
- This means the store is not globally dead. It is selectively failing on the memory-bearing layers.

4. Summary state

- `claude-mem-2026-04-25.log` contains 39 occurrences of `summaryStored=null`.
- Example:
  - lines 1322-1327 request summary twice and complete with `summaryStored=null`
- This directly matches the empty `session_summaries` table.

5. Historical failure state

- `claude-mem-2026-04-23.log:348-364`
  - sessions exceed wall-clock age limit
  - summarize completes with `summaryStored=null`
- `...:405-415`
  - observation messages enqueue
  - session age limit aborts them

### Observation

Tankoban-specific conclusion:

- claude-mem is not failing because the brotherhood forgot to use it.
- It is failing first because the local Tankoban memory instance is not producing searchable Tankoban observations or summaries.
- Under those conditions, a rational agent gets no payoff from `mem-search`.

### Observation

There is also a telemetry-policy mismatch inside claude-mem:

- `settings.json:6` excludes `Skill` and `SlashCommand` from stored-tool handling.
- Yet the brotherhood wants to audit skill discipline.
- Even if claude-mem were otherwise healthy, the skip list weakens its usefulness as a discipline auditor.

That does not by itself explain 0 observations.
It does explain why the system is blind to one of the most important behavioral surfaces.

### Hypothesized root causes (Agent 0 to validate)

- **Hypothesis -** The Tankoban claude-mem instance is partially alive: prompt/session intake works, but observation and summary generation are failing upstream of persistence, leaving the plugin present but behaviorally hollow. **Agent 0 to validate.**
- **Hypothesis -** The repeated `summaryStored=null` pattern means the brotherhood currently perceives claude-mem as installed, while the actual useful-memory path is effectively off. That mismatch is likely more damaging than a clean "plugin unavailable" state because it hides the real cause. **Agent 0 to validate.**
- **Hypothesis -** The skip list for `Skill` and `SlashCommand` was probably chosen to reduce noise, but in this repo it accidentally removes the most important telemetry needed to prove or disprove discipline. **Agent 0 to validate.**

## §5 Tier 1 - Hook/automation recommendations

These are phrased as recommendation candidates for Hemanth ratification and Agent 0 implementation.

### Observation-backed recommendation candidates

| Pri | Recommendation candidate | Why the evidence supports it | Effort |
|---|---|---|---|
| P0 | Add a pre-RTC checker hook that inspects pending RTC text for required evidence fields | Current system has only SessionStart/UserPromptSubmit hooks (`.claude/settings.json:6-28`). Missing-skill detection is not automated anywhere. | 1-2 summons, low LOC |
| P0 | Add contextual skill reminders on `UserPromptSubmit` or `Stop` rather than only at SessionStart | Reminder fatigue is built in now; Anthropic hooks support additional decision points and agent hooks ([hooks docs](https://code.claude.com/docs/en/hooks), lines 436, 617-618, 2111-2119). | 1 summon, low-medium LOC |
| P0 | Add a claude-mem health sentinel hook: if summaries stay null or corpora stay empty, print a prominent degradation warning | Local instance shows 39 `summaryStored=null` events on 2026-04-25 and zero observations/corpora. The system should not silently demand mem-search while memory is empty. | 1-2 summons, medium LOC |
| P1 | Shorten the SessionStart skill card to the few highest-frequency triggers and link to the full matrix | Current card is dense (`session-brief.sh:67-77`) and likely contributes to reminder fatigue. | <1 summon, tiny LOC |
| P1 | Add a `Skills invoked:` auto-template in ship posts / RTC drafting flows | Proven positive examples already exist in `agents/chat.md:1040-1050`. | <1 summon, doc/hook LOC |

### Advisory follow-up questions

- Consider whether the brotherhood wants the hook to be blocking or nag-only.
- Consider whether missing `/build-verify` should be allowed when build evidence is present but the skill name is absent, or whether provenance itself is the new requirement.
- Consider whether `mem-search` should be auto-suggested only when prompts include phrases like "did we solve this before" or whenever an agent opens `chat_archive`.

## §6 Tier 2 - Governance recommendations

### Observation-backed recommendation candidates

| Pri | Recommendation candidate | Why the evidence supports it | Effort |
|---|---|---|---|
| P0 | Add an explicit `skills invoked:` field to the RTC contract for non-trivial work | Positive examples show it works; current RTC regex and commit-sweeper ignore skill provenance (`commit-sweeper.md:36-60`). | 1 summon, small governance edit |
| P1 | Split the 21-skill system into `core mandatory` and `conditional optional` | High-frequency skills show some uptake; long-tail skills show near-zero observable use. | 1 summon, doc-only |
| P1 | Tailor a short skill shortlist per agent/domain instead of one universal mandatory sheet | Player, stream, library, governance, and Agent 7 audit wakes do not share the same dominant triggers. | 1-2 summons, doc-only |
| P1 | Sharpen trigger wording from abstract states to concrete behaviors | Example: replace "unexpected behavior" with "if you grep logs, run smoke, inspect a failing build, or describe a root cause before evidence, run systematic-debugging first." | 1 summon, doc-only |
| P2 | Require phase-boundary tools to preserve skill provenance when they sweep or summarize work | Right now commit-sweeper validates files and commits, not process discipline. | 1-2 summons, doc + small tool edit |

### Observation

If the brotherhood keeps the current 21-skill framing, the governance text should stop implying that SessionStart reminders alone constitute enforcement.

That is disproven by:

- explicit fire-rate data
- zero memory-tool uptake
- the absence of pre-RTC automation

## §7 Tier 3 - Per-skill rehab recommendations

### Observation-backed recommendation candidates

| Pri | Skill / cluster | Recommendation candidate | Evidence | Effort |
|---|---|---|---|---|
| P0 | `claude-mem:*` | Treat memory rehab as prerequisite, not nice-to-have. Fix observation/summary storage before enforcing mem-search discipline. | 56 Tankoban sessions, 0 observations, 0 summaries, 0 corpora, 39 `summaryStored=null` on 2026-04-25 | 2-4 summons, medium-high LOC outside this audit |
| P0 | `claude-mem:*` | Add a visible "memory degraded" mode that tells agents when mem-search is expected to be empty | Current system silently looks healthy while useful memory is absent | 1-2 summons |
| P1 | `/superpowers:systematic-debugging` | Create or adopt a lighter-weight variant for small bug hunts if the full ritual is being perceived as overhead | Only 2 explicit uses across 168 debug-like RTCs | 1-2 summons |
| P1 | `/superpowers:brainstorming` | Narrow the trigger to design/motion/scoping moments instead of leaving it as a general recommendation | Zero explicit mentions suggests weak trigger clarity | <1 summon |
| P1 | `/claude-mem:smart-explore` | Either prove it beats grep on Tankoban or demote it from mandatory rhetoric | Zero explicit uses; the repo already has entrenched grep habits | 1 summon for head-to-head study |
| P2 | `/claude-mem:timeline-report` and `/knowledge-agent` | Move them to milestone-only governance, not day-to-day mandatory framing | No usage evidence yet; that is acceptable if the trigger is genuinely rare | doc-only |

### Observation

claude-mem-specific rehab should probably happen in this order:

1. Restore observations and summaries.
2. Confirm observation search returns Tankoban results.
3. Build one Tankoban corpus.
4. Prime and query it.
5. Only then re-assert `mem-search` as a mandatory cross-session primitive.

Reversing that order guarantees more resentment than compliance.

## §8 Cross-platform note

### Observation

The brotherhood currently talks as if skill discipline is one platform-neutral system.
It is not.

Repo-side evidence:

- `.claude/settings.json:30-33` enables `superpowers` and `claude-mem` for Claude Code.
- The repo-local slash-command surface is only:
  - `brief.md`
  - `build-verify.md`
  - `commit-sweep.md`
  - `repo-health.md`
  - `rotate-chat.md`
- Many adopted skills are not repo-local commands at all. They live in plugin/tooling outside the repo.

External evidence:

- Anthropic docs say skills/slash commands are Claude Code concepts and can auto-load based on `SKILL.md` metadata ([skills docs](https://code.claude.com/docs/en/slash-commands), lines 116-171).
- claude-mem issue #1270 says Codex can read claude-mem via MCP today, but Codex lacks Claude Code-equivalent `PostToolUse` and `SessionStart` hooks for writing memory automatically ([issue #1270](https://github.com/thedotmack/claude-mem/issues/1270), lines 217-239).

Observed in this wake:

- Codex did not expose the same `Skill`-tool slash-command surface that Claude Code uses.
- I had to use local files and the claude-mem HTTP API directly.

Conclusion:

- "24/7 skills" is achievable inside one tool more easily than across mixed tools.
- For cross-tool discipline, the brotherhood needs:
  - either a lowest-common-denominator core
  - or explicit per-platform contracts
  - or both

## §9 Open questions for Hemanth

- Do you want a strict `core mandatory` set and a looser `conditional` set, or do you want to keep saying all 21 are mandatory?
- Is provenance itself a requirement, or is it enough that the work shape proves the skill probably ran?
- Should a missing `skills invoked:` field block RTC acceptance, or only generate a reminder?
- Do you want claude-mem repaired first before any governance tightening around `mem-search`, or do you want both done in parallel?
- Are you willing to accept a 1-2 second pre-RTC hook if it materially improves discipline compliance?
- Should Codex be held to the same literal skill-invocation standard as Claude Code when the tool surfaces differ?
- Do you want `smart-explore` and other zero-usage memory skills kept in the mandatory rhetoric until they show real Tankoban payoff?
- Do you want the 21-skill sheet trimmed to the historically paying set first, then re-expanded only after telemetry proves the additions are actually used?

## §10 References

### Internal

- `CLAUDE.md:85-138,198`
- `agents/GOVERNANCE.md:284-310,337-344,386-388`
- `agents/audits/README.md:1-80`
- `.claude/settings.json:1-34`
- `.claude/scripts/session-brief.sh:60-79`
- `.claude/agents/commit-sweeper.md:2-106`
- `agents/chat.md:1038-1052,1251-1253`
- `C:\Users\Suprabha\.claude\projects\c--Users-Suprabha-Desktop-Tankoban-2\memory\MEMORY.md:67-84,116`
- `C:\Users\Suprabha\.claude\projects\c--Users-Suprabha-Desktop-Tankoban-2\memory\feedback_plugin_skills_adopted.md:1-91`
- `C:\Users\Suprabha\.claude-mem\settings.json:2-48`
- `C:\Users\Suprabha\.claude-mem\logs\claude-mem-2026-04-23.log:289-364,405-415`
- `C:\Users\Suprabha\.claude-mem\logs\claude-mem-2026-04-25.log:1322-1327` and repeated `summaryStored=null` lines across the file
- `C:\Users\Suprabha\.claude\plugins\marketplaces\thedotmack\src\shared\paths.ts:33-71`
- `C:\Users\Suprabha\.claude\plugins\marketplaces\thedotmack\src\services\worker\knowledge\CorpusStore.ts:14-67`
- `C:\Users\Suprabha\.claude\plugins\marketplaces\thedotmack\src\services\worker\http\routes\CorpusRoutes.ts:27-33,124-128`
- `C:\Users\Suprabha\.claude\plugins\marketplaces\thedotmack\src\services\worker\http\routes\SearchRoutes.ts:22-44,49-57,94-116,256-318`

### External

- Anthropic / Claude Code skills docs: [https://code.claude.com/docs/en/slash-commands](https://code.claude.com/docs/en/slash-commands)
- Anthropic / Claude Code hooks docs: [https://code.claude.com/docs/en/hooks](https://code.claude.com/docs/en/hooks)
- claude-mem README: [https://github.com/thedotmack/claude-mem](https://github.com/thedotmack/claude-mem)
- claude-mem releases: [https://github.com/thedotmack/claude-mem/releases](https://github.com/thedotmack/claude-mem/releases)
- claude-mem issue #1270 (Codex platform support): [https://github.com/thedotmack/claude-mem/issues/1270](https://github.com/thedotmack/claude-mem/issues/1270)
- claude-mem issue #565 (Bun / worker startup failure): [https://github.com/thedotmack/claude-mem/issues/565](https://github.com/thedotmack/claude-mem/issues/565)
- LangChain guardrails docs: [https://docs.langchain.com/oss/python/langchain/guardrails](https://docs.langchain.com/oss/python/langchain/guardrails)
- CrewAI tasks/guardrails docs: [https://docs.crewai.com/en/concepts/tasks](https://docs.crewai.com/en/concepts/tasks)
- AutoGen intervention-handler tool-use docs: [https://microsoft.github.io/autogen/dev/user-guide/core-user-guide/cookbook/tool-use-with-intervention.html](https://microsoft.github.io/autogen/dev/user-guide/core-user-guide/cookbook/tool-use-with-intervention.html)

## Bottom line

### Observation

- The gap is real.
- It is large.
- It is not mainly a morality problem.
- It is a systems problem:
  - honor-system enforcement
  - sparse provenance
  - no pre-RTC gate
  - no mid-wake reminder
  - claude-mem storing prompts but not useful memory
  - cross-platform differences between Claude Code and Codex

### Advisory

- If Hemanth wants "24/7," the brotherhood needs fewer assumed habits and more executable enforcement.
- If Hemanth wants `claude-mem` specifically to become a superpower, memory capture must be repaired before memory invocation can become believable discipline.
