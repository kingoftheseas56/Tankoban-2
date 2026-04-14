@echo off
setlocal

set QSB=C:\tools\qt6sdk\6.10.2\msvc2022_64\bin\qsb.exe
set SHADER_DIR=%~dp0..\resources\shaders

echo Compiling shaders...

"%QSB%" --glsl "440,310 es" --hlsl 50 --msl 12 -o "%SHADER_DIR%\video.vert.qsb" "%SHADER_DIR%\video.vert"
if %ERRORLEVEL% NEQ 0 (echo FAILED: video.vert & exit /b 1)
echo   video.vert OK

"%QSB%" --glsl "440,310 es" --hlsl 50 --msl 12 -o "%SHADER_DIR%\video.frag.qsb" "%SHADER_DIR%\video.frag"
if %ERRORLEVEL% NEQ 0 (echo FAILED: video.frag & exit /b 1)
echo   video.frag OK

echo All shaders compiled successfully.
