#include "tsf/edit/tsf_text_edit_sink.h"

#if defined(_WIN32)

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_DEBUG)
#include "tsf/debug/debug_log.h"
#include "tsf/diagnostics/host_diagnostics.h"
#endif
#include "tsf/edit/text_edit_plan.h"
#include "tsf/service/tip_text_service.h"

namespace milkyway::tsf::edit {

// Shared with accepted asynchronous sessions. The host keeps the sink alive,
// but a detached context must not keep permission to edit a later composition.
struct WriteSessionState {
  ~WriteSessionState() { if (identity != nullptr) identity->Release(); }
  IUnknown* identity = nullptr;
  bool valid = true;
  unsigned requests = 0;
  std::shared_ptr<unsigned> executing = std::make_shared<unsigned>(0);
};

namespace {

struct WriteSessionResult {
  bool executed = false;
};

std::wstring FormatHex(std::uint32_t value) {
  wchar_t buffer[16] = {};
  swprintf_s(buffer, L"%08X", value);
  return buffer;
}

#if defined(_DEBUG)
std::wstring PointerToString(const void* pointer) {
  std::wostringstream stream;
  stream << std::uppercase << std::hex
         << reinterpret_cast<std::uintptr_t>(pointer);
  return stream.str();
}
#endif

std::wstring Utf8ToWide(const std::string& text) {
  if (text.empty()) {
    return {};
  }

  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         text.data(),
                                         static_cast<int>(text.size()), nullptr, 0);
  if (length <= 0) {
    return {};
  }

  std::wstring wide_text(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                      static_cast<int>(text.size()), wide_text.data(), length);
  return wide_text;
}

#if defined(_DEBUG)

const wchar_t* OperationTypeName(TextEditOperationType type) {
  switch (type) {
    case TextEditOperationType::kCommitText:
      return L"CommitText";
    case TextEditOperationType::kStartComposition:
      return L"StartComposition";
    case TextEditOperationType::kUpdateComposition:
      return L"UpdateComposition";
    case TextEditOperationType::kEndComposition:
      return L"EndComposition";
  }

  return L"Unknown";
}

const wchar_t* PlannedActionTypeName(PlannedEditActionType type) {
  switch (type) {
    case PlannedEditActionType::kCommitText:
      return L"CommitText";
    case PlannedEditActionType::kStartComposition:
      return L"StartComposition";
    case PlannedEditActionType::kUpdateComposition:
      return L"UpdateComposition";
    case PlannedEditActionType::kCompleteComposition:
      return L"CompleteComposition";
  }

  return L"Unknown";
}

std::wstring JoinOperationTypes(
    const std::vector<TextEditOperation>& operations) {
  std::wstring joined;
  for (const TextEditOperation& operation : operations) {
    if (!joined.empty()) {
      joined += L",";
    }
    joined += OperationTypeName(operation.type);
  }
  return joined;
}

std::wstring JoinPlannedActionTypes(
    const std::vector<PlannedEditAction>& actions) {
  std::wstring joined;
  for (const PlannedEditAction& action : actions) {
    if (!joined.empty()) {
      joined += L",";
    }
    joined += PlannedActionTypeName(action.type);
  }
  return joined;
}

#endif

class WriteEditSession final : public ITfEditSession {
 public:
  WriteEditSession(service::TipTextService* host, ITfContext* context,
                   std::vector<TextEditOperation> operations,
                   std::shared_ptr<WriteSessionState> state,
                   std::shared_ptr<WriteSessionResult> result)
      : host_(host), context_(context), operations_(std::move(operations)),
        state_(std::move(state)), result_(std::move(result)) {
    ++state_->requests;
    host_->AddRef();
    context_->AddRef();
  }

