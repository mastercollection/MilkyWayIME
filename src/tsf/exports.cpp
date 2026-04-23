#include "tsf/registration/text_service_registration.h"
#include "tsf/service/module_state.h"

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    milkyway::tsf::service::SetModuleInstance(instance);
  }

  return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
  return milkyway::tsf::service::GetClassFactory(rclsid, riid, ppv);
}

STDAPI DllCanUnloadNow() {
  return milkyway::tsf::service::CanUnloadNow();
}

STDAPI DllRegisterServer() {
  return milkyway::tsf::registration::RegisterTextService();
}

STDAPI DllUnregisterServer() {
  return milkyway::tsf::registration::UnregisterTextService();
}
