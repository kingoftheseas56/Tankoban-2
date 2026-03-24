@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
cd /d "C:\Users\Suprabha\Desktop\Tankoban 2"
(
echo === CLEAN CONFIGURE ===
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="C:\tools\qt6sdk\6.10.2\msvc2022_64" --fresh 2>&1
echo === CMAKE BUILD ===
cmake --build build 2>&1
echo === DONE ===
) > "C:\Users\Suprabha\Desktop\Tankoban 2\_build_log.txt" 2>&1
