@echo off
setlocal

set "ROOT=%~dp0.."
set "CONFIGURATION=Debug"
set "PLATFORM=x64"
if not "%~1"=="" set "CONFIGURATION=%~1"
if not "%~2"=="" set "PLATFORM=%~2"

set "BUILD_DLL=%ROOT%\build\MilkyWayIME.Tsf\%PLATFORM%\%CONFIGURATION%\mwime_tsf.dll"
set "INSTALL_DIR=%ProgramW6432%\MilkyWayIME"
set "INSTALL_DLL=%INSTALL_DIR%\mwime_tsf.dll"
set "REGSVR32=%SystemRoot%\System32\regsvr32.exe"
set "CTFMON=%SystemRoot%\System32\ctfmon.exe"

net session >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Run this script as Administrator.
    exit /b 1
)

if not exist "%BUILD_DLL%" (
    echo [ERROR] Built DLL not found:
    echo %BUILD_DLL%
    exit /b 1
)

if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"
if errorlevel 1 (
    echo [ERROR] Failed to create install directory:
    echo %INSTALL_DIR%
    exit /b 1
)

taskkill /f /im ctfmon.exe >nul 2>&1

if exist "%INSTALL_DLL%" (
    "%REGSVR32%" /u /s "%INSTALL_DLL%"
)

copy /y "%BUILD_DLL%" "%INSTALL_DLL%" >nul
if errorlevel 1 (
    echo [ERROR] Failed to copy DLL to install directory:
    echo %INSTALL_DLL%
    exit /b 1
)

"%REGSVR32%" /s "%INSTALL_DLL%"
if errorlevel 1 (
    echo [ERROR] regsvr32 failed for:
    echo %INSTALL_DLL%
    exit /b 1
)

start "" "%CTFMON%"

echo Registered %INSTALL_DLL%
exit /b 0
