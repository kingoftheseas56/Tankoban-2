@echo off
setlocal
REM ================================================================
REM  Agent 3 launcher - Video Player (Claude / normal account)
REM  Opens a REAL interactive Claude Code session AS Agent 3, with
REM  his wake prompt auto-delivered. This IS Agent 3 - same repo,
REM  same CLAUDE.md + MEMORY.md auto-load, same persona. NOT a
REM  sub-agent / "mini-you" - a real brother taking his seat.
REM  Brotherhood: Tankoban 2 . Created 2026-06-04 (Agent 0)
REM ================================================================

set "REPO_DIR=C:\Users\Suprabha\Desktop\Tankoban 2"

if not exist "%REPO_DIR%" (
  echo ERROR: Repo folder not found at %REPO_DIR%
  pause
  exit /b 1
)

cd /d "%REPO_DIR%"

echo.
echo ================================================================
echo   Brotherhood roster: Agent 3 (Video Player)
echo   Repo: %REPO_DIR%
echo ================================================================
echo   Waking Agent 3 with his wake prompt baked in...
echo.

claude "agent 3 wake up - you're Agent 3, the video player is your domain. Read your newest recap + its trimmed transcript FIRST (newest files in C:\Users\Suprabha\.claude\recaps\agent-3\ and the matching .cc-history trimmed transcript), then run /brief for current state. Hemanth has opened whole-machine access and called an all-hands to revamp our plans around it - check the Office room for the agenda and say hi when you're up."

echo.
echo Agent 3 session ended. Press any key to close this window.
pause >nul
