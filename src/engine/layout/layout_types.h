#pragma once

#include <string>

namespace milkyway::engine::layout {

using PhysicalLayoutId = std::string;
using KoreanLayoutId = std::string;

struct PhysicalLayout {
  PhysicalLayoutId id;
  std::string display_name;
};

struct KoreanLayoutMapping {
  KoreanLayoutId id;
  std::string display_name;
  PhysicalLayoutId base_physical_layout;
};

}  // namespace milkyway::engine::layout
