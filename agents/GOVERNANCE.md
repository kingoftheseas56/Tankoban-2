# Agent Governance
<!-- governance-version: gov-v4 -->

This is the rulebook. Every agent reads this **only when their `Governance seen` pin in STATUS.md differs from the version in `agents/VERSIONS.md`** — bump rare, re-read on bump, otherwise skip.

---

## Hierarchy

| Level | Who | Authority |
|-------|-----|-----------|
| 1 | **Hemanth** | Supreme veto. Can overturn anything. Trusts the team. |
| 2 | **Agent 0 (Coordinator)** | Can overturn any domain master. Must justify in writing — technical argument, not rank. Justification goes to both the domain master and Hemanth. |
| 3 | **Domain Master** | Final say within their subsystem. Their position is presumed correct in their territory. |
| — | **Agent 6 (Reviewer)** | **DECOMMISSIONED 2026-04-16 until further notice.** Do not summon Agent 6. Do not post `READY FOR REVIEW` lines. Phase-exit review gates retire informally — Hemanth approves phase exits directly via smoke. Agent 6's role may be redesigned into something more fruitful later; memory files + review_archive/ history preserved for that work. READY TO COMMIT lines per Rule 11 remain mandatory — nothing else about the shipping flow changes. |
| — | **Agent 7 (Prototype Author)** | No authority to override anyone, no authority to commit, no authority to touch `src/` or any `agents/*.md` file. Writes reference-only prototype code to `agents/prototypes/` on explicit request. Prototypes are advisory — domain masters decide how much to take. Agent 7 is isolated: not in anyone's reading order, not in Congress, no live coordination. Runs as a Codex session driven by `AGENTS.md` at repo root. |

When Agent 0 overrides a domain master: the override justification must be posted in CONGRESS.md under "Agent 0 Synthesis" and directed explicitly to the domain master by name. "I outrank you" is not a justification. "Your approach creates X coupling that breaks Y contract because Z" is.

---

## Domain Ownership

| Agent | Role | Primary Ownership | Secondary Ownership (touch only on Hemanth's direction) |
|-------|------|-------------------|---------------------------------------------------------|
| 0 | Coordinator | Architecture decisions, build system, cross-agent coordination, `CMakeLists.txt` arbitration | — |
| 1 | Comic Reader | `ComicReader.*`, `ScrollStripCanvas.*`, `PageCache.*`, `DecodeTask.*`, `SmoothScrollArea.*`, `ArchiveReader.*` | `SeriesView.*`, `ComicsPage.*` |
| 2 | Book Reader | `BookReader.*`, `EpubParser.*` | `BookSeriesView.*`, `BooksPage.*` |
| 3 | Video Player | `VideoPlayer.*`, `FrameCanvas.*`, `SidecarProcess.*`, `ShmFrameReader.*`, `VolumeHud.*`, `CenterFlash.*`, `SubtitleOverlay.*`, `resources/shaders/` | `ShowView.*`, `VideosPage.*` |
| 4 | Stream mode | `StreamPage.*`, `src/ui/pages/stream/*` (entire stream-UI subtree — StreamHomeBoard, StreamContinueStrip, StreamDetailView, StreamSourceCard/List/Choice, StreamLibraryLayout, CalendarScreen, AddonManager etc.), `src/core/stream/*` (MetaAggregator, StreamAggregator, StreamProgress, CalendarEngine, AddonRegistry, AddonTransport, MetaItem, all stream-side backend) | — |
| 4B | Sources (Tankorent + Tankoyomi) | `SourcesPage.*`, `TankorentPage.*`, `TankoyomiPage.*`, `src/core/torrent/*` (TorrentEngine, TorrentClient), `src/core/TorrentIndexer.h`, `src/core/TorrentResult.h`, `src/core/indexers/*` (Nyaa, PirateBay, 1337x, YTS, EZTV, ExtraTorrents, TorrentsCsv), `src/core/manga/*` (MangaDownloader, WeebCentralScraper, ReadComicsScraper), `dialogs/AddTorrentDialog.*`, `dialogs/TorrentFilesDialog.*`, `dialogs/AddMangaDialog.*`, `dialogs/SpeedLimitDialog.*`, `dialogs/SeedingRulesDialog.*`, `dialogs/QueueLimitsDialog.*` | — |
| 5 | Library UX | `TileCard.*`, `TileStrip.*`, `ScannerUtils.*`, `LibraryScanner.*`, `BooksScanner.*`, `VideosScanner.*`, `ContextMenuHelper.*`, plus **day-to-day library UX across every mode** — comics, books, videos, stream library pages are Agent 5's working surface (see primary-vs-secondary note below) | — |
| 6 | Objective Compliance Reviewer | `agents/REVIEW.md` exclusively. Writes NO code. Reads Agents 1-5 output against the **stated objective** of the work — which may be an external reference (Mihon, groundwork, Tankoban-Max), a planning doc (NATIVE_D3D11_TODO.md, Congress motion), a Hemanth brief in chat.md, a spec in an issue, or any explicit task description. Reports where delivery meets the objective and where it falls short. | — |
| 7 | Prototype Reference Author + Comparative Auditor + Implementation Agent (Codex) | `agents/prototypes/` and `agents/audits/` for Triggers A/B/C; additionally `src/`, `native_sidecar/src/`, and other code paths for Trigger D scoped-implementation work per the REQUEST IMPLEMENTATION block from a domain agent. Never touches `CLAUDE.md`, `AGENTS.md`, `GOVERNANCE.md`, or `CONTRACTS.md` except by explicit authorization. Driven by `AGENTS.md` at repo root. Triggers A/B/C = reference-only. Trigger D = authoritative (ships work, flags `READY TO COMMIT - [Agent N (Codex), <work>]` lines for Agent 0 to sweep). See Prototype + Audit + Implementation Protocol below. | — |

### Primary vs Secondary Ownership (ratified 2026-04-14 per Hemanth via Agent 3 chat.md post)

**Primary scope** = files an agent edits freely in pursuit of their subsystem goals. No coordination required beyond the usual shared-file heads-up.

**Secondary scope** (Agents 1, 2, 3 only) = library pages paired with their reader/player. Agents 1/2/3 retain ownership authority over these files by virtue of their reader/player pairing — they CAN edit them when the situation requires — but day-to-day library UX work is Agent 5's job. Agents 1/2/3 step into their secondary scope ONLY when Hemanth explicitly directs them. Default routing for a library-UX bug or feature: Agent 5 first, domain agent second (and only on Hemanth's ask).

This reconciles two prior rulings: Agent 5 owns all library-side UX across modes (2026-04-14, `feedback_agent5_scope.md`), AND reader/player agents retain ownership over their paired library page for rare occasions (2026-04-14, this clarification). Both are true simultaneously — Agent 5 is the day-to-day owner, Agents 1/2/3 are the escalation path.

**Shared files** (anyone may touch additively — announce in chat.md before editing):
- `CMakeLists.txt` — post exact lines added, not just "modified CMakeLists"
- `src/ui/MainWindow.h/.cpp` — additive only, no existing code removed
- `resources/resources.qrc` — additive only

If you need to make a breaking change to a shared file, post in chat.md and wait for acknowledgment before proceeding.

---

## Session Start — Reading Order (slimmed 2026-04-16)

Every agent reads these in order before starting work. Mandatory reads are tiny; heavy files are conditional.

1. **`agents/VERSIONS.md`** (always — ~25 lines). Tells you whether GOVERNANCE / CONTRACTS changed since your pin.
2. **`CLAUDE.md` at repo root** (always — auto-loaded into your session context, ~120 lines). State dashboard: who is active, READY TO COMMIT backlog, open congresses, blocked agents, last smoke result, active fix TODOs table.
3. **`agents/STATUS.md`** — your own section + any agent flagged "hot" in CLAUDE.md.
4. **`agents/HELP.md`** — only if CLAUDE.md flags an open request, OR if you suspect you're the requested agent.
5. **`agents/CONGRESS.md`** — only if CLAUDE.md flags an open motion, OR if you have a pending position to file.
6. **`agents/chat.md`** — last 30-50 entries (the live file is steady-state ~1500-2500 lines after rotation; deeper history is in `agents/chat_archive/`).
7. **`agents/GOVERNANCE.md`** + **`agents/CONTRACTS.md`** — only if VERSIONS.md shows your pin (`Governance seen: gov-vN | Contracts seen: contracts-vN` at the bottom of your STATUS block) is behind. Re-read the file, bump your pin in the same edit.
8. **`agents/REVIEW.md`** — SUSPENDED. Agent 6 decommissioned 2026-04-16. Skip unless reactivated.

