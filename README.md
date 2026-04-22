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

## Current Status

This repository currently contains the initial project skeleton:

- `CMake` root build files
- `src/engine` for layout, session, and shortcut model boundaries
- `src/tsf` for the future Windows TSF adapter layer
- `src/adapters/libhangul` for the `libhangul` integration boundary
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
