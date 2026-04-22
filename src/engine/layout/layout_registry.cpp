#include "engine/layout/layout_registry.h"

namespace milkyway::engine::layout {
namespace {

const std::vector<PhysicalLayout> kPhysicalLayouts = {
    {"us_qwerty", "US QWERTY"},
};

const std::vector<KoreanLayoutMapping> kKoreanLayouts = {
    {"ko_dubeolsik", "Korean Dubeolsik", "us_qwerty"},
};

}  // namespace

const std::vector<PhysicalLayout>& LayoutRegistry::physical_layouts() const {
  return kPhysicalLayouts;
}

const std::vector<KoreanLayoutMapping>& LayoutRegistry::korean_layouts() const {
  return kKoreanLayouts;
}

const PhysicalLayout& LayoutRegistry::DefaultPhysicalLayout() const {
  return kPhysicalLayouts.front();
}

const KoreanLayoutMapping& LayoutRegistry::DefaultKoreanLayout() const {
  return kKoreanLayouts.front();
}

}  // namespace milkyway::engine::layout
