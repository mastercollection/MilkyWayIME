#pragma once

#if defined(_WIN32)

#include <windows.h>

#include <string>
#include <vector>

#include "tsf/edit/text_edit_sink.h"
#include "tsf/host/host_text_policy.h"

namespace milkyway::tsf::host {

struct UiaFocusedTextTarget {
  std::wstring framework_id;
  std::wstring control_type;
  std::wstring automation_id;
  std::wstring class_name;
  bool has_keyboard_focus = false;
  bool is_keyboard_focusable = false;
  bool is_enabled = false;
  bool value_pattern_supported = false;
  bool value_readonly = true;
};

inline HostTextBackendPolicy SelectUiaTextBackendPolicy(
    const UiaFocusedTextTarget& target) {
  if (target.framework_id == L"Avalonia" &&
      target.control_type == L"Edit" &&
      target.automation_id == L"SearchTextBox" &&
      target.class_name == L"CustomTextBox" &&
      target.has_keyboard_focus && target.is_keyboard_focusable &&
      target.is_enabled && target.value_pattern_supported &&
      !target.value_readonly) {
    return HostTextBackendPolicy::kUiaAvaloniaSearchBoxWholeValueReplace;
  }
  return HostTextBackendPolicy::kNone;
}

inline std::wstring Utf8ToWideForUiaPolicy(const std::string& text) {
  if (text.empty()) {
    return {};
  }
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         text.data(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0);
  if (length <= 0) {
    return {};
  }
  std::wstring wide_text(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                      static_cast<int>(text.size()), wide_text.data(), length);
  return wide_text;
}

class UiaWholeValueCompositionState {
 public:
  bool IsActive() const { return active_; }

  void Reset() {
    active_ = false;
    base_value_.clear();
    committed_suffix_.clear();
    preedit_.clear();
    last_written_value_.clear();
  }

  bool BuildNextValue(
      const std::wstring& current_value,
      const std::vector<edit::TextEditOperation>& operations,
      std::wstring* next_value) {
    if (next_value == nullptr) {
      return false;
    }

    if (!active_) {
      base_value_ = current_value;
      committed_suffix_.clear();
      preedit_.clear();
      last_written_value_ = current_value;
      active_ = true;
    } else if (current_value != last_written_value_) {
      Reset();
      return false;
    }

    bool has_write = false;
    bool end_requested = false;
    std::wstring next_committed = committed_suffix_;
    std::wstring next_preedit = preedit_;

    for (const edit::TextEditOperation& operation : operations) {
      switch (operation.type) {
        case edit::TextEditOperationType::kCommitText:
          next_committed += Utf8ToWideForUiaPolicy(operation.text);
          next_preedit.clear();
          has_write = true;
          break;
        case edit::TextEditOperationType::kStartComposition:
        case edit::TextEditOperationType::kUpdateComposition:
          next_preedit = Utf8ToWideForUiaPolicy(operation.text);
          has_write = true;
          break;
        case edit::TextEditOperationType::kEndComposition:
          end_requested = true;
          break;
      }
    }

    if (!has_write && end_requested) {
      Reset();
      return false;
    }

    committed_suffix_ = std::move(next_committed);
    preedit_ = std::move(next_preedit);
    *next_value = base_value_ + committed_suffix_ + preedit_;
    last_written_value_ = *next_value;

    if (end_requested) {
      active_ = false;
      base_value_.clear();
      committed_suffix_.clear();
      preedit_.clear();
      last_written_value_.clear();
    }
    return has_write;
  }

 private:
  bool active_ = false;
  std::wstring base_value_;
  std::wstring committed_suffix_;
  std::wstring preedit_;
  std::wstring last_written_value_;
};

}  // namespace milkyway::tsf::host

#endif
