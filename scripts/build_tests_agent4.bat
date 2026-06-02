@echo off
setlocal enabledelayedexpansion
:: Agent 4 lane test runner. Configures out_agent4 with tests ON (once),
:: builds the tankoban_tests target, deploys Qt runtime, and runs an optional
:: gtest filter (arg 1, default: all). Isolated lane = no shared-out collision.
::   usage: scripts\build_tests_agent4.bat [gtest_filter]
set "PROJECT_DIR=%~dp0.."
set "QT_DIR=C:\tools\qt6sdk\6.10.2\msvc2022_64"
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
set "BUILD_DIR=%PROJECT_DIR%\out_agent4"
set "VCPKG_INSTALLED_DIR=%LOCALAPPDATA%\vcpkg\tankoban_out_agent4"
set "FILTER=%~1"
if "%FILTER%"=="" set "FILTER=*"

if "%VCPKG_ROOT%"=="" (
    if exist "C:\vcpkg\vcpkg.exe" set "VCPKG_ROOT=C:\vcpkg"
    if exist "C:\tools\vcpkg\vcpkg.exe" set "VCPKG_ROOT=C:\tools\vcpkg"
)

call "%VCVARS%" x64 >nul 2>&1
if errorlevel 1 ( echo TESTS: MSVC env setup failed & exit /b 3 )

:: Qt runtime must be resolvable BEFORE the build, because gtest_discover_tests
:: runs tankoban_tests.exe at build time to enumerate cases. Headless (offscreen)
:: so no platform display is needed.
set "PATH=%QT_DIR%\bin;%PATH%"
set "QT_PLUGIN_PATH=%QT_DIR%\plugins"
set "QT_QPA_PLATFORM=offscreen"

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo TESTS: configuring %BUILD_DIR% with TANKOBAN_BUILD_TESTS=ON
    cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -G Ninja ^
        -DCMAKE_BUILD_TYPE=Release ^
        -DTANKOBAN_BUILD_TESTS=ON ^
        -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
        -DVCPKG_INSTALLED_DIR="%VCPKG_INSTALLED_DIR%" ^
        -DVCPKG_TARGET_TRIPLET=x64-windows ^
        -DCMAKE_PREFIX_PATH="%QT_DIR%"
    if errorlevel 1 ( echo TESTS: CMake configure failed & exit /b 2 )
)

cmake --build "%BUILD_DIR%" --config Release --target tankoban_tests
if errorlevel 1 ( echo TESTS: BUILD FAILED & exit /b 1 )

:: Ensure Qt runtime + SQL driver plugin resolve for the test exe.
set "PATH=%QT_DIR%\bin;%PATH%"
set "QT_PLUGIN_PATH=%QT_DIR%\plugins"

if exist "%BUILD_DIR%\tankoban_tests.exe" (
    "%BUILD_DIR%\tankoban_tests.exe" --gtest_filter=%FILTER%
    exit /b %ERRORLEVEL%
)
echo TESTS: tankoban_tests.exe not found in %BUILD_DIR%
exit /b 4
