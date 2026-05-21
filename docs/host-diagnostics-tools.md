# Host Diagnostics Tool Usage

This document describes the manual diagnostics flow for comparing the TSF/TIP
input target with the UI Automation input target in a host application.

Read `docs/host-compatibility.md` before changing host-specific input behavior
or repeating an experiment that is already recorded there.

## Tool Roles

- Debug IME DLL: writes `HostDiagnostics` entries to
  `%TEMP%\MilkyWayIME\debug.log` while the IME is used in a real host.
- `mwime_uia_probe.exe`: captures the focused UI Automation element and, when
  requested, tries `ValuePattern.SetValue()`.
- `mwime_text_surface_probe.exe`: checks whether the focused text surface can
  expose selection/caret state through UIA `TextPattern` or native Win32 edit
  messages. It can also run explicit mutation probes when requested.
- `mwime_message_trace.exe`: injects a narrow message hook into a target UI
  thread and records selected `WM_IME_*`, key, char, and text messages to
  compare Microsoft IME and MilkyWayIME behavior.
- `mwime_hostdiag_summarize.exe`: reads an existing debug log and summarizes
  the TIP/key-path diagnostics for a process. It does not run automatically and
  does not collect new host data by itself.

## Basic Flow

1. Install or activate the Debug IME build.
2. Focus the target input control in the host application.
3. Type one real Korean input attempt through the IME.
4. Confirm that `%TEMP%\MilkyWayIME\debug.log` has new entries for the target
   process.
5. Run the UIA probe while the same target input is focused.
6. Run the host diagnostics summarizer against the accumulated debug log.
7. Record TIP identity, UIA identity, write result, classification, and paths
   that should not be retried in `docs/host-compatibility.md`.

## Host Capability Validation Order

Use this order when revalidating host capability before implementing a new
backend policy. The order starts with a known-positive native text surface,
then checks limited UIA replacement, known-negative terminal behavior, and
finally the Unity activation problem.

1. KakaoTalk / `RICHEDIT50W`
   - Purpose: positive baseline for Debug IME logging, UIA probing, and native
     text-surface probing.
   - Confirm: real Korean input attempt, `RICHEDIT50W` focused/native HWND,
     meaningful `EM_GETSEL`, visible `EM_REPLACESEL`.
   - Stop the sequence if this fails. Fix IME activation, install state, or
     diagnostic tool execution before testing other hosts.
2. FluentSearch / Avalonia `SearchTextBox`
   - Purpose: limited UIA whole-value replacement baseline.
   - Confirm: `FrameworkId=Avalonia`, `ControlType=Edit`,
     `AutomationId=SearchTextBox`, `ClassName=CustomTextBox`.
   - Treat visible `ValuePattern.SetValue()` success as whole-value replacement
     only. Do not classify it as caret-preserving preedit support or an enabled
     runtime backend.
3. WezTerm / `org.wezfurlong.wezterm`
   - Purpose: negative baseline separating TSF `S_OK` from visible input
     success.
   - Confirm: top-level `ControlType=Window`, no caret HWND, transitory TSF
     context when present, immediate `OnCompositionTerminated`, and
     `TransitoryBridge[SuppressEngineReset]` when the MilkyWayIME debug path is
     observed.
   - Do not promote this host to UIA, native Edit/RichEdit, or IMM
     result-message support without new evidence of a real text surface.
4. Nikke / Unity `UnityWndClass`
   - Purpose: separate MilkyWayIME activation/load retention from write-backend
     behavior.
   - Confirm first whether `mwime_tsf.dll` stays loaded in `nikke.exe` after
     selecting MilkyWayIME.
   - If the profile immediately reverts to Microsoft IME or the DLL is not
     loaded, stop backend comparison and record
     `tsf_profile_activation_reverted_to_msime`.

After each host, update `docs/host-compatibility.md` with the observed TIP
identity, UIA identity, write result, visible result, classification, and
do-not-retry notes. Keep original typed strings out of the notes.

## UIA Snapshot

Use this when you only need focused element identity and supported UIA patterns.
It redacts values so the user's input text is not logged.

