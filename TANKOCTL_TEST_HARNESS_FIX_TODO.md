# TANKOCTL_TEST_HARNESS FIX TODO

**Author:** Agent 0 (Coordinator)
**Date:** 2026-05-30
**Owner:** Agent 0 (arc author + sequencer + sweep); execution via Codex Trigger D / Agent 9 (DeepSeek) / Agent N Jrs per phase

---

## §1 — Strategic intent

tankoctl today is a fast **state-reader + synthetic-input driver + log correlator** (schema `tankoban.dev.v1.10`, ~140 commands, ~3,800 LOC across `src/devtools/`). It already lets agents smoke the app without Hemanth's clicks for *mechanical* checks. But every smoke is still "agent fires N commands, reads N JSON blobs, eyeballs whether it looks right." There is no pass/fail, no repeatability, no regression net, and a documented blind spot: the bridge proves a signal *fired*, not that the screen *rendered right* (`feedback_dev_bridge_visual_blindspot`).

This arc takes tankoctl from **remote control → self-checking test harness.** The payoff is direct and user-facing: the "Hemanth, please click and tell me what you saw" loop shrinks to "the agents ran the suite, here's green/red." Fewer manual smokes, repeatable regression catching, and eventually a build that gates on tankoctl scenarios.

Hemanth verbatim, 2026-05-30, on seeing the five-tier vision: **"I AM NOT EVEN READING ANYTHING AFTER I SAW THAT. WE ADDING THAT 'A LOT' EFFECTIVE IMMEDIATELY, WHATEVER THAT A LOT IS."** — full green-light on all five tiers, phased rollout accepted ("phased ships clean").

The whole arc is **additive within v1.x** (new commands, new optional payloads/replies) — no breaking v2 rewrite. Bridge extensions fire on commission, no §5 governance gate (additive infrastructure, per `project_dev_control_bridge.md` extension procedure §5).

---

## §2 — Phase breakdown

1. **P1 — Assertion + scenario engine** — ✅ **SHIPPED 2026-05-30 (Agent 0, self-executed; Codex out of quota).** `expect` / `run <scenario.json>` / `wait-for` / `record <file>` verbs, all client-side (new TU `tools/tankoctl_scenario.{h,cpp}`); dot-path + ops (== != >= <= > < contains matches exists), client-side `wait_for` polling (server-side would freeze the single-threaded UI bridge — no schema bump). Full smoke matrix GREEN + independent reviewer pass (4 review fixes folded: numeric coercion gated on actual.isDouble, invalid-regex surfaced, wait-for value guard, error-reply surfaced in wait timeout). — Owner: Agent 0 — Wakes: 1 (done)
2. **P2 — Debt closure (the v1.9 dropped-12 + sidecar queues)** — network observability (list/throttle/block requests via shared QNAM observer), real perf counters (frame-times / CPU / GPU), scanner pause/resume/trigger, cache hit-rate stats, signal tracer, sidecar decoder/render queue depth. Each needs new instrumentation infra — heavier C++, per-domain. — Owner: Codex Trigger D + Agent 9, split by sub-system under domain-agent attribution — Wakes: ~3-4
3. **P3 — Visual assertion (kill the pixel blind-spot)** — generalized `ui-screenshot <objectName>` → PNG + perceptual hash; `expect-visual <widget> ~= baseline` golden-image diff with tolerance. — Owner: Codex Trigger D / Agent 9 — Wakes: ~2
4. **P4 — Developer ergonomics** — `tankoctl repl` interactive mode, `describe <cmd>` self-documenting payload+example, `--watch` live filtered `events.jsonl` stream, `--json`/human output toggle. — Owner: Agent N Jr / Agent 9 (low-risk, parallelizable) — Wakes: ~1-2
5. **P5 — CI gate (the endgame)** — headless runner that boots Tankoban with dev flags, runs the scenario suite, tears down, emits junit-XML + markdown report; wire into a build target. Advisory-first → blocking once trusted. — Owner: Agent 0 (harness/scripts) + Codex Trigger D (runner) — Wakes: ~2-3

Dependency order: **P1 first** (substrate everything else asserts against) → **P2 / P3 / P4 parallelizable** → **P5 last** (consumes P1 scenarios + P3 visual).

---

## §3 — Deliverables per phase

