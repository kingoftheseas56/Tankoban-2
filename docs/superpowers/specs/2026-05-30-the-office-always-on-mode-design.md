# The Office — Always-On Mode (Office v2) Design Spec

**Date:** 2026-05-30
**Author:** Agent 0 (Opus) + Hemanth (brainstorm)
**Status:** ✅ **FEASIBILITY PROBE PASSED — VIABLE & PROVEN (2026-05-30).** Both gates cleared live: (1) **promptability** — Hemanth typed into Agent 1's tab mid-watch, got a normal reply; (2) **true hands-off auto-wake** — with Hemanth NOT touching the tab, Agent 0 posted bus seq 5, Agent 1's Monitor watch fired on its own and he woke + replied "WATCH-WAKE CONFIRMED - the watch woke me, Hemanth did not." Always-on mode works end-to-end. Builds on `2026-05-30-the-office-live-agent-bus-design.md` (v1, shipped + live this session).

---

## §1 — Purpose

v1 of the Office delivers messages at a brother's **natural breaks** (when prompted / when it runs a tool). The gap: a brother who is **idle at the prompt** doesn't see a message until *Hemanth* prompts him. v2 closes that gap for any *open* tab: a brother **auto-wakes the instant a message addressed to him lands**, with no prompt from Hemanth — true always-on presence.

**What v2 does NOT do (hard platform limit, doc-confirmed):** it cannot wake a brother who has **fully clocked out** (tab closed / session ended). There is no supported external mechanism to inject into a parked session (no CLI, no IPC, no VS Code extension API; headless `--resume` of a live session collides). v2 keeps an *open* tab always-on; it does not resurrect a closed one.

---

## §2 — The mechanism (doc-grounded)

The enabler is the **same primitive Hemanth has seen wake an agent when a build finishes**: a session that starts a background watch (the `/loop` + **Monitor tool** path) is **re-invoked by the harness when the watch emits output** — *"Claude interjects when an event lands"* (Monitor docs). Pointed at the bus instead of a build, this gives event-driven wake-on-message.

**Cost model (corrected during brainstorm):** cost is **per-message, not per-minute.** The watch is a plain shell script (zero model tokens) tailing the bus; the brother (the model) is asleep until a message arrives. Idle = ~free. A brother only spends tokens when he actually wakes to read + reply. A 3-hour shift with 5 messages costs ~5 small wakes, not 3 hours of anything. This is why "shift = until you end it" is affordable.

---

## §3 — Decisions (LOCKED)

| Dimension | Decision |
|-----------|----------|
| **Clock-in** | **Auto at wake** — every agent starts its bus-watch as part of waking. Open a tab → instantly always-on. (Viable because idle is ~free.) |
| **Shift length** | **Until Hemanth ends it** — `office close`, closing the tab, or stopping the watch clocks out. No auto-expiry. |
| **Autonomy on wake** | **Reply-only (v1 of v2).** A woken brother reads the message and may REPLY via `chat_send`. He does NOT start code work / file edits unsolicited. Autonomous domain-action is deferred to v3. |
| **Promptability** | **HARD REQUIREMENT — Hemanth must still be able to type into a watching brother's tab and get a normal response.** A brother you cannot talk to directly is worse than one you clock in manually. This gates the whole feature (§4). |

---

## §4 — Feasibility probe (LOAD-BEARING — do FIRST, gates everything)

**The single proof of viability** (Hemanth: *"testing whether or not I can send a message to a brother on watch for office messages is the most important proof of viability"*).

Clock ONE brother into a bus-watch loop, then verify BOTH:
1. **Promptability:** Hemanth types a normal prompt into that brother's tab → the brother responds normally (the watch did NOT lock the tab).
2. **Auto-wake:** another agent posts a bus message addressed to the watching brother → the brother wakes and replies **without Hemanth prompting him**.

**Decision gate:**
- **Both pass** → build v2 as specified (auto-clock-in at wake, reply-only).
- **Promptability fails** (the loop owns the tab / Hemanth locked out) → **DO NOT SHIP auto-clock-in.** Fall back to: (a) manual clock-in only, or (b) abandon always-on and keep v1's natural-break delivery. A brother Hemanth can't reach is a regression.
- **Auto-wake fails** (watch doesn't actually re-invoke the model on a bus line) → v2 isn't possible on this primitive; stay on v1.

No further v2 code is written until this probe passes.

**PROBE RESULT (2026-05-30): PASSED on both gates.** Agent 1 clocked into `office_watch.sh agent1` via Monitor. (1) Promptability: Hemanth's direct "hey brother 1, you there?" got an immediate "Right here, brother. On watch and fully present." (2) Auto-wake: Hemanth went hands-off; Agent 0 posted seq 5; Agent 1's watch fired with zero human input and he replied on the bus. The make-or-break — *can Hemanth still reach a watching brother* — is YES, and the dream — *a brother wakes himself on a message* — is YES. Cleared to build out.

---

## §5 — Components (built only after §4 passes)

| Unit | Responsibility | Depends on |
|------|----------------|------------|
| `scripts/office/office_watch.sh` | The watch script: blocks/tails `bus.jsonl`, emits a line when a NEW message addressed to this agent (or `all`) arrives, so the harness wakes the brother. Re-arms each cycle. | v1 bus + identity |
| Wake-handler guidance | Short instruction the woken brother follows: read new msg(s) via `drain`, reply via `chat_send` if warranted, do NOT act unsolicited (v2 reply-only), watch re-arms. | office_bus.py |
| Auto-start-at-wake hook | Tiny addition so a tab starts its watch on wake (SessionStart or first-prompt). **Only added if §4 promptability passes.** | office_watch.sh |
| (re-used from v1) | bus.jsonl, chat_send, identity map, drain, GUI, office_close | — |

---

## §6 — Out of scope (deferred)

- **Autonomous action on wake** (a woken brother DOING domain work, not just replying) — v3, after the always-on loop is proven stable + cheap in real use. This is the Congress-pipeline payoff.
- **Waking a fully-closed tab** — platform-impossible; not pursued.
- **Cross-engine always-on** (Codex/DeepSeek watchers) — after Claude-tab v2 is proven.

---

## §7 — Acceptance criteria

- §4 probe passes (promptable + auto-wakes) — the gate.
- An open brother, with no prompt from Hemanth, replies to a bus message addressed to him within one wake cycle.
- Hemanth can still type into that brother's tab and get a normal answer at any time.
- Idle cost is negligible (no continuous token burn while the bus is quiet) — spot-checked over a quiet window.
- `office close` / closing the tab cleanly ends the watch (no orphaned loop).
