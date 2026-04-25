#include "engine/shortcut/shortcut_resolver.h"

namespace milkyway::engine::shortcut {

ShortcutAction ShortcutResolver::Resolve(const ShortcutQuery& query) const {
  if (query.physical_layout.empty()) {
    return ShortcutAction::kNone;
  }

  if (query.modifiers.ctrl && query.modifiers.shift &&
      query.input_label_key == key::LayoutKey::kSpace) {
    return ShortcutAction::kToggleInputMode;
  }

  if (query.modifiers.ctrl && query.modifiers.alt &&
      query.input_label_key == key::LayoutKey::kO) {
    return ShortcutAction::kOpenConfiguration;
  }

  return ShortcutAction::kNone;
}

}  // namespace milkyway::engine::shortcut
