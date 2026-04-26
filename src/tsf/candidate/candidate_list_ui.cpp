#include "tsf/candidate/candidate_list_ui.h"

#if defined(_WIN32)

#include <algorithm>
#include <utility>

namespace milkyway::tsf::candidate {
namespace {

constexpr GUID kCandidateUiElementGuid = {
    0xa1f1e0c4,
    0x8705,
    0x4d93,
    {0x8f, 0x91, 0x3d, 0x88, 0xe7, 0xa5, 0x41, 0x51},
};

bool IsDigitSelectionKey(WPARAM wparam, int* index) {
  if (index == nullptr) {
    return false;
  }

  if (wparam >= '1' && wparam <= '9') {
    *index = static_cast<int>(wparam - '1');
    return true;
  }

  if (wparam >= VK_NUMPAD1 && wparam <= VK_NUMPAD9) {
    *index = static_cast<int>(wparam - VK_NUMPAD1);
    return true;
  }

  return false;
}

std::vector<ui::candidate::CandidateWindowItem> ToWindowItems(
    const std::vector<CandidateUiItem>& items) {
  std::vector<ui::candidate::CandidateWindowItem> window_items;
  window_items.reserve(items.size());
  for (const CandidateUiItem& item : items) {
    window_items.push_back(ui::candidate::CandidateWindowItem{
        item.display_text,
    });
  }
  return window_items;
}

}  // namespace

CandidateListUi::CandidateListUi(ITfThreadMgr* thread_mgr,
                                 ITfDocumentMgr* document_mgr,
                                 HINSTANCE instance,
                                 CandidateListOwner* owner,
                                 std::vector<CandidateUiItem> items)
    : thread_mgr_(thread_mgr),
      document_mgr_(document_mgr),
      owner_(owner),
      items_(std::move(items)),
      window_(instance, this) {
  if (thread_mgr_ != nullptr) {
    thread_mgr_->AddRef();
  }
  if (document_mgr_ != nullptr) {
    document_mgr_->AddRef();
  }
  window_.SetItems(ToWindowItems(items_));
}

CandidateListUi::~CandidateListUi() {
  End();
  if (document_mgr_ != nullptr) {
    document_mgr_->Release();
    document_mgr_ = nullptr;
  }
  if (thread_mgr_ != nullptr) {
    thread_mgr_->Release();
    thread_mgr_ = nullptr;
  }
}

STDMETHODIMP CandidateListUi::QueryInterface(REFIID riid, void** ppv) {
  if (ppv == nullptr) {
    return E_INVALIDARG;
  }

  *ppv = nullptr;
  if (riid == IID_IUnknown || riid == IID_ITfUIElement ||
      riid == IID_ITfCandidateListUIElement) {
    *ppv = static_cast<ITfCandidateListUIElement*>(this);
  } else if (riid == IID_ITfCandidateListUIElementBehavior) {
    *ppv = static_cast<ITfCandidateListUIElementBehavior*>(this);
  }

  if (*ppv == nullptr) {
    return E_NOINTERFACE;
  }

  AddRef();
  return S_OK;
}

STDMETHODIMP_(ULONG) CandidateListUi::AddRef() {
  return ++ref_count_;
}

STDMETHODIMP_(ULONG) CandidateListUi::Release() {
  const ULONG value = --ref_count_;
  if (value == 0) {
    delete this;
  }
  return value;
}

STDMETHODIMP CandidateListUi::GetDescription(BSTR* description) {
  if (description == nullptr) {
    return E_POINTER;
  }

  *description = SysAllocString(L"MilkyWayIME Candidate List");
  return *description != nullptr ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CandidateListUi::GetGUID(GUID* guid) {
  if (guid == nullptr) {
    return E_POINTER;
  }

  *guid = kCandidateUiElementGuid;
  return S_OK;
}

STDMETHODIMP CandidateListUi::Show(BOOL show) {
  show_window_ = show != FALSE;
  if (!show_window_) {
    window_.Hide();
  }
  return S_OK;
}

STDMETHODIMP CandidateListUi::IsShown(BOOL* shown) {
  if (shown == nullptr) {
    return E_POINTER;
  }

  *shown = window_.IsShown() ? TRUE : FALSE;
  return S_OK;
}

STDMETHODIMP CandidateListUi::GetUpdatedFlags(DWORD* flags) {
  if (flags == nullptr) {
    return E_POINTER;
  }

  *flags = updated_flags_;
  return S_OK;
}

STDMETHODIMP CandidateListUi::GetDocumentMgr(ITfDocumentMgr** document_mgr) {
  if (document_mgr == nullptr) {
    return E_POINTER;
  }

  *document_mgr = document_mgr_;
  if (*document_mgr != nullptr) {
    (*document_mgr)->AddRef();
  }
  return S_OK;
}

STDMETHODIMP CandidateListUi::GetCount(UINT* candidate_count) {
  if (candidate_count == nullptr) {
    return E_POINTER;
  }

  *candidate_count = static_cast<UINT>(items_.size());
  return S_OK;
}

STDMETHODIMP CandidateListUi::GetSelection(UINT* selected_index) {
  if (selected_index == nullptr) {
    return E_POINTER;
  }

  *selected_index = static_cast<UINT>(selection_);
  return S_OK;
}

STDMETHODIMP CandidateListUi::GetString(UINT index, BSTR* text) {
  if (text == nullptr) {
    return E_POINTER;
  }

  *text = nullptr;
  if (index >= items_.size()) {
    return E_INVALIDARG;
  }

  const std::wstring& value = items_[index].display_text;
  *text = SysAllocStringLen(value.data(), static_cast<UINT>(value.size()));
  return *text != nullptr ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CandidateListUi::GetPageIndex(UINT* index, UINT size,
                                           UINT* page_count) {
  if (page_count == nullptr) {
    return E_POINTER;
  }

  const std::size_t count = PageCount();
  *page_count = static_cast<UINT>(count);
  if (index == nullptr) {
    return S_OK;
  }

  const std::size_t limit = std::min<std::size_t>(size, count);
  for (std::size_t page = 0; page < limit; ++page) {
    index[page] = static_cast<UINT>(page * kPageSize);
  }
  return S_OK;
}

STDMETHODIMP CandidateListUi::SetPageIndex(UINT* index, UINT page_count) {
  if (index == nullptr || page_count == 0) {
    return S_OK;
  }

  const std::size_t current_page = CurrentPage();
  if (current_page < page_count && index[current_page] < items_.size()) {
    SetSelectionIndex(index[current_page]);
  }
  return S_OK;
}

STDMETHODIMP CandidateListUi::GetCurrentPage(UINT* page) {
  if (page == nullptr) {
    return E_POINTER;
  }

  *page = static_cast<UINT>(CurrentPage());
  return S_OK;
}

STDMETHODIMP CandidateListUi::SetSelection(UINT index) {
  SetSelectionIndex(index);
  return S_OK;
}

STDMETHODIMP CandidateListUi::Finalize() {
  if (owner_ != nullptr) {
    AddRef();
    owner_->OnCandidateListFinalize();
    Release();
  }
  return S_OK;
}

STDMETHODIMP CandidateListUi::Abort() {
  if (owner_ != nullptr) {
    AddRef();
    owner_->OnCandidateListAbort();
    Release();
  }
  return S_OK;
}

bool CandidateListUi::Begin() {
  if (thread_mgr_ == nullptr) {
    return true;
  }

  ITfUIElementMgr* ui_element_mgr = nullptr;
  if (FAILED(thread_mgr_->QueryInterface(
          IID_ITfUIElementMgr, reinterpret_cast<void**>(&ui_element_mgr)))) {
    return true;
  }

  BOOL show = TRUE;
  const HRESULT hr = ui_element_mgr->BeginUIElement(
      static_cast<ITfUIElement*>(this), &show, &ui_element_id_);
  ui_element_mgr->Release();
  show_window_ = show != FALSE;
  return SUCCEEDED(hr);
}

void CandidateListUi::End() {
  window_.Hide();
  if (thread_mgr_ == nullptr || ui_element_id_ == static_cast<DWORD>(-1)) {
    return;
  }

  ITfUIElementMgr* ui_element_mgr = nullptr;
  if (SUCCEEDED(thread_mgr_->QueryInterface(
          IID_ITfUIElementMgr, reinterpret_cast<void**>(&ui_element_mgr)))) {
    ui_element_mgr->EndUIElement(ui_element_id_);
    ui_element_mgr->Release();
  }
  ui_element_id_ = static_cast<DWORD>(-1);
}

bool CandidateListUi::ShowAt(POINT anchor) {
  if (!show_window_) {
    return true;
  }

  return window_.Show(anchor);
}

CandidateKeyResult CandidateListUi::HandleVirtualKey(WPARAM wparam) {
  int digit_index = 0;
  if (IsDigitSelectionKey(wparam, &digit_index)) {
    const std::size_t index =
        CurrentPageStart() + static_cast<std::size_t>(digit_index);
    if (index < items_.size()) {
      SetSelectionIndex(index);
      return CandidateKeyResult::kCommit;
    }
    return CandidateKeyResult::kHandled;
  }

  switch (wparam) {
    case VK_LEFT:
    case VK_UP:
      if (selection_ > 0) {
        SetSelectionIndex(selection_ - 1);
      }
      return CandidateKeyResult::kHandled;
    case VK_RIGHT:
    case VK_DOWN:
      if (selection_ + 1 < items_.size()) {
        SetSelectionIndex(selection_ + 1);
      }
      return CandidateKeyResult::kHandled;
    case VK_PRIOR:
      if (CurrentPage() > 0) {
        SetSelectionIndex((CurrentPage() - 1) * kPageSize);
      }
      return CandidateKeyResult::kHandled;
    case VK_NEXT:
      if (CurrentPage() + 1 < PageCount()) {
        SetSelectionIndex((CurrentPage() + 1) * kPageSize);
      }
      return CandidateKeyResult::kHandled;
    case VK_RETURN:
    case VK_TAB:
      return CandidateKeyResult::kCommit;
    case VK_ESCAPE:
      return CandidateKeyResult::kCancel;
    default:
      return CandidateKeyResult::kIgnored;
  }
}

const CandidateUiItem* CandidateListUi::SelectedItem() const {
  if (selection_ >= items_.size()) {
    return nullptr;
  }

  return &items_[selection_];
}

bool CandidateListUi::empty() const {
  return items_.empty();
}

void CandidateListUi::OnCandidateWindowSelectionActivated(std::size_t index) {
  SetSelectionIndex(index);
  if (owner_ != nullptr) {
    AddRef();
    owner_->OnCandidateListFinalize();
    Release();
  }
}

void CandidateListUi::OnCandidateWindowSelectionChanged(std::size_t index) {
  SetSelectionIndex(index);
}

void CandidateListUi::NotifyUpdated(DWORD flags) {
  updated_flags_ = flags;
  if (thread_mgr_ == nullptr || ui_element_id_ == static_cast<DWORD>(-1)) {
    return;
  }

  ITfUIElementMgr* ui_element_mgr = nullptr;
  if (SUCCEEDED(thread_mgr_->QueryInterface(
          IID_ITfUIElementMgr, reinterpret_cast<void**>(&ui_element_mgr)))) {
    ui_element_mgr->UpdateUIElement(ui_element_id_);
    ui_element_mgr->Release();
  }
}

void CandidateListUi::SetSelectionIndex(std::size_t index) {
  if (items_.empty()) {
    selection_ = 0;
  } else {
    selection_ = std::min(index, items_.size() - 1);
  }
  window_.SetSelection(selection_);
  NotifyUpdated(TF_CLUIE_SELECTION | TF_CLUIE_CURRENTPAGE);
}

std::size_t CandidateListUi::PageCount() const {
  if (items_.empty()) {
    return 0;
  }

  return (items_.size() + kPageSize - 1) / kPageSize;
}

std::size_t CandidateListUi::CurrentPage() const {
  return items_.empty() ? 0 : selection_ / kPageSize;
}

std::size_t CandidateListUi::CurrentPageStart() const {
  return CurrentPage() * kPageSize;
}

bool IsCandidateModeVirtualKey(WPARAM wparam) {
  int digit_index = 0;
  return IsDigitSelectionKey(wparam, &digit_index) || wparam == VK_LEFT ||
         wparam == VK_RIGHT || wparam == VK_UP || wparam == VK_DOWN ||
         wparam == VK_PRIOR || wparam == VK_NEXT || wparam == VK_RETURN ||
         wparam == VK_TAB || wparam == VK_ESCAPE;
}

bool IsPureModifierVirtualKey(WPARAM wparam) {
  return wparam == VK_SHIFT || wparam == VK_LSHIFT || wparam == VK_RSHIFT ||
         wparam == VK_CONTROL || wparam == VK_LCONTROL ||
         wparam == VK_RCONTROL || wparam == VK_MENU || wparam == VK_LMENU ||
         wparam == VK_RMENU || wparam == VK_LWIN || wparam == VK_RWIN;
}

}  // namespace milkyway::tsf::candidate

#endif
