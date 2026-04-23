#pragma once

#include <string>

#include "engine/key/base_layout_key.h"

namespace milkyway::engine::layout {

using PhysicalLayoutId = std::string;
using KoreanLayoutId = std::string;

enum class PhysicalLayoutInterpretation {
  kEffectiveBaseLayout,
};

enum class KoreanLayoutMappingModel {
  kRelative,
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
  PhysicalLayoutId base_physical_layout;
  KoreanLayoutMappingModel mapping_model = KoreanLayoutMappingModel::kRelative;
};

struct HangulMappingKey {
  key::BaseLayoutKey base_layout_key = key::BaseLayoutKey::kUnknown;
  bool shift = false;
};

struct ResolvedHangulInput {
  bool is_mapped = false;
  char ascii = 0;
};

}  // namespace milkyway::engine::layout
