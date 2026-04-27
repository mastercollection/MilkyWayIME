@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0.."
set "CONFIGURATION=Debug"
set "PLATFORM=All"
set "PROGRAMFILES_X86=%ProgramFiles(x86)%"
if not "%~1"=="" set "CONFIGURATION=%~1"
if not "%~2"=="" set "PLATFORM=%~2"

if /i not "%PLATFORM%"=="All" if /i not "%PLATFORM%"=="x64" if /i not "%PLATFORM%"=="Win32" (
    echo [ERROR] Unsupported platform: %PLATFORM%
    echo Use All, x64, or Win32.
    exit /b 1
)

set "SOURCE_HANJA_DIR=%ROOT%\external\libhangul\data\hanja"
set "CTFMON64=%SystemRoot%\System32\ctfmon.exe"
set "CTFMON32=%SystemRoot%\SysWOW64\ctfmon.exe"
set "REGISTER_WIN32=0"
if /i "%PLATFORM%"=="All" set "REGISTER_WIN32=1"
if /i "%PLATFORM%"=="Win32" set "REGISTER_WIN32=1"

net session >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Run this script as Administrator.
    exit /b 1
)

if not exist "%SOURCE_HANJA_DIR%\hanja.bin" (
    echo [ERROR] Required Hanja binary cache not found:
    echo %SOURCE_HANJA_DIR%\hanja.bin
    echo Run tools\generate-hanja-cache.cmd first.
    exit /b 1
)

if not exist "%SOURCE_HANJA_DIR%\mssymbol.bin" (
    echo [ERROR] Required symbol binary cache not found:
    echo %SOURCE_HANJA_DIR%\mssymbol.bin
    echo Run tools\generate-hanja-cache.cmd first.
    exit /b 1
)

taskkill /f /im ctfmon.exe >nul 2>&1

if /i "%PLATFORM%"=="All" (
    call :RegisterPlatform Win32
    if errorlevel 1 exit /b 1
    call :RegisterPlatform x64
    if errorlevel 1 exit /b 1
) else (
    call :RegisterPlatform "%PLATFORM%"
    if errorlevel 1 exit /b 1
)

start "" "%CTFMON64%"
if "%REGISTER_WIN32%"=="1" if exist "%CTFMON32%" start "" "%CTFMON32%"

exit /b 0

:RegisterPlatform
set "TARGET_PLATFORM=%~1"
set "BUILD_DLL=%ROOT%\build\MilkyWayIME.Tsf\%TARGET_PLATFORM%\%CONFIGURATION%\mwime_tsf.dll"
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
set "INSTALL_HANJA_DIR=%INSTALL_DIR%\data\hanja"

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

if exist "%INSTALL_DLL%" (
    "%REGSVR32%" /u /s "%INSTALL_DLL%"
    if errorlevel 1 (
        echo [ERROR] regsvr32 /u failed for:
        echo %INSTALL_DLL%
        exit /b 1
    )
)

copy /y "%BUILD_DLL%" "%INSTALL_DLL%" >nul
if errorlevel 1 (
    echo [ERROR] Failed to copy DLL to install directory:
    echo %INSTALL_DLL%
    exit /b 1
)

if not exist "%INSTALL_HANJA_DIR%" mkdir "%INSTALL_HANJA_DIR%"
if errorlevel 1 (
    echo [ERROR] Failed to create Hanja data directory:
    echo %INSTALL_HANJA_DIR%
    exit /b 1
)

copy /y "%SOURCE_HANJA_DIR%\hanja.bin" "%INSTALL_HANJA_DIR%\hanja.bin" >nul
if errorlevel 1 (
    echo [ERROR] Failed to copy hanja.bin to:
    echo %INSTALL_HANJA_DIR%
    exit /b 1
)

copy /y "%SOURCE_HANJA_DIR%\mssymbol.bin" "%INSTALL_HANJA_DIR%\mssymbol.bin" >nul
if errorlevel 1 (
    echo [ERROR] Failed to copy mssymbol.bin to:
    echo %INSTALL_HANJA_DIR%
    exit /b 1
)

"%REGSVR32%" /s "%INSTALL_DLL%"
if errorlevel 1 (
    echo [ERROR] regsvr32 failed for:
    echo %INSTALL_DLL%
    exit /b 1
)

echo Registered %INSTALL_DLL%
exit /b 0
