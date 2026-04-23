#include "tsf/service/tip_text_service.h"

#if defined(_WIN32)

#include <array>
#include <string>
#include <string_view>
#include <utility>

#include "tsf/langbar/input_mode_lang_bar_item.h"
#include "tsf/debug/debug_log.h"
#include "tsf/registration/text_service_registration.h"
#include "tsf/service/module_state.h"

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

bool IsImeModeToggleVirtualKey(WPARAM wparam) {
  return static_cast<std::uint16_t>(wparam) == VK_HANGUL;
}

std::wstring FormatHex(std::uint32_t value) {
  wchar_t buffer[16] = {};
  swprintf_s(buffer, L"%08X", value);
  return buffer;
}

#if defined(_DEBUG)

std::wstring Utf8ToWide(std::string_view text) {
  if (text.empty()) {
    return {};
  }

  const int length =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                          static_cast<int>(text.size()), nullptr, 0);
  if (length <= 0) {
    return L"<invalid-utf8>";
  }

  std::wstring wide_text(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                      static_cast<int>(text.size()), wide_text.data(), length);
  return wide_text;
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
    case engine::shortcut::ShortcutAction::kToggleInputMode:
      return L"ToggleInputMode";
    case engine::shortcut::ShortcutAction::kOpenConfiguration:
      return L"OpenConfiguration";
  }

  return L"Unknown";
}

#endif

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
         left.base_layout_key == right.base_layout_key &&
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
    : session_(layout_registry_.DefaultPhysicalLayout().id,
               layout_registry_.DefaultKoreanLayout().id),
      edit_sink_(this),
      logic_(&session_, adapters::libhangul::CreateLibhangulComposer(),
             &edit_sink_, &layout_registry_) {
  DllAddRef();
}

TipTextService::~TipTextService() {
  if (thread_mgr_ != nullptr) {
    Deactivate();
  }

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
    Deactivate();
    return hr;
  }

  hr = AdviseKeyEventSink();
  if (FAILED(hr)) {
    Deactivate();
    return hr;
  }

  hr = RefreshFocusedContext();
  if (FAILED(hr)) {
    Deactivate();
    return hr;
  }

  hr = AddInputModeLanguageBarItem();
  if (FAILED(hr)) {
    Deactivate();
    return hr;
  }

  SetImeOpen(true);
  return S_OK;
}

STDMETHODIMP TipTextService::Deactivate() {
  deactivating_ = true;

  if (session_.IsComposing() && text_edit_sink_context_ != nullptr) {
    logic_.OnFocusLost();
    FlushPendingOperations(text_edit_sink_context_,
                           EditSessionRequestPolicy::kSyncPreferredWrite,
                           L"Deactivate");
  }

  ClearPendingKeyResult();
  edit_sink_.ClearPendingOperations();
  RemoveInputModeLanguageBarItem();
  DetachTextEditSink();
  UnadviseKeyEventSink();
  UnadviseThreadMgrEventSink();
  ClearCompositionTracking();
  SafeRelease(thread_mgr_);

  client_id_ = TF_CLIENTID_NULL;
  activate_flags_ = 0;
  ime_open_ = true;
  deactivating_ = false;
  return S_OK;
}

STDMETHODIMP TipTextService::OnInitDocumentMgr(ITfDocumentMgr*) {
  return S_OK;
}

STDMETHODIMP TipTextService::OnUninitDocumentMgr(ITfDocumentMgr*) {
  return S_OK;
}

STDMETHODIMP TipTextService::OnSetFocus(ITfDocumentMgr* focused,
                                        ITfDocumentMgr*) {
  if (text_edit_sink_context_ != nullptr && session_.IsComposing()) {
    logic_.OnFocusLost();
    FlushPendingOperations(text_edit_sink_context_,
                           EditSessionRequestPolicy::kSyncPreferredWrite,
                           L"OnSetFocus(DocumentMgr)");
  }

  return RefreshFocusedContext(focused);
}

STDMETHODIMP TipTextService::OnPushContext(ITfContext*) {
  return RefreshFocusedContext();
}

STDMETHODIMP TipTextService::OnPopContext(ITfContext*) {
  return RefreshFocusedContext();
}

