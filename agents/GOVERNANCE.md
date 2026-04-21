# Agent Governance
<!-- governance-version: gov-v3 -->

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

## PROTOTYPE + AUDIT + IMPLEMENTATION Protocol (added 2026-04-14 — Agent 7; Trigger D added 2026-04-21)

Agent 7 is the Codex-driven agent. Originally (2026-04-14) scoped to write reference prototypes (Triggers A/B) and comparative audits (Trigger C) only — no src/ writes, no commits, no authority. Expanded 2026-04-21 to include implementation work (Trigger D) — Codex may ship actual code in `src/` when a domain agent posts a `REQUEST IMPLEMENTATION` block with task + scope + files + exit criterion. Trigger D is the implementation substrate for any of Agents 0-5 / 4B who wants to dispatch a scoped task to Codex instead of executing it themselves (rate-limited, stuck fix-loop, Anthropic 500s, or just substrate preference).

Purposes by trigger:
- **A/B:** give a domain agent a concrete second-perspective implementation to consult when facing an architecturally novel or risky batch. Reference-only; domain agent writes their own version.
- **C:** give a domain agent a structured comparative analysis against reference apps, with observations separated from hypotheses. Advisory; domain agent decides which gaps become work.
- **D:** let a domain agent dispatch a scoped implementation task to Codex when they want to. Authoritative; Codex ships the work and flags RTC for Agent 0 to sweep. Domain agent keeps ownership via the REQUEST block (scope + files + constraints); Codex executes inside that envelope.

### Triggers

Agent 7 accepts three trigger types. He writes nothing unsolicited. Triggers A and B produce prototypes; Trigger C produces audits.

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

**Trigger D scope discipline:**
- Codex stays inside the `Files:` list. If the task requires edits outside scope, Codex posts a clarification question in chat.md and stops — waits for the requesting agent to expand scope or rescope.
- Codex does NOT modify `CLAUDE.md`, `AGENTS.md`, `agents/GOVERNANCE.md`, `agents/CONTRACTS.md`, `agents/VERSIONS.md`, or other governance docs unless the REQUEST line explicitly authorizes.
- Codex does NOT expand into adjacent refactoring, polish, or cleanup unless the REQUEST line authorizes.
- One-fix-per-rebuild: if the task spans multiple independent fixes, Codex produces one RTC line per fix.
- All other brotherhood rules (1, 11, 14, 15, 17, CSS scoping, no color/emoji, etc.) apply identically to Trigger D work.

**When to pick which trigger:**

- **Trigger A/B (prototype):** architectural novelty; you want a reference-only second perspective to consult; you'll still reimplement in your own style. No src/ touch.
- **Trigger C (audit):** comparative analysis against reference apps; observation vs hypothesis separation; you'll validate hypotheses and decide which gaps become work. No src/ touch.
- **Trigger D (implementation):** you want Codex to actually do the work and ship it. You author the REQUEST block with files + constraints + exit criterion; Codex executes inside that envelope and flags RTC. You keep ownership (the REQUEST block IS your plan); Codex executes it.

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
11. When a batch verifies (compiles clean + feature works), post a "READY TO COMMIT" line in chat.md listing the exact files touched and a one-line commit message. Do NOT run git yourself — Agent 0 or Hemanth batches commits at session end. Format: `READY TO COMMIT — [Agent N, Batch X]: <one-line message> | files: path/a.cpp, path/b.h`. If a batch fails verification or is mid-refactor, do NOT post this line — the work stays dirty until it's green.
12. When you overwrite your own STATUS.md block, bump the `Last agent-section touch` line at the top of STATUS.md in the same edit. Pure self-enforcing — the next agent reading STATUS.md will notice if you forgot. (Added 2026-04-16 alongside the two-field header reform.)
13. **CLAUDE.md ownership** — Agent 0 maintains the dashboard block at the top of `CLAUDE.md` at every phase-boundary commit (same cadence as Rule 11 commits). Other agents may suggest edits via chat.md but do not edit CLAUDE.md directly — single-author keeps the dashboard consistent. The "For Claude sessions" portion below the dashboard is editable by anyone fixing typos or pointers, but the dashboard itself is Agent 0's pen.

