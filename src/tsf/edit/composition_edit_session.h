#pragma once

#include <string>

#include "engine/session/input_session.h"
#include "tsf/edit/text_edit_sink.h"

namespace milkyway::tsf::edit {

class StartOrUpdateCompositionEditSession {
 public:
  explicit StartOrUpdateCompositionEditSession(std::string preedit);

  void Apply(engine::session::InputSession& session, TextEditSink& sink) const;

 private:
  std::string preedit_;
};

class EndCompositionEditSession {
 public:
  explicit EndCompositionEditSession(
      engine::session::CompositionEndReason reason);

  bool Apply(engine::session::InputSession& session, TextEditSink& sink) const;

 private:
  engine::session::CompositionEndReason reason_;
};

}  // namespace milkyway::tsf::edit
