#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <oleacc.h>
#include <richedit.h>

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "tsf/host/uia_caret_diagnostics.h"

namespace {

template <typename Interface>
void SafeRelease(Interface*& pointer) {
  if (pointer != nullptr) {
    pointer->Release();
    pointer = nullptr;
  }
}

std::wstring FormatHresult(HRESULT hr) {
  std::wostringstream stream;
  stream << L"0x" << std::uppercase << std::hex << std::setw(8)
         << std::setfill(L'0') << static_cast<std::uint32_t>(hr);
  return stream.str();
}

std::wstring FormatHwnd(HWND hwnd) {
  std::wostringstream stream;
  stream << L"0x" << std::uppercase << std::hex
         << reinterpret_cast<std::uintptr_t>(hwnd);
  return stream.str();
}

std::wstring BoolText(bool value) { return value ? L"1" : L"0"; }

std::wstring BstrToString(BSTR value) {
  if (value == nullptr) {
    return {};
  }
  return std::wstring(value, SysStringLen(value));
}

std::wstring RectText(const RECT& rect) {
  std::wostringstream stream;
  stream << rect.left << L"," << rect.top << L"," << rect.right << L","
         << rect.bottom;
  return stream.str();
}

std::wstring ControlTypeName(CONTROLTYPEID id) {
  switch (id) {
    case UIA_EditControlTypeId:
      return L"Edit";
    case UIA_DocumentControlTypeId:
      return L"Document";
    case UIA_WindowControlTypeId:
      return L"Window";
    case UIA_PaneControlTypeId:
      return L"Pane";
    case UIA_TextControlTypeId:
      return L"Text";
    default:
      return L"id:" + std::to_wstring(id);
  }
}

struct Options {
  int sleep_ms = 0;
  bool help = false;
  bool redact_values = false;
  bool set_value = false;
  bool uia_caret_end = false;
  bool set_uia_caret_after_set_offset = false;
  bool send_end_after_set = false;
  bool native_probe = false;
  bool native_replace_selection = false;
  bool msaa_probe = false;
  bool ime_caret_overlay_probe = false;
  int uia_caret_after_set_offset = 0;
  int overlay_ms = 3000;
  int overlay_offset_x = 0;
  int overlay_offset_y = 0;
  std::wstring set_text = L"테스트";
  std::wstring native_text = L"테스트";
};

bool ParseInt(const wchar_t* text, int* value) {
  if (text == nullptr || value == nullptr) {
    return false;
  }
  wchar_t* end = nullptr;
  const long parsed = wcstol(text, &end, 10);
  if (end == text || *end != L'\0' || parsed < 0 || parsed > 60000) {
    return false;
  }
  *value = static_cast<int>(parsed);
  return true;
}

bool ParseSignedInt(const wchar_t* text, int* value) {
  if (text == nullptr || value == nullptr) {
    return false;
  }
  wchar_t* end = nullptr;
  const long parsed = wcstol(text, &end, 10);
  if (end == text || *end != L'\0' || parsed < -60000 || parsed > 60000) {
    return false;
  }
  *value = static_cast<int>(parsed);
  return true;
}

Options ParseArgs(int argc, wchar_t** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::wstring arg = argv[index];
    if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
      options.help = true;
    } else if (arg == L"--sleep" && index + 1 < argc) {
      if (!ParseInt(argv[++index], &options.sleep_ms)) {
        options.help = true;
      }
    } else if (arg == L"--redact-values") {
      options.redact_values = true;
    } else if (arg == L"--set" && index + 1 < argc) {
      options.set_value = true;
      options.set_text = argv[++index];
    } else if (arg == L"--uia-caret-end") {
      options.uia_caret_end = true;
    } else if (arg == L"--uia-caret-after-set-offset" &&
               index + 1 < argc) {
      options.set_uia_caret_after_set_offset = true;
      if (!ParseInt(argv[++index], &options.uia_caret_after_set_offset)) {
        options.help = true;
      }
    } else if (arg == L"--send-end-after-set" ||
               arg == L"--keyboard-end-after-set") {
      options.send_end_after_set = true;
    } else if (arg == L"--native-probe") {
      options.native_probe = true;
    } else if (arg == L"--native-replace-selection") {
      options.native_replace_selection = true;
      options.native_probe = true;
    } else if (arg == L"--msaa-probe") {
      options.msaa_probe = true;
    } else if (arg == L"--ime-caret-overlay-probe") {
      options.ime_caret_overlay_probe = true;
    } else if (arg == L"--overlay-ms" && index + 1 < argc) {
      if (!ParseInt(argv[++index], &options.overlay_ms)) {
        options.help = true;
      }
    } else if (arg == L"--overlay-offset-x" && index + 1 < argc) {
      if (!ParseSignedInt(argv[++index], &options.overlay_offset_x)) {
        options.help = true;
      }
    } else if (arg == L"--overlay-offset-y" && index + 1 < argc) {
      if (!ParseSignedInt(argv[++index], &options.overlay_offset_y)) {
        options.help = true;
      }
    } else if (arg == L"--text" && index + 1 < argc) {
      options.native_text = argv[++index];
    } else {
      options.help = true;
    }
  }
  return options;
}

void PrintUsage() {
  std::wcout
      << L"Usage:\n"
      << L"  mwime_text_surface_probe.exe [--sleep ms] [--redact-values]\n"
      << L"    [--set text] [--uia-caret-end]\n"
      << L"    [--uia-caret-after-set-offset utf16_offset]\n"
      << L"    [--send-end-after-set]\n"
      << L"    [--native-probe]\n"
      << L"    [--native-replace-selection --text text]\n"
      << L"    [--msaa-probe]\n"
      << L"    [--ime-caret-overlay-probe]\n"
      << L"    [--overlay-ms ms]\n"
      << L"    [--overlay-offset-x px] [--overlay-offset-y px]\n\n"
      << L"Default mode is read-only except normal UIA focus inspection.\n"
      << L"--set changes the focused UIA ValuePattern value.\n"
      << L"--uia-caret-end may move the focused control caret. With --set,\n"
      << L"  it runs after SetValue() and focused-element reacquisition.\n"
      << L"--uia-caret-after-set-offset moves the caret after SetValue().\n"
      << L"--send-end-after-set sends a diagnostic VK_END after SetValue().\n"
      << L"--native-replace-selection sends EM_REPLACESEL to the native HWND.\n"
      << L"--msaa-probe inspects LegacyIAccessible and direct MSAA objects.\n"
      << L"--ime-caret-overlay-probe shows a temporary diagnostic caret.\n";
}

