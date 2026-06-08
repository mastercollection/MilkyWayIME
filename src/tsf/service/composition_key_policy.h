#pragma once

#include <cstdint>

namespace milkyway::tsf::service {

inline bool IsPlainAcceptAndForwardVirtualKey(std::uint16_t virtual_key) {
  switch (virtual_key) {
    case 0x09:  // VK_TAB
    case 0x0D:  // VK_RETURN
    case 0x1B:  // VK_ESCAPE
      return true;
    default:
      return false;
  }
}

inline bool IsCaretNavigationVirtualKey(std::uint16_t virtual_key) {
  switch (virtual_key) {
    case 0x21:  // VK_PRIOR
    case 0x22:  // VK_NEXT
    case 0x23:  // VK_END
    case 0x24:  // VK_HOME
    case 0x25:  // VK_LEFT
    case 0x26:  // VK_UP
    case 0x27:  // VK_RIGHT
    case 0x28:  // VK_DOWN
      return true;
    default:
      return false;
  }
}

inline bool IsShiftHostEditingOrNavigationVirtualKey(
    std::uint16_t virtual_key) {
  switch (virtual_key) {
    case 0x08:  // VK_BACK
    case 0x09:  // VK_TAB
    case 0x1B:  // VK_ESCAPE
    case 0x21:  // VK_PRIOR
    case 0x22:  // VK_NEXT
    case 0x23:  // VK_END
    case 0x24:  // VK_HOME
    case 0x25:  // VK_LEFT
    case 0x26:  // VK_UP
    case 0x27:  // VK_RIGHT
    case 0x28:  // VK_DOWN
    case 0x2D:  // VK_INSERT
    case 0x2E:  // VK_DELETE
      return true;
    default:
      return false;
  }
}

}  // namespace milkyway::tsf::service
