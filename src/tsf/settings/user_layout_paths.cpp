#include "tsf/settings/user_layout_paths.h"

#if defined(_WIN32)

#include <windows.h>
#include <shellapi.h>

#include <string>
#include <system_error>

namespace milkyway::tsf::settings {
namespace {

std::filesystem::path UserLayoutsRootDirectory() {
  const DWORD required = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
  if (required == 0) {
    return {};
  }

  std::wstring appdata(required, L'\0');
  const DWORD length =
      GetEnvironmentVariableW(L"APPDATA", appdata.data(), required);
  if (length == 0 || length >= required) {
    return {};
  }

  appdata.resize(length);
  return std::filesystem::path(appdata) / L"MilkyWayIME" / L"layouts";
}

}  // namespace

std::filesystem::path UserBaseLayoutDirectory() {
  const std::filesystem::path root = UserLayoutsRootDirectory();
  return root.empty() ? std::filesystem::path{} : root / L"base";
}

std::filesystem::path UserKoreanLayoutDirectory() {
  const std::filesystem::path root = UserLayoutsRootDirectory();
  return root.empty() ? std::filesystem::path{} : root / L"korean";
}

bool EnsureDirectoryExists(const std::filesystem::path& directory) {
  if (directory.empty()) {
    return false;
  }

  std::error_code error_code;
  if (std::filesystem::is_directory(directory, error_code)) {
    return true;
  }

  error_code.clear();
  std::filesystem::create_directories(directory, error_code);
  if (error_code) {
    return false;
  }

  error_code.clear();
  return std::filesystem::is_directory(directory, error_code);
}

bool OpenDirectoryInExplorer(const std::filesystem::path& directory) {
  if (directory.empty()) {
    return false;
  }

  const HINSTANCE result =
      ShellExecuteW(nullptr, L"open", directory.c_str(), nullptr, nullptr,
                    SW_SHOWNORMAL);
  return reinterpret_cast<INT_PTR>(result) > 32;
}

}  // namespace milkyway::tsf::settings

#endif  // defined(_WIN32)
