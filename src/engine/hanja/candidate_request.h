#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace milkyway::engine::hanja {

enum class CandidateKind {
  kHanja,
  kSymbol,
};

struct CandidateRequest {
  std::string query_text;
  CandidateKind kind = CandidateKind::kHanja;
};

struct Candidate {
  std::string key;
  std::string value;
  std::string comment;
  CandidateKind kind = CandidateKind::kHanja;
};

std::optional<CandidateRequest> CreateCandidateRequestFromPreedit(
    std::string_view preedit);

}  // namespace milkyway::engine::hanja
