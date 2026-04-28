# AGENTS.md

This document defines the working rules for agents operating in `D:\Git\MilkyWayIME`.

## Core Rules

- Always get user confirmation before writing new code or modifying existing code.
- When design judgment, behavior changes, or tradeoff decisions are required, do not proceed on assumption alone. Ask questions and verify the intended direction first.
- All responses to the user must be written in Korean.
- Avoid unsupported optimism. If there are constraints, risks, or unknowns, state them explicitly.

## Project Goals

- Support multiple Korean layouts through the `libhangul` library.
- Support multiple base layouts that describe the user's current key-label arrangement.
- Convert input labels through the selected base layout's inverse map to the fixed QWERTY/libhangul token layout for Korean composition.
- Interpret shortcuts from the input labels reported by Windows/TSF in both Korean and English input states, including `Ctrl`, `Alt`, `Win`, and `Shift`.
- Support Hanja candidate selection from the currently composing Korean syllable when the Hanja key is invoked.
- Keep the structure extensible so user-defined custom layouts can be supported later.
- Keep user-facing layout settings reachable from the language bar menu; a full settings UI can remain a separate future project.

## Design Principles

- Treat the base layout and the Korean composition layout as separate first-class concepts.
- Keep input state, layout definitions, key conversion logic, and shortcut resolution logic separated as much as possible.
- Do not rely only on the resulting character output. Distinguish input labels, virtual keys, QWERTY/libhangul tokens, and modifier state explicitly.
- A base layout maps fixed QWERTY/libhangul token positions to the labels used by the current key arrangement; omitted keys are identity mappings.
- Korean composition must use the selected base layout's inverse mapping to recover the fixed QWERTY/libhangul token before forwarding input to `libhangul`.
- Shortcut interpretation must use the input label key reported by Windows/TSF, not the Korean composition token.
- Delegate Korean composition logic to `libhangul`, while keeping layout selection and event forwarding responsibilities clear inside this project.
- Hanja conversion must operate only on the currently composing Korean syllable, not on already committed text.
- Do not hardcode around built-in layouts only. Prefer data-driven structures that can later accommodate user-defined layouts.

## Implementation Notes

- Adding a layout must not silently break behavior in other input modes.
- Changing the selected base layout must update Korean composition token mapping without changing shortcut interpretation away from the reported input label.
- Korean input handling and shortcut handling must not corrupt each other's modifier state.
- If a layout-specific exception is necessary, prefer an explicit branch with documentation over contaminating the common path.
- If there are limitations from the operating system keyboard APIs or from `libhangul`, explain those limitations to the user before implementation.

## Validation Criteria

- Run project build validation through elevated/admin MSBuild. Non-elevated build
  results are not the authoritative validation result for this repository.
- Verify that Korean composition input still behaves correctly.
- Verify that the selected base layout maps input labels to QWERTY/libhangul tokens correctly.
- Verify that each Korean layout forwards the expected QWERTY/libhangul token to `libhangul`.
- Verify that shortcut combinations using `Ctrl`, `Alt`, `Win`, and `Shift` are handled consistently in both Korean and English states.
- Verify that invoking the Hanja key during composition opens a candidate window for the current composing syllable and that selecting a candidate replaces that syllable correctly.
- Verify that adding a new layout or custom layout support does not regress behavior for existing layouts.

## Documentation Rules

- Reflect new layouts, key mapping rules, exceptions, and known limitations in documentation together with the code.
- Record the rationale for decisions that affect future custom layout support.

## Future Tooling Goals

- Provide a keyboard matrix tester for development and validation purposes.
- The keyboard matrix tester should help verify that the selected base layout is recognized correctly.
- The keyboard matrix tester should help verify input label to QWERTY/libhangul token mapping before Korean composition.
- The keyboard matrix tester should make it easier to inspect physical keys, virtual keys, scan codes, modifier states, and resulting mapped output during debugging and validation.

## Reference Projects

- `D:\Git\MilkyWayIME\references\DIME`
  - Use this project as a reference for the breadth of a full TSF IME implementation in native C++.
  - It is especially useful for identifying the set of TSF-facing components that a production IME typically needs, such as composition handling, candidate handling, edit sessions, display attributes, language bar integration, registration, settings, and tests.
  - Do not copy its large monolithic source layout directly. MilkyWayIME should avoid letting TSF glue, engine logic, UI, settings, and dictionary logic collapse into one large module tree.

- `D:\Git\MilkyWayIME\references\windows-chewing-tsf`
  - Use this project as a reference for architectural separation between IME core, host integration, installer, preferences, and build automation.
  - Even though it is Rust-based, it demonstrates a cleaner project split than many older IME codebases.
  - Follow the separation principle, not the language choice. MilkyWayIME is expected to stay C++-based unless the user explicitly decides otherwise.

- `D:\Git\MilkyWayIME\references\MetasequoiaImeTsf`
  - Use this project as a reference for a more modern native Windows IME layout with clearer subsystem directories, CMake-based builds, scripts, and data/resource separation.
  - It is useful when designing project structure, candidate UI boundaries, dictionary-related modules, registration code, and build scripts.
  - Do not inherit its decisions blindly. Manual installation or project-specific shortcuts should not be treated as defaults for MilkyWayIME.

- `D:\Git\MilkyWayIME\references\kime`
  - Use this project as a reference for separating input engine logic, frontend integrations, tools, and configuration-driven layout behavior.
  - It is especially useful when designing base layout abstractions, custom layout files, layout addons, hotkey scopes, and configuration structure.
  - Do not treat it as a Windows TSF implementation reference. It targets Linux frontend environments, so TSF/COM integration, Windows registration, language bar behavior, and installer behavior must still be derived from Windows-focused references.

## How To Use References

- Use reference projects to understand proven TSF component boundaries, not to justify copying behavior without review.
- Prefer extracting structural lessons over copying implementation details.
- When multiple references disagree, prefer the option that keeps MilkyWayIME simpler, more modular, and more compatible with its goals around base layouts, Korean token mapping, shortcut consistency, and future custom layouts.
- If a design idea comes from a reference project, record which project influenced the decision and why.

## Wiki Knowledge Base
WikiMode: managed
WikiPath: D:\wiki\milkyWayIME

## Security / antivirus compatibility
This environment uses Bitdefender on Windows, and PowerShell-heavy or suspicious command lines may be blocked.

Follow these rules strictly:
- Avoid PowerShell unless absolutely necessary
- Do not use encoded PowerShell commands
- Do not generate long shell one-liners for file rewriting
- Do not chain many shell commands together
- Do not touch registry, startup settings, scheduled tasks, antivirus settings, or security settings
- Do not use obfuscated scripts or suspicious automation patterns
- Prefer standard dotnet CLI and git commands when command execution is needed
- Keep commands short, explicit, and readable
- If a command is blocked or likely to trigger antivirus heuristics, stop and propose the smallest safe manual alternative