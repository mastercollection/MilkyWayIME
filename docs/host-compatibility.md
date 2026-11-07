# Host Compatibility Notes

This document records host-specific input-channel findings that should be read
before changing TSF, fallback input, or diagnostics behavior.

For the manual capture commands and tool roles, see
`docs/host-diagnostics-tools.md`.

## TSF Edit Transaction Safety (2026-10-14)

- Separate `RequestEditSession` acceptance from execution completion. A failed
  request that never called `DoEditSession` retains its operations for retry;
  `TF_S_ASYNC` transfers ownership to the accepted session, not to a retry.
- Only `TF_E_SYNCHRONOUS` triggers the normal write queue's asynchronous retry.
  Read-only rejection is not assumed to be transient. Once execution has begun,
  failure may mean partial text delivery and must not replay the batch.
- Move the submitted snapshot out of the pending queue before requesting a
  session. Input enqueued reentrantly must not be removed with that snapshot.
- While asynchronous work is outstanding, request later writes asynchronously
  too, so synchronous writes cannot overtake the accepted work. `OnEndEdit`
  requests asynchronous writes directly rather than requesting a forbidden
  synchronous read/write lock first.
- Context identity and a shared validity token bound delayed sessions to their
  original lifetime. Detach, thread-focus loss, non-suppressed tracked composition
  termination, and write failure invalidate dependent work. The execution guard
  remains active during deferred execution and cleanup, including reentrant calls.
- Detach attempts finalization of an already tracked TSF composition through a
  separate edit session retaining that old composition and context. This session
  cannot end a newer composition through the service's current-composition slot.
  Ordinary queued preedit/commit operations invalidated at detach are cancelled,
  not replayed into the new input target. Pending overlay commits may therefore
  be lost on focus transfer if the original write could not finish in time.
- A host response of `S_OK` with a null composition is rejection, not successful
  input. Track an accepted composition before writing its text so termination
  during the write cannot resurrect it. Ignore termination of untracked objects.
- Debug diagnostics distinguish `EditSession.Completed`,
  `EditSession.StaleContext`, `EditSession.FailedNoReplay`, and
  `DetachedComposition.Request` / `DetachedComposition.End`. These new events
  contain results, not input text. Existing text-bearing debug logs are unchanged.

Deterministic COM test doubles in `tests/integration/edit_session_tests.cpp`
exercise rejection/retry, asynchronous ordering and guard lifetime, reentrant
enqueue, context invalidation, partial failure without replay, composition
rejection, termination during `SetText`, stale termination, and delayed old
composition finalization. They do not establish visible input success in games,
UIA hosts, or the transitory DirectText backend. Existing backend classifications
and previously rejected host experiments remain unchanged.

Validation on 2026-10-14: Debug x64 and Win32 solution builds and both test
executables passed using the installed VS 2022/v143 toolset override. The
mid-batch cancellation test was observed failing before its execution-loop guard
was corrected, then passed on both architectures. These are supplemental,
non-admin results: the repository requires v145/admin MSBuild, v145 is not
installed here, and the attempted UAC launch did not produce a build log before
being interrupted. No IME registration or live application input was performed.

API contracts:
[RequestEditSession](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-requesteditsession),
[StartComposition](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontextcomposition-startcomposition).

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

Support intent:

- WezTerm remains an intended Korean input target. The notes below describe the
  current evidence and safety constraints; they are not a decision to abandon
  WezTerm support.
- Crash prevention has priority over visible text insertion while the usable
  WezTerm input route is still unverified. A diagnostic `S_OK`, a stable
  top-level HWND, or advertised top-level UIA text patterns must not trigger an
  unsafe write backend.
- Future WezTerm work should look for a terminal-specific or otherwise verified
  input path that can accept Korean text without destabilizing the host.

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
  - `TransitoryBridge[SuppressEngineReset]` preserved MilkyWayIME's internal
    preedit state after external composition termination, but this produced
    duplicated evolving preedit in the host (`ㅇ아안안...`) because WezTerm
    terminated every TSF inline composition and later writes appended the next
    preedit snapshot.
  - This is evidence that host composition termination is not a trustworthy
    lifecycle signal for WezTerm, and that preserving internal state while still
    writing TSF preedit to the host is not viable.
  - Transitory parent probing did not expose useful TSF/COM insertion
    interfaces, and document feed probing returned no usable text surface.
  - `HostDiagnostics` classified the target as `stable_target` with
    `write_hr=0x00000000`.
  - `mwime_text_surface_probe.exe --native-replace-selection` reported
    `EM_GETSEL`/`EM_REPLACESEL` return values, but the selection indexes were
    not meaningful for an edit control and the terminal text did not change.

