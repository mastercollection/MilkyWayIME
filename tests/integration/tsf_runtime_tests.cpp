#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <msctf.h>

#include <cassert>
#include <string>
#include <utility>
#include <vector>

#include "tsf/candidate/candidate_list_ui.h"
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

  ITfDisplayAttributeProvider* display_attribute_provider = nullptr;
  hr = unknown->QueryInterface(
      IID_ITfDisplayAttributeProvider,
      reinterpret_cast<void**>(&display_attribute_provider));
  assert(SUCCEEDED(hr));
  assert(display_attribute_provider != nullptr);

  IEnumTfDisplayAttributeInfo* display_attribute_enum = nullptr;
  hr = display_attribute_provider->EnumDisplayAttributeInfo(
      &display_attribute_enum);
  assert(SUCCEEDED(hr));
  assert(display_attribute_enum != nullptr);

  ITfDisplayAttributeInfo* display_attribute_info = nullptr;
  ULONG fetched = 0;
  hr = display_attribute_enum->Next(1, &display_attribute_info, &fetched);
  assert(hr == S_OK);
  assert(fetched == 1);
  assert(display_attribute_info != nullptr);

  TF_DISPLAYATTRIBUTE display_attribute = {};
  hr = display_attribute_info->GetAttributeInfo(&display_attribute);
  assert(SUCCEEDED(hr));
  assert(display_attribute.crText.type == TF_CT_NONE);
  assert(display_attribute.crBk.type == TF_CT_NONE);
  assert(display_attribute.lsStyle == TF_LS_DOT);
  assert(display_attribute.bAttr == TF_ATTR_INPUT);

  display_attribute_info->Release();
  display_attribute_enum->Release();
  display_attribute_provider->Release();

  unknown->Release();
  factory->Release();
  assert(milkyway::tsf::service::CanUnloadNow() == S_OK);
}

void TestCandidateListUiElement() {
  std::vector<milkyway::tsf::candidate::CandidateUiItem> items = {
      {"\xE9\x9F\x93", L"\x97D3"},
      {"\xE6\xBC\xA2", L"\x6F22"},
      {"\xE5\xAF\x92", L"\x5BD2"},
  };

  auto* candidate_list = new milkyway::tsf::candidate::CandidateListUi(
      nullptr, nullptr, nullptr, nullptr, std::move(items));
  assert(candidate_list != nullptr);

  ITfCandidateListUIElement* ui_element = nullptr;
  HRESULT hr = candidate_list->QueryInterface(
      IID_ITfCandidateListUIElement, reinterpret_cast<void**>(&ui_element));
  assert(SUCCEEDED(hr));
  assert(ui_element != nullptr);

  UINT count = 0;
  hr = ui_element->GetCount(&count);
  assert(SUCCEEDED(hr));
  assert(count == 3);

  BSTR text = nullptr;
  hr = ui_element->GetString(0, &text);
  assert(SUCCEEDED(hr));
  assert(text != nullptr);
  assert(std::wstring(text) == L"\x97D3");
  SysFreeString(text);

  ITfCandidateListUIElementBehavior* behavior = nullptr;
  hr = candidate_list->QueryInterface(
      IID_ITfCandidateListUIElementBehavior,
      reinterpret_cast<void**>(&behavior));
  assert(SUCCEEDED(hr));
  assert(behavior != nullptr);

  hr = behavior->SetSelection(1);
  assert(SUCCEEDED(hr));
  UINT selection = 0;
  hr = ui_element->GetSelection(&selection);
  assert(SUCCEEDED(hr));
  assert(selection == 1);

  UINT page_count = 0;
  UINT page_index[2] = {};
  hr = ui_element->GetPageIndex(page_index, 2, &page_count);
  assert(SUCCEEDED(hr));
  assert(page_count == 1);
  assert(page_index[0] == 0);

  behavior->Release();
  ui_element->Release();
  candidate_list->Release();
}

}  // namespace

int main() {
  TestClassFactoryAndInterfaces();
  TestCandidateListUiElement();
  return 0;
}

#endif
