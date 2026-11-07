#pragma once

#if defined(_WIN32)

namespace milkyway::tsf::host {

struct UiaCaretDiagnosticRequest {
  bool set_value = false;
  bool try_uia_caret_end = false;
  bool send_keyboard_end_after_set = false;
  bool set_caret_after_set_offset = false;
  int caret_after_set_offset = 0;
};

struct UiaCaretDiagnosticPlan {
  bool set_value = false;
  bool try_uia_caret_end_before_set = false;
  bool reacquire_after_set = false;
  bool try_uia_caret_end_after_set = false;
  bool send_keyboard_end_after_set = false;
  bool try_uia_caret_offset_after_set = false;
  int caret_offset_after_set = 0;
};

inline UiaCaretDiagnosticPlan BuildUiaCaretDiagnosticPlan(
    const UiaCaretDiagnosticRequest& request) {
  UiaCaretDiagnosticPlan plan;
  plan.set_value = request.set_value;
  if (!request.set_value) {
    plan.try_uia_caret_end_before_set = request.try_uia_caret_end;
    return plan;
  }

  plan.try_uia_caret_end_after_set = request.try_uia_caret_end;
  plan.send_keyboard_end_after_set = request.send_keyboard_end_after_set;
  plan.try_uia_caret_offset_after_set =
      request.set_caret_after_set_offset;
  plan.caret_offset_after_set = request.caret_after_set_offset;
  plan.reacquire_after_set = plan.try_uia_caret_end_after_set ||
                             plan.try_uia_caret_offset_after_set ||
                             plan.send_keyboard_end_after_set;
  return plan;
}

}  // namespace milkyway::tsf::host

#endif
