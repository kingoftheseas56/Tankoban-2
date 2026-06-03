# Agent 2 (Books / TankoLibrary) — Night Recap 2026-06-03

Woken mid-shift by Hemanth ("agent 2 wake up, the office needs you") to pick up a
foreman summon that couldn't auto-spawn me (spawn cap hit). Wrote with complete
honesty per Hemanth's stop-and-document order.

---

## ✅ ACHIEVED — committed AND pushed to origin/master (durable, reset-proof)

1. **Track C #10/#11/#12 — `b6476b3`** (office reliability, from `agents/audits/office_reachability_hardening_2026-06-02.md`)
   - **#10 unified liveness window.** Roster (`office_status.py`, was 25s) and dispatcher
     (`office_dispatch.py`, was 15s) disagreed on heartbeat staleness → roster showed a
     brother "live" while the dispatcher cloned him. Now BOTH read one env
     `OFFICE_LIVENESS_SEC` (default 30). They can never disagree again.
   - **#11 roster activity-fallback.** Codex/DeepSeek (run no `office_watch`, never beat)
     were shown as a dead grey "?". Roster now falls back to recent bus activity →
     new `wake_state "active"`, rendered green **"live (active)"** in `office_web.py`.
     3-state honesty preserved (no beat + no activity = "unknown", never "down").
   - **#12 summon fate-trail.** (Authored by the background agent2 brother under the
     no-commit leash; I reviewed + bundled it.) `_fate()` append-only JSONL to
     `.office_delivery.log` at every summon transition.
   - Gate: py_compile + ALL 4 office test suites green; +6 new #11 tests.

2. **Agent 1's comics freeze-fix — `7f1eb3a`** (cross-domain rescue)
   - Non-blocking AniList/MangaUpdates throttle (`scheduleThrottled` + `m_nextAllowedMs`)
     replacing the blocking `QThread::msleep(1000)` that froze the GUI ~1s per series lookup.
   - Committed by me (live) per **Hemanth's explicit ruling** (save-now / Codex-review-after)
     to break a DOOM LOOP (see below). **Codex reviewed → APPROVED** (all 8 DoD items MET,
     only pre-existing unrelated `PickBestBookFileTest` failing). Mandatory comics-review
     gate SATISFIED.

3. **Track C #18 — `9e74351`** (model validation)
   - `OFFICE_BROTHER_MODEL` validated up front (`_validate_model`): a typo can no longer
     burn a spawn-cap slot + lock on a doomed `claude -p --model <garbage>`. +7 unit tests.

---

## 🔴 KEY SYSTEMIC FINDING — the DOOM LOOP (most important thing I found)

A **live, unguarded session** running `git checkout/restore/reset` on the shared working
tree was **silently erasing brothers' uncommitted work**.
- My #10/#11 was destroyed once (no reflog/stash/blob trail = a `git checkout -- scripts/office/`);
  recovered from session context.
- **Agent 1's comics fix was wiped 7+ times overnight** — he kept re-applying, it kept vanishing.
- `bg_git_guard` ALREADY blocks checkout/restore/reset/clean/stash for BACKGROUND brothers,
  so the culprit is a **LIVE tab** (most likely the foreman's staging routine).
- **Mitigation deployed:** committed all recovered work + posted a standing rule to every
  brother — *commit verified work immediately; never `git checkout/restore .` / `reset --hard`
  on the shared tree.*
- **ROOT-CAUSE ACTOR STILL UNIDENTIFIED.** Until found, "leave uncommitted for staging" == "lose it."

---

## ⚠️ COULDN'T FINISH / left mid-stream

- **My own Books BLITZ freeze-fixes** (from the 09:07 BLITZ, RESULT at seq 909):
  `BooksPage::validateAll()` off-thread (QtConcurrent) + `TankoLibraryPage` cover-glob-once
  + `BooksCatalogueLibraryStore` sync→async save. **BUILD OK at 09:30 but UNCOMMITTED and
  ctest NOT run.** Deliberately NOT committed: the async-save change alters a **persistence
  contract** and was never run through the save/reload test — committing unverified
  persistence risks **library data loss**. These are still dirty in the tree (`src/core/book/
  BooksCatalogueLibraryStore.{cpp,h}`, `src/ui/pages/BooksPage.cpp`, `src/ui/pages/TankoLibraryPage.cpp`)
  and at doom-loop risk (intact as of ~11:15).
- Did **not** take Track C #16/#20/#21/#22 (time-boxed; #20 touches `office_watch.sh` =
  agent5 shared-file collision risk).
- **Could not verify live app smoothness or the tankoctl bridge** — Tankoban is NOT running
  (the bridge binary is healthy but reported "no live Tankoban to talk to").

---

## 📋 STILL TO DO (handoff — next agent2 / live brother / Hemanth post-reset)

1. **Relaunch the app** (`build_and_run.bat`) and actually FEEL the smoothness + smoke the
   freeze fixes (comics series lookup no longer freezes; theme; Books library scroll).
2. **Verify + commit my Books freeze-fixes:** run ctest (esp. `PersistAndReload` for the
   async-save), Codex-review the `BooksCatalogueLibraryStore` Site-3 contract change, THEN
   commit. They're at doom-loop risk until committed.
3. **Hunt the doom-loop actor:** which live session runs `git checkout/restore/reset` on the
   shared tree (likely foreman staging). Highest-priority office-reliability fix.
4. **Track C remaining:** #16 concurrency cap, #20 atomic beat write (touches `office_watch.sh`),
   #21 force-kill cursor cleanup, #22 foreman lease-exemption.
5. **Comics throttle follow-ups (non-blocking, from Codex):** wall-clock→monotonic time
   (pre-existing, not a new regression); add FIFO + destroy-before-timer tests.

---

## Honest self-assessment

Core assigned work (Track C #10/#11/#12 + #18) **delivered, tested, durable.** Biggest win:
diagnosing + mitigating the DOOM LOOP and saving three brothers' worth of work from being
wiped. **Biggest gap: my own Books domain freeze-fixes are NOT landed** — correctly held for
persistence verification, but that means the Books app-smoothness work Hemanth wanted is not
yet durable. The doom-loop root actor is identified-in-kind (a live `git checkout/reset`) but
not pinned to a specific session.

**Durable commits this shift:** `b6476b3`, `7f1eb3a`, `9e74351` (all on origin/master).
