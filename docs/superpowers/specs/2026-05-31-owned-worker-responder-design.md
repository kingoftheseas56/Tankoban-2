# THE OFFICE — Owned-Worker Responder (the idle-agent cure) — design spec

**Date:** 2026-05-31
**Author:** Agent 0 (brainstormed live with Hemanth)
**Status:** Design ratified in brainstorm; awaiting written-spec review → Agent 7 feasibility review → implementation plan
**Part of:** the owned-worker model in `2026-05-31-the-office-app-design.md` (spec v2, §3 capability contract / Slice 2A) — this is the **narrow first cure**, not the full autonomous-worker build.

---

## 1. The problem this cures

Every "agent X non-responsive" Hemanth hit this session (Agent 4 ×2, Agent 1 ×2) has one root: **a Claude tab that goes idle cannot be guaranteed to wake or respond.** The watch dies (5-min Monitor timeout, context compaction, closed tab), or the brother is heads-down on deep work and never circles back to a coordination message. The coordinator (Agent 0) can nudge but cannot *force* a tab awake. It's worst when an owner delegates then goes idle, stranding a collaborator (the Agent 1 → Agent 2 handoff, this session).

The proven cure already exists for one engine: **Agent 7's headless bridge** (`codex_agent7_bridge.py`, merged `f69e3b8`) — an *owned worker*, a process we control that auto-wakes reliably via `codex exec`, instead of a tab we hope responds. This spec brings that reliability to the **Claude brothers**.

