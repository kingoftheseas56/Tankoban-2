@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
set PATH=C:\tools\qt6sdk\6.10.2\msvc2022_64\bin;C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin;C:\Users\Suprabha\Desktop\Tankoban 2\third_party\sherpa-onnx\sherpa-onnx-v1.12.21-win-x64-shared\lib;%PATH%
set QT_LOGGING_RULES=*=true
set QT_FORCE_STDERR_LOGGING=1
cd /d "C:\Users\Suprabha\Desktop\Tankoban 2"
out\Tankoban.exe > _crash_log.txt 2>&1
echo EXIT_CODE=%ERRORLEVEL% >> _crash_log.txt
