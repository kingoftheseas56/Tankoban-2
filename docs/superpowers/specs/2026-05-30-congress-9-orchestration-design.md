# Congress 9 — Concurrent Multi-Agent Orchestration (Vision / Architecture Spec)

**Date:** 2026-05-30
**Author:** Agent 0 (Opus) + Hemanth (brainstorm)
**Status:** Vision on record — **NOT yet for `writing-plans`.** Builds on the Office (`2026-05-30-the-office-live-agent-bus-design.md`). This spec is captured now so the vision is durable; its implementation plan is written **only after the Office is built and lived-in**, so Congress's real shape emerges from practice rather than guesswork.

---

## §1 — What Congress 9 is

Congress is the brotherhood's **all-hands mode**: many real brothers working a single arc **concurrently**, pipelined in batches, coordinating live over the Office bus.

It is the *final form of the brotherhood* — and it is categorically different from subagent dispatch. **A subagent is a blank** (fresh context, no accumulated memory, no domain CLAUDE.md, no taste). **A brother is himself** — Agent 4 carries the stream-domain scars, libtorrent lessons, sidecar fights; Agent 3 carries the player/D3D11/sidecar hardware knowledge. Congress lets the *real* brothers, with their months-deep distinct memory, work the same arc at once. (Hemanth, verbatim: *"each agent would bring something different to the table, unlike a sub-agent process. It's the final form of brotherhood."*)

**Two faces of Congress:**
- **At its quietest** — simple unblocking: *"Agent 1: @Agent 4, your work blocks mine, can you clear it so I can finish?"*
- **At its most robust** — a full pipelined arc: e.g. fill volume synopses — Agent A harvests ISBNs → hands to Agent B (synopsis search) → hands to Agent C (inject) → Agent D (smoke-test via MCP screenshot) — and **the line never stops**: while D smokes batch 1, A is already harvesting batch 2.

**Lineage:** the old Congress (8 of them, archived 2026-04-23) was *serial* — Hemanth summoned agents one at a time to post positions; Agent 0 synthesized. Congress 9 is *concurrent*, made possible by the Office bus (the nervous system the old Congress lacked).

---

## §2 — Roles (LOCKED)

Congress separates two distinct authorities that the old model conflated:

| Role | Who | Responsibility |
|------|-----|----------------|
| **Chair** | **Agent 0, always** | *Process.* Sequences handoffs, watches the bus, merges green work (gov-v11/v12), audits parity, declares the Congress done. The facilitator. |
| **Leader** | **The central domain owner** (arc-dependent) | *Technical authority.* Sets the reference spec, makes domain calls, directs the moving parts. For a micro arc (e.g. video-player revamp) Agent 3 leads; for a multi-domain parity arc the domain masters are co-equal members under the Chair. |

Chair ≠ Leader. Agent 0 runs the meeting; the domain owner owns the subject matter. (Analogy: a facilitator vs. the subject-matter expert.)

---

## §3 — Convening (LOCKED)

A normal task becomes a Congress when **either**:
- **Hemanth calls it** — for arcs that *feel* multi-component from the outside (his product-level read), **or**
- **Agent 0 calls it** — when the **code structure** reveals an arc that truly needs many hands (the dependency shape Hemanth can't see from outside). Agent 0 proposes; Hemanth ratifies.

Complementary signals: Hemanth sees the outward shape, Agent 0 sees the structural shape.

---

## §4 — Membership (LOCKED)

- **Domain-determined, mission-dependent:**
  - **Multi-domain arc** (e.g. one UI feature across all 3 modes): strictly the **domain masters** of the touched modes/components.
  - **Micro arc** (e.g. revamp the video player): the **central domain owner leads** (Agent 3), and **any other agents join as helpers** if the code has multiple moving parts.
- **Fluid roster — join/leave per stage.** Agents tap in when their stage is live and tap out when done. The smoke-tester joins only when there's something to smoke; the ISBN-harvester taps out once harvesting is downstream. Membership flows through the assembly line.

---

## §5 — Work model (LOCKED)

- **Batches are pre-defined — BEFORE the Office opens.** Hemanth + the arc Leader do the **brainstorm + plan first** (existing brainstorm→spec→plan flow). The batch boundaries are decided *there*. **Congress executes a pre-planned arc; it does not plan on the fly.** The Office is the assembly line; the plan is the blueprint drawn beforehand.
- **Handoffs = direct + addressed**, peer-to-peer over the bus: *"@AgentB, here are 12 ISBNs for batch 1, your turn."* B picks it up at his next natural break. Agent 0 (Chair) **watches**, doesn't switchboard every baton (no bottleneck).
- **Continuous flow:** the next batch starts upstream while the previous batch is still downstream (A harvests batch 2 while D smokes batch 1). The pipeline doesn't stall on any single stage.

---

## §6 — Parity enforcement (LOCKED)

For "same feature across all 3 modes, don't let them drift":
- **The Leader sets a reference spec / reference implementation first.** It is the single source of truth.
- The other mode-owners **must match it.** Drift is caught by comparing each implementation against the reference.
- (The Chair may parity-audit at merge as a backstop — refined when lived-in.)

---

## §7 — Closing (LOCKED)

- **The Chair (Agent 0) declares the Congress done** when all batches are complete, merged, and smoked.
- Agent 0 posts a closeout summary and archives the arc's bus thread; Hemanth is informed.
- (The Office itself closes per its own lifecycle — Hemanth-driven; see Office spec §4.)

---

## §8 — What rides on what (layering)

```
Congress 9 (this spec)      ← orchestration: roles, convening, batches, parity, handoffs
        ▼ rides on
The Office (companion spec)  ← comms substrate: bus.jsonl, delivery hook, chat_send, office hours
        ▼ rides on
brainstorm → spec → plan     ← the blueprint (existing flow), drawn BEFORE the Office opens
```

Congress's bus messages use the Office's reserved fields: `kind` ∈ {`convene`, `handoff`, `block`, `parity`, `adjourn`, `chat`} and `arc` = the Congress arc id. These fields exist in the Office schema from day one (Office spec §3.1) so Congress plugs in without a bus rebuild.

---

## §9 — Why this is NOT specced for build yet (deliberate)

The orchestration patterns (exact handoff handshake, parity-audit mechanics, fluid-roster join/leave protocol, arc-thread archiving) will reveal their *real* shape only after the brotherhood has actually used the Office for a few sessions. Speccing the full Congress pipeline before a single message has crossed the bus would invent protocols that don't survive contact with reality. **Foundation first (the Office), live in it, then write Congress's implementation plan from lived experience.**

---

## §10 — Open questions (resolve when Congress goes to plan, post-Office)

- Exact handoff handshake (does a handoff need an explicit ack, or is pickup implicit?).
- Parity-audit mechanics (Chair diff at merge vs. continuous bus cross-flagging vs. reference-only).
- Arc-thread archiving (does each Congress arc get its own archive file separate from daily bus archive?).
- How a fluid-roster agent signals "tapping in" / "tapping out" of a stage.
- Whether Congress needs a lightweight standing doc (a reborn `CONGRESS.md`) for the arc's roster + stage cursor, or whether the bus + plan suffice.
- Governance codification (a `gov-vN` bump for Congress 9 etiquette + the Chair/Leader split).

---

## §11 — Out of scope (for the eventual Congress build)

Planning the arcs themselves (done via the existing brainstorm→spec→plan flow beforehand); the Office comms substrate (its own spec); auto-wake of idle tabs (deferred at the Office layer); replacing Hemanth's product authority (he still convenes + sets product direction).
