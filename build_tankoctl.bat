@echo off
:: One-off: rebuild tankoctl client after tools/tankoctl.cpp edits.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
if errorlevel 1 (
    echo MSVC env setup failed.
    exit /b 3
)
cmake --build "%~dp0out" --config Release --target tankoctl 2>&1 | findstr /v /c:"NSIS" /c:"Cached"
exit /b %ERRORLEVEL%
