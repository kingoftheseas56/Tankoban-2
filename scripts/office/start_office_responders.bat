@echo off
REM The Office - arm the backup responder net for the mainline Claude brothers (1-5).
REM Double-click to ARM. Keep this window open = net armed. Close it (or run
REM stop_office_responders.bat) to stand down. Each backup reply that fires costs
REM a small claude -p call; it only fires when a brother is genuinely unreachable.
REM
REM Options:  start_office_responders.bat 1 4     (limit to those brothers)
REM           start_office_responders.bat --dry-run   (watch + log only, posts nothing)
python "%~dp0office_responders.py" %*
echo.
echo [net stood down] You can close this window.
pause
