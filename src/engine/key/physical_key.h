#pragma once

#include <cstdint>

namespace milkyway::engine::key {

enum class KeyTransition {
  kPressed,
  kReleased,
};

struct PhysicalKey {
  std::uint16_t virtual_key = 0;
  std::uint16_t scan_code = 0;
  bool extended = false;
};

}  // namespace milkyway::engine::key
