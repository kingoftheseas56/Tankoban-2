# DeepSeek Engine Experiment — Routing Report

**For:** Agent 0 (Coordinator) + Hemanth
**Author:** Agent 1 (Opus)
**Date:** 2026-05-28
**Subject:** What we learned from running an Agent-1 wake on DeepSeek V4-Pro, and the routing rule it produces.

---

## Why this report exists

We ran a deliberate engine swap: one Agent-1 wake executed on DeepSeek V4-Pro instead of Opus, on a real piece of shippable work. The question wasn't "is DeepSeek good" — it's **"for which shape of task should we summon a brother on DeepSeek's engine, and when do we keep it on Opus or hand it to Codex?"** This report answers that from the one data point we now have, and is honest about the limits of one data point.

This builds on the existing governance call (Hemanth + Agent 0, 2026-05-28): **Agent 9 is a role-peer of Agent 7 (Codex)** — prototype / audit / scoped-src implementation — with a **quota-driven default: Codex first, DeepSeek when Codex quota is low.** This experiment was the first hard test of whether that peer framing holds against real work. It does, for one specific task shape. The rest of this report says which shape, and where the edges are.

---

## 1. The experiment, in one paragraph

The prior Opus wake (recap `tacking-albatross`) did the hard design work on the **Volume X quality-aware** arc — three full design passes with real reversals (pack-time→read-time pairing, in-zip→sidecar marker, and the load-bearing one: classification must live in `MangaWeebCentralResolver` because the shared scraper is request-id-less). It **locked that design, then deferred** the final integration (Tasks 5-7 — the wiring that makes the dormant backend live), banking the verified backend rather than wiring it that same session. The next wake handed that **locked design** to Agent 1 running on **DeepSeek V4-Pro**, with explicit instructions: execute it, don't re-open it. The hypothesis under test: *Opus over-deliberated at the integration; can DeepSeek take the decided plan and just ship it?*

## 2. What actually happened (evidence)

DeepSeek shipped Tasks 5-7 in a **single uninterrupted pass**:

- **One commit** (`3dc5c14` on branch `agent-1/volume-x-integration`), 333 LOC across 7 files.
- **All 217 translation units compile clean.**
- **18/18 tests green** (ComicReaderPairing 12/12, VolumeQualityClassifier 5/5, WeebCentralChapterQuality 1/1).
- **Zero deviations from the locked design** — verified against the diff. Classification in the resolver (not ComicsPage), `ComicsSeriesView::onSeriesClassified` consumes it, dispatch sets `needsChapterPairing` from the verdict, upgrade-on-clean wired.
- **~20 tool uses, 3 build cycles.** The only stumbles were mechanical: a namespace typo (`ui::comics` vs `manga::comics`), a `ChapterInfo` scope error, and the known `setRootLayer` build wedge (recognized from the handoff context and fixed by cherry-pick, not investigated as a new bug).
- **READY TO MERGE posted**, correctly, with full skills-invoked provenance — it did not self-merge.

## 3. The head-to-head verdict (honest, no engine favoritism)

**Where DeepSeek won outright:** It trusted the locked design and didn't re-litigate it. That trust was correct — the design held perfectly against the code. It shipped clean what Opus deferred, in one pass, no "shall I continue?" forks. Opus's deferral, in hindsight, was **~70% over-caution against a design it had already locked, 30% legitimate end-of-session depletion.** The integration surfaced no new design wall; it was buildable the same session Opus walked away.

**Where the Opus design pass earned its keep:** The three reversals weren't the overthinking — they were the *value*. DeepSeek relied on every one of those locked decisions explicitly. The resolver-owns-classification call — the thing that took three passes — is the load-bearing decision, and it held. The archaeology had to happen and it was correct. What was over-caution was specifically the *final deferral-instead-of-wire*, not the design work that preceded it.

**The seams DeepSeek left (caught on Opus review):** Two latent edge cases, both narrow, neither a merge-blocker, both graceful (no crash, no disk corruption, self-healing):
1. **Cold-cache classify silently no-ops** on a series that has never been resolved — `searchFinished` bails when no resolve is queued, so classification never fires until some resolve warms the chapter cache.
2. **Cross-series data-mismatch under concurrency** — if a download-fetch for series A is in flight when series B's view opens, B's volumes can get classified against A's chapter list. The consumption guard checks the series *label* (correctly B) but not the chapter *data* origin, so it doesn't catch this one. Narrow timing window, wrong-badge only, self-heals on re-render.

