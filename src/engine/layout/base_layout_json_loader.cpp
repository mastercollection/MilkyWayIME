#include "engine/layout/base_layout_json_loader.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#include "engine/key/layout_key.h"
#include "nlohmann/json.hpp"

namespace milkyway::engine::layout {
namespace {

using key::LayoutKey;

constexpr std::array<LayoutKey, 51> kSupportedLayoutKeys = {
    LayoutKey::kA,         LayoutKey::kB,
    LayoutKey::kC,         LayoutKey::kD,
    LayoutKey::kE,         LayoutKey::kF,
    LayoutKey::kG,         LayoutKey::kH,
    LayoutKey::kI,         LayoutKey::kJ,
    LayoutKey::kK,         LayoutKey::kL,
    LayoutKey::kM,         LayoutKey::kN,
    LayoutKey::kO,         LayoutKey::kP,
    LayoutKey::kQ,         LayoutKey::kR,
    LayoutKey::kS,         LayoutKey::kT,
    LayoutKey::kU,         LayoutKey::kV,
    LayoutKey::kW,         LayoutKey::kX,
    LayoutKey::kY,         LayoutKey::kZ,
    LayoutKey::kDigit0,    LayoutKey::kDigit1,
    LayoutKey::kDigit2,    LayoutKey::kDigit3,
    LayoutKey::kDigit4,    LayoutKey::kDigit5,
    LayoutKey::kDigit6,    LayoutKey::kDigit7,
    LayoutKey::kDigit8,    LayoutKey::kDigit9,
    LayoutKey::kSpace,     LayoutKey::kTab,
    LayoutKey::kReturn,    LayoutKey::kBackspace,
    LayoutKey::kOem1,      LayoutKey::kOem2,
    LayoutKey::kOem3,      LayoutKey::kOem4,
    LayoutKey::kOem5,      LayoutKey::kOem6,
    LayoutKey::kOem7,      LayoutKey::kOemPlus,
    LayoutKey::kOemComma,  LayoutKey::kOemMinus,
    LayoutKey::kOemPeriod,
};

std::string Error(std::string_view source_name, std::string_view message) {
  std::string error(source_name);
  if (!error.empty()) {
    error += ": ";
  }
  error += message;
  return error;
}

bool ParseSingleCharacterKey(char value, LayoutKey* key) {
  if (value >= 'a' && value <= 'z') {
    *key = static_cast<LayoutKey>(
        static_cast<int>(LayoutKey::kA) + (value - 'a'));
    return true;
  }
  if (value >= 'A' && value <= 'Z') {
    *key = static_cast<LayoutKey>(
        static_cast<int>(LayoutKey::kA) + (value - 'A'));
    return true;
  }
  if (value >= '0' && value <= '9') {
    *key = static_cast<LayoutKey>(
        static_cast<int>(LayoutKey::kDigit0) + (value - '0'));
    return true;
  }

  switch (value) {
    case ';':
      *key = LayoutKey::kOem1;
      return true;
    case '/':
      *key = LayoutKey::kOem2;
      return true;
    case '`':
      *key = LayoutKey::kOem3;
      return true;
    case '[':
      *key = LayoutKey::kOem4;
      return true;
    case '\\':
      *key = LayoutKey::kOem5;
      return true;
    case ']':
      *key = LayoutKey::kOem6;
      return true;
    case '\'':
      *key = LayoutKey::kOem7;
      return true;
    case '=':
      *key = LayoutKey::kOemPlus;
      return true;
    case ',':
      *key = LayoutKey::kOemComma;
      return true;
    case '-':
      *key = LayoutKey::kOemMinus;
      return true;
    case '.':
      *key = LayoutKey::kOemPeriod;
      return true;
    default:
      return false;
  }
}

bool ParseLayoutKey(std::string_view value, LayoutKey* key) {
  if (key == nullptr || value.empty()) {
    return false;
  }

  if (value.size() == 1 && ParseSingleCharacterKey(value.front(), key)) {
    return true;
  }

  for (const LayoutKey candidate : kSupportedLayoutKeys) {
    if (value == key::LayoutKeyName(candidate)) {
      *key = candidate;
      return true;
    }
  }

  return false;
}

LayoutKey ResolveEffectiveLabel(const std::vector<LayoutKeyMapping>& mappings,
                                LayoutKey token_key) {
  for (const LayoutKeyMapping& mapping : mappings) {
    if (mapping.token_key == token_key) {
      return mapping.label_key;
    }
  }
  return token_key;
}

bool HasDuplicateEffectiveLabels(const std::vector<LayoutKeyMapping>& mappings,
                                 std::string* duplicate_label) {
  std::vector<LayoutKey> labels;
  labels.reserve(kSupportedLayoutKeys.size());

  for (const LayoutKey token_key : kSupportedLayoutKeys) {
    const LayoutKey label_key = ResolveEffectiveLabel(mappings, token_key);
    if (std::find(labels.begin(), labels.end(), label_key) != labels.end()) {
      if (duplicate_label != nullptr) {
        *duplicate_label = key::LayoutKeyName(label_key);
      }
      return true;
    }
    labels.push_back(label_key);
  }

  return false;
}

bool ReadFile(const std::filesystem::path& path, std::string* output) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return false;
  }

