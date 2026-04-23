param(
    [string]$Configuration = "Debug",
    [string]$DllPath,
    [string]$InstallDir = "$env:ProgramW6432\MilkyWayIME"
)

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $DllPath) {
    $DllPath = Join-Path $InstallDir "mwime_tsf.dll"
}

$windowsIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
$windowsPrincipal = [Security.Principal.WindowsPrincipal]::new($windowsIdentity)
$isAdministrator = $windowsPrincipal.IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdministrator) {
    throw "Run this script as Administrator."
}

$resolvedDll = Resolve-Path $DllPath -ErrorAction Stop
$regsvr32 = Join-Path $env:SystemRoot "System32\regsvr32.exe"

$process = Start-Process -FilePath $regsvr32 `
    -ArgumentList @("/u", "/s", $resolvedDll.Path) `
    -PassThru `
    -Wait
if ($process.ExitCode -ne 0) {
    throw "regsvr32 /u failed with exit code $($process.ExitCode) for $($resolvedDll.Path)"
}

Write-Host "Unregistered $($resolvedDll.Path)"