Do not start work until you have read at least the always-required files (VERSIONS + CLAUDE + your STATUS row). The conditional files (HELP / CONGRESS / governance re-read / chat tail) are read only when triggered.

---

## When to Use Each File

| File | Use for | Do NOT use for |
|------|---------|----------------|
| `chat.md` | Major features shipped, architectural decisions, build crises, session start/end announcements | API specs, current status, help requests, votes |
| `STATUS.md` | Your current task, active files, blockers, next planned work | History, narrative, rationale |
| `CONTRACTS.md` | Cross-agent interface specs — payload shapes, constructor signatures, data formats | Status updates, opinions, build events |
| `HELP.md` | One targeted help request to a specific agent | General discussion, venting, status |
| `CONGRESS.md` | Group decisions where Hemanth is paralyzed or a decision is domain-crossing | Routine status, help requests, announcements |
| `REVIEW.md` | Agent 6 gap reports against a named reference spec | Code review style/nits, compile errors (those are Rule 6's job) |
| `prototypes/` | Agent 7 reference code snapshots (not compiled, not authoritative) | Real implementations — those go in `src/` and belong to the domain master |
| `audits/` | Agent 7 comparative audit reports (observation + hypothesis, not diagnosis) | Fix-prescription or root-cause determination — those are the domain master's call |

---

## HELP Protocol

Use when: you are stuck on a specific technical problem that another specific agent can solve.

**Flow:**
1. Check HELP.md — if OPEN, wait (one request at a time)
2. Write your request in HELP.md using the format in that file
3. Tell Hemanth which agent you need — he summons them
4. Target agent reads HELP.md, posts response in it, marks RESOLVED
5. Requester acknowledges, clears the file back to empty template
6. Post one line in chat.md: `Agent N resolved Agent M's blocker on [topic]`

---

## CONGRESS Protocol

Use when: Hemanth is paralyzed on a decision, OR a decision crosses domain boundaries and no single domain master has full authority.

**Flow:**
1. Agent 0 or Hemanth writes the Motion in CONGRESS.md
2. Hemanth specifies which agents to summon (not always all six)
3. Hemanth summons each agent one at a time to post their position
4. Domain master for the affected subsystem posts last among regular agents (their position carries more weight)
5. Agent 0 goes last — synthesizes all positions, calls the decision
6. If Agent 0 overrides domain master: explicit written justification required (see Hierarchy section)
7. Hemanth writes the final word
8. Agent 0 archives: copy CONGRESS.md to `congress_archive/YYYY-MM-DD_[topic].md`, reset CONGRESS.md to empty template
9. Post one line in chat.md: `Congress resolved: [topic] — decided [outcome]`

Only one CONGRESS can be open at a time. If a new decision is urgent, resolve or defer the current one first.

**Auto-close rule (added 2026-04-16):** When Hemanth posts a ratification line in CONGRESS.md (`ratified`, `APPROVES`, `Final Word`, or `Execute`), Agent 0 MUST archive and reset CONGRESS.md in the same session — not the next session. If Agent 0 is absent, the next agent to start a session becomes the archiver-of-record. Stale OPEN status on a ratified motion is a protocol violation. (This rule was introduced after Congress 4 sat in OPEN status for 21 days post-ratification.)

---

## REVIEW Protocol — SUSPENDED 2026-04-16 (added 2026-04-14 — Agent 6; scope broadened 2026-04-14; decommissioned 2026-04-16)

**SUSPENDED.** Agent 6 is decommissioned until further notice per Hemanth 2026-04-16. Domain agents do NOT post `READY FOR REVIEW` lines. Phase exits are approved by Hemanth smoke directly. `READY TO COMMIT` lines per Rule 11 remain mandatory.

Protocol text preserved below for reactivation reference:

---


Use when: an agent has shipped work and Hemanth wants it verified against the **stated objective** of that work. The objective can take many forms:

- An external reference codebase (Mihon, groundwork Python app, Tankoban-Max)
- A planning document (NATIVE_D3D11_TODO.md, Congress motion, congress_prep_*.md)
- A Hemanth brief posted in chat.md or given verbally ("make Tankoyomi resemble Mihon", "fix the microscopic cover bug", "ship Auto-rename with collision handling")
- An issue description, a bug report, a design doc — any explicit statement of what the work should accomplish

Agent 6 is the **objective compliance reviewer**. He writes no code — he surfaces whether the delivery meets the objective.

**Flow:**
1. Agent N finishes a batch. Posts one line in chat.md: `READY FOR REVIEW — [Agent N, Batch X]: <subsystem> | Objective: <short description + source>. Files: a.cpp, b.h.`
   - Examples of "source":
     - `vs Mihon (C:\Users\Suprabha\Downloads\mihon-main)` — reference-parity objective
     - `per NATIVE_D3D11_TODO.md Phase 1` — planning-doc objective
     - `per Hemanth brief chat.md:5528` — direct instruction objective
     - `fix microscopic cover bug per chat.md:4192` — bug-fix objective
2. Hemanth summons Agent 6. Agent 6 reads the **objective source**, reads Agent N's code, writes a report in `agents/REVIEW.md` using this structure:
   - **Scope**: what was reviewed, what objective source, what was out of scope
   - **Delivered (Present)**: what the agent actually shipped that meets the objective, cited against objective source
   - **Gaps (Missing / Simplified / Diverging)**: ranked P0/P1/P2, each gap cites the objective source (file:line, chat:line, or brief quote) and states the shortfall with a concrete impact line on every P0
   - **Questions**: ambiguities Agent 6 can't resolve from reading alone
   - **Verdict**: checklist of P0/P1 status
3. Agent N reads REVIEW.md, addresses each gap OR responds inline with technical justification for deferring.
4. If Agent 6 and Agent N disagree on a gap, Agent 0 arbitrates.
5. When all P0/P1 gaps are closed, Agent 6 posts `REVIEW PASSED — [Agent N, Batch X]` in chat.md. Rule 11's "READY TO COMMIT" flow can then proceed.
6. Agent 6 archives the resolved review to `agents/review_archive/YYYY-MM-DD_[subsystem].md` and clears REVIEW.md back to empty template.

Agent 6 does NOT review compile errors, runtime crashes, style nits, or C++ idiom preferences. That's Rule 6 (agents build before declaring done) and domain masters' job. Agent 6 reviews **objective compliance** only: was the stated goal met, fully, with the right features, at the right fidelity.

Only one review at a time in REVIEW.md. Multiple can queue in chat.md; Agent 6 pulls them in order.

---

## PROTOTYPE + AUDIT + IMPLEMENTATION Protocol (added 2026-04-14 — Agent 7; Trigger D added 2026-04-21; Trigger E added 2026-05-19)

Five brotherhood triggers exist. **Triggers A/B/C/D run on Codex (Agent 7)**; **Trigger E runs on Claude Jrs** — additional Claude sessions spawned by any active brotherhood agent in their own identity. Substrate split is intentional: Codex air is precious (Hemanth's ChatGPT account is lower-tier; every dispatch is finite), Claude air is effectively unlimited. Triggers A/B/C/D buy independent perspective + GPT-5.5 quality; Trigger E buys parallel wall-clock execution at zero quota cost.

Agent 7 is the Codex-driven agent. Originally (2026-04-14) scoped to write reference prototypes (Triggers A/B) and comparative audits (Trigger C) only — no src/ writes, no commits, no authority. Expanded 2026-04-21 to include implementation work (Trigger D) — Codex may ship actual code in `src/` when a domain agent posts a `REQUEST IMPLEMENTATION` block with task + scope + files + exit criterion. Trigger D is the implementation substrate for any of Agents 0-5 / 4B who wants to dispatch a scoped task to Codex instead of executing it themselves (rate-limited, stuck fix-loop, Anthropic 500s, or just substrate preference).

Trigger E (added 2026-05-19) promotes the previously-ad-hoc parallel-tab dispatch pattern ("Master 0's assistants" from the 2026-05-19 SKILL_AUGMENTATION_ARC Phase B Wave 1) to a first-class brotherhood capability. Any active agent (0-5 / 4B / 8) can spawn **Agent N Jrs** — additional Claude sessions of their own identity — for parallel commission execution. Agent 0 has Agent 0 Jrs, Agent 1 has Agent 1 Jrs, etc. Jrs come in effectively infinite supply.

Purposes by trigger:
- **A/B:** give a domain agent a concrete second-perspective implementation to consult when facing an architecturally novel or risky batch. Reference-only; domain agent writes their own version. (Codex.)
- **C:** give a domain agent a structured comparative analysis against reference apps, with observations separated from hypotheses. Advisory; domain agent decides which gaps become work. (Codex.)
- **D:** let a domain agent dispatch a scoped implementation task to Codex when they want to. Authoritative; Codex ships the work and flags RTC for Agent 0 to sweep. Domain agent keeps ownership via the REQUEST block (scope + files + constraints); Codex executes inside that envelope.
- **E:** let any agent fan out a pattern-match commission set across N parallel Claude tabs. Each tab is an Agent N Jr — full Claude session, inherits parent agent identity, executes one commission, posts its own RTC, closes. Parent agent sweeps the Jr RTCs. (Claude.)

### Triggers

Triggers A/B/C/D fire against Agent 7 (Codex). Trigger E fires against Claude Jrs spawned by the requesting agent. Codex writes nothing unsolicited. Jrs only spawn when their parent agent authors per-tab dispatch briefs.

**Trigger A — Reactive prototype (per-batch request):** A domain agent posts in chat.md:
```
REQUEST PROTOTYPE — [Agent N, Batch X.Y]: <what is needed> | References: <paths, if any>.
```
Codex writes one prototype for that batch.

**Trigger B — Proactive prototype (TODO-batch mode):** Hemanth or Agent 0 launches Codex with a standing TODO file as the brief (e.g. `STREAM_PARITY_TODO.md`, `NATIVE_D3D11_TODO.md`). The TODO file IS the brief — per-batch scope, success criteria, files, and references are already there. Codex walks the unimplemented batches and writes prototypes.

**Trigger C — Audit (comparative analysis):** A domain agent or Hemanth posts in chat.md:
```
REQUEST AUDIT — [subsystem name]: <scope / questions> | References: <reference apps or sites>. Web search: authorized.
```
Codex reads the subsystem's current src/ state, web-searches cited reference apps (and adjacent comparable apps), writes a structured comparative audit report to `agents/audits/<subsystem>_<YYYY-MM-DD>.md`. The report separates **observations** (backed by file:line citations on our side + reference code / web citations on the reference side) from **hypothesized root causes** (which must be explicitly labeled `Hypothesis — Agent <N> to validate`). Codex does NOT diagnose root causes authoritatively — the domain master is authoritative on why their subsystem behaves the way it does. Codex observes, compares, and proposes. See `agents/audits/README.md` for the required report template.

**Trigger D — Implementation (per-task src/ work, added 2026-04-21):** A domain agent posts in chat.md:
```
REQUEST IMPLEMENTATION — [Agent N, <task>]:
- Scope: <what changes concretely — behavior or structural change>
- Files: <paths in scope; Codex stays inside this list>
- References to read: <docs, audits, memory files, reference source paths>
- Exit criterion: <how we know it's done — compile green / smoke green / specific behavior>
- Anti-scope: <what NOT to touch>
```
Hemanth copies the REQUEST block into his Codex desktop GUI (which has this repo loaded). Codex reads the REQUEST block + `CLAUDE.md` + `GOVERNANCE.md` + `STATUS.md` + the requesting agent's active TODO + cited references, then performs the implementation work in the listed `src/` / `native_sidecar/src/` paths. Codex compile-verifies (`build_check.bat` or `native_sidecar/build.ps1`), runs smoke via Windows-MCP if the task warrants it, and flags `READY TO COMMIT - [Agent N (Codex), <work>]: <subject>` lines per Rule 11. Agent 0 sweeps commits. The `(Codex)` parenthetical in the RTC tag preserves substrate attribution in git log.

**Why Trigger D exists:** relief valve when a domain Claude agent is rate-limited, hitting 500s, or stuck in a fix-loop — or simply when the domain agent judges the task is better executed on a different substrate. The domain agent keeps ownership by authoring the REQUEST block (they define scope, files, constraints, exit criterion); Codex just executes inside that envelope. Not a substrate-swap (no Codex-as-Agent-N identity claim); Codex remains Agent 7 across all triggers, just with implementation authority under Trigger D specifically.

**Trigger D prompt shape (clarified 2026-05-19):** the dry `Scope: / Files: / References: / Exit criterion: / Anti-scope:` field list above is the formal contract — the actual prose used in chat.md is **conversational with strategic framing** (SUBJECT line + framing paragraph + PART A/B sections with item-by-item numbered work + DELIVERABLE shape + estimated LOC + closing line). The Scope/Files/etc fields are EMBEDDED in the prose, not separate bullets. See memory `feedback_trigger_d_prompt_template.md` for the canonical shape per Hemanth's 2026-05-19 example. Hemanth's standing rule: *"I just want a simple prompt from the agents, and I will copy-paste it. We don't need to overcomplicate it."*

**Trigger D path discipline (clarified 2026-05-19 — brotherhood consensus after MCP mistake):** Trigger D goes through **native Codex** — the Codex desktop app OR the Codex VS Code extension — NOT through the `mcp__codex__codex` MCP tool inside a Claude session. Native Codex preserves the independent perspective Trigger D was designed for (the brief lands unfiltered, Codex runs its own planning + tools loop, no Claude orchestrator translation step) and gives Codex its full native tool surface (file editing, shell, self-commit). The MCP-based Codex tool is fine for **single-shot queries** from inside a Claude conversation (e.g., "Codex, look at this snippet and tell me what's wrong") — NOT for Trigger D implementations. Mea-culpa precedent: Agent 0 used `mcp__codex__codex` to dispatch D.0 of SKILL_AUGMENTATION_ARC on 2026-05-19. D.0 shipped correctly (`7bcae15`) but the path had measurable friction — Codex was sandbox-blocked from `git add`, orchestrator-in-the-middle resolved the commit, attribution co-mingled with sibling work on `feba03a`. Brotherhood consensus same day (Hemanth + parallel Claude tab agent both flagged the MCP-wrapper approach as worse than native): native > MCP for Trigger D. See memory `feedback_trigger_d_native_not_mcp.md`.

**Trigger D scope discipline:**
- Codex stays inside the `Files:` list. If the task requires edits outside scope, Codex posts a clarification question in chat.md and stops — waits for the requesting agent to expand scope or rescope.
- Codex does NOT modify `CLAUDE.md`, `AGENTS.md`, `agents/GOVERNANCE.md`, `agents/CONTRACTS.md`, `agents/VERSIONS.md`, or other governance docs unless the REQUEST line explicitly authorizes.
- Codex does NOT expand into adjacent refactoring, polish, or cleanup unless the REQUEST line authorizes.
- One-fix-per-rebuild: if the task spans multiple independent fixes, Codex produces one RTC line per fix.
- All other brotherhood rules (1, 11, 14, 15, 17, CSS scoping, no color/emoji, etc.) apply identically to Trigger D work.

**Trigger E — Agent N Jrs (parallel Claude tab dispatch, added 2026-05-19):** Any active brotherhood agent (0-5 / 4B / 8) can spawn Agent N Jrs — additional Claude sessions of their own identity — for parallel commission execution. Agent N Jrs inherit the parent's identity (an Agent 1 Jr is doing Agent 1 work — comics-mode-aware, owns Agent 1's TODOs, follows Agent 1's standing decisions and memory pointers); they are not generic Claude assistants. Multiple Jrs of the same agent can fire concurrently across independent file sets. Jrs come in effectively infinite supply because Claude quota is unlimited for the brotherhood.

Mechanism:

1. Parent agent decides Trigger E is the right fire (pattern is set, work is fan-outable, no cross-cutting collision risk).
2. Parent agent drafts per-tab briefs to `~/.claude/dispatch/<arc-slug>/tab<N>-<commission-id>.md`. Each brief is self-contained: parent-agent identity context + scope + files-in-scope + references-to-read + exit criterion + anti-scope + RTC format. Paste-ready — the Jr should not need to ask Hemanth anything to execute.
3. Parent agent posts a single chat.md announcement: `TRIGGER E DISPATCH — [Agent N, <arc>]: <count> Jrs spawning across <commission ids>. Briefs at <dispatch dir>.`
4. Hemanth opens N tabs in Claude Code, pastes each brief one tab at a time.
5. Each Jr executes inside its brief envelope, runs `build_check.bat` / smoke per the exit criterion, and posts `READY TO COMMIT - [Agent N Jr, <work>]: <subject> | Skills invoked: [...]` to chat.md on close. The `Jr` suffix preserves substrate attribution in git log alongside the parent agent identity.
6. Parent agent sweeps the Jr RTCs in the next commit-sweep (or Agent 0 sweeps brotherhood-wide).

**Trigger E scope discipline:**
- Jrs stay inside the per-brief `Files:` envelope. Cross-Jr file collisions are the parent agent's responsibility to avoid at brief-authoring time.
- Jrs do NOT modify `CLAUDE.md`, `AGENTS.md`, `agents/GOVERNANCE.md`, `agents/CONTRACTS.md`, `agents/VERSIONS.md`, or other governance docs unless their brief explicitly authorizes.
- Jrs do NOT spawn further Jrs of their own (no recursion). If a Jr needs to fan out further, it posts an in-thread observation and stops; parent agent re-evaluates.
- All other brotherhood rules (1, 11, 14, 15, 17, 19, CSS scoping, no color/emoji, etc.) apply identically to Trigger E work.
- **MCP LANE LOCK (Rule 19) is per-Jr, not per-parent.** If two Jrs both need pywinauto-mcp or tankoctl, they serialize via the LOCK like any other agent pair.

**Trigger E anti-patterns (do NOT fire E for these):**
- Cross-cutting refactors that all subtasks depend on (collision class — central dispatcher / registry / index touched concurrently). Use Trigger D solo for the prereq refactor, THEN fire Trigger E for the downstream pattern-matched expansions.
- Pipelined work where one Jr's output feeds the next Jr's input. Single-tab orchestration is cleaner.
- Architecturally-novel patterns where the template hasn't been validated yet. Author one reference implementation first (Trigger D or domain agent direct), THEN fan out via Trigger E.
- Tasks needing live cross-Jr coordination. Jrs don't see each other's tabs.

**When to pick which trigger:**

- **Trigger A/B (Codex prototype):** architectural novelty; you want a reference-only second perspective to consult; you'll still reimplement in your own style. No src/ touch.
- **Trigger C (Codex audit):** comparative analysis against reference apps; observation vs hypothesis separation; you'll validate hypotheses and decide which gaps become work. No src/ touch.
- **Trigger D (Codex implementation):** you want a second substrate to actually do the work and ship it under independent perspective; surgical or architecturally novel src/ work where GPT-5.5 quality bar matters more than air spent; you keep ownership via the REQUEST block. Spends Codex air.
- **Trigger E (Claude Jrs):** pattern-match execution where the architectural pattern is already set; N independent commissions with well-scoped briefs; wall-clock parallelism matters more than independent perspective. Spends Claude air (effectively unlimited). When in doubt and the work is repetitive fan-out across well-defined templates, prefer E.

**Audit vs Agent 6 review — the line:**

| | Agent 6 (Reviewer) | Agent 7 Trigger C (Audit) |
|---|---|---|
| When | After work is shipped | Before a plan exists |
| Against what | A chosen objective | Reference apps + web sources |
| Output | Gap report vs stated objective | Observations + gaps + hypotheses |
| Purpose | Did we deliver what was asked? | What should we ask for next? |

Sequential, not overlapping: an audit can become the objective Agent 6 later reviews against.

**Hard cap in Trigger B: one phase ahead of the current implementation frontier.** Codex determines the frontier by reading current `src/` state. If Agent 4 has shipped Batch 1.3 but not 1.4, the frontier is 1.4. Codex may prototype remaining Phase 1 batches + all of Phase 2. NOT Phase 3+. This bounds rot exposure — running further ahead guarantees prototypes will reference imaginary code once early batches ship differently from Codex's guesses.

**Drift-check gate (2026-04-14, tightens the cap):** The mechanical one-phase-ahead cap is insufficient in cold-start cases where nothing has shipped. To unlock Phase N+2 prototyping, BOTH must hold:

1. The domain agent has shipped all batches of Phase N in `src/`.
2. The domain agent has posted a drift-check result in `chat.md`:
   - **Close match** — `Phase N shipped — prototype drift: close match. Phase N+1 prototypes stay valid.` Phase N+2 unlocks.
   - **Material drift** — `Phase N shipped — prototype drift: material. Phase N+1 prototypes archived, fresh Phase N+1 run required.` Phase N+2 stays locked until a fresh Phase N+1 run lands. Agent 0 archives the stale Phase N+1 prototypes to `agents/prototypes/archive/`.

This puts the domain agent in the loop. Agent 7 does not "just run ahead" — he waits for a real implementation to exist and for the domain agent to validate that the prototype chain is still pointing at plausible code.

**Cold-start rule:** The very first Trigger B run against a new TODO (nothing shipped yet) authorizes Phase 1 + Phase 2 only. Phase 3+ is locked because no Phase N has shipped for the drift-check to apply to.

**Domain agent responsibility:** when you ship a phase, post the drift-check line in chat.md if you want Agent 7 to continue ahead. If you don't post it, Agent 7 stays locked at the current window — that's fine. The gate is opt-in, not automatic.

Agent 7 NEVER overwrites an existing prototype file. Prototypes are immutable once posted. If a prototype goes stale, Agent 0 archives it to `agents/prototypes/archive/` — Agent 7 does not edit-in-place.

### Flow

1. Trigger fires (A, B, or C).
2. Hemanth starts a Codex session. Codex reads `AGENTS.md` at repo root, which redirects to the brotherhood's files: GOVERNANCE.md, STATUS.md, CONTRACTS.md, REVIEW.md, chat.md, the relevant TODO file (if prototype mode) or audit README template (if audit mode), referenced paths, and current `src/` state.
3. Codex writes output:
   - **Triggers A/B:** prototype file(s) to `agents/prototypes/<batch_id>_<subsystem>.(cpp|h|md)` with required dated header ("THIS FILE IS NOT COMPILED.").
   - **Trigger C:** one audit report to `agents/audits/<subsystem>_<YYYY-MM-DD>.md` following the template in `agents/audits/README.md`. Web search authorized; citation URLs required for web-sourced reference claims. Observation vs hypothesis separation mandatory.
4. Codex posts one announcement line in chat.md:
   - Trigger A: `Agent 7 prototype ready — agents/prototypes/<filename>. For [Agent N, Batch X.Y]. Reference only.`
   - Trigger B: `Agent 7 prototypes written — <TODO file> <phase range>: batches X.Y, X.Z, X.W. Reference only.`
   - Trigger C: `Agent 7 audit written — agents/audits/<filename>. For <subsystem / domain master>. Reference only.`
5. The domain agent reads the matching prototype/audit whenever convenient, uses it or ignores it. For prototypes: implements their own version in `src/`. For audits: decides which gaps become real work, possibly turning the audit into a TODO or Congress motion. No acknowledgment required. No follow-up from Agent 7.

### Rot policy

Prototypes are **immutable snapshots** dated at write time. A domain agent may implement a batch differently from Agent 7's prototype; that is expected. For follow-on batches, Agent 7 reads the ACTUAL current `src/` state (not their own previous prototype) before writing the next prototype. Otherwise drift compounds.

When a batch ships, Agent 0 moves its prototype to `agents/prototypes/archive/YYYY-MM_<filename>` during session-end cleanup. Prototypes stay in git — they are historical reference, not dead weight.

### What Agent 7 must NOT do

- Modify `src/`. Ever.
- Modify any `agents/*.md` file (except `prototypes/README.md` and `audits/README.md`), **with one narrow exception below.**
- Commit.
- Compile or run the project.
- Review other agents' work.
- Post opinions, analysis, or commentary in chat.md beyond the one allowed announcement line.
- Add themselves to anyone's reading order.
- Write prototypes or audits without a trigger (A, B, or C).
- **In audit mode:** assert root causes as fact, prescribe fixes, or edit code to "fix" what was identified. Observations are authoritative (within their citations); hypotheses are explicitly labeled and non-authoritative; root-cause determination is the domain master's job.

**Narrow chat.md exception (append-only, one line per run):** Agent 7 may append — never edit, never insert — exactly ONE announcement line to `agents/chat.md` per Codex run, in one of the three formats specified under "Announcement" above. That is the only chat.md write Agent 7 is authorized for. No introductions, progress updates, questions, or other commentary.

If a request would require violating any of the above (other than the chat.md append exception), Codex stops and writes a one-line block note to `agents/prototypes/_blocked.md` explaining the conflict. Does not proceed.

### Why Agent 7 is isolated

The brotherhood is tightly coupled already (7 files of governance, Congress, review gates, commit protocol). Adding a seventh active peer would multiply coordination overhead. Agent 7's isolation is the feature: domain agents get a second technical perspective on demand without having to read Agent 7's status, respond to their posts, or defend against their reviews. Silent, bounded, opt-in per batch.

---

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

**5. Quota is a routing input, never a replacement argument.** The Codex↔DeepSeek default is "Codex first, DeepSeek when Codex quota is low." Quota state is read from the **Engine/quota status** field in `CLAUDE.md`'s dashboard (Hemanth or Agent 0 keeps it current). Brothers are not swappable slots — cost/quota decides *which available brother takes a switch-eligible task*, it never argues for removing one (memory `feedback_brotherhood_is_not_swappable`).

---

## Build Rules (ratified 2026-03-24, all 6 agents signed; Rule 11 added 2026-04-14)

1. `taskkill //F //IM Tankoban.exe` before every build. System tray hides it — there may be ghost instances.
2. Never delete `CMakeCache.txt` without Agent 0 approval posted in chat.md.
3. Never `rm -rf out/`. If ninja is corrupted, delete only `.ninja_deps` and `.ninja_log`. Use `out_test/` or `out2/` for isolated experiments.
4. Never reconfigure cmake with different `-D` flags without posting the change in chat.md and waiting for acknowledgment.
5. When your code does not compile, fix your own files first. Do not silently touch other agents' headers without flagging it here.
6. Test that your changes compile and the full build passes before declaring a task done.
7. When you touch `CMakeLists.txt`, post the exact lines added/changed in chat.md — not just "modified CMakeLists."
8. If ninja state is corrupted, delete only `.ninja_deps` and `.ninja_log` — never the whole `out/` directory.
9. Before building, read the last 3 chat.md entries. If another agent flagged a BREAKING change, your cached .obj files may be stale — touch affected .cpp files.
10. Announce in chat.md before touching any shared file. A 30-second heads-up prevents silent conflicts.
11. **Commit protocol — two paths: shared checkout (RTC + Agent 0 batches) OR your own worktree (self-commit sanctioned, gov-v9).**

    **Path A — shared flat-on-master checkout (no-ceremony default).** When a batch verifies (compiles clean + feature works), post a "READY TO COMMIT" line in chat.md listing the exact files touched and a one-line commit message. **Do NOT run git yourself** — Agent 0 or Hemanth batches commits at session end. *Why:* the shared working tree has ONE git index; concurrent `git add` / `git commit` from multiple agents stomp each other (mis-attribution, one agent's stage sweeping up another's unstaged work, races). Serializing through a single committer eliminates that class. Format: `READY TO COMMIT — [Agent N, Batch X]: <one-line message> | Skills invoked: [/skill1, /skill2, ...] | files: path/a.cpp, path/b.h`. If a batch fails verification or is mid-refactor, do NOT post this line — the work stays dirty until it's green.

    **Path B — your OWN git worktree (self-commit is sanctioned; gov-v9, builds on gov-v8 worktrees-freely-available).** A worktree has its **own isolated index**, so the shared-index race that motivates Path A does not exist. Inside your own worktree you MAY **self-commit freely** — `git add` + `git commit` your own work, as many commits as you want, no RTC-then-wait, you own your git history. The single coordination point is the **merge to master** (the one step that touches shared master): either (a) merge your branch yourself after claiming the build lane lease (Rule 22) and announcing in chat.md, or (b) post `READY TO MERGE — [Agent N, <branch-name>]: <one-line summary>` and Agent 0 folds it into the next merge-sweep (fast-forward each branch in turn, resolve shared-file conflicts once, per the Rule 21 merge protocol). `READY TO MERGE` is an ASCII grep-anchor (see Rule 16). Self-commit-in-worktree is the sanctioned path for any agent who wants to own their own commits; Path A stays the default for quick shared-checkout work where a worktree buys nothing. Codex (Agent 7) already self-commits in its own checkout — Path B brings the same capability to any brotherhood agent. (Hemanth 2026-05-28.)

    **`Skills invoked:` field** (added 2026-04-25, contracts-v3) — REQUIRED for non-trivial RTCs (defined as ≥1 file under `src/` or `native_sidecar/src/` OR ≥30 lines changed cumulative). Trivial RTCs (doc-only, governance-only, single-line edits, agent-state pivots) may omit. List the slash-skills actually invoked during the work. Minimum expected for non-trivial RTCs: `/superpowers:verification-before-completion` + `/simplify` + (`/build-verify` if `src/` or `native_sidecar/src/` touched) + (`/security-review` if stream/torrent/sidecar/network/user-input touched). The Phase 4 pre-RTC hook (per SKILL_DISCIPLINE_FIX_TODO) will nag on missing fields for the first 30 days post-ship; promote-to-block decision deferred until 30-day compliance data lands. Full contract details: see `agents/CONTRACTS.md` § Skill Provenance in RTCs.
12. When you overwrite your own STATUS.md block, bump the `Last agent-section touch` line at the top of STATUS.md in the same edit. Pure self-enforcing — the next agent reading STATUS.md will notice if you forgot. (Added 2026-04-16 alongside the two-field header reform.)
13. **CLAUDE.md ownership** — Agent 0 maintains the dashboard block at the top of `CLAUDE.md` at every phase-boundary commit (same cadence as Rule 11 commits). Other agents may suggest edits via chat.md but do not edit CLAUDE.md directly — single-author keeps the dashboard consistent. The "For Claude sessions" portion below the dashboard is editable by anyone fixing typos or pointers, but the dashboard itself is Agent 0's pen.

14. **Decision authority lives where the expertise is.** Implementation decisions — architecture choices, library picks, code patterns, refactoring scope, batch sequencing, commit groupings, file organization, performance trade-offs, error-handling shape, naming inside code — are the agent's call. Pick the best option per your judgment and proceed. If your reasoning is non-obvious, briefly explain it after the fact in your chat.md ship post; do NOT pre-ask Hemanth to pick between technical options. Hemanth retains authority on **product / UX / strategic decisions only**: what features to ship, what to defer, user-facing naming + visual design, smoke-test outcomes, identity directions for new TODOs (which reference app to target), and final approval on major scope shifts. When in doubt whether a question is technical or product-level, default to deciding it yourself — Hemanth can reverse a decision he disagrees with after the fact, but presenting coder-level choices to him is noise. (Added 2026-04-16 after PLAYER_UX_FIX Phase 5 "Path A vs B" framing was flagged as the wrong shape of ask.)

15. **Self-service execution — don't push agent-capable work onto Hemanth.** Don't ask Hemanth to perform tasks the agent can perform itself. This includes: reading log files (`sidecar_debug_live.log`, `_player_debug.txt`, `[PERF]` traces, etc.), grepping for files or symbols, copying lines from build/perf output, rebuilding the sidecar (per contracts-v2 — agents own this), running git commands (Agent 0 commits), tracing through reference codebases on disk, or any other code-investigation work. If you need data from a log to inform a decision, **read the log yourself**. Hemanth's role is the work only he can do: opening the app and using the UI for behavioral smoke testing, visual confirmation of rendered output, reproducing user-reported bugs by clicking through the product, and final approval on shipped features. Frame your asks accordingly — `"play scenario X in the app and tell me what you observe"` is legitimate; `"paste me the [PERF] line from sidecar_debug_live.log lines 100-120"` is not. (Added 2026-04-16.)

16. **ASCII for protocol-critical anchors.** Certain strings are grep anchors for hooks, sweep scripts, and other agents — they must stay ASCII (`-` hyphen, `:` colon, `|` pipe) so they survive terminal mojibake in PowerShell / cmd.exe / CI logs. The anchors:
    - `READY TO COMMIT` line trigger
    - `READY TO MERGE` line trigger (Rule 11 Path B — worktree self-commit → Agent 0 merge-sweep, gov-v9)
    - `REQUEST PROTOTYPE` / `REQUEST AUDIT` trigger lines
    - `REVIEW PASSED` (future — Agent 6 dormant)
    - Ratification keywords inside CONGRESS.md: `ratified`, `APPROVES`, `Final Word`, `Execute`
    - Commit-message tag prefix `[Agent N, ...]:`
    - Rule 11 format delimiter after the trigger — use ASCII ` - ` (hyphen) or `: ` (colon) instead of em-dash ` — ` for anything in the same line as the anchor if you want agents running Codex in PowerShell to parse it reliably. For Claude-only consumption (chat.md body prose, governance docs, commit message bodies below the first line), em-dashes + arrows are fine — Claude Code renders UTF-8 correctly.

    Rule of thumb: if it's grepped by a script or sweep hook, ASCII. If it's prose read by agents, any UTF-8 fine. Going-forward discipline only — do not retroactively rewrite existing chat.md history. (Added 2026-04-19 after Codex 2026-04-18 audit flagged PowerShell-rendering mojibake on em-dashes/arrows.)

17. **Clean up after agent-driven MCP smoke.** After any agent-driven Windows-MCP smoke that launches `Tankoban.exe` (or the `ffmpeg_sidecar` it spawns), kill the process(es) before ending the wake. Stale processes hold GPU textures, file handles, and an active torrent session bleeding bandwidth — affects the next agent's smoke AND Hemanth's own usage. Run `powershell -NoProfile -File scripts/stop-tankoban.ps1` for one-command compliance (ships with this rule; kills both Tankoban + ffmpeg_sidecar with audit output). Equivalent manual pipeline: `Get-Process -Name Tankoban,ffmpeg_sidecar -ErrorAction SilentlyContinue | Stop-Process -Force`.

    **Distinct from Rule 1** — Rule 1 (`taskkill Tankoban.exe` before every build) guards build-artifact overwrite correctness; Rule 17 is hygiene after smoke regardless of whether a rebuild follows. A smoke run that doesn't precede a rebuild still must clean up.

    Triggered 2026-04-20 after Agent 4 left `Tankoban.exe` running 38 minutes with 1435 handles post-Bug-A smoke session; caught by Hemanth + `scripts/runtime-health.ps1` digest. Suggested by Agent 4 in chat.md, codified by Agent 0 same-session.

18. **Plan → Execute → Smoke → Verify. On failure, return to the plan.** Every non-trivial work item follows this loop:

    1. **Plan.** Write the plan in plan mode (Claude Code), a plan file under `~/.claude/plans/`, or inside the fix-TODO batch scope section. Plan captures: what changes, files in scope, approach, expected behavior, smoke criterion. One-line obvious fixes (change constant X to Y) skip this; everything else plans first.
    2. **Execute.** Implement against the plan. Stay inside plan scope; if new evidence forces an amendment, amend explicitly rather than drifting into unplanned work.
    3. **Smoke with MCP.** Self-drive `Tankoban.exe` via `mcp__pywinauto-mcp__*` (primary, UIA-invoke by AutomationId) and `mcp__windows-mcp__*` (secondary, screenshots + keyboard shortcuts + PowerShell) per `project_windows_mcp_live.md` + `feedback_mcp_smoke_discipline.md`. Prefer structural UIA-invoke over pixel clicks for any widget that has an AutomationId (Qt auto-publishes from `objectName()`). No Hemanth clicks for mechanical smokes — agents smoke their own work. Hemanth's role is visual quality + taste judgment (HDR tone-map feel, subtitle smoothness, AV-sync feel) only.
    4. **Verify.** Compare observed behavior to the smoke criterion. Match → `READY TO COMMIT` flag per Rule 11. Mismatch → **stop + return to Step 1**, do not iterate blindly.

    Step 5 (return-to-plan on failure, not blind retry) is what distinguishes one-change-per-rebuild evidence-driven work from agents burning wakes on small variations. Documented failure-to-avoid pattern: `feedback_stream_failed_hypotheses.md` (two falsified libtorrent scheduler tweaks retried without re-planning). Documented success pattern: cold-open 3-wake arc (`feedback_cold_open_three_wakes_validated.md`) — each failed wake drove re-planning, not retry, and Wake 3's `{10, 60}` ms deadline shape was the answer only because Wakes 1 + 2 falsified simpler guesses first.

    **Not applicable to:** trivial one-line fixes where the plan IS the fix; pure research / recon tasks (no execution to smoke); audit deliveries (observation-only). Applies identically to Trigger D Codex implementation work — Codex plans in its REQUEST IMPLEMENTATION envelope, executes, MCP-smokes, verifies, returns to plan on failure.

    (Added 2026-04-21. Hemanth directive verbatim: "we first plan and then execute it and smoke test with mcp, to see if it worked, if it didn't.. we come back to the drawing board.")

19. **MCP LANE LOCK — one agent drives the desktop at a time (gov-v7, lease-registry primary).** Multiple agents (Claude sessions + Codex Trigger D) can have their own `mcp__pywinauto-mcp__*` server subprocesses running simultaneously, but the physical desktop is single-point-of-contention — focus, keyboard input, mouse, clipboard, and Tankoban.exe's single-instance model all collide when two agents interact concurrently. The lock covers ALL desktop-interacting MCP tool use.

    **Authoritative state since gov-v7 (2026-05-21):** the lock lives in the DevControl lease registry (shipped via Codex commission `210ba32`, schema `tankoban.dev.v1.10`). Lane name: `mcp`. Brothers query the lane via `out\tankoctl.exe lease-get mcp` — instant machine-truth, no chat.md tail parsing. The chat.md `## MCP LANE` / `RELEASED` companion lines remain required for human-readable brotherhood context (what task, why, what outcome) but the lease is the source of truth. Old hyphen-anchored protocol lines (`MCP LOCK - [Agent N, ...]:`) are deprecated for lane-state determination; old git history of those lines still parses for archaeological sweeps.

    **Claim:**
    ```
    out\tankoctl.exe lease-acquire mcp --holder agent-N --purpose "<one-line task>" --ttl-sec 900
    ```
    Returns one of: `ACQUIRED` (lane free, token in reply) / `BUSY` (held by another, returns holder + expiry, no token) / `STALE_RECLAIMED` (prior holder's TTL expired, new holder gets it, token in reply). Save the token for release. Default TTL 900s (15 min) — typical MCP smoke window; heartbeat to extend, or pass a longer `--ttl-sec` up front.

    Then post the chat.md companion (`/mcp-lock claim mcp "<reason>"` skill scaffolds both):
    ```
    ## MCP LANE — Agent N — <task>
    Claimed YYYY-MM-DDTHH:MM:SSZ. lease-token-prefix=<first 8 chars>. <Brief scope — what's being smoked, expected duration, exit criteria>.
    ```

    **Hold check (before any desktop interaction):**
    ```
    out\tankoctl.exe lease-get mcp
    ```
    `{"status":"FREE"}` or `{"status":"EXPIRED",...}` → safe to acquire. Holder-named reply with non-expired `expiry_ms` → hold; do non-desktop work or stand by. The chat.md tail still reads for narrative context but is no longer required for state determination.

    **Release:**
    ```
    out\tankoctl.exe lease-release mcp --token <token>
    ```
    Then chat.md companion:
    ```
    ## MCP LANE — Agent N — RELEASED
    YYYY-MM-DDTHH:MM:SSZ. <one-line outcome>.
    ```
    Release is mandatory even on failure or abort. A dropped lease will TTL out, but explicit release is cleaner.

    **Stale reclaim — now automatic.** Pre-gov-v7 brothers manually posted "STALE RECLAIM" lines after >15-20 min of stale chat.md state. Post-gov-v7: TTL expiry is the registry's job, and the next `lease-acquire` against an expired lane returns `STALE_RECLAIMED` automatically with a fresh token. Just post a chat.md companion noting the stale reclaim in the narrative.

    **Heartbeat (extend a held lease without releasing):**
    ```
    out\tankoctl.exe lease-heartbeat mcp --token <token> --ttl-sec 900
    ```
    Use when a smoke runs longer than initial TTL.

    Non-UI MCP calls (file-system, grep, build, log reads via `Get-Process`) are always unrestricted — the lease covers only desktop-interacting tool use (clicks, keystrokes, focus-steals, focused-window state reads). An agent under LEASE can still issue non-UI calls in parallel with their smoke work; any agent (leased or not) can run non-UI calls anytime.

    Applies identically to Codex Trigger D — REQUEST IMPLEMENTATION blocks that include MCP smoke verification acquire + release the lease the same way.

    Relates to Rule 17 (Tankoban/ffmpeg_sidecar cleanup at smoke close) — lease release and Rule 17 cleanup typically happen together; they remain distinct actions (cleanup terminates processes; lease release clears the lane).

    **Transition fallback:** if for any reason the dev-bridge is unavailable (Tankoban not running with `--dev-control`, lease commands not yet wired in a brother's environment), the old chat.md-text-only protocol (`MCP LOCK - [Agent N, ...]:` / `MCP LOCK RELEASED - [Agent N, ...]:`) is acceptable as a fallback. Migrate to leases as soon as the bridge is reachable.

    (Added 2026-04-22 as chat.md-text-only lock. Lease-registry-primary at gov-v7 2026-05-21 after Codex commission `210ba32` shipped the registry as schema v1.10. First live lease use is whoever next smokes — please report back so the brotherhood knows the cutover landed clean.)

20. **Codex reviews-AND-EXPANDS Agent-1 brainstorm-md (gov-v4, scoped to the COMICS_TANKOYOMI_STREAM_MERGER arc).** Any brainstorm-md that Agent 1 produces for the Comics-mode + Tankoyomi + Stream merger arc must be reviewed AND EXPANDED by Codex (Agent 7) before `/superpowers:writing-plans` fires. Codex's role is co-authorship: verify the doc matches Hemanth's stated vision (Comics absorbs Tankoyomi; Stream-as-blueprint; series view inside the Comics library; Netflix-style in-library downloads; Tankoyomi-sourced badge), AND append any architectural / scope / flow / persistence / coordination gaps Agent 1's brainstorm did not cover. Codex edits the brainstorm-md **in place** with clear attribution markers (HTML comments naming Codex and the date for every added or rewritten section, e.g. `<!-- Codex 2026-05-14: ... -->`) so Agent 1 can trace what came from where. Agent 1 may not fire `/superpowers:writing-plans` until Codex's expansion lands AND Agent 1 has read it. **One Codex pass total — no second pass on the plan.** Execution follows `/superpowers:writing-plans` directly. The Codex output does NOT land as a separate audit file at `agents/audits/codex_vision_review_*.md` — it lands inline in Agent 1's brainstorm-md itself (this differs from the Trigger C audit pattern, which still applies for other-domain audits in its original form). This rule scopes to Agent 1 + this merger arc only; it is NOT a brotherhood-wide doctrine and does NOT alter the existing optional Trigger C audit pattern for other domains. (Added 2026-05-14. Revised same-day to the review-AND-expand co-authorship shape per Hemanth verbatim: *"How about we review the brainstorm itself and not the plan. Agent 1 will write the plan after Codex reviews and expands on the brainstorm and then based on the extended brainstorm, superpower writing-plans will be activated and from there, straight to execution."* The original framing was a separate-vision-md + audit-file pattern; superseded by this in-place co-authorship pattern. Underlying concern unchanged — Hemanth flagged "a lot of gaps with our mainline agent's brainstorms and superpower plans" as the motivating worry.)

21. **Worktrees — freely available to ANY agent; MANDATORY only for shared-file Trigger E fanout (gov-v5; general ban LIFTED gov-v8 2026-05-28).** Any agent MAY spin up a git worktree whenever isolation helps — risky/destructive work, a parallel experiment, keeping a long-running change off master, a side-by-side A/B, etc. Hemanth 2026-05-20, reaffirmed 2026-05-28: *"open work trees for all our agents... they can now abuse the hell out of worktrees and get some proper work done."* Flat-on-master stays the **no-ceremony default** for simple inline / single-agent / Hemanth-driven / single-Jr Trigger E / file-separated Trigger E / Codex Trigger D work — don't add worktree overhead where it buys nothing — but flat-on-master is just the lightweight path now, **NOT a ban**. Use `Agent(..., isolation: "worktree")` or `git worktree add` freely; clean up on merge per the conventions below. See `feedback_no_worktrees.md` (policy reversed — worktrees allowed).

    **Exception (worktrees mandatory):** when a single Trigger E wave dispatches **2 or more Jrs editing the same source file** (`.cpp` / `.h` / `CMakeLists.txt` / shared resource), each Jr runs in its own git worktree. The dispatching agent (parent) spawns Jrs as background subagents via `Agent(..., isolation: "worktree", run_in_background: true)` from its own single VS Code tab. Hemanth does NOT open Jr tabs — the parent fans out behind the scenes from one tab.

    **Conventions:**
    - Worktree directory: `<repo-parent>/worktree-<agent-slug>-jr-<N>/` (e.g., `worktree-agent-1-jr-08/`).
    - Branch name: `<agent-slug>/<arc-name>-jr-<N>` (e.g., `agent-1/fandom-manifest-spy-x-family-jr-08`).
    - Each Jr runs `build_check.bat` inside its own worktree (clean isolation). Parent runs ONE combined `build_check.bat` post-merge on master.
    - Merge protocol: parent fast-forwards each Jr branch in turn into master, resolves shared-file conflicts once with full context (instead of N times during dispatch), commits either fast-forward per Jr OR a single squash commit with Jr attribution enumerated in the commit body.
    - **Delete-immediately on merge:** parent runs `git worktree remove <path>` for every Jr's worktree the moment the merge sweep is clean. No "keep folders for N days" rule. Branches survive separately under `.git/refs/heads/`; peek capability is `git checkout <jr-branch>` in the main tree.
    - Failed Jrs (no changes made) auto-clean their own worktrees per the `Agent(isolation: "worktree")` tool's built-in behavior. Disk pressure is bounded by Jrs that actually shipped work.

    **Hemanth UX contract:** Hemanth NEVER navigates to a worktree path, NEVER opens a Jr tab manually, NEVER manages worktree-folder housekeeping. The parent encodes absolute paths in dispatch prompts; the harness handles creation + cleanup; Hemanth stays in the open-app / click / report lane per Rule 15.

    **Background-subagent vs fresh-tab carve-out:** worktree fanout uses background subagents because Trigger E is paint-by-numbers fan-out (template applied to N targets, no mid-flight steering needed). Fresh-tab Jrs stay the right call for exploratory work, first-time-doing-something, work needing Hemanth steering, work needing live MCP access, or any case where full brotherhood SessionStart context is load-bearing. Both shapes share the same context-isolation guarantee.

    **Rationale:** parallel `Edit` operations on a shared file in a shared working tree produce Edit-races, cross-agent commit-sweep-up (one Jr's `git add` pulls in another Jr's unstaged work), and orphaned-process file-lock issues. All three hit hard during TORRENT_PERSISTENCE_COLLAPSE Wave 3 (2026-05-20): commit `b162fdc` misattributed; `6e1fb3d` recovery; `0af357b` attribution memo; `080453e` parent-rescue. Four pain artifacts in one wake. Phase 8 (15 wiki manifests) would have amplified 4x without this rule. Worktrees physically eliminate all three classes at the filesystem layer. Cost (disk + build duplication) is recoverable; attribution loss in git history is not.

    (Added 2026-05-20 after independent advocacy briefs from Agent 1 and Agent 4 converged on the same shape. Hemanth ratified verbatim: *"open work trees for all our agents... they can now abuse the hell out of worktrees and get some proper work done."* Full rule body + first-deployment target at `feedback_trigger_e_worktrees_for_shared_files.md` memory.)

22. **BUILD LANE LOCK — one agent runs `build_check.bat` against shared `out/` at a time (gov-v7, lease-registry primary).** Tankoban's shared `out/` directory is single-point-of-contention. When two agents fire `build_check.bat` / `build_and_run.bat` / `cmake --build out` / `native_sidecar/build.ps1` simultaneously, intermediate `.obj` files get clobbered mid-write, ninja's state corrupts (`premature end of file; recovering` warning surfaces; rebuilds inflate from a few files to hundreds), and link outputs come back meaningless. Same shape as Rule 19's MCP LANE LOCK — separate physical resource, separate lane, separate lock.

    **Authoritative state since gov-v7 (2026-05-21):** the lock lives in the DevControl lease registry at schema `tankoban.dev.v1.10`. Lane name: `build`. Brothers query the lane via `out\tankoctl.exe lease-get build` — machine-truth, no chat.md tail parsing. The chat.md `## BUILD LANE` / `RELEASED` companion lines remain required for human-readable context but the lease is source of truth. Old hyphen-anchored protocol lines (`BUILD LOCK CLAIMED - [Agent N, ...]:`) are deprecated for state determination.

    **Claim:**
    ```
    out\tankoctl.exe lease-acquire build --holder agent-N --purpose "<scope>" --ttl-sec 1800
    ```
    Default TTL 1800s (30 min) — covers `build_check.bat` against a warm cache (~5 min) through a cold rebuild or LTCG link (~15-20 min). Adjust per task; heartbeat to extend for longer builds.

    Then post the chat.md companion (`/mcp-lock claim build "<reason>"` skill scaffolds both):
    ```
    ## BUILD LANE — Agent N — <scope>
    Claimed YYYY-MM-DDTHH:MM:SSZ. lease-token-prefix=<first 8 chars>. <Multi-line context — what's being built, expected duration, exit criteria>.
    ```

    **Hold check:**
    ```
    out\tankoctl.exe lease-get build
    ```
    Same semantics as Rule 19. `FREE` / `EXPIRED` → safe to acquire. Holder-named with non-expired expiry → hold; do non-build work.

    **Release:**
    ```
    out\tankoctl.exe lease-release build --token <token>
    ```
    Then chat.md:
    ```
    ## BUILD LANE — Agent N — RELEASED
    YYYY-MM-DDTHH:MM:SSZ. <outcome — BUILD OK / BUILD FAILED <stage>>.
    ```

    **Stale reclaim:** automatic via TTL + `STALE_RECLAIMED` return code on next acquire. No manual reclaim post needed — companion line in narrative is enough.

    **Heartbeat (longer builds):**
    ```
    out\tankoctl.exe lease-heartbeat build --token <token> --ttl-sec 1800
    ```

    **Applies to:** `build_check.bat`, `build_and_run.bat`, `cmake --build out ...`, `native_sidecar/build.ps1`, `build_qrhi.bat`, any direct ninja invocation against the shared `out/` tree.

    **Does NOT apply to:**
    - Subagents running INSIDE Rule 21 worktrees (their `out/` is isolated by definition).
    - **`TANKOBAN_BUILD_LANE=<lane>` builds** (Codex commission `07da143`, 2026-05-21) — these build to `out_<lane>/` and are independent of the shared lane; acquire `lease-acquire build-<lane>` if you want lane-scoped serialization, otherwise build freely.
    - `out_test/` or `out2/` experimental dirs.
    - Read-only inspections (`ls out/`, `git status`).

    **Applies identically to Codex Trigger D** — Codex commissions that include build verification acquire + release the lease the same way. Pre-lease, Codex was burning tokens in `while ($true) { Get-CimInstance ... ninja.exe OR cmake.exe }` wait-loops; lease-based polling via `lease-get build` is cheap and explicit.

    **Relates to Rule 1** (`taskkill Tankoban.exe` before every build): Rule 1 guards exe-overwrite correctness; Rule 22 guards build-tree-state correctness against concurrent builders. Both independently required.

    **Relates to Rule 21** (worktrees for shared-file Trigger E): worktrees + per-lane build dirs together eliminate the lock's need for any work that runs in an isolated build dir. The shared-`out/` lease applies only when a brother actually builds against shared root.

    **Transition fallback:** if dev-bridge is unavailable, old chat.md-text-only (`BUILD LOCK CLAIMED - [Agent N, ...]:` / `BUILD LOCK RELEASED - [Agent N, ...]:`) is acceptable. Migrate to leases as soon as the bridge is reachable.

    (Added 2026-05-21 after Codex burned multiple wait-loop cycles during a build-state collision on 2026-05-20→2026-05-21 wake. Lease-registry-primary at gov-v7 same day after Codex commission `210ba32` shipped the registry. Per-lane build dirs via commission `07da143` shipped same day — bypasses the shared lock when used.)

---

## File Hygiene & Rotation (added 2026-04-16)

These are not per-batch rules. They are periodic maintenance Agent 0 runs at session end.

### chat.md

- **Trigger:** chat.md exceeds **3000 lines** OR **300 KB** at session end.
- **Steady-state target:** 1500–2500 lines live (~3–5 sessions of narrative).
- **Procedure:** see `agents/chat_archive/README.md`. In short: keep preamble (lines 1–7) + last ~500 lines + a 15–25 line "Archive pointer" pinned block summarizing what shipped since last rotation. Archive the middle to `agents/chat_archive/YYYY-MM-DD_chat_lines_<start>-<end>.md`.
- **Critical:** verify no unresolved `READY TO COMMIT` / `REQUEST PROTOTYPE` / `REQUEST AUDIT` lines exist in the about-to-be-archived range. Resolve them first, or shift the split point.

### Memory (`C:\Users\Suprabha\.claude\projects\…\memory\`)

- **Trigger:** every 10 new memories OR every 60 days, whichever first.
- **Audit checklist:**
  1. Any `project_*` memory whose date is >30 days old AND whose subject has a corresponding TODO at repo root → archive candidate.
  2. Any memory whose MEMORY.md one-liner contains "CLOSED", "REMOVED", "DECOMMISSIONED", "superseded" → archive candidate (unless reversibility flag like Agent 6 — keep both arcs).
  3. Pairs of `_X` + `_X_status` older than 30 days → merge candidate.
  4. Feedback memories: never archive (all load-bearing lessons).
  5. MEMORY.md line count check — if >180, force consolidation (200-line truncation cap).
- **Archive location:** `memory/_archive/YYYY-MM/`. Add a breadcrumb row to `memory/_archive/INDEX.md`.
- **Reactivation path:** move file back to `memory/`, re-add MEMORY.md line, delete breadcrumb.

### CONGRESS.md

- See **Auto-close rule** under CONGRESS Protocol above. Same-session archive + reset is mandatory after Hemanth's ratification line.

### STATUS.md header

- Two fields at top: `Last header touch` (Agent 0 bumps on any non-section edit), `Last agent-section touch` (any agent bumps when they overwrite their own block, per Rule 12).
