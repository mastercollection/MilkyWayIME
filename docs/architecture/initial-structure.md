# Initial Structure

MilkyWayIME starts from four explicit boundaries:

- `engine`: input state, layout model, shortcut resolution, and session flow
- `adapters`: third-party or external integration boundaries such as `libhangul`
- `tsf`: Windows TSF service integration, edit sessions, display attributes, and registration
- `ui`: candidate and notification UI layers that consume engine-facing models

## Rules

- `engine` must not depend on TSF, COM, or Win32 UI headers.
- `tsf` must normalize key input before calling the engine.
- `adapters/libhangul` is the only place where `libhangul` integration should live.
- Layout definitions belong in `data/layouts`, not hardcoded TSF classes.
- Hanja conversion requests must operate on the current composing string only.
