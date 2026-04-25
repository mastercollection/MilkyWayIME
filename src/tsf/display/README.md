# Display Attribute Layer

TSF display attributes and text decoration integration belong here.

MilkyWayIME currently exposes one display attribute for the active composing
range:

- The current composing last syllable is rendered with a dotted underline.
- Text and background colors are left to the host application.
- No foreground or background color is specified by MilkyWayIME.

This keeps the scope limited to the last composing syllable instead of the full
preedit string, while avoiding fragile foreground/background color assumptions
in dark-mode host applications. The actual rendering still depends on the host
application's TSF display attribute support.
