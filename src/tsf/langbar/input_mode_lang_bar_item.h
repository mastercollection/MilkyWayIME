#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <msctf.h>
#include <ctfutb.h>
#include <olectl.h>

#include <atomic>
#include <string>

namespace milkyway::tsf::service {
class TipTextService;
}

namespace milkyway::tsf::langbar {

class InputModeLangBarItem final : public ITfLangBarItemButton,
                                   public ITfSource {
 public:
  InputModeLangBarItem(service::TipTextService* host, bool ime_open);

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* info) override;
  STDMETHODIMP GetStatus(DWORD* status) override;
  STDMETHODIMP Show(BOOL show) override;
  STDMETHODIMP GetTooltipString(BSTR* tooltip) override;

  STDMETHODIMP OnClick(TfLBIClick click, POINT point,
                       const RECT* area) override;
  STDMETHODIMP InitMenu(ITfMenu* menu) override;
  STDMETHODIMP OnMenuSelect(UINT menu_id) override;
  STDMETHODIMP GetIcon(HICON* icon) override;
  STDMETHODIMP GetText(BSTR* text) override;

  STDMETHODIMP AdviseSink(REFIID riid, IUnknown* punk,
                          DWORD* cookie) override;
  STDMETHODIMP UnadviseSink(DWORD cookie) override;

  HRESULT AddToLanguageBar(ITfThreadMgr* thread_mgr);
  void RemoveFromLanguageBar(ITfThreadMgr* thread_mgr);
  void OnImeOpenChanged(bool ime_open);

 private:
  void NotifyUpdated();

  std::atomic<ULONG> ref_count_{1};
  service::TipTextService* host_ = nullptr;
  ITfLangBarItemSink* sink_ = nullptr;
  bool added_to_language_bar_ = false;
  bool ime_open_ = true;
  DWORD status_ = 0;
  std::wstring tooltip_ = L"MilkyWayIME 입력 모드";
  std::wstring text_;
};

}  // namespace milkyway::tsf::langbar

#endif
