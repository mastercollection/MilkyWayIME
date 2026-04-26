#include "engine/layout/layout_registry.h"

#include <cstddef>
#include <iterator>
#include <utility>

#include "engine/key/layout_key.h"

namespace milkyway::engine::layout {
namespace {

constexpr LayoutKeyMapping kColemakMappings[] = {
    {key::LayoutKey::kE, key::LayoutKey::kF},
    {key::LayoutKey::kR, key::LayoutKey::kP},
    {key::LayoutKey::kT, key::LayoutKey::kG},
    {key::LayoutKey::kY, key::LayoutKey::kJ},
    {key::LayoutKey::kU, key::LayoutKey::kL},
    {key::LayoutKey::kI, key::LayoutKey::kU},
    {key::LayoutKey::kO, key::LayoutKey::kY},
    {key::LayoutKey::kP, key::LayoutKey::kOem1},
    {key::LayoutKey::kS, key::LayoutKey::kR},
    {key::LayoutKey::kD, key::LayoutKey::kS},
    {key::LayoutKey::kF, key::LayoutKey::kT},
    {key::LayoutKey::kG, key::LayoutKey::kD},
    {key::LayoutKey::kJ, key::LayoutKey::kN},
    {key::LayoutKey::kK, key::LayoutKey::kE},
    {key::LayoutKey::kL, key::LayoutKey::kI},
    {key::LayoutKey::kOem1, key::LayoutKey::kO},
    {key::LayoutKey::kN, key::LayoutKey::kK},
};

std::vector<BaseLayoutDefinition> BuildBuiltInBaseLayoutDefinitions() {
  std::vector<BaseLayoutDefinition> definitions;
  definitions.push_back(BaseLayoutDefinition{
      {"us_qwerty", "미국식 쿼티",
       BaseLayoutInterpretation::kEffectiveBaseLayout},
      {}});
  definitions.push_back(BaseLayoutDefinition{
      {"colemak", "콜맥",
       BaseLayoutInterpretation::kEffectiveBaseLayout},
      std::vector<LayoutKeyMapping>(std::begin(kColemakMappings),
                                    std::end(kColemakMappings))});
  return definitions;
}

const std::vector<KoreanLayoutMapping> kKoreanLayouts = {
    {"libhangul:2", "두벌식", "2", false},
    {"libhangul:2y", "두벌식 옛글", "2y", false},
    {"libhangul:32", "세벌식 두벌자판", "32", true},
    {"libhangul:39", "세벌식 390", "39", true},
    {"libhangul:3f", "세벌식 최종", "3f", true},
    {"libhangul:3s", "세벌식 순아래", "3s", true},
    {"libhangul:3y", "세벌식 옛글", "3y", true},
    {"libhangul:ro", "로마자", "ro", true},
    {"libhangul:ahn", "안마태", "ahn", true},
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

}  // namespace

LayoutRegistry::LayoutRegistry()
    : base_layout_definitions_(BuildBuiltInBaseLayoutDefinitions()) {
  base_layouts_.reserve(base_layout_definitions_.size());
  for (const BaseLayoutDefinition& definition : base_layout_definitions_) {
    base_layouts_.push_back(definition.layout);
  }
}

const std::vector<BaseLayout>& LayoutRegistry::base_layouts() const {
  return base_layouts_;
}

const std::vector<KoreanLayoutMapping>& LayoutRegistry::korean_layouts() const {
  return kKoreanLayouts;
}

bool LayoutRegistry::AddBaseLayout(BaseLayoutDefinition definition) {
  if (definition.layout.id.empty()) {
    return false;
  }

  for (std::size_t index = 0; index < base_layout_definitions_.size();
       ++index) {
    if (base_layout_definitions_[index].layout.id == definition.layout.id) {
      base_layouts_[index] = definition.layout;
      base_layout_definitions_[index] = std::move(definition);
      return true;
    }
  }

  base_layouts_.push_back(definition.layout);
  base_layout_definitions_.push_back(std::move(definition));
  return true;
}

const BaseLayout& LayoutRegistry::DefaultBaseLayout() const {
  return base_layouts_.front();
}

const KoreanLayoutMapping& LayoutRegistry::DefaultKoreanLayout() const {
  return kKoreanLayouts.front();
}

const BaseLayout* LayoutRegistry::FindBaseLayout(
    const BaseLayoutId& id) const {
  for (const BaseLayout& layout : base_layouts_) {
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

std::string LayoutRegistry::ResolveLibhangulKeyboardId(
    const KoreanLayoutId& korean_layout_id) const {
  const KoreanLayoutMapping* layout = FindKoreanLayout(korean_layout_id);
  if (layout == nullptr) {
    return DefaultKoreanLayout().libhangul_keyboard_id;
  }

  return layout->libhangul_keyboard_id;
}

key::LayoutKey LayoutRegistry::ResolveInputLabelKey(
    const key::PhysicalKey& key) const {
  return ResolveUsQwertyLayoutKey(key.virtual_key);
}

key::LayoutKey LayoutRegistry::ResolveBaseLayoutLabelKey(
    const BaseLayoutId& base_layout_id,
    key::LayoutKey token_key) const {
  for (const BaseLayoutDefinition& definition : base_layout_definitions_) {
    if (definition.layout.id != base_layout_id) {
      continue;
    }

    for (const LayoutKeyMapping& mapping : definition.mappings) {
      if (mapping.token_key == token_key) {
        return mapping.label_key;
      }
    }
    return token_key;
  }

  return token_key;
}

key::LayoutKey LayoutRegistry::ResolveHangulTokenKey(
    const BaseLayoutId& base_layout_id,
    key::LayoutKey input_label_key) const {
  for (const key::LayoutKey token_key : kTokenKeys) {
    if (ResolveBaseLayoutLabelKey(base_layout_id, token_key) ==
        input_label_key) {
      return token_key;
    }
  }

  return key::LayoutKey::kUnknown;
}

key::NormalizedKeyEvent LayoutRegistry::NormalizeKeyEvent(
    const BaseLayoutId& base_layout_id, const key::PhysicalKey& key,
    const state::ModifierState& modifiers,
    key::KeyTransition transition) const {
  (void)base_layout_id;
  key::NormalizedKeyEvent event;
  event.key = key;
  event.modifiers = modifiers;
  event.transition = transition;
  event.input_label_key = ResolveInputLabelKey(key);
  return event;
}

ResolvedHangulInput LayoutRegistry::ResolveHangulInput(
    const KoreanLayoutId& korean_layout_id, const HangulMappingKey& key) const {
  const KoreanLayoutMapping* layout = FindKoreanLayout(korean_layout_id);
  if (layout == nullptr) {
    return {};
  }

  if (!layout->maps_non_letter_ascii &&
      !engine::key::IsAlphabeticLayoutKey(key.hangul_token_key)) {
    return {};
  }

  const char ascii = engine::key::LayoutKeyToUsQwertyAscii(
      key.hangul_token_key, key.shift);
  if (ascii == 0) {
    return {};
  }

  return ResolvedHangulInput{true, ascii};
}

}  // namespace milkyway::engine::layout
