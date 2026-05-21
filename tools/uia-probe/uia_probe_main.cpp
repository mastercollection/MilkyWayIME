#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>

#include <cstdint>
#include <cwchar>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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

std::wstring BstrToString(BSTR value) {
  if (value == nullptr) {
    return {};
  }
  return std::wstring(value, SysStringLen(value));
}

std::wstring BoolText(BOOL value) {
  return value ? L"1" : L"0";
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
    case UIA_ListControlTypeId:
      return L"List";
    case UIA_ListItemControlTypeId:
      return L"ListItem";
    case UIA_ButtonControlTypeId:
      return L"Button";
    default:
      return L"id:" + std::to_wstring(id);
  }
}

struct Options {
  int sleep_ms = 0;
  bool help = false;
  bool set_value = false;
  bool redact_values = false;
  std::wstring value;
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
    } else if (arg == L"--set" && index + 1 < argc) {
      options.set_value = true;
      options.value = argv[++index];
    } else if (arg == L"--redact-values") {
      options.redact_values = true;
    } else {
      options.help = true;
    }
  }
  return options;
}

void PrintUsage() {
  std::wcout
      << L"Usage:\n"
      << L"  mwime_uia_probe.exe [--sleep ms] [--set text] [--redact-values]\n\n"
      << L"Focus the target input first. Without --set this only reports UIA "
         L"capabilities.\n";
}

void PrintValueField(const wchar_t* label, const std::wstring& value,
                     bool redact) {
  std::wcout << label << L"_utf16_length=" << value.size();
  if (!redact) {
    std::wcout << L" " << label << L"=\"" << value << L"\"";
  }
  std::wcout << L"\n";
}

void PrintWindowSnapshot() {
  const HWND foreground = GetForegroundWindow();
  DWORD pid = 0;
  const DWORD tid = GetWindowThreadProcessId(foreground, &pid);
  wchar_t class_name[256] = {};
  GetClassNameW(foreground, class_name, static_cast<int>(std::size(class_name)));

  GUITHREADINFO gui{};
  gui.cbSize = sizeof(gui);
  const BOOL gui_ok = tid != 0 ? GetGUIThreadInfo(tid, &gui) : FALSE;

  std::wcout << L"foreground_hwnd=" << FormatHwnd(foreground)
             << L" foreground_tid=" << tid << L" foreground_pid=" << pid
             << L" foreground_class=" << class_name << L"\n";
  std::wcout << L"gui_ok=" << BoolText(gui_ok)
             << L" focus_hwnd=" << FormatHwnd(gui.hwndFocus)
             << L" caret_hwnd=" << FormatHwnd(gui.hwndCaret)
             << L" caret_rect=" << RectText(gui.rcCaret) << L"\n";
}

void PrintElementString(IUIAutomationElement* element, PROPERTYID property,
                        const wchar_t* label) {
  VARIANT value{};
  VariantInit(&value);
  const HRESULT hr = element->GetCurrentPropertyValue(property, &value);
  std::wstring text;
  if (SUCCEEDED(hr) && value.vt == VT_BSTR) {
    text = BstrToString(value.bstrVal);
  }
  std::wcout << label << L"_hr=" << FormatHresult(hr) << L" " << label
             << L"=\"" << text << L"\"\n";
  VariantClear(&value);
}

void PrintElementBool(IUIAutomationElement* element, PROPERTYID property,
                      const wchar_t* label) {
  VARIANT value{};
  VariantInit(&value);
  const HRESULT hr = element->GetCurrentPropertyValue(property, &value);
  const bool bool_value =
      SUCCEEDED(hr) && value.vt == VT_BOOL && value.boolVal != VARIANT_FALSE;
  std::wcout << label << L"_hr=" << FormatHresult(hr) << L" " << label
             << L"=" << BoolText(bool_value ? TRUE : FALSE) << L"\n";
  VariantClear(&value);
}

std::wstring RuntimeIdText(SAFEARRAY* runtime_id) {
  if (runtime_id == nullptr || SafeArrayGetDim(runtime_id) != 1) {
    return {};
  }

  LONG lower = 0;
  LONG upper = -1;
  if (FAILED(SafeArrayGetLBound(runtime_id, 1, &lower)) ||
      FAILED(SafeArrayGetUBound(runtime_id, 1, &upper))) {
    return {};
  }

  std::wostringstream stream;
  for (LONG index = lower; index <= upper; ++index) {
    int value = 0;
    if (SUCCEEDED(SafeArrayGetElement(runtime_id, &index, &value))) {
      if (index != lower) {
        stream << L",";
      }
      stream << value;
    }
  }
  return stream.str();
}

