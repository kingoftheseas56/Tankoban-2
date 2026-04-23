@echo off
setlocal

:: ── Paths ───────────────────────────────────────────────────────────────────
set QT_DIR=C:\tools\qt6sdk\6.10.2\msvc2022_64
set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
set "PROJECT_DIR=%~dp0."
set "BUILD_DIR=%~dp0out"

:: ── Kill any running instance ───────────────────────────────────────────────
taskkill /F /IM Tankoban.exe >nul 2>&1
taskkill /F /IM ninja.exe    >nul 2>&1
taskkill /F /IM cl.exe       >nul 2>&1

:: ── Set up MSVC environment ────────────────────────────────────────────────
echo [1/4] Setting up MSVC environment...
call %VCVARS% x64 >nul 2>&1
if errorlevel 1 (
    echo ERROR: Failed to set up MSVC environment.
    pause
    exit /b 1
)

:: ── Configure (skipped if already configured) ──────────────────────────────
if exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [2/4] Build dir exists — skipping configure.
) else (
    echo [2/4] Configuring CMake...
    cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -G "Ninja" ^
        -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
        -DLIBTORRENT_ROOT="C:/tools/libtorrent-2.0-msvc" ^
        -DBOOST_ROOT="C:/tools/boost-1.84.0" ^
        -DOPENSSL_MSVC_ROOT="C:/tools/openssl-msvc" ^
        -DLIBTORRENT_INCLUDE_DIR="C:/tools/libtorrent-2.0-msvc/include" ^
        -DLIBTORRENT_LIBRARY="C:/tools/libtorrent-2.0-msvc/lib/torrent-rasterbar.lib" ^
        -DBOOST_INCLUDE_DIR="C:/tools/boost-1.84.0"
    if errorlevel 1 (
        echo ERROR: CMake configure failed.
        pause
        exit /b 1
    )
)

:: ── Build ──────────────────────────────────────────────────────────────────
echo [3/4] Building...
cmake --build "%BUILD_DIR%" --parallel
set BUILD_EXIT=%ERRORLEVEL%
if %BUILD_EXIT% NEQ 0 (
    echo ERROR: Build failed ^(exit code %BUILD_EXIT%^).
    pause
    exit /b %BUILD_EXIT%
)

:: ── Deploy Kokoro TTS model (if not already present) ──────────────────────
if exist "%PROJECT_DIR%\models\kokoro" (
    if not exist "%BUILD_DIR%\models\kokoro" (
        echo Deploying Kokoro TTS model...
        xcopy /E /I /Q "%PROJECT_DIR%\models\kokoro" "%BUILD_DIR%\models\kokoro" >nul 2>&1
    )
)

:: ── Deploy book reader resources (if not already present) ─────────────────
if exist "%PROJECT_DIR%\resources\book_reader" (
    if not exist "%BUILD_DIR%\resources\book_reader" (
        echo Deploying book reader resources...
        xcopy /E /I /Q "%PROJECT_DIR%\resources\book_reader" "%BUILD_DIR%\resources\book_reader" >nul 2>&1
    )
)

:: ── Run ────────────────────────────────────────────────────────────────────
echo [4/4] Launching Tankoban...
set "SHERPA_BIN=%PROJECT_DIR%\third_party\sherpa-onnx\sherpa-onnx-v1.12.21-win-x64-shared\lib"
set PATH=%QT_DIR%\bin;C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin;%SHERPA_BIN%;%PATH%
:: STREAM_ENGINE_FIX Phase 1.2 — structured telemetry log facility (Agent 4).
:: When set to 1, StreamEngine writes per-stream lifecycle events to
:: stream_telemetry.log next to Tankoban.exe. Cheap when off (cached env-var
:: short-circuit before any allocation in the writeTelemetry hot path).
:: TODO Phase 4 will gate on per-need; for Slice A trace collection it stays
:: on by default. Flip to 0 (or remove) to disable.
set TANKOBAN_STREAM_TELEMETRY=1
:: Mode A/B alert-trace diagnostic (Agent 4B) — when set to 1, TorrentEngine
:: captures libtorrent piece_finished + block_finished alerts with wall-clock
:: ms to alert_trace.log. Disambiguates the "deadlines not being honored"
:: hypothesis for cold-session 0%-buffering + mid-file-seek-hang classes.
:: Delete or flip to 0 after diagnosis concludes.
set TANKOBAN_ALERT_TRACE=1
:: Stremio libtorrent session_params port (Experiment 1 APPROVED 2026-04-23).
:: Activates 10 streaming-optimized libtorrent settings in TorrentEngine.cpp
:: (commit 59cf47b). Empirical: 65% stall reduction, 89.5% cold-open improvement,
:: 86.3% p99 wait reduction on Invincible S01E01 EZTV. Audit at
:: agents/audits/stremio_tuning_ab_2026-04-23.md. Interim path until the
:: STREAM_ENGINE_SPLIT refactor lands (at which point this env var becomes
:: unnecessary because the stream-dedicated engine always applies the settings
:: and Tankorent-dedicated engine stays on current defaults). Flip to 0 to
:: revert to pre-experiment Tankoban behavior for testing.
set TANKOBAN_STREMIO_TUNE=1
start "" "%BUILD_DIR%\Tankoban.exe"

endlocal
