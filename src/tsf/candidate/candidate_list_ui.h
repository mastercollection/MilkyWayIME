#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <msctf.h>

#include <atomic>
#include <string>
#include <vector>

#include "ui/candidate/candidate_window.h"

namespace milkyway::tsf::candidate {

struct CandidateUiItem {
  std::string value_utf8;
  std::wstring display_text;
};

enum class CandidateKeyResult {
  kIgnored,
  kHandled,
  kCommit,
  kCancel,
};

class CandidateListOwner {
 public:
  virtual ~CandidateListOwner() = default;
  virtual void OnCandidateListFinalize() = 0;
  virtual void OnCandidateListAbort() = 0;
};

class CandidateListUi final
    : public ITfCandidateListUIElementBehavior,
      public ui::candidate::CandidateWindowDelegate {
 public:
  CandidateListUi(ITfThreadMgr* thread_mgr, ITfDocumentMgr* document_mgr,
                  HINSTANCE instance, CandidateListOwner* owner,
                  std::vector<CandidateUiItem> items);
  ~CandidateListUi();

  CandidateListUi(const CandidateListUi&) = delete;
  CandidateListUi& operator=(const CandidateListUi&) = delete;

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  STDMETHODIMP GetDescription(BSTR* description) override;
  STDMETHODIMP GetGUID(GUID* guid) override;
  STDMETHODIMP Show(BOOL show) override;
  STDMETHODIMP IsShown(BOOL* shown) override;

  STDMETHODIMP GetUpdatedFlags(DWORD* flags) override;
  STDMETHODIMP GetDocumentMgr(ITfDocumentMgr** document_mgr) override;
  STDMETHODIMP GetCount(UINT* candidate_count) override;
  STDMETHODIMP GetSelection(UINT* selected_index) override;
  STDMETHODIMP GetString(UINT index, BSTR* text) override;
  STDMETHODIMP GetPageIndex(UINT* index, UINT size, UINT* page_count) override;
  STDMETHODIMP SetPageIndex(UINT* index, UINT page_count) override;
  STDMETHODIMP GetCurrentPage(UINT* page) override;

  STDMETHODIMP SetSelection(UINT index) override;
  STDMETHODIMP Finalize() override;
  STDMETHODIMP Abort() override;

  bool Begin();
  void End();
  bool ShowAt(POINT anchor);
  CandidateKeyResult HandleVirtualKey(WPARAM wparam);
  const CandidateUiItem* SelectedItem() const;
  bool empty() const;

  void OnCandidateWindowSelectionActivated(std::size_t index) override;
  void OnCandidateWindowSelectionChanged(std::size_t index) override;

 private:
  static constexpr UINT kPageSize = 9;

  void NotifyUpdated(DWORD flags);
  void SetSelectionIndex(std::size_t index);
  std::size_t PageCount() const;
  std::size_t CurrentPage() const;
  std::size_t CurrentPageStart() const;

  std::atomic<ULONG> ref_count_{1};
  ITfThreadMgr* thread_mgr_ = nullptr;
  ITfDocumentMgr* document_mgr_ = nullptr;
  CandidateListOwner* owner_ = nullptr;
  std::vector<CandidateUiItem> items_;
  ui::candidate::CandidateWindow window_;
  std::size_t selection_ = 0;
  DWORD updated_flags_ = 0;
  DWORD ui_element_id_ = static_cast<DWORD>(-1);
  bool show_window_ = true;
};

bool IsCandidateModeVirtualKey(WPARAM wparam);
bool IsPureModifierVirtualKey(WPARAM wparam);

}  // namespace milkyway::tsf::candidate

#endif
