# Layout Data

Layout data is split by role.

- `physical/`: base layouts. A base layout tells the IME which input label is
  placed on each fixed QWERTY/libhangul token position.
- `korean/`: built-in Korean layout metadata. For libhangul-supported layouts,
  composer details are owned by the engine and are not repeated in user data.

## Base Layout Direction

Base layout `keys` use this direction:

```text
QWERTY/libhangul token position -> current base layout label
```

Example:

```json
{
  "id": "colemak_dh",
  "displayName": "Colemak-DH",
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

The QWERTY/libhangul token layout is fixed by the engine. User data does not
declare `type`, `mapsTo`, `inputTokenLayout`, or libhangul keyboard IDs.
