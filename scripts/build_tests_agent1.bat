@echo off
setlocal enabledelayedexpansion
:: Agent 1 lane test runner. Configures out_agent1 with tests ON (idempotent),
:: builds tankoban_tests, and runs an optional gtest filter (arg 1, default all).
:: Isolated lane = no shared-out collision with Agent 3's out\ build.
::   usage: scripts\build_tests_agent1.bat [gtest_filter]
set "PROJECT_DIR=%~dp0.."
set "QT_DIR=C:\tools\qt6sdk\6.10.2\msvc2022_64"
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
set "BUILD_DIR=%PROJECT_DIR%\out_agent1"
set "VCPKG_INSTALLED_DIR=%LOCALAPPDATA%\vcpkg\tankoban_out_agent1"
set "FILTER=%~1"
if "%FILTER%"=="" set "FILTER=*"

if "%VCPKG_ROOT%"=="" (
    if exist "C:\vcpkg\vcpkg.exe" set "VCPKG_ROOT=C:\vcpkg"
    if exist "C:\tools\vcpkg\vcpkg.exe" set "VCPKG_ROOT=C:\tools\vcpkg"
)

call "%VCVARS%" x64 >nul 2>&1
if errorlevel 1 ( echo TESTS: MSVC env setup failed & exit /b 3 )

:: Qt runtime resolvable BEFORE build â€” gtest_discover_tests runs the exe at
:: build time to enumerate cases. Offscreen so no display needed.
set "PATH=%QT_DIR%\bin;%PATH%"
set "QT_PLUGIN_PATH=%QT_DIR%\plugins"
set "QT_QPA_PLATFORM=offscreen"

:: Reconfigure to flip TANKOBAN_BUILD_TESTS ON (cheap if cache already ON; reuses
:: the lane's existing vcpkg + object cache either way).
cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DTANKOBAN_BUILD_TESTS=ON ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_INSTALLED_DIR="%VCPKG_INSTALLED_DIR%" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows ^
    -DCMAKE_PREFIX_PATH="%QT_DIR%"
if errorlevel 1 ( echo TESTS: CMake configure failed & exit /b 2 )

cmake --build "%BUILD_DIR%" --config Release --target tankoban_tests
if errorlevel 1 ( echo TESTS: BUILD FAILED & exit /b 1 )

if exist "%BUILD_DIR%\tankoban_tests.exe" (
    "%BUILD_DIR%\tankoban_tests.exe" --gtest_filter=%FILTER%
    exit /b %ERRORLEVEL%
)
echo TESTS: tankoban_tests.exe not found in %BUILD_DIR%
exit /b 4
