@echo off
setlocal enabledelayedexpansion

:: ============================================================================
:: setup_libtorrent_msvc.bat
:: Builds libtorrent-rasterbar 2.0.11 with MSVC from source already at C:\tools
:: Also builds OpenSSL 3.2 with MSVC if not present.
::
:: Prerequisites: MSVC BuildTools 2022, Ninja, Perl (Strawberry), NASM (optional)
:: Output: C:\tools\libtorrent-2.0-msvc\  (include + lib)
:: ============================================================================

set TOOLS=C:\tools
set LT_VER=2.0.11
set LT_SRC=%TOOLS%\libtorrent-rasterbar-%LT_VER%
set LT_INSTALL=%TOOLS%\libtorrent-2.0-msvc
set BOOST_DIR=%TOOLS%\boost-1.84.0
set OPENSSL_INSTALL=%TOOLS%\openssl-msvc
set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"

echo ============================================================
echo  Tankoban 2 - libtorrent MSVC Build
echo ============================================================
echo.

:: ── 1. Verify prerequisites ────────────────────────────────────────────────
if not exist %VCVARS% (
    echo ERROR: MSVC BuildTools not found at %VCVARS%
    pause
    exit /b 1
)

if not exist "%LT_SRC%\CMakeLists.txt" (
    echo ERROR: libtorrent source not found at %LT_SRC%
    echo Run: cd C:\tools ^&^& tar xzf libtorrent-rasterbar-%LT_VER%.tar.gz
    pause
    exit /b 1
)

if not exist "%BOOST_DIR%\boost\version.hpp" (
    echo ERROR: Boost headers not found at %BOOST_DIR%
    pause
    exit /b 1
)

:: ── 2. Set up MSVC x64 environment ─────────────────────────────────────────
echo [1/4] Setting up MSVC x64 environment...
call %VCVARS% x64 >nul 2>&1
if errorlevel 1 (
    echo ERROR: Failed to set up MSVC environment.
    pause
    exit /b 1
)

:: ── 3. Build OpenSSL with MSVC (if not already done) ────────────────────────
if exist "%OPENSSL_INSTALL%\lib\libssl.lib" (
    echo [2/4] OpenSSL already built at %OPENSSL_INSTALL% - skipping.
) else if exist "%OPENSSL_INSTALL%\lib\ssl.lib" (
    echo [2/4] OpenSSL already built at %OPENSSL_INSTALL% - skipping.
) else (
    echo [2/4] Building OpenSSL 3.2 with MSVC...

    set OPENSSL_VER=3.2.3
    set OPENSSL_SRC=%TOOLS%\openssl-!OPENSSL_VER!
    set OPENSSL_TAR=%TOOLS%\openssl-!OPENSSL_VER!.tar.gz

    if not exist "!OPENSSL_TAR!" (
        echo      Downloading OpenSSL !OPENSSL_VER!...
        curl -L -o "!OPENSSL_TAR!" "https://github.com/openssl/openssl/releases/download/openssl-!OPENSSL_VER!/openssl-!OPENSSL_VER!.tar.gz"
        if errorlevel 1 (
            echo ERROR: Failed to download OpenSSL.
            pause
            exit /b 1
        )
    )

    if not exist "!OPENSSL_SRC!" (
        echo      Extracting...
        cd /d "%TOOLS%"
        tar xzf "!OPENSSL_TAR!"
    )

    echo      Configuring OpenSSL for MSVC (no-shared)...
    cd /d "!OPENSSL_SRC!"

    perl Configure VC-WIN64A no-shared no-tests --prefix="%OPENSSL_INSTALL%" --openssldir="%OPENSSL_INSTALL%\ssl"
    if errorlevel 1 (
        echo ERROR: OpenSSL configure failed. Is Perl installed? ^(Strawberry Perl^)
        pause
        exit /b 1
    )

    echo      Building OpenSSL ^(this takes a few minutes^)...
    nmake
    if errorlevel 1 (
        echo ERROR: OpenSSL build failed.
        pause
        exit /b 1
    )

    nmake install_sw
    if errorlevel 1 (
        echo ERROR: OpenSSL install failed.
        pause
        exit /b 1
    )

    echo      OpenSSL installed to %OPENSSL_INSTALL%
)

:: ── 4. Build libtorrent with MSVC ───────────────────────────────────────────
if exist "%LT_INSTALL%\lib\torrent-rasterbar.lib" (
    echo [3/4] libtorrent already built at %LT_INSTALL% - skipping.
    goto :verify
)

