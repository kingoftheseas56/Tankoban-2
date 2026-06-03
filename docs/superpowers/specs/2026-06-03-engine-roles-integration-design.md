# Engine Roles + Integration — Design (gov-v14 follow-through)

**Date:** 2026-06-03 · **Author:** Agent 0 (Opus) · **Status:** design, pending build
**Context:** gov-v14 retired the Agent 7/9 personas; Codex/DeepSeek/Gemini are now tools any mainline brother (0-5) summons via `scripts/engines/engine.py`. This spec defines each engine's ROLE and the plan to make all three genuinely usable. Reshaped mid-design by a proof pass that exercised the broker live.

---

## The roles (defined lanes, override-with-reason)

| Engine | Verb | Role | How it's wired |
|---|---|---|---|
| **DeepSeek** | `grunt` | **Coding muscle.** The routine/bulk coding within an implementation. The mainline brother (Claude) keeps the important, load-bearing, architectural parts and hands the routine rest to DeepSeek — "senior keeps the hard 20%, junior does the boilerplate 80%." | Runs through the Claude CLI with DeepSeek's endpoint, so it speaks our tool protocol and can do multi-step file work. |
| **Codex** | `review` | **Brain.** Cross-model review of a brother's diff against a written Definition of Done; gnarly/novel production-C++; architecture second-opinion. The producer≠reviewer gate. | `codex exec -` (stdin), native. |
| **Gemini** | `read` + `see` | **Memory + eyes.** `read`: long-context text research, web-grounded synthesis. `see`: multimodal — read screenshots / comic pages / PDFs, visual-smoke assist ("does this layout look right"). The one capability neither Codex nor DeepSeek has. | Raw REST API (ask-and-receive; no multi-step file work). |

**Override rule:** the lane is the default; a brother may pick another engine with a one-line reason.

**Why these lanes (the load-bearing reasoning, validated by proof):** review is the lane with real organic demand — a brother can't see his own blind spots, so cross-model review pulls naturally (the ledger shows agent1 already firing real Codex reviews). Pure "execution muscle" had NO demand because every brother is a capable Claude that executes its own work — so DeepSeek's role is sharpened to *coding muscle for the routine portion of a brother's own implementation*, which creates recurring demand without risking quality (Claude owns what matters). Gemini's differentiator is multimodal — uniquely valuable in a visual media app.

---

## What proof revealed (the live exercise, 2026-06-03)

Insisting on proving instead of assuming surfaced three real things:

1. **Codex review: proven + already adopted.** Ledger shows agent1 firing real Codex reviews today (western-downloads: match scorer, HttpFileDownloader, GetComicsResolver). This lane works.
2. **DeepSeek: engine works (June 1-2 smokes), but the broker's concurrency bug broke a real attempt.** Agent 3 fired a genuine DeepSeek `grunt` review of a player FrameCanvas waitable-loop deadlock fix; it collided with a concurrent call and errored (no success row logged). See bug #1.
3. **Gemini: account-blocked.** The API returns `403 PERMISSION_DENIED — "Your project has been denied access."` The key is well-formed but its Google project is denied. The gate had flipped `enabled: true` without a durably-working key; now corrected to `false`. This is an access problem, not a code problem.

---

## Blockers to clear

**Bug #1 — broker holds its lock across the entire call (concurrency).** `engine.py main()` runs `dispatch()` (the multi-minute engine call) *inside* `with _ledger_lock()`, so one long call blocks every other engine call globally and concurrent callers deadlock (EDEADLK). This bit Agent 3 + a test call live. **Fix:** hold the lock only around the atomic cap-check + slot-reservation (log row), then run `dispatch()` OUTSIDE the lock. Must keep cap accounting correct (the reservation under the lock is what prevents two callers both passing the hard cap). Verify against `test_engine.py` (don't regress the 20 tests).

**Blocker #2 — Gemini access (Hemanth's action).** A working key from a non-denied Google AI Studio project must land in `scripts/engines/.env`. No code fixes the 403. Until then the `read`/`see` lanes stay dormant.

---

## Integration plan (approach A: prove-then-extend)

1. **Fix Bug #1** (broker lock) — vs the existing tests. Unblocks reliable concurrent engine use.
2. **DeepSeek coding-muscle:** prove with one real "do the routine part" task that returns usable code (Codex review already proven/adopted).
3. **Gemini (after Blocker #2 clears):** re-run `gemini_gate.py` (text reliability), prove `read` on a real research question, then **build the `see` lane** — add `call_gemini_visual(image_paths, prompt)` that base64-packs images into the `inline_data` parts of the generateContent payload + a `see --image <path> "<prompt>"` CLI verb (model `gemini-2.5-flash` is already multimodal); prove it on a real Tankoban screenshot.
4. **Governance:** write the verbs table + the DeepSeek division-of-labor rule + the producer≠reviewer review reflex into the gov-v14 amendment / `project_engines_as_tools_gov_v14` memory so brothers actually reach for the right engine.

**Done =** Bug #1 fixed; DeepSeek `grunt`, Codex `review`, Gemini `read` + `see` each have one real green logged to the ledger; the lanes are in governance.

---

## Guardrails (inherited, unchanged)

Per-wake hard cap 25 / per-task soft 8 / 8K-char packets; the token ledger (`scripts/engines/.ledger.jsonl`); keys from `.env` only, never logged or committed; the Gemini text-gate stays as the reliability check before `enabled` flips true. Quota is a routing input (read the ledger), never a replacement argument — the Claude brothers (0-5) are never swapped out ([[feedback_brotherhood_is_not_swappable]]).
