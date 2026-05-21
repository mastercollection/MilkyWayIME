# Host Compatibility Notes

This document records host-specific input-channel findings that should be read
before changing TSF, fallback input, or diagnostics behavior.

For the manual capture commands and tool roles, see
`docs/host-diagnostics-tools.md`.

## Host Identity Normalization

Normalize framework-generated window classes before making host compatibility
decisions.

- Treat `Avalonia-*` window classes as the `Avalonia` family. The suffix is an
  instance/framework-generated identifier and must not be used as a stable host
  rule key.
- Keep the original class name in diagnostics, but classify runtime policy on
  the normalized text-surface family. Process name should be used for log
  filtering and reproduction notes, not as a primary backend key.
- For Avalonia text inputs, prefer UI Automation element identity over HWND
  class suffixes. Useful keys include `FrameworkId`, `ControlType`,
  `AutomationId`, and `ClassName`.
- Do not treat `ValuePattern` support alone as proof that the focused element is
  a usable text input backend. The candidate must also have an input-oriented
  element identity and a visible write result.
- Treat `ControlType=Window` as excluded by default, even when UI Automation
  reports `ValuePattern`, `TextPattern`, or `LegacyIAccessiblePattern` support.
  A top-level window is not enough evidence of a text input surface.

## Validation Procedure

Collect TSF/TIP and UI Automation evidence at the same user-visible input
target before adding a host backend or retrying an old fallback path.

- Enable the debug IME build and perform one real Korean input attempt in the
  target host.
- Capture `HostDiagnostics` entries from `%TEMP%\MilkyWayIME\debug.log`,
  including activation, focus, pushed context, key down, flush/commit, write
  probe, and composition termination stages when present.
- Run `mwime_uia_probe.exe --sleep 3000 --redact-values` while the target input
  is focused.
- Run `mwime_uia_probe.exe --sleep 3000 --set "테스트" --redact-values` only
  when changing the focused control value is acceptable, then record whether the
  visible UI actually changed.
- Summarize the TIP/UIA evidence with
  `mwime_hostdiag_summarize.exe --log %TEMP%\MilkyWayIME\debug.log --process <process> --uia <uia-output.txt>`.
- Record HRESULTs and UTF-16 lengths, but do not log or rely on the user's
  original input text.
- Keep API success and visible input success separate. `S_OK` from TSF or UIA is
  not sufficient if the target UI does not visibly update.

## Backend Candidate Rules

Use these rules to keep host-specific fallbacks narrow.

- A UIA ValuePattern backend candidate must have an active TIP path, observed
  key handling, `ControlType=Edit`, `ValuePattern.IsReadOnly=false`, and a
  visible `SetValue()` success on the focused input.
- For FluentSearch/Avalonia, additionally require
  `FrameworkId=Avalonia`, `AutomationId=SearchTextBox`, and
  `ClassName=CustomTextBox`.
- `RICHEDIT50W` is a separate native/RichEdit family. UIA `SetValue()` success
  on RichEdit must be recorded separately from whether preedit-range
  replacement can preserve caret and selection.
- TIP key-path stability is evidence about TSF identity only. A stable
  `key_path_id`, context, and HWND sequence must still be checked against the
  visible write result before enabling any backend.
- If TIP activation or the key path is missing, classify the host as
  `activation_unknown_or_failed` or `no_tip_key_path_observed` until better
  evidence exists.
- If TIP write returns success but the UI does not visibly change, classify the
  write channel as failed for backend selection.
- Transitory DirectText composition is allowed only for verified native
  selection-replacement surfaces, currently native `Edit` and `RICHEDIT50W`.
  A transitory context, stable key path, full context, or successful TSF
  HRESULT is not enough to enable DirectText on an unknown/framework/window-only
  surface.

## FluentSearch Avalonia Popup

Observed host identity:

