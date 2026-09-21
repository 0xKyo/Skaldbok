@echo off
rem Opens Skaldbok. The app starts the players' web server by itself (Web module), so this is all that is needed:
rem the players' links are in the app (Web), and their pages work as long as the app is open.
setlocal
cd /d "%~dp0"

set "APP=%~dp0skaldbok.exe"
if not exist "%APP%" set "APP=%~dp0build\release\skaldbok.exe"
if not exist "%APP%" (
  echo Could not find skaldbok.exe.
  echo Build it first:  powershell -ExecutionPolicy Bypass -File scripts\build.ps1
  pause
  exit /b 1
)

rem The players' page (Vue): built once, if it is missing and Node.js is installed.
if not exist "web\index.html" if not exist "web\client\dist\index.html" (
  where npm >nul 2>nul
  if errorlevel 1 (
    echo Note: the players' page is not built and Node.js is not installed, so the web link will show an empty page.
    echo Install Node.js from https://nodejs.org and run this file again.
  ) else (
    echo Building the players' web page ^(first time only^)...
    pushd web
    call npm install
    call npm run build
    popd
  )
)

start "" "%APP%"
