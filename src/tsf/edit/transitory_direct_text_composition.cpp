#include "tsf/edit/transitory_direct_text_composition.h"

#if defined(_WIN32)

#include <array>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <new>
#include <sstream>
#include <string_view>
#include <utility>

#if defined(_DEBUG)
#include "tsf/debug/debug_log.h"
#endif

namespace milkyway::tsf::edit {
namespace {

struct ContextTargetSnapshot {
  TransitoryDirectTextTarget target;
  HWND view_hwnd = nullptr;
};

template <typename Interface>
void SafeRelease(Interface*& pointer) {
  if (pointer != nullptr) {
    pointer->Release();
    pointer = nullptr;
  }
}

std::wstring FormatHex(std::uint32_t value) {
  wchar_t buffer[16] = {};
  swprintf_s(buffer, L"%08X", value);
  return buffer;
}

std::wstring PointerToString(const void* pointer) {
  std::wostringstream stream;
  stream << std::uppercase << std::hex
         << reinterpret_cast<std::uintptr_t>(pointer);
  return stream.str();
}

std::wstring CurrentProcessName() {
  wchar_t path[MAX_PATH] = {};
  const DWORD length =
      GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
  if (length == 0) {
    return {};
  }
  return std::filesystem::path(std::wstring(path, path + length))
      .filename()
      .wstring();
}

std::wstring WindowClassName(HWND hwnd) {
  if (hwnd == nullptr) {
    return {};
  }

  wchar_t class_name[256] = {};
  const int length = GetClassNameW(hwnd, class_name,
                                   static_cast<int>(std::size(class_name)));
  if (length <= 0) {
    return {};
  }
  return std::wstring(class_name, class_name + length);
}

std::wstring Utf8ToWide(const std::string& text) {
  if (text.empty()) {
    return {};
  }

  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         text.data(),
                                         static_cast<int>(text.size()), nullptr,
                                         0);
  if (length <= 0) {
    return {};
  }