STDMETHODIMP TipTextService::OnEndEdit(ITfContext* context,
                                       TfEditCookie read_cookie,
                                       ITfEditRecord*) {
  if (context == nullptr || edit_sink_.is_flushing() || !session_.IsComposing()) {
    return S_OK;
  }

  if (!SelectionInsideComposition(context, read_cookie)) {
    logic_.OnSelectionMovedOutsideComposition();
    FlushPendingOperations(context, EditSessionRequestPolicy::kSyncPreferredWrite,
                           L"OnEndEdit");
  }

  return S_OK;
}

STDMETHODIMP TipTextService::OnSetFocus(BOOL) {
  return S_OK;
}

STDMETHODIMP TipTextService::OnTestKeyDown(ITfContext*, WPARAM wparam,
                                           LPARAM lparam, BOOL* eaten) {
  if (eaten == nullptr) {
    return E_INVALIDARG;
  }

  *eaten = FALSE;
  const engine::state::ModifierState modifiers = QueryModifierState();
  const engine::key::NormalizedKeyEvent event = BuildNormalizedKeyEvent(
      wparam, lparam, modifiers, engine::key::KeyTransition::kPressed);

  if (IsImeModeToggleVirtualKey(wparam)) {
    pending_key_result_.active = true;
    pending_key_result_.event = event;
    pending_key_result_.eaten = true;
    *eaten = TRUE;
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
    ClearPendingKeyResult();
    logic_.PrepareImeModeToggle();
    if (!FinalizeImeModeToggle(context, L"OnKeyDown(VK_HANGUL)")) {
      SyncCompositionTermination();
    }
    *eaten = TRUE;
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
  ClearPendingKeyResult();
  const KeyEventResult result =
      logic_.OnKeyEvent(key, modifiers, engine::key::KeyTransition::kPressed);
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
                           L" base=" +
                           Utf8ToWide(engine::key::BaseLayoutKeyName(
                               event.base_layout_key)) +
                           L" ascii=0x" +
                           std::to_wstring(static_cast<unsigned char>(ascii)) +
                           L" category=" + CategoryName(result.category) +
                           L" eaten=" + std::to_wstring(final_eaten ? 1 : 0) +
                           L" pending_eaten=" +
                           std::to_wstring(pending_eaten ? 1 : 0) +
                           L" forward=" +
                           std::to_wstring(result.should_forward ? 1 : 0) +
                           L" shortcut=" +
                           ShortcutActionName(result.shortcut_action) +
                           L" commit=\"" + Utf8ToWide(result.commit_text) +
                           L"\" preedit=\"" + Utf8ToWide(result.preedit_text) +
                           L"\"";
    debug::DebugLog(std::move(message));
  }
#endif

  if (edit_sink_.HasPendingOperations()) {
    const HRESULT hr = FlushPendingOperations(
        context, EditSessionRequestPolicy::kKeyPathWrite, L"OnKeyDown");
    if (FAILED(hr)) {
      SyncCompositionTermination();
      return S_OK;
    }
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

STDMETHODIMP TipTextService::OnPreservedKey(ITfContext*, REFGUID, BOOL* eaten) {
  if (eaten == nullptr) {
    return E_INVALIDARG;
  }

  *eaten = FALSE;
  return S_OK;
}

STDMETHODIMP TipTextService::OnCompositionTerminated(TfEditCookie,
                                                     ITfComposition* composition) {
  const bool tracked = composition_ == composition;
  if (tracked) {
    ClearCompositionTracking();
  }

  if (tracked || session_.IsComposing()) {
    logic_.OnCompositionTerminated();
  }

  return S_OK;
}

TfClientId TipTextService::client_id() const {
  return client_id_;
}

bool TipTextService::HasActiveComposition() const {
  return composition_ != nullptr;
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

  const HRESULT insert_hr = insert_at_selection->InsertTextAtSelection(
      edit_cookie, 0, text.c_str(), static_cast<LONG>(text.size()), nullptr);
  insert_at_selection->Release();
  return insert_hr;
}

HRESULT TipTextService::StartComposition(TfEditCookie edit_cookie,
                                         ITfContext* context,
                                         const std::wstring& text) {
  if (context == nullptr) {
    return E_INVALIDARG;
  }

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
  hr = S_OK;

Exit:
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
    return E_UNEXPECTED;
  }

  ITfRange* composition_range = nullptr;
  HRESULT hr = composition_->GetRange(&composition_range);
  if (FAILED(hr)) {
    return hr;
  }

  const wchar_t* composition_text = text.empty() ? nullptr : text.c_str();
  hr = composition_range->SetText(edit_cookie, 0, composition_text,
                                  static_cast<LONG>(text.size()));
  if (SUCCEEDED(hr)) {
    hr = MoveSelectionToRangeEnd(edit_cookie, composition_context_,
                                 composition_range);
  }

  composition_range->Release();
  return hr;
}

HRESULT TipTextService::CompleteComposition(TfEditCookie edit_cookie) {
  if (composition_ == nullptr) {
    return S_FALSE;
  }

  ITfComposition* composition = composition_;
  composition->AddRef();
  ClearCompositionTracking();
  const HRESULT hr = composition->EndComposition(edit_cookie);
  composition->Release();
  return hr;
}

void TipTextService::ClearCompositionTracking() {
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

HRESULT TipTextService::RefreshFocusedContext(ITfDocumentMgr* document_mgr) {
  ITfDocumentMgr* focused_document_mgr = document_mgr;
  bool release_document_mgr = false;

  if (focused_document_mgr == nullptr) {
    if (thread_mgr_ == nullptr) {
      return S_OK;
    }

    const HRESULT hr = thread_mgr_->GetFocus(&focused_document_mgr);
    if (FAILED(hr)) {
      DetachTextEditSink();
      return hr;
    }
    release_document_mgr = true;
  }

  if (focused_document_mgr == nullptr) {
    DetachTextEditSink();
    return S_OK;
  }

  ITfContext* context = nullptr;
  const HRESULT hr = focused_document_mgr->GetTop(&context);
  if (release_document_mgr) {
    focused_document_mgr->Release();
  }
  if (FAILED(hr)) {
    DetachTextEditSink();
    return hr;
  }

  const HRESULT attach_hr = AttachTextEditSink(context);
  context->Release();
  return attach_hr;
}

HRESULT TipTextService::AttachTextEditSink(ITfContext* context) {
  if (context == text_edit_sink_context_) {
    return S_OK;
  }

  DetachTextEditSink();
  if (context == nullptr) {
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
    return advise_hr;
  }

  text_edit_sink_context_ = context;
  text_edit_sink_context_->AddRef();
  return S_OK;
}

void TipTextService::DetachTextEditSink() {
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

  input_mode_lang_bar_item_->Release();
  input_mode_lang_bar_item_ = nullptr;
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
  return layout_registry_.NormalizeKeyEvent(session_.physical_layout_id(),
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

void TipTextService::SetImeOpen(bool open) {
  ime_open_ = open;

  if (thread_mgr_ == nullptr || client_id_ == TF_CLIENTID_NULL) {
    if (input_mode_lang_bar_item_ != nullptr) {
      input_mode_lang_bar_item_->OnImeOpenChanged(open);
    }
    return;
  }

  ITfCompartmentMgr* compartment_manager = nullptr;
  if (FAILED(thread_mgr_->GetGlobalCompartment(&compartment_manager))) {
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
    compartment->SetValue(client_id_, &value);
    compartment->Release();
  }

  compartment_manager->Release();
  if (input_mode_lang_bar_item_ != nullptr) {
    input_mode_lang_bar_item_->OnImeOpenChanged(open);
  }
}

bool TipTextService::FinalizeImeModeToggle(ITfContext* context,
                                           const wchar_t* origin) {
  if (edit_sink_.HasPendingOperations()) {
    if (context == nullptr) {
      return false;
    }

    const HRESULT hr =
        FlushPendingOperations(context, EditSessionRequestPolicy::kKeyPathWrite,
                               origin);
    if (FAILED(hr)) {
      return false;
    }
  }

  SetImeOpen(!ime_open_);
  return true;
}

void TipTextService::ToggleImeModeFromLanguageBar() {
  logic_.PrepareImeModeToggle();
  if (!FinalizeImeModeToggle(text_edit_sink_context_, L"LangBarClick")) {
    SyncCompositionTermination();
  }
}

void TipTextService::HandleShortcutAction(
    ITfContext* context, engine::shortcut::ShortcutAction action) {
  if (action == engine::shortcut::ShortcutAction::kToggleInputMode) {
    if (!FinalizeImeModeToggle(context, L"ShortcutToggle")) {
      SyncCompositionTermination();
    }
  }
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

void TipTextService::SyncCompositionTermination() {
  ClearPendingKeyResult();
  edit_sink_.ClearPendingOperations();
  ClearCompositionTracking();
  logic_.OnCompositionTerminated();
}

void TipTextService::ClearPendingKeyResult() {
  pending_key_result_ = {};
}

}  // namespace milkyway::tsf::service

#endif