- Process: `FluentSearch.exe`
- Window class pattern: `Avalonia-*`, normalized family `Avalonia`
- Context: transitory TSF context
- Target HWND/focus/caret: stable during the tested key path
- TIP lifecycle: activation, text edit sink attachment, key down, edit-session
  request, composition start/commit, and termination have been observed.
- UI Automation focused element:
  - `FrameworkId`: `Avalonia`
  - `ControlType`: `Edit`
  - `AutomationId`: `SearchTextBox`
  - `ClassName`: `CustomTextBox`
  - `ValuePattern`: supported
  - `ValuePattern.IsReadOnly`: false
- Microsoft IME Spy++ observation:
  - The captured FluentSearch range showed IME-related traffic including
    `WM_IME_REQUEST`, `WM_IME_NOTIFY`, `IMN_SETCANDIDATEPOS`, and registered
    `MSIMEQueryPosition` messages across Avalonia/IME-related HWNDs.
  - This confirms Microsoft IME UI/candidate-position interaction with the
    host.
  - The captured range did not show the result input sequence
    `WM_IME_COMPOSITION(GCS_RESULTSTR) -> WM_IME_CHAR -> WM_CHAR`.
  - Therefore this capture is not evidence that FluentSearch accepts text
    through the same IMM result-message route observed in Nikke.

Tested and rejected paths:

- TSF inline composition is not viable. `StartComposition` and update calls can
  return `S_OK`, but the host terminates the composition immediately.
- TSF direct text insertion is not a reliable UI write channel. Forced
  `TransitoryDirectText` probing made `InsertTextAtSelection` return `S_OK`, and
  `GetText` on the inserted range returned the expected text, but the
  FluentSearch popup UI did not receive visible input.
- TSF range replacement cannot verify the previous preedit in this host. Logs
  showed `ReplacePreviousPreedit[ShiftStartShort] expected_move=-1 moved=0`.
- Synthetic host `Backspace` plus `InsertTextAtSelection` also did not produce
  visible FluentSearch input, even though the diagnostic path returned `S_OK`.
- `SendInput`-based probing, including packet/unicode-style synthetic input,
  was reported as failing for this popup. Do not treat SendInput as an untested
  FluentSearch solution.
- Win32 `Edit` and `RICHEDIT50W` replacement backends do not apply to this
  Avalonia popup.
- `IMR_DOCUMENTFEED` probing did not expose a usable document feed.
- The transitory parent object did not expose useful TSF/COM interfaces for a
  write fallback.

Confirmed working path:

- UI Automation `ValuePattern.SetValue()` works for the focused
  `SearchTextBox`. A manual probe with `--set "테스트"` updated the visible
  FluentSearch search box.
- The observed `SetValue()` behavior is whole-value replacement. After the
  probe writes the test value, the caret moves to the beginning of the field.
- `mwime_text_surface_probe.exe --uia-caret-end` did not visibly restore or move
  the FluentSearch caret. No native child HWND is exposed for Win32
  `EM_GETSEL`/`EM_REPLACESEL` probing.
- This should be treated as a host-specific backend, not evidence that TSF or
  SendInput is viable.

Current classification:

- `FluentSearch.exe` / `Avalonia-*` should be treated as
  `uia_avalonia_searchbox_whole_value_replace`.
- Do not infer real UI input support from `S_OK`, inserted range readback, or a
  stable HWND/focus/caret snapshot for this host.
- A `HostDiagnostics` classification such as `stable_target` is not sufficient
  for FluentSearch because older probes showed TSF success without visible UI
  input.

Next direction:

- Avoid repeating TSF inline, TSF direct insert, TSF range replacement,
  synthetic backspace, or SendInput experiments as if they were untested.
- Enable FluentSearch/Avalonia UIA runtime writing only when the focused
  element matches the `SearchTextBox` identity above and the operation can
  tolerate whole-value replacement.
- Treat `ValuePattern.SetValue()` as a whole-value replacement fallback. It is
  not a caret-preserving preedit update path for FluentSearch.
