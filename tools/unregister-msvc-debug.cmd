@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "PLATFORM=All"
set "PROGRAMFILES_X86=%ProgramFiles(x86)%"
if not "%~1"=="" set "PLATFORM=%~1"

if /i not "%PLATFORM%"=="All" if /i not "%PLATFORM%"=="x64" if /i not "%PLATFORM%"=="Win32" (
    echo [ERROR] Unsupported platform: %PLATFORM%
    echo Use All, x64, or Win32.
    exit /b 1
)

net session >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Run this script as Administrator.
    exit /b 1
)

if /i "%PLATFORM%"=="All" (
    call :UnregisterPlatform Win32 optional
    if errorlevel 1 exit /b 1
    call :UnregisterPlatform x64 optional
    if errorlevel 1 exit /b 1
    exit /b 0
)

call :UnregisterPlatform "%PLATFORM%" required
exit /b %ERRORLEVEL%

:UnregisterPlatform
set "TARGET_PLATFORM=%~1"
set "MISSING_MODE=%~2"
set "INSTALL_DIR=%ProgramW6432%\MilkyWayIME"
set "REGSVR32=%SystemRoot%\System32\regsvr32.exe"
if /i "%TARGET_PLATFORM%"=="Win32" (
    if "!PROGRAMFILES_X86!"=="" (
        echo [ERROR] ProgramFilesX86 is not available on this system.
        exit /b 1
    )
    set "INSTALL_DIR=!PROGRAMFILES_X86!\MilkyWayIME"
    set "REGSVR32=%SystemRoot%\SysWOW64\regsvr32.exe"
)
set "INSTALL_DLL=%INSTALL_DIR%\mwime_tsf.dll"

if not exist "%INSTALL_DLL%" (
    if /i "%MISSING_MODE%"=="optional" (
        echo [WARN] Installed DLL not found for %TARGET_PLATFORM%:
        echo %INSTALL_DLL%
        exit /b 0
    )
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
