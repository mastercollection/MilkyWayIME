#include "engine/session/input_session.h"

#include <utility>

namespace milkyway::engine::session {

InputSession::InputSession(layout::PhysicalLayoutId physical_layout_id,
                           layout::KoreanLayoutId korean_layout_id)
    : physical_layout_id_(std::move(physical_layout_id)),
      korean_layout_id_(std::move(korean_layout_id)) {}

void InputSession::SetLayouts(layout::PhysicalLayoutId physical_layout_id,
                              layout::KoreanLayoutId korean_layout_id) {
  physical_layout_id_ = std::move(physical_layout_id);
  korean_layout_id_ = std::move(korean_layout_id);
}

const layout::PhysicalLayoutId& InputSession::physical_layout_id() const {
  return physical_layout_id_;
}

const layout::KoreanLayoutId& InputSession::korean_layout_id() const {
  return korean_layout_id_;
}

void InputSession::StartComposition() {
  is_composing_ = true;
  snapshot_.preedit.clear();
}

void InputSession::UpdateComposition(std::string preedit) {
  is_composing_ = true;
  snapshot_.preedit = std::move(preedit);
}

void InputSession::EndComposition() {
  is_composing_ = false;
  snapshot_.preedit.clear();
}

bool InputSession::IsComposing() const {
  return is_composing_;
}

const composition::CompositionSnapshot& InputSession::snapshot() const {
  return snapshot_;
}

std::optional<hanja::CandidateRequest> InputSession::RequestHanjaConversion()
    const {
  if (!is_composing_ || snapshot_.preedit.empty()) {
    return std::nullopt;
  }

  return hanja::CandidateRequest{snapshot_.preedit};
}

}  // namespace milkyway::engine::session