- Do not use this path for middle-of-text insertion, selection replacement, or
  caret-preserving Korean preedit.

## WezTerm

Observed host identity:

- Process: `wezterm-gui.exe`
- Window class: `org.wezfurlong.wezterm`
- Foreground/focus HWND: stable top-level WezTerm window
- Caret HWND: unavailable in `GetGUIThreadInfo`
- TIP lifecycle/key path: activation and key handling have been observed in
  some runs, including transitory contexts and immediate composition
  termination. Other runs showed no focused document at activation.
- UI Automation focused element:
  - `FrameworkId`: `Win32`
  - `ControlType`: `Window`
  - `ClassName`: `org.wezfurlong.wezterm`
  - `AutomationId`: empty
  - `NativeWindowHandle`: top-level WezTerm HWND
  - `ValuePattern`: reported as supported on the top-level window
  - `TextPattern`: reported as supported on the top-level window
  - `LegacyIAccessiblePattern`: reported as supported
  - Legacy value length: 0
- Manual observation: Korean input does not appear in the terminal.
- Microsoft IME Spy++ observation:
  - The observed WezTerm window received IME-related traffic including
    `WM_IME_REQUEST`, `WM_IME_NOTIFY`, registered `MSIMEQueryPosition`,
    `IMN_SETCANDIDATEPOS`, `WM_SETTEXT`, `WM_PAINT`, and key messages.
  - This confirms Microsoft IME UI/position interaction with WezTerm.
  - The captured Spy++ range did not show the Nikke-style result input sequence
    `WM_IME_COMPOSITION(GCS_RESULTSTR) -> WM_IME_CHAR -> WM_CHAR`.
  - Therefore this capture is not evidence that WezTerm accepts text through
    the same IMM result-message route observed in Nikke.
- Latest observed run:
  - TSF context: transitory, full context
  - TSF view HWND, foreground HWND, focus HWND, and top-level HWND all matched
    the WezTerm top-level HWND.
  - `GetGUIThreadInfo` did not expose a caret HWND.
  - `EditSink::Apply` planned TSF write operations such as `CommitText`,
    `StartComposition`, and `UpdateComposition` against this transitory
    context.
  - `WriteProbe` reported `S_OK` for `CommitText` and `StartComposition`, but
    the host immediately delivered `OnCompositionTerminated`.
  - `TransitoryBridge[SuppressEngineReset]` became active and suppressed the
    internal engine reset after external composition termination. This preserves
    MilkyWayIME's internal preedit state; it does not prove visible terminal
    text insertion.
  - Transitory parent probing did not expose useful TSF/COM insertion
    interfaces, and document feed probing returned no usable text surface.
  - `HostDiagnostics` classified the target as `stable_target` with
    `write_hr=0x00000000`.
  - `mwime_text_surface_probe.exe --native-replace-selection` reported
    `EM_GETSEL`/`EM_REPLACESEL` return values, but the selection indexes were
    not meaningful for an edit control and the terminal text did not change.

Current classification:

- `WezTerm` should not use the FluentSearch UIA ValuePattern backend.
- The focused UIA element is the top-level terminal window, not an `Edit`
  element or a stable text input surface.
- Treat this as `uia_window_only_not_text_input` unless a deeper UIA subtree or
  terminal-specific integration exposes a real text input element.
- Do not retry this as a generic UIA ValuePattern host merely because the
  top-level window advertises text-related patterns.
- Do not classify WezTerm as simply `no_tip_key_path_observed` when logs show a
  TIP path. The exclusion reason is that the TIP/UIA identities still do not
  expose a usable text input surface.
- Do not treat `stable_target` or `S_OK` write diagnostics as enough for
  WezTerm. The visible terminal input result is still failure.
- Treat the observed MilkyWayIME route as TSF transitory composition with
  external termination suppression, not as UIA `SetValue`, native Edit/RichEdit,
  or Nikke-style IMM result-message input.
