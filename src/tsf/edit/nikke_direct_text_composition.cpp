#include "tsf/edit/nikke_direct_text_composition.h"

#if defined(_WIN32)

#include <array>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <new>
#include <sstream>
#include <utility>

#if defined(_DEBUG)
#include "tsf/debug/debug_log.h"
#endif

namespace milkyway::tsf::edit {
namespace {

struct ContextTargetSnapshot {
  NikkeDirectTextTarget target;
  HWND view_hwnd = nullptr;
};

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
  const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
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

HRESULT InsertTextAtSelection(TfEditCookie edit_cookie, ITfContext* context,
                              const std::wstring& text) {
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
      edit_cookie, 0, text.c_str(), static_cast<LONG>(text.size()),
      &inserted_range);
  insert_at_selection->Release();
  if (FAILED(hr)) {
    return hr;
  }

  hr = MoveSelectionToRangeEnd(edit_cookie, context, inserted_range);
  inserted_range->Release();
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
        L"[MilkyWayIME][NikkeDirectText][ReplacePreviousPreedit][Skip] "
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
        L"[MilkyWayIME][NikkeDirectText][ReplacePreviousPreedit]"
        L"[GetSelectionFailed] hr=0x" +
        FormatHex(static_cast<std::uint32_t>(hr)));
#endif
    return hr;
  }
  if (fetched == 0 || selection.range == nullptr) {
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][NikkeDirectText][ReplacePreviousPreedit]"
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
        L"[MilkyWayIME][NikkeDirectText][ReplacePreviousPreedit]"
        L"[IsEmptyFailed] hr=0x" +
        FormatHex(static_cast<std::uint32_t>(hr)));
#endif
    return hr;
  }
  if (!is_empty) {
    selection.range->Release();
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][NikkeDirectText][ReplacePreviousPreedit]"
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
        L"[MilkyWayIME][NikkeDirectText][ReplacePreviousPreedit]"
        L"[CloneFailed] hr=0x" +
        FormatHex(static_cast<std::uint32_t>(hr)));
#endif
    return hr;
  }

  hr = replacement_range->Collapse(edit_cookie, TF_ANCHOR_START);
  LONG moved = 0;
  const LONG expected_move =
      -static_cast<LONG>(previous_preedit.size());
  if (SUCCEEDED(hr)) {
    hr = replacement_range->ShiftStart(edit_cookie, expected_move, &moved,
                                       nullptr);
  }
  if (FAILED(hr)) {
    replacement_range->Release();
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][NikkeDirectText][ReplacePreviousPreedit]"
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
        L"[MilkyWayIME][NikkeDirectText][ReplacePreviousPreedit]"
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
        L"[MilkyWayIME][NikkeDirectText][ReplacePreviousPreedit]"
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
        L"[MilkyWayIME][NikkeDirectText][ReplacePreviousPreedit]"
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
  if (SUCCEEDED(hr)) {
    hr = MoveSelectionToRangeEnd(edit_cookie, context, replacement_range);
  }

  replacement_range->Release();
  return hr;
}

#if defined(_DEBUG)
std::wstring PlanDebugText(const NikkeDirectTextOperationPlan& plan) {
  return L" commit=\"" + Utf8ToWide(plan.commit_text) + L"\" preedit=\"" +
         Utf8ToWide(plan.preedit_text) + L"\" has_preedit=" +
         std::to_wstring(plan.has_preedit ? 1 : 0) + L" end_requested=" +
         std::to_wstring(plan.end_requested ? 1 : 0) +
         L" has_composition_operation=" +
         std::to_wstring(plan.has_composition_operation ? 1 : 0);
}
#endif

}  // namespace

bool NikkeDirectTextComposition::IsActive() const {
  return active_;
}

