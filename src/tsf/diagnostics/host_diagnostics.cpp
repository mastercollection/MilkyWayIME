#include "tsf/diagnostics/host_diagnostics.h"

#if defined(_WIN32)

#include <cstdint>
#include <sstream>

#if defined(_DEBUG)
#include "tsf/debug/debug_log.h"
#endif

namespace milkyway::tsf::diagnostics {
namespace {

#if defined(_DEBUG)
thread_local std::uint64_t g_current_key_path_id = 0;
#endif

template <typename T>
void SafeRelease(T*& pointer) {
  if (pointer != nullptr) {
    pointer->Release();
    pointer = nullptr;
  }
}

std::wstring FormatHex(std::uint32_t value) {
  wchar_t buffer[16] = {};
  swprintf_s(buffer, L"%08X", value);
  return buffer;
}

std::wstring PointerToString(const void* pointer) {
  wchar_t buffer[32] = {};
  swprintf_s(buffer, L"%p", pointer);
  return buffer;
}

std::wstring CurrentProcessName() {
  wchar_t buffer[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return L"<unknown-process>";
  }

  const wchar_t* file_name = wcsrchr(buffer, L'\\');
  if (file_name != nullptr && *(file_name + 1) != L'\0') {
    return file_name + 1;
  }

  return buffer;
}

std::wstring WindowClassName(HWND hwnd) {
  if (hwnd == nullptr) {
    return L"<none>";
  }

  wchar_t buffer[128] = {};
  const int length =
      GetClassNameW(hwnd, buffer, static_cast<int>(std::size(buffer)));
  if (length <= 0) {
    return std::wstring(L"<class-failed:") + FormatHex(GetLastError()) + L">";
  }

  return std::wstring(buffer, static_cast<std::size_t>(length));
}

void FillWindowIdentity(HWND hwnd, DWORD* pid, DWORD* tid,
                        std::wstring* class_name) {
  DWORD process_id = 0;
  const DWORD thread_id =
      hwnd != nullptr ? GetWindowThreadProcessId(hwnd, &process_id) : 0;
  if (pid != nullptr) {
    *pid = process_id;
  }
  if (tid != nullptr) {
    *tid = thread_id;
  }
  if (class_name != nullptr) {
    *class_name = WindowClassName(hwnd);
  }
}

bool IsContextTransitory(ITfContext* context) {
  if (context == nullptr) {
    return false;
  }

  TF_STATUS status = {};
  return SUCCEEDED(context->GetStatus(&status)) &&
         (status.dwStaticFlags & TS_SS_TRANSITORY) != 0;
}

bool IsCuasEmulatedContext(ITfContext* context) {
  if (context == nullptr) {
    return false;
  }

  ITfCompartmentMgr* compartment_manager = nullptr;
  HRESULT hr = context->QueryInterface(
      IID_ITfCompartmentMgr, reinterpret_cast<void**>(&compartment_manager));
  if (FAILED(hr) || compartment_manager == nullptr) {
    return false;
  }

  ITfCompartment* compartment = nullptr;
  hr = compartment_manager->GetCompartment(
      GUID_COMPARTMENT_TRANSITORYEXTENSION_PARENT, &compartment);
  SafeRelease(compartment_manager);
  if (FAILED(hr) || compartment == nullptr) {
    return false;
  }

  VARIANT value;
  VariantInit(&value);
  hr = compartment->GetValue(&value);
  const bool is_cuas = SUCCEEDED(hr) && value.vt == VT_UNKNOWN &&
                       value.punkVal != nullptr;
  VariantClear(&value);
  SafeRelease(compartment);
  return is_cuas;
}

bool HasFullWriteContext(ITfContext* context) {
  if (context == nullptr) {
    return false;
  }

  ITfInsertAtSelection* insert_at_selection = nullptr;
  const HRESULT insert_hr = context->QueryInterface(
      IID_ITfInsertAtSelection,
      reinterpret_cast<void**>(&insert_at_selection));
  SafeRelease(insert_at_selection);

  ITfContextComposition* context_composition = nullptr;
  const HRESULT composition_hr = context->QueryInterface(
      IID_ITfContextComposition,
      reinterpret_cast<void**>(&context_composition));
  SafeRelease(context_composition);

  return SUCCEEDED(insert_hr) && SUCCEEDED(composition_hr);
}

bool StableHandle(HWND left, HWND right) {
  return left == nullptr || right == nullptr || left == right;
}

bool HasUnstableTarget(std::span<const HostSnapshot> snapshots) {
  if (snapshots.size() < 2) {
    return false;
  }

  const HostSnapshot& first = snapshots.front();
  for (const HostSnapshot& snapshot : snapshots.subspan(1)) {
    if (first.context != nullptr && snapshot.context != nullptr &&
        first.context != snapshot.context) {
      return true;
    }
    if (!StableHandle(first.view_hwnd, snapshot.view_hwnd) ||
        !StableHandle(first.foreground_hwnd, snapshot.foreground_hwnd) ||
        !StableHandle(first.focus_hwnd, snapshot.focus_hwnd) ||
        !StableHandle(first.caret_hwnd, snapshot.caret_hwnd)) {
      return true;
    }
  }

  return false;
}

bool HasTransitoryCuas(std::span<const HostSnapshot> snapshots) {
  for (const HostSnapshot& snapshot : snapshots) {
    if (snapshot.is_transitory && snapshot.is_cuas_emulated &&
        !snapshot.has_full_context) {
      return true;
    }
  }
  return false;
}

}  // namespace

HostClassification ClassifyHost(std::span<const HostSnapshot> snapshots,
                                bool activation_observed,
                                bool external_empty_termination,
                                HRESULT write_hr) {
  if (!activation_observed || snapshots.empty()) {
    return HostClassification::kActivationUnknownOrFailed;
  }

  if (FAILED(write_hr)) {
    return HostClassification::kUnsupportedWriteChannel;
  }

  if (external_empty_termination) {
    return HostClassification::kTransitoryCuasUnstableComposition;
  }

  if (HasUnstableTarget(snapshots)) {
    return HostClassification::kUnstablePopupTarget;
  }

  if (HasTransitoryCuas(snapshots)) {
    return HostClassification::kTransitoryCuas;
  }

  return HostClassification::kStableTarget;
}

HostClassification ClassifyHost(std::initializer_list<HostSnapshot> snapshots,
                                bool activation_observed,
                                bool external_empty_termination,
                                HRESULT write_hr) {
  return ClassifyHost(std::span<const HostSnapshot>(snapshots.begin(),
                                                    snapshots.size()),
                      activation_observed, external_empty_termination,
                      write_hr);
}

const wchar_t* HostClassificationName(HostClassification classification) {
  switch (classification) {
    case HostClassification::kStableTarget:
      return L"stable_target";
    case HostClassification::kInlineTsfSupported:
      return L"inline_tsf_supported";
    case HostClassification::kTransitoryCuas:
      return L"transitory_cuas";
    case HostClassification::kUnstablePopupTarget:
      return L"unstable_popup_target";
    case HostClassification::kSendInputPacketIgnored:
      return L"sendinput_packet_ignored";
    case HostClassification::kActivationUnknownOrFailed:
      return L"activation_unknown_or_failed";
    case HostClassification::kUnsupportedWriteChannel:
      return L"unsupported_write_channel";
    case HostClassification::kTransitoryCuasUnstableComposition:
      return L"transitory_cuas_unstable_composition";
  }

  return L"unknown";
}

#if defined(_DEBUG)

void SetCurrentKeyPathId(std::uint64_t key_path_id) {
  g_current_key_path_id = key_path_id;
}

std::uint64_t CurrentKeyPathId() {
  return g_current_key_path_id;
}

#endif

HostSnapshot CaptureHostSnapshot(ITfThreadMgr* thread_mgr,
                                 ITfDocumentMgr* document_mgr,
                                 ITfContext* context) {
  HostSnapshot snapshot;
  snapshot.process_name = CurrentProcessName();
  snapshot.context = context;
  snapshot.document_mgr = document_mgr;
  ITfContext* top_context = nullptr;

  ITfDocumentMgr* resolved_document_mgr = document_mgr;
  bool release_document_mgr = false;
  if (resolved_document_mgr == nullptr && thread_mgr != nullptr) {
    if (SUCCEEDED(thread_mgr->GetFocus(&resolved_document_mgr))) {
      release_document_mgr = true;
      snapshot.document_mgr = resolved_document_mgr;
    }
  }

  if (resolved_document_mgr != nullptr) {
    if (SUCCEEDED(resolved_document_mgr->GetTop(&top_context))) {
      snapshot.top_context = top_context;
      if (snapshot.context == nullptr) {
        snapshot.context = top_context;
        context = top_context;
      }
    }
  }
  if (release_document_mgr) {
    SafeRelease(resolved_document_mgr);
  }

  if (context != nullptr) {
    snapshot.is_transitory = IsContextTransitory(context);
    snapshot.is_cuas_emulated = IsCuasEmulatedContext(context);
    snapshot.has_full_context = HasFullWriteContext(context);

    ITfContextView* view = nullptr;
    snapshot.active_view_hr = context->GetActiveView(&view);
    if (SUCCEEDED(snapshot.active_view_hr) && view != nullptr) {
      snapshot.view_hwnd_hr = view->GetWnd(&snapshot.view_hwnd);
      SafeRelease(view);
    }
  }

  FillWindowIdentity(snapshot.view_hwnd, &snapshot.view_pid,
                     &snapshot.view_tid, &snapshot.view_class);

  snapshot.foreground_hwnd = GetForegroundWindow();
  FillWindowIdentity(snapshot.foreground_hwnd, &snapshot.foreground_pid,
                     &snapshot.foreground_tid, &snapshot.foreground_class);

  GUITHREADINFO gui_thread_info = {};
  gui_thread_info.cbSize = sizeof(gui_thread_info);
  if (GetGUIThreadInfo(0, &gui_thread_info)) {
    snapshot.active_hwnd = gui_thread_info.hwndActive;
    snapshot.focus_hwnd = gui_thread_info.hwndFocus;
    snapshot.caret_hwnd = gui_thread_info.hwndCaret;
  }

  FillWindowIdentity(snapshot.focus_hwnd, &snapshot.focus_pid,
                     &snapshot.focus_tid, &snapshot.focus_class);
  FillWindowIdentity(snapshot.caret_hwnd, &snapshot.caret_pid,
                     &snapshot.caret_tid, &snapshot.caret_class);

  HWND ancestor_source = snapshot.view_hwnd != nullptr ? snapshot.view_hwnd
                                                       : snapshot.focus_hwnd;
  snapshot.top_level_hwnd =
      ancestor_source != nullptr ? GetAncestor(ancestor_source, GA_ROOT)
                                 : nullptr;
  snapshot.top_level_class = WindowClassName(snapshot.top_level_hwnd);

  SafeRelease(top_context);
  return snapshot;
}

#if defined(_DEBUG)

void LogHostSnapshot(const wchar_t* stage, const HostSnapshot& snapshot) {
  const wchar_t* safe_stage = stage != nullptr ? stage : L"<null>";
  debug::DebugLog(
      L"[HostDiagnostics][Snapshot] stage=" + std::wstring(safe_stage) +
      L" key_path_id=" + std::to_wstring(CurrentKeyPathId()) +
      L" process=" + snapshot.process_name +
      L" context=" + PointerToString(snapshot.context) +
      L" document_mgr=" + PointerToString(snapshot.document_mgr) +
      L" top_context=" + PointerToString(snapshot.top_context) +
      L" transitory=" + std::to_wstring(snapshot.is_transitory ? 1 : 0) +
      L" cuas_emulated=" +
      std::to_wstring(snapshot.is_cuas_emulated ? 1 : 0) +
      L" full_context=" +
      std::to_wstring(snapshot.has_full_context ? 1 : 0) +
      L" active_view_hr=0x" +
      FormatHex(static_cast<std::uint32_t>(snapshot.active_view_hr)) +
      L" view_hwnd_hr=0x" +
      FormatHex(static_cast<std::uint32_t>(snapshot.view_hwnd_hr)) +
      L" view_hwnd=" + PointerToString(snapshot.view_hwnd) +
      L" view_pid=" + std::to_wstring(snapshot.view_pid) +
      L" view_tid=" + std::to_wstring(snapshot.view_tid) +
      L" view_class=" + snapshot.view_class +
      L" foreground_hwnd=" + PointerToString(snapshot.foreground_hwnd) +
      L" foreground_pid=" + std::to_wstring(snapshot.foreground_pid) +
      L" foreground_tid=" + std::to_wstring(snapshot.foreground_tid) +
      L" foreground_class=" + snapshot.foreground_class +
      L" active_hwnd=" + PointerToString(snapshot.active_hwnd) +
      L" focus_hwnd=" + PointerToString(snapshot.focus_hwnd) +
      L" focus_pid=" + std::to_wstring(snapshot.focus_pid) +
      L" focus_tid=" + std::to_wstring(snapshot.focus_tid) +
      L" focus_class=" + snapshot.focus_class +
      L" caret_hwnd=" + PointerToString(snapshot.caret_hwnd) +
      L" caret_pid=" + std::to_wstring(snapshot.caret_pid) +
      L" caret_tid=" + std::to_wstring(snapshot.caret_tid) +
      L" caret_class=" + snapshot.caret_class +
      L" top_level_hwnd=" + PointerToString(snapshot.top_level_hwnd) +
      L" top_level_class=" + snapshot.top_level_class);
}

void LogHostClassification(const wchar_t* stage,
                           HostClassification classification,
                           HRESULT write_hr) {
  const wchar_t* safe_stage = stage != nullptr ? stage : L"<null>";
  debug::DebugLog(
      L"[HostDiagnostics][Classify] stage=" + std::wstring(safe_stage) +
      L" key_path_id=" + std::to_wstring(CurrentKeyPathId()) +
      L" process=" + CurrentProcessName() +
      L" classification=" + HostClassificationName(classification) +
      L" write_hr=0x" + FormatHex(static_cast<std::uint32_t>(write_hr)));
}

void LogWriteProbe(const wchar_t* stage, std::size_t text_utf16_length,
                   HRESULT hr) {
  const wchar_t* safe_stage = stage != nullptr ? stage : L"<null>";
  debug::DebugLog(
      L"[HostDiagnostics][WriteProbe] stage=" + std::wstring(safe_stage) +
      L" key_path_id=" + std::to_wstring(CurrentKeyPathId()) +
      L" process=" + CurrentProcessName() +
      L" text_utf16_length=" + std::to_wstring(text_utf16_length) +
      L" hr=0x" + FormatHex(static_cast<std::uint32_t>(hr)));
}

void LogWriteProbeEx(const wchar_t* stage, std::size_t text_utf16_length,
                     HRESULT request_hr, HRESULT session_hr,
                     DWORD request_flags, const wchar_t* operation_types,
                     const wchar_t* planned_action_types,
                     bool used_resolved_context) {
  const wchar_t* safe_stage = stage != nullptr ? stage : L"<null>";
  const wchar_t* safe_operations =
      operation_types != nullptr ? operation_types : L"";
  const wchar_t* safe_plan =
      planned_action_types != nullptr ? planned_action_types : L"";
  debug::DebugLog(
      L"[HostDiagnostics][WriteProbeEx] stage=" + std::wstring(safe_stage) +
      L" key_path_id=" + std::to_wstring(CurrentKeyPathId()) +
      L" process=" + CurrentProcessName() +
      L" text_utf16_length=" + std::to_wstring(text_utf16_length) +
      L" request_hr=0x" +
      FormatHex(static_cast<std::uint32_t>(request_hr)) +
      L" session_hr=0x" +
      FormatHex(static_cast<std::uint32_t>(session_hr)) +
      L" request_flags=0x" +
      FormatHex(static_cast<std::uint32_t>(request_flags)) +
      L" used_resolved_context=" +
      std::to_wstring(used_resolved_context ? 1 : 0) +
      L" operation_types=" + safe_operations +
      L" planned_action_types=" + safe_plan);
}

#endif

}  // namespace milkyway::tsf::diagnostics

#endif
