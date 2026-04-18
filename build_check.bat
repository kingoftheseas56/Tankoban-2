@echo off
setlocal enabledelayedexpansion

:: ── build_check.bat ─────────────────────────────────────────────────────────
:: Agent-safe compile verification for the Tankoban main app. Wraps
:: `cmake --build out/ --target Tankoban` behind a grep-friendly status line.
:: Does NOT configure, does NOT launch the exe, does NOT spawn GUI.
::
:: Agents: run after editing a .cpp to verify "did I break the compile?" in
:: ~30-90s. On success, prints `BUILD OK` (exit 0). On failure, prints last
:: 30 lines of the cl.exe diagnostic (exit propagated from cmake/ninja).
::
:: Full log at %BUILD_DIR%\_build_check.log for post-hoc diagnosis.
:: ────────────────────────────────────────────────────────────────────────────

set "BUILD_DIR=%~dp0out"
set "LOG=%BUILD_DIR%\_build_check.log"

:: Guard 1 — build tree must already be configured by build_and_run.bat
if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo BUILD CHECK: out\ not configured. Run build_and_run.bat first.
    exit /b 2
)

:: Guard 2 — MSVC env (same vcvarsall as build_and_run.bat:6)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
if errorlevel 1 (
    echo BUILD CHECK: MSVC env setup failed.
    exit /b 3
)

:: Compile + link Tankoban target only. Redirect combined streams to log.
cmake --build "%BUILD_DIR%" --config Release --target Tankoban > "%LOG%" 2>&1
set BUILD_EXIT=%ERRORLEVEL%

if %BUILD_EXIT% EQU 0 (
    echo BUILD OK
    exit /b 0
)

echo BUILD FAILED exit=%BUILD_EXIT%
echo --- last 30 lines of %LOG% ---
powershell -NoProfile -Command "Get-Content -Tail 30 -LiteralPath '%LOG%'"
exit /b %BUILD_EXIT%
