#include "engine/layout/layout_registry.h"

#include "engine/key/layout_key.h"

namespace milkyway::engine::layout {
namespace {

const std::vector<PhysicalLayout> kPhysicalLayouts = {
    {"us_qwerty", "US QWERTY",
     PhysicalLayoutInterpretation::kEffectiveBaseLayout},
    {"colemak", "Colemak",
     PhysicalLayoutInterpretation::kEffectiveBaseLayout},
    {"colemak_dh", "Colemak-DH",
     PhysicalLayoutInterpretation::kEffectiveBaseLayout},
};

const std::vector<KoreanLayoutMapping> kKoreanLayouts = {
    {"ko_dubeolsik", "Korean Dubeolsik"},
};

key::LayoutKey ResolveUsQwertyLayoutKey(std::uint16_t virtual_key) {
  if (virtual_key >= 'A' && virtual_key <= 'Z') {
    return static_cast<key::LayoutKey>(
        static_cast<int>(key::LayoutKey::kA) + (virtual_key - 'A'));
  }

  if (virtual_key >= '0' && virtual_key <= '9') {
    return static_cast<key::LayoutKey>(
        static_cast<int>(key::LayoutKey::kDigit0) + (virtual_key - '0'));
  }

  switch (virtual_key) {
    case 0x08:
      return key::LayoutKey::kBackspace;
    case 0x09:
      return key::LayoutKey::kTab;
    case 0x0D:
      return key::LayoutKey::kReturn;
    case 0x20:
      return key::LayoutKey::kSpace;
    case 0xBA:
      return key::LayoutKey::kOem1;
    case 0xBB:
      return key::LayoutKey::kOemPlus;
    case 0xBC:
      return key::LayoutKey::kOemComma;
    case 0xBD:
      return key::LayoutKey::kOemMinus;
    case 0xBE:
      return key::LayoutKey::kOemPeriod;
    case 0xBF:
      return key::LayoutKey::kOem2;
    case 0xC0:
      return key::LayoutKey::kOem3;
    case 0xDB:
      return key::LayoutKey::kOem4;
    case 0xDC:
      return key::LayoutKey::kOem5;
    case 0xDD:
      return key::LayoutKey::kOem6;
    case 0xDE:
      return key::LayoutKey::kOem7;
    default:
      return key::LayoutKey::kUnknown;
  }
}

constexpr key::LayoutKey kTokenKeys[] = {
    key::LayoutKey::kA,         key::LayoutKey::kB,
    key::LayoutKey::kC,         key::LayoutKey::kD,
    key::LayoutKey::kE,         key::LayoutKey::kF,
    key::LayoutKey::kG,         key::LayoutKey::kH,
    key::LayoutKey::kI,         key::LayoutKey::kJ,
    key::LayoutKey::kK,         key::LayoutKey::kL,
    key::LayoutKey::kM,         key::LayoutKey::kN,
    key::LayoutKey::kO,         key::LayoutKey::kP,
    key::LayoutKey::kQ,         key::LayoutKey::kR,
    key::LayoutKey::kS,         key::LayoutKey::kT,
    key::LayoutKey::kU,         key::LayoutKey::kV,
    key::LayoutKey::kW,         key::LayoutKey::kX,
    key::LayoutKey::kY,         key::LayoutKey::kZ,
    key::LayoutKey::kDigit0,    key::LayoutKey::kDigit1,
    key::LayoutKey::kDigit2,    key::LayoutKey::kDigit3,
    key::LayoutKey::kDigit4,    key::LayoutKey::kDigit5,
    key::LayoutKey::kDigit6,    key::LayoutKey::kDigit7,
    key::LayoutKey::kDigit8,    key::LayoutKey::kDigit9,
    key::LayoutKey::kSpace,     key::LayoutKey::kTab,
    key::LayoutKey::kReturn,    key::LayoutKey::kBackspace,
    key::LayoutKey::kOem1,      key::LayoutKey::kOem2,
    key::LayoutKey::kOem3,      key::LayoutKey::kOem4,
    key::LayoutKey::kOem5,      key::LayoutKey::kOem6,
    key::LayoutKey::kOem7,      key::LayoutKey::kOemPlus,
    key::LayoutKey::kOemComma,  key::LayoutKey::kOemMinus,
    key::LayoutKey::kOemPeriod,
};

key::LayoutKey ResolveColemakLabelKey(key::LayoutKey token_key) {
  switch (token_key) {
    case key::LayoutKey::kE:
      return key::LayoutKey::kF;
    case key::LayoutKey::kR:
      return key::LayoutKey::kP;
    case key::LayoutKey::kT:
      return key::LayoutKey::kG;
    case key::LayoutKey::kY:
      return key::LayoutKey::kJ;
    case key::LayoutKey::kU:
      return key::LayoutKey::kL;
    case key::LayoutKey::kI:
      return key::LayoutKey::kU;
    case key::LayoutKey::kO:
      return key::LayoutKey::kY;
    case key::LayoutKey::kP:
      return key::LayoutKey::kOem1;
    case key::LayoutKey::kS:
      return key::LayoutKey::kR;
    case key::LayoutKey::kD:
      return key::LayoutKey::kS;
    case key::LayoutKey::kF:
      return key::LayoutKey::kT;
    case key::LayoutKey::kG:
      return key::LayoutKey::kD;
    case key::LayoutKey::kJ:
      return key::LayoutKey::kN;
    case key::LayoutKey::kK:
      return key::LayoutKey::kE;
    case key::LayoutKey::kL:
      return key::LayoutKey::kI;
    case key::LayoutKey::kOem1:
      return key::LayoutKey::kO;
    case key::LayoutKey::kN:
      return key::LayoutKey::kK;
    default:
      return token_key;
  }
}

key::LayoutKey ResolveColemakDhLabelKey(key::LayoutKey token_key) {
  switch (token_key) {
    case key::LayoutKey::kE:
      return key::LayoutKey::kF;
    case key::LayoutKey::kR:
      return key::LayoutKey::kP;
    case key::LayoutKey::kT:
      return key::LayoutKey::kB;
    case key::LayoutKey::kY:
      return key::LayoutKey::kJ;
    case key::LayoutKey::kU:
      return key::LayoutKey::kL;
    case key::LayoutKey::kI:
      return key::LayoutKey::kU;
    case key::LayoutKey::kO:
      return key::LayoutKey::kY;
    case key::LayoutKey::kP:
      return key::LayoutKey::kOem1;
    case key::LayoutKey::kS:
      return key::LayoutKey::kR;
    case key::LayoutKey::kD:
      return key::LayoutKey::kS;
    case key::LayoutKey::kF:
      return key::LayoutKey::kT;
    case key::LayoutKey::kH:
      return key::LayoutKey::kM;
    case key::LayoutKey::kJ:
      return key::LayoutKey::kN;
    case key::LayoutKey::kK:
      return key::LayoutKey::kE;
    case key::LayoutKey::kL:
      return key::LayoutKey::kI;
    case key::LayoutKey::kV:
      return key::LayoutKey::kD;
    case key::LayoutKey::kB:
      return key::LayoutKey::kV;
    case key::LayoutKey::kOem1:
      return key::LayoutKey::kO;
    case key::LayoutKey::kN:
      return key::LayoutKey::kK;
    case key::LayoutKey::kM:
      return key::LayoutKey::kH;
    default:
      return token_key;
  }
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

key::LayoutKey LayoutRegistry::ResolveInputLabelKey(
    const key::PhysicalKey& key) const {
  return ResolveUsQwertyLayoutKey(key.virtual_key);
}

key::LayoutKey LayoutRegistry::ResolveBaseLayoutLabelKey(
    const PhysicalLayoutId& physical_layout_id,
    key::LayoutKey token_key) const {
  if (physical_layout_id == "colemak") {
    return ResolveColemakLabelKey(token_key);
  }

  if (physical_layout_id == "colemak_dh") {
    return ResolveColemakDhLabelKey(token_key);
  }

  return token_key;
}

key::LayoutKey LayoutRegistry::ResolveHangulTokenKey(
    const PhysicalLayoutId& physical_layout_id,
    key::LayoutKey input_label_key) const {
  for (const key::LayoutKey token_key : kTokenKeys) {
    if (ResolveBaseLayoutLabelKey(physical_layout_id, token_key) ==
        input_label_key) {
      return token_key;
    }
  }

  return key::LayoutKey::kUnknown;
}

key::NormalizedKeyEvent LayoutRegistry::NormalizeKeyEvent(
    const PhysicalLayoutId& physical_layout_id, const key::PhysicalKey& key,
    const state::ModifierState& modifiers,
    key::KeyTransition transition) const {
  (void)physical_layout_id;
  key::NormalizedKeyEvent event;
  event.key = key;
  event.modifiers = modifiers;
  event.transition = transition;
  event.input_label_key = ResolveInputLabelKey(key);
  return event;
}

ResolvedHangulInput LayoutRegistry::ResolveHangulInput(
    const KoreanLayoutId& korean_layout_id, const HangulMappingKey& key) const {
  if (korean_layout_id != "ko_dubeolsik") {
    return {};
  }

  const char ascii = engine::key::LayoutKeyToAsciiLetter(
      key.hangul_token_key, key.shift);
  if (ascii == 0) {
    return {};
  }

  return ResolvedHangulInput{true, ascii};
}

}  // namespace milkyway::engine::layout
