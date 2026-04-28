#pragma once

#include <vector>

#include "engine/hanja/candidate_request.h"

namespace milkyway::adapters::dictionary {

class LibhangulHanjaDictionary {
 public:
  LibhangulHanjaDictionary() = default;
  ~LibhangulHanjaDictionary() = default;

  LibhangulHanjaDictionary(const LibhangulHanjaDictionary&) = delete;
  LibhangulHanjaDictionary& operator=(const LibhangulHanjaDictionary&) = delete;

  std::vector<engine::hanja::Candidate> Lookup(
      const engine::hanja::CandidateRequest& request);
};

}  // namespace milkyway::adapters::dictionary