- Do not treat `EM_GETSEL`/`EM_REPLACESEL` return values on the top-level
  WezTerm window as native edit support.
- The surface classification for this target is `window_only`; this is a
  capability exclusion, not a process-specific backend rule.

## RichEdit50

Verification status: KakaoTalk `RICHEDIT50W` focused UIA probe and visible
write confirmation completed.

Observed evidence:

- Debug logs include `RICHEDIT50W` focused/view classes in a `KakaoTalk.exe`
  process.
- TIP logs show activation, focused document manager refresh, text edit sink
  attachment, key handling, document feed availability, and
  `TransitoryDirectText` detecting a native backend for `RICHEDIT50W`.
- The focused UIA element and TSF view refer to the same native HWND in the
  observed KakaoTalk run:
  - UIA `native_hwnd=0x314AE`, `native_class=RICHEDIT50W`
  - TSF `view_hwnd=0x314AE`, `view_class=RICHEDIT50W`
- UIA identity:
  - `FrameworkId`: `Win32`
  - `ControlType`: `Document`
  - `AutomationId`: `1006`
  - `ClassName`: `RICHEDIT50W`
  - `ValuePattern`: supported
  - `ValuePattern.IsReadOnly`: false
  - `TextPattern`: supported
  - `LegacyIAccessiblePattern`: supported
- The class family is native RichEdit and must remain separate from Avalonia
  host handling.

Write result:

- UIA `ValuePattern.SetValue()` visibly updates the KakaoTalk RichEdit input.
- The observed behavior is whole-value replacement.
- After `SetValue()`, the caret moves to the beginning of the control.
- This does not preserve caret or selection semantics and is not suitable as a
  normal Korean composition/preedit backend.
- UIA `TextPattern` reports selection support and returns one empty selection
  at the caret in the observed run.
- Native RichEdit probing against the same HWND showed meaningful selection
  state with `EM_GETSEL`.
- `EM_REPLACESEL` visibly replaced text at the current RichEdit selection/caret
  in the observed run.

Current classification:

- The observed `RICHEDIT50W` input control should be treated as
  `native_richedit_selection_replace_confirmed`. `KakaoTalk.exe` is the
  reproducer process for this evidence, not a runtime policy key.

## Chromium Unknown Transitory Surface

Observed host identity:

- Process used for reproduction: `chrome.exe`
- Window class: `Chrome_WidgetWin_1`
- Context: transitory TSF context with full context observed
- Target classification: stable TIP/key path in the captured run
- Caret HWND: unavailable in `GetGUIThreadInfo`
- Surface classification: `unknown`
- Native write capability: `unknown`

Rejected path:

- Do not use `TransitoryDirectText` merely because the target is transitory and
  the IME has an active composition. Captured Chrome logs showed DirectText
  `replace_previous_preedit` being used without a verified native backend.
- The document-feed/preedit boundary then drifted across updates, with previous
  preedit matching against longer surrounding text instead of a stable native
  selection range.

Current classification:

- `Chrome_WidgetWin_1` is evidence for the generic `unknown` surface family.
  It is not a process-specific exception.
- Unknown transitory surfaces must stay on the normal TSF composition plan
  unless a separate probe verifies a native `Edit`/`RICHEDIT50W` selection
  replacement surface or another explicitly documented backend.
- The DirectText gate must not remain active solely because MilkyWayIME already
  has an active composition on the same transitory target.
- Prefer the native RichEdit/TSF identity for future RichEdit-specific handling.
- Do not promote RichEdit `ValuePattern.SetValue()` to a general UIA composition
  backend. It is only a limited fallback candidate where whole-value replacement
  and caret reset are acceptable.
- Prefer `EM_GETSEL`/`EM_REPLACESEL` over UIA `SetValue()` for RichEdit fallback
  experiments because the native path preserves selection-aware replacement
  semantics.