  ~WriteEditSession() {
    --state_->requests;
    context_->Release();
    host_->Release();
  }

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (ppv == nullptr) {
      return E_INVALIDARG;
    }

    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_ITfEditSession) {
      *ppv = static_cast<ITfEditSession*>(this);
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

  STDMETHODIMP DoEditSession(TfEditCookie edit_cookie) override {
    if (result_->executed) return E_UNEXPECTED;
    result_->executed = true;
    if (!state_->valid) {
#if defined(_DEBUG)
      diagnostics::LogWriteProbe(L"EditSession.StaleContext", 0, E_ABORT);
#endif
      return E_ABORT;
    }
    ++*state_->executing;
    const HRESULT execution_hr = Execute(edit_cookie);
    const HRESULT hr = !state_->valid && SUCCEEDED(execution_hr)
                           ? E_ABORT : execution_hr;
#if defined(_DEBUG)
    diagnostics::LogWriteProbe(L"EditSession.Completed", 0, hr);
#endif
    if (FAILED(hr) && state_->valid) {
      state_->valid = false;
      if (host_->CanEditCompositionInContext(context_)) {
        host_->CompleteComposition(edit_cookie);
      }
      host_->OnTextEditFailure(hr);
    }
    --*state_->executing;
    return hr;
  }

  HRESULT Execute(TfEditCookie edit_cookie) {
    if (!host_->CanEditCompositionInContext(context_)) return E_ABORT;
    if (host_->ShouldUseTransitoryDirectTextComposition(context_, operations_)) {
      const HRESULT direct_hr =
          host_->ApplyTransitoryDirectTextComposition(edit_cookie, context_,
                                                     operations_);
#if defined(_DEBUG)
      debug::DebugLog(
          L"[MilkyWayIME][EditSession][TransitoryDirectTextResult] hr=0x" +
          FormatHex(static_cast<std::uint32_t>(direct_hr)) +
          L" fallback_to_tsf_plan=" +
          std::to_wstring(direct_hr == S_FALSE ? 1 : 0));
#endif
      if (direct_hr != S_FALSE) {
        return direct_hr;
      }
    }

    const HRESULT window_only_hr =
        host_->ApplyWindowOnlyComposition(edit_cookie, context_, operations_);
    if (window_only_hr != S_FALSE) {
      return window_only_hr;
    }

    const HRESULT uia_hr =
        host_->ApplyUiaWholeValueComposition(context_, operations_);
    if (uia_hr != S_FALSE) {
      return uia_hr;
    }

    const std::vector<PlannedEditAction> plan =
        PlanTextEditActions(host_->HasTrackedTsfComposition(), operations_);

#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][EditSession] begin operations=" +
                    std::to_wstring(operations_.size()) + L" planned_actions=" +
                    std::to_wstring(plan.size()));

    for (const TextEditOperation& operation : operations_) {
      debug::DebugLog(L"[MilkyWayIME][EditSession][operation] type=" +
                      std::wstring(OperationTypeName(operation.type)) +
                      L" text=\"" + Utf8ToWide(operation.text) + L"\"");
    }

    for (const PlannedEditAction& action : plan) {
      debug::DebugLog(L"[MilkyWayIME][EditSession][plan] type=" +
                      std::wstring(PlannedActionTypeName(action.type)) +
                      L" text=\"" + Utf8ToWide(action.text) + L"\"");
    }
#endif

    for (const PlannedEditAction& action : plan) {
      if (!state_->valid) return E_ABORT;
      HRESULT hr = S_OK;

      switch (action.type) {
        case PlannedEditActionType::kCommitText:
          hr = host_->CommitText(edit_cookie, context_, Utf8ToWide(action.text));
          break;
        case PlannedEditActionType::kStartComposition:
          hr = host_->StartComposition(edit_cookie, context_,
                                       Utf8ToWide(action.text));
          break;
        case PlannedEditActionType::kUpdateComposition:
          hr = host_->UpdateComposition(edit_cookie, Utf8ToWide(action.text));
          break;
        case PlannedEditActionType::kCompleteComposition:
          hr = host_->CompleteComposition(edit_cookie);
          break;
      }

      if (FAILED(hr)) {
        return hr;
      }
    }

    return S_OK;
  }