milkyway::tsf::host::UiaCaretDiagnosticPlan BuildCaretPlan(
    const Options& options) {
  return milkyway::tsf::host::BuildUiaCaretDiagnosticPlan(
      milkyway::tsf::host::UiaCaretDiagnosticRequest{
          options.set_value,
          options.uia_caret_end,
          options.send_end_after_set,
          options.set_uia_caret_after_set_offset,
          options.uia_caret_after_set_offset,
      });
}

void PrintCaretPlan(const milkyway::tsf::host::UiaCaretDiagnosticPlan& plan) {
  std::wcout << L"caret_plan_set_value=" << BoolText(plan.set_value)
             << L" caret_plan_uia_before_set="
             << BoolText(plan.try_uia_caret_end_before_set)
             << L" caret_plan_reacquire_after_set="
             << BoolText(plan.reacquire_after_set)
             << L" caret_plan_uia_after_set="
             << BoolText(plan.try_uia_caret_end_after_set)
             << L" caret_plan_uia_offset_after_set="
             << BoolText(plan.try_uia_caret_offset_after_set)
             << L" caret_plan_offset_after_set="
             << plan.caret_offset_after_set
             << L" caret_plan_keyboard_end_after_set="
             << BoolText(plan.send_keyboard_end_after_set) << L"\n";
}

void PrintProbePhase(const wchar_t* phase) {
  std::wcout << L"probe_phase=" << (phase != nullptr ? phase : L"unknown")
             << L"\n";
}

void PrintValueField(std::wstring_view label, const std::wstring& value,
                     bool redact) {
  std::wcout << std::wstring(label) << L"_utf16_length=" << value.size();
  if (!redact) {
    std::wcout << L" " << std::wstring(label) << L"=\"" << value << L"\"";
  }
  std::wcout << L"\n";
}

struct WindowSnapshot {
  HWND foreground = nullptr;
  HWND focus = nullptr;
  HWND caret = nullptr;
  DWORD foreground_tid = 0;
  DWORD foreground_pid = 0;
  wchar_t foreground_class[256] = {};
  GUITHREADINFO gui = {};
  BOOL gui_ok = FALSE;
};

WindowSnapshot CaptureWindowSnapshot() {
  WindowSnapshot snapshot;
  snapshot.foreground = GetForegroundWindow();
  snapshot.foreground_tid =
      GetWindowThreadProcessId(snapshot.foreground, &snapshot.foreground_pid);
  GetClassNameW(snapshot.foreground, snapshot.foreground_class,
                static_cast<int>(std::size(snapshot.foreground_class)));
  snapshot.gui.cbSize = sizeof(snapshot.gui);
  snapshot.gui_ok = snapshot.foreground_tid != 0
                        ? GetGUIThreadInfo(snapshot.foreground_tid,
                                           &snapshot.gui)
                        : FALSE;
  snapshot.focus = snapshot.gui.hwndFocus;
  snapshot.caret = snapshot.gui.hwndCaret;
  return snapshot;
}

void PrintWindowSnapshot(const WindowSnapshot& snapshot) {
  std::wcout << L"foreground_hwnd=" << FormatHwnd(snapshot.foreground)
             << L" foreground_tid=" << snapshot.foreground_tid
             << L" foreground_pid=" << snapshot.foreground_pid
             << L" foreground_class=" << snapshot.foreground_class << L"\n";
  std::wcout << L"gui_ok=" << BoolText(snapshot.gui_ok != FALSE)
             << L" focus_hwnd=" << FormatHwnd(snapshot.focus)
             << L" caret_hwnd=" << FormatHwnd(snapshot.caret)
             << L" caret_rect=" << RectText(snapshot.gui.rcCaret) << L"\n";
}

std::wstring ElementString(IUIAutomationElement* element, PROPERTYID property,
                           HRESULT* out_hr) {
  VARIANT value{};
  VariantInit(&value);
  const HRESULT hr = element->GetCurrentPropertyValue(property, &value);
  if (out_hr != nullptr) {
    *out_hr = hr;
  }
  std::wstring text;
  if (SUCCEEDED(hr) && value.vt == VT_BSTR) {
    text = BstrToString(value.bstrVal);
  }
  VariantClear(&value);
  return text;
}

void PrintElementInfo(IUIAutomationElement* element, const Options& options,
                      HWND* native_window) {
  HRESULT name_hr = E_FAIL;
  const std::wstring name = ElementString(element, UIA_NamePropertyId, &name_hr);
  std::wcout << L"name_hr=" << FormatHresult(name_hr) << L" ";
  PrintValueField(L"name", name, options.redact_values);

  HRESULT automation_id_hr = E_FAIL;
  const std::wstring automation_id =
      ElementString(element, UIA_AutomationIdPropertyId, &automation_id_hr);
  std::wcout << L"automation_id_hr=" << FormatHresult(automation_id_hr)
             << L" automation_id=\"" << automation_id << L"\"\n";

  HRESULT class_name_hr = E_FAIL;
  const std::wstring class_name =
      ElementString(element, UIA_ClassNamePropertyId, &class_name_hr);
  std::wcout << L"class_name_hr=" << FormatHresult(class_name_hr)
             << L" class_name=\"" << class_name << L"\"\n";

  HRESULT framework_id_hr = E_FAIL;
  const std::wstring framework_id =
      ElementString(element, UIA_FrameworkIdPropertyId, &framework_id_hr);
  std::wcout << L"framework_id_hr=" << FormatHresult(framework_id_hr)
             << L" framework_id=\"" << framework_id << L"\"\n";

  CONTROLTYPEID control_type = 0;
  const HRESULT control_hr = element->get_CurrentControlType(&control_type);
  std::wcout << L"control_type_hr=" << FormatHresult(control_hr)
             << L" control_type=" << ControlTypeName(control_type) << L"\n";

  RECT rect{};
  const HRESULT rect_hr = element->get_CurrentBoundingRectangle(&rect);
  std::wcout << L"bounding_rect_hr=" << FormatHresult(rect_hr)
             << L" bounding_rect=" << RectText(rect) << L"\n";

  UIA_HWND native_hwnd = nullptr;
  const HRESULT native_hr = element->get_CurrentNativeWindowHandle(&native_hwnd);
  HWND hwnd = reinterpret_cast<HWND>(native_hwnd);
  if (native_window != nullptr) {
    *native_window = hwnd;
  }
  DWORD native_pid = 0;
  const DWORD native_tid =
      hwnd != nullptr ? GetWindowThreadProcessId(hwnd, &native_pid) : 0;
  wchar_t native_class[256] = {};
  if (hwnd != nullptr) {
    GetClassNameW(hwnd, native_class, static_cast<int>(std::size(native_class)));
  }
  std::wcout << L"native_hwnd_hr=" << FormatHresult(native_hr)
             << L" native_hwnd=" << FormatHwnd(hwnd)
             << L" native_tid=" << native_tid << L" native_pid=" << native_pid
             << L" native_class=" << native_class << L"\n";
}

