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

foreach ($name in @("hanja.bin", "mssymbol.bin")) {
    $source = Join-Path $sourceHanjaDir $name
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Required Hanja binary cache not found: $source. Run tools\generate-hanja-cache.cmd first."
    }
    Copy-Item -LiteralPath $source `
        -Destination (Join-Path $installedHanjaDir $name) `
        -Force
}

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
