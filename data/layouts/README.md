# Layout Data

Physical English layouts and Korean layout mappings are stored separately.

- `physical/`: the effective base English key interpretation layer that Windows/TSF sees
- `korean/`: Korean mappings defined relative to the selected effective base layout

Notes:

- `physical English layout` does not mean the raw hardware switch matrix. It means the post-remap base layout after firmware remaps such as QMK/ZMK and OS-level remaps such as `Scancode Map`.
- Korean mappings must be able to distinguish `Shift` as part of a Hangul mapping key. For example, `R` and `Shift+R` may resolve to different Hangul input tokens.
