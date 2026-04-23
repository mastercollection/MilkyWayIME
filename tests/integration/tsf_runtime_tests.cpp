#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <msctf.h>

#include <cassert>

#include "tsf/registration/text_service_registration.h"
#include "tsf/service/module_state.h"

namespace {

void TestClassFactoryAndInterfaces() {
  IClassFactory* factory = nullptr;
  HRESULT hr = milkyway::tsf::service::GetClassFactory(
      milkyway::tsf::registration::kTextServiceClsid, IID_IClassFactory,
      reinterpret_cast<void**>(&factory));
  assert(SUCCEEDED(hr));
  assert(factory != nullptr);

  IUnknown* unknown = nullptr;
  hr = factory->CreateInstance(nullptr, IID_IUnknown,
                               reinterpret_cast<void**>(&unknown));
  assert(SUCCEEDED(hr));
  assert(unknown != nullptr);

  ITfTextInputProcessorEx* tip_ex = nullptr;
  hr = unknown->QueryInterface(IID_ITfTextInputProcessorEx,
                               reinterpret_cast<void**>(&tip_ex));
  assert(SUCCEEDED(hr));
  assert(tip_ex != nullptr);
  tip_ex->Release();

  ITfKeyEventSink* key_sink = nullptr;
  hr = unknown->QueryInterface(IID_ITfKeyEventSink,
                               reinterpret_cast<void**>(&key_sink));
  assert(SUCCEEDED(hr));
  assert(key_sink != nullptr);
  key_sink->Release();

  ITfThreadMgrEventSink* thread_sink = nullptr;
  hr = unknown->QueryInterface(IID_ITfThreadMgrEventSink,
                               reinterpret_cast<void**>(&thread_sink));
  assert(SUCCEEDED(hr));
  assert(thread_sink != nullptr);
  thread_sink->Release();

  ITfTextEditSink* edit_sink = nullptr;
  hr = unknown->QueryInterface(IID_ITfTextEditSink,
                               reinterpret_cast<void**>(&edit_sink));
  assert(SUCCEEDED(hr));
  assert(edit_sink != nullptr);
  edit_sink->Release();

  ITfCompositionSink* composition_sink = nullptr;
  hr = unknown->QueryInterface(IID_ITfCompositionSink,
                               reinterpret_cast<void**>(&composition_sink));
  assert(SUCCEEDED(hr));
  assert(composition_sink != nullptr);
  composition_sink->Release();

  unknown->Release();
  factory->Release();
  assert(milkyway::tsf::service::CanUnloadNow() == S_OK);
}

}  // namespace

int main() {
  TestClassFactoryAndInterfaces();
  return 0;
}

#endif
