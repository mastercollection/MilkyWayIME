#include "tsf/settings/user_settings.h"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

namespace milkyway::tsf::settings {
namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\MilkyWayIME\\Settings";
constexpr wchar_t kPhysicalLayoutValue[] = L"BaseLayoutId";
constexpr wchar_t kKoreanLayoutValue[] = L"KoreanLayoutId";

std::wstring Utf8ToWide(std::string_view value) {
  if (value.empty()) {
    return {};
  }

  const int length =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), nullptr, 0);
  if (length <= 0) {
    return {};
  }

  std::wstring wide(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                      static_cast<int>(value.size()), wide.data(), length);
  return wide;
}

std::string WideToUtf8(std::wstring_view value) {
  if (value.empty()) {
    return {};
  }

  const int length =
      WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), nullptr, 0, nullptr,
                          nullptr);
  if (length <= 0) {
    return {};
  }

  std::string utf8(static_cast<std::size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                      static_cast<int>(value.size()), utf8.data(), length,
                      nullptr, nullptr);
  return utf8;
}

std::optional<std::wstring> ReadStringValue(const wchar_t* value_name) {
  DWORD type = 0;
  DWORD byte_count = 0;
  LONG result = RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, value_name,
                             RRF_RT_REG_SZ, &type, nullptr, &byte_count);
  if (result != ERROR_SUCCESS || byte_count == 0) {
    return std::nullopt;
  }

  std::wstring value(byte_count / sizeof(wchar_t), L'\0');
  result = RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, value_name,
                        RRF_RT_REG_SZ, &type, value.data(), &byte_count);
  if (result != ERROR_SUCCESS) {
    return std::nullopt;
  }

  while (!value.empty() && value.back() == L'\0') {
    value.pop_back();
  }
  return value;
}

HRESULT WriteStringValue(HKEY key, const wchar_t* value_name,
                         std::string_view value) {
  const std::wstring wide_value = Utf8ToWide(value);
  if (wide_value.empty() && !value.empty()) {
    return E_INVALIDARG;
  }

  const DWORD byte_count =
      static_cast<DWORD>((wide_value.size() + 1) * sizeof(wchar_t));
  const LONG result =
      RegSetValueExW(key, value_name, 0, REG_SZ,
                     reinterpret_cast<const BYTE*>(wide_value.c_str()),
                     byte_count);
  return result == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(result);
}

}  // namespace

UserSettings LoadUserSettings(
    const engine::layout::LayoutRegistry& layout_registry) {
  UserSettings settings{layout_registry.DefaultPhysicalLayout().id,
                        layout_registry.DefaultKoreanLayout().id};

  if (const auto value = ReadStringValue(kPhysicalLayoutValue)) {
    const std::string id = WideToUtf8(*value);
    if (layout_registry.FindPhysicalLayout(id) != nullptr) {
      settings.physical_layout_id = id;
    }
  }

  if (const auto value = ReadStringValue(kKoreanLayoutValue)) {
    const std::string id = WideToUtf8(*value);
    if (layout_registry.FindKoreanLayout(id) != nullptr) {
      settings.korean_layout_id = id;
    }
  }

  return settings;
}

HRESULT SaveUserSettings(const UserSettings& settings) {
  HKEY key = nullptr;
  const LONG create_result =
      RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0,
                      KEY_SET_VALUE, nullptr, &key, nullptr);
  if (create_result != ERROR_SUCCESS) {
    return HRESULT_FROM_WIN32(create_result);
  }

  HRESULT hr = WriteStringValue(key, kPhysicalLayoutValue,
                                settings.physical_layout_id);
  if (SUCCEEDED(hr)) {
    hr = WriteStringValue(key, kKoreanLayoutValue, settings.korean_layout_id);
  }

  RegCloseKey(key);
  return hr;
}

}  // namespace milkyway::tsf::settings

#endif
