param(
    [string]$Configuration = "Debug",
    [string]$Platform = "All",
    [string]$DllPath,
    [string]$InstallDir
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

$windowsIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
$windowsPrincipal = [Security.Principal.WindowsPrincipal]::new($windowsIdentity)
$isAdministrator = $windowsPrincipal.IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdministrator) {
    throw "Run this script as Administrator."
}

foreach ($targetPlatform in $targetPlatforms) {
    $targetInstallDir = $InstallDir
    if (-not $targetInstallDir) {
        $targetInstallDir = Get-DefaultInstallDir $targetPlatform
    }
    $targetDllPath = $DllPath
    if (-not $targetDllPath) {
        $targetDllPath = Join-Path $targetInstallDir "mwime_tsf.dll"
    }

    if (-not (Test-Path -LiteralPath $targetDllPath)) {
        if ($targetPlatforms.Count -gt 1) {
            Write-Warning "Installed DLL not found for ${targetPlatform}: $targetDllPath. Skipping."
            continue
        }
        throw "Installed DLL not found: $targetDllPath"
    }

    $resolvedDll = Resolve-Path $targetDllPath -ErrorAction Stop
    $regsvr32 = Get-Regsvr32Path $targetPlatform

    $process = Start-Process -FilePath $regsvr32 `
        -ArgumentList @("/u", "/s", $resolvedDll.Path) `
        -PassThru `
        -Wait
    if ($process.ExitCode -ne 0) {
        throw "regsvr32 /u failed with exit code $($process.ExitCode) for $($resolvedDll.Path)"
    }

    Write-Host "Unregistered $($resolvedDll.Path)"
}
