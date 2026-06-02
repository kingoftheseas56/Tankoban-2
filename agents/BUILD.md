# Tankoban 2 — Build & Tooling Reference

> Full command list. Pointed to from the CLAUDE.md kernel. Stable reference.


- Main app (Release + asset deploy + run): `build_and_run.bat`
- Main app (Debug, MSVC2022 + Qt6.10.2): `build2.bat`
- Main app (compile-only, agent-safe): `build_check.bat` — `BUILD OK` / `BUILD FAILED exit=<n>` + 30-line cl.exe tail, no exe run, no GUI spawn.
- Sidecar (MinGW, installs to `resources/ffmpeg_sidecar/`): `powershell -File native_sidecar/build.ps1`
- Main-app tests (opt in with `-DTANKOBAN_BUILD_TESTS=ON`): `cmake --build out --target tankoban_tests && cd out && ctest --output-on-failure -R tankoban_tests`
- Drift audit (tracked junk / large files / chat.md rotation / STATUS drift / RTC backlog / CONGRESS/HELP): `/repo-health`
- Post-smoke log-scan (process state / telemetry / PERF / error-line scan): `powershell -NoProfile -File scripts/runtime-health.ps1`
- Smoke cleanup (Rule 17 — kill Tankoban + ffmpeg_sidecar): `powershell -NoProfile -File scripts/stop-tankoban.ps1`
- Mpv-vs-Tankoban log-diff harness: `powershell -NoProfile -File scripts/compare-mpv-tanko.ps1 -MpvLog <path> -SidecarLog <path>` — verdict=CONVERGED/DIVERGED-WORSE on drops + stalls.
- IPC round-trip latency: automatic on every Tankoban run; per-session block appended to `out/ipc_latency.log` with `cmd=<name> count p50 p99 max` rows.
- UIA enumeration (Qt 100% AutomationId via `objectName()`): `powershell -NoProfile -File scripts/uia-dump.ps1 [-MaxDepth 6] [-TargetClass StreamPage]`
- Dev-control bridge client (primary for app-state queries): `out\tankoctl.exe <subcommand>` — see HEMANTH'S ROLE § Tool priority above for the schema + gates + standing flags. Full catalog: `out\tankoctl.exe ping`. Memory `project_dev_control_bridge.md` for ship history + extension procedure.
- **Multi-engine brother helper — OPTIONAL, NOT MANDATORY (de-mandated 2026-06-02).** The 2026-06-01 "default working mode for every brother" rule is **REVOKED** — no brother is required to route work through `scripts/engines/engine.py`. It remains an *available* experimental tool you MAY use for your own lane (grunt→DeepSeek, review→Codex, search→Gemini), but it is not required and not a default. The whole multi-engine approach is **under reconsideration** — likely superseded by a "mini-congress" group-conversation medium (research done 2026-06-02; brainstorm pending). Until that lands, work as you normally would; reach for the helper only if it genuinely helps. Spec/research: `docs/superpowers/{specs,plans}/2026-06-01-multi-engine-brother*`. Memory: `project_multi_engine_brother_shipped`.
- **Always:** `taskkill //F //IM Tankoban.exe` before any rebuild (Rule 1); `scripts/stop-tankoban.ps1` after any agent-driven smoke (Rule 17); claim the appropriate lane lease (`out\tankoctl.exe lease-acquire mcp|build|shared-file:<path>`) before desktop / build / file-shared work per Rules 19 + 22 (gov-v7).
