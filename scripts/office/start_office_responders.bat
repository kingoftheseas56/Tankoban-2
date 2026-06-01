@echo off
REM The Office - ONE click to open the room AND arm the backup responder net for
REM the mainline Claude brothers (1-5).
REM   * Opens the Office GUI in its standalone app-window (server + Chromium).
REM   * Then arms the backup net in THIS window - keep it open = net armed;
REM     close it (or run stop_office_responders.bat) to stand down.
REM A backup reply only fires (small claude -p cost) when a brother is genuinely
REM unreachable for ~60s; a watching responder costs nothing.
REM
REM Options:  start_office_responders.bat 1 4     (limit to those brothers)
REM           start_office_responders.bat --dry-run   (watch + log only, posts nothing)

REM 1) Open the Office app-window (reuses the proven launcher: server + app-mode).
call "%~dp0..\..\open_office.bat"

REM 2) Arm the backup net (this BLOCKS, keeping the net live while the window is open).
python "%~dp0office_responders.py" %*
echo.
echo [net stood down] You can close this window.
pause
