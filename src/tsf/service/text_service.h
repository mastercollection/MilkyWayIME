#pragma once

#include "engine/key/physical_key.h"
#include "engine/session/input_session.h"
#include "engine/state/modifier_state.h"

namespace milkyway::tsf::service {

class TextService {
 public:
  explicit TextService(engine::session::InputSession* session);

  void OnKeyEvent(const engine::key::PhysicalKey& key,
                  const engine::state::ModifierState& modifiers,
                  engine::key::KeyTransition transition);

 private:
  engine::session::InputSession* session_ = nullptr;
};

}  // namespace milkyway::tsf::service