### P1 deliverables
- `tankoctl expect <snapshot.path> <op> <value>` — exit-0 on pass, exit-1 on fail; ops `== != >= <= > < contains matches exists`. Dot-path into any command's JSON reply.
- `tankoctl run <scenario.json>` — ordered steps: `{cmd, payload, wait_for?, expect?[]}`; stops on first failed assert (or `--keep-going`); prints per-step PASS/FAIL + summary line + exit code.
- `tankoctl record <out.json>` — capture a live session of issued commands into a replayable scenario.
- Server-side deterministic `wait_for` predicates with timeout: extend v1.8 `ui-wait-for` into a general `wait_for {path, op, value, timeout_ms}` so scripts don't sleep-and-hope (e.g. wait until `stream.buffer.percent >= 95` or `download.state == complete`).
- Files: `tools/tankoctl.cpp` (CLI: expect/run/record + scenario parser + assert engine), `src/devtools/DevControlServer.{h,cpp}` (schema bump + general wait predicate), `src/ui/MainWindow.cpp` (wait-predicate routing). Sample scenarios under `scripts/tankoctl_scenarios/`.
- Smoke matrix: `expect` true→exit0 / false→exit1; a 5-step scenario green; a deliberately-broken scenario red at the right step; record→replay round-trip reproduces.

### P2 deliverables
- **Network observability**: shared `QNetworkAccessManager` observer; `net-list-requests` / `net-get-active` / `net-throttle-set <kbps>` / `net-block-host <host>`. (Refactors ~15 emission sites onto one observed QNAM — the reason these were cut at v1.9.)
- **Perf counters**: `perf-get-frame-times` / `perf-get-cpu-usage` / `perf-get-gpu-usage` backed by real counter infra (not the v1.9 stubs).
- **Scanner control**: `scanner-pause` / `scanner-resume` / `scanner-trigger` (requires giving VideosScanner a pause API — currently one-shot).
- **Cache stats**: `cache-get-stats` (PosterCache hit/miss counters).
- **Signal tracer**: `app-trace-signals` (opt-in signal-emission tracer).
- **Sidecar queues**: `sidecar-get-decoder-queue` / `sidecar-get-render-queue` (flip from `NOT_YET_IMPLEMENTED` — needs a periodic push on the sidecar stats loop or a sync IPC bridge; touches `native_sidecar/src/`).
- Smoke matrix: each new command round-trips real values against a running app; throttle visibly slows a stream; block-host kills a request; scanner-pause halts an in-progress scan.

### P3 deliverables
- `ui-screenshot <objectName>` → writes PNG to `out/` + returns perceptual hash (extends the v1.8 player-only screenshot to any named widget).
- `expect-visual <objectName> ~= <baseline-id> [--tolerance N]` → golden-image diff, exit-0/1.
- Baseline manifest: committed hashes (text), PNGs stay local in `out/` (no binary sprawl — see §5 Q3).
- Smoke matrix: screenshot of a known panel produces stable hash across two runs; intentional UI change trips the diff.

### P4 deliverables
- `tankoctl repl` — persistent interactive prompt (history, tab-discovery of commands).
- `tankoctl describe <cmd>` — prints payload schema + a worked example, generated from the dispatcher's own registration.
- `tankoctl --watch [filter]` — live-streams `events.jsonl` with optional prefix/type filter.
- `--json` vs default human-table output across read commands.
- Smoke matrix: repl runs a 3-command session; describe prints a correct example for 3 sampled commands; watch surfaces a live torrent event.

### P5 deliverables
- `scripts/tankoctl_ci.ps1` (or `.sh`) — boot Tankoban with `--dev-control TANKOBAN_DEV_UI_SIM=1 TANKOBAN_DEV_WRITE=1`, run all `scripts/tankoctl_scenarios/*.json`, collect results, tear down (Rule 17 cleanup), emit `out/tankoctl_ci_report.{xml,md}`.
- A `tankoctl-smoke` build/run target or `/`-skill wrapper so any agent runs the full suite in one call.
- Advisory report first; flip to a merge-blocking gate only after the suite has proven stable (§5 Q2).
- Smoke matrix: full suite runs unattended start-to-finish; report distinguishes pass/fail/error; a seeded regression shows red in the report.

---

## §4 — Acceptance criteria

- **P1 closed**: `expect`/`run`/`record` all round-trip; a sample 5-step scenario goes green, a broken one goes red at the correct step, recorded session replays identically; BUILD OK + tankoctl ping healthy against a running app. This is the gate the rest of the arc asserts against.
- **P2 closed**: every restored command returns real (non-stub) values verified against running app; network throttle/block produce observable effects; scanner-pause halts a live scan; sidecar queue depths report non-`NOT_YET_IMPLEMENTED` numbers.
- **P3 closed**: a stable widget yields a repeatable hash across runs; an intentional visual change trips `expect-visual`; baseline PNGs confirmed absent from git (manifest-only committed).
- **P4 closed**: repl/describe/watch each demonstrated; `describe` example for a sampled command actually executes successfully when copy-run.
- **P5 closed**: full suite runs unattended, report emitted, a seeded regression is caught red. Hemanth eyeballs one report to confirm it reads clean.

