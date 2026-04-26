param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$ProductVersion = "0.1.0",
    [switch]$SkipSolutionBuild,
    [switch]$ValidateMsi
)

$ErrorActionPreference = "Stop"

if ($Platform -ne "x64") {
    throw "The current installer is x64-only. Use -Platform x64."
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$solutionPath = Join-Path $repoRoot "MilkyWayIME.sln"
$wixProjectPath = Join-Path $PSScriptRoot "MilkyWayIME.wixproj"

function Find-MSBuild {
    $vsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vsWhere) {
        $installPath = & $vsWhere -latest -products * `
            -requires Microsoft.Component.MSBuild `
            -property installationPath
        if ($installPath) {
            $candidate = Join-Path $installPath "MSBuild\Current\Bin\amd64\MSBuild.exe"
            $vcToolsRoot = Join-Path $installPath "VC\Tools\MSVC"
            $v145Tools = Get-ChildItem $vcToolsRoot -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -like "14.5*" } |
                Select-Object -First 1
            if ((Test-Path -LiteralPath $candidate) -and $v145Tools) {
                return $candidate
            }
        }
    }

    $known = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
    $knownTools = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC"
    if ((Test-Path -LiteralPath $known) -and
        (Get-ChildItem $knownTools -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like "14.5*" } |
            Select-Object -First 1)) {
        return $known
    }

    throw "MSBuild with MSVC v145 was not found. Install Visual Studio 2026 or Build Tools with the v145 toolset."
}

if (-not $SkipSolutionBuild) {
    $msbuild = Find-MSBuild
    & $msbuild $solutionPath /t:Build /p:Configuration=$Configuration /p:Platform=$Platform /m
    if ($LASTEXITCODE -ne 0) {
        throw "Solution build failed with exit code $LASTEXITCODE."
    }
}

$requiredFiles = @(
    "build\MilkyWayIME.Tsf\x64\$Configuration\mwime_tsf.dll",
    "external\libhangul\data\hanja\hanja.bin",
    "external\libhangul\data\hanja\mssymbol.bin",
    "data\layouts\base\us_qwerty.json",
    "data\layouts\base\colemak.json"
)

foreach ($relativePath in $requiredFiles) {
    $path = Join-Path $repoRoot $relativePath
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required installer input not found: $path"
    }
}

$suppressValidation = if ($ValidateMsi) { "false" } else { "true" }

& dotnet build $wixProjectPath `
    -c Release `
    /p:ProductVersion=$ProductVersion `
    /p:Platform=x64 `
    /p:BuildConfiguration=$Configuration `
    /p:BuildPlatform=$Platform `
    /p:SuppressValidation=$suppressValidation
if ($LASTEXITCODE -ne 0) {
    throw "WiX MSI build failed with exit code $LASTEXITCODE."
}

$msiPath = Join-Path $repoRoot "build\installer\bin\Release\MilkyWayIME.msi"
if (-not (Test-Path -LiteralPath $msiPath)) {
    throw "Expected MSI was not produced: $msiPath"
}

Write-Host "Built installer: $msiPath"
