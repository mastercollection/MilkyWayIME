#include "tsf/service/tip_text_service.h"

#if defined(_WIN32)

#include <imm.h>

#include <cstdint>
#include <array>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "engine/layout/base_layout_json_loader.h"
#include "tsf/langbar/input_mode_lang_bar_item.h"
#if defined(_DEBUG)
#include "tsf/debug/debug_log.h"
#endif
#include "tsf/registration/text_service_registration.h"
#include "tsf/service/module_state.h"
#include "tsf/settings/user_layout_paths.h"
#include "tsf/settings/user_settings.h"

namespace milkyway::tsf::service {
namespace {

template <typename T>
void SafeRelease(T*& pointer) {
  if (pointer != nullptr) {
    pointer->Release();
    pointer = nullptr;
  }
}

bool KeyPressed(int virtual_key) {
  return (GetKeyState(virtual_key) & 0x8000) != 0;
}

bool IsUsableScreenRect(const RECT& rect) {
  if (rect.left == 0 && rect.top == 0 && rect.right == 0 &&
      rect.bottom == 0) {
    return false;
  }

  return rect.right >= rect.left && rect.bottom > rect.top;
}

std::optional<POINT> AnchorFromGuiThreadCaret() {
  GUITHREADINFO gui_thread_info = {};
  gui_thread_info.cbSize = sizeof(gui_thread_info);
  if (!GetGUIThreadInfo(0, &gui_thread_info)) {
    return std::nullopt;
  }

  if (IsUsableScreenRect(gui_thread_info.rcCaret)) {
    return POINT{gui_thread_info.rcCaret.left, gui_thread_info.rcCaret.bottom};
  }

  if (gui_thread_info.hwndCaret != nullptr) {
    POINT caret = {};
    if (GetCaretPos(&caret) && ClientToScreen(gui_thread_info.hwndCaret,
                                               &caret)) {
      return caret;
    }
  }

  return std::nullopt;
}

bool IsImeModeToggleVirtualKey(WPARAM wparam) {
  return static_cast<std::uint16_t>(wparam) == VK_HANGUL;
}

bool IsHanjaVirtualKey(WPARAM wparam) {
  const auto key = static_cast<std::uint16_t>(wparam);
  return key == VK_HANJA || key == VK_KANJI;
}

constexpr GUID kImeModePreservedKeyGuid = {
    0x7774b3e6,
    0xf464,
    0x4674,
    {0xa1, 0xcf, 0xb0, 0xed, 0xdb, 0x0e, 0x2b, 0x2a},
};

constexpr wchar_t kImeModePreservedKeyDescription[] =
    L"MilkyWayIME Hangul toggle";

class EditSessionBase : public ITfEditSession {
 public:
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (ppv == nullptr) {
      return E_INVALIDARG;
    }

    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_ITfEditSession) {
      *ppv = static_cast<ITfEditSession*>(this);
      AddRef();
      return S_OK;
    }

    return E_NOINTERFACE;
  }

  STDMETHODIMP_(ULONG) AddRef() override {
    return ++ref_count_;
  }

  STDMETHODIMP_(ULONG) Release() override {
    const ULONG value = --ref_count_;
    if (value == 0) {
      delete this;
    }
    return value;
  }

 protected:
  virtual ~EditSessionBase() = default;

 private:
  std::atomic<ULONG> ref_count_{1};
};

class TextExtentEditSession final : public EditSessionBase {
 public:
  TextExtentEditSession(ITfContextView* view, ITfRange* range,
                        std::optional<RECT>* text_rect)
      : view_(view), range_(range), text_rect_(text_rect) {
    view_->AddRef();
    range_->AddRef();
  }

  ~TextExtentEditSession() override {
    range_->Release();
    view_->Release();
  }

  STDMETHODIMP DoEditSession(TfEditCookie edit_cookie) override {
    RECT rect = {};
    BOOL clipped = FALSE;
    const HRESULT hr = view_->GetTextExt(edit_cookie, range_, &rect, &clipped);
    if (SUCCEEDED(hr) && IsUsableScreenRect(rect)) {
      *text_rect_ = rect;
    }
    return S_OK;
  }

 private:
  ITfContextView* view_ = nullptr;
  ITfRange* range_ = nullptr;
  std::optional<RECT>* text_rect_ = nullptr;
};

HRESULT SetContextSelectionToRangeEnd(TfEditCookie edit_cookie,
                                      ITfContext* context, ITfRange* range) {
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

struct SelectionTextSnapshot {
  SelectionTextSnapshot() = default;
  SelectionTextSnapshot(const SelectionTextSnapshot&) = delete;
  SelectionTextSnapshot& operator=(const SelectionTextSnapshot&) = delete;

  ~SelectionTextSnapshot() {
    SafeRelease(range);
  }

  std::wstring text;
  ITfRange* range = nullptr;
  bool is_empty = true;
  bool has_selection = false;
};

class SelectionTextReadEditSession final : public EditSessionBase {
 public:
  SelectionTextReadEditSession(ITfContext* context,
                               SelectionTextSnapshot* snapshot)
      : context_(context), snapshot_(snapshot) {
    context_->AddRef();
  }

  ~SelectionTextReadEditSession() override {
    context_->Release();
  }

  STDMETHODIMP DoEditSession(TfEditCookie edit_cookie) override {
    if (snapshot_ == nullptr) {
      return E_INVALIDARG;
    }

    snapshot_->text.clear();
    SafeRelease(snapshot_->range);

    TF_SELECTION selection = {};
    ULONG fetched = 0;
    HRESULT hr = context_->GetSelection(edit_cookie, 0, 1, &selection, &fetched);
    if (FAILED(hr) || fetched == 0 || selection.range == nullptr) {
      return S_OK;
    }
    snapshot_->has_selection = true;

    BOOL empty = TRUE;
    hr = selection.range->IsEmpty(edit_cookie, &empty);
    snapshot_->is_empty = empty != FALSE;
    if (FAILED(hr) || empty) {
      selection.range->Release();
      return S_OK;
    }

    std::array<WCHAR, 256> buffer = {};
    ULONG text_length = 0;
    hr = selection.range->GetText(
        edit_cookie, 0, buffer.data(), static_cast<ULONG>(buffer.size()),
        &text_length);
    if (SUCCEEDED(hr) && text_length > 0) {
      snapshot_->text.assign(buffer.data(), buffer.data() + text_length);
      hr = selection.range->Clone(&snapshot_->range);
    }

    selection.range->Release();
    return SUCCEEDED(hr) ? S_OK : hr;
  }

 private:
  ITfContext* context_ = nullptr;
  SelectionTextSnapshot* snapshot_ = nullptr;
};

struct CaretTextSnapshot {
  CaretTextSnapshot() = default;
  CaretTextSnapshot(const CaretTextSnapshot&) = delete;
  CaretTextSnapshot& operator=(const CaretTextSnapshot&) = delete;

  ~CaretTextSnapshot() {
    SafeRelease(caret_range);
  }

  std::wstring text_before_caret;
  ITfRange* caret_range = nullptr;
};

class CaretTextReadEditSession final : public EditSessionBase {
 public:
  CaretTextReadEditSession(ITfContext* context, CaretTextSnapshot* snapshot)
      : context_(context), snapshot_(snapshot) {
    context_->AddRef();
  }

  ~CaretTextReadEditSession() override {
    context_->Release();
  }

  STDMETHODIMP DoEditSession(TfEditCookie edit_cookie) override {
    if (snapshot_ == nullptr) {
      return E_INVALIDARG;
    }

    snapshot_->text_before_caret.clear();
    SafeRelease(snapshot_->caret_range);

    TF_SELECTION selection = {};
    ULONG fetched = 0;
    HRESULT hr = context_->GetSelection(edit_cookie, 0, 1, &selection, &fetched);
    if (FAILED(hr) || fetched == 0 || selection.range == nullptr) {
      return S_OK;
    }

    BOOL empty = TRUE;
    hr = selection.range->IsEmpty(edit_cookie, &empty);
    if (FAILED(hr) || !empty) {
      selection.range->Release();
      return S_OK;
    }

    hr = selection.range->Clone(&snapshot_->caret_range);
    if (SUCCEEDED(hr)) {
      hr = snapshot_->caret_range->Collapse(edit_cookie, TF_ANCHOR_START);
    }

    ITfRange* text_range = nullptr;
    if (SUCCEEDED(hr)) {
      hr = selection.range->Clone(&text_range);
    }
    selection.range->Release();
    if (FAILED(hr) || text_range == nullptr) {
      SafeRelease(text_range);
      return SUCCEEDED(hr) ? S_OK : hr;
    }

    hr = text_range->Collapse(edit_cookie, TF_ANCHOR_START);
    LONG moved = 0;
    constexpr LONG kMaxCaretContextUtf16 = 64;
    if (SUCCEEDED(hr)) {
      hr = text_range->ShiftStart(edit_cookie, -kMaxCaretContextUtf16, &moved,
                                  nullptr);
    }

    std::array<WCHAR, kMaxCaretContextUtf16> buffer = {};
    ULONG text_length = 0;
    if (SUCCEEDED(hr)) {
      hr = text_range->GetText(
          edit_cookie, 0, buffer.data(), static_cast<ULONG>(buffer.size()),
          &text_length);
    }
    if (SUCCEEDED(hr) && text_length > 0) {
      snapshot_->text_before_caret.assign(buffer.data(),
                                          buffer.data() + text_length);
    }

    text_range->Release();
    return SUCCEEDED(hr) ? S_OK : hr;
  }

 private:
  ITfContext* context_ = nullptr;
  CaretTextSnapshot* snapshot_ = nullptr;
};

class SelectionCandidateCommitEditSession final : public EditSessionBase {
 public:
  SelectionCandidateCommitEditSession(ITfContext* context,
                                      ITfRange* selection_range,
                                      std::uint32_t matched_utf16_length,
                                      std::wstring candidate_text)
      : context_(context),
        selection_range_(selection_range),
        matched_utf16_length_(matched_utf16_length),
        candidate_text_(std::move(candidate_text)) {
    context_->AddRef();
    selection_range_->AddRef();
  }

  ~SelectionCandidateCommitEditSession() override {
    selection_range_->Release();
    context_->Release();
  }

  STDMETHODIMP DoEditSession(TfEditCookie edit_cookie) override {
    if (candidate_text_.empty() || matched_utf16_length_ == 0) {
      return E_INVALIDARG;
    }

    ITfRange* replacement_range = nullptr;
    HRESULT hr = selection_range_->Clone(&replacement_range);
    if (FAILED(hr)) {
      return hr;
    }

    hr = replacement_range->Collapse(edit_cookie, TF_ANCHOR_START);
    LONG moved = 0;
    const LONG matched_length = static_cast<LONG>(matched_utf16_length_);
    if (SUCCEEDED(hr)) {
      hr = replacement_range->ShiftEnd(edit_cookie, matched_length, &moved,
                                       nullptr);
    }
    if (SUCCEEDED(hr) && moved != matched_length) {
      hr = S_FALSE;
    }
    if (SUCCEEDED(hr)) {
      hr = replacement_range->SetText(
          edit_cookie, 0, candidate_text_.c_str(),
          static_cast<LONG>(candidate_text_.size()));
    }
    if (SUCCEEDED(hr)) {
      hr = SetContextSelectionToRangeEnd(edit_cookie, context_,
                                         replacement_range);
    }

    replacement_range->Release();
    return hr;
  }

 private:
  ITfContext* context_ = nullptr;
  ITfRange* selection_range_ = nullptr;
  std::uint32_t matched_utf16_length_ = 0;
  std::wstring candidate_text_;
};

class RangeCandidateCommitEditSession final : public EditSessionBase {
 public:
  RangeCandidateCommitEditSession(ITfContext* context, ITfRange* target_range,
                                  std::wstring candidate_text)
      : context_(context),
        target_range_(target_range),
        candidate_text_(std::move(candidate_text)) {
    context_->AddRef();
    target_range_->AddRef();
  }

  ~RangeCandidateCommitEditSession() override {
    target_range_->Release();
    context_->Release();
  }

  STDMETHODIMP DoEditSession(TfEditCookie edit_cookie) override {
    if (candidate_text_.empty()) {
      return E_INVALIDARG;
    }

    ITfRange* replacement_range = nullptr;
    HRESULT hr = target_range_->Clone(&replacement_range);
    if (FAILED(hr)) {
      return hr;
    }

    hr = replacement_range->SetText(edit_cookie, 0, candidate_text_.c_str(),
                                    static_cast<LONG>(candidate_text_.size()));
    if (SUCCEEDED(hr)) {
      hr = SetContextSelectionToRangeEnd(edit_cookie, context_,
                                         replacement_range);
    }

    replacement_range->Release();
    return hr;
  }

 private:
  ITfContext* context_ = nullptr;
  ITfRange* target_range_ = nullptr;
  std::wstring candidate_text_;
};

class CaretSegmentRangeEditSession final : public EditSessionBase {
 public:
  CaretSegmentRangeEditSession(ITfRange* caret_range,
                               std::uint32_t run_utf16_length,
                               std::uint32_t start_utf16_offset,
                               std::uint32_t matched_utf16_length,
                               ITfRange** target_range)
      : caret_range_(caret_range),
        run_utf16_length_(run_utf16_length),
        start_utf16_offset_(start_utf16_offset),
        matched_utf16_length_(matched_utf16_length),
        target_range_(target_range) {
    caret_range_->AddRef();
  }

  ~CaretSegmentRangeEditSession() override {
    caret_range_->Release();
  }

  STDMETHODIMP DoEditSession(TfEditCookie edit_cookie) override {
    if (target_range_ == nullptr || *target_range_ != nullptr ||
        matched_utf16_length_ == 0 ||
        start_utf16_offset_ + matched_utf16_length_ > run_utf16_length_) {
      return E_INVALIDARG;
    }

    ITfRange* range = nullptr;
    HRESULT hr = caret_range_->Clone(&range);
    if (FAILED(hr)) {
      return hr;
    }

    const LONG distance_to_start =
        -static_cast<LONG>(run_utf16_length_ - start_utf16_offset_);
    const LONG distance_to_end = -static_cast<LONG>(
        run_utf16_length_ - start_utf16_offset_ - matched_utf16_length_);
    LONG moved_start = 0;
    LONG moved_end = 0;
    hr = range->ShiftStart(edit_cookie, distance_to_start, &moved_start,
                           nullptr);
    if (SUCCEEDED(hr)) {
      hr = range->ShiftEnd(edit_cookie, distance_to_end, &moved_end, nullptr);
    }
    if (SUCCEEDED(hr) &&
        (moved_start != distance_to_start || moved_end != distance_to_end)) {
      hr = S_FALSE;
    }
    if (SUCCEEDED(hr)) {
      *target_range_ = range;
      range = nullptr;
    }

    SafeRelease(range);
    return hr;
  }

 private:
  ITfRange* caret_range_ = nullptr;
  std::uint32_t run_utf16_length_ = 0;
  std::uint32_t start_utf16_offset_ = 0;
  std::uint32_t matched_utf16_length_ = 0;
  ITfRange** target_range_ = nullptr;
};

class SelectionPrefixTextExtentEditSession final : public EditSessionBase {
 public:
  SelectionPrefixTextExtentEditSession(ITfContextView* view,
                                       ITfRange* selection_range,
                                       std::uint32_t matched_utf16_length,
                                       std::optional<RECT>* text_rect)
      : view_(view),
        selection_range_(selection_range),
        matched_utf16_length_(matched_utf16_length),
        text_rect_(text_rect) {
    view_->AddRef();
    selection_range_->AddRef();
  }

  ~SelectionPrefixTextExtentEditSession() override {
    selection_range_->Release();
    view_->Release();
  }

  STDMETHODIMP DoEditSession(TfEditCookie edit_cookie) override {
    if (matched_utf16_length_ == 0 || text_rect_ == nullptr) {
      return S_OK;
    }

    ITfRange* prefix_range = nullptr;
    HRESULT hr = selection_range_->Clone(&prefix_range);
    if (FAILED(hr)) {
      return S_OK;
    }

    hr = prefix_range->Collapse(edit_cookie, TF_ANCHOR_START);
    LONG moved = 0;
    const LONG matched_length = static_cast<LONG>(matched_utf16_length_);
    if (SUCCEEDED(hr)) {
      hr = prefix_range->ShiftEnd(edit_cookie, matched_length, &moved,
                                  nullptr);
    }
    if (SUCCEEDED(hr) && moved == matched_length) {
      RECT rect = {};
      BOOL clipped = FALSE;
      hr = view_->GetTextExt(edit_cookie, prefix_range, &rect, &clipped);
      if (SUCCEEDED(hr) && IsUsableScreenRect(rect)) {
        *text_rect_ = rect;
      }
    }

    prefix_range->Release();
    return S_OK;
  }

 private:
  ITfContextView* view_ = nullptr;
  ITfRange* selection_range_ = nullptr;
  std::uint32_t matched_utf16_length_ = 0;
  std::optional<RECT>* text_rect_ = nullptr;
};

std::wstring FormatHex(std::uint32_t value) {
  wchar_t buffer[16] = {};
  swprintf_s(buffer, L"%08X", value);
  return buffer;
}

std::wstring Utf8ToWideText(std::string_view text) {
  if (text.empty()) {
    return {};
  }

  const int length =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                          static_cast<int>(text.size()), nullptr, 0);
  if (length <= 0) {
    return {};
  }

  std::wstring wide_text(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                      static_cast<int>(text.size()), wide_text.data(), length);
  return wide_text;
}

std::string WideToUtf8Text(std::wstring_view text) {
  if (text.empty()) {
    return {};
  }

  const int length = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
      static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  if (length <= 0) {
    return {};
  }

  std::string utf8_text(static_cast<std::size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                      static_cast<int>(text.size()), utf8_text.data(), length,
                      nullptr, nullptr);
  return utf8_text;
}

std::unique_ptr<adapters::libhangul::HangulComposer>
CreateComposerForKoreanLayout(
    const engine::layout::LayoutRegistry& layout_registry,
    const engine::layout::KoreanLayoutId& korean_layout_id) {
  const engine::layout::KoreanLayoutMapping* layout =
      layout_registry.FindKoreanLayout(korean_layout_id);
  if (layout == nullptr) {
    return nullptr;
  }

  return adapters::libhangul::CreateLibhangulComposer(
      layout->libhangul_keyboard_id);
}

std::wstring Utf8ToWide(std::string_view text) {
  std::wstring wide_text = Utf8ToWideText(text);
  if (wide_text.empty() && !text.empty()) {
    return L"<invalid-utf8>";
  }
  return wide_text;
}

std::wstring CurrentProcessName() {
  wchar_t buffer[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return L"<unknown-process>";
  }

  const wchar_t* file_name = wcsrchr(buffer, L'\\');
  if (file_name != nullptr && *(file_name + 1) != L'\0') {
    return file_name + 1;
  }

  return buffer;
}

std::wstring WindowClassName(HWND hwnd) {
  if (hwnd == nullptr) {
    return L"<none>";
  }

  wchar_t buffer[128] = {};
  const int length =
      GetClassNameW(hwnd, buffer, static_cast<int>(std::size(buffer)));
  if (length <= 0) {
    return std::wstring(L"<class-failed:") + FormatHex(GetLastError()) + L">";
  }

  return std::wstring(buffer, static_cast<std::size_t>(length));
}

