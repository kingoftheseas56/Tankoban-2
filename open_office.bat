@echo off
:: The Office — one-click launcher. Serves the local bus GUI and opens it in its
:: OWN standalone Chromium app-window (no tabs / no address bar), falling back to
:: the default browser if neither Edge nor Chrome is found.
cd /d "%~dp0"
set "OFFICE_URL=http://127.0.0.1:8787"

:: Start the server in its own console window (so this script can launch the UI).
start "The Office (server)" cmd /c "python scripts\office\office_web.py 8787"

:: Give the server a moment to bind the port before the window loads.
ping -n 2 127.0.0.1 >nul

:: Prefer Edge (ships on every Win11), then Chrome, then default browser.
set "EDGE=%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe"
set "CHROME=%ProgramFiles%\Google\Chrome\Application\chrome.exe"
if exist "%EDGE%" (
  start "" "%EDGE%" --app=%OFFICE_URL%
) else if exist "%CHROME%" (
  start "" "%CHROME%" --app=%OFFICE_URL%
) else (
  start "" %OFFICE_URL%
)
