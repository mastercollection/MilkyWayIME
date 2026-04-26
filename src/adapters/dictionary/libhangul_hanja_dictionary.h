#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "engine/hanja/candidate_request.h"

namespace milkyway::adapters::dictionary {

struct HanjaDictionaryPaths {
  std::filesystem::path hanja_binary_path;
  std::filesystem::path symbol_binary_path;
};

HanjaDictionaryPaths DefaultHanjaDictionaryPaths();

class LibhangulHanjaDictionary {
 public:
  struct SharedState;

  explicit LibhangulHanjaDictionary(
      HanjaDictionaryPaths paths = DefaultHanjaDictionaryPaths());
  ~LibhangulHanjaDictionary();

  LibhangulHanjaDictionary(const LibhangulHanjaDictionary&) = delete;
  LibhangulHanjaDictionary& operator=(const LibhangulHanjaDictionary&) = delete;

  std::vector<engine::hanja::Candidate> Lookup(
      const engine::hanja::CandidateRequest& request);
  void Preload();

 private:
  HanjaDictionaryPaths paths_;
  SharedState* state_ = nullptr;
};

}  // namespace milkyway::adapters::dictionary
