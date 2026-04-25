#include "tsf/display/display_attribute.h"

#if defined(_WIN32)

#include <new>

#include "tsf/service/module_state.h"

namespace milkyway::tsf::display {
namespace {

constexpr ULONG kDisplayAttributeCount = 1;
constexpr wchar_t kComposingLastSyllableDescription[] =
    L"MilkyWayIME composing last syllable";

const TF_DISPLAYATTRIBUTE kComposingLastSyllableDisplayAttribute = {
    {TF_CT_NONE, 0},
    {TF_CT_NONE, 0},
    TF_LS_DOT,
    FALSE,
    {TF_CT_NONE, 0},
    TF_ATTR_INPUT,
};

class DisplayAttributeInfo final : public ITfDisplayAttributeInfo {
 public:
  DisplayAttributeInfo() { service::DllAddRef(); }
  ~DisplayAttributeInfo() { service::DllRelease(); }

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (ppv == nullptr) {
      return E_INVALIDARG;
    }

    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_ITfDisplayAttributeInfo) {
      *ppv = static_cast<ITfDisplayAttributeInfo*>(this);
    }

    if (*ppv == nullptr) {
      return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
  }

  STDMETHODIMP_(ULONG) AddRef() override { return ++ref_count_; }

  STDMETHODIMP_(ULONG) Release() override {
    const ULONG value = --ref_count_;
    if (value == 0) {
      delete this;
    }
    return value;
  }

  STDMETHODIMP GetGUID(GUID* guid) override {
    if (guid == nullptr) {
      return E_INVALIDARG;
    }

    *guid = kComposingLastSyllableDisplayAttributeGuid;
    return S_OK;
  }

  STDMETHODIMP GetDescription(BSTR* description) override {
    if (description == nullptr) {
      return E_INVALIDARG;
    }

    *description = SysAllocString(kComposingLastSyllableDescription);
    return *description != nullptr ? S_OK : E_OUTOFMEMORY;
  }

  STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* display_attribute) override {
    if (display_attribute == nullptr) {
      return E_INVALIDARG;
    }

    *display_attribute = kComposingLastSyllableDisplayAttribute;
    return S_OK;
  }

  STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE*) override {
    return E_NOTIMPL;
  }

  STDMETHODIMP Reset() override { return S_OK; }

 private:
  ULONG ref_count_ = 1;
};

class EnumDisplayAttributeInfo final : public IEnumTfDisplayAttributeInfo {
 public:
  EnumDisplayAttributeInfo() { service::DllAddRef(); }
  ~EnumDisplayAttributeInfo() { service::DllRelease(); }

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (ppv == nullptr) {
      return E_INVALIDARG;
    }

    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IEnumTfDisplayAttributeInfo) {
      *ppv = static_cast<IEnumTfDisplayAttributeInfo*>(this);
    }

    if (*ppv == nullptr) {
      return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
  }

  STDMETHODIMP_(ULONG) AddRef() override { return ++ref_count_; }

  STDMETHODIMP_(ULONG) Release() override {
    const ULONG value = --ref_count_;
    if (value == 0) {
      delete this;
    }
    return value;
  }

  STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** enum_info) override {
    if (enum_info == nullptr) {
      return E_INVALIDARG;
    }

    *enum_info = nullptr;
    auto* clone = new (std::nothrow) EnumDisplayAttributeInfo();
    if (clone == nullptr) {
      return E_OUTOFMEMORY;
    }

    clone->index_ = index_;
    *enum_info = clone;
    return S_OK;
  }

  STDMETHODIMP Next(ULONG count, ITfDisplayAttributeInfo** info,
                    ULONG* fetched) override {
    if (info == nullptr || (count > 1 && fetched == nullptr)) {
      return E_INVALIDARG;
    }

    for (ULONG i = 0; i < count; ++i) {
      info[i] = nullptr;
    }
    if (fetched != nullptr) {
      *fetched = 0;
    }

    ULONG fetched_count = 0;
    while (fetched_count < count && index_ < kDisplayAttributeCount) {
      HRESULT hr =
          CreateDisplayAttributeInfo(kComposingLastSyllableDisplayAttributeGuid,
                                     &info[fetched_count]);
      if (FAILED(hr)) {
        return hr;
      }

      ++fetched_count;
      ++index_;
    }

    if (fetched != nullptr) {
      *fetched = fetched_count;
    }
    return fetched_count == count ? S_OK : S_FALSE;
  }

  STDMETHODIMP Reset() override {
    index_ = 0;
    return S_OK;
  }

  STDMETHODIMP Skip(ULONG count) override {
    if (count > kDisplayAttributeCount - index_) {
      index_ = kDisplayAttributeCount;
      return S_FALSE;
    }

    index_ += count;
    return S_OK;
  }

 private:
  ULONG ref_count_ = 1;
  ULONG index_ = 0;
};

}  // namespace

const GUID kComposingLastSyllableDisplayAttributeGuid = {
    0x33bd775c,
    0x6214,
    0x4487,
    {0x92, 0x07, 0x37, 0x95, 0xd7, 0x1f, 0x01, 0xce},
};

HRESULT CreateEnumDisplayAttributeInfo(
    IEnumTfDisplayAttributeInfo** enum_info) {
  if (enum_info == nullptr) {
    return E_INVALIDARG;
  }

  *enum_info = new (std::nothrow) EnumDisplayAttributeInfo();
  return *enum_info != nullptr ? S_OK : E_OUTOFMEMORY;
}

HRESULT CreateDisplayAttributeInfo(REFGUID guid,
                                   ITfDisplayAttributeInfo** info) {
  if (info == nullptr) {
    return E_INVALIDARG;
  }

  *info = nullptr;
  if (!IsEqualGUID(guid, kComposingLastSyllableDisplayAttributeGuid)) {
    return E_INVALIDARG;
  }

  *info = new (std::nothrow) DisplayAttributeInfo();
  return *info != nullptr ? S_OK : E_OUTOFMEMORY;
}

HRESULT RegisterComposingLastSyllableDisplayAttributeAtom(TfGuidAtom* atom) {
  if (atom == nullptr) {
    return E_INVALIDARG;
  }

  *atom = TF_INVALID_GUIDATOM;
  ITfCategoryMgr* category_manager = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                                CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                reinterpret_cast<void**>(&category_manager));
  if (FAILED(hr)) {
    return hr;
  }

  hr = category_manager->RegisterGUID(
      kComposingLastSyllableDisplayAttributeGuid, atom);
  category_manager->Release();
  return hr;
}

}  // namespace milkyway::tsf::display

#endif
