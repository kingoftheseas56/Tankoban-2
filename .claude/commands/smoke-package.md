---
description: Scaffold a smoke evidence bundle (PNGs + logs + verdict markdown). Use when delivering smoke results for an audit or fix-TODO closeout.
---

You are scaffolding a smoke evidence bundle for Tankoban 2.

**Arguments:**
- `<finding-name>` — required, UPPERCASE_SNAKE_CASE identifier (e.g. `THEATRE_PACK_DISPATCH_RACE`)

**Procedure:**

1. **Compute timestamp:** `HHMMSS` in local time, e.g. `194523`.

2. **Create the evidence directory:**
   ```
   agents/audits/smoke_evidence/<FINDING>_<HHMMSS>/
   ```
   Use `mkdir -p`. If directory already exists, append a numeric suffix `_2`, `_3`, etc.

3. **Capture context logs (if Tankoban is running):**
   - `out/sidecar_debug_live.log` (last 500 lines) → `<dir>/sidecar.log`
   - `out/stream_telemetry.log` (last 200 lines) → `<dir>/telemetry.log`
   - `out/events.jsonl` (last 200 lines, if present) → `<dir>/events.jsonl`
   - Current Tankoban window screenshot via pywinauto-mcp → `<dir>/initial_state.png` (use ScreenCapture or automation_visual)
   Skip any source that doesn't exist; warn but don't fail.

4. **Write the stub evidence-md:**
   ```
   <dir>/evidence_<FINDING>_<HHMMSS>.md
   ```
   With this skeleton:
   ```markdown
   # Evidence: <FINDING>

   **Captured:** <ISO timestamp>
   **Agent:** <auto-detect from session>
   **Smoke scope:** <one-line description of what was being smoked>

   ## Smoke description

   <fill in: what was the agent trying to verify or reproduce?>

   ## Pre-smoke state

   - Tankoban running: <yes/no>
   - Branch: $(git rev-parse --abbrev-ref HEAD)
   - HEAD: $(git rev-parse --short HEAD)
   - Working tree: $(git status --short | wc -l) dirty files

   ## Observations

   <fill in: what happened during the smoke? include MCP screenshot pointers + log timestamps>

   ## Verdict

   - [ ] PASS / FAIL / INCONCLUSIVE
   - Notes:

   ## Evidence files in this directory

   - sidecar.log (if captured)
   - telemetry.log (if captured)
   - events.jsonl (if captured)
   - initial_state.png (if captured)
   - <numbered PNGs from smoke iteration>
   ```

5. **Print the directory path to stdout** so the agent can reference it in subsequent screenshots:
   ```
   Smoke evidence bundle ready: agents/audits/smoke_evidence/<FINDING>_<HHMMSS>/
   Append screenshots as <FINDING>_<HHMMSS>_<NN>.png to this directory.
   ```

**Quality gates:**
- Directory name uses UPPERCASE_FINDING per convention
- All captured logs exist OR have a per-source warning explaining absence
- Stub evidence-md has populated pre-smoke state from real git/system queries
- No silent failures: every skipped step prints a one-line reason

**Example:**

For `/smoke-package THEATRE_PACK_DISPATCH_RACE`:
```
Smoke evidence bundle ready: agents/audits/smoke_evidence/THEATRE_PACK_DISPATCH_RACE_194523/
Captured: sidecar.log (483 lines), telemetry.log (180 lines), initial_state.png
Append screenshots as THEATRE_PACK_DISPATCH_RACE_194523_NN.png to this directory.
```
