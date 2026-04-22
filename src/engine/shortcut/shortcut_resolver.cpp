#include "engine/shortcut/shortcut_resolver.h"

namespace milkyway::engine::shortcut {

ShortcutAction ShortcutResolver::Resolve(const ShortcutQuery& query) const {
  if (query.physical_layout.empty()) {
    return ShortcutAction::kNone;
  }

  if (query.modifiers.ctrl && query.modifiers.shift &&
      query.key.virtual_key == 0x20) {
    return ShortcutAction::kToggleInputMode;
  }

  if (query.modifiers.ctrl && query.modifiers.alt &&
      query.key.virtual_key == 'O') {
    return ShortcutAction::kOpenConfiguration;
  }

  return ShortcutAction::kNone;
}

}  // namespace milkyway::engine::shortcut