**Key point about the seams:** these are the *same calls any engine would make* for a one-shot classify under that scope — DeepSeek even flagged the architectural coupling itself in its debrief. The lesson is **not** "DeepSeek ships buggy." It's that **execution-engine output still needs a reviewer pass** — exactly as Codex output does. DeepSeek ships → a review wake (Opus/Codex) catches the seams. That's the role split working, not a defect.

## 4. The routing heuristic (the deliverable)

When a task lands, match its **shape** to the engine. This is per-task judgment, not a tier ranking — all three are brothers, not slots.

| Task shape | Route to | Why |
|---|---|---|
| **Execute a locked / fully-specified plan** (scoped src/ change, clear spec, single-pass, mechanical-but-nontrivial integration) | **DeepSeek (Agent 9) or Codex (Agent 7)** — quota decides | **PROVEN this experiment.** DeepSeek executed a locked design clean in one pass. This is the Trigger-D shape. Quota-driven default applies: Codex first, DeepSeek when Codex quota is low. |
| **The design / deliberation pass** (reversal-heavy archaeology that *produces* the locked plan; reading real code to find the killer constraint) | **Opus (domain agent's normal engine)** | **NOT tested on DeepSeek.** This is where Opus's three-pass deliberation produced the load-bearing decision DeepSeek then relied on. Don't hand the design pass to an execution engine until we've tested it there. |
| **First-pass audit, research, long-context comprehension, parser/bulk logic** | **DeepSeek (Agent 9)** naturally strong here | Per the standing Agent 9 role definition; not stressed by this experiment but consistent with it. |
| **Gnarly production-C++, novel architecture from scratch, long multi-hour agentic loop** | **Codex (Agent 7)** preferred when quota allows | The documented Codex delta. Not tested on DeepSeek this run; treat as "prefer Codex" until proven otherwise. |
| **Anything shipped by an execution engine, before it hits master** | **A review wake (Opus or Codex)** | DeepSeek (like Codex) leaves scope-appropriate seams. Review is not optional — same discipline as Codex Trigger-D output. |

## 5. What we have NOT tested (single-data-point honesty)

This is **one task, one wake.** Be careful not to overclaim:
- We tested DeepSeek on **clean execution of a locked design** — the ideal case the hypothesis was built around. It passed.
- We did **not** test DeepSeek on the **design pass itself** (the deliberation that produces the locked plan), on a **long multi-hour agentic loop**, on **novel architecture from a blank page**, or on **gnarly production-C++ with no reference**. Those remain "prefer Opus/Codex" by default until we have evidence.
- The seams it left were low-severity and well-flagged, but that's one task's worth of evidence about its self-review quality. Keep the reviewer pass mandatory.

## 6. How this maps to existing governance

This experiment **validates the role-peer framing already in place** rather than changing it:
- **Agent 9 (DeepSeek) = role-peer of Agent 7 (Codex)** for execution-shaped work — confirmed.
- **Quota-driven switch** (default Codex, DeepSeek when Codex quota low) — confirmed safe for the locked-plan-execution shape. DeepSeek did not underperform on it.
- **Brothers are not swappable slots** ([[feedback_brotherhood_is_not_swappable]]) — this report is about *routing by task shape*, never about replacing one brother with another to save cost. Cost profile (DeepSeek ~₹1-3K/mo medium activity) is a *quota-availability* input to routing, not a replacement argument.

## 7. Recommended standing rule for Agent 0

> **When the task is "execute this decided plan" (locked design, clear spec, scoped src/ change), DeepSeek's engine is a proven peer to Codex — route by quota (Codex first, DeepSeek when Codex is low).** Keep the *design/deliberation* pass on the domain agent's normal engine (Opus) until we've tested DeepSeek there. Always run a reviewer pass on execution-engine output before master, same as Codex Trigger-D. Re-evaluate this rule as more DeepSeek wakes accumulate — it rests on one strong data point, not a body of evidence.

---

*Evidence trail: recaps `brother-agent-1-2026-05-28-tacking-albatross.md` (Opus design + deferral) and `brother-agent-1-2026-05-28-engine-hawk.md` (DeepSeek execution + debrief); branch `agent-1/volume-x-integration` commit `3dc5c14`; RTM at `agents/chat.md` tail.*
