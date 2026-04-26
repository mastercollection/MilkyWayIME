#pragma once

#include <optional>
#include <vector>

#include "engine/key/normalized_key_event.h"
#include "engine/layout/layout_types.h"
#include "engine/state/modifier_state.h"

namespace milkyway::engine::layout {

class LayoutRegistry {
 public:
  LayoutRegistry();

  const std::vector<BaseLayout>& base_layouts() const;
  const std::vector<KoreanLayoutMapping>& korean_layouts() const;

  bool AddBaseLayout(BaseLayoutDefinition definition);

  const BaseLayout& DefaultBaseLayout() const;
  const KoreanLayoutMapping& DefaultKoreanLayout() const;
  const BaseLayout* FindBaseLayout(
      const BaseLayoutId& id) const;
  const KoreanLayoutMapping* FindKoreanLayout(const KoreanLayoutId& id) const;
  std::string ResolveLibhangulKeyboardId(
      const KoreanLayoutId& korean_layout_id) const;
  key::LayoutKey ResolveInputLabelKey(const key::PhysicalKey& key) const;
  key::LayoutKey ResolveBaseLayoutLabelKey(
      const BaseLayoutId& base_layout_id,
      key::LayoutKey token_key) const;
  key::LayoutKey ResolveHangulTokenKey(
      const BaseLayoutId& base_layout_id,
      key::LayoutKey input_label_key) const;
  key::NormalizedKeyEvent NormalizeKeyEvent(
      const BaseLayoutId& base_layout_id,
      const key::PhysicalKey& key,
      const state::ModifierState& modifiers,
      key::KeyTransition transition) const;
  ResolvedHangulInput ResolveHangulInput(
      const KoreanLayoutId& korean_layout_id,
      const HangulMappingKey& key) const;

 private:
  std::vector<BaseLayoutDefinition> base_layout_definitions_;
  std::vector<BaseLayout> base_layouts_;
};

}  // namespace milkyway::engine::layout
