@echo off
setlocal

:: ── Paths ───────────────────────────────────────────────────────────────────
set QT_DIR=C:\tools\qt6sdk\6.10.2\msvc2022_64
set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
set "PROJECT_DIR=%~dp0."
set "BUILD_DIR=%~dp0out"

:: ── Set up MSVC environment ────────────────────────────────────────────────
echo [1/4] Setting up MSVC environment...
call %VCVARS% x64 >nul 2>&1
if errorlevel 1 (
    echo ERROR: Failed to set up MSVC environment.
    pause
    exit /b 1
)

:: ── Configure ──────────────────────────────────────────────────────────────
echo [2/4] Configuring CMake...
cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -G "Ninja" ^
    -DCMAKE_BUILD_TYPE=Debug ^
    -DCMAKE_PREFIX_PATH="%QT_DIR%"
if errorlevel 1 (
    echo ERROR: CMake configure failed.
    pause
    exit /b 1
)

:: ── Build ──────────────────────────────────────────────────────────────────
echo [3/4] Building...
cmake --build "%BUILD_DIR%"
if errorlevel 1 (
    echo ERROR: Build failed.
    pause
    exit /b 1
)

:: ── Run ────────────────────────────────────────────────────────────────────
echo [4/4] Launching Tankoban...
set PATH=%QT_DIR%\bin;C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin;%PATH%
"%BUILD_DIR%\Tankoban.exe"

endlocal
