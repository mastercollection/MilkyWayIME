#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <msctf.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "adapters/dictionary/libhangul_hanja_dictionary.h"
#include "engine/key/normalized_key_event.h"
#include "adapters/libhangul/hangul_composer.h"
#include "engine/layout/layout_registry.h"
#include "engine/session/input_session.h"
#include "engine/shortcut/shortcut_resolver.h"
#include "engine/state/modifier_state.h"
#include "tsf/display/display_attribute.h"
#include "tsf/edit/nikke_direct_text_composition.h"
#include "tsf/edit/transitory_composition_bridge.h"
#include "tsf/edit/transitory_direct_text_composition.h"
#include "tsf/edit/tsf_text_edit_sink.h"
#include "tsf/candidate/candidate_list_ui.h"
#include "tsf/service/text_service.h"

namespace milkyway::tsf::langbar {
class InputModeLangBarItem;
}

namespace milkyway::tsf::service {

enum class EditSessionRequestPolicy {
  kKeyPathWrite,
  kSyncPreferredWrite,
};

struct PendingKeyResult {
  bool active = false;
  bool eaten = false;
  engine::key::NormalizedKeyEvent event;
};

enum class CandidateCommitTargetKind {
  kNone,
  kComposition,
  kSelectionRange,
  kCaretRange,
};

struct SelectionCandidateTarget {
  ITfContext* context = nullptr;
  ITfRange* range = nullptr;
  std::uint32_t matched_utf16_length = 0;
};

struct CaretCandidateTarget {
  ITfContext* context = nullptr;
  ITfRange* range = nullptr;
};

struct CaretCandidateSegment {
  ITfRange* range = nullptr;
  std::vector<engine::hanja::Candidate> candidates;
};

class TipTextService final : public ITfTextInputProcessorEx,
                             public ITfThreadMgrEventSink,
                             public ITfTextEditSink,
                             public ITfKeyEventSink,
                             public ITfCompositionSink,
                             public ITfThreadFocusSink,
                             public ITfCompartmentEventSink,
                             public ITfActiveLanguageProfileNotifySink,
                             public ITfDisplayAttributeProvider,
                             public candidate::CandidateListOwner {
 public:
  static HRESULT CreateInstance(IUnknown* outer, REFIID riid, void** ppv);

  TipTextService();
  ~TipTextService();

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  STDMETHODIMP Activate(ITfThreadMgr* thread_mgr, TfClientId client_id) override;
  STDMETHODIMP ActivateEx(ITfThreadMgr* thread_mgr, TfClientId client_id,
                          DWORD flags) override;
  STDMETHODIMP Deactivate() override;

  STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* document_mgr) override;
  STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* document_mgr) override;
  STDMETHODIMP OnSetFocus(ITfDocumentMgr* focused,
                          ITfDocumentMgr* previous) override;
  STDMETHODIMP OnPushContext(ITfContext* context) override;
  STDMETHODIMP OnPopContext(ITfContext* context) override;

  STDMETHODIMP OnEndEdit(ITfContext* context, TfEditCookie read_cookie,
                         ITfEditRecord* edit_record) override;

  STDMETHODIMP OnSetFocus(BOOL foreground) override;
  STDMETHODIMP OnTestKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam,
                             BOOL* eaten) override;
  STDMETHODIMP OnKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam,
                         BOOL* eaten) override;
  STDMETHODIMP OnTestKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam,
                           BOOL* eaten) override;
  STDMETHODIMP OnKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam,
                       BOOL* eaten) override;
  STDMETHODIMP OnPreservedKey(ITfContext* context, REFGUID guid,
                              BOOL* eaten) override;

  STDMETHODIMP OnCompositionTerminated(TfEditCookie write_cookie,
                                       ITfComposition* composition) override;

  STDMETHODIMP OnSetThreadFocus() override;
  STDMETHODIMP OnKillThreadFocus() override;

  STDMETHODIMP OnChange(REFGUID guid) override;

  STDMETHODIMP OnActivated(REFCLSID clsid, REFGUID profile,
                           BOOL activated) override;

  STDMETHODIMP EnumDisplayAttributeInfo(
      IEnumTfDisplayAttributeInfo** enum_info) override;
  STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid_info,
                                       ITfDisplayAttributeInfo** info) override;
  void OnCandidateListFinalize() override;
  void OnCandidateListAbort() override;

  TfClientId client_id() const;
  bool HasTrackedTsfComposition() const;
  HRESULT CommitText(TfEditCookie edit_cookie, ITfContext* context,
                     const std::wstring& text);
  HRESULT StartComposition(TfEditCookie edit_cookie, ITfContext* context,
                           const std::wstring& text);
  HRESULT UpdateComposition(TfEditCookie edit_cookie,
                            const std::wstring& text);
  HRESULT CompleteComposition(TfEditCookie edit_cookie);
  void ClearCompositionTracking();
  bool ShouldUseTransitoryDirectTextComposition(
      ITfContext* context,
      const std::vector<edit::TextEditOperation>& operations) const;
  ITfContext* ResolveTransitoryDirectTextContext(
      ITfContext* context,
      const std::vector<edit::TextEditOperation>& operations) const;
  HRESULT ApplyTransitoryDirectTextComposition(
      TfEditCookie edit_cookie, ITfContext* context,
      const std::vector<edit::TextEditOperation>& operations);
  void ResetTransitoryDirectTextComposition(const wchar_t* reason);
  bool ShouldUseNikkeDirectTextComposition(
      ITfContext* context,
      const std::vector<edit::TextEditOperation>& operations) const;
  HRESULT ApplyNikkeDirectTextComposition(
      TfEditCookie edit_cookie, ITfContext* context,
      const std::vector<edit::TextEditOperation>& operations);
  void ResetNikkeDirectTextComposition(const wchar_t* reason);

 private:
  HRESULT AdviseThreadMgrEventSink();
  void UnadviseThreadMgrEventSink();
  HRESULT AdviseThreadFocusSink();
  void UnadviseThreadFocusSink();
  HRESULT AdviseKeyEventSink();
  void UnadviseKeyEventSink();
  HRESULT AdviseKeyboardOpenCloseCompartmentSink();
  void UnadviseKeyboardOpenCloseCompartmentSink();
  HRESULT AdviseActiveLanguageProfileNotifySink();
  void UnadviseActiveLanguageProfileNotifySink();
  HRESULT RegisterPreservedKeys();
  void UnregisterPreservedKeys();
  HRESULT InitDisplayAttributeGuidAtom();
  HRESULT SyncImeOpenFromCompartment(const wchar_t* origin);
  HRESULT RefreshFocusedContext(ITfDocumentMgr* document_mgr = nullptr);
  HRESULT AttachTextEditSink(ITfContext* context);
  void DetachTextEditSink();
  HRESULT AddInputModeLanguageBarItem();
  void RemoveInputModeLanguageBarItem();
  HRESULT FlushPendingOperations(ITfContext* context,
                                 EditSessionRequestPolicy policy,
                                 const wchar_t* origin);
  engine::state::ModifierState QueryModifierState() const;
  engine::key::PhysicalKey BuildPhysicalKey(WPARAM wparam, LPARAM lparam) const;
  engine::key::NormalizedKeyEvent BuildNormalizedKeyEvent(
      WPARAM wparam, LPARAM lparam,
      const engine::state::ModifierState& modifiers,
      engine::key::KeyTransition transition) const;
  bool SelectionInsideComposition(ITfContext* context,
                                  TfEditCookie read_cookie) const;
  void SetImeOpen(bool open);
  const engine::layout::LayoutRegistry& layout_registry() const;
  const engine::layout::BaseLayoutId& current_base_layout_id() const;
  const engine::layout::KoreanLayoutId& current_korean_layout_id() const;
  void SelectBaseLayoutFromLanguageBar(
      const engine::layout::BaseLayoutId& base_layout_id);
  void SelectKoreanLayoutFromLanguageBar(
      const engine::layout::KoreanLayoutId& korean_layout_id);
  void SyncLayoutSelectionFromSettings(const wchar_t* origin);
  void ApplyLayoutSelection(
      engine::layout::BaseLayoutId base_layout_id,
      engine::layout::KoreanLayoutId korean_layout_id,
      const wchar_t* origin,
      bool persist_settings = true);
  bool FinalizeImeModeToggle(ITfContext* context, const wchar_t* origin);
  void ToggleImeModeFromLanguageBar();
  void HandleShortcutAction(ITfContext* context,
                            engine::shortcut::ShortcutAction action);
  bool HandleHanjaKey(ITfContext* context);
  bool HandleCompositionHanjaKey(ITfContext* context);
  bool HandleSelectionHanjaKey(ITfContext* context);
  bool HandleCaretHanjaKey(ITfContext* context);
  bool AdvanceHanjaCandidateSegment(ITfContext* context);
  bool OpenCandidateList(
      ITfContext* context,
      const std::vector<engine::hanja::Candidate>& candidates);
  bool OpenCaretSegmentCandidateList(ITfContext* context,
                                     std::size_t segment_index);
  void CloseCandidateList();
  void CloseCandidateListUi();
  void SetCompositionCandidateCommitTarget();
  void SetSelectionCandidateCommitTarget(ITfContext* context, ITfRange* range,
                                         std::uint32_t matched_utf16_length);
  void SetCaretCandidateCommitTarget(ITfContext* context, ITfRange* range);
  void ClearCandidateCommitTarget();
  void SetCaretCandidateSegments(ITfContext* context,
                                 std::vector<CaretCandidateSegment> segments);
  void ClearCaretCandidateSegments();
  bool ReadSelectionText(ITfContext* context, std::wstring* text,
                         ITfRange** range, bool* is_empty) const;
  bool ReadCaretTextBeforeSelection(ITfContext* context,
                                    std::wstring* text_before_caret,
                                    ITfRange** caret_range) const;
  bool CreateCaretCandidateRange(ITfContext* context, ITfRange* caret_range,
                                 std::uint32_t run_utf16_length,
                                 std::uint32_t start_utf16_offset,
                                 std::uint32_t matched_utf16_length,
                                 ITfRange** target_range) const;
  HRESULT CommitSelectionCandidate(const std::string& candidate_text,
                                   ITfContext* context, ITfRange* range,
                                   std::uint32_t matched_utf16_length) const;
  HRESULT CommitCaretCandidate(const std::string& candidate_text,
                               ITfContext* context, ITfRange* range) const;
  std::optional<POINT> CandidateWindowAnchor(ITfContext* context) const;
  std::optional<RECT> CompositionTextRect(ITfContext* context) const;
  std::optional<RECT> SelectionCandidateTextRect(ITfContext* context) const;
  std::optional<RECT> CaretCandidateTextRect(ITfContext* context) const;
  HRESULT MoveSelectionToRangeEnd(TfEditCookie edit_cookie, ITfContext* context,
                                  ITfRange* range) const;
  void ResetTransitoryCompositionBridge(const wchar_t* reason);
  HRESULT ClearCompositionDisplayAttribute(TfEditCookie edit_cookie,
                                           ITfContext* context) const;
  HRESULT ApplyCompositionDisplayAttribute(TfEditCookie edit_cookie,
                                           ITfContext* context,
                                           const std::wstring& text) const;
  void SyncCompositionTermination();
  void ClearPendingKeyResult();

  std::atomic<ULONG> ref_count_{1};
  ITfThreadMgr* thread_mgr_ = nullptr;
  TfClientId client_id_ = TF_CLIENTID_NULL;
  DWORD activate_flags_ = 0;
  DWORD thread_mgr_event_sink_cookie_ = TF_INVALID_COOKIE;
  DWORD thread_focus_sink_cookie_ = TF_INVALID_COOKIE;
  DWORD keyboard_openclose_compartment_sink_cookie_ = TF_INVALID_COOKIE;
  DWORD active_language_profile_notify_sink_cookie_ = TF_INVALID_COOKIE;
  DWORD text_edit_sink_cookie_ = TF_INVALID_COOKIE;
  ITfContext* text_edit_sink_context_ = nullptr;
  ITfComposition* composition_ = nullptr;
  ITfContext* composition_context_ = nullptr;
  bool ime_open_ = true;
  bool deactivating_ = false;
  bool preserved_keys_registered_ = false;
  TfGuidAtom composing_display_attribute_atom_ = TF_INVALID_GUIDATOM;
  PendingKeyResult pending_key_result_;
  engine::layout::LayoutRegistry layout_registry_;
  adapters::dictionary::LibhangulHanjaDictionary hanja_dictionary_;
  engine::session::InputSession session_;
  edit::TransitoryCompositionBridge transitory_composition_bridge_;
  edit::TransitoryDirectTextComposition transitory_direct_text_;
  edit::NikkeDirectTextComposition nikke_direct_text_;
  edit::TsfTextEditSink edit_sink_;
  TextService logic_;
  candidate::CandidateListUi* candidate_list_ = nullptr;
  CandidateCommitTargetKind candidate_commit_target_kind_ =
      CandidateCommitTargetKind::kNone;
  SelectionCandidateTarget selection_candidate_target_;
  CaretCandidateTarget caret_candidate_target_;
  ITfContext* caret_candidate_segments_context_ = nullptr;
  std::vector<CaretCandidateSegment> caret_candidate_segments_;
  std::size_t active_caret_candidate_segment_ = 0;
  ::milkyway::tsf::langbar::InputModeLangBarItem* input_mode_lang_bar_item_ =
      nullptr;

  friend class ::milkyway::tsf::langbar::InputModeLangBarItem;
};

}  // namespace milkyway::tsf::service

#endif
