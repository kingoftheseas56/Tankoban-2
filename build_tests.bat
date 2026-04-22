@echo off
setlocal enabledelayedexpansion

:: ── build_tests.bat ─────────────────────────────────────────────────────────
:: Agent-safe unit-test build + run. Reconfigures out\ with
:: TANKOBAN_BUILD_TESTS=ON, builds the tankoban_tests target, and runs ctest.
:: Does NOT touch the main Tankoban.exe; tests live in out\tankoban_tests.exe.
::
:: Gated real-probe tests in AudiobookMetaCacheTest read
:: TANKOBAN_TEST_AUDIOBOOK_FIXTURE + TANKOBAN_TEST_FFPROBE from the env.

set "BUILD_DIR=%~dp0out"
set "LOG=%BUILD_DIR%\_build_tests.log"

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
if errorlevel 1 (
    echo TESTS: MSVC env setup failed.
    exit /b 3
)

"C:\tools\cmake-3.31.6-windows-x86_64\bin\cmake.exe" -S "%~dp0." -B "%BUILD_DIR%" -DTANKOBAN_BUILD_TESTS=ON > "%LOG%" 2>&1
if errorlevel 1 (
    echo TESTS: configure failed. See %LOG%.
    powershell -NoProfile -Command "Get-Content -Tail 30 -LiteralPath '%LOG%'"
    exit /b 4
)

"C:\tools\cmake-3.31.6-windows-x86_64\bin\cmake.exe" --build "%BUILD_DIR%" --config Release --target tankoban_tests >> "%LOG%" 2>&1
if errorlevel 1 (
    echo TESTS: build failed. See %LOG%.
    powershell -NoProfile -Command "Get-Content -Tail 30 -LiteralPath '%LOG%'"
    exit /b 5
)

:: Qt6 bin must be on PATH so tankoban_tests.exe finds Qt6Core.dll at runtime.
set "PATH=C:\tools\qt6sdk\6.10.2\msvc2022_64\bin;%PATH%"
pushd "%BUILD_DIR%"
"C:\tools\cmake-3.31.6-windows-x86_64\bin\ctest.exe" --output-on-failure -R tankoban_tests
set RUN_EXIT=%ERRORLEVEL%
popd

exit /b %RUN_EXIT%
