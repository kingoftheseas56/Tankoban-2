---
description: Opt-in build runner — main app or sidecar. Tail-captures last 30 lines on failure.
allowed-tools: Bash, Read
argument-hint: "[main|sidecar|both]"
---

You are running the Tankoban 2 build verifier. This is the manual replacement for an aggressive auto-rebuild hook (which Hemanth rejected as too costly per turn). Agents call this when they think they're done with a code-touching batch.

**Argument:** $ARGUMENTS — `main` (default), `sidecar`, or `both`.

**Procedure:**

1. **Pre-build hygiene.** Run `taskkill //F //IM Tankoban.exe` (per Build Rule 1). Ignore "process not found" — that just means the app isn't running.

2. **Branch on argument:**

   - **`main`** (default if no arg given): run
     ```
     cmake --build out --parallel --target Tankoban
     ```
     Capture stdout + stderr. On non-zero exit: print the last 30 lines of output and report `FAIL: main app build`. On zero exit: report `OK: main app build`.

   - **`sidecar`**: run
     ```
     powershell -File native_sidecar/build.ps1
     ```
     Same fail/ok handling.

   - **`both`**: run sidecar first (it's faster + isolated). If sidecar fails, abort before running main. If sidecar OK, run main.

3. **Report.** Print one of:
   ```
   /build-verify <arg> — OK (build green, <duration>s)
   /build-verify <arg> — FAIL (last 30 lines of output below)
   <output tail>
   ```

4. **Do NOT auto-commit anything.** This is verification only. If the build is green, the agent who invoked this still needs to post their `READY TO COMMIT` line in chat.md per Rule 11.

**Constraints:**
- Honor-system replacement — agents may forget to call this. That's acceptable per Hemanth's design (aggressive auto-rebuild was rejected).
- Do not edit any files, do not modify chat.md or STATUS.md.
- Long builds (multi-minute) are fine — the user invoked this knowing the cost.
- If neither `out/` nor `native_sidecar/build/` exists, report that clearly — initial setup needs `cmake -S . -B out` (main) or `native_sidecar/build.ps1` first-run scaffolding (sidecar).
