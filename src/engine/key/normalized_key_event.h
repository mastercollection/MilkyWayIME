#pragma once

#include "engine/key/layout_key.h"
#include "engine/key/physical_key.h"
#include "engine/state/modifier_state.h"

namespace milkyway::engine::key {

struct NormalizedKeyEvent {
  PhysicalKey key;
  state::ModifierState modifiers;
  KeyTransition transition = KeyTransition::kPressed;
  LayoutKey input_label_key = LayoutKey::kUnknown;

  bool UsesHangulShift() const {
    return modifiers.shift && !modifiers.ctrl && !modifiers.alt && !modifiers.win;
  }
};

}  // namespace milkyway::engine::key
