# Spec — The Self-Contained Multi-Engine Brother

**Status:** DRAFT (spec) · **Author:** Agent 0 (Opus) · **Date:** 2026-06-01
**Arc:** Office Slice 2 (reframed) — "self-contained multi-engine brother" replaces "owned SDK-worker"
**Predecessor reading:** `brother-agent-0-2026-06-01-braided-current.md` (recap) + `…tempered-foundry.md` + the trimmed transcript `2026-06-01_170256_…_c8a73dfa.trimmed.md`
**Feasibility:** PROVEN LIVE 2026-06-01 ~6:30pm — all 4 engines called headless by Claude, end-to-end handoff, no fork. See §7.

---

## 1. The one-sentence shape

Each brother stops being "a single Claude tab" and becomes **one Claude brain (the brother's identity + memory) that wields up to three other engines as bare tools on his own lane** — DeepSeek for grunt, Codex for sign-off review, Gemini 2.5 Flash (conditional) for reading big things — self-routing his own work across them so the cheap engines eat the volume and the expensive Opus fuel only burns for thinking.

## 2. The problem this solves (in Hemanth's words)

- *"I completely want to get rid of talking to each agent individually."* — the emotional driver.
- The brotherhood's standing **Engine Switching Protocol** (route grunt→DeepSeek, review→Codex, design→Claude) is done **by hand, across tabs, by Agent 0 every wake.** This spec **internalizes that routing into the brother himself**, so the human coordination disappears for a brother's own-lane work.
- Wins three ways at once: (a) kills the per-engine coordination Hemanth is sick of; (b) bakes in quota discipline *by design* — DeepSeek/Gemini absorb volume automatically, not because someone remembered; (c) plays each engine to its proven strength.

## 3. What this is NOT (scope fence)

This is the **bounded, high-value, low-risk brick** — deliberately the smaller of the two hard problems. It explicitly does **not** include:

- ❌ **No fork / no new app / no IDE.** The engines are command-line tools the existing `claude` brain already shells out to. Proven (§7).
- ❌ **No idle-brother wake / reachability.** Making a dark tab answer on command is a *later* layer. This spec touches only a brother's own active work.
- ❌ **No auto-orchestration of a crew.** The "drop-a-screenshot, Agent 4 auto-leads, crew self-organizes" dream stacks on top *later* (Bricks 2–3). Not here.
- ❌ **No change to cross-lane collaboration.** Brothers still help each other through chat.md / the Office; Hemanth mediates; **a brother who jumps in to assist comes as plain Claude only** (he's helping, not running a mission). The multi-engine rig fires *only* on a brother's own-lane work.

## 4. The engine roster + locked roles

**The whole system in four words (Hemanth's framing, canonical):**
> **Claude = the person. Codex = the brain. DeepSeek = the muscle. Gemini = the memory.**

The person has the identity and makes the calls; the brain reasons, debugs, and keeps it on track; the muscle does the heavy lifting; the memory holds and recalls the huge context so the others don't have to carry it.

| Engine | Role | Why this engine | Quota posture |
|---|---|---|---|
| **Claude (Opus/Sonnet)** | **IS the brother** — memory, identity, judgment, planning, the important coding | Identity lives where the memory lives | Max plan; reserve for thinking + load-bearing code |
| **DeepSeek V4-Pro/Flash** | **Grunt** — file edits, bulk parsing, boilerplate, mechanical refactors | Proven workhorse (Agent 9 = days of it); ~50× cheaper than Opus | Dirt cheap — absorb the volume |
| **Codex (gpt-5.5)** | **The guardrail (most critical of the four)** — reviews load-bearing decisions/diffs (APPROVE/REJECT) AND does **systematic debugging + solution-finding (diagnosis)**. Does NOT implement — he keeps the workflow on track, others execute his findings. | Different-model eyes; orthogonal blind spots = the one engine that catches what an all-Claude/DeepSeek pipeline misses | Tight quota → use **deliberately at quality gates, not on trivia** — but he is the backbone, NOT a rationed luxury |
| **Gemini 2.5 Flash** | **The memory (conditional 4th)** — holds + recalls huge context: chew a big log/file/history, hand back only the relevant slice | Cheap + huge context window → frees the person's context (serves the quota lesson) | ~free; wire in only if it stays reliable |

### Governance line (settles the roster muddle)
- **Engine = the wire used as a bare tool.** Agent 4's Claude calls the DeepSeek/Codex/Gemini *wire*, no persona attached.
- **Brother = the wire + a persona running its own lane.** That's Agent 9 (DeepSeek-the-brother), Agent 7 (Codex-the-brother).
- **Same model, two hats.** Calling DeepSeek as an engine does not make Agent 9 present; summoning Agent 9 is a separate, persona-bearing act. One clean line, no roster confusion.

## 5. How the brother routes his own work

Claude (the brother) is the **planner and the router** — he self-dispatches. The decision rule he applies, per unit of work:

1. **Is it grunt?** (mechanical edit, bulk parse, boilerplate, well-templated refactor) → **DeepSeek.**
2. **Does a load-bearing decision/diff need a guardrail pass before it lands, OR am I stuck on a bug / hunting the right fix?** → **Codex** — he reviews AND debugs/diagnoses. This is the *quality checkpoint of the whole flow*, not an optional extra. Run it on anything load-bearing; skip it only on trivia.
3. **Do I need to read something huge before I can think?** (giant log, long file) → **Gemini** reader-extract, *if* available; else Claude reads it directly.
4. **Is it design / judgment / identity / the important code?** → **keep on Claude himself.**

**Division of labor at the bug-fix boundary:** Codex *finds* the solution (systematic debugging, root-cause, the right approach); DeepSeek or Claude *implements* it; Codex *reviews* the implementation. Codex is the brain that keeps the workflow on track — he diagnoses and signs off, he does not type the production code.

The brother decides because he is the one holding the plan and the memory. Routing is not a fixed pipeline — it's per-task judgment, the same judgment Agent 0 exercises today, now living inside the brother.

## 6. The hard part: the small-context handoff

This is the real engineering, and it's the **exact lesson that killed `claude -p` this morning** (the 120s timeout): an engine handed the whole bloated project context chokes or times out. The discipline:

- **Hand each engine the smallest packet that lets it do the job** — the function to write, the diff to review, the log to read — never the whole repo.
- **Each engine call is one-shot and stateless** — prompt in, answer out, fold back into Claude's working context. No engine holds session state across calls.
- **Claude owns the stitching** — decomposing the task into engine-sized packets and re-assembling the answers is the brother's job, not the engine's.
- **Structured output where it matters** — Codex `exec` already returns clean structured text; ask DeepSeek/Gemini for terse, parseable answers (code-block only, single value, etc.) so fold-back is mechanical.

## 7. Proven invocation recipes (from the live probe)

All three external engines were called headless by Claude on 2026-06-01, clean, first real try.

### DeepSeek (grunt) — `claude` CLI, endpoint swapped
```bash
# Run from a CLEAN scratch dir with an ISOLATED config so the giant project
# CLAUDE.md / .claude hooks are NOT injected (DeepSeek's endpoint rejects that).
(cd /tmp && mkdir -p engine_probe && cd engine_probe && \
  CLAUDE_CONFIG_DIR=/tmp/engine_probe/.cfg \
  ANTHROPIC_BASE_URL=https://api.deepseek.com/anthropic \
  ANTHROPIC_AUTH_TOKEN=<deepseek-key> \
  ANTHROPIC_API_KEY= \
  ANTHROPIC_MODEL=deepseek-v4-pro \
  ANTHROPIC_DEFAULT_OPUS_MODEL=deepseek-v4-pro \
  claude -p "<tiny task packet>" --model deepseek-v4-pro)
```
**Gotcha cleared:** isolated `CLAUDE_CONFIG_DIR` + scratch cwd avoids the mid-array `system`-content rejection. (Source recipe: `C:\Users\Suprabha\Desktop\start_agent9.bat` lines 9–17.)

### Codex (auditor) — `codex exec`, stdin closed
```bash
codex exec "<review prompt + the exact artifact to review>" </dev/null 2>&1
```
**Gotcha cleared:** `codex exec` blocks forever waiting on stdin unless you close it with `</dev/null`. Returns `APPROVE`/`REJECT` + reason, exit 0. (Model: gpt-5.5, ~15k tokens for a one-line review.)

### Gemini 2.5 Flash (reader) — REST
```bash
curl -s "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=<gemini-key>" \
  -H "Content-Type: application/json" \
  -d '{"contents":[{"parts":[{"text":"<reader prompt + blob>"}]}]}'
```
**No gotcha.** Clean JSON, 159 tokens for a fact-extract. No CLI needed; pure HTTP. (Gemini is the only engine with no "speaks-Anthropic" wire — hence REST, hence the "if reliable" conditional.)

### Proven handoff
DeepSeek *wrote* `int clampVolume(int v)` → Claude passed that exact output to Codex → Codex *reviewed it* and returned `APPROVE` with correct reasoning → Gemini independently extracted `3.2` from a log line → Claude stitched the verdict. Hemanth was not in the loop for any call. **The one untested seam — Claude pressing the engines' buttons itself — is closed.**

## 8. The hard cost-cap (mandatory, baked from day one)

A crew spending tokens unsupervised is exactly the quota bonfire Hemanth fears. The design **must** ship with a hard ceiling, not advisory:

- **Tiny context only** — enforce the small-packet rule mechanically (truncate/refuse oversized engine packets).
- **Fire limits** — a cap on engine calls per task/per wake; exceeding it stops and asks the brother to reconsider, not auto-spends.
- **Cheap-engine-first for non-critical** — grunt + reader default to DeepSeek/Gemini; Codex gated behind "load-bearing only"; Claude-Opus reserved for thinking.
- **Visible accounting** — each engine call logs `engine · tokens · purpose` so spend is auditable (mirror the `ipc_latency.log` / `log-mark` pattern).

## 9. Secrets / key hygiene

- DeepSeek key + Gemini key are **secrets**. They live in launcher env (`start_agent9.bat`) / a gitignored env file — **never** committed in tracked source or this spec.
- The probe's Gemini key was provided ad-hoc by Hemanth in chat and **will be rotated by him** — do not persist it anywhere tracked.
- Engine wrapper reads keys from environment only.

## 10. Resolved decisions (ratified by Hemanth 2026-06-01)

1. **First adopter → shared wrapper, Agent 4 pilots.** Build the engine-call helper **engine-agnostic from the start** (cost-cap + key hygiene written once), and **Agent 4 is the first brother to use it** on a real Theatre task. Reusable infra, proven on one lane before others opt in.
2. **Gemini → reader only, behind a reliability gate.** Wired strictly as the reader (chew big logs/files → hand back the slice). Ships in v1 **only if** it passes a small reliability bar in testing; if it flakes, v1 ships **3-engine** and Gemini lands later. Claude reads big files directly in the interim.
3. **Wrapper home → shared `scripts/engines/` helper.** One helper exposing `grunt()` / `review()` / `read()`, sourced by any brother. Cost-cap, key hygiene, and per-call logging written **exactly once** — single source of truth, no drift.
4. **Fire limit → moderate: ~25 calls/wake, soft ~8/task.** Hard per-wake ceiling of ~25 engine calls; soft sub-cap of ~8 per single task. Exceeding the hard cap **stops and asks** — never auto-spends. Mostly DeepSeek (dirt cheap); Codex gated to load-bearing reviews only.

## 11. Acceptance criteria (what "built" means)

- [ ] A shared engine-call helper exposes `grunt()` (DeepSeek), `review()` (Codex), `read()` (Gemini), each one-shot, each enforcing the small-packet rule.
- [ ] Each helper applies the §8 cost-cap + §9 key hygiene + logs `engine·tokens·purpose`.
- [ ] A brother (Agent 4 first) demonstrably completes one real own-lane task by self-routing: Claude plans → DeepSeek grunts → Codex signs off → Claude lands it, with cost under cap.
- [ ] The §3 fences hold: no fork, no idle-wake, no auto-crew, cross-lane collaboration unchanged.
- [ ] Reviewer pass (Codex or Agent 9) before merge to master, per Trigger-D discipline.

## 12. Next step after this spec locks

Spec → **plan** (`superpowers:writing-plans`, phased) → **build** routed to DeepSeek/Codex (fittingly, the thing we're building *is* the engine-routing layer). Engine stays on Opus through plan-lock, per the wake's engine note.
