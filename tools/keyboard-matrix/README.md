# Keyboard Matrix Tester

`mwime_keyboard_matrix.exe` is a console-only development tool for checking how
MilkyWayIME interprets keyboard input.

It verifies the current low-level path:

```text
input label -> selected base layout inverse -> QWERTY/libhangul token -> libhangul preview
```

## Usage

```powershell
.\build\MilkyWayIME.Tools.KeyboardMatrix\x64\Debug\mwime_keyboard_matrix.exe matrix --base us_qwerty --korean ko_dubeolsik
.\build\MilkyWayIME.Tools.KeyboardMatrix\x64\Debug\mwime_keyboard_matrix.exe matrix --base colemak --korean ko_dubeolsik
.\build\MilkyWayIME.Tools.KeyboardMatrix\x64\Debug\mwime_keyboard_matrix.exe matrix --base colemak_dh --korean ko_dubeolsik
.\build\MilkyWayIME.Tools.KeyboardMatrix\x64\Debug\mwime_keyboard_matrix.exe watch --base colemak_dh --korean ko_dubeolsik
```

`--physical` is still accepted as a compatibility alias for `--base`.

`matrix` prints a tab-separated table for alphabetic keys, selected OEM keys,
basic delimiter keys, shortcut checks, and a few Hangul composition sequences.
The important columns are:

- `input_label_key`: the key label reported by Windows/TSF, used for shortcut resolution.
- `hangul_token_key`: the fixed QWERTY/libhangul token recovered through the selected base layout.
- `hangul_ascii`: the ASCII token forwarded to libhangul when the key composes Hangul.
- `hangul_preview`: libhangul's single-key preview for that token.

For Colemak-DH, `R` is expected to show `input_label_key=R`,
`hangul_token_key=S`, and preview `ㄴ`.

`watch` reads key events from the focused console window and prints each
observed VK, scan code, modifier state, input label, Hangul token, libhangul
commit/preedit preview, category, and shortcut action. Press `Esc` to exit.

The current shortcut resolver maps `Ctrl+Shift+Space` to `ToggleInputMode` and
`Ctrl+Alt+O` to `OpenConfiguration`; shortcuts use the input label key, not the
Hangul token key.