bool IsContextTransitory(ITfContext* context) {
  if (context == nullptr) {
    return false;
  }

  TF_STATUS status = {};
  return SUCCEEDED(context->GetStatus(&status)) &&
         (status.dwStaticFlags & TS_SS_TRANSITORY) != 0;
}

edit::TransitoryCompositionBridgeSnapshot CaptureTransitoryBridgeSnapshot(
    ITfContext* context, bool internal_composing,
    bool has_tracked_tsf_composition, std::wstring preedit) {
  edit::TransitoryCompositionBridgeSnapshot snapshot;
  snapshot.target.process_name = CurrentProcessName();
  snapshot.context = context;
  snapshot.internal_composing = internal_composing;
  snapshot.has_tracked_tsf_composition = has_tracked_tsf_composition;
  snapshot.preedit = std::move(preedit);
  if (context == nullptr) {
    return snapshot;
  }

  snapshot.target.is_transitory = IsContextTransitory(context);

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

#if defined(_DEBUG)

std::wstring GuidToString(REFGUID guid) {
  wchar_t buffer[39] = {};
  if (StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer))) == 0) {
    return L"<invalid-guid>";
  }

  return buffer;
}

std::wstring PointerToString(const void* pointer) {
  wchar_t buffer[32] = {};
  swprintf_s(buffer, L"%p", pointer);
  return buffer;
}

std::wstring FileNameFromPath(std::wstring_view path) {
  const std::size_t separator = path.find_last_of(L"\\/");
  if (separator != std::wstring_view::npos && separator + 1 < path.size()) {
    return std::wstring(path.substr(separator + 1));
  }

  return std::wstring(path);
}

std::wstring ProcessNameFromPid(DWORD process_id) {
  if (process_id == 0) {
    return L"<none>";
  }

  HANDLE process =
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
  if (process == nullptr) {
    return std::wstring(L"<open-failed:") + FormatHex(GetLastError()) + L">";
  }

  wchar_t buffer[MAX_PATH] = {};
  DWORD length = MAX_PATH;
  const BOOL ok = QueryFullProcessImageNameW(process, 0, buffer, &length);
  const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
  CloseHandle(process);
  if (!ok || length == 0) {
    return std::wstring(L"<query-failed:") + FormatHex(error) + L">";
  }

  return FileNameFromPath(std::wstring_view(buffer, length));
}

std::wstring ForegroundWindowDiagnostics() {
  HWND hwnd = GetForegroundWindow();
  DWORD process_id = 0;
  const DWORD thread_id =
      hwnd != nullptr ? GetWindowThreadProcessId(hwnd, &process_id) : 0;

  return L"foreground_hwnd=" + PointerToString(hwnd) + L" foreground_pid=" +
         std::to_wstring(process_id) + L" foreground_tid=" +
         std::to_wstring(thread_id) + L" foreground_process=" +
         ProcessNameFromPid(process_id) + L" foreground_class=" +
         WindowClassName(hwnd);
}

std::wstring WindowProcessDiagnostics(HWND hwnd, const wchar_t* label) {
  DWORD process_id = 0;
  const DWORD thread_id =
      hwnd != nullptr ? GetWindowThreadProcessId(hwnd, &process_id) : 0;

  return std::wstring(label) + L"_hwnd=" + PointerToString(hwnd) + L" " +
         label + L"_pid=" + std::to_wstring(process_id) + L" " + label +
         L"_tid=" + std::to_wstring(thread_id) + L" " + label +
         L"_process=" + ProcessNameFromPid(process_id) + L" " + label +
         L"_class=" + WindowClassName(hwnd);
}

std::wstring VariantValueToString(const VARIANT& value) {
  switch (value.vt) {
    case VT_EMPTY:
      return L"<empty>";
    case VT_I4:
      return std::to_wstring(value.lVal);
    case VT_UI4:
      return std::to_wstring(value.ulVal);
    case VT_BOOL:
      return value.boolVal == VARIANT_TRUE ? L"1" : L"0";
    default:
      return std::wstring(L"<vt:") + std::to_wstring(value.vt) + L">";
  }
}

std::wstring CompartmentDiagnostics(ITfCompartmentMgr* manager, REFGUID guid,
                                    const wchar_t* label) {
  ITfCompartment* compartment = nullptr;
  const HRESULT hr = manager->GetCompartment(guid, &compartment);
  std::wstring diagnostics = std::wstring(L" ") + label + L"_get_hr=0x" +
                              FormatHex(static_cast<std::uint32_t>(hr));
  if (FAILED(hr) || compartment == nullptr) {
    return diagnostics;
  }

  VARIANT value;
  VariantInit(&value);
  const HRESULT value_hr = compartment->GetValue(&value);
  diagnostics += std::wstring(L" ") + label + L"_value_hr=0x" +
                 FormatHex(static_cast<std::uint32_t>(value_hr));
  if (SUCCEEDED(value_hr)) {
    diagnostics += std::wstring(L" ") + label + L"_vt=" +
                   std::to_wstring(value.vt) + L" " + label + L"_value=" +
                   VariantValueToString(value);
  }
  VariantClear(&value);
  compartment->Release();
  return diagnostics;
}

std::wstring ActiveViewDiagnostics(ITfContext* context, HWND* hwnd_out) {
  if (hwnd_out != nullptr) {
    *hwnd_out = nullptr;
  }
  if (context == nullptr) {
    return L"active_view_context=null";
  }

  ITfContextView* view = nullptr;
  const HRESULT view_hr = context->GetActiveView(&view);
  std::wstring diagnostics =
      L"active_view_hr=0x" +
      FormatHex(static_cast<std::uint32_t>(view_hr)) + L" active_view=" +
      PointerToString(view);
  if (FAILED(view_hr) || view == nullptr) {
    return diagnostics;
  }

  HWND hwnd = nullptr;
  const HRESULT hwnd_hr = view->GetWnd(&hwnd);
  diagnostics += L" get_wnd_hr=0x" +
                 FormatHex(static_cast<std::uint32_t>(hwnd_hr)) + L" " +
                 WindowProcessDiagnostics(hwnd, L"view");
  if (SUCCEEDED(hwnd_hr) && hwnd_out != nullptr) {
    *hwnd_out = hwnd;
  }
  view->Release();
  return diagnostics;
}

std::wstring ContextDiagnostics(ITfContext* context) {
  if (context == nullptr) {
    return L"context_diag=null";
  }

  std::wstring diagnostics = L"context_diag=1";
  TF_STATUS status = {};
  const HRESULT status_hr = context->GetStatus(&status);
  diagnostics += L" status_hr=0x" +
                 FormatHex(static_cast<std::uint32_t>(status_hr));
  if (SUCCEEDED(status_hr)) {
    diagnostics += L" dynamic=0x" +
                   FormatHex(static_cast<std::uint32_t>(
                       status.dwDynamicFlags)) +
                   L" static=0x" +
                   FormatHex(static_cast<std::uint32_t>(
                       status.dwStaticFlags)) +
                   L" readonly=" +
                   std::to_wstring(
                       (status.dwDynamicFlags & TF_SD_READONLY) ? 1 : 0) +
                   L" transitory=" +
                   std::to_wstring(
                       (status.dwStaticFlags & TS_SS_TRANSITORY) ? 1 : 0) +
                   L" regions=" +
                   std::to_wstring(
                       (status.dwStaticFlags & TF_SS_REGIONS) ? 1 : 0) +
                   L" disjointsel=" +
                   std::to_wstring(
                       (status.dwStaticFlags & TF_SS_DISJOINTSEL) ? 1 : 0);
  }

  ITfCompartmentMgr* compartment_manager = nullptr;
  const HRESULT compartment_hr = context->QueryInterface(
      IID_ITfCompartmentMgr, reinterpret_cast<void**>(&compartment_manager));
  diagnostics += L" compartment_mgr_hr=0x" +
                 FormatHex(static_cast<std::uint32_t>(compartment_hr));
  if (SUCCEEDED(compartment_hr) && compartment_manager != nullptr) {
    diagnostics +=
        CompartmentDiagnostics(compartment_manager,
                               GUID_COMPARTMENT_EMPTYCONTEXT, L"empty");
    diagnostics += CompartmentDiagnostics(
        compartment_manager, GUID_COMPARTMENT_KEYBOARD_DISABLED,
        L"keyboard_disabled");
    compartment_manager->Release();
  }

  HWND active_view_hwnd = nullptr;
  diagnostics += L" " + ActiveViewDiagnostics(context, &active_view_hwnd);
  return diagnostics;
}

const wchar_t* TransitoryBridgeTargetKindName(
    edit::TransitoryCompositionBridgeTargetKind kind) {
  switch (kind) {
    case edit::TransitoryCompositionBridgeTargetKind::kNone:
      return L"None";
    case edit::TransitoryCompositionBridgeTargetKind::kObserveOnly:
      return L"ObserveOnly";
    case edit::TransitoryCompositionBridgeTargetKind::kSuppressEngineReset:
      return L"SuppressEngineReset";
  }

  return L"Unknown";
}

std::wstring TransitoryBridgeSnapshotDiagnostics(
    const edit::TransitoryCompositionBridgeSnapshot& snapshot) {
  const edit::TransitoryCompositionBridgeTargetKind kind =
      edit::GetTransitoryCompositionBridgeTargetKind(snapshot.target);
  return L"transitory_bridge_target_kind=" +
         std::wstring(TransitoryBridgeTargetKindName(kind)) +
         L" transitory_bridge_process=" + snapshot.target.process_name +
         L" transitory_bridge_view_class=" + snapshot.target.view_class +
         L" transitory_bridge_transitory=" +
         std::to_wstring(snapshot.target.is_transitory ? 1 : 0) +
         L" transitory_bridge_context=" + PointerToString(snapshot.context) +
         L" transitory_bridge_view_hwnd=" +
         PointerToString(snapshot.view_hwnd) +
         L" transitory_bridge_internal_engine_composing=" +
         std::to_wstring(snapshot.internal_composing ? 1 : 0) +
         L" transitory_bridge_tracked_tsf_composition=" +
         std::to_wstring(snapshot.has_tracked_tsf_composition ? 1 : 0);
}

const wchar_t* CategoryName(KeyEventCategory category) {
  switch (category) {
    case KeyEventCategory::kHangulAscii:
      return L"HangulAscii";
    case KeyEventCategory::kBackspace:
      return L"Backspace";
    case KeyEventCategory::kDelimiter:
      return L"Delimiter";
    case KeyEventCategory::kPureModifier:
      return L"PureModifier";
    case KeyEventCategory::kModifiedShortcut:
      return L"ModifiedShortcut";
    case KeyEventCategory::kUnhandled:
      return L"Unhandled";
  }

  return L"Unknown";
}

const wchar_t* ShortcutActionName(engine::shortcut::ShortcutAction action) {
  switch (action) {
    case engine::shortcut::ShortcutAction::kNone:
      return L"None";
  }

  return L"Unknown";
}

std::wstring CompositionTrackingState(const wchar_t* label,
                                      const ITfComposition* composition,
                                      const ITfContext* context) {
  return std::wstring(label) + L"_composition=" + PointerToString(composition) +
         L" " + label + L"_context=" + PointerToString(context);
}

std::wstring EscapeTextForLog(std::wstring_view text) {
  constexpr std::size_t kMaxLogText = 64;
  std::wstring escaped;
  escaped.reserve(text.size());
  for (wchar_t ch : text) {
    switch (ch) {
      case L'\r':
        escaped += L"\\r";
        break;
      case L'\n':
        escaped += L"\\n";
        break;
      case L'\t':
        escaped += L"\\t";
        break;
      default:
        escaped.push_back(ch);
        break;
    }
    if (escaped.size() >= kMaxLogText) {
      escaped.resize(kMaxLogText);
      escaped += L"...";
      break;
    }
  }
  return escaped;
}

bool EndsWithText(std::wstring_view text, std::wstring_view suffix) {
  if (suffix.empty() || suffix.size() > text.size()) {
    return false;
  }
  return text.substr(text.size() - suffix.size()) == suffix;
}

bool SafeDwordAdd(DWORD a, DWORD b, DWORD* result) {
  if (result == nullptr || a > MAXDWORD - b) {
    return false;
  }
  *result = a + b;
  return true;
}

bool SafeDwordMultiply(DWORD a, DWORD b, DWORD* result) {
  if (result == nullptr || (a != 0 && b > MAXDWORD / a)) {
    return false;
  }
  *result = a * b;
  return true;
}

struct ReconvertDocumentFeed {
  std::wstring preceding_text;
  std::wstring preceding_composition;
  std::wstring target;
  std::wstring following_composition;
  std::wstring following_text;
  bool valid = false;
};

ReconvertDocumentFeed DecomposeReconvertDocumentFeed(
    const RECONVERTSTRING* reconvert_string) {
  ReconvertDocumentFeed feed;
  if (reconvert_string == nullptr ||
      reconvert_string->dwSize < sizeof(RECONVERTSTRING) ||
      reconvert_string->dwVersion != 0 ||
      reconvert_string->dwStrOffset > reconvert_string->dwSize) {
    return feed;
  }

  DWORD buffer_size_in_bytes = 0;
  if (!SafeDwordAdd(reconvert_string->dwSize - reconvert_string->dwStrOffset,
                    0, &buffer_size_in_bytes)) {
    return feed;
  }

  DWORD string_size_in_bytes = 0;
  if (!SafeDwordMultiply(reconvert_string->dwStrLen, sizeof(wchar_t),
                         &string_size_in_bytes) ||
      string_size_in_bytes > buffer_size_in_bytes ||
      reconvert_string->dwCompStrOffset > buffer_size_in_bytes ||
      reconvert_string->dwTargetStrOffset > buffer_size_in_bytes ||
      (reconvert_string->dwCompStrOffset % sizeof(wchar_t)) != 0 ||
      (reconvert_string->dwTargetStrOffset % sizeof(wchar_t)) != 0) {
    return feed;
  }

  const DWORD composition_begin =
      reconvert_string->dwCompStrOffset / sizeof(wchar_t);
  DWORD composition_end = 0;
  if (!SafeDwordAdd(composition_begin, reconvert_string->dwCompStrLen,
                    &composition_end)) {
    return feed;
  }

  const DWORD target_begin =
      reconvert_string->dwTargetStrOffset / sizeof(wchar_t);
  DWORD target_end = 0;
  if (!SafeDwordAdd(target_begin, reconvert_string->dwTargetStrLen,
                    &target_end)) {
    return feed;
  }

  if (!(composition_begin <= target_begin && target_end <= composition_end &&
        composition_end <= reconvert_string->dwStrLen)) {
    return feed;
  }

  const wchar_t* string_buffer = reinterpret_cast<const wchar_t*>(
      reinterpret_cast<const BYTE*>(reconvert_string) +
      reconvert_string->dwStrOffset);
  feed.preceding_text.assign(string_buffer, string_buffer + composition_begin);
  feed.preceding_composition.assign(string_buffer + composition_begin,
                                    string_buffer + target_begin);
  feed.target.assign(string_buffer + target_begin, string_buffer + target_end);
  feed.following_composition.assign(string_buffer + target_end,
                                    string_buffer + composition_end);
  feed.following_text.assign(string_buffer + composition_end,
                             string_buffer + reconvert_string->dwStrLen);
  feed.valid = true;
  return feed;
}

template <typename Interface>
void AppendInterfaceProbe(IUnknown* unknown, REFIID iid, const wchar_t* name,
                          std::wstring* message) {
  if (message == nullptr) {
    return;
  }

  Interface* interface_ptr = nullptr;
  const HRESULT hr =
      unknown != nullptr
          ? unknown->QueryInterface(iid,
                                    reinterpret_cast<void**>(&interface_ptr))
          : E_INVALIDARG;
  *message += L" ";
  *message += name;
  *message += L"_hr=0x";
  *message += FormatHex(static_cast<std::uint32_t>(hr));
  *message += L" ";
  *message += name;
  *message += L"=";
  *message += PointerToString(interface_ptr);
  SafeRelease(interface_ptr);
}

void LogTransitoryParentProbe(ITfContext* context, const wchar_t* origin) {
  ITfDocumentMgr* document_mgr = nullptr;
  HRESULT doc_hr =
      context != nullptr ? context->GetDocumentMgr(&document_mgr) : E_INVALIDARG;

  std::wstring message =
      L"[MilkyWayIME][FallbackProbe][TransitoryParent] origin=" +
      std::wstring(origin) + L" context=" + PointerToString(context) +
      L" doc_hr=0x" + FormatHex(static_cast<std::uint32_t>(doc_hr)) +
      L" docmgr=" + PointerToString(document_mgr);
  if (FAILED(doc_hr) || document_mgr == nullptr) {
    debug::DebugLog(std::move(message));
    SafeRelease(document_mgr);
    return;
  }

  ITfCompartmentMgr* compartment_manager = nullptr;
  const HRESULT manager_hr = document_mgr->QueryInterface(
      IID_ITfCompartmentMgr, reinterpret_cast<void**>(&compartment_manager));
  message += L" compartment_mgr_hr=0x" +
             FormatHex(static_cast<std::uint32_t>(manager_hr)) +
             L" compartment_mgr=" + PointerToString(compartment_manager);
  if (FAILED(manager_hr) || compartment_manager == nullptr) {
    debug::DebugLog(std::move(message));
    SafeRelease(document_mgr);
    return;
  }

  ITfCompartment* compartment = nullptr;
  const HRESULT compartment_hr = compartment_manager->GetCompartment(
      GUID_COMPARTMENT_TRANSITORYEXTENSION_PARENT, &compartment);
  message += L" parent_get_hr=0x" +
             FormatHex(static_cast<std::uint32_t>(compartment_hr)) +
             L" parent_compartment=" + PointerToString(compartment);
  if (SUCCEEDED(compartment_hr) && compartment != nullptr) {
    VARIANT value;
    VariantInit(&value);
    const HRESULT value_hr = compartment->GetValue(&value);
    message += L" parent_value_hr=0x" +
               FormatHex(static_cast<std::uint32_t>(value_hr)) +
               L" parent_vt=" + std::to_wstring(value.vt);
    if (SUCCEEDED(value_hr) && value.vt == VT_UNKNOWN &&
        value.punkVal != nullptr) {
      message += L" parent_unknown=" + PointerToString(value.punkVal);
      AppendInterfaceProbe<IUnknown>(value.punkVal, IID_IUnknown, L"qi_iunknown",
                                     &message);
      AppendInterfaceProbe<ITfDocumentMgr>(value.punkVal, IID_ITfDocumentMgr,
                                           L"qi_document_mgr", &message);
      AppendInterfaceProbe<ITfContext>(value.punkVal, IID_ITfContext,
                                       L"qi_context", &message);
      AppendInterfaceProbe<ITfContextComposition>(
          value.punkVal, IID_ITfContextComposition, L"qi_context_composition",
          &message);
      AppendInterfaceProbe<ITfInsertAtSelection>(
          value.punkVal, IID_ITfInsertAtSelection, L"qi_insert_at_selection",
          &message);
      AppendInterfaceProbe<ITfCompartmentMgr>(
          value.punkVal, IID_ITfCompartmentMgr, L"qi_compartment_mgr",
          &message);
      AppendInterfaceProbe<ITfSource>(value.punkVal, IID_ITfSource,
                                      L"qi_source", &message);
      AppendInterfaceProbe<ITfThreadMgr>(value.punkVal, IID_ITfThreadMgr,
                                         L"qi_thread_mgr", &message);
      AppendInterfaceProbe<IServiceProvider>(
          value.punkVal, IID_IServiceProvider, L"qi_service_provider",
          &message);

      ITfDocumentMgr* parent_document_mgr = nullptr;
      const HRESULT parent_doc_hr = value.punkVal->QueryInterface(
          IID_ITfDocumentMgr,
          reinterpret_cast<void**>(&parent_document_mgr));
      message += L" parent_doc_hr=0x" +
                 FormatHex(static_cast<std::uint32_t>(parent_doc_hr)) +
                 L" parent_docmgr=" + PointerToString(parent_document_mgr);
      if (SUCCEEDED(parent_doc_hr) && parent_document_mgr != nullptr) {
        ITfContext* parent_context = nullptr;
        const HRESULT parent_context_hr =
            parent_document_mgr->GetTop(&parent_context);
        message += L" parent_context_hr=0x" +
                   FormatHex(static_cast<std::uint32_t>(parent_context_hr)) +
                   L" parent_context=" + PointerToString(parent_context);
        if (SUCCEEDED(parent_context_hr) && parent_context != nullptr) {
          message += L" " + ContextDiagnostics(parent_context);
        }
        SafeRelease(parent_context);
      }
      SafeRelease(parent_document_mgr);
    }
    VariantClear(&value);
  }

  debug::DebugLog(std::move(message));
  SafeRelease(compartment);
  SafeRelease(compartment_manager);
  SafeRelease(document_mgr);
}