- The surface classification for this target is `native_richedit`, with
  `native_selection_replace` as the confirmed write capability.

## Nikke / Unity

Verification status: focused UIA probe completed. Microsoft IME input-route
verification completed with Spy++. Current MilkyWayIME runs do not reach a
usable in-process TSF input path because selecting MilkyWayIME in Nikke
immediately reverts to Microsoft IME.

Observed host identity:

- Process: `nikke.exe` / `NIKKE.exe`
- Window class: `UnityWndClass`
- Foreground/focus HWND: same top-level Unity window in the observed run
- Caret HWND: unavailable
- UI Automation focused element:
  - `FrameworkId`: `Win32`
  - `ControlType`: `Window`
  - `ClassName`: `UnityWndClass`
  - `AutomationId`: empty
  - `NativeWindowHandle`: top-level `UnityWndClass` HWND
  - `ValuePattern`: reported as supported
  - `TextPattern`: reported as supported
  - `LegacyIAccessiblePattern`: reported as supported
  - Legacy value length: 0
- Manual observation: selecting MilkyWayIME in the app is possible, but the
  active IME immediately falls back to Microsoft IME.

Microsoft IME input route:

- Spy++ showed Microsoft IME input arriving at the top-level `UnityWndClass`
  HWND, observed as `0x20766` in the captured run.
- The observed route is Win32/IMM message based:
  - `WM_INPUT`
  - `WM_KEYDOWN` / `WM_KEYUP`
  - `WM_IME_COMPOSITION` with `GCS_RESULTSTR`
  - `WM_IME_CHAR`
  - `WM_CHAR`
- This means Nikke can accept Korean input through the classic
  `UnityWndClass` IMM/result-message path used by Microsoft IME.
- This is not evidence for UIA `ValuePattern`, UIA `TextPattern`, or native
  Edit/RichEdit replacement support.

TIP evidence:

- The current run showed `NoFocusedDocument` while the foreground window was
  `nikke.exe` / `UnityWndClass`; this is not a usable TSF input document.
- Older logs include a `NIKKE.exe` key path on `UnityWndClass`, but
  `StartComposition` was immediately followed by `OnCompositionTerminated` with
  an empty range and `disposition=cancel`.
- Document feed probing returned no usable text surface in the older run.
- Current focus/process diagnostics show `mwime_tsf.dll` is not loaded into the
  `nikke.exe` process while Microsoft IME remains usable.
- Therefore the immediate MilkyWayIME issue in Nikke is profile
  activation/load retention, not yet a confirmed write-backend failure inside a
  loaded MilkyWayIME instance.

Current classification:

- `Nikke` / `UnityWndClass` should be treated as
  `unity_window_only_no_text_uia_backend`.
- The Microsoft IME success route should be recorded separately as
  `unity_imm_result_message_input_route_confirmed`.
- MilkyWayIME's current state should be recorded as
  `tsf_profile_activation_reverted_to_msime` until a run shows
  `mwime_tsf.dll` loaded in `nikke.exe`.
- When no focused TSF document is observed in the current process, also record
  `no_focused_tsf_document_for_foreground_unity_window` as the immediate TIP
  reason.

Do not retry:

- Do not enable the FluentSearch/Avalonia UIA backend for Unity window-only
  targets.
- Do not infer text input support from a focused game/application window unless
  a real text input element or host-specific text channel is observed.
- Do not treat top-level `ValuePattern` or `TextPattern` support on
  `UnityWndClass` as evidence of an editable text backend.
- Do not spend more time on UIA/native Edit fallbacks for Nikke based on the MS
  IME result. The observed working route is the `UnityWndClass` IMM/result
  message path.
- Do not compare MilkyWayIME message output in Nikke until MilkyWayIME
  activation is retained and `mwime_tsf.dll` is confirmed loaded in the
  process.
- The surface classification for this target is `window_only`; this is a
  capability exclusion, not a process-specific backend rule.
