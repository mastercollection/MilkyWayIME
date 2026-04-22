#pragma once

#include <vector>

#include "engine/layout/layout_types.h"

namespace milkyway::engine::layout {

class LayoutRegistry {
 public:
  LayoutRegistry() = default;

  const std::vector<PhysicalLayout>& physical_layouts() const;
  const std::vector<KoreanLayoutMapping>& korean_layouts() const;

  const PhysicalLayout& DefaultPhysicalLayout() const;
  const KoreanLayoutMapping& DefaultKoreanLayout() const;
};

}  // namespace milkyway::engine::layout
