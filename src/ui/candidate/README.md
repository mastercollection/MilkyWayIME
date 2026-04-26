# Candidate UI

Candidate list presentation belongs here.

Candidate windows are Win32 UI owned by the TSF service layer. The window should
stay presentation-only: selection, finalization, cancellation, and TSF UI element
state remain in `src/tsf/candidate`.

The default palette follows the Windows app light/dark theme. High contrast mode
uses system colors instead of the custom palette.
