#include "tsf/edit/composition_edit_session.h"

#include <utility>

namespace milkyway::tsf::edit {

StartOrUpdateCompositionEditSession::StartOrUpdateCompositionEditSession(
    std::string preedit)
    : preedit_(std::move(preedit)) {}

void StartOrUpdateCompositionEditSession::Apply(
    engine::session::InputSession& session, TextEditSink& sink) const {
  if (session.IsComposing()) {
    session.UpdateComposition(preedit_);
    sink.Apply(
        TextEditOperation{TextEditOperationType::kUpdateComposition, preedit_});
    return;
  }

  session.StartComposition(preedit_);
  sink.Apply(
      TextEditOperation{TextEditOperationType::kStartComposition, preedit_});
}

EndCompositionEditSession::EndCompositionEditSession(
    engine::session::CompositionEndReason reason)
    : reason_(reason) {}

bool EndCompositionEditSession::Apply(engine::session::InputSession& session,
                                      TextEditSink& sink) const {
  if (!session.IsComposing()) {
    return false;
  }

  sink.Apply(TextEditOperation{TextEditOperationType::kEndComposition, {}});
  session.EndComposition(reason_);
  return true;
}

}  // namespace milkyway::tsf::edit
