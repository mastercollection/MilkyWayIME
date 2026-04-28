#include "tsf/registration/text_service_registration.h"

#if defined(_WIN32)

#include <strsafe.h>

#include <array>
#include <cstdint>
#include <string>

#include "tsf/service/module_state.h"

namespace milkyway::tsf::registration {
namespace {

const std::array<GUID, 8> kSupportedCategories = {
    GUID_TFCAT_TIP_KEYBOARD,
    GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
    GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
    GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
    GUID_TFCAT_TIPCAP_SECUREMODE,
    GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
    GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
    GUID_TFCAT_TIPCAP_COMLESS,
};
constexpr ULONG kTextServiceIconIndex = 0;
constexpr DWORD kInstallLayoutOrTipInstall = 0x00000000;
using InstallLayoutOrTipFn = BOOL(WINAPI*)(LPCWSTR, DWORD);

std::wstring GuidToString(REFGUID guid) {
  wchar_t buffer[39] = {};
  if (StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer))) == 0) {
    return {};
  }

  return buffer;
}

std::wstring ClsidRegistryPath() {
  return L"Software\\Classes\\CLSID\\" + GuidToString(kTextServiceClsid);
}

std::wstring LangidToTipString(LANGID langid) {
  wchar_t buffer[8] = {};
  swprintf_s(buffer, L"0x%04X", static_cast<std::uint16_t>(langid));
  return buffer;
}

std::wstring TextServiceTipString() {
  return LangidToTipString(kTextServiceLangid) + L":" +
         GuidToString(kTextServiceClsid) + GuidToString(kTextServiceProfileGuid);
}

HMODULE LoadSystemInputDll() {
  wchar_t system_directory[MAX_PATH] = {};
  const UINT length = GetSystemDirectoryW(system_directory, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return nullptr;
  }

  std::wstring input_dll_path(system_directory, length);
  if (!input_dll_path.empty() && input_dll_path.back() != L'\\') {
    input_dll_path.push_back(L'\\');
  }
  input_dll_path += L"input.dll";
  return LoadLibraryW(input_dll_path.c_str());
}

HRESULT InstallTextServiceLayoutOrTip() {
  const std::wstring tip = TextServiceTipString();
  if (tip.empty()) {
    return E_FAIL;
  }

  HMODULE input_dll = LoadSystemInputDll();
  if (input_dll == nullptr) {
    return HRESULT_FROM_WIN32(GetLastError());
  }

  auto* install_layout_or_tip = reinterpret_cast<InstallLayoutOrTipFn>(
      GetProcAddress(input_dll, "InstallLayoutOrTip"));
  if (install_layout_or_tip == nullptr) {
    const DWORD error = GetLastError();
    FreeLibrary(input_dll);
    return HRESULT_FROM_WIN32(error);
  }

  const BOOL installed =
      install_layout_or_tip(tip.c_str(), kInstallLayoutOrTipInstall);
  const DWORD error = installed ? ERROR_SUCCESS : GetLastError();
  FreeLibrary(input_dll);
  return installed ? S_OK
                   : (error == ERROR_SUCCESS ? E_FAIL
                                             : HRESULT_FROM_WIN32(error));
}

void DebugRegistrationFailure(const wchar_t* operation, HRESULT hr) {
#if defined(_DEBUG)
  wchar_t buffer[192] = {};
  StringCchPrintfW(buffer, std::size(buffer),
                   L"[MilkyWayIME][RegisterTextService][%ls] hr=0x%08lX\n",
                   operation, static_cast<unsigned long>(hr));
  OutputDebugStringW(buffer);
#else
  (void)operation;
  (void)hr;
#endif
}

HRESULT RegisterServer() {
  const std::wstring module_path = ModulePath(service::ModuleInstance());
  if (module_path.empty()) {
    return E_FAIL;
  }

  HKEY clsid_key = nullptr;
  HKEY inproc_key = nullptr;

  const std::wstring clsid_path = ClsidRegistryPath();
  RegDeleteTreeW(HKEY_CURRENT_USER, clsid_path.c_str());

  LONG status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, clsid_path.c_str(), 0, nullptr,
                                REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                                &clsid_key, nullptr);
  if (status != ERROR_SUCCESS) {
    return HRESULT_FROM_WIN32(status);
  }

  status = RegSetValueExW(
      clsid_key, nullptr, 0, REG_SZ,
      reinterpret_cast<const BYTE*>(kTextServiceDescription),
      static_cast<DWORD>((wcslen(kTextServiceDescription) + 1) *
                         sizeof(wchar_t)));
  if (status == ERROR_SUCCESS) {
    status = RegCreateKeyExW(clsid_key, L"InProcServer32", 0, nullptr,
                             REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                             &inproc_key, nullptr);
  }
  if (status == ERROR_SUCCESS) {
    status = RegSetValueExW(
        inproc_key, nullptr, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(module_path.c_str()),
        static_cast<DWORD>((module_path.size() + 1) * sizeof(wchar_t)));
  }
  if (status == ERROR_SUCCESS) {
    status = RegSetValueExW(
        inproc_key, L"ThreadingModel", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(kThreadingModel),
        static_cast<DWORD>((wcslen(kThreadingModel) + 1) * sizeof(wchar_t)));
  }

  if (inproc_key != nullptr) {
    RegCloseKey(inproc_key);
  }
  if (clsid_key != nullptr) {
    RegCloseKey(clsid_key);
  }

  return HRESULT_FROM_WIN32(status);
}

