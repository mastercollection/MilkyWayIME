#include "engine/session/input_session.h"

#include <utility>

namespace milkyway::engine::session {

InputSession::InputSession(layout::BaseLayoutId base_layout_id,
                           layout::KoreanLayoutId korean_layout_id)
    : base_layout_id_(std::move(base_layout_id)),
      korean_layout_id_(std::move(korean_layout_id)) {}

void InputSession::SetLayouts(layout::BaseLayoutId base_layout_id,
                              layout::KoreanLayoutId korean_layout_id) {
  base_layout_id_ = std::move(base_layout_id);
  korean_layout_id_ = std::move(korean_layout_id);
}

const layout::BaseLayoutId& InputSession::base_layout_id() const {
  return base_layout_id_;
}

const layout::KoreanLayoutId& InputSession::korean_layout_id() const {
  return korean_layout_id_;
}

void InputSession::StartComposition(std::string preedit) {
  is_composing_ = true;
  snapshot_.preedit = std::move(preedit);
  last_end_reason_ = CompositionEndReason::kNone;
}

void InputSession::UpdateComposition(std::string preedit) {
  is_composing_ = true;
  snapshot_.preedit = std::move(preedit);
  last_end_reason_ = CompositionEndReason::kNone;
}

void InputSession::EndComposition(CompositionEndReason reason) {
  is_composing_ = false;
  snapshot_.preedit.clear();
  last_end_reason_ = reason;
}

bool InputSession::IsComposing() const {
  return is_composing_;
}

const composition::CompositionSnapshot& InputSession::snapshot() const {
  return snapshot_;
}

CompositionEndReason InputSession::last_end_reason() const {
  return last_end_reason_;
}

std::optional<hanja::CandidateRequest> InputSession::RequestHanjaConversion()
    const {
  if (!is_composing_ || snapshot_.preedit.empty()) {
    return std::nullopt;
  }

  return hanja::CreateCandidateRequestFromPreedit(snapshot_.preedit);
}

}  // namespace milkyway::engine::session
