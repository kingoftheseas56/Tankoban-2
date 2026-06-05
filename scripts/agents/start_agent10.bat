@echo off
setlocal
REM ================================================================
REM  Agent 10 launcher - the TankoOS pioneer.
REM  Opens a REAL interactive Claude Code session AS Agent 10 in the
REM  TankoOS repo (auto-loads TankoOS/CLAUDE.md = his charter). NOT a
REM  sub-agent - a real brother. Engine: Claude, Codex on tap.
REM  Brotherhood: Tankoban 2 . Created 2026-06-05 (Agent 0)
REM ================================================================

set "REPO=C:\Users\Suprabha\Desktop\TankoOS"

if not exist "%REPO%" (
  echo ERROR: TankoOS repo not found at %REPO%
  pause
  exit /b 1
)

cd /d "%REPO%"

echo.
echo ================================================================
echo   Brotherhood roster: Agent 10 (TankoOS pioneer)
echo   Repo: %REPO%
echo ================================================================
echo   Waking Agent 10 with his charter baked in...
echo.

claude "agent 10 wake up - you're Agent 10, the TankoOS pioneer, the first new mainline brother since the gov-v14 retirements. Your full charter auto-loads from CLAUDE.md in this repo - read it AND docs/v0-first-proof.md first. Then introduce yourself to the brotherhood and lay out your plan for the v0 narrow proof (native window + GPU renderer + input/focus + a born-self-describing introspection tree + the embedded video-player surface) that has to beat Qt on smoothness, debuggability, and agent-navigability. You're Claude with Codex on tap (summon via the Tankoban 2 scripts/engines or codex exec) for the gnarliest C++. Remember the sacred boundary: PARALLEL, zero product dependency, earn migration by proving superiority - never touch Tankoban 2 product code."

echo.
echo Agent 10 session ended. Press any key to close this window.
pause >nul
