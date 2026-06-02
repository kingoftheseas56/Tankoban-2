@echo off
:: The Office — one-click launcher. Serves the local bus GUI and opens it in its
:: OWN standalone Chromium app-window (no tabs / no address bar), falling back to
:: the default browser if neither Edge nor Chrome is found.
cd /d "%~dp0"
set "OFFICE_URL=http://127.0.0.1:8787"

:: Self-cleaning: stop any already-running Office server(s) + dispatcher first, so we
:: never pile up duplicate processes and a fresh start always loads current code.
:: Targets ONLY office_web.py / office_dispatch.py python procs (not other tooling).
powershell -NoProfile -Command "Get-CimInstance Win32_Process | Where-Object { $_.Name -eq 'python.exe' -and ($_.CommandLine -like '*office_web.py*' -or $_.CommandLine -like '*office_dispatch.py*') } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }"

:: Let the OS release the port before we re-bind it.
ping -n 2 127.0.0.1 >nul

:: Start the server in its own console window (so this script can launch the UI).
start "The Office (server)" cmd /c "python scripts\office\office_web.py 8787"

:: Start the summon dispatcher (mini-congress reachability, 2026-06-02): watches the
:: bus for kind="summon" and, when the target's tab is closed/idle, spins him up as a
:: BACKGROUND session to do the work and post back (tight leash: posts, never merges).
start "The Office (dispatcher)" cmd /c "python scripts\office\office_dispatch.py"

:: Give the server a moment to bind the port before the window loads.
ping -n 2 127.0.0.1 >nul

:: Open in a STANDALONE app-window. A dedicated --user-data-dir is REQUIRED:
:: without it, if Edge/Chrome is already running, the launch hands off to the
:: existing instance and ignores --app (you get a blank/normal window). The
:: dedicated profile forces a fresh instance that honours app-mode, and it
:: persists window size/position across launches.
set "EDGE=%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe"
set "CHROME=%ProgramFiles%\Google\Chrome\Application\chrome.exe"
set "OFFICE_PROFILE=%LOCALAPPDATA%\TheOffice\chromium"
if exist "%EDGE%" (
  start "" "%EDGE%" --app=%OFFICE_URL% --user-data-dir="%OFFICE_PROFILE%" --no-first-run --no-default-browser-check
) else if exist "%CHROME%" (
  start "" "%CHROME%" --app=%OFFICE_URL% --user-data-dir="%OFFICE_PROFILE%" --no-first-run --no-default-browser-check
) else (
  start "" %OFFICE_URL%
)
