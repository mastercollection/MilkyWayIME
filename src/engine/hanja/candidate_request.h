#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace milkyway::engine::hanja {

enum class CandidateKind {
  kHanjaForward,
  kHanjaReverse,
  kSymbol,
};

struct CandidateRequest {
  std::string query_text;
  CandidateKind kind = CandidateKind::kHanjaForward;
};

struct Candidate {
  std::string key;
  std::string value;
  std::string comment;
  CandidateKind kind = CandidateKind::kHanjaForward;
};

struct SelectionHanjaPrefixRequest {
  CandidateRequest request;
  std::size_t matched_byte_length = 0;
  std::size_t matched_utf16_length = 0;
};

struct CaretHanjaRun {
  std::string text;
  CandidateKind kind = CandidateKind::kHanjaForward;
  std::size_t utf16_length = 0;
};

std::optional<CandidateRequest> CreateCandidateRequestFromPreedit(
    std::string_view preedit);

std::vector<SelectionHanjaPrefixRequest> CreateSelectionHanjaPrefixRequests(
    std::string_view selected_text);

std::vector<SelectionHanjaPrefixRequest>
CreateSelectionHanjaReversePrefixRequests(std::string_view selected_text);

std::optional<CaretHanjaRun> CreateCaretHanjaRun(
    std::string_view text_before_caret);

}  // namespace milkyway::engine::hanja
