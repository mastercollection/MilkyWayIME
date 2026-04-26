#pragma once

#if defined(_WIN32)

#include <filesystem>

namespace milkyway::tsf::settings {

std::filesystem::path UserBaseLayoutDirectory();
std::filesystem::path UserKoreanLayoutDirectory();

bool EnsureDirectoryExists(const std::filesystem::path& directory);
bool OpenDirectoryInExplorer(const std::filesystem::path& directory);

}  // namespace milkyway::tsf::settings

#endif  // defined(_WIN32)