echo [3/4] Building libtorrent %LT_VER% with MSVC...

set LT_BUILD=%LT_SRC%\build-msvc
if exist "%LT_BUILD%" rmdir /s /q "%LT_BUILD%"
mkdir "%LT_BUILD%"
cd /d "%LT_BUILD%"

:: Find OpenSSL libs (MSVC names vary: libssl.lib or ssl.lib)
set SSL_LIB=
if exist "%OPENSSL_INSTALL%\lib\libssl.lib" set SSL_LIB=%OPENSSL_INSTALL%\lib\libssl.lib
if exist "%OPENSSL_INSTALL%\lib\ssl.lib" set SSL_LIB=%OPENSSL_INSTALL%\lib\ssl.lib

set CRYPTO_LIB=
if exist "%OPENSSL_INSTALL%\lib\libcrypto.lib" set CRYPTO_LIB=%OPENSSL_INSTALL%\lib\libcrypto.lib
if exist "%OPENSSL_INSTALL%\lib\crypto.lib" set CRYPTO_LIB=%OPENSSL_INSTALL%\lib\crypto.lib

if "!SSL_LIB!"=="" (
    echo ERROR: OpenSSL SSL library not found in %OPENSSL_INSTALL%\lib\
    dir "%OPENSSL_INSTALL%\lib\" 2>nul
    pause
    exit /b 1
)

echo      SSL lib:    !SSL_LIB!
echo      Crypto lib: !CRYPTO_LIB!

cmake "%LT_SRC%" -G "Ninja" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_INSTALL_PREFIX="%LT_INSTALL%" ^
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL ^
    -DBUILD_SHARED_LIBS=OFF ^
    -Dstatic_runtime=OFF ^
    -Dpython-bindings=OFF ^
    -Dpython-egg-info=OFF ^
    -Dpython-install-system-dir=OFF ^
    -Ddeprecated-functions=OFF ^
    -DBOOST_ROOT="%BOOST_DIR%" ^
    -DBoost_INCLUDE_DIR="%BOOST_DIR%" ^
    -DOPENSSL_ROOT_DIR="%OPENSSL_INSTALL%" ^
    -DOPENSSL_INCLUDE_DIR="%OPENSSL_INSTALL%\include" ^
    -DOPENSSL_SSL_LIBRARY="!SSL_LIB!" ^
    -DOPENSSL_CRYPTO_LIBRARY="!CRYPTO_LIB!"

if errorlevel 1 (
    echo ERROR: CMake configure failed.
    pause
    exit /b 1
)

echo      Building ^(this takes several minutes^)...
cmake --build . --config Release
if errorlevel 1 (
    echo ERROR: Build failed.
    pause
    exit /b 1
)

echo      Installing to %LT_INSTALL%...
cmake --install .
if errorlevel 1 (
    echo ERROR: Install failed.
    pause
    exit /b 1
)

:verify
:: ── 5. Verify ───────────────────────────────────────────────────────────────
echo [4/4] Verifying installation...

set LT_LIB=
if exist "%LT_INSTALL%\lib\torrent-rasterbar.lib" set LT_LIB=%LT_INSTALL%\lib\torrent-rasterbar.lib
if exist "%LT_INSTALL%\lib\libtorrent-rasterbar.lib" set LT_LIB=%LT_INSTALL%\lib\libtorrent-rasterbar.lib

if "!LT_LIB!"=="" (
    echo ERROR: libtorrent library not found in %LT_INSTALL%\lib\
    dir "%LT_INSTALL%\lib\" 2>nul
    pause
    exit /b 1
)

if not exist "%LT_INSTALL%\include\libtorrent\session.hpp" (
    echo ERROR: libtorrent headers not found.
    pause
    exit /b 1
)

echo.
echo ============================================================
echo  SUCCESS
echo ============================================================
echo.
echo  libtorrent: %LT_INSTALL%
echo  Library:    !LT_LIB!
echo  Headers:    %LT_INSTALL%\include\libtorrent\
echo  OpenSSL:    %OPENSSL_INSTALL%
echo  Boost:      %BOOST_DIR%
echo.
echo  Add to CMakeLists.txt:
echo    set(LIBTORRENT_ROOT "C:/tools/libtorrent-2.0-msvc")
echo    set(OPENSSL_MSVC_ROOT "C:/tools/openssl-msvc")
echo.

endlocal
