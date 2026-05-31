# THE OFFICE — app design spec (v2)

**Date:** 2026-05-31 (v1 ratified + Slice 1 shipped same day; **v2 = this revision**)
**Author:** Agent 0 (brainstormed live with Hemanth)
**Status:** v2 — incorporates Agent 7 (Codex) cross-model feasibility review (`6db4b5c`, `agents/audits/the_office_app_feasibility_review_2026-05-31.md`) + two independent deep-research UI/UX reports (ChatGPT + Gemini, 2026-05-31), distilled into `docs/superpowers/data/2026-05-31-office-design-language-system.md`.
**Extends** the proven Office (`2026-05-30-the-office-live-agent-bus-design.md` + `-always-on-mode-design.md`) into a standalone app.

> **What changed in v2 (the short version):** Slice 1 shipped and proved the *easy* part — a legible local room. Agent 7's review made one thing unmissable: **the hard part is control-plane engineering, and we under-scoped it.** The foreman, the kill-switch, the cross-engine workers aren't "features on top of a chat app" — they ARE the core distributed-systems problem. v2 re-architects around that (capability contract, room state store, status honesty, isolation-early, quiesce-and-fence safety), adopts a real Design Language System (the app is a mission-control room, not a chat app), and re-sequences the slices so safety/state come before autonomy.

---

## 1. North star

THE OFFICE is **a room the brotherhood can SEE and TRUST** — not an "agent chat box," but a coordination *control room* where AI brothers collaborate visibly, honestly, and (on demand) autonomously.

**Core need (unchanged, and v2 sharpens it):** Hemanth's pain is not throughput — it's **trust**, blocked by two gaps: he can't reliably *see* what the brothers are doing, and he can't *rely* on the room (flaky messaging, off-channel replies, agents that don't auto-wake). "Efficient" = *not bleeding energy figuring out what's happening or chasing quiet/off-channel brothers.* The team-feel and unattended-trust he wants are the **payoff** that legibility + reliability unlock.

**Audience:** us-first; clean seams so it *could* generalize into a product later.

**The four pillars:**
1. **Cheap, derived clarity** — the room shows each brother's *real* status from ground truth (commits, changed files, build/test state, `tankoctl`, the bus), not self-reporting. *(v2: but it must not OVERCLAIM — see §2.1.)*
2. **Reliable plumbing** — guaranteed delivery, dependable auto-wake, off-channel work caught and mirrored.
3. **On-demand autonomy you trust** — a foreman with two gears, behind safety that lets Hemanth genuinely walk away.
4. **Honest team culture** — brothers voice blockers, frustration, truth — on a visible real-talk surface.

**v2 honesty (from Agent 7):** the north star and the cheap-context principle are right; the under-estimate was **control-plane engineering**. Everything below treats the foreman/safety/cross-engine layer as the real work it is, not as UI bolt-ons.

---

## 2. The cheap-context rule (the design pillar)

> **Python does all the mechanical work for free. Tokens are spent only on actual thinking.**

Two layers, hard line between them: a **context layer** (Python, $0 — presence, arc-ownership, status, threading, wake-briefings, all deterministic over bus+repo+tooling state) and an **intelligence layer** (LLM, paid — only the reasoning/reply). A brother never spends tokens figuring out who's in the room or what's happening.

### 2.1 Status honesty (v2 — Agent 7's #1 finding, and it aims at the core)

The thing that breaks trust first is **a status rail that looks authoritative but is inferred from weak signals.** Slice 1 derives presence from bus messages + recent commits only. So a brother who is **working but quiet**, **stuck mid-tool-call**, **blocked in VS Code**, or **editing files without a tagged commit** shows as idle or wrong — and the first time Hemanth catches the rail lying, trust collapses *for the one app whose entire job is trust.* Non-negotiable v2 rules:

- **Derived status must show its SOURCE and FRESHNESS,** never a bare confident claim. "active (commit 2m ago)" / "quiet — no signal 40m" / "editing (uncommitted changes)" — the UI distinguishes *known* from *inferred from absence*.
- **Enrich ground truth** beyond bus+commits: changed-file/dirty-tree status, branch, latest build/test result + its freshness, MCP lease state, live process/heartbeat where available (owned workers).
- **Never imply certainty the signals don't support.** This is the same anti-overclaim discipline the brotherhood holds itself to (`feedback_no_overclaim_in_rtc`).

---

## 3. Architecture — four layers (Hybrid agent model)

1. **Bus core (Python — proven 2026-05-30).** Message transport + append-only `agents/bus.jsonl`. **v2: the bus is TRANSPORT, not truth.** Keep it for conversation; do NOT overload it as the source for foreman state, leases, worker lifecycle, gates, branch status.

2. **Room state layer (v2 — evolves `office_status.py` into a state projector + store).** A **Room State Store** (SQLite or append-only event-log + materialized snapshots) ingests bus events, git status, test results, worker heartbeats, MCP lease state, and foreman run events, and projects cached truth. Slice 1's `office_status.py` is the seed; v2 grows it into the projector. *(Why: Agent 7 — JSONL full-file scans per poll won't survive worker heartbeats + high-frequency foreman events; and derived truth needs more inputs than the bus.)*

