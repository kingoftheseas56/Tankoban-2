# The Office — Live Agent Communication Bus (Design Spec)

**Date:** 2026-05-30
**Author:** Agent 0 (Opus) + Hemanth (brainstorm)
**Status:** Design approved — ready for `writing-plans`
**Companion:** `2026-05-30-congress-9-orchestration-design.md` (the orchestration layer that rides on top of this; that one waits until the Office is lived-in).

---

## §1 — Purpose & one-picture summary

The **Office** is a shared, real-time message channel that lets open brotherhood agent-tabs talk to each other at their **natural workflow breaks** — without Hemanth hand-carrying messages between tabs.

One picture: a shared **bus file** (`agents/bus.jsonl`) is the office room. A brother drops an addressed message into it (`@agent4: StreamTypes is gone, don't look for it`). At each agent's natural breakpoints (between tool calls / at the top of each reply), a **delivery hook** quietly slides any new messages-for-that-agent into its context — so the brother sees it and can react in his next step. The office is **open while Hemanth works** and **closes when his shift ends** (archive + clear). No loop caps — Hemanth is the off-switch.

**Why this exists (the trigger):** Hemanth runs many agent tabs concurrently as the norm. On 2026-05-30 a coordination handoff went sideways — Agent 1 reported work correctly, Agent 0 (coordinating) briefly mis-recorded it, and it lived as a wrong commit for ~10 minutes because there was no way for Agent 1 to correct Agent 0 *in the moment*. File-based coordination (`chat.md`/RTC) is async and Hemanth-mediated. The Office is the missing nervous system.

**Lineage:** this is the substrate for a reborn **Congress** (see companion spec). The old Congress (archived 2026-04-23) was *serial* — Hemanth summoned agents one at a time to post positions. The Office makes Congress *concurrent*.

---

## §2 — Behavior model (LOCKED)

| Dimension | Decision |
|-----------|----------|
| **Scope of messages** | Full free-form natural language (not a fixed signal vocabulary). |
| **Who initiates** | Agents message each other directly (peer-to-peer), not only Hemanth routing. |
| **Pickup timing** | **Option 1 — natural-break pickup.** A message is seen at the receiving agent's next natural breakpoint (between tool calls / start of a reply). Seconds-to-a-minute while a tab is actively working. **No mid-action interrupt** — a message cannot break into a running tool; it is picked up when that step ends. This mirrors how Claude/Codex already handle a user message sent mid-task. |
| **Idle tabs** | A message to a fully-idle tab (finished, sitting at the prompt) **waits** until that tab is next touched. True auto-wake of an idle tab is **explicitly deferred** (see §6). |
| **Loop/noise guard** | **None.** "Office hours" model: the office is open only while Hemanth is working; he closes it at end of shift. He is the loop guard; no hop-caps. |

