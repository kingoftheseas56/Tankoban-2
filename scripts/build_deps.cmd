@echo off
setlocal

set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
call %VCVARS% x64

set TOOLS=C:\tools
set PERL=%TOOLS%\strawberry-perl\perl\bin\perl.exe
set OPENSSL_SRC=%TOOLS%\openssl-3.2.3
set OPENSSL_INSTALL=%TOOLS%\openssl-msvc
set LT_SRC=%TOOLS%\libtorrent-rasterbar-2.0.11
set LT_INSTALL=%TOOLS%\libtorrent-2.0-msvc
set BOOST_DIR=%TOOLS%\boost-1.84.0

set PATH=%TOOLS%\strawberry-perl\perl\bin;%TOOLS%\strawberry-perl\c\bin;%PATH%

echo === Building OpenSSL 3.2.3 with MSVC ===
cd /d "%OPENSSL_SRC%"
perl Configure VC-WIN64A no-shared no-tests no-asm --prefix="%OPENSSL_INSTALL%" --openssldir="%OPENSSL_INSTALL%\ssl"
nmake
nmake install_sw
echo === OpenSSL done ===

echo === Building libtorrent 2.0.11 with MSVC ===
if exist "%LT_SRC%\build-msvc" rmdir /s /q "%LT_SRC%\build-msvc"
mkdir "%LT_SRC%\build-msvc"
cd /d "%LT_SRC%\build-msvc"

cmake "%LT_SRC%" -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="%LT_INSTALL%" -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL -DBUILD_SHARED_LIBS=OFF -Dstatic_runtime=OFF -Dpython-bindings=OFF -Dpython-egg-info=OFF -Dpython-install-system-dir=OFF -Ddeprecated-functions=OFF -DBOOST_ROOT="%BOOST_DIR%" -DBoost_INCLUDE_DIR="%BOOST_DIR%" -DOPENSSL_ROOT_DIR="%OPENSSL_INSTALL%" -DOPENSSL_INCLUDE_DIR="%OPENSSL_INSTALL%\include"
cmake --build . --config Release
cmake --install .
echo === libtorrent done ===
echo === ALL DONE ===
