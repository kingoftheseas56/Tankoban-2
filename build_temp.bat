@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>nul
echo VCVARS OK
cmake -S "C:\Users\Suprabha\Desktop\Tankoban 2" -B "C:\Users\Suprabha\Desktop\Tankoban 2\build2" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="C:\tools\qt6sdk\6.10.2\msvc2022_64"
cmake --build "C:\Users\Suprabha\Desktop\Tankoban 2\build2"
echo BUILD DONE
endlocal
