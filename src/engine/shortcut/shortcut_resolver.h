#pragma once

#include "engine/key/base_layout_key.h"
#include "engine/layout/layout_types.h"
#include "engine/state/modifier_state.h"

namespace milkyway::engine::shortcut {

enum class ShortcutAction {
  kNone,
  kToggleInputMode,
  kOpenConfiguration,
};

struct ShortcutQuery {
  layout::PhysicalLayoutId physical_layout;
  state::ModifierState modifiers;
  key::BaseLayoutKey base_layout_key = key::BaseLayoutKey::kUnknown;
};

class ShortcutResolver {
 public:
  ShortcutAction Resolve(const ShortcutQuery& query) const;
};

}  // namespace milkyway::engine::shortcut
