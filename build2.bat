@echo off
setlocal
set QT_DIR=C:\tools\qt6sdk\6.10.2\msvc2022_64
set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
call %VCVARS% x64 >/dev/null 2>&1
cmake -S . -B build2 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=%QT_DIR% 2>&1
cmake --build build2 2>&1