Per-phase: BUILD OK (`build_check.bat`, exe mtime verified per `feedback_verify_exe_mtime_after_build`) + bridge round-trip smoke + reviewer pass before master (mandatory for Codex/Agent 9 work).

---

## §5 — Hemanth ratification questions

Bridge extensions are additive infra and do NOT need §5 ratification to *proceed* — these are the few genuine product/workflow calls where Hemanth's preference changes the shape. All have a strong recommendation; per `feedback_no_rhetorical_ratification_pause` I'll proceed on the recommendation unless Hemanth says otherwise.

1. **Scenario file format — JSON or a friendlier DSL?** — *Recommend JSON for v1.* It matches the existing wire format, is trivially machine-recordable/replayable, and needs no parser invention. Human-authoring sugar (YAML/DSL) can layer on later if scenarios get hand-written often. Reasoning: don't block the keystone phase on bikeshedding a syntax.
2. **CI gate — blocking or advisory first?** — *Recommend advisory-first → blocking once trusted.* Emit a report the brotherhood reads; only flip to "red blocks merge" after the suite has demonstrably low false-positive rate. Reasoning: a flaky gate that cries wolf gets ignored or disabled — earn the block.
3. **Visual baselines — commit PNGs or keep local?** — *Recommend committed hash-manifest (text) + PNGs local in `out/` only.* Committing baseline images is exactly the binary-sprawl that killed worktrees and violates `feedback_world_class_repo_not_at_brotherhood_cost`. Reasoning: keep the repo world-class; hashes are enough to gate, PNGs regenerate locally.
4. **Pacing — all five tiers land over multiple wakes, P1 first.** — *Recommend confirming you're good with phased rollout* (you already said "phased ships clean"). "Effective immediately" = the arc is locked + spec written this wake + P1 fired tonight, not all 3,800 LOC in one blast.

---

## §6 — Ownership

- **Primary owner:** Agent 0 — arc author, spec, phase sequencing, commit sweeps, schema-line merge resolution.
- **Execution engines:** Codex Trigger D (native, for novel CLI control-flow + surgical C++ — P1, P3, P5 runner) and Agent 9 / DeepSeek (locked-plan scoped-src execution — proactive per `feedback_deepseek_execution_engine_proven`); Agent N Jrs (Trigger E) for the low-risk parallelizable P4.
- **Cross-agent contributors (P2, per-domain attribution):** Agent 3 (sidecar queues + perf/frame-times — player/sidecar), Agent 4 (network observability — stream/torrent emission sites), Agent 5 (cache stats — library), Agent 1/2 as their domains touch scanner/cache.
- **Codex Trigger D scope:** P1 assert/scenario engine; P2 network observer + sidecar IPC push (highest blast-radius); P3 perceptual-hash diff; P5 headless runner. Routed native, never `mcp__codex__codex` (`feedback_trigger_d_native_not_mcp`).
- **Mandatory:** reviewer pass before master on every Codex/Agent 9 commission (same as all Trigger-D work).

---

## §7 — Dependencies

- **Blocked by:** nothing — substrate (v1.10 bridge + per-page `dispatchDevCommand` + `events.jsonl` + `UiInteractionDispatcher` + `SystemIntrospection`) already exists.
- **Blocks:** every future agent-driven smoke benefits; eventually the build gate (P5) becomes a dependency *for* merges if flipped to blocking.
- **Internal order:** P1 → {P2, P3, P4} → P5.
- **Memory references:** `project_dev_control_bridge` (architecture + extension procedure + ship history), `feedback_dev_bridge_visual_blindspot` (P3 motivation), `feedback_trigger_d_native_not_mcp`, `feedback_trigger_d_prompt_template`, `feedback_deepseek_execution_engine_proven`, `feedback_verify_exe_mtime_after_build`, `feedback_world_class_repo_not_at_brotherhood_cost` (P3 baseline storage), `feedback_no_concurrent_builds_same_out_dir`.

---

## §8 — Risks

1. **Schema-line merge collisions** (multiple parallel commissions bump `tankoban.dev.v1.X` on the same line). Mitigation: Agent 0 resolves the schema string at sweep time; each executor writes its assigned version literally per Rule 14 (the Phase D pattern — versions may skip, detect capability by command presence not exact version).
2. **P2 instrumentation blast radius** (network observer refactors ~15 QNAM sites; sidecar push touches `native_sidecar/src/`). Mitigation: split P2 into independent sub-commissions per subsystem; each ships + smokes alone; do NOT batch (`feedback_one_fix_per_rebuild`).
3. **Visual baselines drift / false positives** (P3 golden images trip on benign re-renders). Mitigation: perceptual hash + tolerance, not exact-pixel; advisory before any gate; baselines regenerable.
4. **Flaky CI gate erodes trust** (P5). Mitigation: advisory-first; only block once false-positive rate proven low; deterministic `wait_for` predicates (P1) instead of sleeps to kill timing flake.
5. **Big-file growth** — `tankoctl.cpp` already 1,554 LOC; `SystemIntrospection.cpp` 1,047. Adding scenario engine risks a refactor-threshold file. Mitigation: split the scenario/assert engine into its own translation unit (e.g. `tools/tankoctl_scenario.{cpp,h}`) rather than growing the monolith.
6. **Cowboy execution against Hemanth's "immediately"** — temptation to blast all phases at once. Mitigation: phased, one commission at a time, reviewer-gated; this doc is the contract that keeps it disciplined.

