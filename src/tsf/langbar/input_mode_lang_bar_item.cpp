#include "tsf/langbar/input_mode_lang_bar_item.h"

#if defined(_WIN32)

#include <strsafe.h>

#include <utility>

#include "tsf/registration/text_service_registration.h"
#include "tsf/resources/resource.h"
#include "tsf/service/module_state.h"
#include "tsf/service/tip_text_service.h"

namespace milkyway::tsf::langbar {
namespace {

constexpr DWORD kLangBarSinkCookie = 1;
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

HICON LoadInputModeIcon(bool ime_open) {
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
      module_instance, MAKEINTRESOURCEW(CurrentModeIconResourceId(ime_open)),
      IMAGE_ICON, width, height, LR_DEFAULTCOLOR));
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
                 L"MilkyWayIME Input Mode");
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

STDMETHODIMP InputModeLangBarItem::OnClick(TfLBIClick click, POINT,
                                           const RECT*) {
  if (click == TF_LBI_CLK_LEFT && host_ != nullptr) {
    host_->ToggleImeModeFromLanguageBar();
  }
  return S_OK;
}

STDMETHODIMP InputModeLangBarItem::InitMenu(ITfMenu*) {
  return S_OK;
}

STDMETHODIMP InputModeLangBarItem::OnMenuSelect(UINT) {
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