void ProbeValuePattern(IUIAutomationElement* element, const Options& options) {
  IUIAutomationValuePattern* value_pattern = nullptr;
  const HRESULT pattern_hr = element->GetCurrentPatternAs(
      UIA_ValuePatternId, IID_PPV_ARGS(&value_pattern));
  std::wcout << L"value_pattern_hr=" << FormatHresult(pattern_hr)
             << L" supported="
             << BoolText(SUCCEEDED(pattern_hr) && value_pattern != nullptr)
             << L"\n";
  if (FAILED(pattern_hr) || value_pattern == nullptr) {
    return;
  }

  BOOL read_only = TRUE;
  const HRESULT read_only_hr = value_pattern->get_CurrentIsReadOnly(&read_only);
  BSTR current_value = nullptr;
  const HRESULT value_hr = value_pattern->get_CurrentValue(&current_value);
  const std::wstring current = BstrToString(current_value);
  std::wcout << L"value_readonly_hr=" << FormatHresult(read_only_hr)
             << L" readonly=" << BoolText(read_only != FALSE) << L"\n";
  std::wcout << L"value_hr=" << FormatHresult(value_hr) << L" ";
  PrintValueField(L"value", current, options.redact_values);
  SysFreeString(current_value);
  value_pattern->Release();
}

long TextRangeLength(IUIAutomationTextRange* range) {
  if (range == nullptr) {
    return -1;
  }
  BSTR text = nullptr;
  const HRESULT hr = range->GetText(-1, &text);
  if (FAILED(hr) || text == nullptr) {
    SysFreeString(text);
    return -1;
  }
  const long length = static_cast<long>(SysStringLen(text));
  SysFreeString(text);
  return length;
}

void PrintTextRangeSummary(std::wstring_view label,
                           IUIAutomationTextRange* range,
                           const Options& options) {
  if (range == nullptr) {
    std::wcout << std::wstring(label) << L"_range_available=0\n";
    return;
  }

  BSTR text = nullptr;
  const HRESULT text_hr = range->GetText(512, &text);
  const std::wstring value = BstrToString(text);
  std::wcout << std::wstring(label) << L"_range_available=1 "
             << std::wstring(label) << L"_get_hr=" << FormatHresult(text_hr)
             << L" ";
  PrintValueField(std::wstring(label) + L"_text", value,
                  options.redact_values);
  SysFreeString(text);
}

void PrintTextSelections(IUIAutomationTextPattern* text_pattern,
                         const Options& options, std::wstring_view label) {
  IUIAutomationTextRangeArray* selections = nullptr;
  const HRESULT selection_hr = text_pattern->GetSelection(&selections);
  std::wcout << std::wstring(label)
             << L"_get_selection_hr=" << FormatHresult(selection_hr);
  int selection_count = 0;
  if (SUCCEEDED(selection_hr) && selections != nullptr) {
    const HRESULT length_hr = selections->get_Length(&selection_count);
    std::wcout << L" " << std::wstring(label)
               << L"_selection_length_hr=" << FormatHresult(length_hr);
    if (FAILED(length_hr)) {
      selection_count = 0;
    }
  }
  std::wcout << L" " << std::wstring(label)
             << L"_selection_count=" << selection_count << L"\n";

  if (SUCCEEDED(selection_hr) && selections != nullptr) {
    for (int index = 0; index < selection_count; ++index) {
      IUIAutomationTextRange* selection = nullptr;
      if (SUCCEEDED(selections->GetElement(index, &selection)) &&
          selection != nullptr) {
        const std::wstring item_label =
            std::wstring(label) + L"_selection_" + std::to_wstring(index);
        PrintTextRangeSummary(item_label, selection, options);
        selection->Release();
      }
    }
  }
  SafeRelease(selections);
}

HRESULT MoveTextPatternCaretToDocumentEnd(
    IUIAutomationTextRange* document_range) {
  if (document_range == nullptr) {
    return E_INVALIDARG;
  }

  IUIAutomationTextRange* end_range = nullptr;
  HRESULT hr = document_range->Clone(&end_range);
  std::wcout << L"text_caret_end_clone_hr=" << FormatHresult(hr) << L"\n";
  if (FAILED(hr) || end_range == nullptr) {
    return FAILED(hr) ? hr : S_FALSE;
  }

  const HRESULT move_start_hr = end_range->MoveEndpointByRange(
      TextPatternRangeEndpoint_Start, document_range,
      TextPatternRangeEndpoint_End);
  const HRESULT move_end_hr = end_range->MoveEndpointByRange(
      TextPatternRangeEndpoint_End, document_range, TextPatternRangeEndpoint_End);
  const HRESULT select_hr = end_range->Select();
  std::wcout << L"text_caret_end_move_start_hr="
             << FormatHresult(move_start_hr)
             << L" text_caret_end_move_end_hr=" << FormatHresult(move_end_hr)
             << L" text_caret_end_select_hr=" << FormatHresult(select_hr)
             << L"\n";
  end_range->Release();
  return FAILED(move_start_hr) ? move_start_hr
       : FAILED(move_end_hr)  ? move_end_hr
                              : select_hr;
}

