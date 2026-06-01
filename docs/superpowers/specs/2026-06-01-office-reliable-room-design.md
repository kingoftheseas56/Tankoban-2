# THE RELIABLE ROOM — design spec

**Date:** 2026-06-01 · **Author:** Agent 0 (Opus) · **Arc:** The Office overhaul, Slice 1 (reliability core)
**Status:** design approved (Hemanth, 2026-06-01), pending implementation plan.

**Agent 7 revision (2026-06-01):** The implementation slice commits locally only and does not push; Agent 0 reviews before origin master.

**Agent 7 revision (2026-06-01):** Ack means "responsibility accepted"; a direct answer is also terminal, so the actual contract is ack-or-answer promptly.

**Agent 7 revision (2026-06-01):** Escalation events are per addressed brother. Use `arc="<ask_seq>:<to_agent>"` for `escalate` events so comma-list asks cannot cross-contaminate. `ack` stays `arc="<ask_seq>"`.

**Agent 7 revision (2026-06-01):** The guarantee is active while `office_web.py` is running. Always-on escalation is a future daemon/service slice, not part of this implementation.

**Agent 7 revision (2026-06-01):** Retire only the in-scope `scripts/office` responder path and UI/status claims. Root launchers and `.gitignore` cleanup are deferred unless scope expands.

Builds on: `agents/audits/office_defect_audit_2026-06-01.md` (Agent 7), `agents/audits/office_research_agent7_2026-06-01.md` (Agent 7), `docs/superpowers/data/2026-06-01-office-research-synthesis.md` (3-source synthesis).

---

## 1 · North star & scope

