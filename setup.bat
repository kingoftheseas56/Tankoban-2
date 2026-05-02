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
echo [1/6] Checking for Qt 6.10.2 at %QT_DIR%...
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
echo [2/6] Checking for MSVC 2022 Build Tools...
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
echo [3/6] Checking for vcpkg...
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
echo [4/6] Installing vcpkg deps from vcpkg.json (first run takes 15-30 min)...
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
echo [5/6] Configuring cmake (Release preset)...
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
echo OK.

:: ── 6. libmpv prebuilt (MPV_RENDER_API_INTEGRATION Phase 1, 2026-04-28) ────
:: Vendor libmpv 0.38.0 from shinchiro/mpv-winbuild-cmake. Self-contained DLL
:: (libplacebo/libass/ffmpeg statically linked). bin/ + lib/ are gitignored;
:: include/mpv/*.h + LICENSE.libmpv.txt + VERSION.txt ARE tracked so fresh
:: clones configure cleanly without running this step (mpv backend just
:: disabled until setup.bat brings the DLL on disk).
echo [6/6] libmpv prebuilt (mpv backend dependency)...
set "MPV_DIR=%~dp0resources\libmpv\windows"
set "MPV_DLL=%MPV_DIR%\bin\libmpv-2.dll"
if exist "%MPV_DLL%" (
    echo   already present, skipping.
    goto :mpv_done
)
set "MPV_TAG=20260421"
set "MPV_FILE=mpv-dev-x86_64-v3-20260421-git-5921fe5.7z"
set "MPV_URL=https://github.com/shinchiro/mpv-winbuild-cmake/releases/download/%MPV_TAG%/%MPV_FILE%"
:: SHA-256 captured 2026-04-28 by Phase-1 author against the live release.
:: To bump libmpv: update MPV_TAG + MPV_FILE above and capture a new SHA.
set "MPV_SHA=9f426ff3fd49d072c16e99b8af95e8a82feb4b0f45f1ff5c7a7b0cf9abbf7449"
set "MPV_7Z=%TEMP%\%MPV_FILE%"
echo   downloading libmpv 0.38.0 (~50 MB)...
curl -L -o "%MPV_7Z%" "%MPV_URL%"
if errorlevel 1 goto :mpv_fail
:: Verify SHA-256
for /f "tokens=*" %%H in ('certutil -hashfile "%MPV_7Z%" SHA256 ^| findstr /v ":" ^| findstr /v "CertUtil"') do set "GOT=%%H"
if /I not "!GOT!"=="%MPV_SHA%" (
    echo   SHA-256 mismatch:
    echo     expected: %MPV_SHA%
    echo     got:      !GOT!
    echo   Update MPV_SHA in setup.bat and retry.
    goto :mpv_fail
)
mkdir "%MPV_DIR%\bin" 2>nul
mkdir "%MPV_DIR%\lib" 2>nul
mkdir "%MPV_DIR%\include\mpv" 2>nul
:: Extract — shinchiro 7z drops files at archive root + include/mpv/ subdir.
set "SEVENZIP=C:\Program Files\7-Zip\7z.exe"
if not exist "%SEVENZIP%" (
    echo   ERROR: 7-Zip not found at %SEVENZIP%. Install from https://www.7-zip.org/
    goto :mpv_fail
)
"%SEVENZIP%" x "%MPV_7Z%" -o"%MPV_DIR%" -y
if errorlevel 1 goto :mpv_fail
:: shinchiro release 20260421 layout (verified): libmpv-2.dll + libmpv.dll.a
:: + include/mpv/{client,render,render_gl,stream_cb}.h. No .def file shipped
:: in this build. The .dll.a IS a standard COFF import lib that MSVC's
:: link.exe accepts — just rename to .lib so cmake's find_library picks it
:: up alongside the .dll. If a future shinchiro tag drops the .dll.a in
:: favour of a .def, this step needs `lib /def:` to generate mpv.lib —
:: re-add the block at that time.
if exist "%MPV_DIR%\libmpv-2.dll" move /Y "%MPV_DIR%\libmpv-2.dll" "%MPV_DIR%\bin\" >nul
if exist "%MPV_DIR%\libmpv.dll.a" move /Y "%MPV_DIR%\libmpv.dll.a" "%MPV_DIR%\lib\mpv.lib" >nul
del "%MPV_7Z%"
echo   libmpv extracted to %MPV_DIR%
goto :mpv_done
:mpv_fail
echo   libmpv setup FAILED — mpv backend will be disabled (ffmpeg backend unaffected).
:mpv_done
echo OK.

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
