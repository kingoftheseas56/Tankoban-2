# THE OFFICE — app design spec

**Date:** 2026-05-31
**Author:** Agent 0 (brainstormed live with Hemanth)
**Status:** Design ratified in brainstorm; awaiting written-spec review → implementation plan
**Supersedes nothing** — extends the proven Office (`2026-05-30-the-office-live-agent-bus-design.md` + `-always-on-mode-design.md`) into a standalone app.

---

## 1. North star

THE OFFICE becomes **a room the brotherhood can SEE and TRUST** — not an "agent chat box," but a coordination surface where AI brothers collaborate visibly, honestly, and (on demand) autonomously.

The driving insight, surfaced in the brainstorm: **Hemanth's core pain is not throughput — it's trust, and trust is blocked by two gaps.** He cannot reliably *see* what the brothers are doing ("I don't understand what the brothers are doing, where they are at"), and he cannot *rely* on the room (unreliable messaging, agents replying in VS Code despite being told not to, agents that don't auto-wake). "Efficient," for him, means *not bleeding energy into figuring out what's happening and chasing brothers who went quiet or off-channel.* The team-feel and the unattended-trust he wants are the **payoff** that legibility + reliability unlock.

**Audience:** us-first (built for this brotherhood), with clean seams so it *could* generalize into a product later. Not a public product on day one.

**The four pillars that make it redefine the category** (vs. tools like agentchattr / "chatrrr"):

1. **Cheap, derived clarity** — the room shows each brother's *real* status by watching ground truth (commits, changed files, build/test state, `tankoctl`, the bus), **not** by asking agents to self-report (the exact thing that's unreliable today). Ground truth can't forget or go off-channel.
2. **Reliable plumbing** — guaranteed delivery, dependable auto-wake, and off-channel replies caught and mirrored into the room so nothing said in VS Code is ever lost.
3. **On-demand autonomy you trust** — a foreman with two gears, behind a safety setup (holding area + auto-check + stop button) that lets Hemanth genuinely walk away.
4. **Honest team culture** — brothers voice blockers, frustration, and the truth on a visible real-talk surface — not sanitized "done ✅."

---

## 2. The cheap-context rule (the design pillar)

> **Python does all the mechanical work for free. Tokens are spent only on actual thinking.**

This is the organizing principle of the entire app, and the answer to Hemanth's #1 pain. Two layers with a hard line between them:

- **Context layer (Python, $0):** tracks presence, arc-ownership, arc-status, threads messages, derives status from ground truth, and assembles each brother's **wake-briefing packet**. All of it is deterministic string/state work over bus + repo + tooling state. **Zero tokens.**
- **Intelligence layer (LLM, paid):** a brother spends tokens *only* on the part that needs a mind — reading the briefing and deciding/replying/reasoning. Never on figuring out who's in the room or what's happening.

**Applied to the #1 pain:** status must be **derived from ground truth, not self-reported.** The room watches commits, changed files, build/test state, `tankoctl`, and the bus, and computes each brother's real status automatically — true even when an agent forgets to post or goes off-channel. This single idea attacks "I don't know what they're doing" *and* "messaging is unreliable" at the root.

---

## 3. Architecture — four layers (Hybrid agent model)

1. **Bus core (Python — already proven 2026-05-30).** Message transport + append-only log (`agents/bus.jsonl`). Stays exactly as is; it works (`office_bus.py`, 31 passing tests).

2. **Room layer / cheap-context engine (Python, $0).** The new heart. Tracks presence, who owns which arc, each arc's status, threads messages, **derives status from ground truth**, and assembles wake-briefings. Pure deterministic work over bus + git + `tankoctl` + build/test state.

