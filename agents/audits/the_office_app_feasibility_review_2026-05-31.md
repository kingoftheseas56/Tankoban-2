# THE OFFICE app feasibility and realism review

Date: 2026-05-31  
Reviewer: Agent 7 (Codex)  
Scope: review only; no code changes outside this audit.

## Bottom line

The plan is buildable in the broad sense, but not realistic as written if the implied expectation is "Slice 2-5 are mostly a UI/app-shell expansion on top of Slice 1." Slice 1 proves a useful local room: JSONL bus, live web surface, derived roster, blocked/activity message kinds, commit mirroring, and a Chromium app-window launcher. It does not prove the hard parts: owned worker lifecycle, reliable cross-engine wake/stop, autonomous merge safety, MCP lease arbitration under contention, or an operational state machine for foreman decisions.

Blunt read: the north star is good, the cheap-context principle is correct, and the hybrid model is the right direction. The under-estimate is control-plane engineering. The design talks like the foreman and kill-switch are features; in practice they are the core distributed-systems problem.

The single riskiest assumption: that app-owned SDK/API workers can be made to behave like currently federated human tabs behind one common "brother" interface without a major capability split. They will not be equivalent. A worker the app owns, a Claude Code tab, a Codex console bridge, and a DeepSeek/Gemini API loop have different context injection, tool access, process control, filesystem authority, interruption semantics, and failure modes.

What breaks first under real use: presence/status trust. The room will say things that look authoritative but are inferred from weak signals. Current Slice 1 derives presence from bus messages and recent commits, and the roster's current arc comes only from chat/blocked bus messages, not from actual file ownership or active process state. That is acceptable as a first indicator, but if Hemanth treats it as "real status," trust will erode the first time an agent is working without commits, stuck in a tool call, blocked in VS Code, or changed files without a tagged commit.

## Sources read

- `docs/superpowers/specs/2026-05-31-the-office-app-design.md`
- `docs/superpowers/plans/2026-05-31-the-office-app-slice1-legible-reliable-room.md`
- `scripts/office/office_bus.py`
- `scripts/office/office_web.py`
- `scripts/office/office_status.py`
- `scripts/office/codex_agent7_bridge.py`
- `scripts/office/tests/test_office.py`
- `scripts/office/tests/test_status.py`
- `open_office.bat`

## Observations

### What is already solid

Risk: low. Slice 1's bus core is simple and appropriate for the first room. `office_bus.py` appends JSONL records with a monotonic sequence, file locking, cursors, direct/all addressing, session identity, `deliver`, `drain`, `watch-peek`, and archive-on-close. The tests cover append, identity, cursor behavior, delivery, drain, nudge, and close.

Risk: low. The Slice 1 status engine is deliberately cheap. `office_status.py` keeps deterministic parsing/merging separate from IO, and `test_status.py` validates the canonical roster shape, commit parsing, blocked state, CLI output, flag posts, and commit mirroring.

Risk: medium. The UI proves the room surface, not the future app architecture. `office_web.py` is a single inline HTML/CSS/JS page served by stdlib `ThreadingHTTPServer`, polling `/messages` every 1.5s and `/roster` every 4s. That is fine for Slice 1 and probably for a handful of agents. It will become brittle once the UI must represent queues, foreman mode, worker subprocesses, MCP leases, holding branches, gate status, kill-switch state, and discussion rounds.

Risk: medium. The app-window path is pragmatic. `open_office.bat` starts the Python web server and opens Edge/Chrome in `--app=` mode with a dedicated profile. This gives the right "real app" feel before Electron. The risk is not the launcher; the risk is assuming "same HTML wrapped in Electron" is enough when Slice 2 adds process ownership, credentials, API calls, persistent worker state, and emergency controls.

### Architecture feasibility

Risk: high. The Hybrid agent model is correct conceptually but not yet specified at the level that makes it buildable. "One common brother interface" is only safe if the interface exposes capability differences instead of hiding them. Federated tabs can be nudged, but the app does not own their lifecycle. Owned workers can be stopped, but may not have the same tools or repo context. API workers can reason, but may not have Claude Code's local execution semantics. Codex currently needs a Windows console bridge that uses clipboard/SendKeys or console input injection; that is inherently less reliable than an API worker and should not be treated as the same class of participant.

Risk: high. The foreman A/B design under-specifies the state machine. "Take the throttle task to green" requires at least: task identity, owner, allowed files, current branch/worktree, dirty-file policy, command queue, wake attempt state, timeout handling, retry policy, review gate result, failure classification, and final handoff. Whole-room autonomous mode adds cross-agent scheduling and fairness. The spec names entry-gate, exit-owner, and MCP traffic-cop, but it does not define the transitions or invariants.

Risk: high. The holding-area + green-gate + kill-switch model is directionally right but too optimistic. A staging branch does not isolate uncommitted working tree state. A green build does not prove semantic safety. A stop button cannot instantly halt a remote model response, a subprocess already mutating files, an MCP action in flight, or a git operation that has already completed. The safety model needs "quiesce and fence" semantics, not just "freeze the room."