bool NikkeDirectTextComposition::ShouldUse(
    ITfContext* context, const std::vector<TextEditOperation>& operations) const {
  const ContextTargetSnapshot snapshot = CaptureTargetSnapshot(context);
  const bool should_use =
      ShouldUseNikkeDirectTextComposition(snapshot.target, operations, active_);
#if defined(_DEBUG)
  if (should_use) {
    debug::DebugLog(
        L"[MilkyWayIME][NikkeDirectText][ShouldUse] process=" +
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

HRESULT NikkeDirectTextComposition::Apply(
    TfEditCookie edit_cookie, ITfContext* context,
    const std::vector<TextEditOperation>& operations) {
  if (context == nullptr) {
    return E_INVALIDARG;
  }

  const ContextTargetSnapshot snapshot = CaptureTargetSnapshot(context);
  if (!ShouldUseNikkeDirectTextComposition(snapshot.target, operations, active_)) {
    return S_FALSE;
  }

  const NikkeDirectTextOperationPlan plan =
      BuildNikkeDirectTextOperationPlan(operations);
  const std::wstring commit_text = Utf8ToWide(plan.commit_text);
  const std::wstring preedit_text = Utf8ToWide(plan.preedit_text);
  const bool should_write = !commit_text.empty() || plan.has_preedit;

#if defined(_DEBUG)
  debug::DebugLog(
      L"[MilkyWayIME][NikkeDirectText][Apply][Begin] context=" +
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

  std::wstring write_text = commit_text;
  if (plan.has_preedit) {
    write_text += preedit_text;
  }

  HRESULT hr = S_OK;
  bool wrote_text = false;
  const bool had_active_state = active_;
  const bool state_matches =
      active_ && context_identity_ == context && view_hwnd_ == snapshot.view_hwnd;

  if (state_matches) {
    hr = ReplacePreviousPreedit(edit_cookie, context, last_preedit_, write_text);
    if (SUCCEEDED(hr) && hr != S_FALSE) {
      wrote_text = true;
    } else if (hr == S_FALSE) {
#if defined(_DEBUG)
      debug::DebugLog(
          L"[MilkyWayIME][NikkeDirectText][Apply][PreeditMismatch] "
          L"context=" +
          PointerToString(context) + L" previous=\"" + last_preedit_ + L"\"");
#endif
      Reset(L"preedit_mismatch");
    } else {
      Reset(L"replace_failed");
      return hr;
    }
  } else if (had_active_state) {
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][NikkeDirectText][Apply][TargetChanged] context=" +
        PointerToString(context) + L" view_hwnd=" +
        PointerToString(snapshot.view_hwnd) + L" stored_context=" +
        PointerToString(context_identity_) + L" stored_view_hwnd=" +
        PointerToString(view_hwnd_));
#endif
    Reset(L"target_changed");
  }

  if (!wrote_text) {
    if (had_active_state) {
      write_text = plan.has_preedit ? preedit_text : std::wstring();
    }
    hr = InsertTextAtSelection(edit_cookie, context, write_text);
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
      L"[MilkyWayIME][NikkeDirectText][Apply][End] hr=0x" +
      FormatHex(static_cast<std::uint32_t>(hr)) + L" active=" +
      std::to_wstring(active_ ? 1 : 0) + L" last_preedit=\"" +
      last_preedit_ + L"\"");
#endif
  return hr;
}

void NikkeDirectTextComposition::Reset(const wchar_t* reason) {
#if defined(_DEBUG)
  if (active_ || !last_preedit_.empty()) {
    debug::DebugLog(
        L"[MilkyWayIME][NikkeDirectText][Reset] reason=" +
        std::wstring(reason != nullptr ? reason : L"<null>") +
        L" context=" + PointerToString(context_identity_) + L" view_hwnd=" +
        PointerToString(view_hwnd_) + L" last_preedit=\"" + last_preedit_ +
        L"\"");
  }
#endif
  active_ = false;
  context_identity_ = nullptr;
  view_hwnd_ = nullptr;
  last_preedit_.clear();
}

}  // namespace milkyway::tsf::edit

#endif