void LogImmDocumentFeedProbe(ITfContext* context, const wchar_t* origin,
                             std::wstring_view current_preedit) {
  HWND hwnd = nullptr;
  std::wstring view_diagnostics = ActiveViewDiagnostics(context, &hwnd);
  std::wstring message =
      L"[MilkyWayIME][FallbackProbe][DocumentFeed] origin=" +
      std::wstring(origin) + L" context=" + PointerToString(context) +
      L" current_preedit=\"" + EscapeTextForLog(current_preedit) +
      L"\" current_preedit_utf16=" +
      std::to_wstring(current_preedit.size()) + L" " + view_diagnostics;
  if (hwnd == nullptr) {
    debug::DebugLog(std::move(message));
    return;
  }

  constexpr UINT kProbeTimeoutMs = 100;
  DWORD_PTR size_result = 0;
  SetLastError(ERROR_SUCCESS);
  const bool size_sent = SendMessageTimeoutW(
      hwnd, WM_IME_REQUEST, static_cast<WPARAM>(IMR_DOCUMENTFEED), 0,
      SMTO_ABORTIFHUNG | SMTO_BLOCK, kProbeTimeoutMs, &size_result) != 0;
  const DWORD size_error = GetLastError();
  message += L" size_sent=" + std::to_wstring(size_sent ? 1 : 0) +
             L" size_result=" + std::to_wstring(size_result) +
             L" size_error=0x" + FormatHex(size_error);
  if (!size_sent || size_result == 0) {
    debug::DebugLog(std::move(message));
    return;
  }

  constexpr DWORD_PTR kMaxDocumentFeedBytes = 1024 * 1024;
  if (size_result < sizeof(RECONVERTSTRING) ||
      size_result > kMaxDocumentFeedBytes) {
    message += L" invalid_size=1";
    debug::DebugLog(std::move(message));
    return;
  }

  std::vector<BYTE> buffer(static_cast<std::size_t>(size_result));
  PRECONVERTSTRING reconvert_string =
      reinterpret_cast<PRECONVERTSTRING>(buffer.data());
  reconvert_string->dwSize = static_cast<DWORD>(buffer.size());
  reconvert_string->dwVersion = 0;

  DWORD_PTR fill_result = 0;
  SetLastError(ERROR_SUCCESS);
  const bool fill_sent = SendMessageTimeoutW(
      hwnd, WM_IME_REQUEST, static_cast<WPARAM>(IMR_DOCUMENTFEED),
      reinterpret_cast<LPARAM>(reconvert_string),
      SMTO_ABORTIFHUNG | SMTO_BLOCK, kProbeTimeoutMs, &fill_result) != 0;
  const DWORD fill_error = GetLastError();
  message += L" fill_sent=" + std::to_wstring(fill_sent ? 1 : 0) +
             L" fill_result=" + std::to_wstring(fill_result) +
             L" fill_error=0x" + FormatHex(fill_error);
  if (!fill_sent || fill_result == 0) {
    debug::DebugLog(std::move(message));
    return;
  }

  const ReconvertDocumentFeed feed =
      DecomposeReconvertDocumentFeed(reconvert_string);
  const std::wstring full_text =
      feed.preceding_text + feed.preceding_composition + feed.target +
      feed.following_composition + feed.following_text;
  const bool preceding_ends_preedit =
      EndsWithText(feed.preceding_text, current_preedit);
  const bool full_ends_preedit = EndsWithText(full_text, current_preedit);
  message += L" rs_size=" + std::to_wstring(reconvert_string->dwSize) +
             L" rs_version=" + std::to_wstring(reconvert_string->dwVersion) +
             L" str_len=" + std::to_wstring(reconvert_string->dwStrLen) +
             L" str_offset=" + std::to_wstring(reconvert_string->dwStrOffset) +
             L" comp_len=" + std::to_wstring(reconvert_string->dwCompStrLen) +
             L" comp_offset=" +
             std::to_wstring(reconvert_string->dwCompStrOffset) +
             L" target_len=" +
             std::to_wstring(reconvert_string->dwTargetStrLen) +
             L" target_offset=" +
             std::to_wstring(reconvert_string->dwTargetStrOffset) +
             L" valid=" + std::to_wstring(feed.valid ? 1 : 0);
  if (feed.valid) {
    message += L" preceding=\"" + EscapeTextForLog(feed.preceding_text) +
               L"\" precomp=\"" +
               EscapeTextForLog(feed.preceding_composition) +
               L"\" target=\"" + EscapeTextForLog(feed.target) +
               L"\" followcomp=\"" +
               EscapeTextForLog(feed.following_composition) +
               L"\" following=\"" + EscapeTextForLog(feed.following_text) +
               L"\" preceding_ends_preedit=" +
               std::to_wstring(preceding_ends_preedit ? 1 : 0) +
               L" full_ends_preedit=" +
               std::to_wstring(full_ends_preedit ? 1 : 0);
  }

  debug::DebugLog(std::move(message));
}

void LogTransitoryFallbackProbe(ITfContext* context, const wchar_t* origin,
                                std::wstring_view current_preedit) {
  if (!IsContextTransitory(context)) {
    return;
  }

  LogTransitoryParentProbe(context, origin);
  LogImmDocumentFeedProbe(context, origin, current_preedit);
}

#endif

void LoadUserBaseLayouts(engine::layout::LayoutRegistry* layout_registry) {
  if (layout_registry == nullptr) {
    return;
  }

  const std::filesystem::path directory =
      settings::UserBaseLayoutDirectory();
  if (directory.empty()) {
    return;
  }

  std::error_code error_code;
  if (!std::filesystem::is_directory(directory, error_code)) {
    return;
  }

#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][LayoutLoader][Base][Begin] dir=" +
                  directory.wstring());
#endif

  engine::layout::BaseLayoutDirectoryLoadResult result;
  try {
    result = engine::layout::LoadBaseLayoutDirectory(directory);
  } catch (const std::exception& error) {
#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][LayoutLoader][Base][Exception] " +
                    Utf8ToWide(error.what()));
#endif
    return;
  }
#if defined(_DEBUG)
  std::size_t error_count = 0;
  for (const std::string& error : result.errors) {
    ++error_count;
    debug::DebugLog(L"[MilkyWayIME][LayoutLoader][Base][Error] " +
                    Utf8ToWide(error));
  }
#endif

  std::size_t loaded_count = 0;
  std::size_t override_count = 0;
  std::size_t skipped_count = 0;
  for (auto definition : result.definitions) {
    const std::string id = definition.layout.id;
    const bool overrides_existing =
        layout_registry->FindBaseLayout(id) != nullptr;
    if (!layout_registry->AddBaseLayout(std::move(definition))) {
      ++skipped_count;
#if defined(_DEBUG)
      debug::DebugLog(L"[MilkyWayIME][LayoutLoader][Base][Skipped] id=" +
                      Utf8ToWide(id));
#endif
      continue;
    }
    ++loaded_count;
    if (overrides_existing) {
      ++override_count;
    }
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][LayoutLoader][Base]" +
        std::wstring(overrides_existing ? L"[Override] id="
                                        : L"[Loaded] id=") +
        Utf8ToWide(id));
#endif
  }

#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][LayoutLoader][Base][End] loaded=" +
                  std::to_wstring(loaded_count) + L" overrides=" +
                  std::to_wstring(override_count) + L" errors=" +
                  std::to_wstring(error_count) + L" skipped=" +
                  std::to_wstring(skipped_count));
#endif
}

const wchar_t* EditSessionRequestPolicyName(EditSessionRequestPolicy policy) {
  switch (policy) {
    case EditSessionRequestPolicy::kKeyPathWrite:
      return L"KeyPathWrite";
    case EditSessionRequestPolicy::kSyncPreferredWrite:
      return L"SyncPreferredWrite";
  }

  return L"Unknown";
}

DWORD RequestFlagsForPolicy(EditSessionRequestPolicy policy,
                            bool async_fallback = false) {
  if (policy == EditSessionRequestPolicy::kKeyPathWrite || async_fallback) {
    return TF_ES_ASYNCDONTCARE | TF_ES_READWRITE;
  }

  return TF_ES_SYNC | TF_ES_READWRITE;
}

bool ShouldRetryAsync(HRESULT hr) {
  return hr == TF_E_SYNCHRONOUS || hr == TS_E_READONLY;
}

bool SameNormalizedKeyEvent(const engine::key::NormalizedKeyEvent& left,
                            const engine::key::NormalizedKeyEvent& right) {
  return left.key.virtual_key == right.key.virtual_key &&
         left.key.scan_code == right.key.scan_code &&
         left.key.extended == right.key.extended &&
         left.transition == right.transition &&
         left.input_label_key == right.input_label_key &&
         left.modifiers.ctrl == right.modifiers.ctrl &&
         left.modifiers.alt == right.modifiers.alt &&
         left.modifiers.shift == right.modifiers.shift &&
         left.modifiers.win == right.modifiers.win;
}

}  // namespace

HRESULT TipTextService::CreateInstance(IUnknown* outer, REFIID riid,
                                       void** ppv) {
  if (ppv == nullptr) {
    return E_INVALIDARG;
  }

  *ppv = nullptr;
  if (outer != nullptr) {
    return CLASS_E_NOAGGREGATION;
  }

  TipTextService* service = new (std::nothrow) TipTextService();
  if (service == nullptr) {
    return E_OUTOFMEMORY;
  }

  const HRESULT hr = service->QueryInterface(riid, ppv);
  service->Release();
  return hr;
}

TipTextService::TipTextService()
    : session_(layout_registry_.DefaultBaseLayout().id,
               layout_registry_.DefaultKoreanLayout().id),
      edit_sink_(this),
      logic_(&session_, adapters::libhangul::CreateLibhangulComposer(),
             &edit_sink_, &layout_registry_) {
  LoadUserBaseLayouts(&layout_registry_);
  const settings::SettingsStore settings_store;
  const settings::UserSettings user_settings =
      settings::ResolveUserSettings(settings_store.Load(), layout_registry_);
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][LayoutSelection][Initial] base=" +
                  Utf8ToWide(user_settings.base_layout_id) + L" korean=" +
                  Utf8ToWide(user_settings.korean_layout_id));
#endif
  std::unique_ptr<adapters::libhangul::HangulComposer> composer =
      CreateComposerForKoreanLayout(layout_registry_,
                                    user_settings.korean_layout_id);
  if (composer != nullptr) {
    logic_.ReplaceComposer(std::move(composer));
    session_.SetLayouts(user_settings.base_layout_id,
                        user_settings.korean_layout_id);
  }
  DllAddRef();
}

TipTextService::~TipTextService() {
  if (thread_mgr_ != nullptr) {
    Deactivate();
  }

  CloseCandidateList();
  ClearCompositionTracking();
  DllRelease();
}

STDMETHODIMP TipTextService::QueryInterface(REFIID riid, void** ppv) {
  if (ppv == nullptr) {
    return E_INVALIDARG;
  }

  *ppv = nullptr;
  if (riid == IID_IUnknown || riid == IID_ITfTextInputProcessor) {
    *ppv = static_cast<ITfTextInputProcessor*>(this);
  } else if (riid == IID_ITfTextInputProcessorEx) {
    *ppv = static_cast<ITfTextInputProcessorEx*>(this);
  } else if (riid == IID_ITfThreadMgrEventSink) {
    *ppv = static_cast<ITfThreadMgrEventSink*>(this);
  } else if (riid == IID_ITfTextEditSink) {
    *ppv = static_cast<ITfTextEditSink*>(this);
  } else if (riid == IID_ITfKeyEventSink) {
    *ppv = static_cast<ITfKeyEventSink*>(this);
  } else if (riid == IID_ITfCompositionSink) {
    *ppv = static_cast<ITfCompositionSink*>(this);
  } else if (riid == IID_ITfThreadFocusSink) {
    *ppv = static_cast<ITfThreadFocusSink*>(this);
  } else if (riid == IID_ITfCompartmentEventSink) {
    *ppv = static_cast<ITfCompartmentEventSink*>(this);
  } else if (riid == IID_ITfActiveLanguageProfileNotifySink) {
    *ppv = static_cast<ITfActiveLanguageProfileNotifySink*>(this);
  } else if (riid == IID_ITfDisplayAttributeProvider) {
    *ppv = static_cast<ITfDisplayAttributeProvider*>(this);
  }

  if (*ppv == nullptr) {
    return E_NOINTERFACE;
  }

  AddRef();
  return S_OK;
}

STDMETHODIMP_(ULONG) TipTextService::AddRef() {
  return ++ref_count_;
}

STDMETHODIMP_(ULONG) TipTextService::Release() {
  const ULONG value = --ref_count_;
  if (value == 0) {
    delete this;
  }
  return value;
}

STDMETHODIMP TipTextService::Activate(ITfThreadMgr* thread_mgr,
                                      TfClientId client_id) {
  return ActivateEx(thread_mgr, client_id, 0);
}

STDMETHODIMP TipTextService::ActivateEx(ITfThreadMgr* thread_mgr,
                                        TfClientId client_id, DWORD flags) {
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][ActivateEx][Begin] process=" +
                  CurrentProcessName() + L" pid=" +
                  std::to_wstring(GetCurrentProcessId()) + L" tid=" +
                  std::to_wstring(GetCurrentThreadId()) + L" client_id=" +
                  std::to_wstring(client_id) + L" flags=0x" +
                  FormatHex(flags) + L" immersive=" +
                  std::to_wstring((flags & TF_TMF_IMMERSIVEMODE) ? 1 : 0) +
                  L" secure=" +
                  std::to_wstring((flags & TF_TMAE_SECUREMODE) ? 1 : 0) +
                  L" comless=" +
                  std::to_wstring((flags & TF_TMAE_COMLESS) ? 1 : 0) +
                  L" " + ForegroundWindowDiagnostics());
#endif
  if (thread_mgr == nullptr) {
    return E_INVALIDARG;
  }

  if (thread_mgr_ != nullptr) {
    return E_UNEXPECTED;
  }

  thread_mgr_ = thread_mgr;
  thread_mgr_->AddRef();
  client_id_ = client_id;
  activate_flags_ = flags;
  ime_open_ = true;

  HRESULT hr = AdviseThreadMgrEventSink();
  if (FAILED(hr)) {
#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][ActivateEx][AdviseThreadMgrEventSinkFailed] hr=0x" +
                    FormatHex(static_cast<std::uint32_t>(hr)));
#endif
    Deactivate();
    return hr;
  }

  hr = AdviseThreadFocusSink();
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][ActivateEx][AdviseThreadFocusSink] hr=0x" +
                  FormatHex(static_cast<std::uint32_t>(hr)) +
                  L" cookie=" + std::to_wstring(thread_focus_sink_cookie_));
#endif

  hr = AdviseActiveLanguageProfileNotifySink();
#if defined(_DEBUG)
  debug::DebugLog(
      L"[MilkyWayIME][ActivateEx][AdviseActiveLanguageProfileNotifySink] "
      L"hr=0x" +
      FormatHex(static_cast<std::uint32_t>(hr)) + L" cookie=" +
      std::to_wstring(active_language_profile_notify_sink_cookie_));
#endif

  hr = AdviseKeyEventSink();
  if (FAILED(hr)) {
#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][ActivateEx][AdviseKeyEventSinkFailed] hr=0x" +
                    FormatHex(static_cast<std::uint32_t>(hr)));
#endif
    Deactivate();
    return hr;
  }

  hr = AdviseKeyboardOpenCloseCompartmentSink();
#if defined(_DEBUG)
  debug::DebugLog(
      L"[MilkyWayIME][ActivateEx][AdviseKeyboardOpenCloseCompartmentSink] hr=0x" +
      FormatHex(static_cast<std::uint32_t>(hr)) + L" cookie=" +
      std::to_wstring(keyboard_openclose_compartment_sink_cookie_));
#endif

  hr = RegisterPreservedKeys();
  if (FAILED(hr)) {
#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][ActivateEx][RegisterPreservedKeysFailed] hr=0x" +
                    FormatHex(static_cast<std::uint32_t>(hr)));
#endif
    Deactivate();
    return hr;
  }

  const HRESULT display_attribute_hr = InitDisplayAttributeGuidAtom();
#if defined(_DEBUG)
  if (FAILED(display_attribute_hr)) {
    debug::DebugLog(
        L"[MilkyWayIME][ActivateEx][InitDisplayAttributeGuidAtomFailed] hr=0x" +
        FormatHex(static_cast<std::uint32_t>(display_attribute_hr)));
  }
#endif

  hr = RefreshFocusedContext();
  if (FAILED(hr)) {
#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][ActivateEx][RefreshFocusedContextFailed] hr=0x" +
                    FormatHex(static_cast<std::uint32_t>(hr)));
#endif
    Deactivate();
    return hr;
  }

  hr = AddInputModeLanguageBarItem();
  if (FAILED(hr)) {
#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][ActivateEx][AddInputModeLanguageBarItemFailed] hr=0x" +
                    FormatHex(static_cast<std::uint32_t>(hr)));
#endif
    Deactivate();
    return hr;
  }

  SetImeOpen(true);
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][ActivateEx][End] process=" +
                  CurrentProcessName() + L" thread_mgr=" +
                  PointerToString(thread_mgr_) + L" text_context=" +
                  PointerToString(text_edit_sink_context_) + L" ime_open=" +
                  std::to_wstring(ime_open_ ? 1 : 0));
