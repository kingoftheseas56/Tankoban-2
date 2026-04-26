@echo off
:: LIBPLACEBO_SINGLE_RENDERER_FIX P2 smoke launcher (2026-04-26).
:: Sets TANKOBAN_LIBPLACEBO_SDR=1 then delegates to build_and_run.bat. The
:: env var inherits to the Tankoban.exe child process before build_and_run's
:: setlocal/endlocal scope kicks in. To verify env-OFF baseline behavior,
:: launch the regular build_and_run.bat instead. After P3 (env-gate dropped
:: from main.cpp), this wrapper gets deleted.
set TANKOBAN_LIBPLACEBO_SDR=1
call "%~dp0build_and_run.bat"