void PrintElementInfo(IUIAutomationElement* element, const Options& options) {
  VARIANT name_value{};
  VariantInit(&name_value);
  const HRESULT name_hr =
      element->GetCurrentPropertyValue(UIA_NamePropertyId, &name_value);
  std::wstring name;
  if (SUCCEEDED(name_hr) && name_value.vt == VT_BSTR) {
    name = BstrToString(name_value.bstrVal);
  }
  std::wcout << L"name_hr=" << FormatHresult(name_hr) << L" ";
  PrintValueField(L"name", name, options.redact_values);
  VariantClear(&name_value);

  PrintElementString(element, UIA_AutomationIdPropertyId, L"automation_id");
  PrintElementString(element, UIA_ClassNamePropertyId, L"class_name");
  PrintElementString(element, UIA_FrameworkIdPropertyId, L"framework_id");
  PrintElementBool(element, UIA_HasKeyboardFocusPropertyId,
                   L"has_keyboard_focus");
  PrintElementBool(element, UIA_IsKeyboardFocusablePropertyId,
                   L"is_keyboard_focusable");
  PrintElementBool(element, UIA_IsEnabledPropertyId, L"is_enabled");

  CONTROLTYPEID control_type = 0;
  const HRESULT control_hr = element->get_CurrentControlType(&control_type);
  std::wcout << L"control_type_hr=" << FormatHresult(control_hr)
             << L" control_type=" << ControlTypeName(control_type) << L"\n";

  RECT rect{};
  const HRESULT rect_hr = element->get_CurrentBoundingRectangle(&rect);
  std::wcout << L"bounding_rect_hr=" << FormatHresult(rect_hr)
             << L" bounding_rect=" << RectText(rect) << L"\n";

  UIA_HWND native_hwnd = nullptr;
  const HRESULT native_hwnd_hr = element->get_CurrentNativeWindowHandle(&native_hwnd);
  HWND native_window = reinterpret_cast<HWND>(native_hwnd);
  DWORD native_pid = 0;
  DWORD native_tid =
      native_window != nullptr
          ? GetWindowThreadProcessId(native_window, &native_pid)
          : 0;
  wchar_t native_class[256] = {};
  if (native_window != nullptr) {
    GetClassNameW(native_window, native_class,
                  static_cast<int>(std::size(native_class)));
  }
  std::wcout << L"native_hwnd_hr=" << FormatHresult(native_hwnd_hr)
             << L" native_hwnd=" << FormatHwnd(native_window)
             << L" native_tid=" << native_tid
             << L" native_pid=" << native_pid
             << L" native_class=" << native_class << L"\n";

  SAFEARRAY* runtime_id = nullptr;
  const HRESULT runtime_id_hr = element->GetRuntimeId(&runtime_id);
  std::wcout << L"runtime_id_hr=" << FormatHresult(runtime_id_hr)
             << L" runtime_id=\"" << RuntimeIdText(runtime_id) << L"\"\n";
  if (runtime_id != nullptr) {
    SafeArrayDestroy(runtime_id);
  }
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
             << L" readonly=" << BoolText(read_only) << L"\n";
  std::wcout << L"value_hr=" << FormatHresult(value_hr) << L" ";
  PrintValueField(L"value", current, options.redact_values);
  SysFreeString(current_value);

  if (options.set_value) {
    BSTR new_value = SysAllocStringLen(options.value.data(),
                                       static_cast<UINT>(options.value.size()));
    const HRESULT set_hr = value_pattern->SetValue(new_value);
    SysFreeString(new_value);
    std::wcout << L"value_set_hr=" << FormatHresult(set_hr) << L"\n";

    BSTR after_value = nullptr;
    const HRESULT after_hr = value_pattern->get_CurrentValue(&after_value);
    const std::wstring after = BstrToString(after_value);
    std::wcout << L"value_after_hr=" << FormatHresult(after_hr) << L" ";
    PrintValueField(L"value_after", after, options.redact_values);
    std::wcout << L"value_requested_match="
               << BoolText(after == options.value ? TRUE : FALSE) << L"\n";
    SysFreeString(after_value);
  }

  value_pattern->Release();
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

  IUIAutomationTextRange* range = nullptr;
  const HRESULT range_hr = text_pattern->get_DocumentRange(&range);
  std::wcout << L"text_document_range_hr=" << FormatHresult(range_hr) << L"\n";
  if (SUCCEEDED(range_hr) && range != nullptr) {
    BSTR text = nullptr;
    const HRESULT text_hr = range->GetText(512, &text);
    const std::wstring document_text = BstrToString(text);
    std::wcout << L"text_get_hr=" << FormatHresult(text_hr) << L" ";
    PrintValueField(L"text", document_text, options.redact_values);
    SysFreeString(text);
    range->Release();
  }
  text_pattern->Release();
}

void ProbeLegacyPattern(IUIAutomationElement* element, const Options& options) {
  IUIAutomationLegacyIAccessiblePattern* legacy_pattern = nullptr;
  const HRESULT pattern_hr = element->GetCurrentPatternAs(
      UIA_LegacyIAccessiblePatternId, IID_PPV_ARGS(&legacy_pattern));
  std::wcout << L"legacy_iaccessible_pattern_hr=" << FormatHresult(pattern_hr)
             << L" supported=" << BoolText(SUCCEEDED(pattern_hr)) << L"\n";
  if (FAILED(pattern_hr) || legacy_pattern == nullptr) {
    return;
  }

  BSTR value = nullptr;
  const HRESULT value_hr = legacy_pattern->get_CurrentValue(&value);
  const std::wstring legacy_value = BstrToString(value);
  std::wcout << L"legacy_value_hr=" << FormatHresult(value_hr) << L" ";
  PrintValueField(L"legacy_value", legacy_value, options.redact_values);
  SysFreeString(value);
  legacy_pattern->Release();
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

  PrintWindowSnapshot();

  const HRESULT init_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(init_hr)) {
    std::wcout << L"coinitialize_hr=" << FormatHresult(init_hr) << L"\n";
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
  if (SUCCEEDED(focus_hr) && focused != nullptr) {
    PrintElementInfo(focused, options);
    ProbeValuePattern(focused, options);
    ProbeTextPattern(focused, options);
    ProbeLegacyPattern(focused, options);
  }

  SafeRelease(focused);
  SafeRelease(automation);
  CoUninitialize();
  return 0;
}
