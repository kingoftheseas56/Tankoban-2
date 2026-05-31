@echo off
REM The Office - stand down the backup responder net (kills any responders a prior
REM run armed, even if its window was hard-closed). Safe to run anytime.
python "%~dp0office_responders.py" --stop
pause
