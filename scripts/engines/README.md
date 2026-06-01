# scripts/engines — the multi-engine brother helper

One Claude brain (the brother), three engines on tap. Spec:
`docs/superpowers/specs/2026-06-01-multi-engine-brother-design.md`.

## Use it
Set your identity first:    $env:ENGINE_AGENT="agentN"   (per-brother cap isolation; defaults to "solo" if unset)
At wake start (once):       python scripts/engines/engine.py wake-start
Grunt (DeepSeek):           python scripts/engines/engine.py grunt  --task T1 --purpose "write fn" "<tiny packet>"
Review (Codex):             python scripts/engines/engine.py review --task T1 --purpose "sign off" "<diff + ask>"
Read  (Gemini, if enabled): python scripts/engines/engine.py read   --task T1 --purpose "extract"  "<blob + ask>"
Status / spend:             python scripts/engines/engine.py status

## The routing rule (the brother decides, per task)
- grunt  -> mechanical edit / bulk parse / boilerplate            -> DeepSeek
- review -> a load-bearing decision/diff needs second eyes (rare) -> Codex
- read   -> chew a big log/file, hand back the slice              -> Gemini (if enabled)
- think  -> design / judgment / identity / important code         -> keep on Claude

## Contract
- Keys come from env OR a gitignored `scripts/engines/.env` (auto-loaded; real env wins). Never commit them.
- Packets must be tiny (small-context rule); oversized packets are refused.
- Hard cap ~25 calls/wake, soft ~8/task. Hitting the hard cap STOPS and asks.

## Writing good packets (the muscle has NO project context — by design)
The bare grunt/read engines run context-starved (clean scratch dir, no CLAUDE.md)
to stay cheap and fast. So the packet must be **self-contained**. Lessons from live use:
- Plain prose, not code-pseudo. Describe the behavior step by step.
- Avoid loaded trigger words ("classifier", "ML") that pull the engine off-task.
- Spell out the exact output you want ("output ONLY the function in a code block").
- One small job per call. Need a bit of context? Paste the ONE relevant snippet —
  never the whole file/repo (that's the slow timeout path).
- Always verify what comes back. DeepSeek can diverge on fuzzy packets; the verify
  gate is what catches it before it touches code.

## Live smoke 2026-06-01

DeepSeek grunt: produced correct `clampVolume` (ternary with correct bounds).
Codex review: `APPROVE: Correctly clamps values below 0 to 0, above 100 to 100, and preserves in-range values.`
No key leakage in ledger. Both calls logged, 2/25 wake cap.
Platform fix: `claude.cmd` / `codex.cmd` on Windows (Python subprocess requires .cmd extension).