#endif
  return S_OK;
}

STDMETHODIMP TipTextService::Deactivate() {
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][Deactivate][Begin] process=" +
                  CurrentProcessName() + L" pid=" +
                  std::to_wstring(GetCurrentProcessId()) + L" thread_mgr=" +
                  PointerToString(thread_mgr_) + L" text_context=" +
                  PointerToString(text_edit_sink_context_) + L" composing=" +
                  std::to_wstring(session_.IsComposing() ? 1 : 0));
#endif
  deactivating_ = true;

  CloseCandidateList();
  if (session_.IsComposing() && text_edit_sink_context_ != nullptr) {
    logic_.OnFocusLost();
    FlushPendingOperations(text_edit_sink_context_,
                           EditSessionRequestPolicy::kSyncPreferredWrite,
                           L"Deactivate");
  }

  ResetTransitoryDirectTextComposition(L"Deactivate");
  ResetNikkeDirectTextComposition(L"Deactivate");
  ResetTransitoryCompositionBridge(L"Deactivate");
  ClearPendingKeyResult();
  edit_sink_.ClearPendingOperations();
  RemoveInputModeLanguageBarItem();
  DetachTextEditSink();
  UnregisterPreservedKeys();
  UnadviseKeyboardOpenCloseCompartmentSink();
  UnadviseKeyEventSink();
  UnadviseActiveLanguageProfileNotifySink();
  UnadviseThreadFocusSink();
  UnadviseThreadMgrEventSink();
  ClearCompositionTracking();
  SafeRelease(thread_mgr_);

  client_id_ = TF_CLIENTID_NULL;
  activate_flags_ = 0;
  ime_open_ = true;
  composing_display_attribute_atom_ = TF_INVALID_GUIDATOM;
  deactivating_ = false;
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][Deactivate][End]");
#endif
  return S_OK;
}

STDMETHODIMP TipTextService::OnInitDocumentMgr(ITfDocumentMgr*) {
  return S_OK;
}

STDMETHODIMP TipTextService::OnUninitDocumentMgr(ITfDocumentMgr*) {
  return S_OK;
}

STDMETHODIMP TipTextService::OnSetFocus(ITfDocumentMgr* focused,
                                        ITfDocumentMgr* previous) {
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][OnSetFocus(DocumentMgr)] process=" +
                  CurrentProcessName() + L" focused=" +
                  PointerToString(focused) + L" previous=" +
                  PointerToString(previous) + L" current_text_context=" +
                  PointerToString(text_edit_sink_context_) + L" composing=" +
                  std::to_wstring(session_.IsComposing() ? 1 : 0) +
                  L" " + ForegroundWindowDiagnostics());
#endif
  if (candidate_list_ != nullptr) {
    CloseCandidateList();
  }
  if (text_edit_sink_context_ != nullptr && session_.IsComposing()) {
    logic_.OnFocusLost();
    FlushPendingOperations(text_edit_sink_context_,
                           EditSessionRequestPolicy::kSyncPreferredWrite,
                           L"OnSetFocus(DocumentMgr)");
  }
  ResetTransitoryDirectTextComposition(L"OnSetFocus(DocumentMgr)");
  ResetNikkeDirectTextComposition(L"OnSetFocus(DocumentMgr)");
  ResetTransitoryCompositionBridge(L"OnSetFocus(DocumentMgr)");

  const HRESULT refresh_hr = RefreshFocusedContext(focused);
  if (SUCCEEDED(refresh_hr)) {
    SyncLayoutSelectionFromSettings(L"OnSetFocus(DocumentMgr)");
  }
  return refresh_hr;
}

STDMETHODIMP TipTextService::OnPushContext(ITfContext* context) {
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][OnPushContext] context=" +
                  PointerToString(context) + L" " +
                  ContextDiagnostics(context) + L" " +
                  ForegroundWindowDiagnostics());
#endif
  const HRESULT refresh_hr = RefreshFocusedContext();
  if (SUCCEEDED(refresh_hr)) {
    SyncLayoutSelectionFromSettings(L"OnPushContext");
  }
  return refresh_hr;
}

STDMETHODIMP TipTextService::OnPopContext(ITfContext* context) {
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][OnPopContext] context=" +
                  PointerToString(context) + L" " +
                  ContextDiagnostics(context) + L" " +
                  ForegroundWindowDiagnostics());
#endif
  const HRESULT refresh_hr = RefreshFocusedContext();
  if (SUCCEEDED(refresh_hr)) {
    SyncLayoutSelectionFromSettings(L"OnPopContext");
  }
  return refresh_hr;
}

STDMETHODIMP TipTextService::OnEndEdit(ITfContext* context,
                                       TfEditCookie read_cookie,
                                       ITfEditRecord*) {
  if (context == nullptr || edit_sink_.is_flushing()) {
    return S_OK;
  }

  if ((candidate_commit_target_kind_ ==
           CandidateCommitTargetKind::kSelectionRange &&
       selection_candidate_target_.context == context) ||
      (candidate_commit_target_kind_ == CandidateCommitTargetKind::kCaretRange &&
       caret_candidate_target_.context == context)) {
    CloseCandidateList();
    return S_OK;
  }

  if (!session_.IsComposing()) {
    return S_OK;
  }

  if (!SelectionInsideComposition(context, read_cookie)) {
    CloseCandidateList();
    logic_.OnSelectionMovedOutsideComposition();
    FlushPendingOperations(context, EditSessionRequestPolicy::kSyncPreferredWrite,
                           L"OnEndEdit");
  }

  return S_OK;
}

STDMETHODIMP TipTextService::OnSetFocus(BOOL foreground) {
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][ITfKeyEventSink::OnSetFocus] foreground=" +
                  std::to_wstring(foreground ? 1 : 0) + L" process=" +
                  CurrentProcessName());
#endif
  if (foreground) {
    SyncLayoutSelectionFromSettings(L"ITfKeyEventSink::OnSetFocus");
  }
  return S_OK;
}

STDMETHODIMP TipTextService::OnTestKeyDown(ITfContext* context, WPARAM wparam,
                                           LPARAM lparam, BOOL* eaten) {
  if (eaten == nullptr) {
    return E_INVALIDARG;
  }

  *eaten = FALSE;
  SyncLayoutSelectionFromSettings(L"OnKeyDown");
  const engine::state::ModifierState modifiers = QueryModifierState();
  const engine::key::NormalizedKeyEvent event = BuildNormalizedKeyEvent(
      wparam, lparam, modifiers, engine::key::KeyTransition::kPressed);

#if defined(_DEBUG)
  debug::DebugLog(
      L"[MilkyWayIME][OnTestKeyDown] vk=0x" +
      std::to_wstring(static_cast<std::uint16_t>(wparam)) + L" context=" +
      PointerToString(context) + L" ime_open=" +
      std::to_wstring(ime_open_ ? 1 : 0) + L" " +
      ContextDiagnostics(context));
#endif

  if (IsImeModeToggleVirtualKey(wparam)) {
#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][OnTestKeyDown][VK_HANGUL] context=" +
                    PointerToString(context) + L" process=" +
                    CurrentProcessName() + L" ime_open=" +
                    std::to_wstring(ime_open_ ? 1 : 0));
#endif
    pending_key_result_.active = true;
    pending_key_result_.event = event;
    pending_key_result_.eaten = true;
    *eaten = TRUE;
    return S_OK;
  }

  if (candidate_list_ != nullptr) {
    pending_key_result_.active = true;
    pending_key_result_.event = event;
    pending_key_result_.eaten =
        !candidate::IsPureModifierVirtualKey(wparam);
    *eaten = pending_key_result_.eaten ? TRUE : FALSE;
    return S_OK;
  }

  if (IsHanjaVirtualKey(wparam)) {
    pending_key_result_.active = true;
    pending_key_result_.event = event;
    pending_key_result_.eaten = ime_open_;
    *eaten = pending_key_result_.eaten ? TRUE : FALSE;
    return S_OK;
  }

  if (!ime_open_ && !(modifiers.ctrl || modifiers.alt || modifiers.win)) {
    ClearPendingKeyResult();
    return S_OK;
  }

  pending_key_result_.active = true;
  pending_key_result_.event = event;
  pending_key_result_.eaten =
      logic_.WouldEatKey(event.key, modifiers, engine::key::KeyTransition::kPressed);

  *eaten = pending_key_result_.eaten
               ? TRUE
               : FALSE;
  return S_OK;
}

STDMETHODIMP TipTextService::OnKeyDown(ITfContext* context, WPARAM wparam,
                                       LPARAM lparam, BOOL* eaten) {
  if (eaten == nullptr) {
    return E_INVALIDARG;
  }

  *eaten = FALSE;
  const engine::state::ModifierState modifiers = QueryModifierState();
  if (IsImeModeToggleVirtualKey(wparam)) {
#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][OnKeyDown][VK_HANGUL] context=" +
                    PointerToString(context) + L" process=" +
                    CurrentProcessName() + L" ime_open_before=" +
                    std::to_wstring(ime_open_ ? 1 : 0));
#endif
    ClearPendingKeyResult();
    logic_.PrepareImeModeToggle();
    ResetTransitoryDirectTextComposition(L"OnKeyDown(VK_HANGUL)");
    ResetTransitoryCompositionBridge(L"OnKeyDown(VK_HANGUL)");
    if (!FinalizeImeModeToggle(context, L"OnKeyDown(VK_HANGUL)")) {
      SyncCompositionTermination();
    }
#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][OnKeyDown][VK_HANGUL][Done] ime_open_after=" +
                    std::to_wstring(ime_open_ ? 1 : 0));
#endif
    *eaten = TRUE;
    return S_OK;
  }

  if (candidate_list_ != nullptr &&
      !candidate::IsPureModifierVirtualKey(wparam)) {
    ClearPendingKeyResult();
    if (IsHanjaVirtualKey(wparam) && ime_open_ &&
        AdvanceHanjaCandidateSegment(context)) {
      *eaten = TRUE;
      return S_OK;
    }

    const candidate::CandidateKeyResult candidate_key_result =
        candidate_list_->HandleVirtualKey(wparam);
    if (candidate_key_result == candidate::CandidateKeyResult::kCommit) {
      OnCandidateListFinalize();
    } else if (candidate_key_result == candidate::CandidateKeyResult::kCancel) {
      OnCandidateListAbort();
    }
    *eaten = TRUE;
    return S_OK;
  }

  if (IsHanjaVirtualKey(wparam)) {
    ClearPendingKeyResult();
    if (ime_open_) {
      HandleHanjaKey(context);
      *eaten = TRUE;
    }
    return S_OK;
  }

  if (context == nullptr) {
    return S_OK;
  }

  if (!ime_open_ && !(modifiers.ctrl || modifiers.alt || modifiers.win)) {
    ClearPendingKeyResult();
    return S_OK;
  }

  const engine::key::PhysicalKey key = BuildPhysicalKey(wparam, lparam);
  const engine::key::NormalizedKeyEvent event = BuildNormalizedKeyEvent(
      wparam, lparam, modifiers, engine::key::KeyTransition::kPressed);
  const bool reuse_pending = pending_key_result_.active &&
                             SameNormalizedKeyEvent(pending_key_result_.event, event);
  const bool pending_eaten = reuse_pending && pending_key_result_.eaten;
  const bool composing_before = session_.IsComposing();
  const bool has_tracked_composition_before = composition_ != nullptr;
  ClearPendingKeyResult();
  const KeyEventResult result =
      logic_.OnKeyEvent(key, modifiers, engine::key::KeyTransition::kPressed);
  const bool composing_after_logic = session_.IsComposing();
  const bool has_tracked_composition_after_logic = composition_ != nullptr;
  const bool reset_transitory_direct_after_flush =
      !composing_after_logic && transitory_direct_text_.IsActive();
  if (!composing_after_logic && transitory_composition_bridge_.IsActive()) {
    ResetTransitoryCompositionBridge(L"OnKeyDownCompositionEnded");
  }
  const bool final_eaten = result.eaten || pending_eaten;
  *eaten = final_eaten ? TRUE : FALSE;

#if defined(_DEBUG)
  {
    const char ascii = logic_.DebugToAscii(key, modifiers);
    std::wstring message = L"[MilkyWayIME][OnKeyDown] vk=0x" +
                           std::to_wstring(key.virtual_key) +
                           L" scan=0x" + std::to_wstring(key.scan_code) +
                           L" ext=" + std::to_wstring(key.extended ? 1 : 0) +
                           L" shift=" + std::to_wstring(modifiers.shift ? 1 : 0) +
                           L" ctrl=" + std::to_wstring(modifiers.ctrl ? 1 : 0) +
                           L" alt=" + std::to_wstring(modifiers.alt ? 1 : 0) +
                           L" win=" + std::to_wstring(modifiers.win ? 1 : 0) +
                            L" input_label=" +
                            Utf8ToWide(engine::key::LayoutKeyName(
                                event.input_label_key)) +
                           L" ascii=0x" +
                           std::to_wstring(static_cast<unsigned char>(ascii)) +
                           L" category=" + CategoryName(result.category) +
                           L" eaten=" + std::to_wstring(final_eaten ? 1 : 0) +
                           L" pending_eaten=" +
                           std::to_wstring(pending_eaten ? 1 : 0) +
                           L" forward=" +
                           std::to_wstring(result.should_forward ? 1 : 0) +
                           L" session_before=" +
                           std::to_wstring(composing_before ? 1 : 0) +
                           L" session_after_logic=" +
                           std::to_wstring(composing_after_logic ? 1 : 0) +
                           L" tracked_before=" +
                           std::to_wstring(has_tracked_composition_before ? 1 : 0) +
                           L" tracked_after_logic=" +
                           std::to_wstring(
                               has_tracked_composition_after_logic ? 1 : 0) +
                           L" context=" + PointerToString(context) +
                           L" " +
                           CompositionTrackingState(
                               L"tracked", composition_, composition_context_) +
                           L" " + transitory_composition_bridge_.DebugState() +
                           L" shortcut=" +
                           ShortcutActionName(result.shortcut_action) +
                           L" commit=\"" + Utf8ToWide(result.commit_text) +
                           L"\" preedit=\"" + Utf8ToWide(result.preedit_text) +
                           L"\"";
    debug::DebugLog(std::move(message));
  }
  if (composing_after_logic) {
    LogTransitoryFallbackProbe(context, L"OnKeyDownAfterLogic",
                               Utf8ToWide(result.preedit_text));
  }
#endif

  if (result.shortcut_action != engine::shortcut::ShortcutAction::kNone) {
    ResetTransitoryDirectTextComposition(L"OnKeyDownShortcut");
  }
  if (edit_sink_.HasPendingOperations()) {
    const HRESULT hr = FlushPendingOperations(
        context, EditSessionRequestPolicy::kKeyPathWrite, L"OnKeyDown");
    if (FAILED(hr)) {
      SyncCompositionTermination();
      return S_OK;
    }
  }
  if (reset_transitory_direct_after_flush) {
    ResetTransitoryDirectTextComposition(L"OnKeyDownCompositionEnded");
  }

  HandleShortcutAction(context, result.shortcut_action);
  return S_OK;
}

STDMETHODIMP TipTextService::OnTestKeyUp(ITfContext*, WPARAM, LPARAM,
                                         BOOL* eaten) {
  if (eaten == nullptr) {
    return E_INVALIDARG;
  }

  *eaten = FALSE;
  return S_OK;
}

STDMETHODIMP TipTextService::OnKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* eaten) {
  if (eaten == nullptr) {
    return E_INVALIDARG;
  }

  *eaten = FALSE;
  return S_OK;
}

STDMETHODIMP TipTextService::OnPreservedKey(ITfContext* context, REFGUID rguid,
                                            BOOL* eaten) {
  if (eaten == nullptr) {
    return E_INVALIDARG;
  }

  *eaten = FALSE;
  if (!IsEqualGUID(rguid, kImeModePreservedKeyGuid)) {
#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][OnPreservedKey][Ignored] guid=" +
                    GuidToString(rguid) + L" context=" +
                    PointerToString(context) + L" process=" +
                    CurrentProcessName());
#endif
    return S_OK;
  }

#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][OnPreservedKey][VK_HANGUL] guid=" +
                  GuidToString(rguid) + L" context=" +
                  PointerToString(context) + L" fallback_context=" +
                  PointerToString(text_edit_sink_context_) + L" process=" +
                  CurrentProcessName() + L" ime_open_before=" +
                  std::to_wstring(ime_open_ ? 1 : 0));
#endif
  logic_.PrepareImeModeToggle();
  ResetTransitoryDirectTextComposition(L"OnPreservedKey(VK_HANGUL)");
  ResetTransitoryCompositionBridge(L"OnPreservedKey(VK_HANGUL)");
  ITfContext* toggle_context =
      context != nullptr ? context : text_edit_sink_context_;
  if (!FinalizeImeModeToggle(toggle_context, L"OnPreservedKey(VK_HANGUL)")) {
    SyncCompositionTermination();
  }
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][OnPreservedKey][VK_HANGUL][Done] ime_open_after=" +
                  std::to_wstring(ime_open_ ? 1 : 0));
#endif
  *eaten = TRUE;
  return S_OK;
}

STDMETHODIMP TipTextService::OnCompositionTerminated(TfEditCookie edit_cookie,
                                                     ITfComposition* composition) {
  const bool tracked = composition_ == composition;
  ITfContext* termination_context =
      composition_context_ != nullptr ? composition_context_
                                      : text_edit_sink_context_;
  const edit::TransitoryCompositionBridgeSnapshot bridge_snapshot =
      CaptureTransitoryBridgeSnapshot(
          termination_context, session_.IsComposing(), tracked,
          Utf8ToWide(session_.snapshot().preedit));
  const bool suppress_engine_reset =
      tracked &&
      transitory_composition_bridge_.ShouldSuppressEngineReset(bridge_snapshot);
  const bool observe_bridge_termination =
      !suppress_engine_reset &&
      transitory_composition_bridge_.ShouldObserveTermination(bridge_snapshot);
  const bool should_reset_engine =
      !suppress_engine_reset && (tracked || session_.IsComposing());
#if defined(_DEBUG)
  debug::DebugLog(
      L"[MilkyWayIME][OnCompositionTerminated][Begin] process=" +
      CurrentProcessName() + L" edit_cookie=0x" +
      FormatHex(static_cast<std::uint32_t>(edit_cookie)) + L" incoming=" +
      PointerToString(composition) + L" tracked=" +
      std::to_wstring(tracked ? 1 : 0) + L" internal_engine_composing=" +
      std::to_wstring(session_.IsComposing() ? 1 : 0) + L" " +
      CompositionTrackingState(L"current", composition_, composition_context_) +
      L" suppress_engine_reset=" +
      std::to_wstring(suppress_engine_reset ? 1 : 0) +
      L" observe_bridge_termination=" +
      std::to_wstring(observe_bridge_termination ? 1 : 0) + L" " +
      transitory_composition_bridge_.DebugState() + L" " +
      TransitoryBridgeSnapshotDiagnostics(bridge_snapshot));
  LogTransitoryFallbackProbe(termination_context,
                             L"OnCompositionTerminatedBeforeReset",
                             Utf8ToWide(session_.snapshot().preedit));
#endif
  if (tracked) {
    CloseCandidateList();
    if (composition_context_ != nullptr) {
      ClearCompositionDisplayAttribute(edit_cookie, composition_context_);
    }
    ClearCompositionTracking();
  }

  if (suppress_engine_reset) {
    transitory_composition_bridge_.NoteSuppressedEngineReset(bridge_snapshot,
                                                            composition);
  } else {
    if (observe_bridge_termination) {
      transitory_composition_bridge_.NoteObservedTermination(bridge_snapshot,
                                                            composition);
    }
    if (should_reset_engine) {
      ResetTransitoryCompositionBridge(L"OnCompositionTerminatedEngineReset");
      logic_.OnCompositionTerminated();
    } else {
      ResetTransitoryCompositionBridge(L"OnCompositionTerminatedNoState");
    }
  }

#if defined(_DEBUG)
  debug::DebugLog(
      L"[MilkyWayIME][OnCompositionTerminated][End] tracked=" +
      std::to_wstring(tracked ? 1 : 0) + L" internal_engine_composing=" +
      std::to_wstring(session_.IsComposing() ? 1 : 0) + L" " +
      CompositionTrackingState(L"current", composition_, composition_context_) +
      L" " + transitory_composition_bridge_.DebugState());
#endif
  return S_OK;
}

