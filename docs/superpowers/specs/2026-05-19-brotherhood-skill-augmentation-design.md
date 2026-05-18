# Brotherhood Skill Augmentation — Design Spec

**Author:** Agent 0
**Date:** 2026-05-19
**Status:** DRAFT awaiting Hemanth review
**Arc identifier:** SKILL_AUGMENTATION_ARC (provisional)
**Wake budget:** brainstorm + spec only this wake; writing-plans + execution across 5-7 future wakes
**Companion artifacts:**
- Brainstorm session: this wake's chat transcript
- Implementation plan: TBD (next wake's `/superpowers:writing-plans` output → `docs/superpowers/plans/YYYY-MM-DD-brotherhood-skill-augmentation.md`)
- Memory references: [project_dev_control_bridge.md](../../memory/project_dev_control_bridge.md), [feedback_plugin_skills_adopted.md](../../memory/feedback_plugin_skills_adopted.md), [feedback_skill_discipline_remeasurement.md](../../memory/feedback_skill_discipline_remeasurement.md), [project_codex_substrate_live.md](../../memory/project_codex_substrate_live.md)

---

## Strategic Intent

Hemanth's directive (2026-05-18): *"I want you to use superpowers: brainstorm, planning, execution to comprehensively come up with new skills that would genuinely help our boys and implement them into our system."*

Two design constraints from the brainstorm:
1. **Comprehensive scope** — don't pre-trim. All identified pains get addressed.
2. **Don't overwhelm the boys** — SKILL_DISCIPLINE_FIX (2026-04-25) found that mandating 21 skills produced 0 explicit `/mem-search` invocations across 176 RTCs. Mandation is not adoption.

**Success criterion:** felt-quality. Boys do their jobs faster, Hemanth notices less. Background indicators (orphan-cleanup count, MEMORY.md size, Skills-invoked field completeness, autonomous smoke count, MCP collision count) are Agent 0's internal checks, not Hemanth-facing gates.

## Scope Summary

**Two parallel tracks:**

1. **Track A — Workflow skills**: ships 6 hooks + 10 universal skills + 2 specialist skills + 1 optional skill + sweeper race-fix. Agent 0 inline authoring (markdown files in `.claude/commands/` + bash/PowerShell helper scripts in `.claude/scripts/`).
2. **Track B — Dev-bridge expansion**: ships 4 new bridge layers (v1.3-v1.6) + 2 cross-cutting layers (v1.7 synthetic UI + v1.8 system state) + deeper v1.1/v1.2 coverage, totaling ~95 new tankoctl commands. Codex Trigger D commissions, one per agent domain.

**Phase shape** (rough — full sequencing in plan):
- Phase A: Hooks first (lowest cognitive load lands first)
- Phase B + Phase D: Workflow skills + bridge expansion in parallel
- Phase C: Specialist + optional skills
- Phase E: Surface integration (STATUS.md per-agent shortlists, CLAUDE.md tier list)

**Tier defaults for new skills:**
- Hook-fired where mechanical (zero cognitive load)
- Tier 2 conditional for workflow skills (use when applicable)
- Specialist skills on per-agent shortlists only
- NO Tier 1 mandatory at first; promote based on 30-day re-measurement telemetry

---

## Section 1 — What the boys get (Hemanth-facing)

Four buckets of help, plus 2 specialist skills and 1 optional skill.

### Bucket A — Smoke + UI control gets easier

- **4 new dev-bridge power-ups** for Agent 2 (books), Agent 3 (player, deeper), Agent 4B (sources), Agent 5 (library) — same shape as today's v1.1/v1.2 power-ups for Agent 1 + Agent 4
- **v1.7 synthetic UI interaction layer** (cross-cutting) — bridge can fire QPushButton::click() / QKeyEvent / setText programmatically; agents stop depending on pywinauto-mcp pixel-clicks for the cases where UIA fails on Qt custom widgets
- **v1.8 system state + introspection layer** (cross-cutting) — modal/window/focus awareness, settings + JsonStore + cache direct access, log tailing + correlation marks, sidecar deep state, theme introspection, perf counters, fault injection
- **`/mcp-lock`** — Rule 19 LANE LOCK as a tool, not honor-system. Claim/release the desktop via single command, refuses claim if held
- **`/smoke-package`** — when an agent runs a smoke, auto-creates the evidence directory, names PNGs to convention, captures sidecar + telemetry logs, writes stub evidence-md

### Bucket B — Commits + RTC stop dropping things

- **`/rtc <tag> <message>`** — scaffolds a fully-formed contracts-v3 RTC line; validates against current `git status` + sweeper regex BEFORE posting; auto-fills `Skills invoked: [...]` field from the session
- **Sweeper race-condition fix** — re-snapshot `chat.md` immediately before marker commit; prevents the BulkPackVerifier-class orphan from this wake
- **Skill-provenance hook** — PreToolUse on `chat.md` Edit; auto-detects which skills fired in the session and auto-fills the RTC field; replaces self-attestation

### Bucket C — Memory + state stays healthy

- **`/memory-trim`** — proposes MEMORY.md archive candidates by topic age + last-cited frequency
- **`/memory-write <type>`** — frontmatter scaffolding + auto-add to MEMORY.md index when a new memory file is created
- **Dashboard-drift hook** — at session start, auto-flags STATUS.md sections >7 days stale, CLAUDE.md decay markers, chat.md rotation overdue

### Bucket D — Planning + authoring stops re-typing

- **`/fix-todo-new <name>`** — scaffolds the ratified 14-section fix-TODO template
- **`/codex-trigger-d <spec-file>`** — packages a Codex Trigger D handoff in the established pattern
- **`/audit-skeleton`** — Agent 7-style audit shape skeleton (finding-ranked table, severity, repro, §5 ratification)
- **`/handoff-brief <agent-N>`** — mid-wake context handoff between agents (when work passes Agent A → Agent B during a single wake)
- **`/summon-from-todo-phase <todo> <phase>`** — drafts the summon prompt for the responsible agent of a TODO phase

### Specialist skills (per-agent shortlists only)

- **`/tdd-scaffold <class-name>`** — Agent 4 / 4B shortlist; scaffolds pure-logic primitive test file shape (GoogleTest + frozen-fixture pattern + MSVC link-dep-free target)
- **`/smoke-report`** — any execution agent; standardizes smoke output format (matrix + evidence + deferred + findings)

### Optional skill (never mandated)

- **`/hemanth-rewrite`** — judgmental Hemanth-language translator on a paragraph. Opt-in forever; agents don't see it unless they choose it.

---

## Section 2 — Observable changes (what Hemanth will notice)

**Less of:**
- Orphan housekeeping commits after Agent 0 sweeps (this week averaged 3-5/sweep; target <1)
- MCP collision messages in chat.md (lock is enforced, not honor-system)
- MEMORY.md overflow warnings (trim runs periodically, currently 27.4KB > 24.4KB limit)
- "What does this mean?" moments from technical jargon (hemanth-rewrite + simple-language discipline in skills)
- Agents waiting for Hemanth to open the app for diagnostic smokes (dev-bridges answer state queries directly)
- Re-typed RTC fields and plan/spec/TODO boilerplate

**Same amount of:**
- Visual-quality + taste judgment requests (HDR / subtitles / frame pacing) — that's Hemanth's domain
- §5 strategic ratification on new TODOs
- Product/strategic direction questions
- `build_and_run.bat` double-clicks

**More of:**
- Skills appearing in `Skills invoked:` RTC fields (auto-detected, not self-attested)
- Agent 2/3/4B/5 shipping smokes without Hemanth opening the app

---

## Section 3 — Background machinery (hooks, auto-fired)

Six hooks. Agent 0 sees these in his SessionStart brief; other agents don't see them unless they trigger one. Three EXTEND existing infrastructure; two are NEW; one is a small extension of existing repo-health logic.

1. **Pre-RTC field scaffolding** — PreToolUse hook on `agents/chat.md` Edit. Scaffolds missing contracts-v3 fields BEFORE the RTC is committed; warns on stale file references. Extends `.claude/scripts/pre-rtc-checker.sh`.
2. **Smoke-evidence auto-naming** — PreToolUse hook on Write/Edit of files in `agents/audits/smoke_evidence/`. Enforces `<UPPERCASE_FINDING>_<HHMMSS>.png` pattern; redirects bad paths; captures matching sidecar log + telemetry snapshot. NEW script at `.claude/scripts/smoke-evidence-rename.sh`.
3. **MEMORY.md size watch** — SessionStart hook. If MEMORY.md > 24.4KB, prepends `/memory-trim` to Agent 0's brief. Extends `.claude/scripts/memory-health.sh`.
4. **Dashboard drift flag** — SessionStart hook. Scans STATUS.md timestamps + CLAUDE.md decay markers; prints drift in the brief. Extends `.claude/scripts/session-brief.sh`.
5. **Skill-provenance auto-detect** — PreToolUse hook on `agents/chat.md` Edit. Reads session tool-use telemetry, auto-fills `Skills invoked: [...]` field in any RTC line being committed. NEW script at `.claude/scripts/skill-provenance-detect.sh`.
6. **Chat.md rotation watch** — SessionStart hook. If chat.md > 3000 lines or > 300KB, prepends `/rotate-chat` to Agent 0's brief. Extends `.claude/scripts/session-brief.sh`.

All hooks wire via `.claude/settings.json` in the existing hooks section.

---

## Section 4 — Phase shape (rough; full sequencing in plan)

### Phase A — Hooks first

- Extend `.claude/scripts/pre-rtc-checker.sh` to scaffold-before-post
- Extend `.claude/scripts/memory-health.sh` to MEMORY.md size watch
- Extend `.claude/scripts/session-brief.sh` to dashboard-drift flag + chat.md rotation watch
- NEW `.claude/scripts/smoke-evidence-rename.sh`
- NEW `.claude/scripts/skill-provenance-detect.sh`
- Wire all into `.claude/settings.json`
- Smoke: each hook fires correctly + doesn't break existing brief

### Phase B — Workflow skills (Agent 0 authors, markdown)

Order within phase, high-frequency first:
1. `/rtc` (consolidated rtc + rtc-precheck)
2. `/mcp-lock`
3. `/smoke-package`
4. Sweeper race-fix (in-place edit to `.claude/agents/commit-sweeper.md`)
5. `/memory-trim`
6. `/memory-write`
7. `/fix-todo-new`
8. `/codex-trigger-d`
9. `/audit-skeleton`
10. `/handoff-brief`
11. `/summon-from-todo-phase`

Each skill is 40-120 lines of markdown in `.claude/commands/`. Pattern follows the existing 6 brotherhood skills (`/brief`, `/commit-sweep`, etc).

### Phase C — Specialist + optional skills

- `/tdd-scaffold` (Agent 4 / 4B shortlist only)
- `/smoke-report` (any execution agent)
- `/hemanth-rewrite` (opt-in, never mandated)

### Phase D — Dev-bridge expansion (PARALLEL with Phase B + C)

Four Codex Trigger D commissions, one per agent domain, plus 2 cross-cutting commissions. Each follows the v1.1/v1.2 template:

1. **v1.3 — books domain, Agent 2 attribution.** Files: `src/devtools/DevControlServer.h` + `src/ui/MainWindow.{h,cpp}` + `src/ui/pages/BooksPage.{h,cpp}` + `src/ui/pages/books/BookReader.{h,cpp}` + EdgeTtsClient + `tools/tankoctl.cpp`.
2. **v1.4 — player domain (deeper), Agent 3 attribution.** Files: `src/devtools/DevControlServer.h` + `src/ui/MainWindow.{h,cpp}` + `src/ui/player/VideoPlayer.{h,cpp}` + `src/ui/player/SidecarProcess.{h,cpp}` + native_sidecar IPC + `tools/tankoctl.cpp`.
3. **v1.5 — sources domain, Agent 4B attribution.** Files: `src/devtools/DevControlServer.h` + `src/ui/MainWindow.{h,cpp}` + `src/ui/pages/TankorentPage.{h,cpp}` + `src/ui/pages/TankoLibraryPage.{h,cpp}` + `src/core/IndexerHealth.*` + `tools/tankoctl.cpp`.
4. **v1.6 — library domain, Agent 5 attribution.** Files: `src/devtools/DevControlServer.h` + `src/ui/MainWindow.{h,cpp}` + per-mode landing pages (`StreamPage.cpp`, `ComicsPage.cpp`, `BooksPage.cpp`, `VideosPage.cpp` library-section devSnapshot methods) + `src/ui/Theme.*` + `tools/tankoctl.cpp`.
5. **v1.7 — synthetic UI interaction (CROSS-CUTTING), Agent 0 attribution (Codex commissioned by Agent 0).** Files: `src/devtools/DevControlServer.h` + `src/devtools/UiInteractionDispatcher.{h,cpp}` (NEW) + `tools/tankoctl.cpp`.
6. **v1.8 — system state + introspection (CROSS-CUTTING), Agent 0 attribution (Codex commissioned by Agent 0).** Files: `src/devtools/DevControlServer.h` + `src/devtools/SystemIntrospection.{h,cpp}` (NEW) + per-domain devSnapshot extensions + `tools/tankoctl.cpp`.

The 4 domain commissions can fire concurrently because they touch different per-page files. Only the shared `MainWindow.cpp` dispatcher needs sequencing — solution: I pre-allocate the dispatcher namespace per domain BEFORE commissioning so Codex's edits don't collide. (Each domain's commands prepend their domain prefix in the dispatch chain, and the dispatcher is structurally additive only.)

The 2 cross-cutting commissions (v1.7 + v1.8) wait for at least 2 domain layers to land first so they can be tested against real domain state.

### Phase E — Surface integration

- Update `agents/STATUS.md` per-agent shortlists with new applicable skills (per-agent customization, not universal)
- Update `CLAUDE.md` "Required Skills & Protocols" section: add Tier 2 entries for the new workflow skills, add the new bridge layers to "Build Quick Reference"
- Retire any obsolete sections; merge orphan memory pointers
- Write `project_brotherhood_skill_augmentation_arc.md` memory documenting what shipped + how to extend

### Rough wake budget

1. **Phase A — 1 wake.** All 6 hooks ship in one wake; verify each independently.
2. **Phase B — 2-3 wakes.** 10 skills + sweeper race-fix; batch 3-4 per wake.
3. **Phase C — 1 wake.** 3 specialist + optional skills.
4. **Phase D — 2-3 wakes.** 4 domain commissions + 2 cross-cutting; some parallelism via Codex Trigger D.
5. **Phase E — 0.5 wake.** Cleanup integration (STATUS.md shortlists + CLAUDE.md tier list + memory pointers).

**Total: 5-7 wakes across ~1-2 weeks.**

---

## Section 5 — How we know it worked

### Primary signal — felt quality (Hemanth's call)

After 2-3 wakes post-arc completion, the brotherhood should feel less janky. Specific felt-quality observables:
- Fewer "you need to do X" reminders from Hemanth to agents
- Smokes happen without Hemanth opening the app for diagnostics
- RTC sweeps feel cleaner (less orphan tail)
- MEMORY.md overflow stops happening
- MCP collision warnings stop appearing in chat.md

### Background indicators (Agent 0 tracks; Hemanth doesn't need to)

1. **Orphan-cleanup commits per Agent 0 sweep** — pre-arc baseline: 3-5/sweep this week. Target post-arc: <1/sweep.
2. **MEMORY.md size at session start** — pre-arc: 27.4KB (over 24.4KB limit). Target: <24.4KB sustained without manual intervention.
3. **`Skills invoked: []` empty/missing in non-trivial RTCs** — pre-arc: allowed via self-attest. Target: 0 (hook auto-fills).
4. **Agent 2/3/4B/5 smokes shipped without Hemanth opening app** — pre-arc: rare. Target: regular.
5. **MCP collision events in chat.md** — pre-arc: honor-system, collisions happen. Target: 0 (lock skill enforces).

### Re-measurement gate

30 days from arc completion (estimate ~2026-06-18 if execution starts in next 1-2 wakes), Agent 0 runs a measurement pass:
- If felt-quality good + indicators corroborate: arc closed, skills stay
- If felt-quality ambiguous: consult indicators, identify skills not pulling weight, trim
- If felt-quality bad: roll back the additions that caused friction, re-scope

### Risk acknowledgment

The SKILL_DISCIPLINE_FIX audit found we'd previously made 21 skills mandatory and 0 were actually invoked. This arc could repeat that mistake despite the Tier 2 + hook-fired defaults. The 30-day re-measurement is the safety valve.

Specific risks to monitor:
1. **Hook friction** — if hook #1 (pre-RTC scaffolding) or hook #5 (skill-provenance) feel invasive, agents may start working around them. Mitigation: hooks are passive (warn/scaffold, don't block); env-var kill-switch for any agent who needs to bypass.
2. **Bridge command sprawl** — 95 new commands is a big surface. Discoverability matters. Mitigation: `tankoctl help` grouped by layer; per-agent shortlist in STATUS.md narrows the relevant subset for each agent.
3. **Dispatcher complexity** — `MainWindow::handleDevCommand()` is already a long if/else chain (~270 lines at v1.2). Adding 95 more commands risks readability. Mitigation: refactor to per-domain `dispatchCommand()` delegates in the per-domain page classes (additive; existing v1.0-v1.2 commands stay where they are; v1.3+ go through delegates).
4. **Codex commissioning concurrency** — 4 simultaneous Codex Trigger D commissions on a shared `MainWindow.cpp` could collide. Mitigation: Agent 0 pre-allocates dispatcher namespaces before commissioning; commissions only add to their pre-allocated region.

---

## Detailed catalog — Track A workflow skills (implementation detail)

### Hooks (Phase A)

**1. Pre-RTC field scaffolding** (extends `.claude/scripts/pre-rtc-checker.sh`)
- Trigger: PreToolUse hook on Edit/MultiEdit targeting `agents/chat.md`
- Behavior:
  - If the edit adds a line matching `READY TO COMMIT` regex but missing `Skills invoked:` field → inject `Skills invoked: [<auto-detected>]` from session telemetry
  - If `files:` field references files not in current `git status` → warn
  - If `files:` field includes files clean against HEAD → warn (stale)
- Files: `.claude/scripts/pre-rtc-checker.sh`, `.claude/settings.json`
- Telemetry: `.claude/telemetry/pre-rtc.jsonl` append-only log

**2. Smoke-evidence auto-naming** (NEW `.claude/scripts/smoke-evidence-rename.sh`)
- Trigger: PreToolUse hook on Write/Edit targeting `agents/audits/smoke_evidence/*`
- Behavior:
  - Validates filename matches `<UPPERCASE_FINDING>_<HHMMSS>.<ext>` pattern; suggests rename if not
  - On screenshot save: also captures sidecar_debug_live.log tail (last 200 lines) + telemetry snapshot + writes stub `evidence_<NAME>_<HHMMSS>.md` if it doesn't exist
- Files: `.claude/scripts/smoke-evidence-rename.sh`, `.claude/settings.json`

**3. MEMORY.md size watch** (extends `.claude/scripts/memory-health.sh`)
- Trigger: SessionStart hook
- Behavior: stat MEMORY.md; if > 24.4KB, prepend `MEMORY.md OVERFLOW — run /memory-trim` to the brief; if > 32KB, escalate to red banner
- Files: `.claude/scripts/memory-health.sh`

**4. Dashboard drift flag** (extends `.claude/scripts/session-brief.sh`)
- Trigger: SessionStart hook
- Behavior:
  - Scan STATUS.md per-agent section timestamps; flag any >7 days stale
  - Diff CLAUDE.md "Last successful smoke" + dashboard text against most recent commits; flag if mismatched
  - Print drift findings in brief preamble
- Files: `.claude/scripts/session-brief.sh`

**5. Skill-provenance auto-detect** (NEW `.claude/scripts/skill-provenance-detect.sh`)
- Trigger: PreToolUse hook on Edit/MultiEdit targeting `agents/chat.md`
- Behavior:
  - Read session's Skill tool-use log from `.claude/telemetry/`
  - Detect which skills fired this session
  - When chat.md edit adds an RTC line, auto-fill `Skills invoked: [<list>]` if missing
  - If self-attested list is wrong, warn (don't override — agent picks)
- Files: `.claude/scripts/skill-provenance-detect.sh`, `.claude/telemetry/skill-invocations.jsonl`

**6. Chat.md rotation watch** (extends `.claude/scripts/session-brief.sh`)
- Trigger: SessionStart hook
- Behavior: stat chat.md; if > 3000 lines or > 300KB, prepend `chat.md ROTATION DUE — run /rotate-chat` to brief
- Files: `.claude/scripts/session-brief.sh`

### Workflow skills (Phase B)

**`/rtc <tag> <message>`** — RTC scaffold (40-80 lines)
- Inputs: tag (e.g. "Agent 4, Bug X fix"), message (one-liner)
- Behavior:
  - Read current `git status` and `git diff --name-only HEAD`
  - Read session Skill-invocation log
  - Scaffold a contracts-v3 RTC line: `READY TO COMMIT - [<tag>]: <message> | Skills invoked: [<auto-list>] | files: <git-detected-files>`
  - Run dry-validation against `.claude/agents/commit-sweeper.md` regex
  - Print the scaffolded line to stdout; agent copies into chat.md (or uses `/rtc --append` to auto-append)
- File: `.claude/commands/rtc.md`

**`/mcp-lock <claim|release> [reason]`** — Rule 19 LANE LOCK (60-90 lines)
- Behavior:
  - On `claim`: read recent chat.md tail; if a `MCP LOCK` line exists without a matching `MCP LOCK RELEASED`, refuse with the held-by info
  - If clear: append `MCP LOCK — <agent> — <reason> — <timestamp>` to chat.md
  - On `release`: append `MCP LOCK RELEASED — <agent> — <timestamp>` to chat.md
  - `--peek` flag: report current lock state without changing
- File: `.claude/commands/mcp-lock.md`

**`/smoke-package <finding-name>`** — evidence bundle scaffolding (50-80 lines)
- Behavior:
  - Compute timestamp `HHMMSS`
  - Create `agents/audits/smoke_evidence/<FINDING>_<HHMMSS>/` directory
  - Capture: `out/sidecar_debug_live.log` (tail 500 lines if exists) → `sidecar.log`; `out/stream_telemetry.log` (tail 200) → `telemetry.log`; current Tankoban window screenshot via pywinauto-mcp → `screenshot.png`
  - Write stub `evidence_<FINDING>.md` with `## Smoke description` + `## Observations` + `## Verdict` sections
  - Print directory path to stdout
- File: `.claude/commands/smoke-package.md`

**`/memory-trim`** — MEMORY.md archive proposals (60-100 lines)
- Behavior:
  - Read MEMORY.md
  - For each pointer entry: parse the target file's frontmatter `name` + `metadata.type`; compute last-cited frequency (grep agents/chat.md + chat_archive/* for the slug)
  - Rank entries by (age × 1) + (last-cited-recency-decay × 2) → propose archive candidates
  - Print proposed archive list with rationale; agent confirms which to archive
  - On confirmation: `git mv` selected memory files to `memory/_archive/`; update MEMORY.md index; append entry to `memory/_archive/INDEX.md`
- File: `.claude/commands/memory-trim.md`

**`/memory-write <type>`** — new memory scaffold (40-70 lines)
- Inputs: type (user / feedback / project / reference)
- Behavior:
  - Prompt for memory `name` (kebab-case slug)
  - Scaffold frontmatter + body skeleton (with `**Why:**` + `**How to apply:**` lines for feedback/project type)
  - Write file to `~/.claude/projects/<project-slug>/memory/<type>_<name>.md`
  - Auto-add MEMORY.md index entry under appropriate section
- File: `.claude/commands/memory-write.md`

**`/fix-todo-new <name>`** — fix-TODO scaffold (40-60 lines)
- Inputs: name (UPPERCASE_TODO_NAME)
- Behavior: scaffold the ratified 14-section template (preamble + §1 strategic intent + §2 phase breakdown + §3 deliverables + §4 acceptance + §5 ratification questions + §6 ownership + §7 dependencies + §8 risks + §9 wake budget + §10 anti-patterns + §11 evidence + §12 close criteria + §13 carry-forward + §14 archive trigger) → `<NAME>_FIX_TODO.md` at repo root
- File: `.claude/commands/fix-todo-new.md`

**`/codex-trigger-d <spec-file>`** — Codex TD handoff scaffold (50-80 lines)
- Inputs: spec file path
- Behavior: package the Codex commission per `project_codex_substrate_live.md` pattern (spec body + verification checklist + memory pointers + attribution agent); print the ready-to-paste Codex prompt; provides expected commit shape for verification
- File: `.claude/commands/codex-trigger-d.md`

**`/audit-skeleton`** — Agent 7 audit shape (40-60 lines)
- Behavior: scaffold finding-ranked table + severity column + repro steps + §5 ratification + standard footer; output to `agents/audits/<topic>_<date>.md`
- File: `.claude/commands/audit-skeleton.md`

**`/handoff-brief <agent-N>`** — mid-wake handoff (50-80 lines)
- Inputs: target agent identifier
- Behavior: capture current session state (active TODOs, files dirty, pending RTCs, last 3 chat.md posts, current MCP lock state) into a brief block; output as pastable text for the receiving agent's prompt
- File: `.claude/commands/handoff-brief.md`

**`/summon-from-todo-phase <todo> <phase>`** — summon prompt drafter (40-70 lines)
- Inputs: TODO filename + phase identifier (e.g. "P3.1")
- Behavior: read the TODO, locate the phase row, identify the responsible agent + dependencies + relevant memory pointers; draft a summon prompt for the agent following Agent 8's drafting patterns
- File: `.claude/commands/summon-from-todo-phase.md`

### Sweeper race-fix (Phase B, in-place edit)

`.claude/agents/commit-sweeper.md`:
- Re-snapshot `agents/chat.md` BLOB immediately before staging the marker commit
- Diff the snapshot against the parse-time chat.md; if RTC lines were added during the sweep, halt-and-warn with a "race-condition orphan detected" report listing the missed RTCs
- Agent 0 then manually lifts the orphans under correct attribution before the next sweep

### Specialist + optional skills (Phase C)

**`/tdd-scaffold <class-name>`** — pure-logic test file shape (60-100 lines)
- Per-agent shortlist: Agent 4 + Agent 4B
- Inputs: class name (e.g. `StreamPackParser`)
- Behavior: scaffold `tests/core/<domain>/test_<class>.cpp` with GoogleTest boilerplate, frozen-fixture loading pattern, MSVC link-dep-free target wiring (CMakeLists.txt addition); template includes 1 stub passing test + 1 stub failing test for red-green-refactor warmup
- File: `.claude/commands/tdd-scaffold.md`

**`/smoke-report`** — smoke output formatter (50-80 lines)
- Inputs: smoke session context (smokes run + outcomes)
- Behavior: format as `## Smoke matrix` (S1/S2/.../Sn with verdicts), `## Evidence pointers`, `## Deferred items`, `## Discovered findings`; consistent across all agents
- File: `.claude/commands/smoke-report.md`

**`/hemanth-rewrite`** — opt-in Hemanth-language translator (40-60 lines)
- Inputs: paragraph (agent-facing or technical text)
- Behavior: rewrite per `feedback_simple_language.md` rules (lead with answer, short sentences, translate jargon, no markdown tables, numbered lists with descriptions); print rewritten version; agent decides whether to use
- File: `.claude/commands/hemanth-rewrite.md`

---

## Detailed catalog — Track B dev-bridge expansion (implementation detail)

All command names below are CLI subcommand form (kebab-case). Wire protocol uses snake_case (handled by tankoctl translation layer).

Total: ~95 new commands across 6 schema layers.

### v1.3 — Books-side power-up (Agent 2)

**Files:** `src/devtools/DevControlServer.h` (schema bump), `src/ui/MainWindow.{h,cpp}` (dispatcher), `src/ui/pages/BooksPage.{h,cpp}` (per-page snapshot + commands), `src/ui/pages/books/BookReader.{h,cpp}` (reader state), `src/core/tts/EdgeTtsClient.*` (TTS bridge), `tools/tankoctl.cpp` (CLI surface).

Commands (~15):
- `books-get-state` — current book + page + progress + reader open?
- `books-get-library` — book array with metadata
- `books-open-book <imdbId-or-path>` — open in reader
- `books-get-progress` — current read position
- `books-seek-page <n>` — jump to page
- `books-set-layout <single|double-page|columns>` — page layout
- `books-get-chapters` — TOC for current book
- `books-open-chapter <id>` — jump to chapter
- `books-tts-state` — playing? voice? speed? position?
- `books-tts-play` / `books-tts-pause` / `books-tts-resume`
- `books-tts-set-voice <voice>` / `books-tts-set-speed <speed>`
- `books-get-listen-state` — Listen button state, queued audio
- `dump-ui books` — full books page UI snapshot

### v1.4 — Player-side deeper (Agent 3)

**Files:** `src/devtools/DevControlServer.h`, `src/ui/MainWindow.{h,cpp}`, `src/ui/player/VideoPlayer.{h,cpp}`, `src/ui/player/SidecarProcess.{h,cpp}`, `src/ui/player/SubtitleOverlay.{h,cpp}`, `native_sidecar/src/main.cpp` (IPC), `tools/tankoctl.cpp`.

Commands (~20, on top of existing v1.0 player commands):
- `player-get-audio-tracks` — track list with codec + language + default flag
- `player-get-subtitle-tracks` — same shape for subs
- `player-select-audio-track <id>` / `player-select-subtitle-track <id>`
- `player-set-audio-delay <ms>` / `player-set-sub-delay <ms>`
- `player-set-sub-size <delta>` / `player-set-sub-position <y-offset>`
- `player-get-chapters` / `player-seek-chapter <id>`
- `player-set-volume <0-100>` / `player-set-speed <0.25-4.0>`
- `player-get-hud-state` — chips, time labels, focus indicator
- `player-get-decoder-stats` — drops, queue depth, decoder type
- `player-get-canvas-size` — pixel dimensions
- `player-screenshot <path>` — canvas PNG capture
- `player-simulate-seek-drag <position>` — synthetic QSlider event
- `sidecar-get-process-state` — alive? pid? memory?
- `sidecar-get-current-stream-info` — codec, fps, bitrate, resolution, hwaccel
- `sidecar-get-decoder-queue` / `sidecar-get-render-queue` — queue depths
- `sidecar-restart` — graceful diagnostic restart
- `subs-get-active-track` / `subs-get-positioning` / `subs-get-fonts-loaded`
- `osd-get-state` — overlay state

### v1.5 — Sources-side (Agent 4B)

**Files:** `src/devtools/DevControlServer.h`, `src/ui/MainWindow.{h,cpp}`, `src/ui/pages/TankorentPage.{h,cpp}`, `src/ui/pages/TankoLibraryPage.{h,cpp}`, `src/core/IndexerHealth.*` (if exists, or create), `tools/tankoctl.cpp`.

Commands (~10):
- `sources-search-tankorent <query>` — fire a Tankorent search
- `sources-search-tankolibrary <query>` — fire a TankoLibrary search
- `sources-get-indexer-health` — per-indexer status, response time, error rate
- `sources-get-pending-downloads` — Tankorent download queue
- `sources-cancel-download <id>`
- `sources-force-indexer-refresh <indexer-id>`
- `sources-get-tankorent-state` / `sources-get-tankolibrary-state`
- `dump-ui sources` / `dump-ui tankorent` / `dump-ui tankolibrary`

### v1.6 — Library-side (Agent 5)

**Files:** `src/devtools/DevControlServer.h`, `src/ui/MainWindow.{h,cpp}`, per-mode landing pages (`StreamPage.cpp`, `ComicsPage.cpp`, `BooksPage.cpp`, `VideosPage.cpp` library-section devSnapshot methods), `src/ui/Theme.*`, `tools/tankoctl.cpp`.

Commands (~10):
- `library-get-continue-reading <mode>` — CR strip state per mode
- `library-get-recently-added <mode>` — recent strip
- `library-get-search-state <mode>` — library search overlay state
- `library-apply-theme <theme-id>` — runtime theme application
- `library-get-active-theme`
- `library-set-density <density-id>` — tile density slider
- `library-get-settings` — all settings dump
- `library-set-setting <key> <value>` — settings poke
- `library-get-active-mode-pill` — which top-bar mode is active
- `dump-ui library`

### v1.7 — Synthetic UI interaction (CROSS-CUTTING)

**Files:** `src/devtools/DevControlServer.h`, `src/devtools/UiInteractionDispatcher.{h,cpp}` (NEW class — handles QObject lookup by objectName + QMetaObject::invokeMethod-based event dispatch), `src/ui/MainWindow.{h,cpp}` (registers dispatcher), `tools/tankoctl.cpp`.

Commands (~8):
- `ui-click <objectName>` — fires `QAbstractButton::animateClick()` or generic `QMetaObject::invokeMethod("click")`
- `ui-keypress <objectName> <key>` — fires QKeyEvent (Qt::Key code) on target
- `ui-text-input <objectName> <text>` — fires `setText()` + `textChanged` signal
- `ui-query-widget <objectName>` — returns `{visible, enabled, geometry, text, className}`
- `ui-query-focus` — returns the objectName of the currently focused widget
- `ui-active-layer` — returns current view stack (e.g. `comics/library → series-view`)
- `ui-simulate-scroll <objectName> <delta>` — fires QWheelEvent
- `ui-simulate-mouse <objectName> <event-type> [x] [y]` — fires QMouseEvent (press/release/move)

### v1.8 — System state + introspection (CROSS-CUTTING)

**Files:** `src/devtools/DevControlServer.h`, `src/devtools/SystemIntrospection.{h,cpp}` (NEW), per-domain `devSnapshot()` extensions for new state fields, `tools/tankoctl.cpp`.

Commands (~25):

Modal / window / focus:
- `app-get-active-modals` — open dialogs, popovers, context menus
- `app-get-window-list` — top-level windows with geometry
- `app-get-shortcut-table` — registered QShortcut bindings + state
- `app-trace-signals <objectName>` (start/stop) — signal tracing

Settings / state:
- `settings-get <key>` / `settings-set <key> <value>` / `settings-dump <group>` / `settings-reset <key>` — QSettings access
- `jsonstore-get <path>` / `jsonstore-set <path> <value>` / `jsonstore-dump <path>` — JsonStore access
- `cache-get-stats` — all cache layers (pixmap, sidecar, scraper, mangaupdates, bookwalker, etc) sizes + hit rates
- `cache-clear <layer>` / `cache-list <layer>`

Scanner / filesystem:
- `scanner-get-status` / `scanner-pause` / `scanner-resume` / `scanner-trigger <path>` / `scanner-list-watched`

Logging:
- `log-tail <component> [N]` — last N lines from component log
- `log-grep <pattern>` — search recent logs
- `log-mark <label>` — correlation marker across all log streams
- `log-set-level <component> <level>` — runtime verbosity

Network:
- `network-list-requests` — recent QNetworkAccessManager outbound
- `network-get-active` — currently-pending
- `network-throttle-set <bandwidth>` — slow-network simulation
- `network-block-host <host>` — addon outage simulation
- `events-tail [count]` — `out/events.jsonl` live tail

Theme:
- `theme-get-palette` — current applied palette
- `theme-get-applied-stylesheet <objectName>` — what QSS is on a target widget
- `theme-reload` — force re-application (dev hot-reload)
- `font-list-loaded` — currently-loaded fonts

Performance:
- `perf-get-frame-times` — last N paint frame durations
- `perf-get-cpu-usage` / `perf-get-gpu-usage`
- `perf-mark-start/end <label>` — named timing regions
- `perf-dump-counters` — all perf counters

Fault injection:
- `dev-inject-error <code>` — inject known error conditions
- `dev-toggle-feature <flag>` — feature flag toggle

### Deeper v1.1 stream-side (Agent 4)

Additive to existing v1.1 commands:
- `stream-get-indexer-health` (per-indexer status + response time + error rate)
- `stream-pause-download` / `stream-resume-download` / `stream-cancel-download <id>`
- `stream-get-pack-picker-state` (when picker open, what's selected)
- `stream-get-continue-watching` — Stream CR strip state
- `stream-clear-search` — reset search overlay

### Deeper v1.2 comics-side (Agent 1)

Additive to existing v1.2 commands:
- `comics-get-reader-state` — when reader open: page + chapter + zoom + layout
- `comics-seek-page <n>` / `comics-zoom <level>` / `comics-set-layout <single|double|webtoon>`
- `comics-pause-download` / `comics-cancel-download <id>`
- `comics-get-sources-panel-state` — expanded? selected? ranked rows?
- `comics-get-addons` — which addons feed comics

---

## §5 Ratification candidates (Hemanth-decision points only)

Per Rule 14 (`feedback_decision_authority.md`): agents decide technical, Hemanth decides product/strategic. The following are genuine Hemanth calls:

1. **`/memory-trim` interactive vs automated** — propose-with-confirmation vs auto-archive on threshold. Affects how aggressive memory hygiene feels day-to-day. Recommended: propose-with-confirmation. Auto-archive is too aggressive; agent confirms picks.

2. **`/hemanth-rewrite` opt-in scope** — never mandated (current spec) vs available-on-Hemanth-shortlist (so any agent can invoke when asking Hemanth a question). This affects Hemanth's experience of how agents communicate. Recommended: stay never-mandated; agents discover it organically.

3. **Arc identifier final name** — currently `SKILL_AUGMENTATION_ARC (provisional)`. Hemanth picks the final name when ratifying.

## Technical decisions (Agent 0 owns; documented for transparency)

Per Rule 14, the following are Agent-0 technical calls — no Hemanth ratification needed:

1. **Codex commissioning concurrency cap** — stagger 2 + 2 (v1.3 books + v1.5 sources first since they're independent file sets, then v1.4 player + v1.6 library). Reduces dispatcher namespace collision risk.
2. **Sweeper race-fix scope** — minimal first (warn-and-halt on detected orphans + Agent 0 manual lift). Upgrade to aggressive (auto-retry missed lines) only if minimal proves load-bearing after 2-3 sweep cycles.
3. **v1.7 + v1.8 cross-cutting commissioning attribution** — Agent 0 owns both, per existing pattern (REPO_HYGIENE Phase 3 was Agent 0 + Agent 3).
4. **Dispatcher delegation refactor** — happens as a small sub-phase inside Phase D, BEFORE v1.3 commissioning fires. Refactors `MainWindow::handleDevCommand()` to delegate per-domain dispatch to the per-domain page classes; existing v1.0-v1.2 commands stay where they are; v1.3+ go through delegates.
5. **Tier promotion criteria post-re-measurement** — Tier 2 → Tier 1 if (a) skill is invoked in ≥30% of relevant sessions over the 30-day window AND (b) absence of skill correlates with observed regressions. Agent 0 makes the call based on telemetry.

---

## Anti-patterns to avoid

1. **Don't re-mandate skills.** SKILL_DISCIPLINE_FIX taught us mandation ≠ adoption. Tier 2 conditional + hook-fired only at first. Promote based on 30-day re-measurement data.

2. **Don't expose v1.7 synthetic UI interaction without an audit gate.** The ability to fire arbitrary clicks/keypresses programmatically is powerful. Hard-gate behind `--dev-control` flag (existing gate) AND a separate `TANKOBAN_DEV_UI_SIM=1` env var so production builds genuinely can't reach it.

3. **Don't conflate Agent 0 housekeeping commits with sweep marker commits.** The sweep race-condition fix flags orphans; it doesn't auto-commit them. Agent 0 lifts them with attribution.

4. **Don't ship dev-bridges without dispatcher delegation refactor.** `MainWindow::handleDevCommand()` at v1.2 is already 270 lines. Adding 95 more commands without per-domain delegate refactor would make it unreadable.

5. **Don't trust agent self-attestation post-hook-#5.** Once skill-provenance auto-detect lands, self-attestation drift becomes invisible. Telemetry must be the source of truth.

6. **Don't write skills that duplicate existing tools.** `/clipboard-get` would duplicate Bash `Get-Clipboard`. The spec excludes these already; future additions should grep first.

7. **Don't introduce Tier 1 mandatory skills in this arc.** Even if a skill seems load-bearing, it stays Tier 2 until the 30-day re-measurement gives empirical evidence of universal-adoption.

8. **Don't extend the bridge with breaking changes.** Schema versioning rule: additive within v1.x = non-breaking; removals/renames bump to v2. The 95 new commands are ALL additive.

---

## Standing contracts (survive into execution)

- **Codex Trigger D commissioning** follows `project_codex_substrate_live.md` pattern: agent commissions, Codex writes, attribution under requesting agent
- **Schema versioning** per `DevControlServer.h:24`: additive within v1.x, v2 for breaking
- **Hook discipline** per SKILL_DISCIPLINE_FIX Phase 4: hooks are passive (warn/scaffold, not block); env-var kill-switch always available
- **Memory authoring** per existing patterns: type frontmatter + `**Why:**` + `**How to apply:**` for feedback/project types
- **MEMORY.md index** entries stay under ~150 chars per memory line (current overflow is from over-long entries)
- **Tier defaults** per CLAUDE.md "Required Skills & Protocols" 2026-04-25 framing: Tier 1 mandatory ~6 skills, Tier 2 conditional, Tier 3 milestone-only
- **30-day re-measurement** per `feedback_skill_discipline_remeasurement.md`: telemetry-driven tier promotion decisions

---

## Open carry-forward notes

- `/session-recap` slash command (just shipped 2026-05-18) is the END-of-wake counterpart to `/handoff-brief` (MID-wake). They share telemetry capture pattern; refactor for shared utility script can land in Phase E.
- The `commit-sweeper.md` sub-agent at `.claude/agents/commit-sweeper.md` is in scope for the race-fix; no other behavior changes.
- Dispatcher delegation refactor (anti-pattern #4) is itself a small sub-phase inside Phase D before v1.3 commissioning. Ratify this as part of §5 Q1.

---

## Companion plan reference

When Hemanth ratifies this spec, the next step is `/superpowers:writing-plans` → produces `docs/superpowers/plans/<DATE>-brotherhood-skill-augmentation.md` with task-level breakdown (numbered tasks across the 5 phases, dependencies between tasks, per-task acceptance criteria, smoke matrix for each phase exit).

The plan-writing happens on a future wake. This wake closes after Hemanth approves the spec.
