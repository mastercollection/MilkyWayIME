#if defined(_WIN32)
#include <windows.h>
#include <msctf.h>

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

#include "tsf/edit/tsf_text_edit_sink.h"
#include "tsf/service/tip_text_service.h"

namespace {
using milkyway::tsf::edit::TsfTextEditSink;
using milkyway::tsf::edit::TextEditOperationType;
using milkyway::tsf::service::TipTextService;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}

class TestRange final : public ITfRange {
 public:
  std::function<void()> on_set;
  STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
    *out = nullptr;
    if (iid != IID_IUnknown && iid != IID_ITfRange) return E_NOINTERFACE;
    *out = static_cast<ITfRange*>(this); AddRef(); return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
  STDMETHODIMP_(ULONG) Release() override { auto n = --refs_; if (!n) delete this; return n; }
  STDMETHODIMP SetText(TfEditCookie, DWORD, const WCHAR*, LONG) override {
    if (on_set) on_set();
    return S_OK;
  }
  STDMETHODIMP Clone(ITfRange** out) override { *out = this; AddRef(); return S_OK; }
  STDMETHODIMP Collapse(TfEditCookie, TfAnchor) override { return S_OK; }
  STDMETHODIMP GetText(TfEditCookie, DWORD, WCHAR*, ULONG, ULONG* count) override { *count = 0; return S_OK; }
  STDMETHODIMP GetFormattedText(TfEditCookie, IDataObject** out) override { *out = nullptr; return E_NOTIMPL; }
  STDMETHODIMP GetEmbedded(TfEditCookie, REFGUID, REFIID, IUnknown** out) override { *out = nullptr; return E_NOTIMPL; }
  STDMETHODIMP InsertEmbedded(TfEditCookie, DWORD, IDataObject*) override { return E_NOTIMPL; }
  STDMETHODIMP ShiftStart(TfEditCookie, LONG, LONG*, const TF_HALTCOND*) override { return E_NOTIMPL; }
  STDMETHODIMP ShiftEnd(TfEditCookie, LONG, LONG*, const TF_HALTCOND*) override { return E_NOTIMPL; }
  STDMETHODIMP ShiftStartToRange(TfEditCookie, ITfRange*, TfAnchor) override { return E_NOTIMPL; }
  STDMETHODIMP ShiftEndToRange(TfEditCookie, ITfRange*, TfAnchor) override { return E_NOTIMPL; }
  STDMETHODIMP ShiftStartRegion(TfEditCookie, TfShiftDir, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP ShiftEndRegion(TfEditCookie, TfShiftDir, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP IsEmpty(TfEditCookie, BOOL* out) override { *out = FALSE; return S_OK; }
  STDMETHODIMP IsEqualStart(TfEditCookie, ITfRange*, TfAnchor, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP IsEqualEnd(TfEditCookie, ITfRange*, TfAnchor, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP CompareStart(TfEditCookie, ITfRange*, TfAnchor, LONG*) override { return E_NOTIMPL; }
  STDMETHODIMP CompareEnd(TfEditCookie, ITfRange*, TfAnchor, LONG*) override { return E_NOTIMPL; }
  STDMETHODIMP AdjustForInsert(TfEditCookie, ULONG, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP GetGravity(TfGravity*, TfGravity*) override { return E_NOTIMPL; }
  STDMETHODIMP SetGravity(TfEditCookie, TfGravity, TfGravity) override { return E_NOTIMPL; }
  STDMETHODIMP GetContext(ITfContext** out) override { *out = nullptr; return E_NOTIMPL; }
 private:
  ULONG refs_ = 1;
};

class TestComposition final : public ITfComposition {
 public:
  TestRange* range = new TestRange();
  int ends = 0;
  ~TestComposition() { range->Release(); }
  STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
    *out = nullptr;
    if (iid != IID_IUnknown && iid != IID_ITfComposition) return E_NOINTERFACE;
    *out = static_cast<ITfComposition*>(this); AddRef(); return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
  STDMETHODIMP_(ULONG) Release() override { auto n = --refs_; if (!n) delete this; return n; }
  STDMETHODIMP GetRange(ITfRange** out) override { *out = range; range->AddRef(); return S_OK; }
  STDMETHODIMP ShiftStart(TfEditCookie, ITfRange*) override { return E_NOTIMPL; }
  STDMETHODIMP ShiftEnd(TfEditCookie, ITfRange*) override { return E_NOTIMPL; }
  STDMETHODIMP EndComposition(TfEditCookie) override { ++ends; return S_OK; }
 private:
  ULONG refs_ = 1;
};

// A deterministic TSF scheduler; it never accesses the user's text or IME profile.
class TestContext final : public ITfContext,
                          public ITfInsertAtSelection,
                          public ITfContextComposition {
 public:
  enum class Mode { kReject, kRun, kDefer };
  Mode mode = Mode::kReject;
  HRESULT write_result = TS_E_READONLY;
  std::function<void()> during_write;
  std::vector<DWORD> flags;
  std::vector<ITfEditSession*> deferred;
  int writes = 0;
  int starts = 0;
  TestComposition* accepted_composition = nullptr;
  std::wstring text;

  HRESULT RunNext() {
    auto* session = deferred.front();
    deferred.erase(deferred.begin());
    const HRESULT hr = session->DoEditSession(1);
    session->Release();
    return hr;
  }
  STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
    *out = nullptr;
    if (iid == IID_IUnknown || iid == IID_ITfContext)
      *out = static_cast<ITfContext*>(this);
    else if (iid == IID_ITfInsertAtSelection)
      *out = static_cast<ITfInsertAtSelection*>(this);
    else if (iid == IID_ITfContextComposition)
      *out = static_cast<ITfContextComposition*>(this);
    else return E_NOINTERFACE;
    AddRef();
    return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
  STDMETHODIMP_(ULONG) Release() override {
    const auto refs = --refs_;
    if (!refs) delete this;
    return refs;
  }
  STDMETHODIMP RequestEditSession(TfClientId, ITfEditSession* session,
                                  DWORD request_flags, HRESULT* result) override {
    flags.push_back(request_flags);
    if (mode == Mode::kReject) *result = TF_E_SYNCHRONOUS;
    else if (mode == Mode::kDefer) {
      session->AddRef();
      deferred.push_back(session);
      *result = TF_S_ASYNC;
    } else *result = session->DoEditSession(1);
    return S_OK;
  }
  STDMETHODIMP InsertTextAtSelection(TfEditCookie, DWORD insert_flags,
                                     const WCHAR* input, LONG length, ITfRange** range) override {
    *range = nullptr;
    if (insert_flags & TF_IAS_QUERYONLY) { *range = new TestRange(); return S_OK; }
    ++writes;
    if (during_write) during_write();
    if (SUCCEEDED(write_result)) {
      text.append(input, length);
      *range = new TestRange();
    }
    return write_result;
  }
  STDMETHODIMP StartComposition(TfEditCookie, ITfRange*, ITfCompositionSink*,
                                ITfComposition** composition) override {
    ++starts;
    *composition = nullptr;
    if (accepted_composition != nullptr) {
      *composition = accepted_composition;
      accepted_composition->AddRef();
    }
    return S_OK;
  }
  STDMETHODIMP InWriteSession(TfClientId, BOOL* value) override { *value = FALSE; return S_OK; }
  STDMETHODIMP GetSelection(TfEditCookie, ULONG, ULONG, TF_SELECTION*, ULONG* count) override { *count = 0; return E_NOTIMPL; }
  STDMETHODIMP SetSelection(TfEditCookie, ULONG, const TF_SELECTION*) override { return S_OK; }
  STDMETHODIMP GetStart(TfEditCookie, ITfRange** out) override { *out = nullptr; return E_NOTIMPL; }
  STDMETHODIMP GetEnd(TfEditCookie, ITfRange** out) override { *out = nullptr; return E_NOTIMPL; }
  STDMETHODIMP GetActiveView(ITfContextView** out) override { *out = nullptr; return E_NOTIMPL; }
  STDMETHODIMP EnumViews(IEnumTfContextViews** out) override { *out = nullptr; return E_NOTIMPL; }
  STDMETHODIMP GetStatus(TF_STATUS* out) override { *out = {}; return S_OK; }
  STDMETHODIMP GetProperty(REFGUID, ITfProperty** out) override { *out = nullptr; return E_NOTIMPL; }
  STDMETHODIMP GetAppProperty(REFGUID, ITfReadOnlyProperty** out) override { *out = nullptr; return E_NOTIMPL; }
  STDMETHODIMP TrackProperties(const GUID**, ULONG, const GUID**, ULONG, ITfReadOnlyProperty** out) override { *out = nullptr; return E_NOTIMPL; }
  STDMETHODIMP EnumProperties(IEnumTfProperties** out) override { *out = nullptr; return E_NOTIMPL; }
  STDMETHODIMP GetDocumentMgr(ITfDocumentMgr** out) override { *out = nullptr; return E_NOTIMPL; }
  STDMETHODIMP CreateRangeBackup(TfEditCookie, ITfRange*, ITfRangeBackup** out) override { *out = nullptr; return E_NOTIMPL; }
  STDMETHODIMP InsertEmbeddedAtSelection(TfEditCookie, DWORD, IDataObject*, ITfRange** out) override { *out = nullptr; return E_NOTIMPL; }
  STDMETHODIMP EnumCompositions(IEnumITfCompositionView** out) override { *out = nullptr; return E_NOTIMPL; }
  STDMETHODIMP FindComposition(TfEditCookie, ITfRange*, IEnumITfCompositionView** out) override { *out = nullptr; return E_NOTIMPL; }
  STDMETHODIMP TakeOwnership(TfEditCookie, ITfCompositionView*, ITfCompositionSink*, ITfComposition** out) override { *out = nullptr; return E_NOTIMPL; }
 private:
  ULONG refs_ = 1;
};
}

void TestEditSessionTransactions() {
  auto* host = new TipTextService();
  auto* context = new TestContext();
  {
    TsfTextEditSink sink(host);
    sink.Apply({TextEditOperationType::kEndComposition, {}});
    Check(sink.Flush(context, TF_ES_SYNC | TF_ES_READWRITE) == TF_E_SYNCHRONOUS,
          "sync rejection must be returned");
    Check(sink.PendingOperationCount() == 1, "rejected request must retain work");
    context->mode = TestContext::Mode::kDefer;
    Check(sink.Flush(context, TF_ES_ASYNC | TF_ES_READWRITE) == TF_S_ASYNC,
          "retry must submit the retained work");
    Check(!sink.HasPendingOperations(), "accepted snapshot must leave pending queue");
    Check(SUCCEEDED(context->RunNext()), "deferred completion");

    context->mode = TestContext::Mode::kRun;
    sink.Apply({TextEditOperationType::kCommitText, "a"});
    context->during_write = [&] {
      Check(sink.is_flushing(), "execution must hold the flushing guard");
      sink.Apply({TextEditOperationType::kEndComposition, {}});
    };
    Check(FAILED(sink.Flush(context, TF_ES_SYNC | TF_ES_READWRITE)), "write failure");
    Check(sink.PendingOperationCount() == 1, "new reentrant work must not be erased");
    context->during_write = {};
    Check(SUCCEEDED(sink.Flush(context, TF_ES_SYNC | TF_ES_READWRITE)), "new work flush");
    Check(context->writes == 1, "failed executed batch must not be replayed");

    Check(FAILED(host->StartComposition(1, context, L"a")),
          "S_OK with null composition is host rejection, not success");
    Check(context->starts == 1, "host rejection path was exercised");

    context->write_result = S_OK;
    context->mode = TestContext::Mode::kDefer;
    sink.Apply({TextEditOperationType::kCommitText, "a"});
    Check(sink.Flush(context, TF_ES_ASYNC | TF_ES_READWRITE) == TF_S_ASYNC, "first queued");
    sink.Apply({TextEditOperationType::kCommitText, "b"});
    Check(sink.Flush(context, TF_ES_SYNC | TF_ES_READWRITE) == TF_S_ASYNC, "second queued");
    Check((context->flags.back() & TF_ES_ASYNC) != 0 &&
          (context->flags.back() & TF_ES_SYNC) == 0, "sync must not overtake queued work");
    context->during_write = [&] { Check(sink.is_flushing(), "async execution guard"); };
    Check(SUCCEEDED(context->RunNext()), "first completed");
    Check(SUCCEEDED(context->RunNext()), "second completed");
    Check(context->text == L"ab", "queued text delivered exactly once, in order");
    context->during_write = {};

    sink.Apply({TextEditOperationType::kCommitText, "stale"});
    sink.Flush(context, TF_ES_ASYNC | TF_ES_READWRITE);
    sink.InvalidatePendingEdits();
    Check(context->RunNext() == E_ABORT, "invalidated edit must abort");
    Check(context->text == L"ab", "invalidated edit must not write");

    sink.Apply({TextEditOperationType::kCommitText, "old"});
    sink.Flush(context, TF_ES_ASYNC | TF_ES_READWRITE);
    auto* next_context = new TestContext();
    next_context->mode = TestContext::Mode::kRun;
    next_context->write_result = S_OK;
    sink.Apply({TextEditOperationType::kCommitText, "new"});
    Check(SUCCEEDED(sink.Flush(next_context, TF_ES_SYNC | TF_ES_READWRITE)), "new context write");
    Check(context->RunNext() == E_ABORT, "old context callback must abort");
    Check(context->text == L"ab" && next_context->text == L"new", "context isolation");
    next_context->Release();

    sink.Apply({TextEditOperationType::kCommitText, "once"});
    sink.Apply({TextEditOperationType::kStartComposition, "rejected"});
    sink.Flush(context, TF_ES_ASYNC | TF_ES_READWRITE);
    sink.Apply({TextEditOperationType::kCommitText, "must-not-follow-failure"});
    sink.Flush(context, TF_ES_ASYNC | TF_ES_READWRITE);
    Check(FAILED(context->RunNext()), "partially executed batch reports failure");
    Check(context->RunNext() == E_ABORT, "later dependent batch must be invalidated");
    Check(context->text == L"abonce", "partial success must neither replay nor execute its successor");

    const int starts_before_cancel = context->starts;
    context->mode = TestContext::Mode::kRun;
    context->during_write = [&] { sink.InvalidatePendingEdits(); };
    sink.Apply({TextEditOperationType::kCommitText, "before-cancel"});
    sink.Apply({TextEditOperationType::kStartComposition, "after-cancel"});
    Check(FAILED(sink.Flush(context, TF_ES_SYNC | TF_ES_READWRITE)), "mid-batch cancellation");
    Check(context->starts == starts_before_cancel, "cancellation must stop remaining actions");
    context->during_write = {};
    context->mode = TestContext::Mode::kDefer;

    auto* old_composition = new TestComposition();
    auto* new_composition = new TestComposition();
    context->accepted_composition = new_composition;
    Check(SUCCEEDED(host->StartComposition(1, context, L"a")), "start tracked composition");
    host->OnCompositionTerminated(1, old_composition);
    Check(host->HasTrackedTsfComposition(), "stale termination must not clear new composition");
    host->OnCompositionTerminated(1, new_composition);
    Check(!host->HasTrackedTsfComposition(), "current termination must clear tracking");
    new_composition->range->on_set = [&] { host->OnCompositionTerminated(1, new_composition); };
    Check(FAILED(host->StartComposition(1, context, L"a")), "termination during SetText must fail");
    Check(!host->HasTrackedTsfComposition(), "terminated composition must not be resurrected");
    new_composition->range->on_set = {};
    context->accepted_composition = old_composition;
    Check(SUCCEEDED(host->StartComposition(1, context, L"a")), "old focus composition");
    host->OnKillThreadFocus();
    Check(!host->HasTrackedTsfComposition(), "focus loss detaches old tracking");
    context->accepted_composition = new_composition;
    Check(SUCCEEDED(host->StartComposition(1, context, L"b")), "new focus composition");
    Check(SUCCEEDED(context->RunNext()), "old context finalization runs later");
    Check(old_composition->ends == 1 && new_composition->ends == 0,
          "delayed finalization must end only the captured old composition");
    Check(host->HasTrackedTsfComposition(), "new composition survives old finalization");
    host->CompleteComposition(1);
    context->accepted_composition = nullptr;
    old_composition->Release();
    new_composition->Release();
  }
  context->Release();
  host->Release();
}
#endif