HRESULT MoveTextPatternCaretToOffset(IUIAutomationTextRange* document_range,
                                     int offset) {
  if (document_range == nullptr || offset < 0) {
    return E_INVALIDARG;
  }

  std::wcout << L"text_caret_offset_requested=" << offset << L"\n";
  IUIAutomationTextRange* offset_range = nullptr;
  HRESULT hr = document_range->Clone(&offset_range);
  std::wcout << L"text_caret_offset_clone_hr=" << FormatHresult(hr) << L"\n";
  if (FAILED(hr) || offset_range == nullptr) {
    return FAILED(hr) ? hr : S_FALSE;
  }

  const HRESULT move_end_to_start_hr = offset_range->MoveEndpointByRange(
      TextPatternRangeEndpoint_End, document_range,
      TextPatternRangeEndpoint_Start);
  const HRESULT move_start_to_start_hr = offset_range->MoveEndpointByRange(
      TextPatternRangeEndpoint_Start, document_range,
      TextPatternRangeEndpoint_Start);
  int moved = 0;
  HRESULT move_hr = S_OK;
  if (SUCCEEDED(move_end_to_start_hr) &&
      SUCCEEDED(move_start_to_start_hr) && offset > 0) {
    move_hr = offset_range->Move(TextUnit_Character, offset, &moved);
  }
  if (offset == 0) {
    moved = 0;
  }

  HRESULT select_hr = S_FALSE;
  if (SUCCEEDED(move_end_to_start_hr) &&
      SUCCEEDED(move_start_to_start_hr) && SUCCEEDED(move_hr) &&
      moved == offset) {
    select_hr = offset_range->Select();
  }
  std::wcout << L"text_caret_offset_move_end_to_start_hr="
             << FormatHresult(move_end_to_start_hr)
             << L" text_caret_offset_move_start_to_start_hr="
             << FormatHresult(move_start_to_start_hr)
             << L" text_caret_offset_move_hr=" << FormatHresult(move_hr)
             << L" text_caret_offset_moved=" << moved
             << L" text_caret_offset_select_hr="
             << FormatHresult(select_hr) << L"\n";
  offset_range->Release();
  return FAILED(move_end_to_start_hr) ? move_end_to_start_hr
       : FAILED(move_start_to_start_hr) ? move_start_to_start_hr
       : FAILED(move_hr) ? move_hr
       : moved != offset ? S_FALSE
                         : select_hr;
}

void ProbeTextPattern(IUIAutomationElement* element, const Options& options,
                      bool try_caret_end, bool try_caret_offset,
                      int caret_offset) {
  IUIAutomationTextPattern* text_pattern = nullptr;
  const HRESULT pattern_hr = element->GetCurrentPatternAs(
      UIA_TextPatternId, IID_PPV_ARGS(&text_pattern));
  std::wcout << L"text_pattern_hr=" << FormatHresult(pattern_hr)
             << L" supported="
             << BoolText(SUCCEEDED(pattern_hr) && text_pattern != nullptr)
             << L"\n";
  if (FAILED(pattern_hr) || text_pattern == nullptr) {
    return;
  }

  SupportedTextSelection selection_mode = SupportedTextSelection_None;
  const HRESULT selection_mode_hr =
      text_pattern->get_SupportedTextSelection(&selection_mode);
  std::wcout << L"text_supported_selection_hr="
             << FormatHresult(selection_mode_hr)
             << L" text_supported_selection=" << static_cast<int>(selection_mode)
             << L"\n";

  IUIAutomationTextRange* document_range = nullptr;
  const HRESULT document_hr = text_pattern->get_DocumentRange(&document_range);
  std::wcout << L"text_document_range_hr=" << FormatHresult(document_hr)
             << L"\n";
  if (SUCCEEDED(document_hr) && document_range != nullptr) {
    BSTR document_text = nullptr;
    const HRESULT text_hr = document_range->GetText(512, &document_text);
    const std::wstring text = BstrToString(document_text);
    std::wcout << L"text_get_hr=" << FormatHresult(text_hr) << L" ";
    PrintValueField(L"text", text, options.redact_values);
    SysFreeString(document_text);
  }

  PrintTextSelections(text_pattern, options, L"text");

  if (try_caret_end && document_range != nullptr) {
    const HRESULT caret_hr = MoveTextPatternCaretToDocumentEnd(document_range);
    std::wcout << L"text_caret_end_overall_hr=" << FormatHresult(caret_hr)
               << L"\n";
    PrintTextSelections(text_pattern, options, L"text_after_caret_end");
  }

  if (try_caret_offset && document_range != nullptr) {
    const HRESULT caret_hr =
        MoveTextPatternCaretToOffset(document_range, caret_offset);
    std::wcout << L"text_caret_offset_overall_hr="
               << FormatHresult(caret_hr) << L"\n";
    PrintTextSelections(text_pattern, options, L"text_after_caret_offset");
  }

  SafeRelease(document_range);
  text_pattern->Release();
}

void ProbeTextPattern2(IUIAutomationElement* element, const Options& options) {
  IUIAutomationTextPattern2* text_pattern2 = nullptr;
  const HRESULT pattern_hr = element->GetCurrentPatternAs(
      UIA_TextPattern2Id, IID_PPV_ARGS(&text_pattern2));
  std::wcout << L"text2_pattern_hr=" << FormatHresult(pattern_hr)
             << L" supported="
             << BoolText(SUCCEEDED(pattern_hr) && text_pattern2 != nullptr)
             << L"\n";
  if (FAILED(pattern_hr) || text_pattern2 == nullptr) {
    return;
  }

  BOOL is_active = FALSE;
  IUIAutomationTextRange* caret_range = nullptr;
  const HRESULT caret_hr =
      text_pattern2->GetCaretRange(&is_active, &caret_range);
  std::wcout << L"text2_get_caret_range_hr=" << FormatHresult(caret_hr)
             << L" text2_caret_active=" << BoolText(is_active != FALSE)
             << L"\n";
  if (SUCCEEDED(caret_hr) && caret_range != nullptr) {
    PrintTextRangeSummary(L"text2_caret", caret_range, options);
    caret_range->Release();
  }

  text_pattern2->Release();
}