  std::wstring wide_text(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                      static_cast<int>(text.size()), wide_text.data(), length);
  return wide_text;
}

ContextTargetSnapshot CaptureTargetSnapshot(ITfContext* context) {
  ContextTargetSnapshot snapshot;
  snapshot.target.process_name = CurrentProcessName();
  if (context == nullptr) {
    return snapshot;
  }

  TF_STATUS status = {};
  if (SUCCEEDED(context->GetStatus(&status))) {
    snapshot.target.is_transitory =
        (status.dwStaticFlags & TS_SS_TRANSITORY) != 0;
  }

  ITfContextView* view = nullptr;
  if (SUCCEEDED(context->GetActiveView(&view)) && view != nullptr) {
    HWND hwnd = nullptr;
    if (SUCCEEDED(view->GetWnd(&hwnd))) {
      snapshot.view_hwnd = hwnd;
      snapshot.target.view_class = WindowClassName(hwnd);
    }
    view->Release();
  }

  return snapshot;
}

HRESULT MoveSelectionToRangeEnd(TfEditCookie edit_cookie, ITfContext* context,
                                ITfRange* range) {
  if (context == nullptr || range == nullptr) {
    return E_INVALIDARG;
  }

  ITfRange* selection_range = nullptr;
  HRESULT hr = range->Clone(&selection_range);
  if (FAILED(hr)) {
    return hr;
  }

  hr = selection_range->Collapse(edit_cookie, TF_ANCHOR_END);
  if (SUCCEEDED(hr)) {
    TF_SELECTION selection = {};
    selection.range = selection_range;
    selection.style.ase = TF_AE_NONE;
    selection.style.fInterimChar = FALSE;
    hr = context->SetSelection(edit_cookie, 1, &selection);
  }

  selection_range->Release();
  return hr;
}

#if defined(_DEBUG)
std::wstring DebugTextSnippet(std::wstring_view text);
#endif

bool IsWin32EditTarget(const TransitoryDirectTextTarget& target) {
  return target.view_class == L"Edit";
}

HRESULT SendBackspaceToWindow(HWND hwnd, std::size_t count) {
  if (hwnd == nullptr || count == 0) {
    return count == 0 ? S_OK : E_INVALIDARG;
  }

#if defined(_DEBUG)
  debug::DebugLog(
      L"[MilkyWayIME][TransitoryDirectText][HostDeleteBegin] hwnd=" +
      PointerToString(hwnd) + L" count=" + std::to_wstring(count));
#endif

  const UINT scan_code = MapVirtualKeyW(VK_BACK, MAPVK_VK_TO_VSC);
  const LPARAM key_down_lparam =
      1 | (static_cast<LPARAM>(scan_code) << 16);
  const LPARAM key_up_lparam = key_down_lparam | (1LL << 30) | (1LL << 31);

  constexpr UINT kSendTimeoutMs = 100;
  for (std::size_t index = 0; index < count; ++index) {
    DWORD_PTR result = 0;
    SetLastError(ERROR_SUCCESS);
    const LRESULT key_down_sent = SendMessageTimeoutW(
        hwnd, WM_KEYDOWN, VK_BACK, key_down_lparam,
        SMTO_ABORTIFHUNG | SMTO_BLOCK, kSendTimeoutMs, &result);
    if (key_down_sent == 0) {
      const DWORD error = GetLastError();
#if defined(_DEBUG)
      debug::DebugLog(
          L"[MilkyWayIME][TransitoryDirectText][HostBackspace]"
          L"[KeyDownFailed] hwnd=" +
          PointerToString(hwnd) + L" index=" + std::to_wstring(index) +
          L" error=0x" + FormatHex(error));
#endif
      return HRESULT_FROM_WIN32(error != ERROR_SUCCESS ? error
                                                       : ERROR_TIMEOUT);
    }

    result = 0;
    SetLastError(ERROR_SUCCESS);
    const LRESULT key_up_sent = SendMessageTimeoutW(
        hwnd, WM_KEYUP, VK_BACK, key_up_lparam, SMTO_ABORTIFHUNG | SMTO_BLOCK,
        kSendTimeoutMs, &result);
    if (key_up_sent == 0) {
      const DWORD error = GetLastError();
#if defined(_DEBUG)
      debug::DebugLog(
          L"[MilkyWayIME][TransitoryDirectText][HostBackspace]"
          L"[KeyUpFailed] hwnd=" +
          PointerToString(hwnd) + L" index=" + std::to_wstring(index) +
          L" error=0x" + FormatHex(error));
#endif
      return HRESULT_FROM_WIN32(error != ERROR_SUCCESS ? error
                                                       : ERROR_TIMEOUT);
    }

#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][SyntheticBackspace] hwnd=" +
        PointerToString(hwnd) + L" index=" + std::to_wstring(index));
#endif
  }

#if defined(_DEBUG)
  debug::DebugLog(
      L"[MilkyWayIME][TransitoryDirectText][HostDeleteEnd] hwnd=" +
      PointerToString(hwnd) + L" count=" + std::to_wstring(count));
#endif
  return S_OK;
}

bool ReadWindowText(HWND hwnd, std::wstring* text) {
  if (hwnd == nullptr || text == nullptr) {
    return false;
  }

  constexpr UINT kTimeoutMs = 100;
  DWORD_PTR length_result = 0;
  SetLastError(ERROR_SUCCESS);
  if (SendMessageTimeoutW(hwnd, WM_GETTEXTLENGTH, 0, 0,
                          SMTO_ABORTIFHUNG | SMTO_BLOCK, kTimeoutMs,
                          &length_result) == 0) {
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][Win32EditReplace]"
        L"[GetTextLengthFailed] hwnd=" +
        PointerToString(hwnd) + L" error=0x" + FormatHex(GetLastError()));