14. **Decision authority lives where the expertise is.** Implementation decisions — architecture choices, library picks, code patterns, refactoring scope, batch sequencing, commit groupings, file organization, performance trade-offs, error-handling shape, naming inside code — are the agent's call. Pick the best option per your judgment and proceed. If your reasoning is non-obvious, briefly explain it after the fact in your chat.md ship post; do NOT pre-ask Hemanth to pick between technical options. Hemanth retains authority on **product / UX / strategic decisions only**: what features to ship, what to defer, user-facing naming + visual design, smoke-test outcomes, identity directions for new TODOs (which reference app to target), and final approval on major scope shifts. When in doubt whether a question is technical or product-level, default to deciding it yourself — Hemanth can reverse a decision he disagrees with after the fact, but presenting coder-level choices to him is noise. (Added 2026-04-16 after PLAYER_UX_FIX Phase 5 "Path A vs B" framing was flagged as the wrong shape of ask.)

15. **Self-service execution — don't push agent-capable work onto Hemanth.** Don't ask Hemanth to perform tasks the agent can perform itself. This includes: reading log files (`sidecar_debug_live.log`, `_player_debug.txt`, `[PERF]` traces, etc.), grepping for files or symbols, copying lines from build/perf output, rebuilding the sidecar (per contracts-v2 — agents own this), running git commands (Agent 0 commits), tracing through reference codebases on disk, or any other code-investigation work. If you need data from a log to inform a decision, **read the log yourself**. Hemanth's role is the work only he can do: opening the app and using the UI for behavioral smoke testing, visual confirmation of rendered output, reproducing user-reported bugs by clicking through the product, and final approval on shipped features. Frame your asks accordingly — `"play scenario X in the app and tell me what you observe"` is legitimate; `"paste me the [PERF] line from sidecar_debug_live.log lines 100-120"` is not. (Added 2026-04-16.)

16. **ASCII for protocol-critical anchors.** Certain strings are grep anchors for hooks, sweep scripts, and other agents — they must stay ASCII (`-` hyphen, `:` colon, `|` pipe) so they survive terminal mojibake in PowerShell / cmd.exe / CI logs. The anchors:
    - `READY TO COMMIT` line trigger
    - `REQUEST PROTOTYPE` / `REQUEST AUDIT` trigger lines
    - `REVIEW PASSED` (future — Agent 6 dormant)
    - Ratification keywords inside CONGRESS.md: `ratified`, `APPROVES`, `Final Word`, `Execute`
    - Commit-message tag prefix `[Agent N, ...]:`
    - Rule 11 format delimiter after the trigger — use ASCII ` - ` (hyphen) or `: ` (colon) instead of em-dash ` — ` for anything in the same line as the anchor if you want agents running Codex in PowerShell to parse it reliably. For Claude-only consumption (chat.md body prose, governance docs, commit message bodies below the first line), em-dashes + arrows are fine — Claude Code renders UTF-8 correctly.

    Rule of thumb: if it's grepped by a script or sweep hook, ASCII. If it's prose read by agents, any UTF-8 fine. Going-forward discipline only — do not retroactively rewrite existing chat.md history. (Added 2026-04-19 after Codex 2026-04-18 audit flagged PowerShell-rendering mojibake on em-dashes/arrows.)

17. **Clean up after agent-driven MCP smoke.** After any agent-driven Windows-MCP smoke that launches `Tankoban.exe` (or the `ffmpeg_sidecar` it spawns), kill the process(es) before ending the wake. Stale processes hold GPU textures, file handles, and an active torrent session bleeding bandwidth — affects the next agent's smoke AND Hemanth's own usage. Run `powershell -NoProfile -File scripts/stop-tankoban.ps1` for one-command compliance (ships with this rule; kills both Tankoban + ffmpeg_sidecar with audit output). Equivalent manual pipeline: `Get-Process -Name Tankoban,ffmpeg_sidecar -ErrorAction SilentlyContinue | Stop-Process -Force`.

    **Distinct from Rule 1** — Rule 1 (`taskkill Tankoban.exe` before every build) guards build-artifact overwrite correctness; Rule 17 is hygiene after smoke regardless of whether a rebuild follows. A smoke run that doesn't precede a rebuild still must clean up.

    Triggered 2026-04-20 after Agent 4 left `Tankoban.exe` running 38 minutes with 1435 handles post-Bug-A smoke session; caught by Hemanth + `scripts/runtime-health.ps1` digest. Suggested by Agent 4 in chat.md, codified by Agent 0 same-session.

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
