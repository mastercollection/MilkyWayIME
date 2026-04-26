#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "engine/layout/layout_types.h"

namespace milkyway::engine::layout {

struct BaseLayoutLoadResult {
  bool ok = false;
  BaseLayoutDefinition definition;
  std::string error;
};

struct BaseLayoutDirectoryLoadResult {
  std::vector<BaseLayoutDefinition> definitions;
  std::vector<std::string> errors;
};

BaseLayoutLoadResult LoadBaseLayoutJson(std::string_view json_text,
                                        std::string_view source_name);

BaseLayoutDirectoryLoadResult LoadBaseLayoutDirectory(
    const std::filesystem::path& directory);

}  // namespace milkyway::engine::layout