#endif
    return false;
  }

  const std::size_t length = static_cast<std::size_t>(length_result);
  std::wstring buffer(length + 1, L'\0');
  DWORD_PTR copied_result = 0;
  SetLastError(ERROR_SUCCESS);
  if (SendMessageTimeoutW(hwnd, WM_GETTEXT, static_cast<WPARAM>(buffer.size()),
                          reinterpret_cast<LPARAM>(buffer.data()),
                          SMTO_ABORTIFHUNG | SMTO_BLOCK, kTimeoutMs,
                          &copied_result) == 0) {
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][Win32EditReplace]"
        L"[GetTextFailed] hwnd=" +
        PointerToString(hwnd) + L" error=0x" + FormatHex(GetLastError()));
#endif
    return false;
  }

  buffer.resize(static_cast<std::size_t>(copied_result));
  *text = std::move(buffer);
  return true;
}

bool QueryEditSelection(HWND hwnd, DWORD* start, DWORD* end) {
  if (hwnd == nullptr || start == nullptr || end == nullptr) {
    return false;
  }

  constexpr UINT kTimeoutMs = 100;
  DWORD_PTR result = 0;
  SetLastError(ERROR_SUCCESS);
  if (SendMessageTimeoutW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(start),
                          reinterpret_cast<LPARAM>(end),
                          SMTO_ABORTIFHUNG | SMTO_BLOCK, kTimeoutMs,
                          &result) == 0) {
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][Win32EditReplace]"
        L"[GetSelFailed] hwnd=" +
        PointerToString(hwnd) + L" error=0x" + FormatHex(GetLastError()));
#endif
    return false;
  }
  return true;
}

bool SendEditMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                     DWORD_PTR* result_out = nullptr) {
  constexpr UINT kTimeoutMs = 100;
  DWORD_PTR result = 0;
  SetLastError(ERROR_SUCCESS);
  if (SendMessageTimeoutW(hwnd, message, wparam, lparam,
                          SMTO_ABORTIFHUNG | SMTO_BLOCK, kTimeoutMs,
                          &result) == 0) {
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][Win32EditReplace]"
        L"[SendMessageFailed] hwnd=" +
        PointerToString(hwnd) + L" message=0x" + FormatHex(message) +
        L" error=0x" + FormatHex(GetLastError()));
#endif
    return false;
  }
  if (result_out != nullptr) {
    *result_out = result;
  }
  return true;
}

HRESULT ReplacePreviousPreeditByWin32Edit(
    HWND hwnd, const std::wstring& previous_preedit,
    const std::wstring& replacement_text) {
  if (hwnd == nullptr || previous_preedit.empty()) {
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][Win32EditReplace][Skip] hwnd=" +
        PointerToString(hwnd) + L" previous_len=" +
        std::to_wstring(previous_preedit.size()));
#endif
    return S_FALSE;
  }

  std::wstring before_text;
  DWORD before_start = 0;
  DWORD before_end = 0;
  if (!ReadWindowText(hwnd, &before_text) ||
      !QueryEditSelection(hwnd, &before_start, &before_end)) {
    return S_FALSE;
  }

  const std::size_t previous_len = previous_preedit.size();
  if (before_start != before_end || before_start > before_text.size() ||
      before_start < previous_len ||
      before_text.compare(static_cast<std::size_t>(before_start) - previous_len,
                          previous_len, previous_preedit) != 0) {
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][Win32EditReplace]"
        L"[BeforeMismatch] hwnd=" +
        PointerToString(hwnd) + L" sel_start=" +
        std::to_wstring(before_start) + L" sel_end=" +
        std::to_wstring(before_end) + L" text_len=" +
        std::to_wstring(before_text.size()) + L" previous=\"" +
        DebugTextSnippet(previous_preedit) + L"\" before=\"" +
        DebugTextSnippet(before_text) + L"\"");
