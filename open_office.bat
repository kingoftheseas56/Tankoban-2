@echo off
:: The Office — one-click GUI launcher (Hemanth's window into the live agent bus).
:: Starts the local web server and opens it in the default browser.
:: Close the console window (or Ctrl+C) to stop the server.
cd /d "%~dp0"
start "" http://127.0.0.1:8787
python scripts\office\office_web.py 8787
