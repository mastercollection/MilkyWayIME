#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "engine/layout/layout_registry.h"
#include "engine/layout/layout_types.h"

namespace milkyway::tsf::settings {

struct UserSettings {
  engine::layout::PhysicalLayoutId physical_layout_id;
  engine::layout::KoreanLayoutId korean_layout_id;
};

UserSettings LoadUserSettings(
    const engine::layout::LayoutRegistry& layout_registry);
HRESULT SaveUserSettings(const UserSettings& settings);

}  // namespace milkyway::tsf::settings

#endif
