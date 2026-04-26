#include "tsf/langbar/input_mode_lang_bar_item.h"

#if defined(_WIN32)

#include <strsafe.h>

#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

#include "tsf/registration/text_service_registration.h"
#include "tsf/resources/resource.h"
#include "tsf/debug/debug_log.h"
#include "tsf/service/module_state.h"
#include "tsf/service/tip_text_service.h"

namespace milkyway::tsf::langbar {
namespace {

constexpr DWORD kLangBarSinkCookie = 1;
constexpr UINT kMenuToggleInputMode = 1;
constexpr UINT kMenuBaseLayoutStart = 1000;
constexpr UINT kMenuKoreanLayoutStart = 2000;
// Windows 8+ ignores third-party input mode items unless guidItem matches
// GUID_LBI_INPUTMODE. Use the documented value directly to avoid SDK/link
// variance across environments.
constexpr GUID kGuidLangBarInputMode = {
    0x2c77a81e,
    0x41cc,
    0x4178,
    {0xa3, 0xa7, 0x5f, 0x8a, 0x98, 0x75, 0x68, 0xe6},
};

std::wstring CurrentModeText(bool ime_open) {
  return ime_open ? L"가" : L"A";
}

std::wstring Utf8ToWide(std::string_view text) {
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

std::wstring HresultToHex(HRESULT hr) {
  wchar_t buffer[16] = {};
  swprintf_s(buffer, L"%08X", static_cast<unsigned long>(hr));
  return buffer;
}

bool AppendMenuString(HMENU menu, UINT_PTR id, UINT flags,
                      const std::wstring& text) {
  return AppendMenuW(menu, flags | MF_STRING, id, text.c_str()) != FALSE;
}

bool AppendMenuString(HMENU menu, UINT_PTR id, UINT flags,
                      const wchar_t* text) {
  return AppendMenuW(menu, flags | MF_STRING, id, text) != FALSE;
}

bool AppendSeparator(HMENU menu) {
  return AppendMenuW(menu, MF_SEPARATOR, 0, nullptr) != FALSE;
}

std::wstring LayoutLabel(std::string_view display_name, std::string_view id) {
  std::wstring label = Utf8ToWide(display_name);
  if (label.empty()) {
    label = Utf8ToWide(id);
  }
  return label;
}

HMENU BuildBaseLayoutSubmenu(
    const std::vector<engine::layout::PhysicalLayout>& layouts,
    const engine::layout::PhysicalLayoutId& current_id) {
  HMENU menu = CreatePopupMenu();
  if (menu == nullptr) {
    return nullptr;
  }

  const std::size_t count =
      std::min<std::size_t>(layouts.size(), kMenuKoreanLayoutStart -
                                                kMenuBaseLayoutStart);
  UINT checked_id = 0;
  for (std::size_t index = 0; index < count; ++index) {
    const auto& layout = layouts[index];
    const UINT id = kMenuBaseLayoutStart + static_cast<UINT>(index);
    if (!AppendMenuString(menu, id, 0,
                          LayoutLabel(layout.display_name, layout.id))) {
      DestroyMenu(menu);
      return nullptr;
    }
    if (layout.id == current_id) {
      checked_id = id;
    }
  }

  if (checked_id != 0 && count > 0) {
    CheckMenuRadioItem(menu, kMenuBaseLayoutStart,
                       kMenuBaseLayoutStart + static_cast<UINT>(count) - 1,
                       checked_id, MF_BYCOMMAND);
  }
  return menu;
}

HMENU BuildKoreanLayoutSubmenu(
    const std::vector<engine::layout::KoreanLayoutMapping>& layouts,
    const engine::layout::KoreanLayoutId& current_id) {
  HMENU menu = CreatePopupMenu();
  if (menu == nullptr) {
    return nullptr;
  }

  constexpr std::size_t kMaxKoreanMenuItems = 1000;
  const std::size_t count =
      std::min<std::size_t>(layouts.size(), kMaxKoreanMenuItems);
  UINT checked_id = 0;
  for (std::size_t index = 0; index < count; ++index) {
    const auto& layout = layouts[index];
    const UINT id = kMenuKoreanLayoutStart + static_cast<UINT>(index);
    if (!AppendMenuString(menu, id, 0,
                          LayoutLabel(layout.display_name, layout.id))) {
      DestroyMenu(menu);
      return nullptr;
    }
    if (layout.id == current_id) {
      checked_id = id;
    }
  }

  if (checked_id != 0 && count > 0) {
    CheckMenuRadioItem(menu, kMenuKoreanLayoutStart,
                       kMenuKoreanLayoutStart + static_cast<UINT>(count) - 1,
                       checked_id, MF_BYCOMMAND);
  }
  return menu;
}

HWND ResolvePopupOwner() {
  HWND owner = GetFocus();
  if (owner == nullptr) {
    owner = GetForegroundWindow();
  }
  if (owner == nullptr) {
    owner = GetDesktopWindow();
  }
  return owner;
}

bool TryReadThemeValue(const wchar_t* value_name, DWORD* value) {
  constexpr wchar_t kPersonalizeKey[] =
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
  DWORD type = 0;
  DWORD size = sizeof(*value);
  return RegGetValueW(HKEY_CURRENT_USER, kPersonalizeKey, value_name,
                      RRF_RT_REG_DWORD, &type, value, &size) == ERROR_SUCCESS;
}

bool IsWindowsDarkTheme() {
  DWORD uses_light_theme = 1;
  if (TryReadThemeValue(L"SystemUsesLightTheme", &uses_light_theme)) {
    return uses_light_theme == 0;
  }
  if (TryReadThemeValue(L"AppsUseLightTheme", &uses_light_theme)) {
    return uses_light_theme == 0;
  }
  return false;
}

UINT CurrentModeIconResourceId(bool ime_open) {
  const bool dark_theme = IsWindowsDarkTheme();
  if (ime_open) {
    return dark_theme ? IDI_IME_MODE_HANGUL_DARK : IDI_IME_MODE_HANGUL;
  }
  return dark_theme ? IDI_IME_MODE_ALPHA_DARK : IDI_IME_MODE_ALPHA;
}

HICON LoadSmallIcon(UINT resource_id) {
  const HINSTANCE module_instance = service::ModuleInstance();
  if (module_instance == nullptr) {
    return nullptr;
  }

  int width = GetSystemMetrics(SM_CXSMICON);
  int height = GetSystemMetrics(SM_CYSMICON);
  if (width <= 0) {
    width = 16;
  }
  if (height <= 0) {
    height = 16;
  }

  return static_cast<HICON>(LoadImageW(
      module_instance, MAKEINTRESOURCEW(resource_id), IMAGE_ICON, width,
      height, LR_DEFAULTCOLOR));
}

HICON LoadInputModeIcon(bool ime_open) {
  return LoadSmallIcon(CurrentModeIconResourceId(ime_open));
}

}  // namespace

InputModeLangBarItem::InputModeLangBarItem(service::TipTextService* host,
                                           bool ime_open)
    : host_(host), ime_open_(ime_open), text_(CurrentModeText(ime_open)) {}

STDMETHODIMP InputModeLangBarItem::QueryInterface(REFIID riid, void** ppv) {
  if (ppv == nullptr) {
    return E_INVALIDARG;
  }

  *ppv = nullptr;
  if (riid == IID_IUnknown || riid == IID_ITfLangBarItem) {
    *ppv = static_cast<ITfLangBarItem*>(this);
  } else if (riid == IID_ITfLangBarItemButton) {
    *ppv = static_cast<ITfLangBarItemButton*>(this);
  } else if (riid == IID_ITfSource) {
    *ppv = static_cast<ITfSource*>(this);
  }

  if (*ppv == nullptr) {
    return E_NOINTERFACE;
  }

  AddRef();
  return S_OK;
}

STDMETHODIMP_(ULONG) InputModeLangBarItem::AddRef() {
  return ++ref_count_;
}

STDMETHODIMP_(ULONG) InputModeLangBarItem::Release() {
  const ULONG value = --ref_count_;
  if (value == 0) {
    delete this;
  }
  return value;
}

STDMETHODIMP InputModeLangBarItem::GetInfo(TF_LANGBARITEMINFO* info) {
  if (info == nullptr) {
    return E_INVALIDARG;
  }

  *info = {};
  info->clsidService = registration::kTextServiceClsid;
  info->guidItem = kGuidLangBarInputMode;
  info->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
  info->ulSort = 0;
  StringCchCopyW(info->szDescription, ARRAYSIZE(info->szDescription),
                 L"MilkyWayIME 입력 모드");
  return S_OK;
}

STDMETHODIMP InputModeLangBarItem::GetStatus(DWORD* status) {
  if (status == nullptr) {
    return E_INVALIDARG;
  }

  *status = status_;
  return S_OK;
}

STDMETHODIMP InputModeLangBarItem::Show(BOOL) {
  NotifyUpdated();
  return S_OK;
}

STDMETHODIMP InputModeLangBarItem::GetTooltipString(BSTR* tooltip) {
  if (tooltip == nullptr) {
    return E_INVALIDARG;
  }

  *tooltip = SysAllocString(tooltip_.c_str());
  return *tooltip == nullptr ? E_OUTOFMEMORY : S_OK;
}

STDMETHODIMP InputModeLangBarItem::OnClick(TfLBIClick click, POINT point,
                                           const RECT* area) {
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][LangBar][OnClick] role=input_mode click=" +
                  std::to_wstring(static_cast<int>(click)));
#endif
  if (host_ == nullptr) {
    return S_OK;
  }

