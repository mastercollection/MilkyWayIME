#include "ui/candidate/candidate_theme.h"

#if defined(_WIN32)

namespace milkyway::ui::candidate {
namespace {

constexpr wchar_t kPersonalizeKey[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

std::optional<DWORD> ReadDwordFromPersonalizeKey(const wchar_t* value_name) {
  DWORD type = 0;
  DWORD value = 0;
  DWORD byte_count = sizeof(value);
  const LSTATUS status = RegGetValueW(
      HKEY_CURRENT_USER, kPersonalizeKey, value_name, RRF_RT_REG_DWORD, &type,
      &value, &byte_count);
  if (status != ERROR_SUCCESS || type != REG_DWORD) {
    return std::nullopt;
  }

  return value;
}

CandidatePalette LightPalette() {
  return CandidatePalette{
      RGB(255, 255, 255),
      RGB(28, 28, 28),
      RGB(0, 91, 190),
      RGB(214, 217, 219),
      RGB(0, 95, 184),
      RGB(255, 255, 255),
  };
}

CandidatePalette DarkPalette() {
  return CandidatePalette{
      RGB(32, 32, 32),
      RGB(236, 236, 236),
      RGB(130, 184, 255),
      RGB(72, 72, 72),
      RGB(0, 120, 215),
      RGB(255, 255, 255),
  };
}

CandidatePalette HighContrastPalette() {
  return CandidatePalette{
      GetSysColor(COLOR_WINDOW),
      GetSysColor(COLOR_WINDOWTEXT),
      GetSysColor(COLOR_HOTLIGHT),
      GetSysColor(COLOR_WINDOWTEXT),
      GetSysColor(COLOR_HIGHLIGHT),
      GetSysColor(COLOR_HIGHLIGHTTEXT),
  };
}

}  // namespace

std::optional<CandidateThemeMode> ThemeModeFromLightThemeRegistryValue(
    DWORD value) {
  if (value == 0) {
    return CandidateThemeMode::kDark;
  }
  if (value == 1) {
    return CandidateThemeMode::kLight;
  }

  return std::nullopt;
}

CandidateThemeMode ChooseCandidateThemeMode(
    std::optional<DWORD> app_uses_light_theme,
    std::optional<DWORD> system_uses_light_theme) {
  if (const std::optional<CandidateThemeMode> app_mode =
          app_uses_light_theme.has_value()
              ? ThemeModeFromLightThemeRegistryValue(*app_uses_light_theme)
              : std::nullopt;
      app_mode.has_value()) {
    return *app_mode;
  }

  if (const std::optional<CandidateThemeMode> system_mode =
          system_uses_light_theme.has_value()
              ? ThemeModeFromLightThemeRegistryValue(*system_uses_light_theme)
              : std::nullopt;
      system_mode.has_value()) {
    return *system_mode;
  }

  return CandidateThemeMode::kLight;
}

CandidateThemeMode DetectCandidateThemeMode() {
  return ChooseCandidateThemeMode(
      ReadDwordFromPersonalizeKey(L"AppsUseLightTheme"),
      ReadDwordFromPersonalizeKey(L"SystemUsesLightTheme"));
}

bool IsHighContrastEnabled() {
  HIGHCONTRASTW high_contrast = {};
  high_contrast.cbSize = sizeof(high_contrast);
  if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, high_contrast.cbSize,
                             &high_contrast, 0)) {
    return false;
  }

  return (high_contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

CandidatePalette CandidatePaletteFor(CandidateThemeMode mode,
                                      bool high_contrast) {
  if (high_contrast) {
    return HighContrastPalette();
  }

  return mode == CandidateThemeMode::kDark ? DarkPalette() : LightPalette();
}

}  // namespace milkyway::ui::candidate

#endif
