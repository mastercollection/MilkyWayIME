#include "tsf/service/tip_text_service.h"

#if defined(_WIN32)

#include <cstdint>
#include <array>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "engine/layout/base_layout_json_loader.h"
#include "tsf/langbar/input_mode_lang_bar_item.h"
#include "tsf/debug/debug_log.h"
#include "tsf/registration/text_service_registration.h"
#include "tsf/service/module_state.h"
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

bool IsImeModeToggleVirtualKey(WPARAM wparam) {
  return static_cast<std::uint16_t>(wparam) == VK_HANGUL;
}

constexpr GUID kImeModePreservedKeyGuid = {
    0x7774b3e6,
    0xf464,
    0x4674,
    {0xa1, 0xcf, 0xb0, 0xed, 0xdb, 0x0e, 0x2b, 0x2a},
};

constexpr wchar_t kImeModePreservedKeyDescription[] =
    L"MilkyWayIME Hangul toggle";

std::wstring FormatHex(std::uint32_t value) {
  wchar_t buffer[16] = {};
  swprintf_s(buffer, L"%08X", value);
  return buffer;
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

std::filesystem::path UserBaseLayoutDirectory() {
  const DWORD required = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
  if (required == 0) {
    return {};
  }

  std::wstring appdata(required, L'\0');
  const DWORD length =
      GetEnvironmentVariableW(L"APPDATA", appdata.data(), required);
  if (length == 0 || length >= required) {
    return {};
  }

  appdata.resize(length);
  return std::filesystem::path(appdata) / L"MilkyWayIME" / L"layouts" /
         L"base";
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
  }

  return L"Unknown";
}

#endif

void LoadUserBaseLayouts(engine::layout::LayoutRegistry* layout_registry) {
  if (layout_registry == nullptr) {
    return;
  }

  const std::filesystem::path directory = UserBaseLayoutDirectory();
  if (directory.empty()) {
    return;
  }

  std::error_code error_code;
  if (!std::filesystem::is_directory(directory, error_code)) {
    return;
  }

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
  for (const std::string& error : result.errors) {
    debug::DebugLog(L"[MilkyWayIME][LayoutLoader][Base][Error] " +
                    Utf8ToWide(error));
  }
#endif

  for (auto definition : result.definitions) {
    const std::string id = definition.layout.id;
    const bool overrides_existing =
        layout_registry->FindBaseLayout(id) != nullptr;
    if (!layout_registry->AddBaseLayout(std::move(definition))) {
#if defined(_DEBUG)
      debug::DebugLog(L"[MilkyWayIME][LayoutLoader][Base][Skipped] id=" +
                      Utf8ToWide(id));
#endif
      continue;
    }
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][LayoutLoader][Base]" +
        std::wstring(overrides_existing ? L"[Override] id="
                                        : L"[Loaded] id=") +
        Utf8ToWide(id));
#endif
  }
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
                  std::to_wstring((flags & TF_TMAE_COMLESS) ? 1 : 0));
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
  UnregisterPreservedKeys();
  UnadviseKeyboardOpenCloseCompartmentSink();
  UnadviseKeyEventSink();
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
                                        ITfDocumentMgr*) {
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][OnSetFocus(DocumentMgr)] process=" +
                  CurrentProcessName() + L" focused=" +
                  PointerToString(focused) + L" current_text_context=" +
                  PointerToString(text_edit_sink_context_) + L" composing=" +
                  std::to_wstring(session_.IsComposing() ? 1 : 0));
#endif
  if (text_edit_sink_context_ != nullptr && session_.IsComposing()) {
    logic_.OnFocusLost();
    FlushPendingOperations(text_edit_sink_context_,
                           EditSessionRequestPolicy::kSyncPreferredWrite,
                           L"OnSetFocus(DocumentMgr)");
  }

  const HRESULT refresh_hr = RefreshFocusedContext(focused);
  if (SUCCEEDED(refresh_hr)) {
    SyncLayoutSelectionFromSettings(L"OnSetFocus(DocumentMgr)");
  }
  return refresh_hr;
}

STDMETHODIMP TipTextService::OnPushContext(ITfContext*) {
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][OnPushContext]");
#endif
  const HRESULT refresh_hr = RefreshFocusedContext();
  if (SUCCEEDED(refresh_hr)) {
    SyncLayoutSelectionFromSettings(L"OnPushContext");
  }
  return refresh_hr;
}

STDMETHODIMP TipTextService::OnPopContext(ITfContext*) {
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][OnPopContext]");
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
  if (tracked) {
    if (composition_context_ != nullptr) {
      ClearCompositionDisplayAttribute(edit_cookie, composition_context_);
    }
    ClearCompositionTracking();
  }

  if (tracked || session_.IsComposing()) {
    logic_.OnCompositionTerminated();
  }

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

STDMETHODIMP TipTextService::EnumDisplayAttributeInfo(
    IEnumTfDisplayAttributeInfo** enum_info) {
  return display::CreateEnumDisplayAttributeInfo(enum_info);
}

STDMETHODIMP TipTextService::GetDisplayAttributeInfo(
    REFGUID guid_info, ITfDisplayAttributeInfo** info) {
  return display::CreateDisplayAttributeInfo(guid_info, info);
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
  return hr;
}

HRESULT TipTextService::CompleteComposition(TfEditCookie edit_cookie) {
  if (composition_ == nullptr) {
    return S_FALSE;
  }

  ITfComposition* composition = composition_;
  composition->AddRef();
  if (composition_context_ != nullptr) {
    ClearCompositionDisplayAttribute(edit_cookie, composition_context_);
  }
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
                  CurrentProcessName());
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
    debug::DebugLog(L"[MilkyWayIME][RefreshFocusedContext][NoFocusedDocument]");
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
                    FormatHex(static_cast<std::uint32_t>(hr)));
#endif
    DetachTextEditSink();
    return hr;
  }

  const HRESULT attach_hr = AttachTextEditSink(context);
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][RefreshFocusedContext][End] focused_docmgr=" +
                  PointerToString(focused_document_mgr) + L" context=" +
                  PointerToString(context) + L" attach_hr=0x" +
                  FormatHex(static_cast<std::uint32_t>(attach_hr)));
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
