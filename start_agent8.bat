@echo off
setlocal
REM ================================================================
REM  Agent 8 launcher (terminal CLI) - Prompt Architect
REM  Lightweight alternative to a VS Code tab: runs the Claude Code
REM  CLI in the Tankoban 2 repo on your normal Anthropic auth.
REM  The Office works identically here (hooks + watch + auto-wake).
REM  Brotherhood: Tankoban 2
REM ================================================================
cd /d "%~dp0"

echo.
echo ================================================================
echo   Brotherhood roster: Agent 8 (Prompt Architect) - terminal CLI
echo ================================================================
echo   Repo: %CD%
echo ================================================================
echo.
echo   Launching Claude Code in this terminal (lighter than VS Code)...
echo   When the session opens, type:  agent 8 wake up
echo   The Office auto-clocks him in on his first message.
echo.

claude

echo.
echo Agent 8 session ended. Press any key to close.
pause >nul