#endif
    return S_FALSE;
  }

  const DWORD replace_start =
      before_start - static_cast<DWORD>(previous_len);
  if (!SendEditMessage(hwnd, EM_SETSEL, replace_start, before_end)) {
    return S_FALSE;
  }
  if (!SendEditMessage(hwnd, EM_REPLACESEL, TRUE,
                       reinterpret_cast<LPARAM>(replacement_text.c_str()))) {
    return S_FALSE;
  }

  std::wstring after_text;
  DWORD after_start = 0;
  DWORD after_end = 0;
  if (!ReadWindowText(hwnd, &after_text) ||
      !QueryEditSelection(hwnd, &after_start, &after_end)) {
    return S_FALSE;
  }

  std::wstring expected_text = before_text;
  expected_text.replace(static_cast<std::size_t>(replace_start), previous_len,
                        replacement_text);
  const DWORD expected_caret =
      replace_start + static_cast<DWORD>(replacement_text.size());
  if (after_text != expected_text || after_start != expected_caret ||
      after_end != expected_caret) {
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][Win32EditReplace]"
        L"[AfterMismatch] hwnd=" +
        PointerToString(hwnd) + L" expected_caret=" +
        std::to_wstring(expected_caret) + L" after_start=" +
        std::to_wstring(after_start) + L" after_end=" +
        std::to_wstring(after_end) + L" expected=\"" +
        DebugTextSnippet(expected_text) + L"\" after=\"" +
        DebugTextSnippet(after_text) + L"\"");
#endif
    return S_FALSE;
  }

#if defined(_DEBUG)
  debug::DebugLog(
      L"[MilkyWayIME][TransitoryDirectText][Win32EditReplace][End] hwnd=" +
      PointerToString(hwnd) + L" replace_start=" +
      std::to_wstring(replace_start) + L" previous=\"" +
      DebugTextSnippet(previous_preedit) + L"\" replacement=\"" +
      DebugTextSnippet(replacement_text) + L"\" text=\"" +
      DebugTextSnippet(after_text) + L"\"");
#endif
  return S_OK;
}

HRESULT InsertTextAtSelection(TfEditCookie edit_cookie, ITfContext* context,
                              const std::wstring& text,
                              ITfRange** inserted_range_out = nullptr) {
  if (inserted_range_out != nullptr) {
    *inserted_range_out = nullptr;
  }
  if (context == nullptr || text.empty()) {
    return S_OK;
  }

  ITfInsertAtSelection* insert_at_selection = nullptr;
  HRESULT hr = context->QueryInterface(
      IID_ITfInsertAtSelection, reinterpret_cast<void**>(&insert_at_selection));
  if (FAILED(hr)) {
    return hr;
  }

  ITfRange* inserted_range = nullptr;
  hr = insert_at_selection->InsertTextAtSelection(
      edit_cookie, TF_IAS_QUERYONLY, text.c_str(),
      static_cast<LONG>(text.size()), &inserted_range);
  insert_at_selection->Release();
  if (FAILED(hr)) {
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][InsertTextAtSelection]"
        L"[QueryOnlyFailed] hr=0x" +
        FormatHex(static_cast<std::uint32_t>(hr)));
#endif
    return hr;
  }

  hr = inserted_range->SetText(edit_cookie, 0, text.c_str(),
                               static_cast<LONG>(text.size()));
  if (FAILED(hr)) {
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][InsertTextAtSelection]"
        L"[SetTextFailed] hr=0x" +
        FormatHex(static_cast<std::uint32_t>(hr)) + L" range=" +
        PointerToString(inserted_range) + L" text=\"" + DebugTextSnippet(text) +
        L"\"");
#endif
    inserted_range->Release();
    return hr;
  }

