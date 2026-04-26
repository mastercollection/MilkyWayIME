@echo off
setlocal

set "ROOT=%~dp0.."
set "CONFIGURATION=Debug"
set "PLATFORM=x64"
if not "%~1"=="" set "CONFIGURATION=%~1"
if not "%~2"=="" set "PLATFORM=%~2"
set "SKIP_LIBHANGUL_BUILD=NO"
if /i "%~3"=="--skip-libhangul-build" set "SKIP_LIBHANGUL_BUILD=YES"

set "SOURCE_DIR=%ROOT%\external\libhangul"
set "HANJA_DIR=%SOURCE_DIR%\data\hanja"
set "LIBHANGUL_PROJECT=%SOURCE_DIR%\libhangul.vcxproj"
set "LIBHANGUL_LIB=%ROOT%\build\libhangul\%PLATFORM%\%CONFIGURATION%\libhangul.lib"
set "TOOL_BUILD_DIR=%ROOT%\build\libhangul-tools\%PLATFORM%\%CONFIGURATION%"
set "HANJAC=%TOOL_BUILD_DIR%\hanjac.exe"
set "VSDEVCMD="
set "MSBUILD="
set "TARGET_ARCH=x64"
if /i "%PLATFORM%"=="Win32" set "TARGET_ARCH=x86"
if /i not "%PLATFORM%"=="x64" if /i not "%PLATFORM%"=="Win32" (
    echo [ERROR] Unsupported platform: %PLATFORM%
    exit /b 1
)
set "RUNTIME=/MDd"
set "LINK_OPTIONS=/DEBUG /INCREMENTAL"
if /i "%CONFIGURATION%"=="Release" set "RUNTIME=/MT"
if /i "%CONFIGURATION%"=="Release" set "LINK_OPTIONS=/OPT:REF /OPT:ICF"

for %%P in (
    "%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
    "%ProgramFiles%\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat"
    "%ProgramFiles%\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"
) do (
    if not defined VSDEVCMD if exist "%%~P" set "VSDEVCMD=%%~P"
)

for %%P in (
    "%ProgramFiles%\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
    "%ProgramFiles%\Microsoft Visual Studio\18\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe"
    "%ProgramFiles%\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe"
) do (
    if not defined MSBUILD if exist "%%~P" set "MSBUILD=%%~P"
)

if not defined MSBUILD (
    for /f "delims=" %%P in ('where MSBuild.exe 2^>nul') do (
        if not defined MSBUILD set "MSBUILD=%%~P"
    )
)

if not exist "%HANJA_DIR%\hanja.txt" (
    echo [ERROR] Hanja source file not found:
    echo %HANJA_DIR%\hanja.txt
    exit /b 1
)

if not exist "%HANJA_DIR%\mssymbol.txt" (
    echo [ERROR] Symbol source file not found:
    echo %HANJA_DIR%\mssymbol.txt
    exit /b 1
)

if not defined VSDEVCMD (
    echo [ERROR] Visual Studio developer command prompt was not found.
    exit /b 1
)

if not defined MSBUILD (
    echo [ERROR] MSBuild.exe was not found.
    exit /b 1
)

call "%VSDEVCMD%" -arch=%TARGET_ARCH% -host_arch=x64 >nul
if errorlevel 1 (
    echo [ERROR] Failed to initialize Visual Studio build environment.
    exit /b 1
)

if not exist "%TOOL_BUILD_DIR%" mkdir "%TOOL_BUILD_DIR%"
if errorlevel 1 (
    echo [ERROR] Failed to create tool build directory:
    echo %TOOL_BUILD_DIR%
    exit /b 1
)

if /i not "%SKIP_LIBHANGUL_BUILD%"=="YES" (
    set "MWIME_SKIP_HANJA_CACHE_POSTBUILD=1"
    "%MSBUILD%" "%LIBHANGUL_PROJECT%" /t:Build /p:Configuration=%CONFIGURATION% /p:Platform=%PLATFORM% /m:1 /nr:false
    if errorlevel 1 (
        set "MWIME_SKIP_HANJA_CACHE_POSTBUILD="
        echo [ERROR] Failed to build libhangul.
        exit /b 1
    )
    set "MWIME_SKIP_HANJA_CACHE_POSTBUILD="
)

if not exist "%LIBHANGUL_LIB%" (
    echo [ERROR] Built libhangul library was not found:
    echo %LIBHANGUL_LIB%
    exit /b 1
)

"%VCToolsInstallDir%bin\Hostx64\%TARGET_ARCH%\cl.exe" /nologo /W3 /utf-8 %RUNTIME% /Fo"%TOOL_BUILD_DIR%\hanjac.obj" /Fd"%TOOL_BUILD_DIR%\hanjac.pdb" /Fe"%HANJAC%" /Tc"%SOURCE_DIR%\tools\hanjac.c" /link "%LIBHANGUL_LIB%" %LINK_OPTIONS%
if errorlevel 1 (
    echo [ERROR] Failed to build hanjac.exe.
    exit /b 1
)

"%HANJAC%" "%HANJA_DIR%\hanja.txt" "%HANJA_DIR%\hanja.bin"
if errorlevel 1 (
    echo [ERROR] Failed to generate hanja.bin.
    exit /b 1
)

"%HANJAC%" "%HANJA_DIR%\mssymbol.txt" "%HANJA_DIR%\mssymbol.bin"
if errorlevel 1 (
    echo [ERROR] Failed to generate mssymbol.bin.
    exit /b 1
)

echo Generated:
echo %HANJA_DIR%\hanja.bin
echo %HANJA_DIR%\mssymbol.bin
exit /b 0
