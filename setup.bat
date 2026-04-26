@echo off
:: REPO_HYGIENE Phase 2 (2026-04-26) — one-time prereq check + vcpkg install +
:: cmake configure + first build. Run once after a fresh clone.
::
:: Prereq matrix:
::   - Windows 10 / 11 x64
::   - Qt 6.10.2 at C:\tools\qt6sdk\6.10.2\msvc2022_64    (manual install)
::   - MSVC 2022 Build Tools                              (manual install)
::   - vcpkg at C:\vcpkg or path in VCPKG_ROOT             (this script will offer to install)
::
:: After this script succeeds:
::   - Run `build_and_run.bat` for normal dev cycle.
::   - Run `bash scripts/repo-consistency.sh` for static lint.

setlocal EnableDelayedExpansion

set "PROJECT_DIR=%~dp0."
set "QT_DIR=C:\tools\qt6sdk\6.10.2\msvc2022_64"
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"

echo ======================================================
echo Tankoban one-time setup
echo ======================================================
echo.

:: ── 1. Qt 6.10.2 check ─────────────────────────────────────────────────────
echo [1/5] Checking for Qt 6.10.2 at %QT_DIR%...
if not exist "%QT_DIR%\bin\Qt6Core.dll" (
    echo ERROR: Qt 6.10.2 not found at %QT_DIR%.
    echo.
    echo Install Qt 6.10.2 (msvc2022_64 component^) from:
    echo   https://www.qt.io/download-qt-installer
    echo.
    echo Required Qt components: Core, Gui, Widgets, Network, OpenGL, OpenGLWidgets, Svg.
    pause
    exit /b 1
)
echo OK.

:: ── 2. MSVC 2022 check ─────────────────────────────────────────────────────
echo [2/5] Checking for MSVC 2022 Build Tools...
if not exist "%VCVARS%" (
    echo ERROR: vcvarsall.bat not found at %VCVARS%.
    echo.
    echo Install Visual Studio 2022 Build Tools from:
    echo   https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022
    echo.
    echo Workload: "Desktop development with C++".
    pause
    exit /b 1
)
echo OK.

:: ── 3. vcpkg check + install ───────────────────────────────────────────────
echo [3/5] Checking for vcpkg...
if "%VCPKG_ROOT%"=="" (
    if exist "C:\vcpkg\vcpkg.exe" (
        set "VCPKG_ROOT=C:\vcpkg"
        echo Found vcpkg at C:\vcpkg.
    ) else if exist "C:\tools\vcpkg\vcpkg.exe" (
        set "VCPKG_ROOT=C:\tools\vcpkg"
        echo Found vcpkg at C:\tools\vcpkg.
    ) else (
        echo vcpkg not found. Cloning to C:\vcpkg...
        git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
        if errorlevel 1 (
            echo ERROR: git clone failed. Install git and retry, or manually clone vcpkg to C:\vcpkg.
            pause
            exit /b 1
        )
        call C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics
        if errorlevel 1 (
            echo ERROR: vcpkg bootstrap failed.
            pause
            exit /b 1
        )
        set "VCPKG_ROOT=C:\vcpkg"
    )
) else (
    if not exist "%VCPKG_ROOT%\vcpkg.exe" (
        echo ERROR: VCPKG_ROOT=%VCPKG_ROOT% but vcpkg.exe not found there.
        pause
        exit /b 1
    )
    echo Using vcpkg at %VCPKG_ROOT% (from VCPKG_ROOT env^).
)
echo OK.

:: ── 4. vcpkg install (driven by vcpkg.json manifest) ───────────────────────
echo [4/5] Installing vcpkg deps from vcpkg.json (first run takes 15-30 min)...
echo.
"%VCPKG_ROOT%\vcpkg.exe" install --triplet x64-windows
if errorlevel 1 (
    echo ERROR: vcpkg install failed. Check the log above.
    pause
    exit /b 1
)
echo.
echo OK.

:: ── 5. cmake configure (using preset) ──────────────────────────────────────
echo [5/5] Configuring cmake (Release preset)...
call "%VCVARS%" x64 >nul 2>&1
if errorlevel 1 (
    echo ERROR: vcvarsall failed.
    pause
    exit /b 1
)
cmake --preset default
if errorlevel 1 (
    echo ERROR: cmake configure failed.
    pause
    exit /b 1
)
echo.
echo ======================================================
echo Setup complete.
echo ======================================================
echo.
echo Next steps:
echo   - First build:  cmake --build --preset default
echo   - Or:           build_and_run.bat
echo.
endlocal
