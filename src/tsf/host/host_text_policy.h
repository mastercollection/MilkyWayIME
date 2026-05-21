#pragma once

#if defined(_WIN32)

#include <string>
#include <string_view>

namespace milkyway::tsf::host {

enum class TextSurfaceFamily {
  kUnknown,
  kNativeEdit,
  kNativeRichEdit,
  kFrameworkAvalonia,
  kWindowOnly,
};

enum class HostTextBackendPolicy {
  kNone,
  kNativeEditSelectionReplace,
  kNativeRichEditSelectionReplace,
  kKnownUiaWholeValueDisabled,
  kUiaAvaloniaSearchBoxWholeValueReplace,
  kWindowOnlyExcluded,
};

struct HostTextTarget {
  std::wstring process_name;
  std::wstring view_class;
  std::wstring focus_class;
  std::wstring top_level_class;
  bool is_transitory = false;
};

inline bool StartsWith(std::wstring_view text, std::wstring_view prefix) {
  return text.size() >= prefix.size() &&
         text.substr(0, prefix.size()) == prefix;
}

inline std::wstring NormalizeHostClass(std::wstring_view class_name) {
  if (StartsWith(class_name, L"Avalonia-")) {
    return L"Avalonia";
  }
  return std::wstring(class_name);
}

inline bool HasNormalizedClass(const HostTextTarget& target,
                               std::wstring_view class_name) {
  return NormalizeHostClass(target.view_class) == class_name ||
         NormalizeHostClass(target.focus_class) == class_name ||
         NormalizeHostClass(target.top_level_class) == class_name;
}

inline TextSurfaceFamily SelectTextSurfaceFamily(const HostTextTarget& target) {
  if (HasNormalizedClass(target, L"UnityWndClass") ||
      HasNormalizedClass(target, L"org.wezfurlong.wezterm")) {
    return TextSurfaceFamily::kWindowOnly;
  }

  if (NormalizeHostClass(target.view_class) == L"Edit") {
    return TextSurfaceFamily::kNativeEdit;
  }
  if (NormalizeHostClass(target.view_class) == L"RICHEDIT50W") {
    return TextSurfaceFamily::kNativeRichEdit;
  }
  if (HasNormalizedClass(target, L"Avalonia")) {
    return TextSurfaceFamily::kFrameworkAvalonia;
  }

  return TextSurfaceFamily::kUnknown;
}

inline HostTextBackendPolicy SelectHostTextBackendPolicy(
    const HostTextTarget& target) {
  switch (SelectTextSurfaceFamily(target)) {
    case TextSurfaceFamily::kNativeEdit:
      return HostTextBackendPolicy::kNativeEditSelectionReplace;
    case TextSurfaceFamily::kNativeRichEdit:
      return HostTextBackendPolicy::kNativeRichEditSelectionReplace;
    case TextSurfaceFamily::kWindowOnly:
      return HostTextBackendPolicy::kWindowOnlyExcluded;
    case TextSurfaceFamily::kFrameworkAvalonia:
    case TextSurfaceFamily::kUnknown:
      return HostTextBackendPolicy::kNone;
  }
  return HostTextBackendPolicy::kNone;
}

inline const wchar_t* TextSurfaceFamilyName(TextSurfaceFamily family) {
  switch (family) {
    case TextSurfaceFamily::kUnknown:
      return L"unknown";
    case TextSurfaceFamily::kNativeEdit:
      return L"native_edit";
    case TextSurfaceFamily::kNativeRichEdit:
      return L"native_richedit";
    case TextSurfaceFamily::kFrameworkAvalonia:
      return L"framework_avalonia";
    case TextSurfaceFamily::kWindowOnly:
      return L"window_only";
  }
  return L"unknown";
}

inline const wchar_t* HostTextPolicyName(HostTextBackendPolicy policy) {
  switch (policy) {
    case HostTextBackendPolicy::kNone:
      return L"none";
    case HostTextBackendPolicy::kNativeEditSelectionReplace:
      return L"native_edit_selection_replace";
    case HostTextBackendPolicy::kNativeRichEditSelectionReplace:
      return L"native_richedit_selection_replace";
    case HostTextBackendPolicy::kKnownUiaWholeValueDisabled:
      return L"known_uia_whole_value_disabled";
    case HostTextBackendPolicy::kUiaAvaloniaSearchBoxWholeValueReplace:
      return L"uia_avalonia_searchbox_whole_value_replace";
    case HostTextBackendPolicy::kWindowOnlyExcluded:
      return L"window_only_excluded";
  }
  return L"unknown";
}

}  // namespace milkyway::tsf::host

#endif
