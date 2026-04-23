#pragma once

#include <string>
#include <vector>

#include "tsf/edit/text_edit_sink.h"

namespace milkyway::tsf::edit {

enum class PlannedEditActionType {
  kCommitText,
  kStartComposition,
  kUpdateComposition,
  kCompleteComposition,
};

struct PlannedEditAction {
  PlannedEditActionType type = PlannedEditActionType::kCommitText;
  std::string text;
};

std::vector<PlannedEditAction> PlanTextEditActions(
    bool has_active_composition,
    const std::vector<TextEditOperation>& operations);

}  // namespace milkyway::tsf::edit
