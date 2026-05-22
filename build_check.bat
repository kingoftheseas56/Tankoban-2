@echo off
setlocal enabledelayedexpansion

:: build_check.bat
:: Agent-safe compile verification for the Tankoban main app. Wraps
:: `cmake --build <build-dir> --target Tankoban` behind a grep-friendly status
:: line. Does NOT launch the exe and does NOT spawn GUI.
::
:: Build dir resolution:
::   - no lane: out\ (backwards-compatible main checkout behavior)
::   - TANKOBAN_BUILD_LANE=<lane>: out_<lane>\ after strict validation
::   - unset env in a Git worktree: derive <lane> from .git/worktrees/<name>
:: Lane names must match [A-Za-z0-9_-]+. Invalid names fail before creating
:: any build directory.
::
:: Agents: run after editing a .cpp to verify "did I break the compile?" in
:: ~30-90s. On success, prints `BUILD OK` (exit 0). On failure, prints last
:: 30 lines of the cl.exe diagnostic (exit propagated from cmake/ninja).
::
:: Full log at %BUILD_DIR%\_build_check.log for post-hoc diagnosis.

set "PROJECT_DIR=%~dp0."
set "QT_DIR=C:\tools\qt6sdk\6.10.2\msvc2022_64"
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"

call :resolve_build_dir
if errorlevel 1 exit /b %ERRORLEVEL%
set "LOG=%BUILD_DIR%\_build_check.log"

:: Guard 1 - MSVC env (same vcvarsall as build_and_run.bat).
call "%VCVARS%" x64 >nul 2>&1
if errorlevel 1 (
    echo BUILD CHECK: MSVC env setup failed.
    exit /b 3
)

call :ensure_vcpkg_root
call :configure_if_missing
if errorlevel 1 exit /b %ERRORLEVEL%

:: Compile + link Tankoban target only. Redirect combined streams to log.
cmake --build "%BUILD_DIR%" --config Release --target Tankoban > "%LOG%" 2>&1
set BUILD_EXIT=%ERRORLEVEL%

if %BUILD_EXIT% EQU 0 (
    echo BUILD OK
    exit /b 0
)

echo BUILD FAILED exit=%BUILD_EXIT%
echo --- last 30 lines of %LOG% ---
powershell -NoProfile -Command "Get-Content -Tail 30 -LiteralPath '%LOG%'"
exit /b %BUILD_EXIT%

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
        echo BUILD CHECK: TANKOBAN_BUILD_LANE must match [A-Za-z0-9_-]+
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

:configure_if_missing
if exist "%BUILD_DIR%\CMakeCache.txt" (
    powershell -NoProfile -Command "$cmakeLists=Get-Item -LiteralPath '%PROJECT_DIR%\CMakeLists.txt'; $cache=Get-Item -LiteralPath '%BUILD_DIR%\CMakeCache.txt'; $ninja=Get-Item -LiteralPath '%BUILD_DIR%\build.ninja' -ErrorAction SilentlyContinue; if ($cmakeLists.LastWriteTime -gt $cache.LastWriteTime -or -not $ninja -or $cmakeLists.LastWriteTime -gt $ninja.LastWriteTime -or $cache.LastWriteTime -gt $ninja.LastWriteTime) { exit 1 } else { exit 0 }"
    if errorlevel 1 (
        echo BUILD CHECK: CMakeLists.txt newer than cache - re-configuring %BUILD_DIR%
        cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%"
        if errorlevel 1 (
            echo BUILD CHECK: CMake re-configure failed.
            exit /b 2
        )
        powershell -NoProfile -Command "$cache=Get-Item -LiteralPath '%BUILD_DIR%\CMakeCache.txt'; $ninja=Get-Item -LiteralPath '%BUILD_DIR%\build.ninja' -ErrorAction SilentlyContinue; if ($ninja) { $cache.LastWriteTime = $ninja.LastWriteTime } else { $cache.LastWriteTime = Get-Date }" >nul 2>&1
    )
    exit /b 0
)

echo BUILD CHECK: configuring %BUILD_DIR%
cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_INSTALLED_DIR="%VCPKG_INSTALLED_DIR%" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows ^
    -DCMAKE_PREFIX_PATH="%QT_DIR%"
if errorlevel 1 (
    echo BUILD CHECK: CMake configure failed.
    echo If this is a fresh clone, run setup.bat first to install vcpkg deps.
    exit /b 2
)
exit /b 0
