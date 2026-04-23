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
- Layout definitions belong in `data/layouts`, not hardcoded TSF classes.
- Hanja conversion requests must operate on the current composing syllable only.
- `physical English layout` means the effective base English layout that Windows/TSF sees after firmware and OS remapping, not a raw switch matrix dump.
- Hangul mapping keys are resolved from the selected effective base layout, and `Shift` is part of the Hangul mapping key only for Hangul composition.

## Build Baseline

- The project is Windows-only.
- The official build baseline is `Visual Studio 2026 Community`.
- The solution targets `x64`, MSVC toolset `v145`, and Windows SDK `10.0.26100.0`.
- `MilkyWayIME.sln` is the source of truth for local development builds.
- The solution contains one production DLL project: `MilkyWayIME.Tsf`.
- `MilkyWayIME.Internal` exists only to share repo-owned implementation code across the DLL and test projects.
- A future settings UI, if added, should be a separate project instead of stretching the TSF DLL boundary.
