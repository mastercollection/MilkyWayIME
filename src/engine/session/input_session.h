#pragma once

#include <optional>
#include <string>

#include "engine/composition/composition_snapshot.h"
#include "engine/hanja/candidate_request.h"
#include "engine/layout/layout_types.h"

namespace milkyway::engine::session {

class InputSession {
 public:
  InputSession(layout::PhysicalLayoutId physical_layout_id,
               layout::KoreanLayoutId korean_layout_id);

  void SetLayouts(layout::PhysicalLayoutId physical_layout_id,
                  layout::KoreanLayoutId korean_layout_id);

  const layout::PhysicalLayoutId& physical_layout_id() const;
  const layout::KoreanLayoutId& korean_layout_id() const;

  void StartComposition();
  void UpdateComposition(std::string preedit);
  void EndComposition();

  bool IsComposing() const;
  const composition::CompositionSnapshot& snapshot() const;

  std::optional<hanja::CandidateRequest> RequestHanjaConversion() const;

 private:
  layout::PhysicalLayoutId physical_layout_id_;
  layout::KoreanLayoutId korean_layout_id_;
  bool is_composing_ = false;
  composition::CompositionSnapshot snapshot_;
};

}  // namespace milkyway::engine::session
