#pragma once

#include <optional>
#include <vector>

#include "engine/key/normalized_key_event.h"
#include "engine/layout/layout_types.h"
#include "engine/state/modifier_state.h"

namespace milkyway::engine::layout {

class LayoutRegistry {
 public:
  LayoutRegistry() = default;

  const std::vector<PhysicalLayout>& physical_layouts() const;
  const std::vector<KoreanLayoutMapping>& korean_layouts() const;

  const PhysicalLayout& DefaultPhysicalLayout() const;
  const KoreanLayoutMapping& DefaultKoreanLayout() const;
  const PhysicalLayout* FindPhysicalLayout(
      const PhysicalLayoutId& id) const;
  const KoreanLayoutMapping* FindKoreanLayout(const KoreanLayoutId& id) const;
  key::BaseLayoutKey ResolveBaseLayoutKey(
      const PhysicalLayoutId& physical_layout_id,
      const key::PhysicalKey& key) const;
  key::NormalizedKeyEvent NormalizeKeyEvent(
      const PhysicalLayoutId& physical_layout_id,
      const key::PhysicalKey& key,
      const state::ModifierState& modifiers,
      key::KeyTransition transition) const;
  ResolvedHangulInput ResolveHangulInput(
      const KoreanLayoutId& korean_layout_id,
      const HangulMappingKey& key) const;
};

}  // namespace milkyway::engine::layout