```bat
D:\Git\MilkyWayIME\build\MilkyWayIME.Tools.UiaProbe\x64\Debug\mwime_uia_probe.exe --sleep 3000 --redact-values
```

The `--sleep 3000` delay gives you three seconds to focus the target input
before the probe captures the UIA state.

## UIA Write Probe

Use this only when it is acceptable to change the focused control value.

```bat
D:\Git\MilkyWayIME\build\MilkyWayIME.Tools.UiaProbe\x64\Debug\mwime_uia_probe.exe --sleep 3000 --set "테스트" --redact-values
```

Record both the reported `SetValue()` result and whether the visible UI
actually changed. A successful HRESULT is not enough evidence if the host UI
does not visibly update.

For RichEdit-style controls, repeat the write probe with existing text, caret in
the middle, and a non-empty selection. Record whether `SetValue()` replaces the
whole value or preserves selection/caret semantics.

## TIP Log Summary

`mwime_hostdiag_summarize.exe` reads `%TEMP%\MilkyWayIME\debug.log`. It is a
post-processing tool, not a background service.

```bat
D:\Git\MilkyWayIME\build\MilkyWayIME.Tools.HostDiagSummarize\x64\Debug\mwime_hostdiag_summarize.exe --log "%TEMP%\MilkyWayIME\debug.log" --process FluentSearch.exe
```

Use the target process name for `--process`, for example:

```bat
D:\Git\MilkyWayIME\build\MilkyWayIME.Tools.HostDiagSummarize\x64\Debug\mwime_hostdiag_summarize.exe --log "%TEMP%\MilkyWayIME\debug.log" --process KakaoTalk.exe
```

The summary should be interpreted together with the UIA probe output and the
visible input result. Do not classify a backend from `S_OK` or key-path
stability alone.

The summary also prints `surface_family`, `write_capability`, and
`excluded_reason` hints derived from the recorded text surface identity. These
are guardrails, not proof of visible input success. For example,
FluentSearch/Avalonia `SearchTextBox` is reported as `framework_avalonia`
with `uia_avalonia_searchbox_whole_value_replace`, WezTerm and Unity
top-level-window targets are reported as `window_only`, and `RICHEDIT50W` is
reported as a native RichEdit text surface.

If a transitory target is reported as `surface_family=unknown` and
`write_capability=unknown`, do not promote it to `TransitoryDirectText` from
TIP stability, full-context availability, document-feed text, or active
composition state alone. DirectText requires a verified native
selection-replacement surface such as `Edit` or `RICHEDIT50W`.

## Text Surface Probe

Use this after `SetValue()` succeeds but appears to replace the whole value or
move the caret. The default command is read-only and reports UIA selection
support plus any native HWND identity.

```bat
D:\Git\MilkyWayIME\build\MilkyWayIME.Tools.TextSurfaceProbe\x64\Debug\mwime_text_surface_probe.exe --sleep 3000 --redact-values
```

To test whether UIA `TextPattern` can move the caret to the end, run this while
the target input is focused. This may change the caret position.

```bat
D:\Git\MilkyWayIME\build\MilkyWayIME.Tools.TextSurfaceProbe\x64\Debug\mwime_text_surface_probe.exe --sleep 3000 --uia-caret-end --redact-values
```

For native Win32/RichEdit controls, use `--native-probe` to inspect
`EM_GETSEL` and text length. This is most useful when UIA reports a non-zero
native HWND such as `RICHEDIT50W`.

```bat
D:\Git\MilkyWayIME\build\MilkyWayIME.Tools.TextSurfaceProbe\x64\Debug\mwime_text_surface_probe.exe --sleep 3000 --native-probe --redact-values
```

Only when changing the focused control is acceptable, test native
selection-aware replacement.

```bat
D:\Git\MilkyWayIME\build\MilkyWayIME.Tools.TextSurfaceProbe\x64\Debug\mwime_text_surface_probe.exe --sleep 3000 --native-replace-selection --text "테스트" --redact-values
```

Interpretation:

- If `TextPattern.GetSelection()` and `--uia-caret-end` work visibly, a
  `SetValue()` fallback may be able to restore the caret after whole-value
  replacement.
