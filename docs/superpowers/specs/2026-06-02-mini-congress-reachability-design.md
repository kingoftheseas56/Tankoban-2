# Spec + Plan — Mini-Congress, Arc 1: Reachability Foundation

**Date:** 2026-06-02 · **Author:** Agent 0 (Opus) · **Status:** approved (Hemanth, this wake)
**Predecessor:** `2026-06-01-multi-engine-brother-design.md` (the 4-engine roles this builds on)

## Why (the one-paragraph frame)

The mini-congress turns the brotherhood from "tabs Hemanth messages one at a time" into a
room where a problem gets solved *together*. The whole dream (drop a problem → the crew
self-organizes → solves it) rests on one brick nothing off-the-shelf builds: **reachability**
— a brother getting reached when his tab isn't sitting open and watching. This arc builds
*only that brick* and proves it. The auto-organizer ("foreman") is a later arc that stacks on top.

## What exists today (do not rebuild)

`scripts/office/` already has: a bus (`office_bus.py`, append-only `agents/bus.jsonl`,
`{ts,seq,from,to,kind,arc,msg}`), a per-tab watch (`office_watch.sh` → harness re-invokes a
**live, watching** tab when a new message lands), heartbeats (`agents/.office_heartbeats/<agent>.beat`,
refreshed each watch tick → freshness = "is this tab alive"), and `chat_send.sh`.
**Gap:** a brother whose tab is closed/idle (no watch process) cannot be reached — there is
nothing running to wake.

## Decisions (locked with Hemanth this wake)

1. **Scope:** reachability foundation only; prove "Brother A pulls in idle Brother B without Hemanth."
2. **Mechanism:** a summoned idle brother is spun up as a **background headless `claude` session**
   that loads his identity (kernel + latest recap), does the task, posts the result to the Office, exits.
   If his tab **is** live-watching, the summons routes to the tab instead (never two of the same brother).
3. **Leash (tight):** a background brother **posts results, never commits to master**; **cannot
   summon another brother** (no chains); bounded by a **spawn cap + cost ceiling**.

## Components

1. **Summon (post side)** — `office_bus.py summon <from> <to> <task>` (new subcommand; appends a
   `kind="summon"` record) + `scripts/office/office_summon.sh "@agentN" "task"` wrapper (derives
   `from` from the tab session, mirrors `chat_send.sh`).
2. **Dispatcher** — `scripts/office/office_dispatch.py`: persistent loop (3s poll) over `bus.jsonl`
   for new `kind="summon"` records above its own cursor (`agents/.bus_cursors/_dispatch.seq`).
   Per summon to `agentN`:
   - **Liveness check:** `agentN.beat` fresh (< 15s)? → **live tab**: do nothing (its watch wakes it). Log "routed-live".
   - **Not live:** acquire a per-target mkdir lock; check guardrails; **spawn the brother** (detached); else skip.
   Advance cursor.
3. **Brother runner** — `scripts/office/spawn_brother.sh <agentN> <from> <seq> <task>`: holds the
   per-target lock for the run's lifetime; resolves the brother's latest recap; launches
   `claude -p "<wake prompt>" --permission-mode acceptEdits` (CWD = repo) with the tight-leash prompt;
   logs to the spawn ledger; on exit, unlocks. Posts results via `office_bus.py append agentN <from> chat null "RESULT: ..."`
   (direct `from=agentN`, no session needed).
4. **Launcher wiring** — `open_office.bat` starts `office_dispatch.py` alongside `office_web.py`
   (self-cleaning, same pattern as the existing launcher), so Hemanth's existing "open the Office" gesture arms it.

## Guardrails (enforced, not just hoped)

- **No master commits** — tight-leash prompt forbids it; the run leaves changes uncommitted for a live brother to merge.
- **No chains** — dispatcher refuses summons whose `from` is a background origin (`*-bg`), and the prompt forbids summoning.
- **No double brother** — liveness check (live tab → route there) + per-target lock (one background spawn at a time).
- **Spawn cap + cost** — `agents/.office_spawns.jsonl` ledger; dispatcher refuses past N spawns/hour (default 5) and posts
  "spawn cap hit — ask Hemanth"; each run is a bounded one-shot `claude -p` (small task), no idle loop.
- **Office-open requirement** — dispatch only runs while the Office is open (Hemanth's cockpit). Acceptable for v1.

## Build steps (execute in order)

1. `office_bus.py`: add `cmd_summon` + wire into `main()`; add `summon` to the usage line.
2. `office_summon.sh` wrapper (session → `from`, mirrors `chat_send.sh`).
3. `spawn_brother.sh` — recap resolve, wake prompt, `claude -p` launch, ledger log, lock lifetime, result-post.
4. `office_dispatch.py` — poll loop, liveness check, guardrail checks, detached spawn, cursor.
5. Wire `office_dispatch.py` into `open_office.bat` (self-cleaning start).
6. **Codex review** the dispatcher + runner (safety: no double-spawn, no chain, lock correctness, cap).
7. **Prove:** with a brother's tab CLOSED, Agent 0 `summon`s him a small real task → dispatcher spawns him headless →
   he posts a RESULT to the Office → exits, with Hemanth not in the loop. Capture the bus lines as evidence.

## Success criterion (done = this)

A summon to a closed-tab brother produces a real, identity-correct result posted to the Office by that brother's
background session, with no Hemanth involvement, no master commit, and the spawn visible in the ledger.

## Out of scope (next arc)

The auto-organizer/foreman (deciding *who* to summon for a dropped problem), cross-brother chains, background
brothers that merge to master. Those stack on this brick once it's trusted.
