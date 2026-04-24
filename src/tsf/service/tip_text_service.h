#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <msctf.h>

#include <atomic>
#include <string>

#include "engine/key/normalized_key_event.h"
#include "adapters/libhangul/hangul_composer.h"
#include "engine/layout/layout_registry.h"
#include "engine/session/input_session.h"
#include "engine/shortcut/shortcut_resolver.h"
#include "engine/state/modifier_state.h"
#include "tsf/edit/tsf_text_edit_sink.h"
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

class TipTextService final : public ITfTextInputProcessorEx,
                             public ITfThreadMgrEventSink,
                             public ITfTextEditSink,
                             public ITfKeyEventSink,
                             public ITfCompositionSink,
                             public ITfThreadFocusSink,
                             public ITfCompartmentEventSink {
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

  TfClientId client_id() const;
  bool HasActiveComposition() const;
  HRESULT CommitText(TfEditCookie edit_cookie, ITfContext* context,
                     const std::wstring& text);
  HRESULT StartComposition(TfEditCookie edit_cookie, ITfContext* context,
                           const std::wstring& text);
  HRESULT UpdateComposition(TfEditCookie edit_cookie,
                            const std::wstring& text);
  HRESULT CompleteComposition(TfEditCookie edit_cookie);
  void ClearCompositionTracking();

 private:
  HRESULT AdviseThreadMgrEventSink();
  void UnadviseThreadMgrEventSink();
  HRESULT AdviseThreadFocusSink();
  void UnadviseThreadFocusSink();
  HRESULT AdviseKeyEventSink();
  void UnadviseKeyEventSink();
  HRESULT AdviseKeyboardOpenCloseCompartmentSink();
  void UnadviseKeyboardOpenCloseCompartmentSink();
  HRESULT RegisterPreservedKeys();
  void UnregisterPreservedKeys();
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
  bool FinalizeImeModeToggle(ITfContext* context, const wchar_t* origin);
  void ToggleImeModeFromLanguageBar();
  void HandleShortcutAction(ITfContext* context,
                            engine::shortcut::ShortcutAction action);
  HRESULT MoveSelectionToRangeEnd(TfEditCookie edit_cookie, ITfContext* context,
                                  ITfRange* range) const;
  void SyncCompositionTermination();
  void ClearPendingKeyResult();

  std::atomic<ULONG> ref_count_{1};
  ITfThreadMgr* thread_mgr_ = nullptr;
  TfClientId client_id_ = TF_CLIENTID_NULL;
  DWORD activate_flags_ = 0;
  DWORD thread_mgr_event_sink_cookie_ = TF_INVALID_COOKIE;
  DWORD thread_focus_sink_cookie_ = TF_INVALID_COOKIE;
  DWORD keyboard_openclose_compartment_sink_cookie_ = TF_INVALID_COOKIE;
  DWORD text_edit_sink_cookie_ = TF_INVALID_COOKIE;
  ITfContext* text_edit_sink_context_ = nullptr;
  ITfComposition* composition_ = nullptr;
  ITfContext* composition_context_ = nullptr;
  bool ime_open_ = true;
  bool deactivating_ = false;
  bool preserved_keys_registered_ = false;
  PendingKeyResult pending_key_result_;
  engine::layout::LayoutRegistry layout_registry_;
  engine::session::InputSession session_;
  edit::TsfTextEditSink edit_sink_;
  TextService logic_;
  ::milkyway::tsf::langbar::InputModeLangBarItem* input_mode_lang_bar_item_ =
      nullptr;

  friend class ::milkyway::tsf::langbar::InputModeLangBarItem;
};

}  // namespace milkyway::tsf::service

#endif
