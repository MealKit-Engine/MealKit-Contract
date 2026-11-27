@echo off
rem Builds mkcontract (Release, Ninja) into build\windows\ and runs the tests.
setlocal
if defined VCINSTALLDIR goto build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VS=%%i"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

:build
cd /d "%~dp0"
if not exist build\windows\build.ninja cmake -S . -B build\windows -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build\windows || exit /b 1
ctest --test-dir build\windows --output-on-failure || exit /b 1
echo [mkcontract] Build OK
