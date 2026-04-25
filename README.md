# MilkyWayIME

MilkyWayIME is a Korean TSF IME project for Windows.

## Goals

- Separate the user's base layout from Korean composition.
- Convert input labels through the selected base layout to fixed QWERTY/libhangul tokens for Korean composition.
- Interpret shortcuts from the input labels reported by Windows/TSF.
- Support Hanja candidate selection from the current composing Korean syllable.
- Keep the project extensible for future custom layouts.

## libhangul

MilkyWayIME delegates Hangul composition to `libhangul`.

- TSF integration stays in the project.
- Layout selection, key normalization, shortcut resolution, and session state stay in the project.
- The actual Hangul composition engine is isolated behind `src/adapters/libhangul/`.
- `HANGUL_IC_OPTION_AUTO_REORDER` is enabled so reordered jamo input such as `ㅏ+ㅇ -> 아` follows the same composition behavior as `NavilIME`.
- The upstream dependency is included as the `external/libhangul` git submodule.
- `external/libhangul` remains an external dependency project in the solution.

## Current Status

This repository currently contains an early but manually testable TSF path:

- `Visual Studio 2026 Community` solution and project files
- `MilkyWayIME.Internal` as a build-reuse static library for repo-owned code
- `MilkyWayIME.Tsf` as the only production DLL project
- `src/engine` for layout, session, and shortcut model boundaries
- `src/tsf` for the initial Windows TSF composition lifecycle layer and the
  minimum COM text service runtime
- `src/adapters/libhangul` for the statically linked `libhangul` integration boundary
- `data/layouts` for data-driven layout definitions
- `tests` for unit, layout, and integration test structure

## Layout Model

Base layout data describes the user's current key-label arrangement. Its mapping
direction is `QWERTY/libhangul token position -> current base layout label`.
Omitted keys are identity mappings.

During Korean composition, MilkyWayIME inverts the selected base layout:

```text
input label -> QWERTY/libhangul token -> libhangul
```

For example, in Colemak-DH the fixed QWERTY `s` position is labeled `r`.
Therefore input label `R` maps back to libhangul token `s`, producing `ㄴ` in
two-beolsik. Shortcuts are resolved from the original input label, not from the
Hangul token.

## Initial Layout

```text
MilkyWayIME/
  MilkyWayIME.sln
  MilkyWayIME.*.vcxproj
  docs/
  external/libhangul/
  data/
  src/
  tests/
  tools/
  installer/
```

## Build

Visual Studio baseline:

- Visual Studio 2026 Community
- MSVC toolset `v145` installed with that IDE
- Windows SDK `10.0.26100.0`
- `MilkyWayIME.sln` as the only supported build entry point

Command line build:

```powershell
git submodule update --init --recursive
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" `
  ".\MilkyWayIME.sln" `
  /t:Build `
  /p:Configuration=Debug `
  /p:Platform=x64 `
  /m
```

This solution is Windows-only. CMake and cross-platform build paths are no longer used.

Current output paths:

- `build\MilkyWayIME.Tsf\x64\Debug\mwime_tsf.dll`
- `build\MilkyWayIME.Tests.Unit\x64\Debug\mwime_tests.exe`
- `build\MilkyWayIME.Tests.Tsf\x64\Debug\mwime_tsf_tests.exe`

## Developer Registration

After a `Debug|x64` solution build, register the development DLL with:

```cmd
tools\register-msvc-debug.cmd
```

Unregister it with:

```cmd
tools\unregister-msvc-debug.cmd
```

Run both scripts from an elevated Command Prompt.

The registration script copies `build\MilkyWayIME.Tsf\x64\Debug\mwime_tsf.dll` to
`%ProgramW6432%\MilkyWayIME\mwime_tsf.dll`, registers that installed copy
through `regsvr32`, and restarts `ctfmon.exe` so the TSF profile refreshes.

The unregister script defaults to `%ProgramW6432%\MilkyWayIME\mwime_tsf.dll`
and calls the DLL's `DllUnregisterServer` export through `regsvr32`.

## Manual Smoke Test

Use the current build only as a developer smoke test target.

1. Build `Debug|x64` from `MilkyWayIME.sln`.
2. Run an elevated Command Prompt and register the DLL with `tools/register-msvc-debug.cmd`.
3. Re-open the target app, for example Notepad.
4. Switch to the `MilkyWayIME` profile and verify:
   - Hangul composition starts, updates, and commits.
   - `Backspace`, `Space`, `Enter`, `.`, and `?` end or update composition as expected.
   - `VK_HANGUL` commits the current syllable and toggles IME open/close state.
   - `Ctrl+Shift+Space` commits the current syllable and toggles IME open/close state.
   - The language bar or tray input-mode indicator switches between `가` and `A`.
   - The profile icon uses the MilkyWayIME brand icon instead of the plain text fallback.
   - Losing focus commits the current syllable instead of dropping it.
   - The current composing last syllable is shown with a dotted underline when
     the target app honors TSF display attributes.

This stage still does not include candidate UI, Hanja selection UI, or an
installer.

## Test Execution

Run the test binaries directly after a solution build:

```powershell
.\build\MilkyWayIME.Tests.Unit\x64\Debug\mwime_tests.exe
.\build\MilkyWayIME.Tests.Tsf\x64\Debug\mwime_tsf_tests.exe
```
