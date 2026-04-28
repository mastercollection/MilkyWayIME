#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <msctf.h>

#include <cwctype>
#include <string>
#include <vector>

#include "tsf/edit/text_edit_sink.h"

namespace milkyway::tsf::edit {

struct NikkeDirectTextTarget {
  std::wstring process_name;
  std::wstring view_class;
  bool is_transitory = false;
};

struct NikkeDirectTextOperationPlan {
  std::string commit_text;
  std::string preedit_text;
  bool has_preedit = false;
  bool end_requested = false;
  bool has_composition_operation = false;
};

inline std::wstring NikkeDirectTextLower(std::wstring value) {
  for (wchar_t& ch : value) {
    ch = static_cast<wchar_t>(std::towlower(ch));
  }
  return value;
}

inline bool NikkeDirectTextEqualsInsensitive(const std::wstring& lhs,
                                             const std::wstring& rhs) {
  return NikkeDirectTextLower(lhs) == NikkeDirectTextLower(rhs);
}

inline NikkeDirectTextOperationPlan BuildNikkeDirectTextOperationPlan(
    const std::vector<TextEditOperation>& operations) {
  NikkeDirectTextOperationPlan plan;
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

inline bool IsNikkeDirectTextTarget(const NikkeDirectTextTarget& target) {
  return target.is_transitory &&
         NikkeDirectTextEqualsInsensitive(target.process_name, L"NIKKE.exe") &&
         NikkeDirectTextEqualsInsensitive(target.view_class, L"UnityWndClass");
}

inline bool ShouldUseNikkeDirectTextComposition(
    const NikkeDirectTextTarget& target,
    const std::vector<TextEditOperation>& operations, bool is_active) {
  if (!IsNikkeDirectTextTarget(target)) {
    return false;
  }
  const NikkeDirectTextOperationPlan plan =
      BuildNikkeDirectTextOperationPlan(operations);
  return plan.has_composition_operation || is_active;
}

class NikkeDirectTextComposition final {
 public:
  bool IsActive() const;
  bool ShouldUse(ITfContext* context,
                 const std::vector<TextEditOperation>& operations) const;
  HRESULT Apply(TfEditCookie edit_cookie, ITfContext* context,
                const std::vector<TextEditOperation>& operations);
  void Reset(const wchar_t* reason);

 private:
  bool active_ = false;
  ITfContext* context_identity_ = nullptr;
  HWND view_hwnd_ = nullptr;
  std::wstring last_preedit_;
};

}  // namespace milkyway::tsf::edit

#endif