STDMETHODIMP TipTextService::OnSetThreadFocus() {
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][OnSetThreadFocus] process=" +
                  CurrentProcessName() + L" thread_mgr=" +
                  PointerToString(thread_mgr_) + L" text_context=" +
                  PointerToString(text_edit_sink_context_));
#endif

  const HRESULT hr = RefreshFocusedContext();
  if (SUCCEEDED(hr)) {
    SyncLayoutSelectionFromSettings(L"OnSetThreadFocus");
  }
#if defined(_DEBUG)
  if (FAILED(hr)) {
    debug::DebugLog(L"[MilkyWayIME][OnSetThreadFocus][RefreshFocusedContextFailed] hr=0x" +
                    FormatHex(static_cast<std::uint32_t>(hr)));
  }
#endif
  return S_OK;
}

STDMETHODIMP TipTextService::OnKillThreadFocus() {
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][OnKillThreadFocus] process=" +
                  CurrentProcessName() + L" thread_mgr=" +
                  PointerToString(thread_mgr_) + L" text_context=" +
                  PointerToString(text_edit_sink_context_) + L" composing=" +
                  std::to_wstring(session_.IsComposing() ? 1 : 0));
#endif
  CloseCandidateList();
  if (transitory_direct_text_.IsActive() && text_edit_sink_context_ != nullptr &&
      session_.IsComposing()) {
    logic_.OnFocusLost();
    FlushPendingOperations(text_edit_sink_context_,
                           EditSessionRequestPolicy::kSyncPreferredWrite,
                           L"OnKillThreadFocus");
  }
  ResetTransitoryDirectTextComposition(L"OnKillThreadFocus");
  ResetNikkeDirectTextComposition(L"OnKillThreadFocus");
  ResetTransitoryCompositionBridge(L"OnKillThreadFocus");
  return S_OK;
}

STDMETHODIMP TipTextService::OnChange(REFGUID guid) {
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][CompartmentEventSink::OnChange] process=" +
                  CurrentProcessName() + L" guid=" + GuidToString(guid));
#endif
  if (!IsEqualGUID(guid, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE)) {
    return S_OK;
  }

  const HRESULT hr =
      SyncImeOpenFromCompartment(L"CompartmentEventSink::OnChange");
#if defined(_DEBUG)
  if (FAILED(hr)) {
    debug::DebugLog(L"[MilkyWayIME][CompartmentEventSink::OnChange][SyncFailed] hr=0x" +
                    FormatHex(static_cast<std::uint32_t>(hr)));
  }
#endif
  return S_OK;
}

STDMETHODIMP TipTextService::OnActivated(REFCLSID clsid, REFGUID profile,
                                         BOOL activated) {
#if defined(_DEBUG)
  ITfDocumentMgr* focused_document_mgr = nullptr;
  HRESULT focus_hr = E_UNEXPECTED;
  if (thread_mgr_ != nullptr) {
    focus_hr = thread_mgr_->GetFocus(&focused_document_mgr);
  }
  const bool is_our_clsid =
      IsEqualCLSID(clsid, registration::kTextServiceClsid) != FALSE;
  const bool is_our_profile =
      IsEqualGUID(profile, registration::kTextServiceProfileGuid) != FALSE;
  debug::DebugLog(
      L"[MilkyWayIME][ActiveLanguageProfileNotifySink::OnActivated] "
      L"process=" +
      CurrentProcessName() + L" activated=" +
      std::to_wstring(activated ? 1 : 0) + L" clsid=" +
      GuidToString(clsid) + L" profile=" + GuidToString(profile) +
      L" our_clsid=" + std::to_wstring(is_our_clsid ? 1 : 0) +
      L" our_profile=" + std::to_wstring(is_our_profile ? 1 : 0) +
      L" focus_hr=0x" + FormatHex(static_cast<std::uint32_t>(focus_hr)) +
      L" focused_docmgr=" + PointerToString(focused_document_mgr) +
      L" text_context=" + PointerToString(text_edit_sink_context_) +
      L" ime_open=" + std::to_wstring(ime_open_ ? 1 : 0) + L" " +
      ForegroundWindowDiagnostics());
  SafeRelease(focused_document_mgr);
#endif
  return S_OK;
}

STDMETHODIMP TipTextService::EnumDisplayAttributeInfo(
    IEnumTfDisplayAttributeInfo** enum_info) {
  return display::CreateEnumDisplayAttributeInfo(enum_info);
}

STDMETHODIMP TipTextService::GetDisplayAttributeInfo(
    REFGUID guid_info, ITfDisplayAttributeInfo** info) {
  return display::CreateDisplayAttributeInfo(guid_info, info);
}

void TipTextService::OnCandidateListFinalize() {
  if (candidate_list_ == nullptr) {
    return;
  }

  const candidate::CandidateUiItem* selected_item =
      candidate_list_->SelectedItem();
  const std::string candidate_text =
      selected_item != nullptr ? selected_item->value_utf8 : std::string();
  const CandidateCommitTargetKind target_kind = candidate_commit_target_kind_;
  ITfContext* selection_context = nullptr;
  ITfRange* selection_range = nullptr;
  std::uint32_t selection_matched_utf16_length = 0;
  ITfContext* caret_context = nullptr;
  ITfRange* caret_range = nullptr;
  if (target_kind == CandidateCommitTargetKind::kSelectionRange) {
    selection_context = selection_candidate_target_.context;
    selection_range = selection_candidate_target_.range;
    selection_matched_utf16_length =
        selection_candidate_target_.matched_utf16_length;
    if (selection_context != nullptr) {
      selection_context->AddRef();
    }
    if (selection_range != nullptr) {
      selection_range->AddRef();
    }
  } else if (target_kind == CandidateCommitTargetKind::kCaretRange) {
    caret_context = caret_candidate_target_.context;
    caret_range = caret_candidate_target_.range;
    if (caret_context != nullptr) {
      caret_context->AddRef();
    }
    if (caret_range != nullptr) {
      caret_range->AddRef();
    }
  }

  CloseCandidateList();
  if (candidate_text.empty()) {
    SafeRelease(selection_range);
    SafeRelease(selection_context);
    SafeRelease(caret_range);
    SafeRelease(caret_context);
    return;
  }

  if (target_kind == CandidateCommitTargetKind::kSelectionRange) {
    const HRESULT hr = CommitSelectionCandidate(
        candidate_text, selection_context, selection_range,
        selection_matched_utf16_length);
#if defined(_DEBUG)
    if (FAILED(hr)) {
      debug::DebugLog(
          L"[MilkyWayIME][CandidateFinalize][SelectionCommitFailed] hr=0x" +
          FormatHex(static_cast<std::uint32_t>(hr)));
    }
#endif
    SafeRelease(selection_range);
    SafeRelease(selection_context);
    SafeRelease(caret_range);
    SafeRelease(caret_context);
    return;
  }

  if (target_kind == CandidateCommitTargetKind::kCaretRange) {
    const HRESULT hr =
        CommitCaretCandidate(candidate_text, caret_context, caret_range);
#if defined(_DEBUG)
    if (FAILED(hr)) {
      debug::DebugLog(
          L"[MilkyWayIME][CandidateFinalize][CaretCommitFailed] hr=0x" +
          FormatHex(static_cast<std::uint32_t>(hr)));
    }
#endif
    SafeRelease(selection_range);
    SafeRelease(selection_context);
    SafeRelease(caret_range);
    SafeRelease(caret_context);
    return;
  }

  SafeRelease(selection_range);
  SafeRelease(selection_context);
  SafeRelease(caret_range);
  SafeRelease(caret_context);

  if (!logic_.CommitCandidate(candidate_text)) {
    return;
  }

  if (composition_context_ == nullptr) {
    SyncCompositionTermination();
    return;
  }

  const HRESULT hr = FlushPendingOperations(
      composition_context_, EditSessionRequestPolicy::kKeyPathWrite,
      L"CandidateFinalize");
  if (FAILED(hr)) {
    SyncCompositionTermination();
  }
}

void TipTextService::OnCandidateListAbort() {
  CloseCandidateList();
}

TfClientId TipTextService::client_id() const {
  return client_id_;
}

bool TipTextService::HasTrackedTsfComposition() const {
  return composition_ != nullptr;
}

bool TipTextService::ShouldUseTransitoryDirectTextComposition(
    ITfContext* context,
    const std::vector<edit::TextEditOperation>& operations) const {
  return transitory_direct_text_.ShouldUse(context, operations);
}

ITfContext* TipTextService::ResolveTransitoryDirectTextContext(
    ITfContext* context,
    const std::vector<edit::TextEditOperation>& operations) const {
  return transitory_direct_text_.ResolveFullContextFromTransitory(context,
                                                                  operations);
}

HRESULT TipTextService::ApplyTransitoryDirectTextComposition(
    TfEditCookie edit_cookie, ITfContext* context,
    const std::vector<edit::TextEditOperation>& operations) {
  return transitory_direct_text_.Apply(edit_cookie, context, operations);
}

void TipTextService::ResetTransitoryDirectTextComposition(
    const wchar_t* reason) {
  transitory_direct_text_.Reset(reason);
}

bool TipTextService::ShouldUseNikkeDirectTextComposition(
    ITfContext* context,
    const std::vector<edit::TextEditOperation>& operations) const {
  return nikke_direct_text_.ShouldUse(context, operations);
}

HRESULT TipTextService::ApplyNikkeDirectTextComposition(
    TfEditCookie edit_cookie, ITfContext* context,
    const std::vector<edit::TextEditOperation>& operations) {
  return nikke_direct_text_.Apply(edit_cookie, context, operations);
}

void TipTextService::ResetNikkeDirectTextComposition(const wchar_t* reason) {
  nikke_direct_text_.Reset(reason);
}

void TipTextService::ResetTransitoryCompositionBridge(const wchar_t* reason) {
  transitory_composition_bridge_.Reset(reason);
}

HRESULT TipTextService::CommitText(TfEditCookie edit_cookie, ITfContext* context,
                                   const std::wstring& text) {
  if (context == nullptr || text.empty()) {
    return S_OK;
  }

  ITfInsertAtSelection* insert_at_selection = nullptr;
  const HRESULT hr = context->QueryInterface(
      IID_ITfInsertAtSelection, reinterpret_cast<void**>(&insert_at_selection));
  if (FAILED(hr)) {
    return hr;
  }

  ITfRange* inserted_range = nullptr;
  const HRESULT insert_hr = insert_at_selection->InsertTextAtSelection(
      edit_cookie, 0, text.c_str(), static_cast<LONG>(text.size()),
      &inserted_range);
  insert_at_selection->Release();
  if (FAILED(insert_hr)) {
    return insert_hr;
  }

  const HRESULT selection_hr =
      MoveSelectionToRangeEnd(edit_cookie, context, inserted_range);
  inserted_range->Release();
  return selection_hr;
}

HRESULT TipTextService::StartComposition(TfEditCookie edit_cookie,
                                         ITfContext* context,
                                         const std::wstring& text) {
  if (context == nullptr) {
    return E_INVALIDARG;
  }

#if defined(_DEBUG)
  const edit::TransitoryCompositionBridgeSnapshot bridge_snapshot_begin =
      CaptureTransitoryBridgeSnapshot(context, session_.IsComposing(),
                                      composition_ != nullptr, text);
  debug::DebugLog(
      L"[MilkyWayIME][StartComposition][Begin] context=" +
      PointerToString(context) + L" text=\"" + text + L"\" session_composing=" +
      std::to_wstring(session_.IsComposing() ? 1 : 0) + L" " +
      CompositionTrackingState(L"current", composition_, composition_context_) +
      L" " + transitory_composition_bridge_.DebugState() + L" " +
      TransitoryBridgeSnapshotDiagnostics(bridge_snapshot_begin));
#endif

  ITfInsertAtSelection* insert_at_selection = nullptr;
  ITfContextComposition* context_composition = nullptr;
  ITfRange* insertion_range = nullptr;
  ITfRange* composition_range = nullptr;
  ITfComposition* composition = nullptr;

  HRESULT hr = context->QueryInterface(IID_ITfInsertAtSelection,
                                       reinterpret_cast<void**>(&insert_at_selection));
  if (FAILED(hr)) {
    goto Exit;
  }

  hr = insert_at_selection->InsertTextAtSelection(edit_cookie, TF_IAS_QUERYONLY,
                                                  nullptr, 0, &insertion_range);
  if (FAILED(hr)) {
    goto Exit;
  }

  hr = context->QueryInterface(IID_ITfContextComposition,
                               reinterpret_cast<void**>(&context_composition));
  if (FAILED(hr)) {
    goto Exit;
  }

  hr = context_composition->StartComposition(
      edit_cookie, insertion_range, static_cast<ITfCompositionSink*>(this),
      &composition);
  if (FAILED(hr) || composition == nullptr) {
    goto Exit;
  }

  hr = composition->GetRange(&composition_range);
  if (FAILED(hr)) {
    goto Exit;
  }

  hr = composition_range->SetText(edit_cookie, 0, text.c_str(),
                                  static_cast<LONG>(text.size()));
  if (FAILED(hr)) {
    goto Exit;
  }

  hr = MoveSelectionToRangeEnd(edit_cookie, context, composition_range);
  if (FAILED(hr)) {
    goto Exit;
  }

  ClearCompositionTracking();
  composition_ = composition;
  composition_->AddRef();
  composition_context_ = context;
  composition_context_->AddRef();
  {
    const HRESULT attribute_hr =
        ApplyCompositionDisplayAttribute(edit_cookie, context, text);
#if defined(_DEBUG)
    if (FAILED(attribute_hr)) {
      debug::DebugLog(
          L"[MilkyWayIME][StartComposition][ApplyDisplayAttributeFailed] hr=0x" +
          FormatHex(static_cast<std::uint32_t>(attribute_hr)));
    }
#endif
  }
  hr = S_OK;

Exit:
#if defined(_DEBUG)
  const edit::TransitoryCompositionBridgeSnapshot bridge_snapshot_end =
      CaptureTransitoryBridgeSnapshot(context, session_.IsComposing(),
                                      composition_ != nullptr, text);
  debug::DebugLog(
      L"[MilkyWayIME][StartComposition][End] hr=0x" +
      FormatHex(static_cast<std::uint32_t>(hr)) + L" started=" +
      PointerToString(composition) + L" session_composing=" +
      std::to_wstring(session_.IsComposing() ? 1 : 0) + L" " +
      CompositionTrackingState(L"current", composition_, composition_context_) +
      L" " + transitory_composition_bridge_.DebugState() + L" " +
      TransitoryBridgeSnapshotDiagnostics(bridge_snapshot_end));
#endif
  SafeRelease(composition_range);
  SafeRelease(composition);
  SafeRelease(insertion_range);
  SafeRelease(context_composition);
  SafeRelease(insert_at_selection);
  return hr;
}

HRESULT TipTextService::UpdateComposition(TfEditCookie edit_cookie,
                                          const std::wstring& text) {
  if (composition_ == nullptr) {
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][UpdateComposition][NoTrackedComposition] text=\"" +
        text + L"\" session_composing=" +
        std::to_wstring(session_.IsComposing() ? 1 : 0));
#endif
    return E_UNEXPECTED;
  }

#if defined(_DEBUG)
  debug::DebugLog(
      L"[MilkyWayIME][UpdateComposition][Begin] text=\"" + text +
      L"\" session_composing=" +
      std::to_wstring(session_.IsComposing() ? 1 : 0) + L" " +
      CompositionTrackingState(L"current", composition_, composition_context_));
#endif

  ITfRange* composition_range = nullptr;
  HRESULT hr = composition_->GetRange(&composition_range);
  if (FAILED(hr)) {
#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][UpdateComposition][GetRangeFailed] hr=0x" +
                    FormatHex(static_cast<std::uint32_t>(hr)));
#endif
    return hr;
  }

  const wchar_t* composition_text = text.empty() ? nullptr : text.c_str();
  hr = composition_range->SetText(edit_cookie, 0, composition_text,
                                  static_cast<LONG>(text.size()));
  if (SUCCEEDED(hr)) {
    hr = MoveSelectionToRangeEnd(edit_cookie, composition_context_,
                                 composition_range);
  }
  if (SUCCEEDED(hr)) {
    const HRESULT attribute_hr = ApplyCompositionDisplayAttribute(
        edit_cookie, composition_context_, text);
#if defined(_DEBUG)
    if (FAILED(attribute_hr)) {
      debug::DebugLog(
          L"[MilkyWayIME][UpdateComposition][ApplyDisplayAttributeFailed] hr=0x" +
          FormatHex(static_cast<std::uint32_t>(attribute_hr)));
    }
#endif
  }

  composition_range->Release();
#if defined(_DEBUG)
  debug::DebugLog(
      L"[MilkyWayIME][UpdateComposition][End] hr=0x" +
      FormatHex(static_cast<std::uint32_t>(hr)) + L" session_composing=" +
      std::to_wstring(session_.IsComposing() ? 1 : 0) + L" " +
      CompositionTrackingState(L"current", composition_, composition_context_));
#endif
  return hr;
}

HRESULT TipTextService::CompleteComposition(TfEditCookie edit_cookie) {
  if (composition_ == nullptr) {
#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][CompleteComposition][NoTrackedComposition] "
                    L"session_composing=" +
                    std::to_wstring(session_.IsComposing() ? 1 : 0));
#endif
    return S_FALSE;
  }

#if defined(_DEBUG)
  debug::DebugLog(
      L"[MilkyWayIME][CompleteComposition][Begin] session_composing=" +
      std::to_wstring(session_.IsComposing() ? 1 : 0) + L" " +
      CompositionTrackingState(L"current", composition_, composition_context_));
#endif

  ITfComposition* composition = composition_;
  composition->AddRef();
  if (composition_context_ != nullptr) {
    ClearCompositionDisplayAttribute(edit_cookie, composition_context_);
  }
  ClearCompositionTracking();
  const HRESULT hr = composition->EndComposition(edit_cookie);
  composition->Release();