  if (click == TF_LBI_CLK_LEFT) {
    host_->ToggleImeModeFromLanguageBar();
    return S_OK;
  }

  if (click != TF_LBI_CLK_RIGHT) {
    return S_OK;
  }

  HMENU menu = CreatePopupMenu();
  if (menu == nullptr) {
#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][LangBar][PopupMenu][CreateFailed]");
#endif
    return S_OK;
  }

  AppendMenuString(menu, kMenuToggleInputMode,
                   ime_open_ ? MF_CHECKED : MF_UNCHECKED, L"한글 입력");
  AppendSeparator(menu);

  HMENU base_layout_menu =
      BuildBaseLayoutSubmenu(host_->layout_registry().physical_layouts(),
                             host_->current_physical_layout_id());
  if (base_layout_menu != nullptr &&
      !AppendMenuString(menu, reinterpret_cast<UINT_PTR>(base_layout_menu),
                        MF_POPUP, L"키보드 배열")) {
    DestroyMenu(base_layout_menu);
  }

  HMENU korean_layout_menu =
      BuildKoreanLayoutSubmenu(host_->layout_registry().korean_layouts(),
                               host_->current_korean_layout_id());
  if (korean_layout_menu != nullptr &&
      !AppendMenuString(menu, reinterpret_cast<UINT_PTR>(korean_layout_menu),
                        MF_POPUP, L"한글 자판")) {
    DestroyMenu(korean_layout_menu);
  }

