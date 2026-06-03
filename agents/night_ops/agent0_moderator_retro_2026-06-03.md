# Agent 0 — Moderator Retrospective: What Went Wrong in the Office Collaboration
### 2026-06-02 → 2026-06-03 · first-person, from the coordinator's seat · what to AVOID

Hemanth asked me to document, honestly, my personal experience moderating the overnight Office collaboration — everything that went wrong and what we must avoid. This is that, unvarnished.

---

## THE CORE MISTAKE: an autonomous tree-owner running ALONGSIDE live brothers

The Night Watch foreman was designed to **own the shared working tree** — capture each brother's finished fix to a patch, then `git checkout` the files clean for the next worker. That design is correct **only when the team is idle.** I let it run while agents 1/2/3/4/5 AND Hemanth were all live and editing. The result was a string of collisions:

- **It reverted agent4's live WIP out from under him.** His One Piece fix got captured as patch A001 and the tree reverted — preserved, but he opened his editor to find his work "gone." Startling and wrong.
- **agent1 redid A002 entirely.** A background-summoned agent1 had already fixed the comics `msleep`; the foreman reverted it to a staged patch; the *live* agent1 then saw the blocking `msleep` still in the tree and re-implemented the whole thing. Pure duplicate effort, caused by the revert hiding completed work. **And my "don't redo it, it's already staged" message to him was WRONG** — I assumed the work was in the tree when the foreman had reverted it.
- **#7/#8 and the theme fix were each done twice** (parallel sessions of the same brother). Recoverable, but wasted.

**AVOID:** never run a tree-owning automaton concurrently with live editors on a shared tree. Either (a) run it only when everyone's idle, or (b) give every worker a truly isolated lane and never touch the shared tree from automation. The "flat-on-master shared tree" + "autonomous foreman" combination is fundamentally unsafe while humans/agents are live.

---

## DUPLICATE-SPAWN of a heads-down brother

The dispatcher spawned a background agent1 *while the live agent1 was heads-down on the RCO arc.* His heartbeat went stale during the long task, so liveness-detection judged him "dead" and spawned a clone → two agent1 contexts doing overlapping work. The #6 "re-check live before fallback" guard helps, but a stale heartbeat during a genuinely long heads-down task still slips through.

**AVOID:** liveness must not be a short-window heartbeat alone. A brother mid-long-task is live but quiet. Either beat continuously regardless of task, or treat "recently committed/posted" as liveness, or confirm with the brother before cloning him.

---

## THE FOREMAN OVER-DISPATCHED to one busy brother

It kept summoning agent2 (the Track-C workhorse) for item after item while his per-target lock was still held → a stream of "already handling — try again" notices (which ALSO spammed me every retry until I added the `quiet` flag). It never spread work to the free brothers.

**AVOID:** a dispatcher/foreman must check a brother's lock/availability before summoning, and load-balance across free brothers — not pile onto whoever's already busy.

---

## THE MODERATOR BECAME A BOTTLENECK (me)

My Office watch woke me on **every** brother RESULT — and the foreman was *also* collecting those same bus posts. I reactively answered each one (acking, deconflicting, catching duplicates, landing fixes), which:
- burned Opus fast (I hit a session usage limit once, ~22:53),
- added redundant noise, and
- made *me* the serialization point for landing + coordination.

I should have decoupled my watch from routine RESULTs far sooner and let the foreman own collection, surfacing only true escalations to me.

**AVOID:** the moderator should not be in the loop for routine worker output. Watch for escalations + Hemanth only. Don't let "I'm reachable" become "I process every event."

---

## LANDING OWNERSHIP WAS AMBIGUOUS (me vs foreman vs morning-review)

Brothers left work "uncommitted for the foreman to stage." The foreman staged (reverted to patches, stage-only). I *also* manually committed some (the low-risk Office ones). This created races: I'd commit a fix the foreman was about to stage; a brother's commit interleaved and my local HEAD jumped to theirs mid-operation. No single owner of the "land" step → duplicate handling and confusion about what was on master vs staged vs in-tree.

**AVOID:** define ONE landing path and stick to it. Either the foreman stages everything and agent0 batch-lands at review, OR brothers commit to their own lane and agent0 merges. Never both. The hybrid caused the mess.

---

## PARALLEL-FIX, SERIAL-INTEGRATE, UNOWNED-INTEGRATE → the app was untestable

Five brothers fixed freezes in five lanes / staged patches. But the **running app never had them** — they were never integrated into one build. Hemanth couldn't smoke One Piece for almost the entire session because the fixes lived everywhere except the build he was running. Integrating them into one responsive build (apply patches + rebuild) was *unowned* until I did it manually in the final hour — and that hit its own friction (my `cmd /c` invocation silently no-op'd; the hung app locked the exe per Rule 1; took several tries with PowerShell to actually rebuild).

**AVOID:** parallel fixing without a continuous-integration owner produces a pile of fixes and zero testable result. Someone must own keeping ONE running build current with the merged fixes, continuously — not as a final-hour scramble.

---

## THE AUTONOMOUS WORKER SHIPPED A BROKEN FIX (gate saved it)

The Sonnet foreman prepared hardening #5 (wrong-engine guard) but only half-did it — `classify_summon` returned a new action that `_dispatch` didn't handle, so it would have spawned the impostor anyway. My independent re-check caught it before commit. The gate worked — but it confirms a lighter-model automaton's code cannot be trusted unreviewed.

**AVOID (keep):** never auto-commit an automaton's code without an independent adversarial re-check. This one we got right — keep it.

---

## WHAT ACTUALLY WORKED (so we don't over-correct)
- Reachability + ack-or-fallback delivery: once hardened, summons reliably reached brothers.
- The independent-review gate caught every bad autonomous edit.
- Background brothers are EXCELLENT at parallel diagnosis/research (the sweeps, the observability design).
- Staged patches preserved work even when the tree churned — nothing was truly lost.

---

## THE ONE-LINE LESSON
**Autonomy and a shared mutable tree don't mix while the team is live.** Every collision tonight traces to an automaton (or a duplicate spawn) touching state that a live brother also owned. Isolate the work, single-own the integrate + land steps, and keep the moderator out of routine traffic.
