#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

namespace milkyway::tsf::service {

void SetModuleInstance(HINSTANCE instance);
HINSTANCE ModuleInstance();

void DllAddRef();
void DllRelease();

STDAPI GetClassFactory(REFCLSID rclsid, REFIID riid, void** ppv);
STDAPI CanUnloadNow();

}  // namespace milkyway::tsf::service

#endif
