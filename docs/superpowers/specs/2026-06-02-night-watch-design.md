# The Night Watch — Autonomous Overnight Operation (Design + Operating Contract)

**Date:** 2026-06-02. **Owner:** Agent 0. **Status:** design (brainstorm-approved by Hemanth via decision batch this session).

This doc is BOTH the design record AND the operating contract the foreman reads each tick. If you are a foreman tick, these rules are binding.

## Mission

Run the brotherhood overnight, unattended, to make the app fast and the repo + Office maximally agent-legible — and survive both failure modes Hemanth named: **tabs closing** and **agent0 going idle**. The operation must NEVER depend on any single live session, and if it stalls it must be VISIBLE, never silent.

## The four tracks

| Track | Mode | What | Apply policy (how fixes land) |
|---|---|---|---|
| **A — App heaviness** | execute | Hunt + fix every place the app does too much on the UI thread (sync I/O, O(n)-per-row loops, per-row disk/SQL, unthrottled timers). | **Low-risk auto-commit** behind the green gate; behavior/UI/threading → **stage for morning smoke**. |
| **B — Repo legibility** | research | Make the repo maximally processable by agents (token-cost, accuracy, findability, context). Living agent-map/index + refactors + tooling. | **Propose + stage only.** No repo-structure auto-commits — it's the foundation. |
| **C — Office reliability** | research+execute | Keep hardening the Office (seed = the 22-item backlog in `agents/audits/office_reachability_hardening_2026-06-02.md`). | **Low-risk auto-commit** (pure-Python, unit-tested, low blast radius); structural → stage. |
| **D — Observability layer** | research | The KEYSTONE: make the app "so transparent an agent can see everything with eyes closed" — tankoctl-saturation + profiler + hang-capture + visual-verify + telemetry pillars. | **Propose + stage** architecture; purely-additive read-only commands may auto-apply behind the gate. |

D is the keystone: once it exists, agents auto-verify "the app works" without Hemanth, which is what lets every track self-gate.

## The gate philosophy (Hemanth's words: "as long as the app works I'm good")

Hemanth is NOT a code reviewer. The gate is **automated proof the app still works**, never a diff review:
- **Auto-commit gate** = `build_check.bat` BUILD OK **AND** `ctest` green **AND** (for app changes, once Track D lands a smoke pillar) an automated smoke. No green → no commit.
- **Stage-for-morning** = anything that can't be auto-proven safe (behavior/threading/UI/subjective) → saved as a ready-to-apply patch + report; Hemanth's morning role is a **smoke** (open, click, "feels right"), never a code review.
- **Adversarial pre-commit check:** a fix is never graded by its own author. Before any auto-commit, an INDEPENDENT agent re-checks the fix + its risk classification (the same adversarial pattern that caught agent0's own ack-or-fallback holes this session).

## The self-healing tower (architecture)

```
detached OS loop process  (night_watch_loop.sh — survives tabs + agent0 idle; dies only on reboot/kill)
   └─ every ~15 min fires →  foreman tick  (claude -p, headless Opus, fresh each fire, exits after one cycle)
                                 ├─ health: verify dispatcher alive (heartbeat fresh) → restart if dead
                                 ├─ collect: scan bus + results for completed work from prior ticks → gate → commit/stage
                                 ├─ dispatch: pick next backlog item per track → summon owner-brother / launch research workflow
                                 ├─ write: night_ops/STATUS.md + foreman heartbeat + morning-report append
                                 └─ exit
        ↓ (summons routed by)
   office_dispatch.py  (singleton-locked, self-heartbeat — hardened tonight)
        ↓
   owner-brothers (bg Opus, leashed: prepare fixes, can't commit)  +  research workflows (bg Opus fan-outs)
```

Every layer restarts the one below: the loop re-fires the foreman; the foreman restarts the dispatcher; the dispatcher's ack-or-fallback re-summons a deaf brother. The root of trust is the one detached loop process.

## The foreman tick algorithm (binding)

Each tick, in order:
1. **Acquire the foreman singleton lock** (`agents/night_ops/.foreman.lock`, mkdir, stale-reclaim). If another tick holds it fresh → exit (no double-foreman). Refresh it.
2. **Health-check the dispatcher:** if `agents/.office_heartbeats/dispatcher.beat` is stale → relaunch the dispatcher detached. Log it.
3. **Collect prior-tick results** from the bus + `agents/night_ops/inflight/`. For each completed item:
   - Run the **independent adversarial re-check** (spawn a fresh reviewer) on the fix + its risk class.
   - If classified low-risk (track A/C) AND re-check passes AND `build_check` + `ctest` green → **commit to master** (foreman is NOT leashed; it can commit). Else → **stage** the patch + report.
   - Track B/D → always stage (propose-only); append to the proposal backlog.
4. **Tree-safety precondition:** before dispatching new execute-work, `git status` must be clean of non-night work. If dirty from an unknown source → PAUSE execute-tracks, flag in STATUS.md, do NOT touch the tree.
5. **Dispatch next:** Track A is serialized on the app build (one in-flight). Tracks B/C/D research can run in parallel. Pick the next `todo` item per track, summon its owner (bg Opus) or launch its workflow, mark `inflight`.
6. **Write STATUS.md + heartbeat + morning-report.** Exit.

## Safety invariants (never violate)

- **One app build at a time** (shared `out/`). Track A is strictly sequential.
- **Shared-tree exclusivity:** only ONE brother edits the working tree at a time; the foreman captures + cleans between workers; NEVER `reset --hard`/`checkout` that could destroy another agent's uncommitted work (start from a clean, committed tree).
- **No fix auto-commits unverified:** green gate + independent adversarial re-check, always.
- **Visible, never silent:** every tick stamps a heartbeat + STATUS.md. A dead operation shows as a stale foreman heartbeat — a morning tell, not a silent loss.
- **Quota is no object tonight** (Hemanth) — fan research workflows WIDE; prefer thoroughness.

## What Hemanth wakes to

`agents/night_ops/MORNING_REPORT.md`: app fixes auto-committed (with green proof) vs staged-for-smoke (one-click apply list); the Track-B legibility proposal stack (ranked, yes/no); Track-C Office hardening landed + proposed; Track-D observability design + backlog; and a FAILURES section (what didn't work + why). Plus a one-line health verdict: did the watch run clean all night, or did it stall (and when)?

## Open / deferred
- Track A backlog seed: from tonight's stability sweep + a fresh heavy-area scan (foreman or agent0 seeds before launch).
- Track C seed: the 22-item hardening backlog (already written).
- B/D backlogs: produced by the running research (workflow w0lvoroj7 + Agent 5 summon 848).
- The automated app-smoke gate depends on Track D's smoke pillar; until it lands, app behavior changes always stage for Hemanth's morning smoke.
