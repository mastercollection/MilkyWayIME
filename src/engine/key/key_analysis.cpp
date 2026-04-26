#include "engine/key/key_analysis.h"

#include <cstdint>

namespace milkyway::engine::key {
namespace {

constexpr std::uint16_t kVkBack = 0x08;
constexpr std::uint16_t kVkTab = 0x09;
constexpr std::uint16_t kVkReturn = 0x0D;
constexpr std::uint16_t kVkShift = 0x10;
constexpr std::uint16_t kVkControl = 0x11;
constexpr std::uint16_t kVkMenu = 0x12;
constexpr std::uint16_t kVkSpace = 0x20;
constexpr std::uint16_t kVkLwin = 0x5B;
constexpr std::uint16_t kVkRwin = 0x5C;
constexpr std::uint16_t kVkNumpad0 = 0x60;
constexpr std::uint16_t kVkDivide = 0x6F;
constexpr std::uint16_t kVkOem1 = 0xBA;
constexpr std::uint16_t kVkOem3 = 0xC0;
constexpr std::uint16_t kVkOem4 = 0xDB;
constexpr std::uint16_t kVkOem7 = 0xDE;

bool IsAsciiDigitVirtualKey(std::uint16_t virtual_key) {
  return virtual_key >= '0' && virtual_key <= '9';
}

bool IsNumpadVirtualKey(std::uint16_t virtual_key) {
  return virtual_key >= kVkNumpad0 && virtual_key <= kVkDivide;
}

bool IsOemDelimiterVirtualKey(std::uint16_t virtual_key) {
  return (virtual_key >= kVkOem1 && virtual_key <= kVkOem3) ||
         (virtual_key >= kVkOem4 && virtual_key <= kVkOem7);
}

bool IsDelimiterLayoutKey(LayoutKey key) {
  return (key >= LayoutKey::kDigit0 && key <= LayoutKey::kDigit9) ||
         key == LayoutKey::kSpace || key == LayoutKey::kTab ||
         key == LayoutKey::kReturn ||
         key == LayoutKey::kOem1 || key == LayoutKey::kOem2 ||
         key == LayoutKey::kOem3 || key == LayoutKey::kOem4 ||
         key == LayoutKey::kOem5 || key == LayoutKey::kOem6 ||
         key == LayoutKey::kOem7 || key == LayoutKey::kOemPlus ||
         key == LayoutKey::kOemComma ||
         key == LayoutKey::kOemMinus ||
         key == LayoutKey::kOemPeriod;
}

bool IsPureModifierVirtualKey(std::uint16_t virtual_key) {
  return virtual_key == kVkShift || virtual_key == kVkControl ||
         virtual_key == kVkMenu || virtual_key == kVkLwin ||
         virtual_key == kVkRwin;
}

KeyCategory Classify(const NormalizedKeyEvent& event,
                     LayoutKey hangul_token_key,
                     const layout::ResolvedHangulInput& hangul_input) {
  if (IsPureModifierVirtualKey(event.key.virtual_key)) {
    return KeyCategory::kPureModifier;
  }

  if (event.modifiers.ctrl || event.modifiers.alt || event.modifiers.win) {
    return KeyCategory::kModifiedShortcut;
  }

  if (event.key.virtual_key == kVkBack) {
    return KeyCategory::kBackspace;
  }

  if (hangul_input.is_mapped) {
    return KeyCategory::kHangulAscii;
  }

  if (event.key.virtual_key == kVkSpace || event.key.virtual_key == kVkReturn ||
      event.key.virtual_key == kVkTab ||
      IsAsciiDigitVirtualKey(event.key.virtual_key) ||
      IsNumpadVirtualKey(event.key.virtual_key) ||
      IsOemDelimiterVirtualKey(event.key.virtual_key) ||
      IsDelimiterLayoutKey(event.input_label_key) ||
      IsDelimiterLayoutKey(hangul_token_key)) {
    return KeyCategory::kDelimiter;
  }

  return KeyCategory::kUnhandled;
}

}  // namespace

KeyAnalysis AnalyzeKeyEvent(const layout::LayoutRegistry& layout_registry,
                            const layout::BaseLayoutId& base_layout_id,
                            const layout::KoreanLayoutId& korean_layout_id,
                            const PhysicalKey& key,
                            const state::ModifierState& modifiers,
                            KeyTransition transition) {
  KeyAnalysis analysis;
  analysis.event = layout_registry.NormalizeKeyEvent(
      base_layout_id, key, modifiers, transition);
  analysis.input_label_key = analysis.event.input_label_key;
  analysis.hangul_token_key = layout_registry.ResolveHangulTokenKey(
      base_layout_id, analysis.input_label_key);
  analysis.hangul_input = layout_registry.ResolveHangulInput(
      korean_layout_id, layout::HangulMappingKey{
                            analysis.hangul_token_key,
                            analysis.event.UsesHangulShift()});
  analysis.category =
      Classify(analysis.event, analysis.hangul_token_key, analysis.hangul_input);

  if (analysis.category == KeyCategory::kModifiedShortcut) {
    shortcut::ShortcutResolver resolver;
    analysis.shortcut_action = resolver.Resolve(shortcut::ShortcutQuery{
        base_layout_id, modifiers, analysis.input_label_key});
  }

  return analysis;
}

}  // namespace milkyway::engine::key
