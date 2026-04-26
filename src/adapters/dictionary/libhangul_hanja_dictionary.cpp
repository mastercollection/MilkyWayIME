#include "adapters/dictionary/libhangul_hanja_dictionary.h"

#include <filesystem>
#include <new>
#include <string>
#include <utility>

#include "hangul.h"

namespace milkyway::adapters::dictionary {
namespace {

std::filesystem::path SourceDataDirectory() {
  std::filesystem::path source_file = std::filesystem::path(__FILE__);
  for (int i = 0; i < 4; ++i) {
    source_file = source_file.parent_path();
  }
  return source_file / "external" / "libhangul" / "data";
}

const char* NullableCString(const std::string& text) {
  return text.empty() ? nullptr : text.c_str();
}

std::string PathToDictionaryArgument(const std::filesystem::path& path) {
  if (path.empty()) {
    return {};
  }

  return path.string();
}

std::string LookupCacheKey(const engine::hanja::CandidateRequest& request) {
  std::string key =
      request.kind == engine::hanja::CandidateKind::kSymbol ? "S:" : "H:";
  key += request.query_text;
  return key;
}

}  // namespace

struct LibhangulHanjaDictionary::HanjaTableHandle {
  explicit HanjaTableHandle(std::filesystem::path path)
      : path(std::move(path)),
        path_argument(PathToDictionaryArgument(this->path)),
        table(hanja_table_load(NullableCString(path_argument))) {}

  ~HanjaTableHandle() {
    if (table != nullptr) {
      hanja_table_delete(table);
    }
  }

  std::filesystem::path path;
  std::string path_argument;
  HanjaTable* table = nullptr;
};

HanjaDictionaryPaths DefaultHanjaDictionaryPaths() {
  const std::filesystem::path data_directory = SourceDataDirectory();
  if (data_directory.empty()) {
    return {};
  }

  return HanjaDictionaryPaths{
      data_directory / "hanja" / "hanja.txt",
      data_directory / "hanja" / "mssymbol.txt",
  };
}

LibhangulHanjaDictionary::LibhangulHanjaDictionary(HanjaDictionaryPaths paths)
    : paths_(std::move(paths)) {}

LibhangulHanjaDictionary::~LibhangulHanjaDictionary() {
  delete hanja_table_;
  delete symbol_table_;
}

void LibhangulHanjaDictionary::Preload() {
  TableForKind(engine::hanja::CandidateKind::kHanja);
  TableForKind(engine::hanja::CandidateKind::kSymbol);
}

std::vector<engine::hanja::Candidate> LibhangulHanjaDictionary::Lookup(
    const engine::hanja::CandidateRequest& request) {
  const std::string cache_key = LookupCacheKey(request);
  const auto cached = cache_.find(cache_key);
  if (cached != cache_.end()) {
    return cached->second;
  }

  HanjaTableHandle* table_handle = TableForKind(request.kind);
  if (table_handle == nullptr || table_handle->table == nullptr) {
    return {};
  }

  HanjaList* list =
      hanja_table_match_exact(table_handle->table, request.query_text.c_str());
  if (list == nullptr) {
    return {};
  }

  std::vector<engine::hanja::Candidate> candidates;
  const int size = hanja_list_get_size(list);
  if (size > 0) {
    candidates.reserve(static_cast<std::size_t>(size));
  }

  for (int index = 0; index < size; ++index) {
    const auto unsigned_index = static_cast<unsigned int>(index);
    const char* key = hanja_list_get_nth_key(list, unsigned_index);
    const char* value = hanja_list_get_nth_value(list, unsigned_index);
    const char* comment = hanja_list_get_nth_comment(list, unsigned_index);
    if (value == nullptr || value[0] == '\0') {
      continue;
    }

    candidates.push_back(engine::hanja::Candidate{
        key != nullptr ? key : "",
        value,
        comment != nullptr ? comment : "",
        request.kind,
    });
  }

  hanja_list_delete(list);
  cache_.emplace(cache_key, candidates);
  return candidates;
}

LibhangulHanjaDictionary::HanjaTableHandle*
LibhangulHanjaDictionary::TableForKind(engine::hanja::CandidateKind kind) {
  HanjaTableHandle** target = kind == engine::hanja::CandidateKind::kSymbol
                                  ? &symbol_table_
                                  : &hanja_table_;
  if (*target != nullptr) {
    return *target;
  }

  const std::filesystem::path path =
      kind == engine::hanja::CandidateKind::kSymbol ? paths_.symbol_path
                                                    : paths_.hanja_path;
  if (kind == engine::hanja::CandidateKind::kSymbol && path.empty()) {
    return nullptr;
  }

  *target = new (std::nothrow) HanjaTableHandle(path);
  return *target;
}

}  // namespace milkyway::adapters::dictionary