  HWND owner = ResolvePopupOwner();
  TPMPARAMS params = {};
  TPMPARAMS* popup_params = nullptr;
  if (area != nullptr) {
    params.cbSize = sizeof(params);
    params.rcExclude = *area;
    popup_params = &params;
  }

  const UINT command = TrackPopupMenuEx(
      menu,
      TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY | TPM_RETURNCMD |
          TPM_LEFTBUTTON | TPM_RIGHTBUTTON | TPM_VERTICAL,
      point.x, point.y, owner, popup_params);
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][LangBar][PopupMenu][Command] command=" +
                  std::to_wstring(command));
#endif
  DestroyMenu(menu);

  if (command != 0) {
    OnMenuSelect(command);
  }
  return S_OK;
}

STDMETHODIMP InputModeLangBarItem::InitMenu(ITfMenu* menu) {
  (void)menu;
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][LangBar][InitMenu][Ignored] "
                  L"role=input_mode menu=" +
                  std::to_wstring(menu != nullptr ? 1 : 0));
#endif
  return S_OK;
}

STDMETHODIMP InputModeLangBarItem::OnMenuSelect(UINT menu_id) {
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][LangBar][OnMenuSelect] role=input_mode menu_id=" +
                  std::to_wstring(menu_id));
#endif
  if (host_ == nullptr) {
    return S_OK;
  }

  if (menu_id == kMenuToggleInputMode) {
    host_->ToggleImeModeFromLanguageBar();
    return S_OK;
  }

  if (menu_id >= kMenuBaseLayoutStart &&
      menu_id < kMenuBaseLayoutStart + 1000) {
    const std::size_t index = menu_id - kMenuBaseLayoutStart;
    const auto& layouts = host_->layout_registry().physical_layouts();
    if (index < layouts.size()) {
      host_->SelectPhysicalLayoutFromLanguageBar(layouts[index].id);
    }
    return S_OK;
  }

  if (menu_id >= kMenuKoreanLayoutStart &&
      menu_id < kMenuKoreanLayoutStart + 1000) {
    const std::size_t index = menu_id - kMenuKoreanLayoutStart;
    const auto& layouts = host_->layout_registry().korean_layouts();
    if (index < layouts.size()) {
      host_->SelectKoreanLayoutFromLanguageBar(layouts[index].id);
    }
    return S_OK;
  }

  return S_OK;
}

