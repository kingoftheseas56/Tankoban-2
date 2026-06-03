# Crash / Instability Inventory — 2026-06-03 (build out/Tankoban.exe @ 18:48)

Compiled by Agent 0 during live systematic-debugging with Hemanth. The leading
theory: **#1–#3 are likely ONE root-cause class** — a worker thread busy-spinning
(~1–2 CPU cores) while the GUI heartbeat keeps beating, ending in either a wedge
("Not Responding") or a crash, depending on timing (a race). Not 3 separate bugs.

---

## #1 — Crash/hang on app OPEN (startup)
- **Symptom:** window appears ~3.5s after launch, then a worker thread pegs ~1.7
  cores across **65 threads**. Outcome is a RACE: one run **crashed** at ~6.5s
  (RAM collapsed 170→9MB → process exit), another **hung** spinning (>20s).
- **Key tell:** `HANG_DETECTED.json` NOT written and process Responding=True →
  the GUI thread is alive; a **WORKER thread** is the one spinning.
- **Evidence:** reproduced ×2; full dump captured `out/dumps/hang_live.dmp` (6.4GB,
  contains the spinning thread's stack). No Windows crash-event (orderly teardown,
  not a clean SEH access violation — consistent with abort/spin-then-exit).
- **Suspects:** async/threading work kicked off on home-open (the QtConcurrent
  freeze-fix tasks: Books validateAll, poster cleanup, home-open prefetch) OR a
  pre-warmed FrameCanvas waitable loop.
- **Status:** dump captured → needs `cdb` to read the spinning stack.

## #2 — Crash/freeze when STARTING a video
- **Symptom:** player opens, controls render, timeline stuck at 0:00, Windows
  "Tankoban.exe is not responding" (Hemanth screenshot, One Piece).
- **Signature:** same worker-spin / GUI-wedge as #1.
- **Suspect:** FrameCanvas DXGI frame-latency **waitable-loop flood** (the wait
  thread re-posts renderFrame faster than the GUI can present → event-queue flood).
- **Status:** Agent 3 diagnosed this + shipped a fix (`46555a8`, in the 18:48
  build) — **but it's still happening** → fix incomplete or a sibling path.

## #3 — Playback busy-livelock (Agent 3's original report)
- **Symptom:** during playback, worker TID pegged (477 CPU-s), process CPU climbs,
  RAM grows, "Not Responding."
- **Status:** very likely the SAME bug as #2. `46555a8` landed but is not holding.

## #4 — (Earlier, self-healed) torrents.db corruption hang on open
- **Symptom:** force-kill mid-write corrupts torrents.db → startup reconcile loops
  → UI freeze on open. Memory: `feedback_app_hang_torrents_db_corruption`.
- **Status:** RULED OUT for today — current torrents.db `integrity_check: ok`; the
  `.corrupt-2026-06-02-1739` files are yesterday's, already quarantined by self-heal.
  Caveat: the force-kills we do for rebuilds can re-trigger it; use clean stop.

---

## Plan (systematic, no thrashing)
1. **Read `46555a8` + current FrameCanvas `waitableLoop`** — is Agent 3's fix
   complete? (fast, code-level; covers #2/#3, the one we can see + is diagnosed.)
2. **Get `cdb`, read `hang_live.dmp`** → pinpoint #1's exact spinning loop/function.
3. If #1 and #2/#3 are the same spinner → **one fix → re-verify all triggers.**
4. Whatever we change → cross-model review (it's threading) → rebuild → smoke each trigger.