void UnregisterServer() {
  RegDeleteTreeW(HKEY_LOCAL_MACHINE, ClsidRegistryPath().c_str());
  RegDeleteTreeW(HKEY_CURRENT_USER, ClsidRegistryPath().c_str());
}

HRESULT RegisterProfiles() {
  ITfInputProcessorProfileMgr* profile_manager = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_ITfInputProcessorProfileMgr,
                                reinterpret_cast<void**>(&profile_manager));
  if (FAILED(hr)) {
    return hr;
  }

  const std::wstring module_path = ModulePath(service::ModuleInstance());
  hr = profile_manager->RegisterProfile(
      kTextServiceClsid, kTextServiceLangid, kTextServiceProfileGuid,
      kTextServiceDescription,
      static_cast<ULONG>(wcslen(kTextServiceDescription)),
      module_path.c_str(), static_cast<ULONG>(module_path.size()),
      kTextServiceIconIndex, nullptr, 0, TRUE, 0);

  profile_manager->Release();
  return hr;
}

void UnregisterProfiles() {
  ITfInputProcessorProfileMgr* profile_manager = nullptr;
  const HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_ITfInputProcessorProfileMgr,
                                      reinterpret_cast<void**>(&profile_manager));
  if (FAILED(hr)) {
    return;
  }

  profile_manager->UnregisterProfile(kTextServiceClsid, kTextServiceLangid,
                                     kTextServiceProfileGuid, 0);
  profile_manager->Release();
}

HRESULT RegisterCategories() {
  ITfCategoryMgr* category_manager = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                IID_ITfCategoryMgr,
                                reinterpret_cast<void**>(&category_manager));
  if (FAILED(hr)) {
    return hr;
  }

  for (const GUID& category : kSupportedCategories) {
    hr = category_manager->RegisterCategory(kTextServiceClsid, category,
                                            kTextServiceClsid);
    if (FAILED(hr)) {
      break;
    }
  }

  category_manager->Release();
  return hr;
}

void UnregisterCategories() {
  ITfCategoryMgr* category_manager = nullptr;
  const HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                                      CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                      reinterpret_cast<void**>(&category_manager));
  if (FAILED(hr)) {
    return;
  }

  for (const GUID& category : kSupportedCategories) {
    category_manager->UnregisterCategory(kTextServiceClsid, category,
                                         kTextServiceClsid);
  }

  category_manager->Release();
}

}  // namespace

HRESULT RegisterTextService() {
  const HRESULT init_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool uninitialize =
      SUCCEEDED(init_hr) || init_hr == RPC_E_CHANGED_MODE;

  HRESULT hr = RegisterServer();
  if (SUCCEEDED(hr)) {
    hr = RegisterProfiles();
  }
  if (SUCCEEDED(hr)) {
    hr = RegisterCategories();
  }
  if (SUCCEEDED(hr)) {
    // InstallLayoutOrTip updates the current user's input list and can fail
    // from elevated registration even after the machine-wide TSF registration
    // succeeds. Keep the COM/profile registration intact and log the failure.
    const HRESULT install_layout_hr = InstallTextServiceLayoutOrTip();
    if (FAILED(install_layout_hr)) {
      DebugRegistrationFailure(L"InstallLayoutOrTip", install_layout_hr);
    }
  }
  if (FAILED(hr)) {
    UnregisterCategories();
    UnregisterProfiles();
    UnregisterServer();
  }

  if (uninitialize && init_hr != RPC_E_CHANGED_MODE) {
    CoUninitialize();
  }

  return hr;
}

HRESULT UnregisterTextService() {
  const HRESULT init_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool uninitialize =
      SUCCEEDED(init_hr) || init_hr == RPC_E_CHANGED_MODE;

  UnregisterProfiles();
  UnregisterCategories();
  UnregisterServer();

  if (uninitialize && init_hr != RPC_E_CHANGED_MODE) {
    CoUninitialize();
  }

  return S_OK;
}

std::wstring ModulePath(HINSTANCE instance) {
  if (instance == nullptr) {
    return {};
  }

  wchar_t buffer[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(instance, buffer, MAX_PATH);
  if (length == 0 || length == MAX_PATH) {
    return {};
  }

  return std::wstring(buffer, length);
}

}  // namespace milkyway::tsf::registration

#endif
