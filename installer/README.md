# Installer

Installer and registration packaging assets belong here.

The first installer target is an x64 WiX v6 MSI. It installs the Release TSF
DLL under `%ProgramFiles%\MilkyWayIME`, installs the binary libhangul Hanja
cache files under `%ProgramData%\MilkyWayIME\data\hanja`, and installs base
layout JSON samples under `%ProgramFiles%\MilkyWayIME\data\layouts\base`.

Build it from the repository root with:

```powershell
.\installer\build-installer.ps1
```

For a fast rebuild that only repackages already-built binaries:

```powershell
.\installer\build-installer.ps1 -SkipSolutionBuild
```

The script suppresses WiX/MSI ICE validation by default because that step can
take minutes even when the MSI has already been produced. Run full validation
explicitly when needed:

```powershell
.\installer\build-installer.ps1 -SkipSolutionBuild -ValidateMsi
```

The script builds `MilkyWayIME.sln` as `Release|x64`, verifies the required data
files, then builds:

```text
build\installer\bin\Release\MilkyWayIME.msi
```

GitHub Actions uses the same script from
`.github\workflows\build-installer.yml`. The workflow resolves the MSI
`ProductVersion` automatically:

- tag `v1.2.3` -> `1.2.3`
- manual `workflow_dispatch` input -> that exact version
- other runs -> `0.1.<GITHUB_RUN_NUMBER>`

The workflow runs on GitHub's `windows-2025-vs2026` image and prints the
installed MSVC toolsets before building. If that runner image stops providing
Visual Studio 2026 / MSVC v145, it fails before the build with a clear toolset
error.

The MSI runs `regsvr32` as an elevated deferred custom action:

- install: `regsvr32 /s "%ProgramFiles%\MilkyWayIME\mwime_tsf.dll"`
- uninstall: `regsvr32 /u /s "%ProgramFiles%\MilkyWayIME\mwime_tsf.dll"`

`DllRegisterServer` performs COM registration plus TSF profile/category
registration. `DllUnregisterServer` removes those registrations.

Current packaged files:

- `build\MilkyWayIME.Tsf\x64\Release\mwime_tsf.dll`
- `external\libhangul\data\hanja\hanja.bin` as `%ProgramData%\MilkyWayIME\data\hanja\hanja.bin`
- `external\libhangul\data\hanja\mssymbol.bin` as `%ProgramData%\MilkyWayIME\data\hanja\mssymbol.bin`
- `data\layouts\base\us_qwerty.json` as `data\layouts\base\us_qwerty.json`
- `data\layouts\base\colemak.json` as `data\layouts\base\colemak.json`

The runtime lookup path prefers `%ProgramData%\MilkyWayIME\data\hanja`, then
falls back to the legacy DLL-adjacent `data\hanja` path, then to the development
source tree. The runtime loads binary cache files only; text sources are
development inputs for regenerating those binary caches.

The installed layout JSON files are packaged samples/reference files. The
current runtime still provides `us_qwerty` and `colemak` as built-in layouts and
loads user custom base layouts from `%APPDATA%\MilkyWayIME\layouts\base`.

Limitations of this first MSI:

- x64 only.
- It registers the TSF profile but does not call `InstallLayoutOrTip` to add or
  enable the input method for the current user.
- It does not implement DIME-style locked-DLL rename/copy-back handling. Close
  applications using the IME before upgrade or uninstall.

Release binaries are built with the static MSVC runtime (`/MT`), so the MSI does
not need to bundle or download the Visual C++ Redistributable.

For development registration, the existing scripts are still available:
[tools/register-msvc-debug.cmd](/D:/Git/MilkyWayIME/tools/register-msvc-debug.cmd)
and [tools/unregister-msvc-debug.cmd](/D:/Git/MilkyWayIME/tools/unregister-msvc-debug.cmd).
