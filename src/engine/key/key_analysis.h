#pragma once

#include "engine/key/normalized_key_event.h"
#include "engine/layout/layout_registry.h"
#include "engine/layout/layout_types.h"
#include "engine/shortcut/shortcut_resolver.h"

namespace milkyway::engine::key {

enum class KeyCategory {
  kHangulAscii,
  kBackspace,
  kDelimiter,
  kPureModifier,
  kModifiedShortcut,
  kUnhandled,
};

struct KeyAnalysis {
  NormalizedKeyEvent event;
  LayoutKey input_label_key = LayoutKey::kUnknown;
  LayoutKey hangul_token_key = LayoutKey::kUnknown;
  layout::ResolvedHangulInput hangul_input;
  KeyCategory category = KeyCategory::kUnhandled;
  shortcut::ShortcutAction shortcut_action = shortcut::ShortcutAction::kNone;
};

KeyAnalysis AnalyzeKeyEvent(const layout::LayoutRegistry& layout_registry,
                            const layout::BaseLayoutId& base_layout_id,
                            const layout::KoreanLayoutId& korean_layout_id,
                            const PhysicalKey& key,
                            const state::ModifierState& modifiers,
                            KeyTransition transition);

constexpr const char* KeyCategoryName(KeyCategory category) {
  switch (category) {
    case KeyCategory::kHangulAscii:
      return "HangulAscii";
    case KeyCategory::kBackspace:
      return "Backspace";
    case KeyCategory::kDelimiter:
      return "Delimiter";
    case KeyCategory::kPureModifier:
      return "PureModifier";
    case KeyCategory::kModifiedShortcut:
      return "ModifiedShortcut";
    case KeyCategory::kUnhandled:
      return "Unhandled";
  }

  return "Unknown";
}

}  // namespace milkyway::engine::key