STDMETHODIMP InputModeLangBarItem::GetIcon(HICON* icon) {
  if (icon == nullptr) {
    return E_INVALIDARG;
  }

  *icon = LoadInputModeIcon(ime_open_);
  return S_OK;
}

STDMETHODIMP InputModeLangBarItem::GetText(BSTR* text) {
  if (text == nullptr) {
    return E_INVALIDARG;
  }

  *text = SysAllocString(text_.c_str());
  return *text == nullptr ? E_OUTOFMEMORY : S_OK;
}

STDMETHODIMP InputModeLangBarItem::AdviseSink(REFIID riid, IUnknown* punk,
                                              DWORD* cookie) {
  if (cookie == nullptr) {
    return E_INVALIDARG;
  }
  if (!IsEqualIID(riid, IID_ITfLangBarItemSink)) {
    return CONNECT_E_CANNOTCONNECT;
  }
  if (sink_ != nullptr) {
    return CONNECT_E_ADVISELIMIT;
  }
  if (punk == nullptr) {
    return E_INVALIDARG;
  }

  HRESULT hr = punk->QueryInterface(IID_ITfLangBarItemSink,
                                    reinterpret_cast<void**>(&sink_));
  if (FAILED(hr)) {
    sink_ = nullptr;
    return hr;
  }

  *cookie = kLangBarSinkCookie;
  return S_OK;
}

STDMETHODIMP InputModeLangBarItem::UnadviseSink(DWORD cookie) {
  if (cookie != kLangBarSinkCookie || sink_ == nullptr) {
    return CONNECT_E_NOCONNECTION;
  }

  sink_->Release();
  sink_ = nullptr;
  return S_OK;
}

HRESULT InputModeLangBarItem::AddToLanguageBar(ITfThreadMgr* thread_mgr) {
  if (thread_mgr == nullptr || added_to_language_bar_) {
    return thread_mgr == nullptr ? E_INVALIDARG : S_OK;
  }

  ITfLangBarItemMgr* item_manager = nullptr;
  const HRESULT hr = thread_mgr->QueryInterface(
      IID_ITfLangBarItemMgr, reinterpret_cast<void**>(&item_manager));
  if (FAILED(hr)) {
    return hr;
  }

  const HRESULT add_hr =
      item_manager->AddItem(static_cast<ITfLangBarItem*>(this));
  item_manager->Release();
  if (SUCCEEDED(add_hr)) {
    added_to_language_bar_ = true;
  }

#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][LangBar][AddToLanguageBar] role=input_mode hr=0x" +
                  HresultToHex(add_hr));
#endif

  return add_hr;
}

void InputModeLangBarItem::RemoveFromLanguageBar(ITfThreadMgr* thread_mgr) {
  if (thread_mgr == nullptr || !added_to_language_bar_) {
    return;
  }

  ITfLangBarItemMgr* item_manager = nullptr;
  if (SUCCEEDED(thread_mgr->QueryInterface(
          IID_ITfLangBarItemMgr, reinterpret_cast<void**>(&item_manager)))) {
    item_manager->RemoveItem(static_cast<ITfLangBarItem*>(this));
    item_manager->Release();
  }

#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][LangBar][RemoveFromLanguageBar] "
                  L"role=input_mode");
#endif

  added_to_language_bar_ = false;
}

void InputModeLangBarItem::OnImeOpenChanged(bool ime_open) {
  if (ime_open_ == ime_open) {
    return;
  }

  ime_open_ = ime_open;
  const std::wstring next_text = CurrentModeText(ime_open);
  text_ = next_text;
  NotifyUpdated();
}

void InputModeLangBarItem::NotifyUpdated() {
  if (sink_ != nullptr) {
    sink_->OnUpdate(TF_LBI_STATUS | TF_LBI_ICON | TF_LBI_TEXT);
  }
}

}  // namespace milkyway::tsf::langbar

#endif
