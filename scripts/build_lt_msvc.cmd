@echo off
setlocal

set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
call %VCVARS% x64

set TOOLS=C:\tools
set LT_SRC=%TOOLS%\libtorrent-rasterbar-2.0.11
set LT_BUILD=%LT_SRC%\build-msvc
set LT_INSTALL=%TOOLS%\libtorrent-2.0-msvc
set BOOST_DIR=%TOOLS%\boost-1.84.0
set OPENSSL_INSTALL=%TOOLS%\openssl-msvc

echo === Building libtorrent 2.0.11 with MSVC (cl.exe) ===

cd /d "%LT_BUILD%"

cmake "%LT_SRC%" -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe -DCMAKE_INSTALL_PREFIX="%LT_INSTALL%" -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL -DBUILD_SHARED_LIBS=OFF -Dstatic_runtime=OFF -Dpython-bindings=OFF -Dpython-egg-info=OFF -Dpython-install-system-dir=OFF -Ddeprecated-functions=OFF -DBOOST_ROOT="%BOOST_DIR%" -DBoost_INCLUDE_DIR="%BOOST_DIR%" -DOPENSSL_ROOT_DIR="%OPENSSL_INSTALL%" -DOPENSSL_INCLUDE_DIR="%OPENSSL_INSTALL%\include"
if errorlevel 1 exit /b 1

cmake --build . --config Release
if errorlevel 1 exit /b 1

cmake --install .
echo === DONE ===
dir "%LT_INSTALL%\lib\*torrent*"
