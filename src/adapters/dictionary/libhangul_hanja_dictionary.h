#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/hanja/candidate_request.h"

namespace milkyway::adapters::dictionary {

struct HanjaDictionaryPaths {
  std::filesystem::path hanja_path;
  std::filesystem::path symbol_path;
};

HanjaDictionaryPaths DefaultHanjaDictionaryPaths();

class LibhangulHanjaDictionary {
 public:
  explicit LibhangulHanjaDictionary(
      HanjaDictionaryPaths paths = DefaultHanjaDictionaryPaths());
  ~LibhangulHanjaDictionary();

  LibhangulHanjaDictionary(const LibhangulHanjaDictionary&) = delete;
  LibhangulHanjaDictionary& operator=(const LibhangulHanjaDictionary&) = delete;

  std::vector<engine::hanja::Candidate> Lookup(
      const engine::hanja::CandidateRequest& request);
  void Preload();

 private:
  struct HanjaTableHandle;

  HanjaTableHandle* TableForKind(engine::hanja::CandidateKind kind);

  HanjaDictionaryPaths paths_;
  HanjaTableHandle* hanja_table_ = nullptr;
  HanjaTableHandle* symbol_table_ = nullptr;
  std::unordered_map<std::string, std::vector<engine::hanja::Candidate>> cache_;
};

}  // namespace milkyway::adapters::dictionary
