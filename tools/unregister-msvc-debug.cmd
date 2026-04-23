@echo off
setlocal

set "INSTALL_DIR=%ProgramW6432%\MilkyWayIME"
set "INSTALL_DLL=%INSTALL_DIR%\mwime_tsf.dll"
set "REGSVR32=%SystemRoot%\System32\regsvr32.exe"

net session >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Run this script as Administrator.
    exit /b 1
)

if not exist "%INSTALL_DLL%" (
    echo [ERROR] Installed DLL not found:
    echo %INSTALL_DLL%
    exit /b 1
)

"%REGSVR32%" /u /s "%INSTALL_DLL%"
if errorlevel 1 (
    echo [ERROR] regsvr32 /u failed for:
    echo %INSTALL_DLL%
    exit /b 1
)

echo Unregistered %INSTALL_DLL%
exit /b 0