Risk: high. Cross-engine owned workers are the biggest unknown. The design postpones them to Slice 4, which is good for sequencing, but it still assumes the abstraction will exist. DeepSeek, Gemini, Codex, and Claude may differ on tool calling, system/developer instruction precedence, streaming, cancellation, context limits, code execution, local shell access, and pricing. If Slice 2/3 hard-code Claude-owned-worker assumptions, Slice 4 will be a rewrite.

Risk: medium. The cheap-context rule is good, but the current ground-truth sources are too thin for the claims. Today the derived room watches bus records and git log. The Slice 1 plan says git + bus + roster and mentions commits/activity; the broader spec claims commits, changed files, build/test state, `tankoctl`, and the bus. Changed files, live process state, active command, lease state, test result freshness, and branch status are not yet modeled in the shipped engine.

Risk: medium. Catch-and-mirror currently means "commit mirroring," not "off-channel prose mirroring." The Slice 1 plan is honest about deferring literal VS Code prose classification. That is the right tradeoff, but the design spec's language is stronger than the implementation: "nothing said in VS Code is ever lost" is not true yet.

Risk: medium. The current bus storage will hit operational limits before model limits. JSONL with per-request full-file scans is simple, but `/messages` reads the bus twice per poll, and `/roster` recomputes git log + bus activity on demand. For one local room this is fine. For autonomous mode with frequent activity events, worker heartbeats, tool events, and UI views, it needs either snapshots/indexes or a small state store.

Risk: medium. The current UI violates the stated gray/black/white governance if that rule is enforced literally. `office_web.py` uses blue, green, yellow, red, and per-agent accent colors for state. That may be acceptable for this internal tool if explicitly exempted, but right now the design/reference chosen in Slice 1 conflicts with the repo-level UI rule.

### Sequencing and scale

Risk: medium. Slice 1 -> Slice 2 -> Slice 3 is broadly the right order, but Slice 2 is too large as named. "Foreman A + Electron + first owned SDK worker + per-arc loop on one real arc" combines app shell migration, worker ownership, foreman command semantics, and green-gate behavior. That is too many unknowns for one slice.

Risk: high. The design reference chosen now will not fit if it remains a chat-first page with a side roster. Foreman panel, autonomous console, MCP-lane view, cross-engine membership, and discussion mode are not just extra widgets; they are separate work surfaces with different density and state needs. If the app continues as one inline page without a view model/state schema, the UI will sprawl.

Risk: medium. Discussion mode is correctly deferred, but it depends on instrumentation that should arrive earlier. A useful discussion mode needs task capability gaps, attempted commands, missing tools, failure evidence, and per-agent context packets. If those are not captured during Slice 2/3, Slice 5 will become another chat mode instead of a diagnostic mode.

Risk: medium. MCP traffic-cop should not wait until whole-room autonomy. Lease visibility and enforcement are prerequisites for any foreman loop that may run smokes or browser/UI operations. If Foreman A can touch MCP-backed workflows, it needs MCP lane status in Slice 2.

Risk: high. Holding-area safety is mis-sliced if it first appears in Slice 3. Even a one-arc Foreman A can damage the live branch if it runs autonomously on master with dirty files. The minimal branch/worktree/fence policy belongs before or inside the first foreman implementation.

## Recommendations

### Top 3 fixes before building more

1. Write the "brother capability contract" before Slice 2 implementation. Do not define one fake-uniform interface. Define a common envelope plus explicit capability flags: can_spawn, can_cancel, can_receive_context, can_run_shell, can_use_mcp, can_commit, can_be_force_stopped, context_channel, max_turn_time, tool_backend, and failure modes. The UI should show these differences instead of smoothing them over.

2. Split Slice 2 into two slices: "Electron/process host skeleton" and "Foreman A on one disposable arc." First prove the shell can own one worker, stream events, persist state, cancel/quiesce, and report health without doing real repo mutation. Then let Foreman A run against a tightly scoped task with branch/worktree isolation.

3. Promote safety and state modeling earlier. Before any autonomous code writes, define the run state machine and safety invariants: queued -> armed -> dispatched -> working -> awaiting_gate -> green/red -> parked -> ready_for_human/merged, plus aborting/aborted. The kill-switch should set a durable room state, stop dispatch of new work, request cancellation, revoke MCP leases, and mark in-flight work as needing human inspection.

### Architecture changes I would make

Risk addressed: high. Add a Room State Store before Foreman A. This does not need to be heavy; SQLite or append-only event log plus materialized snapshots is enough. Keep JSONL bus for conversation, but do not overload it as the source for foreman state, leases, worker lifecycle, gates, and branch status.

Risk addressed: high. Treat the bus as transport, not truth. Derived truth should come from a room-state engine that ingests bus events, git status, test results, worker heartbeats, MCP lease state, and foreman run events. The current `office_status.py` is a good seed, but it should evolve into a state projector with cached snapshots.