#if defined(_DEBUG)
  {
    std::wstring actual_text(text.size() + 1, L'\0');
    ULONG actual_length = 0;
    const HRESULT get_text_hr = inserted_range->GetText(
        edit_cookie, 0, actual_text.data(),
        static_cast<ULONG>(actual_text.size()), &actual_length);
    actual_text.resize(SUCCEEDED(get_text_hr) ? actual_length : 0);
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][InsertTextAtSelection][End] "
        L"range=" +
        PointerToString(inserted_range) + L" text_len=" +
        std::to_wstring(text.size()) + L" get_text_hr=0x" +
        FormatHex(static_cast<std::uint32_t>(get_text_hr)) +
        L" actual_len=" + std::to_wstring(actual_text.size()) + L" actual=\"" +
        DebugTextSnippet(actual_text) + L"\"");
  }
#endif

  hr = MoveSelectionToRangeEnd(edit_cookie, context, inserted_range);
  if (SUCCEEDED(hr) && inserted_range_out != nullptr) {
    *inserted_range_out = inserted_range;
    inserted_range = nullptr;
  }
  SafeRelease(inserted_range);
  return hr;
}

HRESULT ReplacePreviousPreeditByWin32EditFallback(
    TfEditCookie edit_cookie, ITfContext* context, HWND hwnd,
    const std::wstring& previous_preedit,
    const std::wstring& replacement_text) {
  (void)edit_cookie;
  if (context == nullptr || hwnd == nullptr || previous_preedit.empty()) {
#if defined(_DEBUG)
    debug::DebugLog(
      L"[MilkyWayIME][TransitoryDirectText][Win32EditFallback][Skip] context=" +
        PointerToString(context) + L" hwnd=" + PointerToString(hwnd) +
        L" previous_len=" + std::to_wstring(previous_preedit.size()));
#endif
    return S_FALSE;
  }

  HRESULT hr =
      ReplacePreviousPreeditByWin32Edit(hwnd, previous_preedit, replacement_text);
  if (hr == S_FALSE) {
    return S_FALSE;
  }
#if defined(_DEBUG)
  debug::DebugLog(
      L"[MilkyWayIME][TransitoryDirectText][Win32EditFallback][End] hr=0x" +
      FormatHex(static_cast<std::uint32_t>(hr)) + L" hwnd=" +
      PointerToString(hwnd) + L" previous=\"" +
      DebugTextSnippet(previous_preedit) + L"\" replacement=\"" +
      DebugTextSnippet(replacement_text) + L"\"");
#endif
  return hr;
}

HRESULT ReplacePreviousPreeditByHostBackspace(
    TfEditCookie edit_cookie, ITfContext* context, HWND hwnd,
    const std::wstring& previous_preedit,
    const std::wstring& replacement_text) {
  if (context == nullptr || hwnd == nullptr || previous_preedit.empty()) {
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][HostReplace][Skip] context=" +
        PointerToString(context) + L" hwnd=" + PointerToString(hwnd) +
        L" previous_len=" + std::to_wstring(previous_preedit.size()));
#endif
    return S_FALSE;
  }

  HRESULT hr = SendBackspaceToWindow(hwnd, previous_preedit.size());
  if (FAILED(hr)) {
    return hr;
  }

  hr = InsertTextAtSelection(edit_cookie, context, replacement_text);
#if defined(_DEBUG)
  debug::DebugLog(
      L"[MilkyWayIME][TransitoryDirectText][InsertAfterHostDelete][End] hr=0x" +
      FormatHex(static_cast<std::uint32_t>(hr)) + L" hwnd=" +
      PointerToString(hwnd) + L" previous=\"" +
      DebugTextSnippet(previous_preedit) + L"\" replacement=\"" +
      DebugTextSnippet(replacement_text) + L"\"");
#endif
  return hr;
}

#if defined(_DEBUG)
std::wstring DebugTextSnippet(std::wstring_view text) {
  constexpr std::size_t kMaxDebugTextLength = 32;
  std::wstring snippet(text.substr(0, kMaxDebugTextLength));
  if (text.size() > kMaxDebugTextLength) {
    snippet += L"...";
  }
  return snippet;
}
#endif

