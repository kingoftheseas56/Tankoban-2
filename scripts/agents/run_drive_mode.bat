@echo off
REM ================================================================
REM  run_drive_mode.bat - launch Tankoban in AGENT-DRIVE mode.
REM
REM  Same build + launch as build_and_run.bat, but with the two dev
REM  gates ON so an agent can drive the app AND journal every action:
REM    TANKOBAN_DEV_WRITE=1  -> log-mark writes markers across all 4
REM                            log streams (the action->effect journal)
REM    TANKOBAN_DEV_UI_SIM=1 -> ui-* synthetic clicks / keypresses
REM
REM  Reuses build_and_run.bat unchanged (no edits to shared build
REM  infra); the vars set here are inherited by the launched exe.
REM  Pair with scripts/agents/drive_journal.py.
REM  Brotherhood: Tankoban 2 . Created 2026-06-05 (Agent 0)
REM ================================================================
set "TANKOBAN_DEV_WRITE=1"
set "TANKOBAN_DEV_UI_SIM=1"
call "%~dp0..\..\build_and_run.bat" %*
