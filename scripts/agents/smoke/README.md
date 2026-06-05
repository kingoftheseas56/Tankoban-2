# Stream/Theatre Smoke Harness

Autonomous on-device smoke for Agent 4's domain. Two-lane oracle:
SELF (tankoctl + logs, decided by the runner) and VISUAL (ffmpeg clips + the
Gemini LLM, couriered manually). Spec + plan:
`docs/superpowers/specs/2026-06-05-stream-theatre-autonomous-smoke-harness-design.md`,
`docs/superpowers/plans/2026-06-05-stream-theatre-smoke-harness.md`.

## Scope: plumbing, not watching
Normal smokes are journey-driven and short (~20–30 min total). Playback is
**spot-checks** at a few positions (first frame painted, audio out, subs rendered,
a mid-seek re-paints, the end is reached) — seconds each, never a playthrough.
Whether the video *looks/feels good* is Hemanth's taste call, out of scope.

## Run (two phases — the visual lane is a manual courier to the Gemini LLM)
Phase A (autonomous): drive + record + SELF judging + clip the flagged windows.
1. Launch in drive mode **in-process / visible** (never detached-hidden — that
   silently kills ddagrab, per Agent 0's recorder fix `c6f59d2`):
   `scripts\agents\run_drive_mode.bat`.
2. `python scripts\agents\smoke\run_smoke.py --session <name> --only J1,J2`
   → `out\smoke_<name>_findings.md` (SELF) + `..._clips\*.mp4` + `..._visual_workorder.md`.

Phase B (visual, manual): courier the clips to the Gemini LLM.
3. (optional) speed a long clip: `python scripts\agents\screen_record.py speedup <clip> <out> 4`.
4. Upload the clips + work-order to the Gemini LLM; save its JSON reply to
   `out\smoke_<name>_visual_answers.json`.
5. `python scripts\agents\smoke\fold_visual.py --session <name>` → final findings,
   every one traceable (action → ts → video offset → gemini → log line).

No vision API / no API key. `--no-visual` skips clipping (SELF still runs fully).

## Soak mode + speed levers (opt-in, not the default)
An occasional **soak run** catches time-dependent bugs (the idle-spin crash that
fires "minutes after open," memory growth, stutter-over-time): loop a journey /
hold playback for N minutes under the same recorder + watchdog. Only soak uses
Agent 0's speed levers (runbook §3c). The default smoke plays through nothing.

## Extend / reuse (Agents 1/2/3)
Author `catalogue_<domain>.json` (same shape as `catalogue_stream.json`) and run
with `--catalogue`. Runner/recording/visual/findings are domain-agnostic.

## Honest blind spot
During a hard freeze `introspect-*` is dead too (GUI-thread-bound). The harness
detects the freeze (watchdog + ping timeout + frozen frame) and reports it
`blocked`, but the internal call-stack needs OBS-4 (out-of-process dump, not
built) — flagged `stack_available:false`, never faked.
