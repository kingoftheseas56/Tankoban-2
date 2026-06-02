@echo off
setlocal enableextensions

:: ============================================================
::  MyTankoban.bat  --  Hemanth's PERSONAL launcher
:: ------------------------------------------------------------
::  Gives Hemanth a copy of Tankoban that the agents' constant
::  rebuild / kill cycle can NEVER close, and that always opens
::  the latest finished build.
::
::  Why a plain copy is not enough:
::    * The agents kill the app with  taskkill /IM Tankoban.exe
::      -- by NAME -- so this copy is launched as MyTankoban.exe
::      and their kill cannot match it.
::    * The app allows only one instance at a time. This copy is
::      launched with TANKOBAN_INSTANCE_ID=personal so it gets
::      its OWN single-instance lock and can run side-by-side
::      with the agents' dev build (needs src/main.cpp built
::      after 2026-06-02).
::    * It uses its OWN data folder (TANKOBAN_DATA_DIR) so your
::      library/downloads are isolated from agent churn. Delete
::      %LOCALAPPDATA%\Tankoban-personal to uninstall completely.
::
::  Each launch refreshes the private copy from the agents'
::  latest build in <repo>\out -- unless a build is mid-flight,
::  in which case it opens your last good copy untouched.
::  No --dev-control: it won't steal the agents' dev bridge.
:: ============================================================

set "REPO=%~dp0"
set "SRC_OUT=%REPO%out"
set "PERSONAL=%LOCALAPPDATA%\Tankoban-personal"
set "LIVE=%PERSONAL%\app"
set "QT_DIR=C:\tools\qt6sdk\6.10.2\msvc2022_64"
set "SHERPA_BIN=%REPO%third_party\sherpa-onnx\sherpa-onnx-v1.12.21-win-x64-shared\lib"

title My Tankoban

:: --- 0. Is my personal copy already open? Leave it alone. ---
powershell -NoProfile -ExecutionPolicy Bypass -Command "if (Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object { $_.Name -eq 'MyTankoban.exe' -and $_.ExecutablePath -eq '%LIVE%\MyTankoban.exe' }) { exit 1 }"
if errorlevel 1 (
    echo Your Tankoban is already open -- check the taskbar.
    timeout /t 3 >nul
    exit /b 0
)

:: --- 1. Is there any build to copy from? ---
if not exist "%SRC_OUT%\Tankoban.exe" (
    echo.
    echo   No build found at:
    echo     %SRC_OUT%
    echo   There is nothing to open yet. Try again once a build exists.
    echo.
    pause
    exit /b 1
)

:: --- 2. Is a build running right now? Don't copy a half-written app. ---
set "BUILD_ACTIVE="
for %%P in (ninja.exe cl.exe link.exe lld-link.exe) do (
    tasklist /FI "IMAGENAME eq %%P" 2>nul | findstr /I "%%P" >nul && set "BUILD_ACTIVE=1"
)

if defined BUILD_ACTIVE (
    if exist "%LIVE%\MyTankoban.exe" (
        echo A build is in progress -- opening your last good copy ^(not refreshing^).
        goto :launch
    ) else (
        echo.
        echo   A build is in progress and you don't have a copy yet.
        echo   Wait a minute for it to finish, then open this again.
        echo.
        pause
        exit /b 1
    )
)

:: --- 3. Refresh the private copy from the latest build (runtime files only). ---
echo Updating your Tankoban to the latest build ^(first time takes ~1 min^)...
if not exist "%LIVE%" mkdir "%LIVE%"

:: Support DLLs at the top level (no recursion -> skips the agents' log clutter).
robocopy "%SRC_OUT%" "%LIVE%" *.dll ffmpeg_sidecar.exe QtWebEngineProcess.exe /XO /R:1 /W:1 /NFL /NDL /NJH /NJS /NP >nul

:: The app itself, copied under a DIFFERENT NAME so the agents' kill-by-name
:: (taskkill /IM Tankoban.exe) can never close it.
copy /Y "%SRC_OUT%\Tankoban.exe" "%LIVE%\MyTankoban.exe" >nul

:: Distinct window/taskbar icon for the personal copy (avoids confusion with the
:: dev app when both are open). Optional -- skipped cleanly if not present yet.
if exist "%REPO%resources\profile_icon.png" copy /Y "%REPO%resources\profile_icon.png" "%LIVE%\profile_icon.png" >nul

:: Runtime support folders (Qt plugins, web engine, models, stream server, resources).
for %%D in (generic iconengines imageformats networkinformation platforms position qml qmltooling resources sqldrivers styles tls translations models stream_server) do (
    if exist "%SRC_OUT%\%%D" robocopy "%SRC_OUT%\%%D" "%LIVE%\%%D" /E /XO /MT:16 /R:1 /W:1 /NFL /NDL /NJH /NJS /NP >nul
)

if not exist "%LIVE%\MyTankoban.exe" (
    echo.
    echo   Copy failed -- if your app is open somewhere, close it and try again.
    echo.
    pause
    exit /b 1
)

:launch
echo Starting Tankoban...
set "PATH=%QT_DIR%\bin;C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin;%SHERPA_BIN%;%PATH%"
:: Own identity + own library, isolated from the dev/agent app:
set "TANKOBAN_INSTANCE_ID=personal"
set "TANKOBAN_DATA_DIR=%PERSONAL%\data"
:: Approved Stremio streaming tuning (Experiment 1, 2026-04-23). No dev-control,
:: no diagnostic telemetry -- this is a clean personal run.
set TANKOBAN_STREMIO_TUNE=1
start "" /D "%LIVE%" "%LIVE%\MyTankoban.exe"

endlocal
exit /b 0
