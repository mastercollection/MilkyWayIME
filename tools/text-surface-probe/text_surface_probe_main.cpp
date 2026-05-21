#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <richedit.h>

#include <cstdint>
#include <cwchar>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

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
  bool uia_caret_end = false;
  bool native_probe = false;
  bool native_replace_selection = false;
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
    } else if (arg == L"--uia-caret-end") {
      options.uia_caret_end = true;
    } else if (arg == L"--native-probe") {
      options.native_probe = true;
    } else if (arg == L"--native-replace-selection") {
      options.native_replace_selection = true;
      options.native_probe = true;
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
      << L"    [--uia-caret-end] [--native-probe]\n"
      << L"    [--native-replace-selection --text text]\n\n"
      << L"Default mode is read-only except normal UIA focus inspection.\n"
      << L"--uia-caret-end may move the focused control caret.\n"
      << L"--native-replace-selection sends EM_REPLACESEL to the native HWND.\n";
}

void PrintValueField(const wchar_t* label, const std::wstring& value,
                     bool redact) {
  std::wcout << label << L"_utf16_length=" << value.size();
  if (!redact) {
    std::wcout << L" " << label << L"=\"" << value << L"\"";
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
             << L" supported=" << BoolText(SUCCEEDED(pattern_hr)) << L"\n";
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

void ProbeTextPattern(IUIAutomationElement* element, const Options& options) {
  IUIAutomationTextPattern* text_pattern = nullptr;
  const HRESULT pattern_hr = element->GetCurrentPatternAs(
      UIA_TextPatternId, IID_PPV_ARGS(&text_pattern));
  std::wcout << L"text_pattern_hr=" << FormatHresult(pattern_hr)
             << L" supported=" << BoolText(SUCCEEDED(pattern_hr)) << L"\n";
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

  IUIAutomationTextRangeArray* selections = nullptr;
  const HRESULT selection_hr = text_pattern->GetSelection(&selections);
  std::wcout << L"text_get_selection_hr=" << FormatHresult(selection_hr);
  int selection_count = 0;
  if (SUCCEEDED(selection_hr) && selections != nullptr) {
    const HRESULT length_hr = selections->get_Length(&selection_count);
    std::wcout << L" text_selection_length_hr=" << FormatHresult(length_hr);
    if (FAILED(length_hr)) {
      selection_count = 0;
    }
  }
  std::wcout << L" selection_count=" << selection_count;
  std::wcout << L"\n";

  if (SUCCEEDED(selection_hr) && selections != nullptr) {
    for (int index = 0; index < selection_count; ++index) {
      IUIAutomationTextRange* selection = nullptr;
      if (SUCCEEDED(selections->GetElement(index, &selection)) && selection != nullptr) {
        BSTR selected_text = nullptr;
        const HRESULT selected_hr = selection->GetText(512, &selected_text);
        const std::wstring selected = BstrToString(selected_text);
        std::wcout << L"text_selection_" << index
                   << L"_get_hr=" << FormatHresult(selected_hr) << L" ";
        PrintValueField(L"text_selection", selected, options.redact_values);
        SysFreeString(selected_text);
        selection->Release();
      }
    }
  }
  SafeRelease(selections);

  if (options.uia_caret_end && document_range != nullptr) {
    IUIAutomationTextRange* end_range = nullptr;
    HRESULT clone_hr = document_range->Clone(&end_range);
    std::wcout << L"text_caret_end_clone_hr=" << FormatHresult(clone_hr)
               << L"\n";
    if (SUCCEEDED(clone_hr) && end_range != nullptr) {
      const HRESULT move_start_hr = end_range->MoveEndpointByRange(
          TextPatternRangeEndpoint_Start, document_range,
          TextPatternRangeEndpoint_End);
      const HRESULT move_end_hr = end_range->MoveEndpointByRange(
          TextPatternRangeEndpoint_End, document_range,
          TextPatternRangeEndpoint_End);
      const HRESULT select_hr = end_range->Select();
      std::wcout << L"text_caret_end_move_start_hr="
                 << FormatHresult(move_start_hr)
                 << L" text_caret_end_move_end_hr="
                 << FormatHresult(move_end_hr)
                 << L" text_caret_end_select_hr=" << FormatHresult(select_hr)
                 << L"\n";
      end_range->Release();
    }
  }

  SafeRelease(document_range);
  text_pattern->Release();
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

}  // namespace

int wmain(int argc, wchar_t** argv) {
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);

  const Options options = ParseArgs(argc, argv);
  if (options.help) {
    PrintUsage();
    return 1;
  }

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

  HWND native_window = nullptr;
  if (SUCCEEDED(focus_hr) && focused != nullptr) {
    PrintElementInfo(focused, options, &native_window);
    ProbeValuePattern(focused, options);
    ProbeTextPattern(focused, options);
  }

  if (options.native_probe) {
    HWND target = native_window != nullptr ? native_window : snapshot.focus;
    ProbeNative(target, options);
  }

  SafeRelease(focused);
  SafeRelease(automation);
  CoUninitialize();
  return 0;
}