void ProbeTextEditPattern(IUIAutomationElement* element,
                          const Options& options) {
  IUIAutomationTextEditPattern* text_edit_pattern = nullptr;
  const HRESULT pattern_hr = element->GetCurrentPatternAs(
      UIA_TextEditPatternId, IID_PPV_ARGS(&text_edit_pattern));
  std::wcout << L"text_edit_pattern_hr=" << FormatHresult(pattern_hr)
             << L" supported="
             << BoolText(SUCCEEDED(pattern_hr) && text_edit_pattern != nullptr)
             << L"\n";
  if (FAILED(pattern_hr) || text_edit_pattern == nullptr) {
    return;
  }

  IUIAutomationTextRange* active_composition = nullptr;
  const HRESULT active_hr =
      text_edit_pattern->GetActiveComposition(&active_composition);
  std::wcout << L"text_edit_active_composition_hr="
             << FormatHresult(active_hr) << L"\n";
  if (SUCCEEDED(active_hr) && active_composition != nullptr) {
    PrintTextRangeSummary(L"text_edit_active_composition", active_composition,
                          options);
    active_composition->Release();
  }

  IUIAutomationTextRange* conversion_target = nullptr;
  const HRESULT conversion_hr =
      text_edit_pattern->GetConversionTarget(&conversion_target);
  std::wcout << L"text_edit_conversion_target_hr="
             << FormatHresult(conversion_hr) << L"\n";
  if (SUCCEEDED(conversion_hr) && conversion_target != nullptr) {
    PrintTextRangeSummary(L"text_edit_conversion_target", conversion_target,
                          options);
    conversion_target->Release();
  }

  text_edit_pattern->Release();
}

void ProbeLegacyIAccessiblePattern(IUIAutomationElement* element,
                                   const Options& options) {
  IUIAutomationLegacyIAccessiblePattern* legacy_pattern = nullptr;
  const HRESULT pattern_hr = element->GetCurrentPatternAs(
      UIA_LegacyIAccessiblePatternId, IID_PPV_ARGS(&legacy_pattern));
  std::wcout << L"legacy_pattern_hr=" << FormatHresult(pattern_hr)
             << L" supported="
             << BoolText(SUCCEEDED(pattern_hr) && legacy_pattern != nullptr)
             << L"\n";
  if (FAILED(pattern_hr) || legacy_pattern == nullptr) {
    return;
  }

  int child_id = 0;
  const HRESULT child_hr = legacy_pattern->get_CurrentChildId(&child_id);
  DWORD role = 0;
  const HRESULT role_hr = legacy_pattern->get_CurrentRole(&role);
  DWORD state = 0;
  const HRESULT state_hr = legacy_pattern->get_CurrentState(&state);
  std::wcout << L"legacy_child_id_hr=" << FormatHresult(child_hr)
             << L" legacy_child_id=" << child_id
             << L" legacy_role_hr=" << FormatHresult(role_hr)
             << L" legacy_role=" << role
             << L" legacy_state_hr=" << FormatHresult(state_hr)
             << L" legacy_state=0x" << std::uppercase << std::hex << state
             << std::dec << L"\n";

  BSTR name = nullptr;
  const HRESULT name_hr = legacy_pattern->get_CurrentName(&name);
  std::wcout << L"legacy_name_hr=" << FormatHresult(name_hr) << L" ";
  PrintValueField(L"legacy_name", BstrToString(name), options.redact_values);
  SysFreeString(name);

  BSTR value = nullptr;
  const HRESULT value_hr = legacy_pattern->get_CurrentValue(&value);
  std::wcout << L"legacy_value_hr=" << FormatHresult(value_hr) << L" ";
  PrintValueField(L"legacy_value", BstrToString(value), options.redact_values);
  SysFreeString(value);

  BSTR default_action = nullptr;
  const HRESULT default_action_hr =
      legacy_pattern->get_CurrentDefaultAction(&default_action);
  std::wcout << L"legacy_default_action_hr="
             << FormatHresult(default_action_hr) << L" ";
  PrintValueField(L"legacy_default_action", BstrToString(default_action),
                  options.redact_values);
  SysFreeString(default_action);

  IAccessible* accessible = nullptr;
  const HRESULT accessible_hr = legacy_pattern->GetIAccessible(&accessible);
  std::wcout << L"legacy_get_iaccessible_hr="
             << FormatHresult(accessible_hr)
             << L" legacy_iaccessible_available="
             << BoolText(SUCCEEDED(accessible_hr) && accessible != nullptr)
             << L"\n";
  SafeRelease(accessible);
  legacy_pattern->Release();
}

HRESULT SetValuePattern(IUIAutomationElement* element, const Options& options) {
  IUIAutomationValuePattern* value_pattern = nullptr;
  HRESULT hr = element->GetCurrentPatternAs(UIA_ValuePatternId,
                                            IID_PPV_ARGS(&value_pattern));
  std::wcout << L"value_set_pattern_hr=" << FormatHresult(hr)
             << L" value_set_supported="
             << BoolText(SUCCEEDED(hr) && value_pattern != nullptr) << L"\n";
  if (FAILED(hr) || value_pattern == nullptr) {
    return FAILED(hr) ? hr : S_FALSE;
  }

  BOOL read_only = TRUE;
  hr = value_pattern->get_CurrentIsReadOnly(&read_only);
  std::wcout << L"value_set_readonly_hr=" << FormatHresult(hr)
             << L" value_set_readonly=" << BoolText(read_only != FALSE)
             << L"\n";
  if (FAILED(hr) || read_only != FALSE) {
    value_pattern->Release();
    return FAILED(hr) ? hr : E_ACCESSDENIED;
  }

  BSTR value = SysAllocStringLen(options.set_text.data(),
                                 static_cast<UINT>(options.set_text.size()));
  if (value == nullptr && !options.set_text.empty()) {
    value_pattern->Release();
    return E_OUTOFMEMORY;
  }
  hr = value_pattern->SetValue(value);
  SysFreeString(value);
  value_pattern->Release();
  std::wcout << L"value_set_hr=" << FormatHresult(hr)
             << L" value_set_utf16_length=" << options.set_text.size()
             << L"\n";
  return hr;
}

HRESULT ReacquireFocusedElement(IUIAutomation* automation,
                                IUIAutomationElement** focused) {
  if (automation == nullptr || focused == nullptr) {
    return E_INVALIDARG;
  }
  SafeRelease(*focused);
  const HRESULT hr = automation->GetFocusedElement(focused);
  std::wcout << L"focused_reacquire_hr=" << FormatHresult(hr) << L"\n";
  return hr;
}

void SendKeyboardEndAfterSet() {
  INPUT inputs[2] = {};
  inputs[0].type = INPUT_KEYBOARD;
  inputs[0].ki.wVk = VK_END;
  inputs[1].type = INPUT_KEYBOARD;
  inputs[1].ki.wVk = VK_END;
  inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

  SetLastError(ERROR_SUCCESS);
  const UINT sent = SendInput(static_cast<UINT>(std::size(inputs)), inputs,
                              static_cast<int>(sizeof(INPUT)));
  const DWORD error = GetLastError();
  std::wcout << L"keyboard_end_sendinput_count=" << sent
             << L" keyboard_end_sendinput_error=0x" << std::uppercase
             << std::hex << error << std::dec << L"\n";
}

