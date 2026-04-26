#include "engine/shortcut/shortcut_resolver.h"

namespace milkyway::engine::shortcut {

ShortcutAction ShortcutResolver::Resolve(const ShortcutQuery& query) const {
  (void)query;
  return ShortcutAction::kNone;
}

}  // namespace milkyway::engine::shortcut