 private:
  std::atomic<ULONG> ref_count_{1};
  service::TipTextService* host_ = nullptr;
  ITfContext* context_ = nullptr;
  std::vector<TextEditOperation> operations_;
  std::shared_ptr<WriteSessionState> state_;
  std::shared_ptr<WriteSessionResult> result_;
};

HRESULT RequestWriteEditSession(service::TipTextService* host,
                                ITfContext* context,
                                std::vector<TextEditOperation> operations,
                                DWORD request_flags,
                                const std::shared_ptr<WriteSessionState>& state,
                                const std::shared_ptr<WriteSessionResult>& result,
                                HRESULT* session_hr,
                                bool used_resolved_context) {
  if (host == nullptr || context == nullptr ||
      session_hr == nullptr) {
    return E_INVALIDARG;
  }

#if defined(_DEBUG)
  std::size_t total_text_length = 0;
  for (const TextEditOperation& operation : operations) {
    total_text_length += Utf8ToWide(operation.text).size();
  }
  const std::wstring operation_types = JoinOperationTypes(operations);
  const std::vector<PlannedEditAction> planned_actions =
      PlanTextEditActions(host->HasTrackedTsfComposition(), operations);
  const std::wstring planned_action_types =
      JoinPlannedActionTypes(planned_actions);
#endif

  WriteEditSession* edit_session =
      new (std::nothrow) WriteEditSession(host, context, std::move(operations),
                                       state, result);
  if (edit_session == nullptr) {
    return E_OUTOFMEMORY;
  }

  *session_hr = E_FAIL;
  ++*state->executing;
  const HRESULT hr = context->RequestEditSession(
      host->client_id(), edit_session, request_flags, session_hr);
  --*state->executing;
#if defined(_DEBUG)
  diagnostics::LogWriteProbe(L"RequestEditSession", total_text_length,
                             FAILED(hr) ? hr : *session_hr);
  diagnostics::LogWriteProbeEx(
      L"RequestEditSession", total_text_length, hr, *session_hr, request_flags,
      operation_types.c_str(), planned_action_types.c_str(),
      used_resolved_context);
#endif

  edit_session->Release();
  return hr;
}

}  // namespace

TsfTextEditSink::TsfTextEditSink(service::TipTextService* host)
    : host_(host), state_(std::make_shared<WriteSessionState>()) {}

TsfTextEditSink::~TsfTextEditSink() { state_->valid = false; }

void TsfTextEditSink::Apply(const TextEditOperation& operation) {
  pending_operations_.push_back(operation);

#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][EditSink::Apply] type=" +
                  std::wstring(OperationTypeName(operation.type)) + L" text=\"" +
                  Utf8ToWide(operation.text) + L"\"");
#endif
}

bool TsfTextEditSink::HasPendingOperations() const {
  return !pending_operations_.empty();
}

std::size_t TsfTextEditSink::PendingOperationCount() const {
  return pending_operations_.size();
}

bool TsfTextEditSink::is_flushing() const {
  return *state_->executing != 0;
}

void TsfTextEditSink::ClearPendingOperations() {
  pending_operations_.clear();
}

void TsfTextEditSink::InvalidatePendingEdits() {
  state_->valid = false;
  pending_operations_.clear();
  auto next = std::make_shared<WriteSessionState>();
  next->executing = state_->executing;
  state_ = std::move(next);
}

HRESULT TsfTextEditSink::Flush(ITfContext* context, DWORD request_flags) {
  if (pending_operations_.empty()) {
    return S_OK;
  }
  if (context == nullptr) return E_INVALIDARG;
  if (is_flushing()) return TF_E_SYNCHRONOUS;

  IUnknown* identity = nullptr;
  const HRESULT identity_hr = context->QueryInterface(
      IID_IUnknown, reinterpret_cast<void**>(&identity));
  if (FAILED(identity_hr)) return identity_hr;
  if (!state_->valid || (state_->identity != nullptr && state_->identity != identity)) {
    state_->valid = false;
    auto next = std::make_shared<WriteSessionState>();
    next->executing = state_->executing;
    state_ = std::move(next);
  }
  if (state_->identity == nullptr) state_->identity = identity;
  else identity->Release();
  const auto state = state_;
  const auto result = std::make_shared<WriteSessionResult>();
  // TSF can execute synchronous requests ahead of accepted asynchronous work.
  if (state->requests != 0) {
    request_flags = (request_flags & ~(TF_ES_SYNC | TF_ES_ASYNCDONTCARE)) | TF_ES_ASYNC;
  }

#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][EditSink::Flush] pending_operations=" +
                  std::to_wstring(pending_operations_.size()) +
                  L" request_flags=0x" +
                  FormatHex(static_cast<std::uint32_t>(request_flags)));
