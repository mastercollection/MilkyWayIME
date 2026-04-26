#pragma once

#include <optional>
#include <string>

#include "engine/composition/composition_snapshot.h"
#include "engine/hanja/candidate_request.h"
#include "engine/layout/layout_types.h"

namespace milkyway::engine::session {

enum class CompositionEndReason {
  kNone,
  kCompleted,
  kBackspace,
  kDelimiter,
  kImeModeToggle,
  kFocusLost,
  kSelectionMoved,
  kExternalTermination,
  kShortcutBypass,
  kLayoutChanged,
  kCandidateSelected,
};

class InputSession {
 public:
  InputSession(layout::BaseLayoutId base_layout_id,
               layout::KoreanLayoutId korean_layout_id);

  void SetLayouts(layout::BaseLayoutId base_layout_id,
                  layout::KoreanLayoutId korean_layout_id);

  const layout::BaseLayoutId& base_layout_id() const;
  const layout::KoreanLayoutId& korean_layout_id() const;

  void StartComposition(std::string preedit);
  void UpdateComposition(std::string preedit);
  void EndComposition(CompositionEndReason reason);

  bool IsComposing() const;
  const composition::CompositionSnapshot& snapshot() const;
  CompositionEndReason last_end_reason() const;

  std::optional<hanja::CandidateRequest> RequestHanjaConversion() const;

 private:
  layout::BaseLayoutId base_layout_id_;
  layout::KoreanLayoutId korean_layout_id_;
  bool is_composing_ = false;
  composition::CompositionSnapshot snapshot_;
  CompositionEndReason last_end_reason_ = CompositionEndReason::kNone;
};

}  // namespace milkyway::engine::session