Risk addressed: high. Build cancellation as "best effort plus fencing," not "instant halt." The UI can say "STOPPING" and then "STOPPED / NEEDS INSPECTION"; it should not promise that every brother halts immediately. For local subprocesses, kill process groups. For API workers, request cancellation where available. For federated tabs, stop further wake prompts and mark them fenced. For git/MCP, block new operations and expire/revoke leases.

Risk addressed: high. Make branch/worktree isolation part of Foreman A, not only whole-room autonomy. The first autonomous arc should run in a dedicated worktree or staging branch with a clean-start check and a dirty-end report. If that is too much, Foreman A should be read-only/planning-only until isolation exists.

Risk addressed: medium. Keep Electron adoption boring. Do not rewrite the UI stack just to enter Electron. But do extract the current inline `PAGE` into static assets and define typed JSON endpoints before adding the foreman panel. The risk is not Electron; it is unstructured UI state.

Risk addressed: medium. Decide the color rule now. Either grant THE OFFICE an explicit internal-tool exception for semantic colors, or convert the UI to grayscale state treatments before it becomes the visual reference. Leaving this ambiguous will create churn.

### Resequenced slices

Recommended Slice 1.5: State foundation. Extract UI assets from inline Python, add a room-state schema, cache roster/status snapshots, add changed-file status, branch, dirty tree, latest build/test result freshness, and MCP lease readout. No autonomy yet.

Recommended Slice 2A: Electron/process host skeleton. Wrap the same HTML, add a local backend process manager, spawn one harmless owned worker in dry-run mode, stream worker heartbeats/events, and prove cancel/quiesce semantics. No repo writes.

Recommended Slice 2B: Foreman A dry-run. Per-arc command can assemble a briefing, dispatch a worker, receive a proposed plan/result, and run read-only checks. It must show every state transition in the UI.

Recommended Slice 2C: Foreman A write mode with isolation. Only after 2A/2B, allow code mutation in a dedicated branch/worktree with green gate and human-visible diff/result.

Recommended Slice 3: Whole-room autonomous lifecycle. Entry gate, exit owner, MCP traffic-cop, and holding area become much more realistic after a single-arc run state machine works.

Recommended Slice 4: Cross-engine owned workers. Start with one non-Claude engine in the capability contract, not all engines at once. Codex bridge is useful as a transitional mechanism but should not define the final abstraction.

Recommended Slice 5: Discussion mode. Keep it last, but start collecting the evidence it needs in Slice 1.5/2: failed commands, missing MCP tools, timeouts, lease waits, and manual-nudge events.

## Risk register

High: Common brother abstraction hides real capability gaps. Why: owned API workers, Claude tabs, Codex console injection, and DeepSeek/Gemini workers cannot honestly support the same operations.

High: Foreman state machine is not specified. Why: autonomous work needs durable states, retries, timeout handling, gates, failure classes, branch policy, and clear ownership.

High: Kill-switch promise is too strong. Why: cancellation is not uniformly available and cannot undo completed tool/git/filesystem operations.

High: Holding-area arrives too late. Why: Foreman A can still mutate the live repo before Slice 3 unless isolation is introduced earlier.

High: UI architecture may not scale from chat room to operations console. Why: current surface is a single inline page with polling and local rendering, while later slices need multiple coordinated state-heavy views.

Medium: Derived status may overclaim. Why: current ground truth is bus + commits; actual work can happen in uncommitted files, blocked tools, running processes, stale branches, and failed builds.

Medium: JSONL polling will become inefficient/noisy. Why: full-file scans and git log calls per request are okay now but poor fit for worker heartbeats and high-frequency foreman events.

Medium: Electron migration is conceptually fine but under-scoped. Why: the hard work is local backend/process/security/state, not the window wrapper.

Medium: Cross-engine timing is okay, but cross-engine influence must start earlier. Why: Slice 2/3 should not bake in Claude-only assumptions if Slice 4 is a stated goal.

Medium: Governance/UI mismatch. Why: current Office uses semantic colors while repo instructions say gray/black/white UI.

Low: Current bus/message primitives. Why: for a local trusted room with few agents, the JSONL bus is simple, tested, and easy to reason about.

Low: Chromium app-window bridge. Why: it is a pragmatic first shell and does not block Electron if the backend/UI boundaries are cleaned up soon.

## Answer to Hemanth's design-reference worry

The current design/reference is fine as a Slice 1 room, but not as the long-term information architecture. Do not let the chat log remain the center of gravity. The future app wants three first-class surfaces:

1. Room: chat, roster, honesty lane, activity.
2. Operations: foreman runs, queues, gates, branches, build/test status, kill-switch.
3. Infrastructure: workers, capabilities, MCP leases, engine reachability, credentials/health.

If those surfaces are modeled now, the Slice 1 visual language can survive. If everything is bolted onto the current chat page, the app will paint itself into a corner by Slice 3.

## One-line summary for chat.md

Agent 7 feasibility review ready - agents/audits/the_office_app_feasibility_review_2026-05-31.md. Blunt bottom line: plan is buildable, but control-plane/state/safety are under-scoped; fix capability contract, split Slice 2, and move isolation/state earlier before more buildout.