void ProbeNative(HWND hwnd, const Options& options) {
  std::wcout << L"native_probe_hwnd=" << FormatHwnd(hwnd) << L"\n";
  if (hwnd == nullptr) {
    std::wcout << L"native_probe_available=0\n";
    return;
  }

  wchar_t class_name[256] = {};
  GetClassNameW(hwnd, class_name, static_cast<int>(std::size(class_name)));
  std::wcout << L"native_probe_class=" << class_name << L"\n";

  DWORD start = 0;
  DWORD end = 0;
  const LRESULT get_sel = SendMessageTimeoutW(
      hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&start),
      reinterpret_cast<LPARAM>(&end), SMTO_ABORTIFHUNG, 1000, nullptr);
  std::wcout << L"native_em_getsel_result=" << get_sel
             << L" native_sel_start=" << start << L" native_sel_end=" << end
             << L"\n";

  const LRESULT text_length = SendMessageTimeoutW(
      hwnd, WM_GETTEXTLENGTH, 0, 0, SMTO_ABORTIFHUNG, 1000, nullptr);
  std::wcout << L"native_wm_gettextlength_result=" << text_length << L"\n";

  if (options.native_replace_selection) {
    const LRESULT replace_result = SendMessageTimeoutW(
        hwnd, EM_REPLACESEL, TRUE,
        reinterpret_cast<LPARAM>(options.native_text.c_str()),
        SMTO_ABORTIFHUNG, 1000, nullptr);
    std::wcout << L"native_em_replacesel_result=" << replace_result
               << L" native_replace_text_utf16_length="
               << options.native_text.size() << L"\n";

    DWORD after_start = 0;
    DWORD after_end = 0;
    const LRESULT after_get_sel = SendMessageTimeoutW(
        hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&after_start),
        reinterpret_cast<LPARAM>(&after_end), SMTO_ABORTIFHUNG, 1000, nullptr);
    std::wcout << L"native_after_em_getsel_result=" << after_get_sel
               << L" native_after_sel_start=" << after_start
               << L" native_after_sel_end=" << after_end << L"\n";
  }
}

VARIANT ChildSelf() {
  VARIANT child{};
  VariantInit(&child);
  child.vt = VT_I4;
  child.lVal = CHILDID_SELF;
  return child;
}

std::wstring VariantSummary(const VARIANT& value) {
  std::wostringstream stream;
  stream << L"vt=" << value.vt;
  switch (value.vt) {
    case VT_EMPTY:
      stream << L",empty";
      break;
    case VT_I4:
      stream << L",i4=" << value.lVal;
      break;
    case VT_DISPATCH:
      stream << L",dispatch=" << BoolText(value.pdispVal != nullptr);
      break;
    case VT_UNKNOWN:
      stream << L",unknown=" << BoolText(value.punkVal != nullptr);
      break;
    default:
      break;
  }
  return stream.str();
}

std::wstring MsaaObjectName(LONG object_id) {
  switch (object_id) {
    case OBJID_CLIENT:
      return L"client";
    case OBJID_CARET:
      return L"caret";
    default:
      return L"id:" + std::to_wstring(object_id);
  }
}

void PrintMsaaStringField(const wchar_t* label, HRESULT hr, BSTR value,
                          const Options& options) {
  std::wcout << label << L"_hr=" << FormatHresult(hr) << L" ";
  PrintValueField(label, BstrToString(value), options.redact_values);
}

void ProbeAccessibleObject(IAccessible* accessible,
                           const std::wstring& label,
                           const Options& options) {
  if (accessible == nullptr) {
    std::wcout << label << L"_available=0\n";
    return;
  }
  std::wcout << label << L"_available=1\n";

  long child_count = 0;
  const HRESULT child_count_hr = accessible->get_accChildCount(&child_count);
  std::wcout << label << L"_child_count_hr=" << FormatHresult(child_count_hr)
             << L" " << label << L"_child_count=" << child_count << L"\n";

  VARIANT self = ChildSelf();
  BSTR name = nullptr;
  const HRESULT name_hr = accessible->get_accName(self, &name);
  PrintMsaaStringField((label + L"_name").c_str(), name_hr, name, options);
  SysFreeString(name);

  BSTR value = nullptr;
  const HRESULT value_hr = accessible->get_accValue(self, &value);
  PrintMsaaStringField((label + L"_value").c_str(), value_hr, value, options);
  SysFreeString(value);

  BSTR default_action = nullptr;
  const HRESULT default_action_hr =
      accessible->get_accDefaultAction(self, &default_action);
  PrintMsaaStringField((label + L"_default_action").c_str(),
                       default_action_hr, default_action, options);
  SysFreeString(default_action);

  VARIANT role{};
  VariantInit(&role);
  const HRESULT role_hr = accessible->get_accRole(self, &role);
  std::wcout << label << L"_role_hr=" << FormatHresult(role_hr)
             << L" " << label << L"_role=" << VariantSummary(role) << L"\n";
  VariantClear(&role);

  VARIANT state{};
  VariantInit(&state);
  const HRESULT state_hr = accessible->get_accState(self, &state);
  std::wcout << label << L"_state_hr=" << FormatHresult(state_hr)
             << L" " << label << L"_state=" << VariantSummary(state) << L"\n";
  VariantClear(&state);

  VARIANT focus{};
  VariantInit(&focus);
  const HRESULT focus_hr = accessible->get_accFocus(&focus);
  std::wcout << label << L"_focus_hr=" << FormatHresult(focus_hr)
             << L" " << label << L"_focus=" << VariantSummary(focus) << L"\n";
  VariantClear(&focus);

  VARIANT selection{};
  VariantInit(&selection);
  const HRESULT selection_hr = accessible->get_accSelection(&selection);
  std::wcout << label << L"_selection_hr=" << FormatHresult(selection_hr)
             << L" " << label << L"_selection=" << VariantSummary(selection)
             << L"\n";
  VariantClear(&selection);

  long x = 0;
  long y = 0;
  long width = 0;
  long height = 0;
  const HRESULT location_hr =
      accessible->accLocation(&x, &y, &width, &height, self);
  std::wcout << label << L"_location_hr=" << FormatHresult(location_hr)
             << L" " << label << L"_location=" << x << L"," << y << L","
             << width << L"," << height << L"\n";
}

