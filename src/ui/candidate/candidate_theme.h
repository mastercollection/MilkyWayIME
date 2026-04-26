#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <optional>

namespace milkyway::ui::candidate {

enum class CandidateThemeMode {
  kLight,
  kDark,
};

struct CandidatePalette {
  COLORREF background = RGB(255, 255, 255);
  COLORREF text = RGB(0, 0, 0);
  COLORREF number = RGB(0, 91, 190);
  COLORREF border = RGB(214, 217, 219);
  COLORREF selected_background = RGB(0, 95, 184);
  COLORREF selected_text = RGB(255, 255, 255);
};

std::optional<CandidateThemeMode> ThemeModeFromLightThemeRegistryValue(
    DWORD value);
CandidateThemeMode ChooseCandidateThemeMode(
    std::optional<DWORD> app_uses_light_theme,
    std::optional<DWORD> system_uses_light_theme);
CandidateThemeMode DetectCandidateThemeMode();
bool IsHighContrastEnabled();
CandidatePalette CandidatePaletteFor(CandidateThemeMode mode,
                                      bool high_contrast);

}  // namespace milkyway::ui::candidate

#endif
