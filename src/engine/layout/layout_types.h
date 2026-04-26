#pragma once

#include <string>
#include <vector>

#include "engine/key/layout_key.h"

namespace milkyway::engine::layout {

using BaseLayoutId = std::string;
using KoreanLayoutId = std::string;

enum class BaseLayoutInterpretation {
  kEffectiveBaseLayout,
};

struct BaseLayout {
  BaseLayoutId id;
  std::string display_name;
  BaseLayoutInterpretation interpretation =
      BaseLayoutInterpretation::kEffectiveBaseLayout;
};

struct LayoutKeyMapping {
  key::LayoutKey token_key;
  key::LayoutKey label_key;
};

struct BaseLayoutDefinition {
  BaseLayout layout;
  std::vector<LayoutKeyMapping> mappings;
};

struct KoreanLayoutMapping {
  KoreanLayoutId id;
  std::string display_name;
  std::string libhangul_keyboard_id;
  bool maps_non_letter_ascii = false;
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
