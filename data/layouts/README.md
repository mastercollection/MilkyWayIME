# Layout Data

Layout data is split by role. These files are schema samples for future custom
layout support; the current built-in layouts are compiled as C++ data tables and
are not loaded from JSON at runtime.

- `base/`: base layouts. A base layout tells the IME which input label is
  placed on each fixed QWERTY/libhangul token position.
- `korean/`: Korean layout metadata samples. Built-in Korean layouts are
  identified as `libhangul:<id>` and resolved through libhangul keyboard IDs.

## Base Layout Direction

Base layout `keys` use this direction:

```text
QWERTY/libhangul token position -> current base layout label
```

Example:

```json
{
  "id": "colemak",
  "displayName": "콜맥",
  "keys": {
    "s": "r"
  }
}
```

This means the fixed QWERTY/libhangul `s` position is labeled `r` in the
selected base layout. At runtime, the IME builds the inverse map:

```text
input label r -> QWERTY/libhangul token s -> libhangul input "s" -> ㄴ
```

Omitted keys are identity mappings. For example, if `"a"` is not listed, `a`
maps to `a`.

The QWERTY/libhangul token layout is fixed by the engine. Base layout data does
not declare `type`, `mapsTo`, `inputTokenLayout`, or libhangul keyboard IDs.

`keys` contains only changed mappings. Each member name is the fixed
QWERTY/libhangul token, and each value is the label in the selected base layout.
Every value must be a string.

Supported key strings are:

- `a` through `z`
- `0` through `9`
- unshifted OEM labels: `;`, `/`, `` ` ``, `[`, `\`, `]`, `'`, `=`, `,`, `-`, `.`
- canonical names such as `Space`, `Tab`, `Return`, `Backspace`, `Oem1`

Shifted labels such as `:`, `?`, and `!` are not accepted in base layout JSON.
The effective label set, including omitted identity mappings, must not contain
duplicates.

The JSON loader is wired to unit tests, the `keyboard-matrix` development tool,
and the TSF runtime. The TSF runtime reads custom base layout JSON once when
the text service is created from:

```text
%APPDATA%\MilkyWayIME\layouts\base
```

The repository `data/layouts/base` directory remains sample/test data. The TSF
runtime does not read it directly.

Each JSON file is loaded independently. A malformed file is reported and
skipped, but other valid files from the same directory can still be used.

Base layout ID collisions are treated as override, matching the Korean
`libhangul:<id>` resolver policy. If a later file defines an ID that already
exists, including `us_qwerty` or `colemak`, the later definition becomes the
current meaning of that ID.

## Korean Layout IDs

Users do not write MilkyWayIME-specific JSON for custom Korean layouts. Custom
Korean layouts use libhangul XML directly:

```xml
<hangul-keyboard id="my-sebeol" type="jamo">
  <name xml:lang="ko">내 세벌식</name>
  <map id="0">
    <item key="0x71" value="0x1107"/>
  </map>
</hangul-keyboard>
```

Korean layout IDs use a source-qualified form. Built-in and future custom
libhangul layouts use:

```text
libhangul:<libhangul-keyboard-id>
```

For example, `libhangul:2` resolves to libhangul keyboard id `2`. This ID means
"whatever libhangul currently resolves as id `2`"; future custom XML support may
override built-in ids by registering a custom layout with the same libhangul id.

Current builds still compile `external/libhangul` with `ExternalKeyboard=NO` and
`ENABLE_EXTERNAL_KEYBOARDS=0`. Runtime custom XML loading requires a later build
configuration change that enables external keyboards and the expat dependency.