The Office stays a **room you see and trust** (Hemanth's locked north-star — not a platform, not an autonomous engine). This slice makes one guarantee real:

> **No direct ask ever silently drops.** Every direct message to a brother reaches a visible terminal state: **acknowledged → answered**, or **escalated** (to Agent 0, then Hemanth).

The guarantee is **100% deterministic** — no LLM in the guarantee path. This is the "guaranteed ack + visible gaps" reliability target (Hemanth's choice over "absolute certainty"), which the field validates (EPAM's required-ack; "guarantee the ack, escalate the rest"). It is built on the Office's existing **append-log + projection** architecture (Approach B) — no new database, no new dependency.

**In scope (this slice):** ask lifecycle + state, three new bus event kinds, `office_asks.py` projection, deterministic escalation tick, the Open Asks lane, the roll-call command, retiring the `claude -p` responder, the ack governance line.

**Out of scope (later slices or never):** SQLite Room State Store, owned-worker SDK substrate, foreman/autonomy, operations board, capability registry, observability traces, cost governors, Congress-as-voting, worktree reconciliation. The **substantive LLM backup is dropped** from the guarantee entirely.

---

## 2 · The ask lifecycle (the heart)

A **direct ask** = a `kind:chat` message whose `to` names a specific brother (`agentN` or a comma-list), from someone other than that brother. Every direct ask needs at least a quick **acknowledgement** (EPAM's "every message gets acked or the pipeline stalls").

**States & transitions (per ask, per addressed brother):**

```
open ──ack──▶ acked ──answer──▶ answered   (closed)
  │                  └────────answer───────▶ answered (closed; ack optional)
  │
  └─(W elapses, no ack/answer)─▶ escalated_a0 ─(W2 elapses, still unresolved)─▶ escalated_hemanth
```

- An **ack stops the escalation clock** — it means "I've got this." An acked-but-unanswered ask shows on the board as *"acked · awaiting answer"* — a **visible** gap, never a silent one.
- An **answer** closes the ask whether or not an explicit ack came first (answering directly is its own closure).
- **Resolution** that stops escalation = an `ack` or an inferred `answer` appears for that ask. If it appears before W2, the ask closes / stays acked and never reaches Hemanth.
- **Broadcasts (`to:all`) are exempt** — they do not create tracked asks (anti-spam, per the defect audit's anti-fanout finding). The one exception is the explicit **roll-call** (§6).

**Windows (defaults, env-tunable):** `W = 5 min` (open → escalated_a0), `W2 = +5 min` (escalated_a0 → escalated_hemanth). Set via `OFFICE_ASK_WINDOW_SEC` / `OFFICE_ASK_ESCALATE2_SEC`.

---

## 3 · Data — three event types on the existing bus

No new database. We extend `agents/bus.jsonl` (schema `{ts,seq,from,to,kind,arc,msg}`), which already carries `kind` ∈ {chat, activity, blocked}. The unused `arc` field carries the referenced ask seq.

- **`ack`** — posted by a brother acknowledging an ask: `kind:"ack"`, `from:agentN`, `to:<asker>`, `arc:"<ask_seq>"`, `msg:"<optional note>"`. Wrapper: `office_ack.sh <agentN> <ask_seq> [note]`.
- **`escalate`** — posted by the escalation tick: `kind:"escalate"`, `from:"system"`, `to:"agent0"` (then `"hemanth"`), `arc:"<ask_seq>:<to_agent>"`, `msg:"agentN hasn't acked #<seq> in <W>m"`.
- **answers** — *not* a new kind. An answer is inferred: an existing `kind:chat` from the addressed brother back to the asker with `seq > ask_seq`, reusing the target-aware matching already written for the responder (`should_suppress`-style logic).
- **asks** — *not* a new kind and **no new send-time step**. Asks are inferred from the direct-chat messages already flowing. Posting a normal `@agentN` chat is unchanged.

This keeps the bus human-readable and append-only; lifecycle state is *derived*, not stored separately (the Approach-B principle: graduate to SQLite only if state outgrows projection — not now).

---

## 4 · The deterministic engine — `office_asks.py` + escalation tick

**`office_asks.py` (new, pure logic — mirrors `office_status.py`):**
- `compute_asks(records, now_epoch, window, escalate2)` → list of open asks, each with: `ask_seq`, `from`, `to_agent`, `text`, `age_sec`, `state` (open / acked / answered / escalated_a0 / escalated_hemanth), `acked_by`, `escalated`.
- Pure function over bus records; no side effects; fully unit-testable.
- A `due_escalations(records, now_epoch, window, escalate2)` → list of asks that have crossed a threshold and need an `escalate` event posted *this tick* (i.e., newly due, not already escalated to that level — idempotent via checking for an existing `escalate` event for that ask+level).

**The escalation tick:**
- Runs as a **background thread inside the already-running `office_web.py` server** (opening the Office = escalation is live; no extra process, no extra `.bat`, preserving Hemanth's one-click model).
- Every ~30s: read bus → `due_escalations()` → for each, post the `escalate` event (which wakes Agent 0 via his watch exactly like any addressed message; then Hemanth at W2).
- Deterministic, no LLM, ~$0 cost. Idempotent: never double-posts an escalation for the same ask+level.

**Separation of concerns:** `office_asks.py` = pure projection + due-detection; `office_web.py` = runs the tick + renders. (Same split as `office_status.py` ↔ `office_web.py`.)

---

## 5 · The "Open Asks" lane (UI)

A new panel in `office_web.py`, beside chat + roster: a live list of every **open** ask, each row showing **asker → owed-brother**, a **state badge** (acked / answered / owed / escalated), and **age**. Closed asks (answered) fall off after a short grace. Same cache-busted poll-and-render pattern as the roster (`/asks` endpoint backed by `office_asks.compute_asks`). This is the single "what's still hanging" surface — a gap cannot hide.

Visual: monochrome + the WhatsApp-family palette already in use; "escalated" rows carry a restrained warning accent (consistent with the blocked lane). No emoji chrome.

---

## 6 · Roll-call

`office_rollcall.sh <from>` posts a lightweight "check in" **ask** to each mainline brother (agents 1–5, configurable) at once — modeled as direct asks so they flow through the same lifecycle. The Open Asks board (or a roll-call filter) then shows **present (acked) vs missing** as acks land. This directly answers Hemanth's "everybody make your presence known" moment with a visible missing-list, without `@all` fanning out anything paid.

---

## 7 · Retire the `claude -p` backup

`scripts/office/office_responder.py`, `office_responders.py`, and the responder status/UI claims are **retired from the active path**. Root launcher cleanup is deferred unless scope expands:
- The launchers + supervisor are removed from active use (deleted or moved to an `_archive`/shelf; code remains in git history, revivable only if the "absolute certainty" rung is ever pursued).
- The responder heartbeat + `responder_alive` "backup" chip in the roster are removed or repurposed (the chip overpromised, per the defect audit).
- The deterministic guarantee replaces it; the expensive, flaky 120s-timeout path is gone.

---

## 8 · The one behavioral contract (governance)

For acks to happen, the brotherhood protocol gains one line: **"Acknowledge direct asks promptly"** (a quick `office_ack.sh <agentN> <ask_seq>` or a direct answer). Added to `agents/GOVERNANCE.md` (Office section) + the Office onboarding. The visible Open Asks board makes it **self-enforcing** — an unacked ask is visible pressure, not a silent miss. No new heavy rule; one sentence + the surface that backs it.

---

## 9 · Testing

- **Pure-logic TDD** on `office_asks.py` (`tests/test_asks.py`, mirroring `test_status.py`): ask detection from direct chats; ack closes the clock; answer closes the ask (with and without prior ack); broadcast exemption; escalation timing at W and W2; idempotent due-detection; roll-call asks tracked.
- **Web smoke**: `/asks` endpoint returns the projection; the Open Asks panel renders states; in-process verifier (no port zombies).
- Both existing suites (`test_status.py`, office bus tests) stay green.

---

## 10 · Future shell (on record, NOT this slice)

The Office UI is plain HTML today (served into a standalone Chromium `--app` window). The agreed future home is a **VS Code extension** — the Office as a panel *inside the IDE the brothers already live in* — which *wraps* this same Python+HTML. This is a normal-sized future slice, **not** a fork of VS Code/Code-OSS (the Cursor route, deemed product-company-scale and out of bounds). This slice's HTML surface is built shell-agnostic so it ports cleanly into that extension later. Recorded here so the path is set; nothing in this slice depends on it.

---

## 11 · Open items for the implementation plan

- Exact default for W / W2 and the env var names (proposed: 5 min / +5 min).
- Whether to delete vs `_archive` the retired responder files.
- Grace period before answered asks drop off the lane (proposed: ~2 min).
- Roster cleanup: remove `responder_alive`/`backup` chip + responder heartbeat plumbing.
- Roll-call brother set (default agents 1–5) + whether it's a `.bat` (Hemanth-clickable) or CLI.
- Ack-detection nuance: should an `@agentA,agentB` comma-ask require an ack from *each* addressee (proposed: yes — one open ask per addressed brother).