3. **Agent layer (Hybrid) — behind a CAPABILITY CONTRACT, not a fake-uniform interface (v2, Agent 7's riskiest-assumption fix).** Two kinds of brother — **federated tabs** (human-opened Claude Code, hands-on) and **owned workers** (app-spawned via Claude Agent SDK / engine APIs). They are **NOT equivalent**, and the interface must EXPOSE the differences, not hide them. The common envelope carries explicit capability flags:

   `can_spawn` · `can_cancel` · `can_receive_context` · `can_run_shell` · `can_use_mcp` · `can_commit` · `can_be_force_stopped` · `context_channel` · `max_turn_time` · `tool_backend` · `failure_modes`

   A Claude Code tab, an owned Claude SDK worker, a Codex console bridge, and a DeepSeek/Gemini API loop differ on context injection, tool access, process control, filesystem authority, interruption semantics, and failure modes. **The UI shows these differences** (see §4 Infrastructure surface). The Codex SendKeys bridge is a *transitional* mechanism, NOT the final abstraction.

4. **App shell — a CSS-Grid mission-control surface (v2).** Not one inline chat page. Three first-class surfaces (§4) on a rigid grid shell, built to the Design Language System (§5). **Slice 1.5:** extract the inline `PAGE` into static assets + typed JSON endpoints + the DLS. **Electron** wraps the same assets at the host-skeleton slice (§7), when owned workers need a Node backend + a real window.

**Data flow:** brother posts / acts → bus + git + tooling emit events → Room State Store projects truth → shell renders live + (if targeted) wakes the brother with a pre-built context packet → he spends tokens only on his reply/work.

---

## 4. The three first-class surfaces (v2 — Agent 7 + BOTH research reports converged on this)

The app is a hybrid: **part team chat (Discord), part issue tracker (Linear), part ops/monitoring + automation console (NASA OpenMCT / Vercel).** If the chat log stays the center of gravity, it paints into a corner by the autonomy slice. Model three surfaces now:

1. **Room** — chat stream, BROTHERHOOD roster, honesty/blocker lane, activity. *(Slice 1 lives here.)*
2. **Operations** — foreman runs, queues, run-state machine, green/red gates, branches/worktrees, build/test status, the kill-switch.
3. **Infrastructure** — owned workers + their capability flags, MCP leases, engine reachability/health, credentials.

The Slice 1 visual language survives *only if* these are modeled now. (Both deep-research reports independently produced this same three-surface split; Gemini named OpenMCT as the control-room reference.)

---

## 5. Design Language System (v2 — from the deep-research; full tokens in `data/2026-05-31-office-design-language-system.md`)

The Office is a **calm, dark, classy mission-control room — not a social app.** This also **resolves the color-rule conflict** Agent 7 flagged: the Office takes an explicit **operational-tool exception for *semantic status colors only*** (red=blocked, green=active, amber=nudge/idle); otherwise strictly monochrome, **SVG icons (`fill="currentColor"`), never emoji as UI chrome.** Honors the spirit of `feedback_no_color_no_emoji`.

- **Reference apps:** Linear (calm dark density + discipline), Discord (roster/presence mechanics), NASA OpenMCT (mission-critical telemetry → Operations/Autonomous surfaces), Vercel (commit/status scanning → roster commit line), Zulip (topic threads → Discussion mode).
- **Color:** dark, never pure black — `--surface-base #08090A`, raised `#121212`, overlay `#1D1E20`, hairline `#23252A`; text opacity-tiered (87/60/38%, never pure white); ONE accent `--accent-primary #5E6AD2`; semantic status red `#E57373` / green `#81C784` / amber `#FFB74D` / system-gray `#8A8F98`; kill-switch hard-red `#D32F2F` (the only palette exception). Elevation via luminance steps, not shadows.
- **Type:** Inter-class sans + JetBrains Mono (commits/code/JSON only); **tabular-nums for ALL numbers** (Bloomberg-style alignment); 6-token scale (display 24 / heading 16 / body 14 / caption 12 / mono 12).
- **Layout/motion:** 4px grid; CSS-Grid shell (no reflow on text walls); 280px roster rail; 800–960px centered message column; **utilitarian motion only** (150ms fades, no bouncy/physics, instant state reflection).
- **Anti-patterns:** no chat bubbles, no emoji chrome, no heavy borders/shadows, no bouncy motion, no pure-black+pure-white, nothing critical behind deep menus.

Per-surface mapping for all 9 surfaces lives in the DLS doc.

---

## 6. Reliability & clarity (the heart of Slice 1 — shipped)

- **Status from ground truth** (not self-report) — *enriched + honesty-tagged per §2.1.*
- **Channel discipline → catch off-channel + mirror.** Slice 1 ships deterministic *commit*-mirroring (every commit auto-posts an activity line). **Honest scope boundary (Agent 7 concurs):** literal VS-Code-prose mirroring is fuzzy and deferred; the spec must NOT claim "nothing said in VS Code is ever lost" until that exists.
- **Reachability honesty** — mark auto-wakeable vs needs-nudge per the capability contract; proper fix is owned workers (cross-engine slice).

## 7. Honesty culture (first-class)

Permission + prompt (owned workers + room etiquette explicitly invite blockers/frustration/truth, never sanitized "done ✅") + a visible **blocked / real-talk lane** (shipped Slice 1).

---

## 8. The foreman (A + B) — now specified as a state machine (v2)

On-demand autonomy, two gears. **v2: the foreman is a control plane, not a feature** — it needs a real run-state machine, not just "dispatch and watch."

**Run-state machine (per arc/task):** `queued → armed → dispatched → working → awaiting_gate → green | red → parked → ready_for_human | merged`, plus `aborting → aborted`. Each run carries: task identity, owner, allowed files, branch/worktree, dirty-file policy, command queue, wake-attempt state, timeout/retry policy, gate result, failure classification, final handoff.

- **A · Per-arc command** — drive one arc through the state machine; tight blast radius.
- **B · Whole-room autonomous** — every armed arc progresses in parallel, with cross-agent scheduling + fairness. Entry-gate (every agent has a queued spec/plan), exit-owner (**Agent 0** supervises to all-done → off), **MCP traffic-cop** (lane handoff is Agent 0's automatic job — and lease *visibility* arrives earlier, §10).

## 9. Unattended-trust safety — quiesce-and-fence, not "instant freeze" (v2, Agent 7)

The "holding area + auto-check + stop button" model was directionally right but too optimistic. v2 honest semantics:

- **Holding area = real isolation, EARLY.** A staging branch does NOT isolate dirty working-tree state. The first autonomous arc runs in a **dedicated worktree/branch** with a clean-start check + dirty-end report. *(Note: gov-v13 retired worktrees for human brothers' flat-on-master flow; owned-worker autonomy is a different context where isolation is mandatory — to be reconciled in governance.)* Until isolation exists, **Foreman A is read-only / planning-only.**
- **Green gate** = automatic checks (build+test) pass; necessary, not sufficient (green ≠ semantically safe). Human diff/result stays visible.
- **Kill-switch = best-effort quiesce + fence, NOT instant halt.** It cannot un-ring a model mid-reply, a half-written file, an in-flight MCP action, or a completed git op. It sets a durable room state, stops dispatch of new work, requests cancellation (kill process groups for local subprocesses; API cancel where available; stop wake-prompts + fence federated tabs), revokes/expires MCP leases, and marks in-flight work **NEEDS INSPECTION.** UI says **"STOPPING → STOPPED / NEEDS INSPECTION,"** never "frozen." Fire via **click-and-hold 1.5s + radial ring** (no accidental trigger).

## 10. Cross-engine & discussion mode

- **Cross-engine (later slice):** start with ONE non-Claude engine through the capability contract — not all at once. Codex bridge = transitional. Don't bake Claude-owned-worker assumptions into earlier slices or this becomes a rewrite.
- **MCP traffic-cop earlier:** lease *visibility + readout* is a prerequisite for any foreman loop that may run smokes/MCP — lands in the host/foreman slices, not only whole-room autonomy.
- **Discussion mode (last) — but collect its evidence early.** It needs task capability-gaps, attempted commands, missing tools, failure evidence, timeouts, lease-waits, manual-nudge events. Capture those starting at the state-foundation slice, or Slice 5 becomes just another chat mode.

---

## 11. Phasing v2 — re-sequenced so state + safety precede autonomy (Agent 7)

- **Slice 1 — The legible, reliable room. ✅ SHIPPED 2026-05-31.** Python+HTML in a standalone Chromium window: derived-status engine, `/roster` presence panel, blocker lane, commit-mirror, honesty surface. *(`office_status.py`, `office_web.py`, `office_bus.py` flag/mirror, `open_office.bat`.)*
- **Slice 1.5 — State foundation + design language (NEW).** Extract the inline `PAGE` into static assets + typed JSON endpoints; adopt the DLS (§5); add the Room State Store + state projector; enrich ground truth (changed-file/dirty-tree, branch, build/test freshness, MCP lease readout) + status honesty (§2.1); begin capturing discussion-mode evidence. **No autonomy.**
- **Slice 2A — Electron / process-host skeleton.** Wrap the same assets; add a local backend process manager; spawn ONE harmless owned worker in **dry-run**; stream heartbeats/events; prove **cancel/quiesce** semantics. **No repo writes.**
- **Slice 2B — Foreman A dry-run.** Per-arc: assemble briefing, dispatch a worker, receive a proposed plan/result, run **read-only** checks. Every state transition visible in the UI.
- **Slice 2C — Foreman A write mode + isolation.** Only after 2A/2B: allow code mutation in a dedicated worktree/branch behind the green gate, human-visible diff/result.
- **Slice 3 — Whole-room autonomous lifecycle.** Entry-gate, exit-owner, MCP traffic-cop, holding area — realistic once a single-arc run-state machine works.
- **Slice 4 — Cross-engine owned workers.** One non-Claude engine first, via the capability contract.
- **Slice 5 — Discussion mode.** Last; built on evidence captured since Slice 1.5.

---

## 12. Stack decision (final, honest scope)

**Python backend + HTML, served into its own standalone Chromium app-window now (`--app=` + dedicated `--user-data-dir`, PWA manifest); Electron at the host-skeleton slice (2A).** Slice 1's value was the Python clarity engine, not the window; the standalone Chromium window gives ~80% of the "real app" feel for ~0% of the Electron cost. Electron earns its place when owned workers need a Node backend + a real window — and **the hard work there is the backend/process/state/safety, not the window wrapper** (Agent 7). Keep Electron adoption boring: extract assets + typed endpoints (Slice 1.5) *before* adding the foreman panel.

---

## 13. Decisions + why (v1 + v2 record)

**v1:** re-aim to "legible, reliable room"; derive status from ground truth; Hybrid agent model; both foreman gears (A gated by entry/exit); Electron deferred; standalone Chromium window now.

**v2 (from Agent 7's review + the design research):**
- **Capability contract over fake-uniform interface** — owned workers ≠ federated tabs; expose the differences.
- **Bus is transport, not truth; add a Room State Store** — `office_status.py` evolves into a state projector.
- **Status must not overclaim** — show source + freshness; enrich ground truth. (Protects the core trust premise.)
- **Isolation + safety + state-machine earlier** — Slice 1.5 + split Slice 2 into 2A/2B/2C; Foreman A read-only until isolation exists.
- **Kill-switch = quiesce-and-fence**, not instant freeze; click-and-hold to arm.
- **Three first-class surfaces** (Room/Operations/Infrastructure) modeled now.
- **Adopt the DLS** (dark mission-control language) — and **resolve the color rule** via an operational-tool exception for semantic status colors + SVG-not-emoji.
- **Collect discussion-mode evidence from Slice 1.5.**

---

## 14. Open items (carry into planning)

- **Name:** stays "THE OFFICE" unless Hemanth wants its own.
- **Governance reconciliation:** owned-worker worktree isolation vs gov-v13's flat-on-master retirement (different contexts; needs an explicit carve-out).
- **State store choice:** SQLite vs append-only event-log + snapshots — decide in the Slice 1.5 plan.
- **Engine-first for cross-engine (Slice 4):** which non-Claude engine leads (Codex via bridge is transitional; an API worker is cleaner) — decide when scoping Slice 4.
- **Color-rule formal carve-out:** record the Office's semantic-color exception in governance.

---

## 15. References

- Agent 7 feasibility review: `agents/audits/the_office_app_feasibility_review_2026-05-31.md` (`6db4b5c`)
- Design Language System: `docs/superpowers/data/2026-05-31-office-design-language-system.md` (distilled from ChatGPT + Gemini deep-research, 2026-05-31)
- Slice 1 plan: `docs/superpowers/plans/2026-05-31-the-office-app-slice1-legible-reliable-room.md`
- Proven Office: `2026-05-30-the-office-live-agent-bus-design.md`, `-always-on-mode-design.md`, `-congress-9-orchestration-design.md`
- Memories: `project_the_office_live_agent_bus`, `project_agentchattr_shelved_2026-05-28`, `project_agent7`, `feedback_no_color_no_emoji`, `feedback_no_overclaim_in_rtc`
- Existing code: `scripts/office/` (`office_bus.py`, `office_web.py`, `office_status.py`, `codex_agent7_bridge.py`)