void ProbeMsaaObjectFromWindow(HWND hwnd, LONG object_id,
                               const Options& options) {
  std::wcout << L"msaa_object_hwnd=" << FormatHwnd(hwnd)
             << L" msaa_object_id=" << MsaaObjectName(object_id) << L"\n";
  if (hwnd == nullptr) {
    std::wcout << L"msaa_object_available=0\n";
    return;
  }

  IAccessible* accessible = nullptr;
  const HRESULT hr = AccessibleObjectFromWindow(
      hwnd, object_id, IID_IAccessible, reinterpret_cast<void**>(&accessible));
  std::wcout << L"msaa_accessible_from_window_hr=" << FormatHresult(hr)
             << L" msaa_accessible_available="
             << BoolText(SUCCEEDED(hr) && accessible != nullptr) << L"\n";
  if (SUCCEEDED(hr) && accessible != nullptr) {
    const std::wstring label =
        L"msaa_" + MsaaObjectName(object_id) + L"_" +
        FormatHwnd(hwnd).substr(2);
    ProbeAccessibleObject(accessible, label, options);
  }
  SafeRelease(accessible);
}

void AddUniqueHwnd(std::vector<HWND>* hwnds, HWND hwnd) {
  if (hwnds == nullptr || hwnd == nullptr) {
    return;
  }
  for (HWND existing : *hwnds) {
    if (existing == hwnd) {
      return;
    }
  }
  hwnds->push_back(hwnd);
}

void ProbeMsaa(const WindowSnapshot& snapshot, HWND native_window,
               const Options& options) {
  std::vector<HWND> hwnds;
  AddUniqueHwnd(&hwnds, snapshot.focus);
  AddUniqueHwnd(&hwnds, native_window);
  AddUniqueHwnd(&hwnds, snapshot.foreground);

  std::wcout << L"msaa_probe_hwnd_count=" << hwnds.size() << L"\n";
  for (HWND hwnd : hwnds) {
    wchar_t class_name[256] = {};
    GetClassNameW(hwnd, class_name, static_cast<int>(std::size(class_name)));
    std::wcout << L"msaa_probe_hwnd=" << FormatHwnd(hwnd)
               << L" msaa_probe_class=" << class_name << L"\n";
    ProbeMsaaObjectFromWindow(hwnd, OBJID_CLIENT, options);
    ProbeMsaaObjectFromWindow(hwnd, OBJID_CARET, options);
  }
}

