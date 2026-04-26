# MilkyWayIME

MilkyWayIME is a Korean TSF IME project for Windows.

한국어 문서는 [README.ko.md](README.ko.md)를 참고하세요.

## Goals

- Separate the user's base layout from Korean composition.
- Convert input labels through the selected base layout to fixed QWERTY/libhangul tokens for Korean composition.
- Interpret shortcuts from the input labels reported by Windows/TSF.
- Support Hanja and symbol candidate selection from the current composing
  single Hangul unit.
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
- `src/adapters/dictionary` for libhangul Hanja and symbol dictionary lookup
- `src/ui/candidate` and `src/tsf/candidate` for candidate list presentation
  and TSF UI element integration
- `data/layouts` for layout schema samples and future custom layout data
- `tests` for unit, layout, and integration test structure

## Layout Model

Base layout data describes the user's current key-label arrangement. Its mapping
direction is `QWERTY/libhangul token position -> current base layout label`.
Omitted keys are identity mappings.

During Korean composition, MilkyWayIME inverts the selected base layout:

```text
input label -> QWERTY/libhangul token -> libhangul
```

For example, in Colemak the fixed QWERTY `s` position is labeled `r`.
Therefore input label `R` maps back to libhangul token `s`, producing `ㄴ` in
two-beolsik. Shortcuts are resolved from the original input label, not from the
Hangul token.

## Settings

MilkyWayIME exposes the current user settings through the language bar menu.
The supported built-in base layouts are `us_qwerty` and `colemak`. Korean
layouts use `libhangul:<id>` IDs such as `libhangul:2` and `libhangul:3f`.
The selected layouts are stored under
`HKCU\Software\MilkyWayIME\Settings` and loaded when the text service starts.
Only the selected IDs are persisted:

```text
BaseLayoutId
KoreanLayoutId
```

Layout definitions, libhangul XML, and mapping tables are not copied into the
settings store.

There is no dedicated settings window yet, and `Ctrl+Alt+O` is intentionally not
registered as a settings shortcut.

## Custom Layout Formats

Future custom Korean layouts should be written as libhangul XML, not as
MilkyWayIME JSON. The XML `id` becomes the MilkyWayIME Korean layout ID through
the `libhangul:<id>` form; for example, XML `id="my-sebeol"` becomes
`libhangul:my-sebeol`.

Future custom base layouts should use MilkyWayIME JSON under the same direction
as the built-in samples:

```json
{
  "id": "my_colemak_variant",
  "displayName": "내 콜맥 변형",
  "keys": {
    "s": "r"
  }
}
```

The base JSON loader is connected to unit tests, the `keyboard-matrix`
development tool, and the TSF runtime. The TSF runtime loads base layout JSON
once when the text service is created from:

```text
%APPDATA%\MilkyWayIME\layouts\base
```

Malformed files are skipped independently. Duplicate base layout IDs override
earlier definitions, including built-in IDs. Existing TSF instances do not
reload JSON automatically; restart the target app after changing layout files.

Current builds still compile `external/libhangul` with `ExternalKeyboard=NO`
and `ENABLE_EXTERNAL_KEYBOARDS=0`, so runtime custom Korean XML loading remains
a separate future step.

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
through `regsvr32`, copies required `hanja.bin` and `mssymbol.bin` into
`%ProgramW6432%\MilkyWayIME\data\hanja`, and restarts `ctfmon.exe` so the TSF
profile refreshes. Runtime Hanja lookup loads the binary cache only; text
sources are used to regenerate the binary cache during development.

Regenerate the source-tree Hanja binary caches with:

```cmd
tools\generate-hanja-cache.cmd
```

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
   - The language bar or tray input-mode indicator switches between `가` and `A`.
   - The language bar menu can switch base and Korean layouts and keeps the selection
     under `HKCU\Software\MilkyWayIME\Settings`.
   - The profile icon uses the MilkyWayIME brand icon instead of the plain text fallback.
   - Losing focus commits the current syllable instead of dropping it.
   - The current composing last syllable is shown with a dotted underline when
     the target app honors TSF display attributes.
   - `한` followed by the Hanja key opens Hanja candidates, number keys or
     `Enter` commit the selected candidate, and `Esc` keeps the composing text.
   - A selected Hangul prefix opens Hanja candidates; a selected Hanja prefix
     opens Hangul reverse-conversion candidates.
   - With no selection or composition, the Hanja key converts the Hangul or Hanja
     run immediately before the caret, and repeated Hanja key presses cycle
     through dictionary-resolved segments without committing the current
     candidate.
   - `ㅁ` followed by the Hanja key opens symbol candidates including `※`.
   - Candidate colors follow the Windows app light/dark theme and high contrast
     system colors.

This stage still does not include a user-facing installer. Candidate lookup is
exact dictionary lookup over the selected prefix or caret run segment; morphology,
particle splitting, and selection-internal searching remain out of scope.

## Test Execution

Run the test binaries directly after a solution build:

```powershell
.\build\MilkyWayIME.Tests.Unit\x64\Debug\mwime_tests.exe
.\build\MilkyWayIME.Tests.Tsf\x64\Debug\mwime_tsf_tests.exe
```
