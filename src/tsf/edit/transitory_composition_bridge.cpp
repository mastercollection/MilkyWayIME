#include "tsf/edit/transitory_composition_bridge.h"

#if defined(_WIN32)

#include <sstream>

#if defined(_DEBUG)
#include "tsf/debug/debug_log.h"
#endif

namespace milkyway::tsf::edit {
namespace {

std::wstring PointerToString(const void* pointer) {
  wchar_t buffer[32] = {};
  swprintf_s(buffer, L"%p", pointer);
  return buffer;
}

std::wstring TargetDebugText(const TransitoryCompositionBridgeSnapshot& snapshot) {
  return L" process=" + snapshot.target.process_name + L" view_class=" +
         snapshot.target.view_class + L" transitory=" +
         std::to_wstring(snapshot.target.is_transitory ? 1 : 0) +
         L" context=" + PointerToString(snapshot.context) + L" view_hwnd=" +
         PointerToString(snapshot.view_hwnd) + L" internal_composing=" +
         std::to_wstring(snapshot.internal_composing ? 1 : 0) +
         L" has_tracked_tsf_composition=" +
         std::to_wstring(snapshot.has_tracked_tsf_composition ? 1 : 0) +
         L" preedit=\"" + snapshot.preedit + L"\"";
}

}  // namespace

TransitoryCompositionBridgeTargetKind
GetTransitoryCompositionBridgeTargetKind(
    const TransitoryCompositionBridgeTarget& target) {
  if (target.is_transitory) {
    return TransitoryCompositionBridgeTargetKind::kSuppressEngineReset;
  }
  return TransitoryCompositionBridgeTargetKind::kNone;
}

bool ShouldSuppressTransitoryCompositionEngineReset(
    const TransitoryCompositionBridgeSnapshot& snapshot) {
  return snapshot.context != nullptr && snapshot.internal_composing &&
         snapshot.has_tracked_tsf_composition && snapshot.target.is_transitory;
}

bool TransitoryCompositionBridge::IsActive() const {
  return active_;
}

std::uint32_t TransitoryCompositionBridge::suppressed_termination_count() const {
  return suppressed_termination_count_;
}

bool TransitoryCompositionBridge::ShouldSuppressEngineReset(
    const TransitoryCompositionBridgeSnapshot& snapshot) const {
  return ShouldSuppressTransitoryCompositionEngineReset(snapshot);
}

bool TransitoryCompositionBridge::ShouldObserveTermination(
    const TransitoryCompositionBridgeSnapshot& snapshot) const {
  (void)snapshot;
  return false;
}

void TransitoryCompositionBridge::NoteSuppressedEngineReset(
    const TransitoryCompositionBridgeSnapshot& snapshot,
    ITfComposition* terminated_composition) {
  active_ = true;
  ++suppressed_termination_count_;
  context_identity_ = snapshot.context;
  view_hwnd_ = snapshot.view_hwnd;
  process_name_ = snapshot.target.process_name;
  view_class_ = snapshot.target.view_class;
  last_preedit_ = snapshot.preedit;

#if defined(_DEBUG)
  debug::DebugLog(
      L"[MilkyWayIME][TransitoryBridge][SuppressEngineReset]" +
      TargetDebugText(snapshot) + L" terminated_composition=" +
      PointerToString(terminated_composition) + L" suppress_count=" +
      std::to_wstring(suppressed_termination_count_));
#endif
}

void TransitoryCompositionBridge::NoteObservedTermination(
    const TransitoryCompositionBridgeSnapshot& snapshot,
    ITfComposition* terminated_composition) {
#if defined(_DEBUG)
  debug::DebugLog(L"[MilkyWayIME][TransitoryBridge][ObserveTermination]" +
                  TargetDebugText(snapshot) + L" terminated_composition=" +
                  PointerToString(terminated_composition));
#else
  (void)snapshot;
  (void)terminated_composition;
#endif
}

void TransitoryCompositionBridge::Reset(const wchar_t* reason) {
#if defined(_DEBUG)
  if (active_ || suppressed_termination_count_ != 0) {
    debug::DebugLog(
        L"[MilkyWayIME][TransitoryBridge][Reset] reason=" +
        std::wstring(reason != nullptr ? reason : L"<null>") +
        L" active=" + std::to_wstring(active_ ? 1 : 0) +
        L" suppress_count=" +
        std::to_wstring(suppressed_termination_count_) + L" context=" +
        PointerToString(context_identity_) + L" view_hwnd=" +
        PointerToString(view_hwnd_) + L" process=" + process_name_ +
        L" view_class=" + view_class_ + L" last_preedit=\"" +
        last_preedit_ + L"\"");
  }
#endif
  active_ = false;
  suppressed_termination_count_ = 0;
  context_identity_ = nullptr;
  view_hwnd_ = nullptr;
  process_name_.clear();
  view_class_.clear();
  last_preedit_.clear();
}

std::wstring TransitoryCompositionBridge::DebugState() const {
  return L"transitory_bridge_active=" + std::to_wstring(active_ ? 1 : 0) +
         L" transitory_bridge_suppress_count=" +
         std::to_wstring(suppressed_termination_count_) +
         L" transitory_bridge_context=" + PointerToString(context_identity_) +
         L" transitory_bridge_view_hwnd=" + PointerToString(view_hwnd_);
}

}  // namespace milkyway::tsf::edit

#endif
