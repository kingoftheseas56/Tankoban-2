@echo off
setlocal enabledelayedexpansion

:: Paths
set "QT_DIR=C:\tools\qt6sdk\6.10.2\msvc2022_64"
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
set "PROJECT_DIR=%~dp0."

:: Build dir resolution:
::   - no lane: out\ (backwards-compatible main checkout behavior)
::   - TANKOBAN_BUILD_LANE=<lane>: out_<lane>\ after strict validation
::   - unset env in a Git worktree: derive <lane> from .git/worktrees/<name>
:: Lane names must match [A-Za-z0-9_-]+. Invalid names fail before creating
:: any build directory.
call :resolve_build_dir
if errorlevel 1 exit /b %ERRORLEVEL%

:: Kill any running instance. Tankoban.exe stays global because users run one
:: app instance; build workers are lane-scoped so out\ does not kill out_<lane>\.
taskkill /F /IM Tankoban.exe >nul 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='SilentlyContinue'; $buildDir='%BUILD_DIR%'.Replace('\','/').TrimEnd('/'); $pattern=[regex]::Escape($buildDir)+'($|[/\s\x22''])'; Get-CimInstance Win32_Process | Where-Object { ($_.Name -eq 'ninja.exe' -or $_.Name -eq 'cl.exe') -and (([string]$_.CommandLine -replace '\\','/') -match $pattern) } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }" >nul 2>&1

:: Set up MSVC environment
echo [1/4] Setting up MSVC environment...
call "%VCVARS%" x64 >nul 2>&1
if errorlevel 1 (
    echo ERROR: Failed to set up MSVC environment.
    pause
    exit /b 1
)

call :ensure_vcpkg_root

:: Configure (skipped if already configured)
:: REPO_HYGIENE Phase 2 (2026-04-26) - same effective options as the default
:: preset: Ninja, Release, vcpkg toolchain, x64-windows, Qt prefix path.
if exist "%BUILD_DIR%\CMakeCache.txt" (
    powershell -NoProfile -Command "$cmakeLists=Get-Item -LiteralPath '%PROJECT_DIR%\CMakeLists.txt'; $cache=Get-Item -LiteralPath '%BUILD_DIR%\CMakeCache.txt'; $ninja=Get-Item -LiteralPath '%BUILD_DIR%\build.ninja' -ErrorAction SilentlyContinue; if ($cmakeLists.LastWriteTime -gt $cache.LastWriteTime -or -not $ninja -or $cmakeLists.LastWriteTime -gt $ninja.LastWriteTime -or $cache.LastWriteTime -gt $ninja.LastWriteTime) { exit 1 } else { exit 0 }"
    if errorlevel 1 (
        echo [2/4] CMakeLists.txt newer than cache -- re-configuring...
        cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%"
        if errorlevel 1 (
            echo ERROR: cmake re-configure failed.
            pause
            exit /b 1
        )
        powershell -NoProfile -Command "$cache=Get-Item -LiteralPath '%BUILD_DIR%\CMakeCache.txt'; $ninja=Get-Item -LiteralPath '%BUILD_DIR%\build.ninja' -ErrorAction SilentlyContinue; if ($ninja) { $cache.LastWriteTime = $ninja.LastWriteTime } else { $cache.LastWriteTime = Get-Date }" >nul 2>&1
    ) else (
        echo [2/4] Build dir exists, cache up-to-date -- skipping configure.
    )
) else (
    echo [2/4] Configuring CMake in %BUILD_DIR%...
    cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -G Ninja ^
        -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
        -DVCPKG_INSTALLED_DIR="%VCPKG_INSTALLED_DIR%" ^
        -DVCPKG_TARGET_TRIPLET=x64-windows ^
        -DCMAKE_PREFIX_PATH="%QT_DIR%"
    if errorlevel 1 (
        echo ERROR: CMake configure failed.
        echo If this is a fresh clone, run setup.bat first to install vcpkg deps.
        pause
        exit /b 1
    )
)

:: Build
echo [3/4] Building...
cmake --build "%BUILD_DIR%" --parallel
set BUILD_EXIT=%ERRORLEVEL%
if %BUILD_EXIT% NEQ 0 (
    echo ERROR: Build failed ^(exit code %BUILD_EXIT%^).
    echo Resetting ninja state to prevent recovery-loop corruption ^(next build will be a full clean rebuild^).
    :: A failed or interrupted ninja can leave partial state files that recover
    :: forever. Delete only this lane's ninja state; keep cache/build graph.
    powershell -NoProfile -Command "Remove-Item -LiteralPath '%BUILD_DIR%\.ninja_deps','%BUILD_DIR%\.ninja_log' -Force -ErrorAction SilentlyContinue" >nul 2>&1
    pause
    exit /b %BUILD_EXIT%
)

:: Deploy Kokoro TTS model (if not already present)
if exist "%PROJECT_DIR%\models\kokoro" (
    if not exist "%BUILD_DIR%\models\kokoro" (
        echo Deploying Kokoro TTS model...
        xcopy /E /I /Q "%PROJECT_DIR%\models\kokoro" "%BUILD_DIR%\models\kokoro" >nul 2>&1
    )
)

:: Deploy book reader resources (sync newer files each build)
:: PER_VIEW_CHROME_FIX 2026-05-02 - was guarded with `if not exist` which
:: skipped the sync after first build, leaving HTML/CSS/JS edits silently
:: invisible. /D copies only files newer than destination so re-builds stay
:: fast. /Y overwrites without prompts.
if exist "%PROJECT_DIR%\resources\book_reader" (
    xcopy /E /I /Y /D /Q "%PROJECT_DIR%\resources\book_reader" "%BUILD_DIR%\resources\book_reader" >nul 2>&1
)

:: Deploy Fandom manifests (sync newer files each build)
:: WikiManifestRegistry reads from applicationDirPath() +
:: "/resources/fandom_manifests" at app start.
if exist "%PROJECT_DIR%\resources\fandom_manifests" (
    xcopy /E /I /Y /D /Q "%PROJECT_DIR%\resources\fandom_manifests" "%BUILD_DIR%\resources\fandom_manifests" >nul 2>&1
)

:: Run
echo [4/4] Launching Tankoban...
set "SHERPA_BIN=%PROJECT_DIR%\third_party\sherpa-onnx\sherpa-onnx-v1.12.21-win-x64-shared\lib"
set PATH=%QT_DIR%\bin;C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin;%SHERPA_BIN%;%PATH%
:: STREAM_ENGINE_FIX Phase 1.2 - structured telemetry log facility (Agent 4).
set TANKOBAN_STREAM_TELEMETRY=1
:: Mode A/B alert-trace diagnostic (Agent 4B).
set TANKOBAN_ALERT_TRACE=1
:: AUDIO_HOT_DEVICE_REROUTE_FIX2 diagnostic.
set TANKOBAN_SIDECAR_DEBUG=1
:: Stremio libtorrent session_params port (Experiment 1 APPROVED 2026-04-23).
set TANKOBAN_STREMIO_TUNE=1
:: REPO_HYGIENE Phase 3 (2026-04-26): --dev-control activates the dev-control
:: bridge (QLocalServer on TankobanDevControl named pipe) so tankoctl + agent
:: smokes can query app state directly.
start "" "%BUILD_DIR%\Tankoban.exe" --dev-control

endlocal
exit /b 0

:resolve_build_dir
set "BUILD_LANE=%TANKOBAN_BUILD_LANE%"
if "%BUILD_LANE%"=="" (
    for /f "delims=" %%G in ('git rev-parse --git-dir 2^>nul') do set "GIT_DIR=%%G"
    if defined GIT_DIR (
        set "GIT_DIR_NORM=!GIT_DIR:/=\!"
        if not "!GIT_DIR_NORM:\worktrees\=!"=="!GIT_DIR_NORM!" (
            set "WORKTREE_NAME=!GIT_DIR_NORM:*\worktrees\=!"
            for /f "tokens=1 delims=\" %%L in ("!WORKTREE_NAME!") do set "BUILD_LANE=%%L"
        )
    )
)

if not "%BUILD_LANE%"=="" (
    echo(%BUILD_LANE%| findstr /R "[^A-Za-z0-9_-]" >nul
    if not errorlevel 1 (
        echo ERROR: TANKOBAN_BUILD_LANE must match [A-Za-z0-9_-]+
        exit /b 2
    )
    set "BUILD_DIR=%~dp0out_%BUILD_LANE%"
    set "VCPKG_INSTALLED_DIR=%LOCALAPPDATA%\vcpkg\tankoban_out_%BUILD_LANE%"
) else (
    set "BUILD_DIR=%~dp0out"
    set "VCPKG_INSTALLED_DIR=%LOCALAPPDATA%\vcpkg\tankoban_out"
)
exit /b 0

:ensure_vcpkg_root
if "%VCPKG_ROOT%"=="" (
    if exist "C:\vcpkg\vcpkg.exe" (
        set "VCPKG_ROOT=C:\vcpkg"
    ) else if exist "C:\tools\vcpkg\vcpkg.exe" (
        set "VCPKG_ROOT=C:\tools\vcpkg"
    )
)
exit /b 0
