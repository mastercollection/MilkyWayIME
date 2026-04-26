param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64",
    [string]$DllPath,
    [string]$InstallDir = "$env:ProgramW6432\MilkyWayIME"
)

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $DllPath) {
    $DllPath = Join-Path $repoRoot "build\MilkyWayIME.Tsf\$Platform\$Configuration\mwime_tsf.dll"
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
$installedDll = Join-Path $InstallDir "mwime_tsf.dll"
$sourceHanjaDir = Join-Path $repoRoot "external\libhangul\data\hanja"
$installedHanjaDir = Join-Path $InstallDir "data\hanja"

New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
Copy-Item -LiteralPath $resolvedDll.Path -Destination $installedDll -Force
New-Item -ItemType Directory -Path $installedHanjaDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $sourceHanjaDir "hanja.txt") `
    -Destination (Join-Path $installedHanjaDir "hanja.txt") `
    -Force
Copy-Item -LiteralPath (Join-Path $sourceHanjaDir "mssymbol.txt") `
    -Destination (Join-Path $installedHanjaDir "mssymbol.txt") `
    -Force

$process = Start-Process -FilePath $regsvr32 `
    -ArgumentList @("/s", $installedDll) `
    -PassThru `
    -Wait
if ($process.ExitCode -ne 0) {
    throw "regsvr32 failed with exit code $($process.ExitCode) for $installedDll"
}

Get-Process -Name "ctfmon" -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Process -FilePath (Join-Path $env:SystemRoot "System32\ctfmon.exe") | Out-Null

Write-Host "Registered $installedDll"
