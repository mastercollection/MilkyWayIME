#include "engine/shortcut/shortcut_resolver.h"

namespace milkyway::engine::shortcut {

ShortcutAction ShortcutResolver::Resolve(const ShortcutQuery& query) const {
  if (query.physical_layout.empty()) {
    return ShortcutAction::kNone;
  }

  if (query.modifiers.ctrl && query.modifiers.shift &&
      query.base_layout_key == key::BaseLayoutKey::kSpace) {
    return ShortcutAction::kToggleInputMode;
  }

  if (query.modifiers.ctrl && query.modifiers.alt &&
      query.base_layout_key == key::BaseLayoutKey::kO) {
    return ShortcutAction::kOpenConfiguration;
  }

  return ShortcutAction::kNone;
}

}  // namespace milkyway::engine::shortcut
