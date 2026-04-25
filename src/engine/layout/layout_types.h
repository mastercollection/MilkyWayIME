#pragma once

#include <string>

#include "engine/key/layout_key.h"

namespace milkyway::engine::layout {

using PhysicalLayoutId = std::string;
using KoreanLayoutId = std::string;

enum class PhysicalLayoutInterpretation {
  kEffectiveBaseLayout,
};

struct PhysicalLayout {
  PhysicalLayoutId id;
  std::string display_name;
  PhysicalLayoutInterpretation interpretation =
      PhysicalLayoutInterpretation::kEffectiveBaseLayout;
};

struct KoreanLayoutMapping {
  KoreanLayoutId id;
  std::string display_name;
};

struct HangulMappingKey {
  key::LayoutKey hangul_token_key = key::LayoutKey::kUnknown;
  bool shift = false;
};

struct ResolvedHangulInput {
  bool is_mapped = false;
  char ascii = 0;
};

}  // namespace milkyway::engine::layout