Current classification:

- Current visible Korean input behavior is a known failure to be fixed, not the
  desired product behavior.
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
  WezTerm. The visible terminal input result is still failure, and the runtime
  must prefer a no-crash fallback over speculative writes.
- Treat the observed MilkyWayIME route as `window_only` overlay composition:
  keep libhangul preedit internally, show preedit in an IME-owned overlay near
  the caret/window anchor, and send only confirmed text to the host. Do not send
  TSF `StartComposition` or `UpdateComposition` preedit writes to WezTerm.
- For overlay placement, prefer TSF `GetTextExt` on the current selection or
  active composition when available, then GUI caret, then read-only UIA
  `TextPattern` bounding rectangles, then active/foreground window fallback.
  UIA `ValuePattern.SetValue()` remains excluded.
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

### 2026-06-08 inline-composition investigation (do not repeat)

Goal was true in-field inline composition in Nikke. Every lighter path was
tried and empirically closed; decision is to keep the window-only overlay (B)
and not pursue inline further without a fundamentally different approach.

Activation (how MilkyWayIME TSF gets into `nikke.exe` at all):

- Selecting the profile while Nikke is running does NOT load us; Nikke adopts
  only the **default** input method at process start.
- It loads only when MilkyWayIME is set as the **default input method override**
  (Settings > Typing > Advanced keyboard settings) AND Nikke is restarted.
  Confirmed via `tasklist /m mwime_tsf.dll` showing `nikke.exe` and per-process
  `[pid=<nikke>]` lines in `%TEMP%\MilkyWayIME\debug.log` (our in-process logs
  DO work in Nikke; only external `SetWindowsHookEx` tracing is blocked).

TSF composition in Nikke (with MilkyWayIME loaded, overlay routing bypassed):

- Nikke's focused context is transitory (`transitory=1`, no usable parent
  document: `qi_document_mgr_hr=0x80004002`). The host fires
  `OnCompositionTerminated` immediately after EVERY `StartComposition` edit
  session (we do not end it - no `OnEndEdit`/finalize between), and it commits
  the preedit as a result. Re-starting per keystroke therefore duplicates jamo
  ("ㅇ아안안").
- Preserving `composition_` across the suppressed termination and calling
  `UpdateComposition` fails: `GetRange` on the terminated `ITfComposition`
  returns `0x8000FFFF` (E_UNEXPECTED). A terminated composition cannot be
  reused. MS IME keeps ONE composition for the whole syllable
  (1×START / N×COMPOSITION(GCS_COMPSTR) / 1×END via Spy++), which we could not
  reproduce in this transitory context.
- Driving IMM directly from the TIP (`ImmGetContext` +
  `ImmSetCompositionStringW`) fails: `ImmGetContext` succeeds but
  `ImmSetCompositionString` is rejected (it is an IME-internal API; a TSF TIP /
  client may not write the composition of a CUAS-owned IMC).

Standalone IMM32 IME (H1) channel test:

- A minimal IMM32 `.ime` (loads, `ImeProcessKey`/`ImeToAsciiEx`, force-open via
  `ImmSetOpenStatus`/`ImmSetConversionStatus` in `ImeSelect`) works in ordinary
  apps (emits a fixed char). It does NOT load into `nikke.exe`: no `nikke` pid
  ever appears in `%TEMP%\MilkyWayIME\imm32_test.log`, even when selected active
  before launch - same default-adoption wall as TSF.
- `ImmInstallIME` only registers under English (0409) and the result is not a
  default-input-override candidate; a hand-registered Korean KLID (HKLM only,
  no per-user language-profile association) does not appear in the input list.
  Making a minimal IME selectable as the Korean default needs production-grade
  registration, and a shipped IMM32 IME would also be a SECOND visible
  "MilkyWayIME" entry alongside the TSF TIP (different input-method models).
- Reference projects carry no IMM32 `.ime` implementation (all TSF-only,
  including mozc). MS IME itself ships no separate IMM32 IME; it is one TSF TIP
  bridged to IMM apps by CUAS.

Decision: Nikke stays `window_only` overlay (clean committed text via the
CUAS result path + IME-owned overlay preedit). Improving the overlay anchor
position (Gap 2) is the remaining worthwhile work; true inline is out of scope.
Experiment scaffolding lives untracked under `src/imm32/`,
`MilkyWayIME.Imm32*.vcxproj`.
