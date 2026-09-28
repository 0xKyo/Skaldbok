@echo off
rem Build.bat — compiles the entire Skaldbok project:
rem   1. Vue web client  (npm run build, skipped if Node is not installed)
rem   2. C++ release     (scripts\build.ps1)
rem
rem Usage:
rem   Build.bat           — full release build
rem   Build.bat --test    — release build + run tests
rem   Build.bat --clean   — wipe build\release before building
setlocal

cd /d "%~dp0"

set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
set "PS_SCRIPT=%~dp0scripts\build.ps1"
set "PS_FLAGS="
if /i "%~1"=="--test"  set "PS_FLAGS=-Test"
if /i "%~1"=="--clean" set "PS_FLAGS=-Clean"

rem ------------------------------------------------------------------ web client
npm --version >nul 2>nul
if errorlevel 1 goto no_node

echo [web]  Building Vue client...
pushd "%~dp0web\client"
call npm install
if errorlevel 1 ( echo [web]  npm install failed. & popd & exit /b 1 )
call npm run build
if errorlevel 1 ( echo [web]  Vue build failed. & popd & exit /b 1 )
popd
goto build_cpp

:no_node
echo [web]  Node.js not found -- skipping Vue build.
echo        Install from https://nodejs.org to include the web client.

:build_cpp
echo.
echo [cpp]  Building C++ release...
"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%PS_SCRIPT%" %PS_FLAGS%
if errorlevel 1 ( echo [cpp]  C++ build failed. & exit /b 1 )

echo.
echo Done. Executables are in build\release\