HRESULT ReplacePreviousPreedit(TfEditCookie edit_cookie, ITfContext* context,
                               const std::wstring& previous_preedit,
                               const std::wstring& replacement_text) {
  if (context == nullptr || previous_preedit.empty()) {
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][ReplacePreviousPreedit][Skip] "
        L"context=" +
        PointerToString(context) + L" previous_len=" +
        std::to_wstring(previous_preedit.size()));
#endif
    return S_FALSE;
  }

  TF_SELECTION selection = {};
  ULONG fetched = 0;
  HRESULT hr = context->GetSelection(edit_cookie, 0, 1, &selection, &fetched);
  if (FAILED(hr)) {
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][ReplacePreviousPreedit]"
        L"[GetSelectionFailed] hr=0x" +
        FormatHex(static_cast<std::uint32_t>(hr)));
#endif
    return hr;
  }
  if (fetched == 0 || selection.range == nullptr) {
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][ReplacePreviousPreedit]"
        L"[NoSelection] fetched=" +
        std::to_wstring(fetched) + L" range=" +
        PointerToString(selection.range));
#endif
    return S_FALSE;
  }

  BOOL is_empty = TRUE;
  hr = selection.range->IsEmpty(edit_cookie, &is_empty);
  if (FAILED(hr)) {
    selection.range->Release();
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][ReplacePreviousPreedit]"
        L"[IsEmptyFailed] hr=0x" +
        FormatHex(static_cast<std::uint32_t>(hr)));
#endif
    return hr;
  }
  if (!is_empty) {
    selection.range->Release();
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][ReplacePreviousPreedit]"
        L"[SelectionNotEmpty]");
#endif
    return S_FALSE;
  }

  ITfRange* replacement_range = nullptr;
  hr = selection.range->Clone(&replacement_range);
  selection.range->Release();
  if (FAILED(hr)) {
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][ReplacePreviousPreedit]"
        L"[CloneFailed] hr=0x" +
        FormatHex(static_cast<std::uint32_t>(hr)));
#endif
    return hr;
  }

  hr = replacement_range->Collapse(edit_cookie, TF_ANCHOR_START);
  LONG moved = 0;
  const LONG expected_move = -static_cast<LONG>(previous_preedit.size());
  if (SUCCEEDED(hr)) {
    hr = replacement_range->ShiftStart(edit_cookie, expected_move, &moved,
                                       nullptr);
  }
  if (FAILED(hr)) {
    replacement_range->Release();
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][ReplacePreviousPreedit]"
        L"[ShiftStartFailed] hr=0x" +
        FormatHex(static_cast<std::uint32_t>(hr)) + L" expected_move=" +
        std::to_wstring(expected_move) + L" moved=" + std::to_wstring(moved));
#endif
    return S_FALSE;
  }
  if (moved != expected_move) {
    replacement_range->Release();
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][ReplacePreviousPreedit]"
        L"[ShiftStartShort] expected_move=" +
        std::to_wstring(expected_move) + L" moved=" + std::to_wstring(moved));
#endif
    return S_FALSE;
  }

  std::wstring actual_text(previous_preedit.size() + 1, L'\0');
  ULONG actual_length = 0;
  hr = replacement_range->GetText(
      edit_cookie, 0, actual_text.data(), static_cast<ULONG>(actual_text.size()),
      &actual_length);
  if (FAILED(hr)) {
    replacement_range->Release();
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][ReplacePreviousPreedit]"
        L"[GetTextFailed] hr=0x" +
        FormatHex(static_cast<std::uint32_t>(hr)) + L" requested_len=" +
        std::to_wstring(previous_preedit.size() + 1));
#endif
    return hr;
  }
  actual_text.resize(actual_length);
  if (actual_text != previous_preedit) {
    replacement_range->Release();
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][ReplacePreviousPreedit]"
        L"[TextMismatch] expected_len=" +
        std::to_wstring(previous_preedit.size()) + L" expected=\"" +
        DebugTextSnippet(previous_preedit) + L"\" actual_len=" +
        std::to_wstring(actual_text.size()) + L" actual=\"" +
        DebugTextSnippet(actual_text) + L"\"");