  std::ostringstream buffer;
  buffer << stream.rdbuf();
  *output = buffer.str();
  return true;
}

}  // namespace

BaseLayoutLoadResult LoadBaseLayoutJson(std::string_view json_text,
                                        std::string_view source_name) {
  BaseLayoutLoadResult result;
  nlohmann::json document;

  try {
    document = nlohmann::json::parse(json_text);
  } catch (const nlohmann::json::parse_error& error) {
    result.error = Error(source_name, error.what());
    return result;
  }

  if (!document.is_object()) {
    result.error = Error(source_name, "root must be an object");
    return result;
  }

  const auto id = document.find("id");
  if (id == document.end() || !id->is_string() || id->get<std::string>().empty()) {
    result.error = Error(source_name, "id must be a non-empty string");
    return result;
  }

  const auto display_name = document.find("displayName");
  if (display_name == document.end() || !display_name->is_string() ||
      display_name->get<std::string>().empty()) {
    result.error = Error(source_name, "displayName must be a non-empty string");
    return result;
  }

  const auto keys = document.find("keys");
  if (keys == document.end() || !keys->is_object()) {
    result.error = Error(source_name, "keys must be an object");
    return result;
  }

  std::vector<LayoutKeyMapping> mappings;
  for (auto iterator = keys->begin(); iterator != keys->end(); ++iterator) {
    LayoutKey token_key = LayoutKey::kUnknown;
    if (!ParseLayoutKey(iterator.key(), &token_key)) {
      result.error = Error(source_name,
                           std::string("unsupported layout token key: ") +
                               iterator.key());
      return result;
    }

    if (!iterator.value().is_string()) {
      result.error = Error(source_name,
                           std::string("label for ") + iterator.key() +
                               " must be a string");
      return result;
    }

    const std::string label = iterator.value().get<std::string>();
    LayoutKey label_key = LayoutKey::kUnknown;
    if (!ParseLayoutKey(label, &label_key)) {
      result.error =
          Error(source_name,
                std::string("unsupported layout label key: ") + label);
      return result;
    }

    mappings.push_back(LayoutKeyMapping{token_key, label_key});
  }

  std::string duplicate_label;
  if (HasDuplicateEffectiveLabels(mappings, &duplicate_label)) {
    result.error =
        Error(source_name,
              std::string("duplicate effective label key: ") + duplicate_label);
    return result;
  }

  result.definition = BaseLayoutDefinition{
      {id->get<std::string>(), display_name->get<std::string>(),
       BaseLayoutInterpretation::kEffectiveBaseLayout},
      std::move(mappings)};
  result.ok = true;
  return result;
}

BaseLayoutDirectoryLoadResult LoadBaseLayoutDirectory(
    const std::filesystem::path& directory) {
  BaseLayoutDirectoryLoadResult result;
  if (!std::filesystem::exists(directory) ||
      !std::filesystem::is_directory(directory)) {
    result.errors.push_back(directory.string() + ": directory does not exist");
    return result;
  }

  std::vector<std::filesystem::path> files;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(directory)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());

  for (const std::filesystem::path& path : files) {
    std::string text;
    if (!ReadFile(path, &text)) {
      result.errors.push_back(path.string() + ": failed to read file");
      continue;
    }

    BaseLayoutLoadResult file_result =
        LoadBaseLayoutJson(text, path.string());
    if (!file_result.ok) {
      result.errors.push_back(file_result.error);
      continue;
    }

    result.definitions.push_back(std::move(file_result.definition));
  }

  return result;
}

}  // namespace milkyway::engine::layout
