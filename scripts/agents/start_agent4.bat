@echo off
setlocal
REM ================================================================
REM  Agent 4 launcher - Stream mode + Tankorent
REM  Opens a REAL interactive Claude Code session AS Agent 4, with
REM  his wake prompt auto-delivered. This IS Agent 4 - same repo,
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
echo   Brotherhood roster: Agent 4 (Stream mode + Tankorent)
echo   Repo: %REPO_DIR%
echo ================================================================
echo   Waking Agent 4 with his wake prompt baked in...
echo.

claude "agent 4 wake up - you're Agent 4, stream mode + Tankorent are your domain. Read your newest recap + its trimmed transcript FIRST (newest files in C:\Users\Suprabha\.claude\recaps\agent-4\ and the matching .cc-history trimmed transcript), then run /brief for current state. Hemanth has opened whole-machine access and called an all-hands to revamp our plans around it - your angle is the rqbit sources/streaming reboot as a major new load the flight recorder must watch, the two built aware of each other. Check the Office room for the agenda and say hi when you're up."

echo.
echo Agent 4 session ended. Press any key to close this window.
pause >nul
