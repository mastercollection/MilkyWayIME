#pragma once

#include "engine/key/layout_key.h"
#include "engine/layout/layout_types.h"
#include "engine/state/modifier_state.h"

namespace milkyway::engine::shortcut {

enum class ShortcutAction {
  kNone,
  kToggleInputMode,
};

struct ShortcutQuery {
  layout::BaseLayoutId base_layout_id;
  state::ModifierState modifiers;
  key::LayoutKey input_label_key = key::LayoutKey::kUnknown;
};

class ShortcutResolver {
 public:
  ShortcutAction Resolve(const ShortcutQuery& query) const;
};

constexpr const char* ShortcutActionDebugName(ShortcutAction action) {
  switch (action) {
    case ShortcutAction::kNone:
      return "None";
    case ShortcutAction::kToggleInputMode:
      return "ToggleInputMode";
  }

  return "Unknown";
}

}  // namespace milkyway::engine::shortcut
