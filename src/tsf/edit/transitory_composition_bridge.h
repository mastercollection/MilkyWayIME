#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <msctf.h>

#include <cstdint>
#include <string>

namespace milkyway::tsf::edit {

struct TransitoryCompositionBridgeTarget {
  std::wstring process_name;
  std::wstring view_class;
  bool is_transitory = false;
};

struct TransitoryCompositionBridgeSnapshot {
  TransitoryCompositionBridgeTarget target;
  ITfContext* context = nullptr;
  HWND view_hwnd = nullptr;
  bool internal_composing = false;
  bool has_tracked_tsf_composition = false;
  std::wstring preedit;
};

enum class TransitoryCompositionBridgeTargetKind {
  kNone,
  kObserveOnly,
  kSuppressEngineReset,
};

TransitoryCompositionBridgeTargetKind
GetTransitoryCompositionBridgeTargetKind(
    const TransitoryCompositionBridgeTarget& target);

bool ShouldSuppressTransitoryCompositionEngineReset(
    const TransitoryCompositionBridgeSnapshot& snapshot);

class TransitoryCompositionBridge final {
 public:
  bool IsActive() const;
  std::uint32_t suppressed_termination_count() const;

  bool ShouldSuppressEngineReset(
      const TransitoryCompositionBridgeSnapshot& snapshot) const;
  bool ShouldObserveTermination(
      const TransitoryCompositionBridgeSnapshot& snapshot) const;
  void NoteSuppressedEngineReset(
      const TransitoryCompositionBridgeSnapshot& snapshot,
      ITfComposition* terminated_composition);
  void NoteObservedTermination(
      const TransitoryCompositionBridgeSnapshot& snapshot,
      ITfComposition* terminated_composition);
  void Reset(const wchar_t* reason);

  std::wstring DebugState() const;

 private:
  bool active_ = false;
  std::uint32_t suppressed_termination_count_ = 0;
  ITfContext* context_identity_ = nullptr;
  HWND view_hwnd_ = nullptr;
  std::wstring process_name_;
  std::wstring view_class_;
  std::wstring last_preedit_;
};

}  // namespace milkyway::tsf::edit

#endif