**Scope (deliberately narrow — Hemanth chose "reliable responder first," YAGNI):** a guaranteed *coordination responder*, NOT a full autonomous worker. It answers coordination/handoff/ack messages reliably; it does NOT do deep autonomous code work (that's a later slice). The interactive tab stays the place for hands-on deep work.

---

## 2. The model: Fallback safety-net (Hemanth's Rule-14 call to Agent 0)

The headless worker is a **backup**, not a replacement. The real brother (in his tab) gets first crack at every coordination message; the worker only steps in if the brother stays silent.

**Why this is the most reliable option** (the chosen criterion): it's the only one that *guarantees a reply* (the backup always catches a dropped message) AND keeps replies *high-quality* (the real brother, with full mid-task context, answers first when present). It catches BOTH failure modes seen this session — the dead watch *and* the "saw it, didn't reply" case (Agent 4) — because the trigger is the brother's *silence*, not his watch state.

**The fallback signal is simple and robust:** after a message addressed to Agent N lands (seq X), the worker waits `FALLBACK_WINDOW` (~60s) and checks one thing: *did Agent N post anything after seq X?* If yes → the real brother handled it, the worker stays silent. If no → the worker answers. No cursor games, no double-posting (the worker speaks only when the tab didn't).

---

## 3. Architecture

A single generalized script — `scripts/office/office_responder.py` — parameterized by agent number, one instance per brother. ~80% a generalization of `codex_agent7_bridge.py` (swap `codex exec` → `claude -p`; add the fallback-window check).

**Per-brother loop:**
1. **Identity:** join a dedicated session (`responder-agentN` → `agentN`), guard `whoami == agentN` (refuse to run on mismatch — the cardinal safety rule from the Codex bridge).
2. **Watch:** poll the bus for messages addressed to Agent N (direct or `@all`), not from Agent N, not wake/activity lines.
3. **Fallback window:** on a trigger at seq X, record it and wait `FALLBACK_WINDOW` (~60s).
4. **Silence check:** has Agent N posted anything with seq > X? **Yes** → drop the trigger, the brother handled it. **No** → proceed.
5. **Draft:** invoke `claude -p` (headless, read-only tools, structured-output) with: the unanswered message(s), recent room context, and reply-only instructions. The worker may read repo files + the bus to answer well (claude -p has tool access, like `codex exec`).
6. **Post:** structured reply → `office_bus.py send responder-agentN @target "[auto — AgentN's tab was idle] <reply>"` — posts only as Agent N, **marked** as a backup reply (§4).
7. **Mark seen** before drafting (loop-safety): the trigger is consumed once even if `claude -p` fails (logged, no infinite retry).

**Driver:** `claude -p "<prompt>"` (Claude Code headless print mode) is the chosen mechanism — the direct Claude equivalent of `codex exec`: one binary, tool access, no API-key plumbing, no desktop. (Alternatives considered: Agent SDK — heavier; raw Anthropic API — loses repo/tool access. `claude -p` is the cleanest mirror of the proven pattern.)

**Headless:** no window, no SendKeys, no desktop — same as the fixed Codex bridge.

---

## 4. Honesty (non-negotiable)

A backup reply is **explicitly marked** — prefix `[auto — AgentN's tab was idle]` (or similar) — so Hemanth and the brothers know it's the safety net, not the man himself. The real brother **confirms or course-corrects** when he resurfaces. The room never pretends the backup is the real brother. Ties to the brotherhood's no-overclaim DNA (`feedback_no_overclaim_in_rtc`) and the status-honesty principle (the Office spec §2.1).

The backup answers *helpfully* but with appropriate humility on substantive calls: for a simple ack/coordination it just handles it; for a domain decision it gives its best read AND flags it for the owner's confirmation (rather than committing the brother to a position he didn't take).

---

## 5. Safety (mirrored from the Codex bridge — proven)

- **Posts only as Agent N** (identity-guarded; the fake-hemanth class of bug is structurally impossible).
- **Loop-guarded:** ignores its own posts, filters wake/activity/marker lines, marks-seen-before-drafting.
- **Single-instance lock** per agent (no two responders on one identity → no double-post).
- **Reply-only:** it coordinates; it does NOT do deep autonomous code work, commit, or mutate the repo. `claude -p` runs with read-only / limited tools.
- **Conservative trigger:** reply only when *useful* (don't ack every `@all` broadcast); the fallback window already filters to genuinely-unanswered messages.

---

## 6. What this is NOT (deferred)

- **Not** a replacement for the interactive tab — the tab stays for hands-on deep work.
- **Not** an autonomous worker that does code/build work on its own — that's the full owned-worker slice (Slice 2A, foreman territory), explicitly later.
- **Not** the heartbeat/status feature (already shipped) — this is the *response guarantee*; the heartbeat is the *visibility*. They're complementary.

---

## 7. Open items (carry into planning / review)

- **`FALLBACK_WINDOW` value:** ~60s default (real brothers take ~30-90s to wake+draft). Tune in the plan.
- **`claude -p` invocation specifics:** exact flags for headless + read-only tools + structured output (mirror the Codex bridge's `codex exec -s read-only --output-schema`). Verify `claude -p` supports an output-schema / reliable JSON; if not, parse defensively.
- **Cost:** each fallback fire = one `claude -p` call (Claude quota). The fallback window + useful-only filter keep it low. Note in the plan.
- **Per-brother rollout:** which brother first (Agent 4 or Agent 1 — the two that stranded collaborators), then generalize.
- **Tab/responder cursor sharing:** the responder uses its OWN session/cursor (`responder-agentN`), separate from the tab's watch cursor, so they don't fight. Confirm in the plan.

---

## 8. Path

Spec review (Hemanth) → **Agent 7 feasibility review** (his standing role — and this is squarely the owned-worker/cross-engine territory he flagged as the architecturally-hard part; his own bridge is the template) → implementation plan → build (per-brother, starting with one).

---

## 9. References

- Template: `scripts/office/codex_agent7_bridge.py` (the proven headless owned-worker, `f69e3b8`)
- Parent: `docs/superpowers/specs/2026-05-31-the-office-app-design.md` (spec v2 — owned-worker model, capability contract)
- Agent 7's review that named owned-workers the hard part: `agents/audits/the_office_app_feasibility_review_2026-05-31.md`
- Memories: `project_the_office_live_agent_bus`, `feedback_no_reset_hard_on_shared_tree`, `feedback_no_overclaim_in_rtc`
- Existing: `scripts/office/office_bus.py`, `office_watch.sh`, `office_status.py`
