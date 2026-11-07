# CLAUDE.md

Guidance for Claude Code working in this repository.

## Working Rules (read first)

`AGENTS.md` is the authoritative working contract for this repo. Key points:

- **Respond to the user in Korean.** All assistant responses must be Korean.
- **Get confirmation before writing or modifying code.** Explain the intended
  change first; for design/behavior/tradeoff decisions, ask before assuming.
- State constraints, risks, and unknowns explicitly. Avoid unsupported optimism.
- **Antivirus (Bitdefender) constraints:** avoid PowerShell unless necessary,
  no encoded/obfuscated commands, no long file-rewriting one-liners, no chaining
  many commands, never touch registry/startup/scheduled-tasks/security settings.
  Prefer short, explicit `dotnet`/`git`/`msbuild` commands.

## What This Is

MilkyWayIME is a Korean TSF IME for Windows (native C++, Visual Studio 2026).
Hangul composition is delegated to the `external/libhangul` submodule; layout
selection, key normalization, shortcut resolution, TSF integration, and session
state stay in this project.

Core layout model: a *base layout* maps fixed QWERTY/libhangul token positions
to the user's current key labels. Korean composition **inverts** the base layout
(`input label -> QWERTY/libhangul token -> libhangul`). Shortcuts are resolved
from the original input label, never the Hangul token. See README.md "Layout
Model" for the worked Colemak example.

## Build

Windows-only; `MilkyWayIME.sln` is the only supported build entry point.
Authoritative validation requires **elevated/admin MSBuild** (non-elevated
results are not authoritative per AGENTS.md).

```powershell
git submodule update --init --recursive
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" `
  ".\MilkyWayIME.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /m
```

Outputs:
- `build\MilkyWayIME.Tsf\x64\Debug\mwime_tsf.dll` — the only production DLL
- `build\MilkyWayIME.Tests.Unit\x64\Debug\mwime_tests.exe`
- `build\MilkyWayIME.Tests.Tsf\x64\Debug\mwime_tsf_tests.exe`

`MilkyWayIME.Internal` is a build-reuse static library for repo-owned code.

## Test

Run the test binaries directly after a build:

```powershell
.\build\MilkyWayIME.Tests.Unit\x64\Debug\mwime_tests.exe
.\build\MilkyWayIME.Tests.Tsf\x64\Debug\mwime_tsf_tests.exe
```

For manual TSF smoke testing, register the debug DLL from an **elevated** prompt
with `tools\register-msvc-debug.cmd` (unregister with the matching script), then
follow README.md "Manual Smoke Test".

## Source Layout

- `src/engine/` — layout, session, shortcut, key, state, composition, hanja
  (engine logic, kept separate from TSF glue)
- `src/tsf/` — TSF text service runtime: composition lifecycle, candidate UI
  integration, edit sessions, display attributes, language bar, registration,
  settings, host compatibility, diagnostics
- `src/adapters/libhangul/` — the libhangul integration boundary
- `src/adapters/dictionary/` — static Hanja/symbol dictionary lookup
  (compiled-in tables via `generated_hanja_data.cpp`)
- `src/ui/` — candidate, composition, and notify presentation windows
- `data/layouts/` — base layout JSON samples; `data/dictionaries/`
- `tests/` — `unit`, `layout`, `integration`
- `tools/` — keyboard-matrix, host diagnostics probes (uia-probe,
  text-surface-probe, message-trace, focus-watch), registration scripts
- `docs/` — architecture notes; `host-compatibility.md` (read before changing
  host input/diagnostics behavior — do not repeat recorded experiments)

## Settings & Registration

Selected layout IDs are persisted under `HKCU\Software\MilkyWayIME\Settings`
(`BaseLayoutId`, `KoreanLayoutId` only). Built-in base layouts: `us_qwerty`,
`colemak`. Korean layouts use `libhangul:<id>` form (e.g. `libhangul:2`,
`libhangul:3f`). Runtime base-layout JSON loads once at service creation from
`%APPDATA%\MilkyWayIME\layouts\base`; restart the target app after editing.

## Conventions

- Keep input state, layout definitions, key conversion, and shortcut resolution
  separated. Distinguish input labels, virtual keys, QWERTY/libhangul tokens,
  and modifier state explicitly — do not rely only on resulting characters.
- Prefer data-driven structures over hardcoding built-in layouts (future custom
  layout support). Layout-specific exceptions get an explicit, documented branch.
- Reflect new layouts, mapping rules, exceptions, and limitations in docs
  alongside code.
- Reference projects live under `references/` (DIME, windows-chewing-tsf,
  MetasequoiaImeTsf, kime) — extract structural lessons, don't copy
  implementation. See AGENTS.md "Reference Projects".