#if defined(_DEBUG)
  debug::DebugLog(
      L"[MilkyWayIME][CompleteComposition][End] hr=0x" +
      FormatHex(static_cast<std::uint32_t>(hr)) + L" session_composing=" +
      std::to_wstring(session_.IsComposing() ? 1 : 0) + L" " +
      CompositionTrackingState(L"current", composition_, composition_context_));
#endif
  return hr;
}

void TipTextService::ClearCompositionTracking() {
#if defined(_DEBUG)
  if (composition_ != nullptr || composition_context_ != nullptr) {
    debug::DebugLog(L"[MilkyWayIME][ClearCompositionTracking] " +
                    CompositionTrackingState(L"previous", composition_,
                                             composition_context_));
  }
#endif
  SafeRelease(composition_);
  SafeRelease(composition_context_);
}

HRESULT TipTextService::AdviseThreadMgrEventSink() {
  if (thread_mgr_ == nullptr) {
    return E_UNEXPECTED;
  }

  ITfSource* source = nullptr;
  const HRESULT hr = thread_mgr_->QueryInterface(IID_ITfSource,
                                                 reinterpret_cast<void**>(&source));
  if (FAILED(hr)) {
    return hr;
  }

  const HRESULT advise_hr = source->AdviseSink(
      IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this),
      &thread_mgr_event_sink_cookie_);
  source->Release();
  return advise_hr;
}

void TipTextService::UnadviseThreadMgrEventSink() {
  if (thread_mgr_ == nullptr || thread_mgr_event_sink_cookie_ == TF_INVALID_COOKIE) {
    return;
  }

  ITfSource* source = nullptr;
  if (SUCCEEDED(thread_mgr_->QueryInterface(IID_ITfSource,
                                            reinterpret_cast<void**>(&source)))) {
    source->UnadviseSink(thread_mgr_event_sink_cookie_);
    source->Release();
  }

  thread_mgr_event_sink_cookie_ = TF_INVALID_COOKIE;
}

HRESULT TipTextService::AdviseThreadFocusSink() {
  if (thread_mgr_ == nullptr) {
    return E_UNEXPECTED;
  }
  if (thread_focus_sink_cookie_ != TF_INVALID_COOKIE) {
    return S_OK;
  }

  ITfSource* source = nullptr;
  const HRESULT hr = thread_mgr_->QueryInterface(IID_ITfSource,
                                                 reinterpret_cast<void**>(&source));
  if (FAILED(hr)) {
    return hr;
  }

  const HRESULT advise_hr = source->AdviseSink(
      IID_ITfThreadFocusSink, static_cast<ITfThreadFocusSink*>(this),
      &thread_focus_sink_cookie_);
  source->Release();
  if (FAILED(advise_hr)) {
    thread_focus_sink_cookie_ = TF_INVALID_COOKIE;
  }
  return advise_hr;
}

void TipTextService::UnadviseThreadFocusSink() {
  if (thread_mgr_ == nullptr || thread_focus_sink_cookie_ == TF_INVALID_COOKIE) {
    return;
  }

  ITfSource* source = nullptr;
  if (SUCCEEDED(thread_mgr_->QueryInterface(IID_ITfSource,
                                            reinterpret_cast<void**>(&source)))) {
    source->UnadviseSink(thread_focus_sink_cookie_);
    source->Release();
  }

  thread_focus_sink_cookie_ = TF_INVALID_COOKIE;
}

HRESULT TipTextService::AdviseKeyEventSink() {
  ITfKeystrokeMgr* keystroke_mgr = nullptr;
  const HRESULT hr = thread_mgr_->QueryInterface(
      IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&keystroke_mgr));
  if (FAILED(hr)) {
    return hr;
  }

  const HRESULT advise_hr = keystroke_mgr->AdviseKeyEventSink(
      client_id_, static_cast<ITfKeyEventSink*>(this), TRUE);
  keystroke_mgr->Release();
  return advise_hr;
}

void TipTextService::UnadviseKeyEventSink() {
  if (thread_mgr_ == nullptr || client_id_ == TF_CLIENTID_NULL) {
    return;
  }

  ITfKeystrokeMgr* keystroke_mgr = nullptr;
  if (SUCCEEDED(thread_mgr_->QueryInterface(IID_ITfKeystrokeMgr,
                                            reinterpret_cast<void**>(&keystroke_mgr)))) {
    keystroke_mgr->UnadviseKeyEventSink(client_id_);
    keystroke_mgr->Release();
  }
}

HRESULT TipTextService::AdviseActiveLanguageProfileNotifySink() {
  if (thread_mgr_ == nullptr) {
    return E_UNEXPECTED;
  }
  if (active_language_profile_notify_sink_cookie_ != TF_INVALID_COOKIE) {
    return S_OK;
  }

  ITfSource* source = nullptr;
  const HRESULT hr = thread_mgr_->QueryInterface(
      IID_ITfSource, reinterpret_cast<void**>(&source));
  if (FAILED(hr)) {
    return hr;
  }

  const HRESULT advise_hr = source->AdviseSink(
      IID_ITfActiveLanguageProfileNotifySink,
      static_cast<ITfActiveLanguageProfileNotifySink*>(this),
      &active_language_profile_notify_sink_cookie_);
  source->Release();
  if (FAILED(advise_hr)) {
    active_language_profile_notify_sink_cookie_ = TF_INVALID_COOKIE;
  }
  return advise_hr;
}

void TipTextService::UnadviseActiveLanguageProfileNotifySink() {
  if (thread_mgr_ == nullptr ||
      active_language_profile_notify_sink_cookie_ == TF_INVALID_COOKIE) {
    return;
  }

  ITfSource* source = nullptr;
  if (SUCCEEDED(thread_mgr_->QueryInterface(IID_ITfSource,
                                            reinterpret_cast<void**>(&source)))) {
    source->UnadviseSink(active_language_profile_notify_sink_cookie_);
    source->Release();
  }

  active_language_profile_notify_sink_cookie_ = TF_INVALID_COOKIE;
}

HRESULT TipTextService::AdviseKeyboardOpenCloseCompartmentSink() {
  if (thread_mgr_ == nullptr) {
    return E_UNEXPECTED;
  }
  if (keyboard_openclose_compartment_sink_cookie_ != TF_INVALID_COOKIE) {
    return S_OK;
  }

  ITfCompartmentMgr* compartment_manager = nullptr;
  HRESULT hr = thread_mgr_->GetGlobalCompartment(&compartment_manager);
  if (FAILED(hr)) {
    return hr;
  }

  ITfCompartment* compartment = nullptr;
  hr = compartment_manager->GetCompartment(
      GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &compartment);
  compartment_manager->Release();
  if (FAILED(hr)) {
    return hr;
  }

  ITfSource* source = nullptr;
  hr = compartment->QueryInterface(IID_ITfSource,
                                   reinterpret_cast<void**>(&source));
  if (FAILED(hr)) {
    compartment->Release();
    return hr;
  }

  const HRESULT advise_hr = source->AdviseSink(
      IID_ITfCompartmentEventSink,
      static_cast<ITfCompartmentEventSink*>(this),
      &keyboard_openclose_compartment_sink_cookie_);
  source->Release();
  compartment->Release();
  if (FAILED(advise_hr)) {
    keyboard_openclose_compartment_sink_cookie_ = TF_INVALID_COOKIE;
  }
  return advise_hr;
}

void TipTextService::UnadviseKeyboardOpenCloseCompartmentSink() {
  if (thread_mgr_ == nullptr ||
      keyboard_openclose_compartment_sink_cookie_ == TF_INVALID_COOKIE) {
    return;
  }

  ITfCompartmentMgr* compartment_manager = nullptr;
  if (SUCCEEDED(thread_mgr_->GetGlobalCompartment(&compartment_manager))) {
    ITfCompartment* compartment = nullptr;
    if (SUCCEEDED(compartment_manager->GetCompartment(
            GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &compartment))) {
      ITfSource* source = nullptr;
      if (SUCCEEDED(compartment->QueryInterface(
              IID_ITfSource, reinterpret_cast<void**>(&source)))) {
        source->UnadviseSink(keyboard_openclose_compartment_sink_cookie_);
        source->Release();
      }
      compartment->Release();
    }
    compartment_manager->Release();
  }

  keyboard_openclose_compartment_sink_cookie_ = TF_INVALID_COOKIE;
}

HRESULT TipTextService::RegisterPreservedKeys() {
  if (thread_mgr_ == nullptr || client_id_ == TF_CLIENTID_NULL) {
    return E_UNEXPECTED;
  }
  if (preserved_keys_registered_) {
    return S_OK;
  }

  ITfKeystrokeMgr* keystroke_mgr = nullptr;
  const HRESULT hr = thread_mgr_->QueryInterface(
      IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&keystroke_mgr));
  if (FAILED(hr)) {
    return hr;
  }

  TF_PRESERVEDKEY preserved_key = {};
  preserved_key.uVKey = VK_HANGUL;
  preserved_key.uModifiers = 0;

  const HRESULT preserve_hr = keystroke_mgr->PreserveKey(
      client_id_, kImeModePreservedKeyGuid, &preserved_key,
      kImeModePreservedKeyDescription,
      static_cast<ULONG>(std::size(kImeModePreservedKeyDescription) - 1));
  keystroke_mgr->Release();
  if (SUCCEEDED(preserve_hr)) {
    preserved_keys_registered_ = true;
  }

#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][RegisterPreservedKeys] hr=0x" +
                  FormatHex(static_cast<std::uint32_t>(preserve_hr)) +
                  L" client_id=" + std::to_wstring(client_id_) +
                  L" registered=" +
                  std::to_wstring(preserved_keys_registered_ ? 1 : 0));
#endif

  return preserve_hr;
}

void TipTextService::UnregisterPreservedKeys() {
  if (thread_mgr_ == nullptr || client_id_ == TF_CLIENTID_NULL ||
      !preserved_keys_registered_) {
    return;
  }

  ITfKeystrokeMgr* keystroke_mgr = nullptr;
  if (SUCCEEDED(thread_mgr_->QueryInterface(
          IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&keystroke_mgr)))) {
    TF_PRESERVEDKEY preserved_key = {};
    preserved_key.uVKey = VK_HANGUL;
    preserved_key.uModifiers = 0;
    keystroke_mgr->UnpreserveKey(kImeModePreservedKeyGuid, &preserved_key);
    keystroke_mgr->Release();
  }

  preserved_keys_registered_ = false;
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][UnregisterPreservedKeys]");
#endif
}

HRESULT TipTextService::InitDisplayAttributeGuidAtom() {
  return display::RegisterComposingLastSyllableDisplayAttributeAtom(
      &composing_display_attribute_atom_);
}

HRESULT TipTextService::RefreshFocusedContext(ITfDocumentMgr* document_mgr) {
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][RefreshFocusedContext][Begin] requested_docmgr=" +
                  PointerToString(document_mgr) + L" thread_mgr=" +
                  PointerToString(thread_mgr_) + L" process=" +
                  CurrentProcessName() + L" " + ForegroundWindowDiagnostics());
#endif
  ITfDocumentMgr* focused_document_mgr = document_mgr;
  bool release_document_mgr = false;

  if (focused_document_mgr == nullptr) {
    if (thread_mgr_ == nullptr) {
      return S_OK;
    }

    const HRESULT hr = thread_mgr_->GetFocus(&focused_document_mgr);
    if (FAILED(hr)) {
#if defined(_DEBUG)
      debug::DebugLog(L"[MilkyWayIME][RefreshFocusedContext][GetFocusFailed] hr=0x" +
                      FormatHex(static_cast<std::uint32_t>(hr)));
#endif
      DetachTextEditSink();
      return hr;
    }
    release_document_mgr = true;
  }

  if (focused_document_mgr == nullptr) {
#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][RefreshFocusedContext][NoFocusedDocument] " +
                    ForegroundWindowDiagnostics());
#endif
    DetachTextEditSink();
    return S_OK;
  }

  ITfContext* context = nullptr;
  const HRESULT hr = focused_document_mgr->GetTop(&context);
  if (release_document_mgr) {
    focused_document_mgr->Release();
  }
  if (FAILED(hr)) {
#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][RefreshFocusedContext][GetTopFailed] docmgr=" +
                    PointerToString(focused_document_mgr) + L" hr=0x" +
                    FormatHex(static_cast<std::uint32_t>(hr)) + L" " +
                    ForegroundWindowDiagnostics());
#endif
    DetachTextEditSink();
    return hr;
  }

  const HRESULT attach_hr = AttachTextEditSink(context);
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][RefreshFocusedContext][End] focused_docmgr=" +
                  PointerToString(focused_document_mgr) + L" context=" +
                  PointerToString(context) + L" attach_hr=0x" +
                  FormatHex(static_cast<std::uint32_t>(attach_hr)) + L" " +
                  ContextDiagnostics(context));
#endif
  context->Release();
  return attach_hr;
}

HRESULT TipTextService::AttachTextEditSink(ITfContext* context) {
  if (context == text_edit_sink_context_) {
#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][AttachTextEditSink][Reuse] context=" +
                    PointerToString(context));
#endif
    return S_OK;
  }

  DetachTextEditSink();
  if (context == nullptr) {
#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][AttachTextEditSink][NullContext]");
#endif
    return S_OK;
  }

  ITfSource* source = nullptr;
  const HRESULT hr = context->QueryInterface(IID_ITfSource,
                                             reinterpret_cast<void**>(&source));
  if (FAILED(hr)) {
    return hr;
  }

  const HRESULT advise_hr = source->AdviseSink(
      IID_ITfTextEditSink, static_cast<ITfTextEditSink*>(this),
      &text_edit_sink_cookie_);
  source->Release();
  if (FAILED(advise_hr)) {
#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][AttachTextEditSink][AdviseFailed] context=" +
                    PointerToString(context) + L" hr=0x" +
                    FormatHex(static_cast<std::uint32_t>(advise_hr)));
#endif
    return advise_hr;
  }

  text_edit_sink_context_ = context;
  text_edit_sink_context_->AddRef();
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][AttachTextEditSink][Attached] context=" +
                  PointerToString(text_edit_sink_context_) + L" cookie=" +
                  std::to_wstring(text_edit_sink_cookie_));
#endif
  return S_OK;
}

void TipTextService::DetachTextEditSink() {
  ResetTransitoryDirectTextComposition(L"DetachTextEditSink");
  ResetNikkeDirectTextComposition(L"DetachTextEditSink");
  ResetTransitoryCompositionBridge(L"DetachTextEditSink");
  ITfContext* previous_context = text_edit_sink_context_;
  const DWORD previous_cookie = text_edit_sink_cookie_;
  if (text_edit_sink_context_ != nullptr &&
      text_edit_sink_cookie_ != TF_INVALID_COOKIE) {
    ITfSource* source = nullptr;
    if (SUCCEEDED(text_edit_sink_context_->QueryInterface(
            IID_ITfSource, reinterpret_cast<void**>(&source)))) {
      source->UnadviseSink(text_edit_sink_cookie_);
      source->Release();
    }
  }

  text_edit_sink_cookie_ = TF_INVALID_COOKIE;
  SafeRelease(text_edit_sink_context_);
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][DetachTextEditSink] previous_context=" +
                  PointerToString(previous_context) + L" previous_cookie=" +
                  std::to_wstring(previous_cookie));
#endif
}

HRESULT TipTextService::AddInputModeLanguageBarItem() {
  if (thread_mgr_ == nullptr) {
    return E_UNEXPECTED;
  }
  if (input_mode_lang_bar_item_ != nullptr) {
    return S_OK;
  }

  input_mode_lang_bar_item_ = new (std::nothrow)
      ::milkyway::tsf::langbar::InputModeLangBarItem(this, ime_open_);
  if (input_mode_lang_bar_item_ == nullptr) {
    return E_OUTOFMEMORY;
  }

  const HRESULT hr = input_mode_lang_bar_item_->AddToLanguageBar(thread_mgr_);
  if (FAILED(hr)) {
    input_mode_lang_bar_item_->Release();
    input_mode_lang_bar_item_ = nullptr;
    return hr;
  }

  return S_OK;
}

void TipTextService::RemoveInputModeLanguageBarItem() {
  if (input_mode_lang_bar_item_ == nullptr) {
    return;
  }

  if (thread_mgr_ != nullptr) {
    input_mode_lang_bar_item_->RemoveFromLanguageBar(thread_mgr_);
  }

  SafeRelease(input_mode_lang_bar_item_);
}

HRESULT TipTextService::FlushPendingOperations(ITfContext* context,
                                               EditSessionRequestPolicy policy,
                                               const wchar_t* origin) {
  const DWORD request_flags = RequestFlagsForPolicy(policy);
  HRESULT hr = edit_sink_.Flush(context, request_flags);
  if (FAILED(hr) && ShouldRetryAsync(hr) &&
      policy == EditSessionRequestPolicy::kSyncPreferredWrite) {
    hr = edit_sink_.Flush(context, RequestFlagsForPolicy(policy, true));
  }

  if (FAILED(hr)) {
#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][TipTextService::FlushPendingOperations][Failed] "
                    L"origin=" + std::wstring(origin) + L" policy=" +
                    EditSessionRequestPolicyName(policy) +
                    L" request_flags=0x" +
                    FormatHex(static_cast<std::uint32_t>(request_flags)) +
                    L" pending_operations=" +
                    std::to_wstring(edit_sink_.PendingOperationCount()) +
                    L" hr=0x" +
                    FormatHex(static_cast<std::uint32_t>(hr)));
#endif
    ResetTransitoryDirectTextComposition(L"FlushPendingOperationsFailed");
    ResetNikkeDirectTextComposition(L"FlushPendingOperationsFailed");
    ResetTransitoryCompositionBridge(L"FlushPendingOperationsFailed");
    edit_sink_.ClearPendingOperations();
  }
  return hr;
}

engine::state::ModifierState TipTextService::QueryModifierState() const {
  engine::state::ModifierState modifiers;
  modifiers.ctrl = KeyPressed(VK_CONTROL);
  modifiers.alt = KeyPressed(VK_MENU);
  modifiers.shift = KeyPressed(VK_SHIFT);
  modifiers.win = KeyPressed(VK_LWIN) || KeyPressed(VK_RWIN);
  return modifiers;
}

engine::key::PhysicalKey TipTextService::BuildPhysicalKey(WPARAM wparam,
                                                          LPARAM lparam) const {
  engine::key::PhysicalKey key;
  key.virtual_key = static_cast<std::uint16_t>(wparam);
  key.scan_code = static_cast<std::uint16_t>((lparam >> 16) & 0xFF);
  key.extended = (lparam & (1 << 24)) != 0;
  return key;
}

engine::key::NormalizedKeyEvent TipTextService::BuildNormalizedKeyEvent(
    WPARAM wparam, LPARAM lparam,
    const engine::state::ModifierState& modifiers,
    engine::key::KeyTransition transition) const {
  return layout_registry_.NormalizeKeyEvent(session_.base_layout_id(),
                                            BuildPhysicalKey(wparam, lparam),
                                            modifiers, transition);
}

