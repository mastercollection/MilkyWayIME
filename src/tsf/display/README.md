# Display Attribute Layer

TSF display attributes and text decoration integration belong here.

MilkyWayIME currently exposes one display attribute for the active composing
range:

- The current composing last syllable is rendered with no underline.
- Text uses the system window background color.
- Background uses the system window text color.

This follows the project's intended inverted composing-cell style while keeping
the scope limited to the last composing syllable instead of the full preedit
string. The actual rendering still depends on the host application's TSF display
attribute support.
