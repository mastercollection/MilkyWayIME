#include "engine/layout/layout_registry.h"

#include "engine/key/base_layout_key.h"

namespace milkyway::engine::layout {
namespace {

const std::vector<PhysicalLayout> kPhysicalLayouts = {
    {"us_qwerty", "US QWERTY",
     PhysicalLayoutInterpretation::kEffectiveBaseLayout},
#if defined(_DEBUG)
    {"test_swapped_rp", "Test Effective Base Layout (Swap R/P)",
     PhysicalLayoutInterpretation::kEffectiveBaseLayout},
#endif
};

const std::vector<KoreanLayoutMapping> kKoreanLayouts = {
    {"ko_dubeolsik", "Korean Dubeolsik", "us_qwerty",
     KoreanLayoutMappingModel::kRelative},
};

key::BaseLayoutKey ResolveUsQwertyBaseLayoutKey(std::uint16_t virtual_key) {
  if (virtual_key >= 'A' && virtual_key <= 'Z') {
    return static_cast<key::BaseLayoutKey>(
        static_cast<int>(key::BaseLayoutKey::kA) + (virtual_key - 'A'));
  }

  if (virtual_key >= '0' && virtual_key <= '9') {
    return static_cast<key::BaseLayoutKey>(
        static_cast<int>(key::BaseLayoutKey::kDigit0) + (virtual_key - '0'));
  }

  switch (virtual_key) {
    case 0x08:
      return key::BaseLayoutKey::kBackspace;
    case 0x09:
      return key::BaseLayoutKey::kTab;
    case 0x0D:
      return key::BaseLayoutKey::kReturn;
    case 0x20:
      return key::BaseLayoutKey::kSpace;
    case 0xBA:
      return key::BaseLayoutKey::kOem1;
    case 0xBF:
      return key::BaseLayoutKey::kOem2;
    case 0xC0:
      return key::BaseLayoutKey::kOem3;
    case 0xDB:
      return key::BaseLayoutKey::kOem4;
    case 0xDC:
      return key::BaseLayoutKey::kOem5;
    case 0xDD:
      return key::BaseLayoutKey::kOem6;
    case 0xDE:
      return key::BaseLayoutKey::kOem7;
    default:
      return key::BaseLayoutKey::kUnknown;
  }
}

key::BaseLayoutKey ResolveTestSwappedRpBaseLayoutKey(std::uint16_t virtual_key) {
  const key::BaseLayoutKey base_key = ResolveUsQwertyBaseLayoutKey(virtual_key);
  if (base_key == key::BaseLayoutKey::kR) {
    return key::BaseLayoutKey::kP;
  }
  if (base_key == key::BaseLayoutKey::kP) {
    return key::BaseLayoutKey::kR;
  }
  return base_key;
}

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

const PhysicalLayout* LayoutRegistry::FindPhysicalLayout(
    const PhysicalLayoutId& id) const {
  for (const PhysicalLayout& layout : kPhysicalLayouts) {
    if (layout.id == id) {
      return &layout;
    }
  }

  return nullptr;
}

const KoreanLayoutMapping* LayoutRegistry::FindKoreanLayout(
    const KoreanLayoutId& id) const {
  for (const KoreanLayoutMapping& layout : kKoreanLayouts) {
    if (layout.id == id) {
      return &layout;
    }
  }

  return nullptr;
}

key::BaseLayoutKey LayoutRegistry::ResolveBaseLayoutKey(
    const PhysicalLayoutId& physical_layout_id,
    const key::PhysicalKey& key) const {
  if (physical_layout_id == "us_qwerty") {
    return ResolveUsQwertyBaseLayoutKey(key.virtual_key);
  }

#if defined(_DEBUG)
  if (physical_layout_id == "test_swapped_rp") {
    return ResolveTestSwappedRpBaseLayoutKey(key.virtual_key);
  }
#endif

  return key::BaseLayoutKey::kUnknown;
}

key::NormalizedKeyEvent LayoutRegistry::NormalizeKeyEvent(
    const PhysicalLayoutId& physical_layout_id, const key::PhysicalKey& key,
    const state::ModifierState& modifiers,
    key::KeyTransition transition) const {
  key::NormalizedKeyEvent event;
  event.key = key;
  event.modifiers = modifiers;
  event.transition = transition;
  event.base_layout_key = ResolveBaseLayoutKey(physical_layout_id, key);
  return event;
}

ResolvedHangulInput LayoutRegistry::ResolveHangulInput(
    const KoreanLayoutId& korean_layout_id, const HangulMappingKey& key) const {
  if (korean_layout_id != "ko_dubeolsik") {
    return {};
  }

  const char ascii = engine::key::BaseLayoutKeyToAsciiLetter(key.base_layout_key,
                                                             key.shift);
  if (ascii == 0) {
    return {};
  }

  return ResolvedHangulInput{true, ascii};
}

}  // namespace milkyway::engine::layout