LRESULT CALLBACK OverlayCaretWindowProc(HWND hwnd, UINT message, WPARAM wparam,
                                        LPARAM lparam) {
  switch (message) {
    case WM_PAINT: {
      PAINTSTRUCT paint = {};
      HDC dc = BeginPaint(hwnd, &paint);
      if (dc != nullptr) {
        RECT client_rect = {};
        GetClientRect(hwnd, &client_rect);
        HBRUSH brush = CreateSolidBrush(RGB(255, 0, 80));
        if (brush != nullptr) {
          FillRect(dc, &client_rect, brush);
          DeleteObject(brush);
        }
        EndPaint(hwnd, &paint);
      }
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    default:
      break;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

bool RegisterOverlayCaretWindowClass(HINSTANCE instance) {
  constexpr wchar_t kClassName[] = L"MilkyWayTextSurfaceOverlayCaret";
  WNDCLASSEXW window_class = {};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = OverlayCaretWindowProc;
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.lpszClassName = kClassName;
  const ATOM atom = RegisterClassExW(&window_class);
  return atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool IsUsableRect(const RECT& rect) {
  return rect.left != 0 || rect.top != 0 || rect.right != 0 ||
         rect.bottom != 0;
}

bool TryMsaaCaretRect(HWND hwnd, RECT* rect) {
  if (hwnd == nullptr || rect == nullptr) {
    return false;
  }
  IAccessible* accessible = nullptr;
  const HRESULT object_hr = AccessibleObjectFromWindow(
      hwnd, static_cast<DWORD>(OBJID_CARET), IID_IAccessible,
      reinterpret_cast<void**>(&accessible));
  if (FAILED(object_hr) || accessible == nullptr) {
    SafeRelease(accessible);
    return false;
  }

  VARIANT self = ChildSelf();
  long x = 0;
  long y = 0;
  long width = 0;
  long height = 0;
  const HRESULT location_hr =
      accessible->accLocation(&x, &y, &width, &height, self);
  SafeRelease(accessible);
  if (FAILED(location_hr) || (x == 0 && y == 0 && width == 0 && height == 0)) {
    return false;
  }

  rect->left = x;
  rect->top = y;
  rect->right = x + std::max<LONG>(width, 2);
  rect->bottom = y + std::max<LONG>(height, 18);
  return true;
}

bool TryGuiCaretRect(const WindowSnapshot& snapshot, RECT* rect) {
  if (rect == nullptr || snapshot.caret == nullptr ||
      !IsUsableRect(snapshot.gui.rcCaret)) {
    return false;
  }

  POINT top_left{snapshot.gui.rcCaret.left, snapshot.gui.rcCaret.top};
  POINT bottom_right{snapshot.gui.rcCaret.right, snapshot.gui.rcCaret.bottom};
  if (!ClientToScreen(snapshot.caret, &top_left) ||
      !ClientToScreen(snapshot.caret, &bottom_right)) {
    return false;
  }
  rect->left = top_left.x;
  rect->top = top_left.y;
  rect->right = std::max(top_left.x + 2, bottom_right.x);
  rect->bottom = std::max(top_left.y + 18, bottom_right.y);
  return true;
}

bool TryFocusedElementRect(IUIAutomationElement* focused, RECT* rect) {
  if (focused == nullptr || rect == nullptr) {
    return false;
  }
  RECT bounds = {};
  const HRESULT bounds_hr = focused->get_CurrentBoundingRectangle(&bounds);
  if (FAILED(bounds_hr) || !IsUsableRect(bounds)) {
    return false;
  }
  rect->left = bounds.left + 6;
  rect->right = rect->left + 2;
  rect->bottom = bounds.bottom - 6;
  rect->top = rect->bottom - 20;
  return true;
}

RECT NormalizeOverlayRect(RECT rect, const Options& options) {
  rect.left += options.overlay_offset_x;
  rect.right += options.overlay_offset_x;
  rect.top += options.overlay_offset_y;
  rect.bottom += options.overlay_offset_y;

  const LONG width = std::max<LONG>(2, rect.right - rect.left);
  const LONG height = std::max<LONG>(18, rect.bottom - rect.top);
  rect.right = rect.left + width;
  rect.bottom = rect.top + height;
  return rect;
}

void RunImeCaretOverlayProbe(const WindowSnapshot& snapshot,
                             IUIAutomationElement* focused,
                             const Options& options) {
  RECT rect = {};
  const wchar_t* source = L"none";
  if (TryMsaaCaretRect(snapshot.focus, &rect) ||
      TryMsaaCaretRect(snapshot.foreground, &rect)) {
    source = L"msaa_caret";
  } else if (TryGuiCaretRect(snapshot, &rect)) {
    source = L"gui_caret";
  } else if (TryFocusedElementRect(focused, &rect)) {
    source = L"focused_element_bounds";
  } else {
    std::wcout << L"overlay_caret_available=0 overlay_caret_source=none\n";
    return;
  }

  rect = NormalizeOverlayRect(rect, options);
  std::wcout << L"overlay_caret_available=1 overlay_caret_source=" << source
             << L" overlay_caret_rect=" << RectText(rect)
             << L" overlay_caret_ms=" << options.overlay_ms << L"\n";

  HINSTANCE instance = GetModuleHandleW(nullptr);
  constexpr wchar_t kClassName[] = L"MilkyWayTextSurfaceOverlayCaret";
  if (!RegisterOverlayCaretWindowClass(instance)) {
    std::wcout << L"overlay_register_error=0x" << std::uppercase << std::hex
               << GetLastError() << std::dec << L"\n";
    return;
  }

  HWND hwnd = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClassName, L"",
      WS_POPUP, rect.left, rect.top, rect.right - rect.left,
      rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
  std::wcout << L"overlay_create_hwnd=" << FormatHwnd(hwnd)
             << L" overlay_create_error=0x" << std::uppercase << std::hex
             << (hwnd == nullptr ? GetLastError() : ERROR_SUCCESS) << std::dec
             << L"\n";
  if (hwnd == nullptr) {
    return;
  }

  ShowWindow(hwnd, SW_SHOWNOACTIVATE);
  UpdateWindow(hwnd);
  Sleep(static_cast<DWORD>(options.overlay_ms));
  DestroyWindow(hwnd);
  std::wcout << L"overlay_destroyed=1\n";
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);

  const Options options = ParseArgs(argc, argv);
  if (options.help) {
    PrintUsage();
    return 1;
  }
  const milkyway::tsf::host::UiaCaretDiagnosticPlan caret_plan =
      BuildCaretPlan(options);

  if (options.sleep_ms > 0) {
    Sleep(static_cast<DWORD>(options.sleep_ms));
  }

  const WindowSnapshot snapshot = CaptureWindowSnapshot();
  PrintWindowSnapshot(snapshot);

  const HRESULT init_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  std::wcout << L"coinitialize_hr=" << FormatHresult(init_hr) << L"\n";
  if (FAILED(init_hr)) {
    return 2;
  }

  IUIAutomation* automation = nullptr;
  const HRESULT create_hr =
      CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                       IID_PPV_ARGS(&automation));
  std::wcout << L"uia_create_hr=" << FormatHresult(create_hr) << L"\n";
  if (FAILED(create_hr) || automation == nullptr) {
    CoUninitialize();
    return 3;
  }

  IUIAutomationElement* focused = nullptr;
  const HRESULT focus_hr = automation->GetFocusedElement(&focused);
  std::wcout << L"focused_element_hr=" << FormatHresult(focus_hr) << L"\n";
  PrintCaretPlan(caret_plan);

  HWND native_window = nullptr;
  if (SUCCEEDED(focus_hr) && focused != nullptr) {
    PrintProbePhase(L"before_set");
    PrintElementInfo(focused, options, &native_window);
    ProbeValuePattern(focused, options);
    ProbeTextPattern(focused, options,
                     caret_plan.try_uia_caret_end_before_set, false, 0);
    ProbeTextPattern2(focused, options);
    ProbeTextEditPattern(focused, options);
    if (options.msaa_probe) {
      ProbeLegacyIAccessiblePattern(focused, options);
    }
  }

  HRESULT set_hr = S_FALSE;
  if (caret_plan.set_value && focused != nullptr) {
    set_hr = SetValuePattern(focused, options);
    if (caret_plan.reacquire_after_set && set_hr == S_OK) {
      Sleep(100);
      const HRESULT reacquire_hr = ReacquireFocusedElement(automation, &focused);
      if (SUCCEEDED(reacquire_hr) && focused != nullptr) {
        PrintProbePhase(L"after_set_reacquire");
        PrintElementInfo(focused, options, &native_window);
        ProbeValuePattern(focused, options);
        ProbeTextPattern(focused, options,
                         caret_plan.try_uia_caret_end_after_set,
                         caret_plan.try_uia_caret_offset_after_set,
                         caret_plan.caret_offset_after_set);
        ProbeTextPattern2(focused, options);
        ProbeTextEditPattern(focused, options);
        if (options.msaa_probe) {
          ProbeLegacyIAccessiblePattern(focused, options);
        }
      }
    }
  }

  if (caret_plan.send_keyboard_end_after_set && set_hr == S_OK) {
    SendKeyboardEndAfterSet();
    Sleep(100);
    const HRESULT reacquire_hr = ReacquireFocusedElement(automation, &focused);
    if (SUCCEEDED(reacquire_hr) && focused != nullptr) {
      PrintProbePhase(L"after_keyboard_end");
      PrintElementInfo(focused, options, &native_window);
      ProbeValuePattern(focused, options);
      ProbeTextPattern(focused, options, false, false, 0);
      ProbeTextPattern2(focused, options);
      ProbeTextEditPattern(focused, options);
      if (options.msaa_probe) {
        ProbeLegacyIAccessiblePattern(focused, options);
      }
    }
  }

  if (options.native_probe) {
    HWND target = native_window != nullptr ? native_window : snapshot.focus;
    ProbeNative(target, options);
  }

  if (options.msaa_probe) {
    ProbeMsaa(snapshot, native_window, options);
  }

  if (options.ime_caret_overlay_probe) {
    const WindowSnapshot overlay_snapshot = CaptureWindowSnapshot();
    RunImeCaretOverlayProbe(overlay_snapshot, focused, options);
  }

  SafeRelease(focused);
  SafeRelease(automation);
  CoUninitialize();
  return 0;
}