#endif
    return S_FALSE;
  }

  hr = replacement_range->SetText(edit_cookie, 0, replacement_text.c_str(),
                                  static_cast<LONG>(replacement_text.size()));
  if (FAILED(hr)) {
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][ReplacePreviousPreedit]"
        L"[SetTextFailed] hr=0x" +
        FormatHex(static_cast<std::uint32_t>(hr)));
#endif
    replacement_range->Release();
    return hr;
  }

  hr = MoveSelectionToRangeEnd(edit_cookie, context, replacement_range);
  replacement_range->Release();
  return hr;
}

#if defined(_DEBUG)
std::wstring PlanDebugText(const TransitoryDirectTextOperationPlan& plan) {
  return L" commit=\"" + Utf8ToWide(plan.commit_text) + L"\" preedit=\"" +
         Utf8ToWide(plan.preedit_text) + L"\" has_preedit=" +
         std::to_wstring(plan.has_preedit ? 1 : 0) + L" end_requested=" +
         std::to_wstring(plan.end_requested ? 1 : 0) +
         L" has_composition_operation=" +
         std::to_wstring(plan.has_composition_operation ? 1 : 0);
}
#endif

}  // namespace

bool TransitoryDirectTextComposition::IsActive() const {
  return active_;
}

bool TransitoryDirectTextComposition::ShouldUse(
    ITfContext* context, const std::vector<TextEditOperation>& operations) const {
  const ContextTargetSnapshot snapshot = CaptureTargetSnapshot(context);
  const bool should_use = ShouldUseTransitoryDirectTextComposition(
      snapshot.target, operations, active_);
#if defined(_DEBUG)
  if (should_use) {
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][ShouldUse] process=" +
        snapshot.target.process_name + L" view_class=" +
        snapshot.target.view_class + L" transitory=" +
        std::to_wstring(snapshot.target.is_transitory ? 1 : 0) +
        L" active=" + std::to_wstring(active_ ? 1 : 0) +
        L" context=" + PointerToString(context) + L" view_hwnd=" +
        PointerToString(snapshot.view_hwnd));
  }
#endif
  return should_use;
}

ITfContext* TransitoryDirectTextComposition::ResolveFullContextFromTransitory(
    ITfContext* context,
    const std::vector<TextEditOperation>& operations) const {
  (void)context;
  (void)operations;
  return nullptr;
}