---

## §9 — Wake budget

- P1: ~2-3 wakes (CLI engine + wait predicates + smoke).
- P2: ~3-4 wakes (instrumentation infra is the heavy part; parallelizable across domain agents).
- P3: ~2 wakes.
- P4: ~1-2 wakes (parallelizable, low-risk).
- P5: ~2-3 wakes.
- **Total: ~10-14 wakes**, P1 startable immediately. No calendar dates (`feedback_no_human_days_in_agentic`).

---

## §10 — Anti-patterns to avoid

1. **DO NOT** break v1.x — every command is additive; no removals/renames (those force v2 and break every existing client per the schema-versioning rule).
2. **DO NOT** route Trigger D through `mcp__codex__codex` — native Codex only (independence + self-commit).
3. **DO NOT** commit baseline PNGs to git (binary sprawl). Hashes/manifest only.
4. **DO NOT** use sleep-and-hope timing in scenarios — use deterministic `wait_for` predicates.
5. **DO NOT** batch multiple phases into one rebuild/commit — one change, one rebuild, one smoke.
6. **DO NOT** grow `tankoctl.cpp` / `SystemIntrospection.cpp` past refactor threshold — split new engines into their own TUs.
7. **DO NOT** claim a command works without a live round-trip against a running app (`firstFrameSeen`-style "signal emit ≠ truth" trap; verify exe mtime advanced before trusting BUILD OK).
8. **DO NOT** treat synthetic-UI / hash-diff as proof the screen looks right to a human — visual-taste smokes still gate on Hemanth's eyes for aesthetics (`feedback_dev_bridge_visual_blindspot`).

---

## §11 — Evidence pointers

- Memory: `project_dev_control_bridge.md` (full architecture, naming convention, schema rule, extension procedure, v1.0→v1.10 ship history, dropped-12 catalog).
- Source: `src/devtools/DevControlServer.{h,cpp}`, `src/devtools/UiInteractionDispatcher.{h,cpp}`, `src/devtools/SystemIntrospection.{h,cpp}`, `tools/tankoctl.cpp`, per-page `dispatchDevCommand` stubs, `src/core/JsonlEventLog.{h,cpp}`.
- In-code debt markers: `DevControlServer.h:71-72` (dropped-12 list), `SystemIntrospection.cpp:580,759` (v1.9.1 deferrals), `VideoPlayer.cpp:4534` + `src/ui/player/CLAUDE.md:77` (sidecar queue NOT_YET_IMPLEMENTED).
- Prior pattern: the 2026-05-18 v1.1+v1.2 RTCs and the 2026-05-19 Phase D 7-commission arc (canonical extension templates).

---

## §12 — Close criteria

Arc closes when all five phases meet §4, and a single unattended `tankoctl_ci` run boots the app, executes the full scenario suite (functional + visual), tears down clean, and emits a green report — with one seeded regression proven to surface red. Hemanth eyeballs one report to confirm it reads clean and trustworthy. At that point the brotherhood's mechanical smokes are repeatable, scriptable, and (optionally) merge-gating.

---

## §13 — Standing contracts (survive close)

1. **Additive-within-v1.x forever** — bridge never breaks existing clients; capability detection by command presence in `ping.commands`, not exact schema version.
2. **Scenario files live in `scripts/tankoctl_scenarios/`** as JSON; the assert/scenario engine lives in its own TU, not the tankoctl monolith.
3. **Visual baselines: hash-manifest committed, PNGs local-only** — permanent repo-hygiene contract.
4. **Every new command ships with a live round-trip smoke + reviewer pass before master.**
5. **`describe <cmd>` is generated from dispatcher registration** — new commands self-document by construction (no separate doc drift).
6. **CI gate advisory until trusted, then blocking** — and stays deterministic (`wait_for`, never sleeps).

---

## §14 — Archive trigger

When all five phases close, move to `agents/_archive/todos/TANKOCTL_TEST_HARNESS_FIX_TODO.md` and mark CLOSED in the CLAUDE.md "Active Fix TODOs" table. Fold the durable contracts (§13) into `project_dev_control_bridge.md` so they survive as bridge doctrine.
