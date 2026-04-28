param(
    [string]$Configuration = "Debug",
    [string]$Platform = "All",
    [string]$DllPath,
    [string]$InstallDir,
    [string]$HanjaDataDir
)

$repoRoot = Split-Path -Parent $PSScriptRoot

function Get-TargetPlatforms {
    param([string]$RequestedPlatform)

    if ($RequestedPlatform -ieq "All") {
        return @("Win32", "x64")
    }
    if ($RequestedPlatform -ieq "x64") {
        return @("x64")
    }
    if ($RequestedPlatform -ieq "Win32") {
        return @("Win32")
    }

    throw "Unsupported platform '$RequestedPlatform'. Use All, x64, or Win32."
}

function Get-DefaultInstallDir {
    param([string]$TargetPlatform)

    if ($TargetPlatform -ieq "Win32") {
        $programFilesX86 = ${env:ProgramFiles(x86)}
        if (-not $programFilesX86) {
            throw "ProgramFiles(x86) is not available on this system."
        }
        return Join-Path $programFilesX86 "MilkyWayIME"
    }

    if (-not $env:ProgramW6432) {
        throw "ProgramW6432 is not available on this system."
    }
    return Join-Path $env:ProgramW6432 "MilkyWayIME"
}

function Get-Regsvr32Path {
    param([string]$TargetPlatform)

    $regsvr32Folder = if ($TargetPlatform -ieq "Win32") { "SysWOW64" } else { "System32" }
    return Join-Path $env:SystemRoot "$regsvr32Folder\regsvr32.exe"
}

$targetPlatforms = @(Get-TargetPlatforms $Platform)
if (($targetPlatforms.Count -gt 1) -and ($DllPath -or $InstallDir)) {
    throw "DllPath and InstallDir can only be used when Platform is x64 or Win32."
}

if (-not $HanjaDataDir) {
    if (-not $env:ProgramData) {
        throw "ProgramData is not available on this system."
    }
    $HanjaDataDir = Join-Path $env:ProgramData "MilkyWayIME\data\hanja"
}

$windowsIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
$windowsPrincipal = [Security.Principal.WindowsPrincipal]::new($windowsIdentity)
$isAdministrator = $windowsPrincipal.IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdministrator) {
    throw "Run this script as Administrator."
}

$sourceHanjaDir = Join-Path $repoRoot "external\libhangul\data\hanja"
$installedHanjaDir = $HanjaDataDir

New-Item -ItemType Directory -Path $installedHanjaDir -Force | Out-Null

foreach ($name in @("hanja.bin", "mssymbol.bin")) {
    $source = Join-Path $sourceHanjaDir $name
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Required Hanja binary cache not found: $source. Run tools\generate-hanja-cache.cmd first."
    }
    Copy-Item -LiteralPath $source `
        -Destination (Join-Path $installedHanjaDir $name) `
        -Force
}

Get-Process -Name "ctfmon" -ErrorAction SilentlyContinue | Stop-Process -Force

foreach ($targetPlatform in $targetPlatforms) {
    $targetDllPath = $DllPath
    if (-not $targetDllPath) {
        $targetDllPath = Join-Path $repoRoot "build\MilkyWayIME.Tsf\$targetPlatform\$Configuration\mwime_tsf.dll"
    }
    $targetInstallDir = $InstallDir
    if (-not $targetInstallDir) {
        $targetInstallDir = Get-DefaultInstallDir $targetPlatform
    }

    $resolvedDll = Resolve-Path $targetDllPath -ErrorAction Stop
    $regsvr32 = Get-Regsvr32Path $targetPlatform
    $installedDll = Join-Path $targetInstallDir "mwime_tsf.dll"

    New-Item -ItemType Directory -Path $targetInstallDir -Force | Out-Null

    if (Test-Path -LiteralPath $installedDll) {
        $unregisterProcess = Start-Process -FilePath $regsvr32 `
            -ArgumentList @("/u", "/s", $installedDll) `
            -PassThru `
            -Wait
        if ($unregisterProcess.ExitCode -ne 0) {
            throw "regsvr32 /u failed with exit code $($unregisterProcess.ExitCode) for $installedDll"
        }
    }

    Copy-Item -LiteralPath $resolvedDll.Path -Destination $installedDll -Force

    $registerProcess = Start-Process -FilePath $regsvr32 `
        -ArgumentList @("/s", $installedDll) `
        -PassThru `
        -Wait
    if ($registerProcess.ExitCode -ne 0) {
        throw "regsvr32 failed with exit code $($registerProcess.ExitCode) for $installedDll"
    }

    Write-Host "Registered $installedDll"
}

$ctfmon64 = Join-Path $env:SystemRoot "System32\ctfmon.exe"
Start-Process -FilePath $ctfmon64 | Out-Null
if ($targetPlatforms -contains "Win32") {
    $ctfmon32 = Join-Path $env:SystemRoot "SysWOW64\ctfmon.exe"
    if (Test-Path -LiteralPath $ctfmon32) {
        Start-Process -FilePath $ctfmon32 | Out-Null
    }
}
