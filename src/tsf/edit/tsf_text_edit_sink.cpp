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
#endif
#include "tsf/edit/text_edit_plan.h"
#include "tsf/service/tip_text_service.h"

namespace milkyway::tsf::edit {
namespace {

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

#endif

class WriteEditSession final : public ITfEditSession {
 public:
  WriteEditSession(service::TipTextService* host, ITfContext* context,
                   std::vector<TextEditOperation> operations)
      : host_(host), context_(context), operations_(std::move(operations)) {
    host_->AddRef();
    context_->AddRef();
  }

  ~WriteEditSession() {
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
    if (host_->ShouldUseTransitoryDirectTextComposition(context_, operations_)) {
      const HRESULT direct_hr =
          host_->ApplyTransitoryDirectTextComposition(edit_cookie, context_,
                                                     operations_);
      if (direct_hr != S_FALSE) {
        return direct_hr;
      }
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
};

HRESULT RequestWriteEditSession(service::TipTextService* host,
                                ITfContext* context,
                                std::vector<TextEditOperation> operations,
                                DWORD request_flags, bool* flushing,
                                HRESULT* session_hr) {
  if (host == nullptr || context == nullptr || flushing == nullptr ||
      session_hr == nullptr) {
    return E_INVALIDARG;
  }

  WriteEditSession* edit_session =
      new (std::nothrow) WriteEditSession(host, context, std::move(operations));
  if (edit_session == nullptr) {
    return E_OUTOFMEMORY;
  }

  *session_hr = E_FAIL;
  *flushing = true;
  const HRESULT hr = context->RequestEditSession(
      host->client_id(), edit_session, request_flags, session_hr);
  *flushing = false;

  edit_session->Release();
  return hr;
}

}  // namespace

TsfTextEditSink::TsfTextEditSink(service::TipTextService* host) : host_(host) {}

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
  return flushing_;
}

void TsfTextEditSink::ClearPendingOperations() {
  pending_operations_.clear();
}

HRESULT TsfTextEditSink::Flush(ITfContext* context, DWORD request_flags) {
  if (context == nullptr || pending_operations_.empty()) {
    return S_OK;
  }

#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][EditSink::Flush] pending_operations=" +
                  std::to_wstring(pending_operations_.size()) +
                  L" request_flags=0x" +
                  FormatHex(static_cast<std::uint32_t>(request_flags)));
#endif

  std::vector<TextEditOperation> operations = pending_operations_;

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
                                       std::move(operations), request_flags,
                                       &flushing_, &session_hr);

  if (FAILED(hr) && used_resolved_context) {
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
    std::vector<TextEditOperation> fallback_operations = pending_operations_;
    session_hr = E_FAIL;
    hr = RequestWriteEditSession(host_, request_context,
                                 std::move(fallback_operations), request_flags,
                                 &flushing_, &session_hr);
  }

  request_context->Release();
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
    return hr;
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

  pending_operations_.clear();
  return session_hr;
}

}  // namespace milkyway::tsf::edit

#endif
