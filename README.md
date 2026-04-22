# MilkyWayIME

MilkyWayIME is a Korean TSF IME project for Windows.

## Goals

- Separate the physical English layout from the Korean layout mapping.
- Interpret shortcuts consistently from the selected physical English layout.
- Support Hanja candidate selection from the current composing Korean string.
- Keep the project extensible for future custom layouts.

## libhangul

MilkyWayIME delegates Hangul composition to `libhangul`.

- TSF integration stays in the project.
- Layout selection, key normalization, shortcut resolution, and session state stay in the project.
- The actual Hangul composition engine is isolated behind `src/adapters/libhangul/`.
- The upstream dependency is included as the `external/libhangul` git submodule.
- `mwime_core` statically links the bundled `libhangul` sources during the project build.

## Current Status

This repository currently contains the initial project skeleton:

- `CMake` root build files
- `src/engine` for layout, session, and shortcut model boundaries
- `src/tsf` for the future Windows TSF adapter layer
- `src/adapters/libhangul` for the statically linked `libhangul` integration boundary
- `data/layouts` for data-driven layout definitions
- `tests` for unit, layout, and integration test structure

## Initial Layout

```text
MilkyWayIME/
  CMakeLists.txt
  CMakePresets.json
  docs/
  external/libhangul/
  data/
  src/
  tests/
  tools/
  installer/
```

## Build

Configure:

```sh
git submodule update --init --recursive
cmake --preset debug
```

Build:

```sh
cmake --build --preset debug
```

Run tests:

```sh
ctest --preset debug
```

Use the MSVC presets when you want `Ninja` builds backed by the latest
installed Visual Studio x64 toolchain:

```sh
cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --preset msvc-debug

cmake --preset msvc-release
cmake --build --preset msvc-release
ctest --preset msvc-release
```

For now, run the `msvc-*` build and test presets from a Visual Studio
Developer Command Prompt or Developer PowerShell so the MSVC include and
library environment is available to `Ninja`.