**The "office hours" mental model (Hemanth's framing, verbatim intent):** *"think of this more like a workplace and this chat is the time you spend in office with your co-workers and friends. once your shift is done, it's done. the office closes."*

---

## §3 — Architecture (LOCKED)

Two layered halves, each doing what it is good at:

### 3.1 The channel — `agents/bus.jsonl`
- A dedicated, append-only JSONL file, **separate from `chat.md`**. `chat.md` stays the permanent human-readable narrative log; the bus is the ephemeral machine channel.
- **Gitignored** — ephemeral office talk, local-only, never on GitHub.
- One message per line:
  ```json
  {"ts":"2026-05-30T10:40:12+05:30","from":"agent1","to":"agent4","kind":"chat","arc":null,"seq":42,"msg":"StreamTypes.h is gone — don't look for it in your deletion pass"}
  ```
- **Congress-aware fields reserved from day one** (so the orchestration layer plugs in without a rebuild): `kind` (v1 always `"chat"`; later `handoff`/`block`/`parity`/`convene`/`adjourn`) and `arc` (v1 always `null`; later the Congress arc id). `seq` is a monotonic per-bus counter used for the "unseen" cursor (§3.2).
- `to` supports a single agent (`"agent4"`), a list, or `"all"` (broadcast).

### 3.2 Delivery — the hook (the guaranteed glance)
- A delivery script runs at each agent's natural breakpoints via the harness hook system (we already run `SessionStart`, `UserPromptSubmit`, `PreToolUse`, and `Stop` hooks — see `.claude/settings.json`).
- On fire: read `bus.jsonl`, find messages addressed to *this agent* (or `all`) with `seq` greater than this agent's last-seen cursor, inject them into the agent's context, advance the cursor.
- **Per-agent cursor:** `agents/.bus_cursors/<agent>.seq` (gitignored) records the last `seq` delivered to each agent, so nobody re-reads old messages.
- **Agent identity:** the hook must know *which agent* this tab is. Resolved from the same signal the session uses to self-identify (the wake-prompt agent number / a per-tab env var / the existing agent-router signal). **This is a v1 feasibility item (§7).**

### 3.3 Sending — one primitive
- A single send action: `chat_send "@agent4" "message"` (and `chat_send "@all" "..."`).
- **v1:** a small CLI/script (e.g. `scripts/office/chat_send.*` or a `tankoctl office-send` subcommand) that appends a well-formed line to `bus.jsonl` with correct `from`/`ts`/`seq`.
- **Cross-engine increment:** expose the same primitive as an **MCP tool** so Codex (Agent 7) and DeepSeek (Agent 9) call the identical `chat_send` and their hooks pull deliveries — whole brotherhood on one bus regardless of engine.

### 3.4 Why layered (vs one half alone)
- **Hook-only:** delivery bulletproof, but sending would be hand-writing JSON (error-prone).
- **MCP/CLI-only:** sending clean, but delivery would depend on each agent *remembering* to poll every step — models forget → silent dropped messages.
- **Layered:** guaranteed delivery (hook pushes) + clean sending (one call). Each half plays to its strength.

---

## §4 — Office lifecycle (LOCKED)

- **Auto-open:** the office opens implicitly when the first message of a session is posted (first line appears in `bus.jsonl`). Nothing for Hemanth to switch on.
- **Close (Hemanth-driven):** Hemanth runs an `office close` action (or tells Agent 0). On close:
  1. The current `bus.jsonl` is **archived** to `agents/bus_archive/YYYY-MM-DD[-N].jsonl`.
  2. The live `bus.jsonl` is **cleared** (and per-agent cursors reset).
  3. Next session starts in a clean, quiet room; history retained if ever wanted.
- **No auto-close, no caps.** The office stays open across the work session; only Hemanth closes it.

---

## §5 — Components (each independently buildable/testable)

| Unit | Responsibility | Depends on |
|------|----------------|------------|
| `bus.jsonl` schema + append helper | Define the line format + a safe concurrent append (advisory lock / atomic append) | filesystem |
| `chat_send` primitive (CLI v1) | Validate + append a message with `from`/`ts`/`seq` | bus schema |
| delivery hook script | At a breakpoint: read new messages for this agent, inject, advance cursor | bus schema, per-agent cursor, agent-identity resolver |
| agent-identity resolver | Tell the hook which agent this tab is | existing wake/router signal |
| `office close` action | Archive + clear + reset cursors | bus schema, archive dir |
| MCP `chat_send`/`chat_read` (increment 2) | Cross-engine send/receive for Codex + DeepSeek | bus schema |

**Concurrency note:** multiple tabs append to `bus.jsonl` simultaneously (Hemanth runs many at once). Appends must be atomic/locked so lines don't interleave-corrupt. This is a first-class design constraint, not an afterthought.

---

## §6 — Explicitly deferred (YAGNI for v1)

- **Auto-wake of idle tabs.** Reviving a finished/idle tab to act hands-free requires keystroke-injection (`WriteConsoleInput` on Windows / `tmux send-keys` on Mac) into the tab's terminal. This is OS-specific, fragile (VS Code integrated-terminal handles), and only helps a tab that is *idle at the prompt* — a *busy* tab degrades to natural-break pickup anyway, which the cheap path already gives free. **Decision:** build the bus first; add auto-wake only if "waits until the idle tab is touched" proves genuinely painful. Cost/risk: high; value: narrow.
- **Loop/hop caps.** Not needed under the office-hours model (Hemanth-attended).
- **Cross-engine (Codex/DeepSeek) delivery.** v1 proves the Claude-tab loop; MCP layer is increment 2.

---

## §7 — Open feasibility items (resolve in the implementation plan, step 1)

1. **Hook injection feasibility (load-bearing).** Confirm *which* hook event(s) can inject text into an agent's context mid-flow at a natural break. We already run `Stop`/`UserPromptSubmit`/`PreToolUse`/`SessionStart` hooks; the plan's first step must verify the injection path empirically before committing the rest. **Fallback if no event injects cleanly:** the MCP `chat_read` pull model (agent reads at its breaks) becomes primary instead of hook-push.
2. **Agent-identity in the hook.** Confirm how a hook reliably knows which agent number the current tab is (env var set at wake / parse of the wake-prompt / the existing `agent_router` mechanism).
3. **Atomic concurrent append** to `bus.jsonl` under many simultaneous tabs.

---

## §8 — Acceptance criteria (v1)

- Two open Claude tabs (e.g. Agent 0 + Agent 1): Agent 1 runs `chat_send "@agent0" "..."`; Agent 0 sees the message injected at its next natural break **without Hemanth relaying it**, and can reply via `chat_send "@agent1" "..."` which Agent 1 likewise receives. Round-trip proven.
- `@all` broadcast reaches every open tab at their next breaks.
- An idle tab receives a queued message when next touched (no auto-wake claim).
- `office close` archives the session to `agents/bus_archive/` and clears the live bus; next session starts clean.
- `bus.jsonl` + cursors + archive are gitignored; `chat.md` untouched by Office traffic.
- Concurrent appends from ≥3 tabs produce no corrupted/interleaved lines.

---

## §9 — Relationship to existing systems

- **`chat.md`** stays the permanent narrative + RTC/RTM log (Path A commit protocol unchanged). The Office is *live ephemeral talk*; `chat.md` is *durable record*. A brother may still post an RTC to `chat.md` — the Office is for the real-time coordination *around* that.
- **Governance:** the Office introduces no new commit authority. Merges still follow gov-v11/v12; worktrees stay retired (gov-v13). A future governance bump will codify Office etiquette once it's lived-in.
- **Congress 9** (companion spec) is the orchestration layer that rides this bus. It is **not** in this spec's build scope — the Office ships and gets lived-in first; Congress's real shape emerges from use.

---

## §10 — Out of scope

Planning/brainstorm work (Congress arcs are planned via the existing brainstorm→spec→plan flow *before* the Office opens — the Office executes, it does not plan); auto-wake; loop caps; cross-engine delivery (increment 2); any change to `chat.md`'s role.
