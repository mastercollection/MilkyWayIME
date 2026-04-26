#include "adapters/dictionary/libhangul_hanja_dictionary.h"

#include <filesystem>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
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
  std::string key;
  switch (request.kind) {
    case engine::hanja::CandidateKind::kSymbol:
      key = "S:";
      break;
    case engine::hanja::CandidateKind::kHanjaReverse:
      key = "R:";
      break;
    case engine::hanja::CandidateKind::kHanjaForward:
      key = "H:";
      break;
  }
  key += request.query_text;
  return key;
}

}  // namespace

struct HanjaTableHandle {
  explicit HanjaTableHandle(std::filesystem::path binary_path)
      : binary_path(std::move(binary_path)),
        binary_path_argument(PathToDictionaryArgument(this->binary_path)),
        table(hanja_table_load_binary(NullableCString(binary_path_argument),
                                      nullptr)) {}

  ~HanjaTableHandle() {
    if (table != nullptr) {
      hanja_table_delete(table);
    }
  }

  std::filesystem::path binary_path;
  std::string binary_path_argument;
  HanjaTable* table = nullptr;
};

struct LibhangulHanjaDictionary::SharedState {
  explicit SharedState(HanjaDictionaryPaths paths) : paths(std::move(paths)) {}

  HanjaTableHandle* TableForKind(engine::hanja::CandidateKind kind) {
    HanjaTableHandle** target =
        kind == engine::hanja::CandidateKind::kSymbol ? &symbol_table
                                                      : &hanja_table;
    if (*target != nullptr) {
      return *target;
    }

    const std::filesystem::path binary_path =
        kind == engine::hanja::CandidateKind::kSymbol ? paths.symbol_binary_path
                                                      : paths.hanja_binary_path;
    if (binary_path.empty()) {
      return nullptr;
    }

    *target = new (std::nothrow) HanjaTableHandle(binary_path);
    return *target;
  }

  ~SharedState() {
    delete hanja_table;
    delete symbol_table;
  }

  std::mutex mutex;
  HanjaDictionaryPaths paths;
  HanjaTableHandle* hanja_table = nullptr;
  HanjaTableHandle* symbol_table = nullptr;
  std::unordered_map<std::string, std::vector<engine::hanja::Candidate>> cache;
};

std::mutex g_shared_state_mutex;
LibhangulHanjaDictionary::SharedState* g_shared_state = nullptr;

LibhangulHanjaDictionary::SharedState* SharedStateForPaths(
    HanjaDictionaryPaths paths) {
  std::lock_guard<std::mutex> lock(g_shared_state_mutex);
  if (g_shared_state == nullptr) {
    g_shared_state = new (std::nothrow)
        LibhangulHanjaDictionary::SharedState(std::move(paths));
  }
  return g_shared_state;
}

HanjaDictionaryPaths DefaultHanjaDictionaryPaths() {
  const std::filesystem::path data_directory = SourceDataDirectory();
  if (data_directory.empty()) {
    return {};
  }

  return HanjaDictionaryPaths{
      data_directory / "hanja" / "hanja.bin",
      data_directory / "hanja" / "mssymbol.bin",
  };
}

LibhangulHanjaDictionary::LibhangulHanjaDictionary(HanjaDictionaryPaths paths)
    : paths_(std::move(paths)), state_(SharedStateForPaths(paths_)) {}

LibhangulHanjaDictionary::~LibhangulHanjaDictionary() = default;

void LibhangulHanjaDictionary::Preload() {
  if (state_ == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(state_->mutex);
  state_->TableForKind(engine::hanja::CandidateKind::kHanjaForward);
  state_->TableForKind(engine::hanja::CandidateKind::kSymbol);
}

std::vector<engine::hanja::Candidate> LibhangulHanjaDictionary::Lookup(
    const engine::hanja::CandidateRequest& request) {
  if (state_ == nullptr) {
    return {};
  }

  std::lock_guard<std::mutex> lock(state_->mutex);
  const std::string cache_key = LookupCacheKey(request);
  const auto cached = state_->cache.find(cache_key);
  if (cached != state_->cache.end()) {
    return cached->second;
  }

  HanjaTableHandle* table_handle = state_->TableForKind(request.kind);
  if (table_handle == nullptr || table_handle->table == nullptr) {
    return {};
  }

  HanjaList* list = request.kind == engine::hanja::CandidateKind::kHanjaReverse
                        ? hanja_table_match_exact_value(
                              table_handle->table, request.query_text.c_str())
                        : hanja_table_match_exact(table_handle->table,
                                                  request.query_text.c_str());
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
  state_->cache.emplace(cache_key, candidates);
  return candidates;
}

}  // namespace milkyway::adapters::dictionary