HRESULT TransitoryDirectTextComposition::Apply(
    TfEditCookie edit_cookie, ITfContext* context,
    const std::vector<TextEditOperation>& operations) {
  if (context == nullptr) {
    return E_INVALIDARG;
  }

  const ContextTargetSnapshot snapshot = CaptureTargetSnapshot(context);
  if (!ShouldUseTransitoryDirectTextComposition(snapshot.target, operations,
                                               active_)) {
    return S_FALSE;
  }

  const TransitoryDirectTextOperationPlan plan =
      BuildTransitoryDirectTextOperationPlan(operations);
  const std::wstring commit_text = Utf8ToWide(plan.commit_text);
  const std::wstring preedit_text = Utf8ToWide(plan.preedit_text);
  const bool should_write = !commit_text.empty() || plan.has_preedit;

#if defined(_DEBUG)
  debug::DebugLog(
      L"[MilkyWayIME][TransitoryDirectText][Apply][Begin] context=" +
      PointerToString(context) + L" view_hwnd=" +
      PointerToString(snapshot.view_hwnd) + L" stored_context=" +
      PointerToString(context_identity_) + L" stored_view_hwnd=" +
      PointerToString(view_hwnd_) + L" active=" +
      std::to_wstring(active_ ? 1 : 0) + L" last_preedit=\"" +
      last_preedit_ + L"\"" + PlanDebugText(plan));
#endif

  if (!should_write) {
    Reset(L"end_without_write");
    return S_OK;
  }

  HRESULT hr = S_OK;
  bool wrote_text = false;
  const bool had_active_state = active_;
  const bool state_matches =
      active_ && context_identity_ == context && view_hwnd_ == snapshot.view_hwnd;
  std::wstring replacement_text = commit_text;
  if (plan.has_preedit) {
    replacement_text += preedit_text;
  }

  if (state_matches) {
    hr = ReplacePreviousPreedit(edit_cookie, context, last_preedit_,
                                replacement_text);
    if (SUCCEEDED(hr) && hr != S_FALSE) {
      wrote_text = true;
    }

    if (hr == S_FALSE && IsWin32EditTarget(snapshot.target)) {
      hr = ReplacePreviousPreeditByWin32EditFallback(
          edit_cookie, context, snapshot.view_hwnd, last_preedit_,
          replacement_text);
      if (SUCCEEDED(hr) && hr != S_FALSE) {
        wrote_text = true;
      }
    }

    if (hr == S_FALSE && !IsWin32EditTarget(snapshot.target)) {
      hr = ReplacePreviousPreeditByHostBackspace(
          edit_cookie, context, snapshot.view_hwnd, last_preedit_,
          replacement_text);
      if (SUCCEEDED(hr) && hr != S_FALSE) {
        wrote_text = true;
      }
    }

    if (hr == S_FALSE) {
#if defined(_DEBUG)
      debug::DebugLog(
          L"[MilkyWayIME][TransitoryDirectText][Apply][PreeditMismatch] "
          L"context=" +
          PointerToString(context) + L" previous=\"" + last_preedit_ +
          L"\"");
#endif
      Reset(L"preedit_mismatch");
      return S_OK;
    }
    if (FAILED(hr)) {
      Reset(L"replace_failed");
      return hr;
    }
  } else if (had_active_state) {
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][Apply][TargetMismatch] context=" +
        PointerToString(context) + L" view_hwnd=" +
        PointerToString(snapshot.view_hwnd) + L" stored_context=" +
        PointerToString(context_identity_) + L" stored_view_hwnd=" +
        PointerToString(view_hwnd_));
#endif
    Reset(L"target_mismatch");
    return S_FALSE;
  }

  if (!wrote_text) {
    hr = InsertTextAtSelection(edit_cookie, context, replacement_text);
    if (FAILED(hr)) {
      Reset(L"insert_failed");
      return hr;
    }
  }

  if (plan.has_preedit && !preedit_text.empty()) {
    active_ = true;
    context_identity_ = context;
    view_hwnd_ = snapshot.view_hwnd;
    last_preedit_ = preedit_text;
  } else {
    Reset(L"no_preedit");
  }

#if defined(_DEBUG)
  debug::DebugLog(
      L"[MilkyWayIME][TransitoryDirectText][Apply][End] hr=0x" +
      FormatHex(static_cast<std::uint32_t>(hr)) + L" active=" +
      std::to_wstring(active_ ? 1 : 0) + L" last_preedit=\"" + last_preedit_ +
      L"\"");
#endif
  return hr;
}

void TransitoryDirectTextComposition::Reset(const wchar_t* reason) {
#if defined(_DEBUG)
  if (active_ || !last_preedit_.empty()) {
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryDirectText][Reset] reason=" +
        std::wstring(reason != nullptr ? reason : L"<null>") +
        L" context=" + PointerToString(context_identity_) + L" view_hwnd=" +
        PointerToString(view_hwnd_) + L" last_preedit=\"" +
        last_preedit_ + L"\"");
  }
#endif
  active_ = false;
  context_identity_ = nullptr;
  view_hwnd_ = nullptr;
  last_preedit_.clear();
}

}  // namespace milkyway::tsf::edit

#endif
