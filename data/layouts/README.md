# Layout Data

Layout data is split by role. These files are schema samples for future custom
layout support; the current built-in layouts are compiled as C++ data tables and
are not loaded from JSON at runtime.

- `physical/`: base layouts. A base layout tells the IME which input label is
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

## Korean Layout IDs

Korean layout IDs use a source-qualified form. Built-in libhangul layouts use:

```text
libhangul:<libhangul-keyboard-id>
```

For example, `libhangul:2` resolves to libhangul keyboard id `2`. This ID means
"whatever libhangul currently resolves as id `2`"; future custom XML support may
override built-in ids by registering a custom layout with the same libhangul id.
