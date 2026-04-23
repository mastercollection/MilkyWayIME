#include "tsf/edit/text_edit_plan.h"

namespace milkyway::tsf::edit {

std::vector<PlannedEditAction> PlanTextEditActions(
    bool has_active_composition,
    const std::vector<TextEditOperation>& operations) {
  std::string commit_text;
  std::string preedit_text;
  bool has_preedit = false;
  bool end_requested = false;

  for (const TextEditOperation& operation : operations) {
    switch (operation.type) {
      case TextEditOperationType::kCommitText:
        commit_text += operation.text;
        break;
      case TextEditOperationType::kStartComposition:
      case TextEditOperationType::kUpdateComposition:
        preedit_text = operation.text;
        has_preedit = true;
        break;
      case TextEditOperationType::kEndComposition:
        end_requested = true;
        break;
    }
  }

  std::vector<PlannedEditAction> plan;

  if (has_active_composition && !commit_text.empty()) {
    plan.push_back(
        PlannedEditAction{PlannedEditActionType::kUpdateComposition, commit_text});
    plan.push_back(PlannedEditAction{PlannedEditActionType::kCompleteComposition,
                                     {}});
    has_active_composition = false;
  } else if (!commit_text.empty()) {
    plan.push_back(
        PlannedEditAction{PlannedEditActionType::kCommitText, commit_text});
  }

  if (has_preedit) {
    plan.push_back(PlannedEditAction{
        has_active_composition ? PlannedEditActionType::kUpdateComposition
                               : PlannedEditActionType::kStartComposition,
        preedit_text});
    has_active_composition = true;
  }

  if (end_requested && has_active_composition) {
    plan.push_back(
        PlannedEditAction{PlannedEditActionType::kCompleteComposition, {}});
  }

  return plan;
}

}  // namespace milkyway::tsf::edit