bool TipTextService::SelectionInsideComposition(ITfContext* context,
                                                TfEditCookie read_cookie) const {
  if (context == nullptr || composition_ == nullptr ||
      context != composition_context_) {
    return true;
  }

  TF_SELECTION selection = {};
  ULONG fetched = 0;
  if (FAILED(context->GetSelection(read_cookie, 0, 1, &selection, &fetched)) ||
      fetched == 0 || selection.range == nullptr) {
    return true;
  }

  ITfRange* composition_range = nullptr;
  const HRESULT range_hr = composition_->GetRange(&composition_range);
  if (FAILED(range_hr) || composition_range == nullptr) {
    selection.range->Release();
    return true;
  }

  LONG start_compare = 0;
  LONG end_compare = 0;
  const HRESULT start_hr = selection.range->CompareStart(
      read_cookie, composition_range, TF_ANCHOR_START, &start_compare);
  const HRESULT end_hr = selection.range->CompareEnd(
      read_cookie, composition_range, TF_ANCHOR_END, &end_compare);

  composition_range->Release();
  selection.range->Release();

  if (FAILED(start_hr) || FAILED(end_hr)) {
    return true;
  }

  return start_compare >= 0 && end_compare <= 0;
}

HRESULT TipTextService::SyncImeOpenFromCompartment(const wchar_t* origin) {
  if (thread_mgr_ == nullptr) {
    return E_UNEXPECTED;
  }

  ITfCompartmentMgr* compartment_manager = nullptr;
  HRESULT hr = thread_mgr_->GetGlobalCompartment(&compartment_manager);
  if (FAILED(hr)) {
    return hr;
  }

  ITfCompartment* compartment = nullptr;
  hr = compartment_manager->GetCompartment(
      GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &compartment);
  compartment_manager->Release();
  if (FAILED(hr)) {
    return hr;
  }

  VARIANT value;
  VariantInit(&value);
  hr = compartment->GetValue(&value);
  compartment->Release();
  if (FAILED(hr)) {
    return hr;
  }

  bool open = ime_open_;
  bool has_open_value = true;
  if (value.vt == VT_I4) {
    open = value.lVal != 0;
  } else if (value.vt == VT_BOOL) {
    open = value.boolVal != VARIANT_FALSE;
  } else {
    has_open_value = false;
  }

#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][SyncImeOpenFromCompartment] origin=" +
                  std::wstring(origin != nullptr ? origin : L"<unknown>") +
                  L" vt=" + std::to_wstring(value.vt) + L" has_value=" +
                  std::to_wstring(has_open_value ? 1 : 0) + L" previous=" +
                  std::to_wstring(ime_open_ ? 1 : 0) + L" next=" +
                  std::to_wstring(open ? 1 : 0));
#endif

  VariantClear(&value);
  if (!has_open_value) {
    return S_FALSE;
  }

  ime_open_ = open;
  if (input_mode_lang_bar_item_ != nullptr) {
    input_mode_lang_bar_item_->OnImeOpenChanged(open);
  }
  if (!open) {
    ResetTransitoryDirectTextComposition(L"SyncImeOpenFromCompartment(Closed)");
    ResetTransitoryCompositionBridge(L"SyncImeOpenFromCompartment(Closed)");
  }
  return S_OK;
}

void TipTextService::SetImeOpen(bool open) {
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][SetImeOpen][Begin] open=" +
                  std::to_wstring(open ? 1 : 0) + L" previous=" +
                  std::to_wstring(ime_open_ ? 1 : 0) + L" process=" +
                  CurrentProcessName() + L" client_id=" +
                  std::to_wstring(client_id_) + L" thread_mgr=" +
                  PointerToString(thread_mgr_));
#endif
  ime_open_ = open;

  if (thread_mgr_ == nullptr || client_id_ == TF_CLIENTID_NULL) {
    if (input_mode_lang_bar_item_ != nullptr) {
      input_mode_lang_bar_item_->OnImeOpenChanged(open);
    }
    return;
  }

  ITfCompartmentMgr* compartment_manager = nullptr;
  if (FAILED(thread_mgr_->GetGlobalCompartment(&compartment_manager))) {
#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][SetImeOpen][GetGlobalCompartmentFailed]");
#endif
    if (input_mode_lang_bar_item_ != nullptr) {
      input_mode_lang_bar_item_->OnImeOpenChanged(open);
    }
    return;
  }

  ITfCompartment* compartment = nullptr;
  if (SUCCEEDED(compartment_manager->GetCompartment(
          GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &compartment))) {
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_I4;
    value.lVal = open ? 1 : 0;
    const HRESULT set_value_hr = compartment->SetValue(client_id_, &value);
#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][SetImeOpen][SetValue] hr=0x" +
                    FormatHex(static_cast<std::uint32_t>(set_value_hr)) +
                    L" compartment=" + PointerToString(compartment));
#endif
    compartment->Release();
  } else {
#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][SetImeOpen][GetCompartmentFailed]");
#endif
  }

  compartment_manager->Release();
  if (input_mode_lang_bar_item_ != nullptr) {
    input_mode_lang_bar_item_->OnImeOpenChanged(open);
  }
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][SetImeOpen][End] ime_open=" +
                  std::to_wstring(ime_open_ ? 1 : 0));
#endif
}

const engine::layout::LayoutRegistry& TipTextService::layout_registry() const {
  return layout_registry_;
}

const engine::layout::BaseLayoutId&
TipTextService::current_base_layout_id() const {
  return session_.base_layout_id();
}

const engine::layout::KoreanLayoutId&
TipTextService::current_korean_layout_id() const {
  return session_.korean_layout_id();
}

void TipTextService::SelectBaseLayoutFromLanguageBar(
    const engine::layout::BaseLayoutId& base_layout_id) {
  if (layout_registry_.FindBaseLayout(base_layout_id) == nullptr) {
    return;
  }

  ApplyLayoutSelection(base_layout_id, session_.korean_layout_id(),
                       L"LanguageBarBaseLayout");
}

void TipTextService::SelectKoreanLayoutFromLanguageBar(
    const engine::layout::KoreanLayoutId& korean_layout_id) {
  if (layout_registry_.FindKoreanLayout(korean_layout_id) == nullptr) {
    return;
  }

  ApplyLayoutSelection(session_.base_layout_id(), korean_layout_id,
                       L"LanguageBarKoreanLayout");
}

void TipTextService::SyncLayoutSelectionFromSettings(const wchar_t* origin) {
  const settings::SettingsStore settings_store;
  const settings::UserSettings user_settings =
      settings::ResolveUserSettings(settings_store.Load(), layout_registry_);
  if (user_settings.base_layout_id == session_.base_layout_id() &&
      user_settings.korean_layout_id == session_.korean_layout_id()) {
    return;
  }

#if defined(_DEBUG)
  debug::DebugLog(
      L"[MilkyWayIME][SyncLayoutSelectionFromSettings] origin=" +
      std::wstring(origin != nullptr ? origin : L"<unknown>") + L" previous=" +
      Utf8ToWide(session_.base_layout_id()) + L"/" +
      Utf8ToWide(session_.korean_layout_id()) + L" next=" +
      Utf8ToWide(user_settings.base_layout_id) + L"/" +
      Utf8ToWide(user_settings.korean_layout_id));
#endif
  ApplyLayoutSelection(user_settings.base_layout_id,
                       user_settings.korean_layout_id, origin, false);
}

void TipTextService::ApplyLayoutSelection(
    engine::layout::BaseLayoutId base_layout_id,
    engine::layout::KoreanLayoutId korean_layout_id,
    const wchar_t* origin,
    bool persist_settings) {
  if (base_layout_id == session_.base_layout_id() &&
      korean_layout_id == session_.korean_layout_id()) {
    return;
  }

  std::unique_ptr<adapters::libhangul::HangulComposer> replacement_composer;
  if (korean_layout_id != session_.korean_layout_id()) {
    replacement_composer =
        CreateComposerForKoreanLayout(layout_registry_, korean_layout_id);
    if (replacement_composer == nullptr) {
      return;
    }
  }

  ClearPendingKeyResult();
  ResetTransitoryDirectTextComposition(L"ApplyLayoutSelection");
  ResetTransitoryCompositionBridge(L"ApplyLayoutSelection");
  CloseCandidateList();
  if (logic_.PrepareLayoutChange()) {
    if (text_edit_sink_context_ != nullptr) {
      const HRESULT flush_hr =
          FlushPendingOperations(text_edit_sink_context_,
                                 EditSessionRequestPolicy::kSyncPreferredWrite,
                                 origin);
      if (FAILED(flush_hr)) {
        SyncCompositionTermination();
      }
    } else {
      SyncCompositionTermination();
    }
  } else {
    edit_sink_.ClearPendingOperations();
  }

  session_.SetLayouts(std::move(base_layout_id), std::move(korean_layout_id));
  if (replacement_composer != nullptr) {
    logic_.ReplaceComposer(std::move(replacement_composer));
  }
  if (!persist_settings) {
    return;
  }

  const settings::SettingsStore settings_store;
  const HRESULT save_hr = settings_store.Save(settings::UserSettings{
      session_.base_layout_id(), session_.korean_layout_id()});
#if defined(_DEBUG)
  if (FAILED(save_hr)) {
    debug::DebugLog(L"[MilkyWayIME][ApplyLayoutSelection][SaveFailed] hr=0x" +
                    FormatHex(static_cast<std::uint32_t>(save_hr)));
  } else {
    debug::DebugLog(L"[MilkyWayIME][ApplyLayoutSelection][Saved] base=" +
                    Utf8ToWide(session_.base_layout_id()) + L" korean=" +
                    Utf8ToWide(session_.korean_layout_id()));
  }
#endif
}

bool TipTextService::FinalizeImeModeToggle(ITfContext* context,
                                           const wchar_t* origin) {
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][FinalizeImeModeToggle][Begin] origin=" +
                  std::wstring(origin) + L" context=" +
                  PointerToString(context) + L" pending_operations=" +
                  std::to_wstring(edit_sink_.PendingOperationCount()) +
                  L" ime_open_before=" +
                  std::to_wstring(ime_open_ ? 1 : 0));
#endif
  if (edit_sink_.HasPendingOperations()) {
    if (context == nullptr) {
#if defined(_DEBUG)
      debug::DebugLog(L"[MilkyWayIME][FinalizeImeModeToggle][NoContext]");
#endif
      return false;
    }

    const HRESULT hr =
        FlushPendingOperations(context, EditSessionRequestPolicy::kKeyPathWrite,
                               origin);
    if (FAILED(hr)) {
#if defined(_DEBUG)
      debug::DebugLog(L"[MilkyWayIME][FinalizeImeModeToggle][FlushFailed] hr=0x" +
                      FormatHex(static_cast<std::uint32_t>(hr)));
#endif
      return false;
    }
  }

  SetImeOpen(!ime_open_);
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][FinalizeImeModeToggle][End] ime_open_after=" +
                  std::to_wstring(ime_open_ ? 1 : 0));
#endif
  return true;
}

void TipTextService::ToggleImeModeFromLanguageBar() {
  logic_.PrepareImeModeToggle();
  ResetTransitoryDirectTextComposition(L"LangBarClick");
  ResetTransitoryCompositionBridge(L"LangBarClick");
  if (!FinalizeImeModeToggle(text_edit_sink_context_, L"LangBarClick")) {
    SyncCompositionTermination();
  }
}

void TipTextService::HandleShortcutAction(
    ITfContext* context, engine::shortcut::ShortcutAction action) {
  (void)context;
  (void)action;
}

bool TipTextService::HandleHanjaKey(ITfContext* context) {
  CloseCandidateList();
  if (context == nullptr) {
    return true;
  }

  if (session_.IsComposing()) {
    return HandleCompositionHanjaKey(context);
  }

  return HandleSelectionHanjaKey(context);
}

bool TipTextService::HandleCompositionHanjaKey(ITfContext* context) {
  if (context == nullptr || !session_.IsComposing()) {
    return true;
  }

  const std::optional<engine::hanja::CandidateRequest> request =
      session_.RequestHanjaConversion();
  if (!request.has_value()) {
    return true;
  }

  const std::vector<engine::hanja::Candidate> candidates =
      hanja_dictionary_.Lookup(*request);
  if (candidates.empty()) {
    return true;
  }

  SetCompositionCandidateCommitTarget();
  const bool opened = OpenCandidateList(context, candidates);
  if (!opened) {
    ClearCandidateCommitTarget();
  }
  return opened;
}

bool TipTextService::HandleSelectionHanjaKey(ITfContext* context) {
  if (context == nullptr) {
    return true;
  }

  std::wstring selection_text;
  ITfRange* selection_range = nullptr;
  bool selection_empty = false;
  if (!ReadSelectionText(context, &selection_text, &selection_range,
                         &selection_empty)) {
    SafeRelease(selection_range);
    if (selection_empty) {
      return HandleCaretHanjaKey(context);
    }
    return true;
  }

  const std::string selected_utf8 = WideToUtf8Text(selection_text);
  if (selected_utf8.empty()) {
    SafeRelease(selection_range);
    return true;
  }

  std::vector<engine::hanja::SelectionHanjaPrefixRequest> requests =
      engine::hanja::CreateSelectionHanjaPrefixRequests(selected_utf8);
  if (requests.empty()) {
    requests =
        engine::hanja::CreateSelectionHanjaReversePrefixRequests(selected_utf8);
  }
  for (const engine::hanja::SelectionHanjaPrefixRequest& request : requests) {
    const std::vector<engine::hanja::Candidate> candidates =
        hanja_dictionary_.Lookup(request.request);
    if (candidates.empty()) {
      continue;
    }

    SetSelectionCandidateCommitTarget(
        context, selection_range,
        static_cast<std::uint32_t>(request.matched_utf16_length));
    const bool opened = OpenCandidateList(context, candidates);
    SafeRelease(selection_range);
    if (!opened) {
      ClearCandidateCommitTarget();
    }
    return opened;
  }

  SafeRelease(selection_range);
  return true;
}

bool TipTextService::HandleCaretHanjaKey(ITfContext* context) {
  if (context == nullptr) {
    return true;
  }

  std::wstring text_before_caret;
  ITfRange* caret_range = nullptr;
  if (!ReadCaretTextBeforeSelection(context, &text_before_caret,
                                    &caret_range)) {
    SafeRelease(caret_range);
    return true;
  }

  const std::string text_before_caret_utf8 = WideToUtf8Text(text_before_caret);
  const std::optional<engine::hanja::CaretHanjaRun> run =
      engine::hanja::CreateCaretHanjaRun(text_before_caret_utf8);
  if (!run.has_value() || run->utf16_length == 0 || run->text.empty()) {
    SafeRelease(caret_range);
    return true;
  }

  std::vector<CaretCandidateSegment> segments;
  std::size_t byte_offset = 0;
  std::uint32_t utf16_offset = 0;
  while (byte_offset < run->text.size()) {
    const std::string_view remaining(run->text.data() + byte_offset,
                                     run->text.size() - byte_offset);
    std::vector<engine::hanja::SelectionHanjaPrefixRequest> requests =
        run->kind == engine::hanja::CandidateKind::kHanjaReverse
            ? engine::hanja::CreateSelectionHanjaReversePrefixRequests(
                  remaining)
            : engine::hanja::CreateSelectionHanjaPrefixRequests(remaining);

    bool resolved = false;
    for (const engine::hanja::SelectionHanjaPrefixRequest& request : requests) {
      std::vector<engine::hanja::Candidate> candidates =
          hanja_dictionary_.Lookup(request.request);
      if (candidates.empty()) {
        continue;
      }

      ITfRange* target_range = nullptr;
      if (!CreateCaretCandidateRange(
              context, caret_range, static_cast<std::uint32_t>(run->utf16_length),
              utf16_offset,
              static_cast<std::uint32_t>(request.matched_utf16_length),
              &target_range)) {
        continue;
      }

      segments.push_back(CaretCandidateSegment{
          target_range,
          std::move(candidates),
      });
      byte_offset += request.matched_byte_length;
      utf16_offset +=
          static_cast<std::uint32_t>(request.matched_utf16_length);
      resolved = true;
      break;
    }

    if (!resolved) {
      break;
    }
  }

  SafeRelease(caret_range);
  if (segments.empty()) {
    return true;
  }

  SetCaretCandidateSegments(context, std::move(segments));
  if (!OpenCaretSegmentCandidateList(context, 0)) {
    ClearCaretCandidateSegments();
    ClearCandidateCommitTarget();
    return true;
  }
  return true;
}

bool TipTextService::AdvanceHanjaCandidateSegment(ITfContext* context) {
  ITfContext* target_context =
      context != nullptr ? context : caret_candidate_segments_context_;
  if (target_context == nullptr ||
      caret_candidate_segments_context_ != target_context ||
      caret_candidate_segments_.size() < 2) {
    return false;
  }

  const std::size_t next_segment =
      (active_caret_candidate_segment_ + 1) % caret_candidate_segments_.size();
  CloseCandidateListUi();
  ClearCandidateCommitTarget();
  if (!OpenCaretSegmentCandidateList(target_context, next_segment)) {
    CloseCandidateList();
    return false;
  }
  return true;
}

bool TipTextService::OpenCandidateList(
    ITfContext* context,
    const std::vector<engine::hanja::Candidate>& candidates) {
  if (thread_mgr_ == nullptr || candidates.empty()) {
    return false;
  }

  ITfDocumentMgr* document_mgr = nullptr;
  if (context != nullptr) {
    context->GetDocumentMgr(&document_mgr);
  }

  std::vector<candidate::CandidateUiItem> items;
  items.reserve(candidates.size());
  for (const engine::hanja::Candidate& candidate : candidates) {
    const std::wstring display_text = Utf8ToWideText(candidate.value);
    if (display_text.empty()) {
      continue;
    }
    items.push_back(candidate::CandidateUiItem{
        candidate.value,
        display_text,
    });
  }

  if (items.empty()) {
    SafeRelease(document_mgr);
    return false;
  }

  candidate_list_ = new (std::nothrow) candidate::CandidateListUi(
      thread_mgr_, document_mgr, ModuleInstance(), this, std::move(items));
  SafeRelease(document_mgr);
  if (candidate_list_ == nullptr) {
    return false;
  }

  if (!candidate_list_->Begin()) {
    CloseCandidateList();
    return false;
  }

  const std::optional<POINT> anchor = CandidateWindowAnchor(context);
  if (!anchor.has_value() || !candidate_list_->ShowAt(*anchor)) {
    CloseCandidateList();
    return false;
  }

  return true;
}

bool TipTextService::OpenCaretSegmentCandidateList(ITfContext* context,
                                                   std::size_t segment_index) {
  if (context == nullptr || segment_index >= caret_candidate_segments_.size()) {
    return false;
  }

  CaretCandidateSegment& segment = caret_candidate_segments_[segment_index];
  SetCaretCandidateCommitTarget(context, segment.range);
  const bool opened = OpenCandidateList(context, segment.candidates);
  if (!opened) {
    ClearCandidateCommitTarget();
    return false;
  }

  active_caret_candidate_segment_ = segment_index;
  return true;
}