- If `EM_GETSEL` and `EM_REPLACESEL` work on an `Edit`/`RichEdit` native HWND
  and the visible UI changes as expected, prefer a native selection-aware
  fallback over UIA `SetValue()`.
- Do not treat nonzero `EM_GETSEL`/`EM_REPLACESEL` return values on top-level
  non-edit windows as success. The native class and visible UI result must both
  match an editable text surface.
- If the focused element is a top-level `Window` with no caret and no editable
  native HWND, keep the host excluded even if text-related UIA patterns are
  reported.

## Message Trace

Use this when Microsoft IME can input text in a host but MilkyWayIME cannot.
Run one trace with Microsoft IME and one trace with MilkyWayIME, then compare
whether result-path messages such as `WM_IME_COMPOSITION` with
`GCS_RESULTSTR`, `WM_IME_CHAR`, or `WM_CHAR` appear.

```bat
D:\Git\MilkyWayIME\build\MilkyWayIME.Tools.MessageTrace\x64\Debug\mwime_message_trace.exe --process wezterm-gui.exe --duration 20 --clear
```

If the target process cannot be resolved reliably, focus the target window and
run without `--process`:

```bat
D:\Git\MilkyWayIME\build\MilkyWayIME.Tools.MessageTrace\x64\Debug\mwime_message_trace.exe --duration 20 --clear
```

For Unity or game hosts where the visible main window may not own the input
message path, hook every top-level window thread in the process:

```bat
D:\Git\MilkyWayIME\build\MilkyWayIME.Tools.MessageTrace\x64\Debug\mwime_message_trace.exe --process nikke.exe --all-threads --duration 20 --clear
```

If the hooks install successfully but no message lines are recorded, run a
short all-message trace to verify whether hook callbacks are firing and which
hidden window receives traffic:

```bat
D:\Git\MilkyWayIME\build\MilkyWayIME.Tools.MessageTrace\x64\Debug\mwime_message_trace.exe --process nikke.exe --all-threads --log-all --duration 5 --clear
```

Output:

```bat
%TEMP%\MilkyWayIME\message_trace.log
```

The tool now writes a diagnostic header before installing hooks. Check this
header first when a target host appears to produce no trace lines. It records:

- exact process matches and substring suggestions when `--process` does not
  match an executable name;
- selected pid, thread id, HWND, window class, and process bitness;
- visible top-level windows found for the process;
- trace thread count and `SetWindowsHookEx` results with `GetLastError` values
  for each installed hook.

If `--process Nikke.exe` produces only another application's class, the wrong
process was selected. If the target process is found but both hook handles are
zero, treat it as a hook installation failure and record the reported error and
bitness before drawing host compatibility conclusions.

Notes:

- This tool uses `WH_GETMESSAGE` and `WH_CALLWNDPROC`, so the hook DLL must be
  the same bitness as the target process.
- It records message names, HWND/class, pid/tid, `lParam`, and IME composition
  flags. Character payload messages are redacted instead of logging the input
  string.
- `--log-all` records every observed message number and should be used only for
  very short focused runs.
- Use this only for short focused comparison runs. It is a diagnostic hook, not
  a backend.

## Data To Record

- TIP lifecycle: activation, focus, pushed context, key-down path,
  flush/commit, write probe, composition termination.
- TIP identity: process, context, document manager, top context, view HWND,
  focus/caret HWNDs, HWND classes, `key_path_id` stability.
- UIA identity: `FrameworkId`, `ControlType`, `ClassName`, `AutomationId`,
  `Name`, supported patterns, `ValuePattern.IsReadOnly`.
- Write result: operation type, HRESULT, text length only, external empty
  termination, and visible UI success/failure.
- Classification: use the host-specific classification rules in
  `docs/host-compatibility.md`.
- Do-not-retry notes: record failed paths so later work does not repeat them
  without new evidence.

## Notes

- Keep original input strings out of logs and documentation. Record lengths and
  HRESULTs instead.
- `ControlType=Window` is excluded by default for UIA text-input backends even
  when text-related patterns are reported.
- Avalonia window class suffixes must be normalized as the `Avalonia` family;
  use focused UIA element identity for final matching.