#endif

  std::vector<TextEditOperation> operations;
  operations.swap(pending_operations_);

  ITfContext* request_context =
      host_->ResolveTransitoryDirectTextContext(context, operations);
  const bool used_resolved_context = request_context != nullptr;
  if (request_context == nullptr) {
    request_context = context;
    request_context->AddRef();
  }

#if defined(_DEBUG)
  if (used_resolved_context) {
    debug::DebugLog(
        L"[MilkyWayIME][EditSink::Flush][ResolvedTransitoryContext] "
        L"original_context=" +
        PointerToString(context) + L" request_context=" +
        PointerToString(request_context));
  }
#endif

  HRESULT session_hr = E_FAIL;
  HRESULT hr = RequestWriteEditSession(host_, request_context,
                                       operations, request_flags,
                                       state, result, &session_hr,
                                       used_resolved_context);

  if (FAILED(hr) && used_resolved_context && !result->executed && state->valid) {
#if defined(_DEBUG)
    debug::DebugLog(
        L"[MilkyWayIME][EditSink::Flush][ResolvedContextRequestFailed] "
        L"request_flags=0x" +
        FormatHex(static_cast<std::uint32_t>(request_flags)) +
        L" original_context=" + PointerToString(context) +
        L" request_context=" + PointerToString(request_context) + L" hr=0x" +
        FormatHex(static_cast<std::uint32_t>(hr)) + L" session_hr=0x" +
        FormatHex(static_cast<std::uint32_t>(session_hr)));
#endif
    request_context->Release();
    request_context = context;
    request_context->AddRef();
    session_hr = E_FAIL;
    hr = RequestWriteEditSession(host_, request_context,
                                 operations, request_flags,
                                 state, result, &session_hr, false);
  }

  request_context->Release();
  const bool accepted = SUCCEEDED(hr) && SUCCEEDED(session_hr);
  if (!result->executed && !accepted && state->valid) {
    pending_operations_.insert(pending_operations_.begin(),
                               operations.begin(), operations.end());
  }
  if (FAILED(hr)) {
#if defined(_DEBUG)
    debug::DebugLog(L"[MilkyWayIME][EditSink::Flush][RequestEditSessionFailed] "
                    L"request_flags=0x" +
                    FormatHex(static_cast<std::uint32_t>(request_flags)) +
                    L" context=" + PointerToString(context) +
                    L" hr=0x" +
                    FormatHex(static_cast<std::uint32_t>(hr)) +
                    L" session_hr=0x" +
                    FormatHex(static_cast<std::uint32_t>(session_hr)));
#endif
    return result->executed ? E_FAIL : hr;
  }

#if defined(_DEBUG)
  if (FAILED(session_hr)) {
    debug::DebugLog(L"[MilkyWayIME][EditSink::Flush][EditSessionFailed] "
                    L"request_flags=0x" +
                    FormatHex(static_cast<std::uint32_t>(request_flags)) +
                    L" hr=0x" +
                    FormatHex(static_cast<std::uint32_t>(hr)) +
                    L" session_hr=0x" +
                    FormatHex(static_cast<std::uint32_t>(session_hr)));
  }
#endif

  // An executed batch may already have changed text. Never classify its failure
  // as a lock rejection that the caller can safely retry.
  return result->executed && FAILED(session_hr) ? E_FAIL : session_hr;
}

}  // namespace milkyway::tsf::edit

#endif