void TipTextService::CloseCandidateList() {
  CloseCandidateListUi();
  ClearCandidateCommitTarget();
  ClearCaretCandidateSegments();
}

void TipTextService::CloseCandidateListUi() {
  if (candidate_list_ != nullptr) {
    candidate_list_->End();
    candidate_list_->Release();
    candidate_list_ = nullptr;
  }
}

void TipTextService::SetCompositionCandidateCommitTarget() {
  ClearCandidateCommitTarget();
  candidate_commit_target_kind_ = CandidateCommitTargetKind::kComposition;
}

void TipTextService::SetSelectionCandidateCommitTarget(
    ITfContext* context, ITfRange* range,
    std::uint32_t matched_utf16_length) {
  ClearCandidateCommitTarget();
  if (context == nullptr || range == nullptr || matched_utf16_length == 0) {
    return;
  }

  selection_candidate_target_.context = context;
  selection_candidate_target_.context->AddRef();
  selection_candidate_target_.range = range;
  selection_candidate_target_.range->AddRef();
  selection_candidate_target_.matched_utf16_length = matched_utf16_length;
  candidate_commit_target_kind_ = CandidateCommitTargetKind::kSelectionRange;
}

void TipTextService::SetCaretCandidateCommitTarget(ITfContext* context,
                                                   ITfRange* range) {
  ClearCandidateCommitTarget();
  if (context == nullptr || range == nullptr) {
    return;
  }

  caret_candidate_target_.context = context;
  caret_candidate_target_.context->AddRef();
  caret_candidate_target_.range = range;
  caret_candidate_target_.range->AddRef();
  candidate_commit_target_kind_ = CandidateCommitTargetKind::kCaretRange;
}

void TipTextService::ClearCandidateCommitTarget() {
  candidate_commit_target_kind_ = CandidateCommitTargetKind::kNone;
  SafeRelease(selection_candidate_target_.range);
  SafeRelease(selection_candidate_target_.context);
  selection_candidate_target_.matched_utf16_length = 0;
  SafeRelease(caret_candidate_target_.range);
  SafeRelease(caret_candidate_target_.context);
}

void TipTextService::SetCaretCandidateSegments(
    ITfContext* context, std::vector<CaretCandidateSegment> segments) {
  ClearCaretCandidateSegments();
  if (context == nullptr || segments.empty()) {
    return;
  }

  caret_candidate_segments_context_ = context;
  caret_candidate_segments_context_->AddRef();
  caret_candidate_segments_ = std::move(segments);
  active_caret_candidate_segment_ = 0;
}

void TipTextService::ClearCaretCandidateSegments() {
  for (CaretCandidateSegment& segment : caret_candidate_segments_) {
    SafeRelease(segment.range);
  }
  caret_candidate_segments_.clear();
  SafeRelease(caret_candidate_segments_context_);
  active_caret_candidate_segment_ = 0;
}

bool TipTextService::ReadSelectionText(ITfContext* context, std::wstring* text,
                                       ITfRange** range, bool* is_empty) const {
  if (context == nullptr || text == nullptr || range == nullptr ||
      is_empty == nullptr || client_id_ == TF_CLIENTID_NULL) {
    return false;
  }

  text->clear();
  *range = nullptr;
  *is_empty = false;

  SelectionTextSnapshot snapshot;
  SelectionTextReadEditSession* edit_session =
      new (std::nothrow) SelectionTextReadEditSession(context, &snapshot);
  if (edit_session == nullptr) {
    return false;
  }

  HRESULT edit_session_result = E_FAIL;
  const HRESULT hr = context->RequestEditSession(
      client_id_, edit_session, TF_ES_SYNC | TF_ES_READ, &edit_session_result);
  edit_session->Release();
  if (FAILED(hr) || FAILED(edit_session_result) || snapshot.text.empty() ||
      snapshot.range == nullptr) {
    *is_empty = snapshot.has_selection && snapshot.is_empty;
    return false;
  }

  *text = std::move(snapshot.text);
  *range = snapshot.range;
  snapshot.range = nullptr;
  return true;
}

bool TipTextService::ReadCaretTextBeforeSelection(
    ITfContext* context, std::wstring* text_before_caret,
    ITfRange** caret_range) const {
  if (context == nullptr || text_before_caret == nullptr ||
      caret_range == nullptr || client_id_ == TF_CLIENTID_NULL) {
    return false;
  }

  text_before_caret->clear();
  *caret_range = nullptr;

  CaretTextSnapshot snapshot;
  CaretTextReadEditSession* edit_session =
      new (std::nothrow) CaretTextReadEditSession(context, &snapshot);
  if (edit_session == nullptr) {
    return false;
  }

  HRESULT edit_session_result = E_FAIL;
  const HRESULT hr = context->RequestEditSession(
      client_id_, edit_session, TF_ES_SYNC | TF_ES_READ, &edit_session_result);
  edit_session->Release();
  if (FAILED(hr) || FAILED(edit_session_result) ||
      snapshot.text_before_caret.empty() || snapshot.caret_range == nullptr) {
    return false;
  }

  *text_before_caret = std::move(snapshot.text_before_caret);
  *caret_range = snapshot.caret_range;
  snapshot.caret_range = nullptr;
  return true;
}

bool TipTextService::CreateCaretCandidateRange(
    ITfContext* context, ITfRange* caret_range,
    std::uint32_t run_utf16_length, std::uint32_t start_utf16_offset,
    std::uint32_t matched_utf16_length, ITfRange** target_range) const {
  if (context == nullptr || caret_range == nullptr || target_range == nullptr ||
      *target_range != nullptr || run_utf16_length == 0 ||
      matched_utf16_length == 0 || client_id_ == TF_CLIENTID_NULL) {
    return false;
  }

  CaretSegmentRangeEditSession* edit_session =
      new (std::nothrow) CaretSegmentRangeEditSession(
          caret_range, run_utf16_length, start_utf16_offset,
          matched_utf16_length, target_range);
  if (edit_session == nullptr) {
    return false;
  }

  HRESULT edit_session_result = E_FAIL;
  const HRESULT hr = context->RequestEditSession(
      client_id_, edit_session, TF_ES_SYNC | TF_ES_READ, &edit_session_result);
  edit_session->Release();
  return SUCCEEDED(hr) && SUCCEEDED(edit_session_result) &&
         *target_range != nullptr;
}

HRESULT TipTextService::CommitSelectionCandidate(
    const std::string& candidate_text, ITfContext* context, ITfRange* range,
    std::uint32_t matched_utf16_length) const {
  if (context == nullptr || range == nullptr || matched_utf16_length == 0 ||
      client_id_ == TF_CLIENTID_NULL) {
    return E_INVALIDARG;
  }

  const std::wstring candidate_wide = Utf8ToWideText(candidate_text);
  if (candidate_wide.empty()) {
    return E_INVALIDARG;
  }

  SelectionCandidateCommitEditSession* edit_session =
      new (std::nothrow) SelectionCandidateCommitEditSession(
          context, range, matched_utf16_length, candidate_wide);
  if (edit_session == nullptr) {
    return E_OUTOFMEMORY;
  }

  HRESULT edit_session_result = E_FAIL;
  HRESULT hr = context->RequestEditSession(
      client_id_, edit_session, TF_ES_SYNC | TF_ES_READWRITE,
      &edit_session_result);
  if ((FAILED(hr) && ShouldRetryAsync(hr)) ||
      (SUCCEEDED(hr) && ShouldRetryAsync(edit_session_result))) {
    edit_session_result = E_FAIL;
    hr = context->RequestEditSession(
        client_id_, edit_session, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE,
        &edit_session_result);
  }
  edit_session->Release();
  if (FAILED(hr)) {
    return hr;
  }

  return edit_session_result;
}

HRESULT TipTextService::CommitCaretCandidate(
    const std::string& candidate_text, ITfContext* context,
    ITfRange* range) const {
  if (context == nullptr || range == nullptr || client_id_ == TF_CLIENTID_NULL) {
    return E_INVALIDARG;
  }

  const std::wstring candidate_wide = Utf8ToWideText(candidate_text);
  if (candidate_wide.empty()) {
    return E_INVALIDARG;
  }

  RangeCandidateCommitEditSession* edit_session =
      new (std::nothrow) RangeCandidateCommitEditSession(
          context, range, candidate_wide);
  if (edit_session == nullptr) {
    return E_OUTOFMEMORY;
  }

  HRESULT edit_session_result = E_FAIL;
  HRESULT hr = context->RequestEditSession(
      client_id_, edit_session, TF_ES_SYNC | TF_ES_READWRITE,
      &edit_session_result);
  if ((FAILED(hr) && ShouldRetryAsync(hr)) ||
      (SUCCEEDED(hr) && ShouldRetryAsync(edit_session_result))) {
    edit_session_result = E_FAIL;
    hr = context->RequestEditSession(
        client_id_, edit_session, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE,
        &edit_session_result);
  }
  edit_session->Release();
  if (FAILED(hr)) {
    return hr;
  }

  return edit_session_result;
}

std::optional<POINT> TipTextService::CandidateWindowAnchor(
    ITfContext* context) const {
  if (candidate_commit_target_kind_ ==
      CandidateCommitTargetKind::kSelectionRange) {
    const std::optional<RECT> selection_rect =
        SelectionCandidateTextRect(context);
    if (selection_rect.has_value()) {
      return POINT{selection_rect->left, selection_rect->bottom};
    }
  } else if (candidate_commit_target_kind_ ==
             CandidateCommitTargetKind::kCaretRange) {
    const std::optional<RECT> caret_rect = CaretCandidateTextRect(context);
    if (caret_rect.has_value()) {
      return POINT{caret_rect->left, caret_rect->bottom};
    }
  } else {
    const std::optional<RECT> composition_rect = CompositionTextRect(context);
    if (composition_rect.has_value()) {
      return POINT{composition_rect->left, composition_rect->bottom};
    }
  }

  const std::optional<POINT> caret_anchor = AnchorFromGuiThreadCaret();
  if (caret_anchor.has_value()) {
    return caret_anchor;
  }

  POINT cursor = {};
  if (GetCursorPos(&cursor)) {
    return cursor;
  }

  return std::nullopt;
}

std::optional<RECT> TipTextService::CompositionTextRect(
    ITfContext* context) const {
  if (context == nullptr || composition_ == nullptr ||
      context != composition_context_ || client_id_ == TF_CLIENTID_NULL) {
    return std::nullopt;
  }

  ITfContextView* view = nullptr;
  HRESULT hr = context->GetActiveView(&view);
  if (FAILED(hr) || view == nullptr) {
    return std::nullopt;
  }

  ITfRange* range = nullptr;
  hr = composition_->GetRange(&range);
  if (FAILED(hr) || range == nullptr) {
    SafeRelease(view);
    return std::nullopt;
  }

  std::optional<RECT> text_rect;
  TextExtentEditSession* edit_session =
      new (std::nothrow) TextExtentEditSession(view, range, &text_rect);
  SafeRelease(range);
  SafeRelease(view);
  if (edit_session == nullptr) {
    return std::nullopt;
  }

  HRESULT edit_session_result = E_FAIL;
  hr = context->RequestEditSession(client_id_, edit_session,
                                   TF_ES_SYNC | TF_ES_READ,
                                   &edit_session_result);
  edit_session->Release();
  if (FAILED(hr) || FAILED(edit_session_result)) {
    return std::nullopt;
  }

  return text_rect;
}

std::optional<RECT> TipTextService::SelectionCandidateTextRect(
    ITfContext* context) const {
  if (context == nullptr ||
      candidate_commit_target_kind_ != CandidateCommitTargetKind::kSelectionRange ||
      selection_candidate_target_.context != context ||
      selection_candidate_target_.range == nullptr ||
      selection_candidate_target_.matched_utf16_length == 0 ||
      client_id_ == TF_CLIENTID_NULL) {
    return std::nullopt;
  }

  ITfContextView* view = nullptr;
  HRESULT hr = context->GetActiveView(&view);
  if (FAILED(hr) || view == nullptr) {
    return std::nullopt;
  }

  std::optional<RECT> text_rect;
  SelectionPrefixTextExtentEditSession* edit_session =
      new (std::nothrow) SelectionPrefixTextExtentEditSession(
          view, selection_candidate_target_.range,
          selection_candidate_target_.matched_utf16_length, &text_rect);
  SafeRelease(view);
  if (edit_session == nullptr) {
    return std::nullopt;
  }

  HRESULT edit_session_result = E_FAIL;
  hr = context->RequestEditSession(client_id_, edit_session,
                                   TF_ES_SYNC | TF_ES_READ,
                                   &edit_session_result);
  edit_session->Release();
  if (FAILED(hr) || FAILED(edit_session_result)) {
    return std::nullopt;
  }

  return text_rect;
}

std::optional<RECT> TipTextService::CaretCandidateTextRect(
    ITfContext* context) const {
  if (context == nullptr ||
      candidate_commit_target_kind_ != CandidateCommitTargetKind::kCaretRange ||
      caret_candidate_target_.context != context ||
      caret_candidate_target_.range == nullptr ||
      client_id_ == TF_CLIENTID_NULL) {
    return std::nullopt;
  }

  ITfContextView* view = nullptr;
  HRESULT hr = context->GetActiveView(&view);
  if (FAILED(hr) || view == nullptr) {
    return std::nullopt;
  }

  std::optional<RECT> text_rect;
  TextExtentEditSession* edit_session = new (std::nothrow)
      TextExtentEditSession(view, caret_candidate_target_.range, &text_rect);
  SafeRelease(view);
  if (edit_session == nullptr) {
    return std::nullopt;
  }

  HRESULT edit_session_result = E_FAIL;
  hr = context->RequestEditSession(client_id_, edit_session,
                                   TF_ES_SYNC | TF_ES_READ,
                                   &edit_session_result);
  edit_session->Release();
  if (FAILED(hr) || FAILED(edit_session_result)) {
    return std::nullopt;
  }

  return text_rect;
}

HRESULT TipTextService::MoveSelectionToRangeEnd(TfEditCookie edit_cookie,
                                                ITfContext* context,
                                                ITfRange* range) const {
  if (context == nullptr || range == nullptr) {
    return E_INVALIDARG;
  }

  ITfRange* selection_range = nullptr;
  HRESULT hr = range->Clone(&selection_range);
  if (FAILED(hr)) {
    return hr;
  }

  hr = selection_range->Collapse(edit_cookie, TF_ANCHOR_END);
  if (FAILED(hr)) {
    selection_range->Release();
    return hr;
  }

  TF_SELECTION selection = {};
  selection.range = selection_range;
  selection.style.ase = TF_AE_NONE;
  selection.style.fInterimChar = FALSE;
  hr = context->SetSelection(edit_cookie, 1, &selection);
  selection_range->Release();
  return hr;
}

HRESULT TipTextService::ClearCompositionDisplayAttribute(
    TfEditCookie edit_cookie, ITfContext* context) const {
  if (context == nullptr || composition_ == nullptr) {
    return S_FALSE;
  }

  ITfRange* composition_range = nullptr;
  HRESULT hr = composition_->GetRange(&composition_range);
  if (FAILED(hr)) {
    return hr;
  }

  ITfProperty* display_attribute_property = nullptr;
  hr = context->GetProperty(GUID_PROP_ATTRIBUTE, &display_attribute_property);
  if (SUCCEEDED(hr)) {
    hr = display_attribute_property->Clear(edit_cookie, composition_range);
    display_attribute_property->Release();
  }

  composition_range->Release();
  return hr;
}

HRESULT TipTextService::ApplyCompositionDisplayAttribute(
    TfEditCookie edit_cookie, ITfContext* context,
    const std::wstring& text) const {
  if (context == nullptr || composition_ == nullptr) {
    return S_FALSE;
  }

  ITfRange* composition_range = nullptr;
  HRESULT hr = composition_->GetRange(&composition_range);
  if (FAILED(hr)) {
    return hr;
  }

  ITfProperty* display_attribute_property = nullptr;
  hr = context->GetProperty(GUID_PROP_ATTRIBUTE, &display_attribute_property);
  if (FAILED(hr)) {
    composition_range->Release();
    return hr;
  }

  hr = display_attribute_property->Clear(edit_cookie, composition_range);
  if (FAILED(hr) || text.empty() ||
      composing_display_attribute_atom_ == TF_INVALID_GUIDATOM) {
    display_attribute_property->Release();
    composition_range->Release();
    return hr;
  }

  ITfRange* target_range = nullptr;
  hr = composition_range->Clone(&target_range);
  if (SUCCEEDED(hr)) {
    const LONG last_code_point_units =
        text.size() >= 2 && text.back() >= 0xDC00 && text.back() <= 0xDFFF &&
                text[text.size() - 2] >= 0xD800 &&
                text[text.size() - 2] <= 0xDBFF
            ? 2
            : 1;
    const LONG text_units = static_cast<LONG>(text.size());
    const LONG target_start = text_units - last_code_point_units;
    LONG moved = 0;

    hr = target_range->Collapse(edit_cookie, TF_ANCHOR_START);
    if (SUCCEEDED(hr)) {
      hr = target_range->ShiftEnd(edit_cookie, text_units, &moved, nullptr);
    }
    if (SUCCEEDED(hr) && moved != text_units) {
      hr = S_FALSE;
    }
    if (SUCCEEDED(hr)) {
      moved = 0;
      hr = target_range->ShiftStart(edit_cookie, target_start, &moved, nullptr);
    }
    if (SUCCEEDED(hr) && moved != target_start) {
      hr = S_FALSE;
    }
    if (SUCCEEDED(hr)) {
      VARIANT value;
      VariantInit(&value);
      value.vt = VT_I4;
      value.lVal = static_cast<LONG>(composing_display_attribute_atom_);
      hr = display_attribute_property->SetValue(edit_cookie, target_range,
                                                &value);
    }
  }

  if (target_range != nullptr) {
    target_range->Release();
  }
  display_attribute_property->Release();
  composition_range->Release();
  return hr;
}

void TipTextService::SyncCompositionTermination() {
#if defined(_DEBUG)
  debug::DebugLog(
      L"[MilkyWayIME][SyncCompositionTermination][Begin] session_composing=" +
      std::to_wstring(session_.IsComposing() ? 1 : 0) +
      L" pending_operations=" +
      std::to_wstring(edit_sink_.PendingOperationCount()) + L" " +
      CompositionTrackingState(L"current", composition_, composition_context_));
#endif
  ClearPendingKeyResult();
  edit_sink_.ClearPendingOperations();
  ClearCompositionTracking();
  ResetTransitoryDirectTextComposition(L"SyncCompositionTermination");
  ResetNikkeDirectTextComposition(L"SyncCompositionTermination");
  ResetTransitoryCompositionBridge(L"SyncCompositionTermination");
  logic_.OnCompositionTerminated();
#if defined(_DEBUG)
  debug::DebugLog(
      L"[MilkyWayIME][SyncCompositionTermination][End] session_composing=" +
      std::to_wstring(session_.IsComposing() ? 1 : 0) + L" " +
      CompositionTrackingState(L"current", composition_, composition_context_));
#endif
}

void TipTextService::ClearPendingKeyResult() {
  pending_key_result_ = {};
}

}  // namespace milkyway::tsf::service

#endif
