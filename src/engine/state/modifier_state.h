#pragma once

namespace milkyway::engine::state {

struct ModifierState {
  bool ctrl = false;
  bool alt = false;
  bool shift = false;
  bool win = false;
};

}  // namespace milkyway::engine::state
