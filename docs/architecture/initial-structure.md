# Initial Structure

MilkyWayIME starts from four explicit boundaries:

- `engine`: input state, layout model, shortcut resolution, and session flow
- `adapters`: third-party or external integration boundaries such as `libhangul`
- `tsf`: Windows TSF service integration, edit sessions, display attributes, and registration
- `ui`: candidate and notification UI layers that consume engine-facing models

## Rules

- `engine` must not depend on TSF, COM, or Win32 UI headers.
- `tsf` must normalize key input into an engine-facing event before calling the engine.
- `adapters/libhangul` is the only place where `libhangul` integration should live.
- `MilkyWayIME.Internal` is a build-reuse static library, not an architecture boundary.
- Folder boundaries under `src/` remain the source of truth for subsystem ownership.
- Built-in layout definitions belong in engine-owned data tables, not TSF classes.
  `data/layouts` remains the schema/sample home for future custom layout files.
- Hanja conversion requests must operate on the current composing syllable only.
- `base layout` means the user's current key-label arrangement as seen through Windows/TSF input labels. It is not a raw hardware switch matrix.
- TSF key events are normalized first to an `input_label_key`. Shortcuts are resolved from that input label and modifier state.
- Hangul composition uses the selected base layout's inverse map to convert the input label into the fixed QWERTY/libhangul token before forwarding input to `libhangul`.
- `Shift` is part of the Hangul token only for Korean composition. It must not leak into shortcut modifier handling.

## Build Baseline

- The project is Windows-only.
- The official build baseline is `Visual Studio 2026 Community`.
- The solution targets `x64`, MSVC toolset `v145`, and Windows SDK `10.0.26100.0`.
- `MilkyWayIME.sln` is the source of truth for local development builds.
- The solution contains one production DLL project: `MilkyWayIME.Tsf`.
- `MilkyWayIME.Internal` exists only to share repo-owned implementation code across the DLL and test projects.
- The TSF DLL owns the minimal language bar settings menu and HKCU settings persistence. A future full settings UI, if added, should be a separate project instead of stretching the TSF DLL boundary.
