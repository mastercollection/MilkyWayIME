#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <msctf.h>

#include <initializer_list>
#include <cstdint>
#include <span>
#include <string>

namespace milkyway::tsf::diagnostics {

enum class HostClassification {
  kStableTarget,
  kInlineTsfSupported,
  kTransitoryCuas,
  kUnstablePopupTarget,
  kSendInputPacketIgnored,
  kActivationUnknownOrFailed,
  kUnsupportedWriteChannel,
  kTransitoryCuasUnstableComposition,
};

struct HostSnapshot {
  void* context = nullptr;
  void* document_mgr = nullptr;
  void* top_context = nullptr;
  HWND view_hwnd = nullptr;
  HWND foreground_hwnd = nullptr;
  HWND active_hwnd = nullptr;
  HWND focus_hwnd = nullptr;
  HWND caret_hwnd = nullptr;
  HWND top_level_hwnd = nullptr;
  DWORD view_pid = 0;
  DWORD view_tid = 0;
  DWORD foreground_pid = 0;
  DWORD foreground_tid = 0;
  DWORD focus_pid = 0;
  DWORD focus_tid = 0;
  DWORD caret_pid = 0;
  DWORD caret_tid = 0;
  bool is_transitory = false;
  bool is_cuas_emulated = false;
  bool has_full_context = false;
  HRESULT active_view_hr = E_UNEXPECTED;
  HRESULT view_hwnd_hr = E_UNEXPECTED;
  std::wstring process_name;
  std::wstring view_class;
  std::wstring foreground_class;
  std::wstring focus_class;
  std::wstring caret_class;
  std::wstring top_level_class;
};

HostClassification ClassifyHost(std::span<const HostSnapshot> snapshots,
                                bool activation_observed,
                                bool external_empty_termination,
                                HRESULT write_hr);

HostClassification ClassifyHost(std::initializer_list<HostSnapshot> snapshots,
                                bool activation_observed,
                                bool external_empty_termination,
                                HRESULT write_hr);

const wchar_t* HostClassificationName(HostClassification classification);

#if defined(_DEBUG)
void SetCurrentKeyPathId(std::uint64_t key_path_id);
std::uint64_t CurrentKeyPathId();
#endif

HostSnapshot CaptureHostSnapshot(ITfThreadMgr* thread_mgr,
                                 ITfDocumentMgr* document_mgr,
                                 ITfContext* context);

#if defined(_DEBUG)
void LogHostSnapshot(const wchar_t* stage, const HostSnapshot& snapshot);
void LogHostClassification(const wchar_t* stage,
                           HostClassification classification,
                           HRESULT write_hr);
void LogWriteProbe(const wchar_t* stage, std::size_t text_utf16_length,
                   HRESULT hr);
void LogWriteProbeEx(const wchar_t* stage, std::size_t text_utf16_length,
                     HRESULT request_hr, HRESULT session_hr,
                     DWORD request_flags, const wchar_t* operation_types,
                     const wchar_t* planned_action_types,
                     bool used_resolved_context);
#endif

}  // namespace milkyway::tsf::diagnostics

#endif
