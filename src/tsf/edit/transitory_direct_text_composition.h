#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <msctf.h>

#include <string>
#include <utility>
#include <vector>

#include "tsf/edit/text_edit_sink.h"
#include "tsf/host/host_text_policy.h"

namespace milkyway::tsf::edit {

struct TransitoryDirectTextTarget {
  TransitoryDirectTextTarget() = default;
  TransitoryDirectTextTarget(std::wstring process, std::wstring view,
                             bool transitory)
      : process_name(std::move(process)),
        view_class(std::move(view)),
        is_transitory(transitory) {}

  std::wstring process_name;
  std::wstring view_class;
  std::wstring focus_class;
  std::wstring top_level_class;
  bool is_transitory = false;
};

struct TransitoryDirectTextOperationPlan {
  std::string commit_text;
  std::string preedit_text;
  bool has_preedit = false;
  bool end_requested = false;
  bool has_composition_operation = false;
};

inline TransitoryDirectTextOperationPlan BuildTransitoryDirectTextOperationPlan(
    const std::vector<TextEditOperation>& operations) {
  TransitoryDirectTextOperationPlan plan;
  for (const TextEditOperation& operation : operations) {
    switch (operation.type) {
      case TextEditOperationType::kCommitText:
        plan.commit_text += operation.text;
        break;
      case TextEditOperationType::kStartComposition:
      case TextEditOperationType::kUpdateComposition:
        plan.preedit_text = operation.text;
        plan.has_preedit = true;
        plan.has_composition_operation = true;
        break;
      case TextEditOperationType::kEndComposition:
        plan.end_requested = true;
        break;
    }
  }
  return plan;
}

inline bool IsTransitoryDirectTextTarget(
    const TransitoryDirectTextTarget& target) {
  return target.is_transitory;
}

inline bool CanUseWin32SelectionReplacementForTransitoryDirectText(
    const TransitoryDirectTextTarget& target) {
  milkyway::tsf::host::HostTextTarget host_target;
  host_target.process_name = target.process_name;
  host_target.view_class = target.view_class;
  host_target.focus_class = target.focus_class;
  host_target.top_level_class = target.top_level_class;
  host_target.is_transitory = target.is_transitory;
  return milkyway::tsf::host::SelectHostTextBackendPolicy(host_target) ==
         milkyway::tsf::host::HostTextBackendPolicy::
             kNativeEditSelectionReplace;
}

inline bool CanUseRichEditRangeReplacementForTransitoryDirectText(
    const TransitoryDirectTextTarget& target) {
  milkyway::tsf::host::HostTextTarget host_target;
  host_target.process_name = target.process_name;
  host_target.view_class = target.view_class;
  host_target.focus_class = target.focus_class;
  host_target.top_level_class = target.top_level_class;
  host_target.is_transitory = target.is_transitory;
  return milkyway::tsf::host::SelectHostTextBackendPolicy(host_target) ==
         milkyway::tsf::host::HostTextBackendPolicy::
             kNativeRichEditSelectionReplace;
}

inline bool ShouldUseHostBackspaceReplacementForTransitoryDirectText(
    const TransitoryDirectTextTarget&) {
  return false;
}

inline bool HasVerifiedNativeReplacementBackendForTransitoryDirectText(
    const TransitoryDirectTextTarget& target) {
  return CanUseWin32SelectionReplacementForTransitoryDirectText(target) ||
         CanUseRichEditRangeReplacementForTransitoryDirectText(target);
}

inline bool ShouldUseTransitoryDirectTextComposition(
    const TransitoryDirectTextTarget& target,
    const std::vector<TextEditOperation>& operations, bool is_active) {
  if (!IsTransitoryDirectTextTarget(target)) {
    return false;
  }
  if (!HasVerifiedNativeReplacementBackendForTransitoryDirectText(target)) {
    return false;
  }
  const TransitoryDirectTextOperationPlan plan =
      BuildTransitoryDirectTextOperationPlan(operations);
  return plan.has_composition_operation || is_active;
}

inline bool ShouldAppendTransitoryRepeatCommit(
    const std::wstring& last_preedit,
    const std::wstring& commit_text,
    bool has_preedit,
    const std::wstring& preedit_text) {
  return has_preedit && !last_preedit.empty() && !commit_text.empty() &&
         !preedit_text.empty() && last_preedit == commit_text &&
         commit_text == preedit_text;
}

class TransitoryDirectTextComposition final {
 public:
  bool IsActive() const;
  bool ShouldUse(ITfContext* context,
                 const std::vector<TextEditOperation>& operations) const;
  ITfContext* ResolveFullContextFromTransitory(
      ITfContext* context,
      const std::vector<TextEditOperation>& operations) const;
  HRESULT Apply(TfEditCookie edit_cookie, ITfContext* context,
                const std::vector<TextEditOperation>& operations);
  void Reset(const wchar_t* reason);

 private:
  bool active_ = false;
  ITfContext* context_identity_ = nullptr;
  HWND view_hwnd_ = nullptr;
  std::wstring last_preedit_;
  bool deferred_autocomplete_update_ = false;
};

}  // namespace milkyway::tsf::edit

#endif
