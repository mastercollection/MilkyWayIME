#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <optional>

#include "engine/layout/layout_registry.h"
#include "engine/layout/layout_types.h"

namespace milkyway::tsf::settings {

struct RawUserSettings {
  std::optional<engine::layout::BaseLayoutId> base_layout_id;
  std::optional<engine::layout::KoreanLayoutId> korean_layout_id;
};

struct UserSettings {
  engine::layout::BaseLayoutId base_layout_id;
  engine::layout::KoreanLayoutId korean_layout_id;
};

class SettingsStore final {
 public:
  RawUserSettings Load() const;
  HRESULT Save(const UserSettings& settings) const;
};

UserSettings ResolveUserSettings(
    const RawUserSettings& raw_settings,
    const engine::layout::LayoutRegistry& layout_registry);

}  // namespace milkyway::tsf::settings

#endif