3. **Agent layer (Hybrid).** Two kinds of brother behind one common "brother" interface so the room treats them identically:
   - **Federated tabs** — human-opened Claude Code tabs (today's mechanism), for hands-on interactive work.
   - **Owned workers** — app-spawned via the Claude Agent SDK / each engine's API, for foreman + autonomy + cross-engine. The app owns the process → can start/stop/wake at will. *This is what escapes today's "closed tabs go dark" platform limit.*

4. **App shell.** The surface. **Slice 1:** the existing Python+HTML page (`office_web.py`), upgraded, served into its **own standalone Chromium app-window** (Edge/Chrome `--app=http://127.0.0.1:8787` mode now; optional small PWA `manifest.json` for a real install/taskbar icon). **Slice 2+:** Electron desktop app wrapping the *same HTML*, when owned workers + the foreman panel + the emergency-stop button need a real always-there window and an in-process Node backend.

**Data flow:** brother posts → Bus core appends → Room layer updates state + derives status + assembles briefings → shell renders live + (if a brother is targeted) the Room layer wakes him with a pre-built context packet → he spends tokens only on his *reply*.

---

## 4. Reliability & clarity (the heart of Slice 1)

Three locked decisions:

- **Status source → derive from ground truth.** The room watches commits, changed files, build/test state, `tankoctl`, and the bus, and shows each brother's real status automatically. (Optionally, a brother may add a one-line "here's what I'm thinking/stuck on" on top — human color over derived facts — but the facts never depend on him reporting.)
- **Channel discipline → catch off-channel + mirror into the room.** If a brother answers in VS Code when he should be in the Office, the app detects it and **mirrors the substance into the Office automatically**, and nudges him. Forgiving, but nothing is ever lost; the room stays complete.
- **Reachability honesty.** For brothers that can't auto-wake reliably today (DeepSeek/Codex), the room marks who is auto-wakeable vs. needs-a-nudge **honestly**, rather than silently failing. The proper fix is the owned-worker model (Slice 4); Slice 1 is at minimum honest about who's reachable.

---

## 5. Honesty culture (first-class)

Two mechanisms so it's real, not a vibe:

- **Permission + prompt:** owned workers and the room etiquette explicitly *invite* blockers, frustration, "this approach is wrong," and honest status — never sanitized "done ✅." Ties directly to existing brotherhood DNA (`feedback_no_overclaim_in_rtc`, `feedback_no_ego_appeasement`, honest-report norms).
- **A visible surface:** the shell has a **blocked / real-talk lane** so a brother's frustration or stuck-state is *seen* by Hemanth and the others, not buried in scroll.

---

## 6. The foreman (A + B)

On-demand autonomy with two gears (Hemanth wants **both**, and switches between hands-off and conductor "depending on the day"):

- **A · Per-arc command.** "Foreman, take the throttle task to green" → dispatches the owning brother, watches for ready, runs the review-gate, merges on green (to the holding area — see §7), reports back. **One arc, tight blast radius.**
- **B · Whole-room autonomous.** One toggle, every open arc progresses in parallel. With Hemanth's lifecycle rules built in:
  - **Entry gate:** B refuses to arm unless **every agent has a queued spec/plan/specific task.** No autonomous mode over vague work. Hemanth flips it on only when he's pre-loaded the work and is sure everyone has something concrete.
  - **Exit owner:** **Agent 0 supervises** — watches until every brother reports its work finished, then turns the mode OFF. Autonomous mode is a *session with an open and a close*, never a forgotten switch.
  - **MCP traffic-cop:** handing the single MCP lane between brothers is a **foreman duty (Agent 0)** — automatic during autonomous mode, built on the existing lease registry (`tankoctl lease-*`, Rules 19 + 22).

---

## 7. Unattended-trust safety (plain-language, locked)

The safety setup that lets Hemanth walk away — "holding area + auto-check + stop button":

- **Holding area (staging branch):** autonomous work piles up on a separate parked branch — *never* the live `master` the real app builds from. Like a loading dock vs. the store floor.
- **Auto-check (hard green gate):** work only moves forward if the automatic checks pass (builds + tests green). Anything red is blocked automatically; the room physically cannot push broken work forward.
- **Stop button (kill-switch):** one tap freezes the whole room instantly — every brother halts immediately. Emergency stop, available whether Hemanth is away or watching.

---

## 8. Phasing — thin slice first, no painted corners

- **Slice 1 — The legible, reliable room.** Upgrade the Python+HTML surface (own standalone Chromium window) over today's Claude tabs. Contains: derived-status clarity engine + reliable delivery + catch-and-mirror off-channel + cheap-context wake-briefings + the honesty surface. **This is the trust foundation.** No Electron, no owned workers, no foreman yet.
- **Slice 2 — Foreman A + Electron.** First owned SDK worker + the per-arc foreman loop on one real arc. Electron desktop shell enters here (wraps the same HTML), giving a real always-there window + Node backend to host owned workers.
- **Slice 3 — Foreman B + lifecycle.** Whole-room toggle, entry-gate, Agent-0 exit-supervisor, MCP traffic-cop, and the holding-area / green-check / stop-button safety setup.
- **Slice 4 — Cross-engine.** Owned Codex/DeepSeek/Gemini workers fed context directly (sidesteps the injected-context DeepSeek rejects and the flaky auto-wake from Congress 9). **Pull in Agent 7's proposed cross-engine solutions when detailing this slice** — they likely slot under this umbrella.
- **Slice 5 — Discussion mode.** A sequential capability-gap roundtable: each brother states his task's difficulties + what tooling he's missing; Agent 7 + Agent 9 (+ Gemini, + mainline) do web-search / debugging to find the `tankoctl` command or MCP or external tool that unblocks him. Archetype: *"Agent 4, smoke theatre like a human would — what MCP/tankoctl/external stuff would you need?"* Explicitly deferred until the core lands.

---

## 9. Stack decision (final, honest scope)

**Python backend + HTML page, served into its own standalone Chromium app-window now; Electron at Slice 2.**

Rationale (the honest scope call): Slice 1's value is entirely in the **Python clarity engine**, not the window. The current `office_web.py` already live-polls and renders; standing up an Electron toolchain in Slice 1 would pour effort into the frame while the painting is the Python. The standalone Chromium window (`--app=` flag, optional PWA manifest) gives ~80% of the "real app" feel for ~0% of the Electron cost. Electron earns its place at Slice 2, where owned workers (process control via Node) and a real desktop window (foreman panel, kill-switch) actually need it — and it wraps the same HTML, so adoption is low-risk and throws nothing away. **Don't add complexity before it pays.**

---

## 10. Decisions made + why (brainstorm record)

- **Re-aim from "smarter agents" to "legible, reliable room."** Hemanth's stated #1 pain is opacity + unreliability, not throughput. Slice 1's heart shifted accordingly.
- **Derive status from ground truth, not self-report.** Self-reporting is the unreliable thing; ground truth (git/tankoctl/build/bus) can't forget or lie. Applies the cheap-context rule to the core pain.
- **Hybrid agent model.** Companion-only would inherit every hard platform limit (closed tabs, cross-engine, one-turn autonomy); Host-only changes how Hemanth works too much. Hybrid escapes the limits where it matters (owned workers) while keeping familiar tabs for hands-on.
- **Both foreman gears (A + B), B gated + supervised.** Hemanth switches posture by the day. B is powerful, so it's bounded by an entry-gate (all work queued) + an exit-owner (Agent 0 closes it).
- **Electron deferred to Slice 2; standalone Chromium window now.** Honest scope discipline — the window isn't where Slice 1's value is, and the Chromium app-window bridges cleanly to Electron later with the same HTML.

---

## 11. Open items (carry into planning)

- **Name:** stays "THE OFFICE" unless Hemanth wants the app to have its own name.
- **Slice 1 seed:** reuse the existing `office_web.py` HTML as the starting point (lean: yes — it already works).
- **Ground-truth derivation sources (Slice 1 detail):** exact set + polling cadence for git / `tankoctl` / build-state / bus — to be specified in the implementation plan.
- **Cross-engine (Slice 4):** retrieve Agent 7's proposed solutions (offered on the bus / chat.md before quota ran out) when that slice is scoped.

---

## 12. References

- Proven Office: `docs/superpowers/specs/2026-05-30-the-office-live-agent-bus-design.md`, `-always-on-mode-design.md`, `-congress-9-orchestration-design.md`
- Memories: `project_the_office_live_agent_bus`, `project_agentchattr_shelved_2026-05-28` (autonomous-foreman sketch + unattended-merge risk note), `feedback_office_idle_bus_wakes_directly`, `feedback_no_office_post_narration`, `feedback_answer_in_channel_asked`, `user_profile` (concurrency norm)
- Existing code: `scripts/office/` (`office_bus.py`, `office_web.py`, `office_watch.sh`, `office_cost.py`, `codex_agent7_bridge.py`, `tests/test_office.py`)
