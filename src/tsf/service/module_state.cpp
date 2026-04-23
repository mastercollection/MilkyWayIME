#include "tsf/service/module_state.h"

#if defined(_WIN32)

#include <atomic>

#include "tsf/registration/text_service_registration.h"
#include "tsf/service/tip_text_service.h"

namespace milkyway::tsf::service {
namespace {

class ClassFactory final : public IClassFactory {
 public:
  ClassFactory() {
    DllAddRef();
  }

  ~ClassFactory() {
    DllRelease();
  }

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (ppv == nullptr) {
      return E_INVALIDARG;
    }

    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
      *ppv = static_cast<IClassFactory*>(this);
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

  STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid,
                              void** ppv) override {
    return TipTextService::CreateInstance(outer, riid, ppv);
  }

  STDMETHODIMP LockServer(BOOL lock) override {
    if (lock) {
      DllAddRef();
    } else {
      DllRelease();
    }

    return S_OK;
  }

 private:
  std::atomic<ULONG> ref_count_{1};
};

std::atomic<long> g_dll_ref_count{0};
HINSTANCE g_module_instance = nullptr;

}  // namespace

void SetModuleInstance(HINSTANCE instance) {
  g_module_instance = instance;
}

HINSTANCE ModuleInstance() {
  return g_module_instance;
}

void DllAddRef() {
  ++g_dll_ref_count;
}

void DllRelease() {
  --g_dll_ref_count;
}

STDAPI GetClassFactory(REFCLSID rclsid, REFIID riid, void** ppv) {
  if (ppv == nullptr) {
    return E_INVALIDARG;
  }

  *ppv = nullptr;
  if (rclsid != registration::kTextServiceClsid) {
    return CLASS_E_CLASSNOTAVAILABLE;
  }

  ClassFactory* factory = new (std::nothrow) ClassFactory();
  if (factory == nullptr) {
    return E_OUTOFMEMORY;
  }

  const HRESULT hr = factory->QueryInterface(riid, ppv);
  factory->Release();
  return hr;
}

STDAPI CanUnloadNow() {
  return g_dll_ref_count.load() == 0 ? S_OK : S_FALSE;
}

}  // namespace milkyway::tsf::service

#endif
