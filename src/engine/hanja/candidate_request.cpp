#include "engine/hanja/candidate_request.h"

#include <cstdint>

namespace milkyway::engine::hanja {
namespace {

std::optional<std::uint32_t> DecodeSingleUtf8Scalar(std::string_view text) {
  if (text.empty()) {
    return std::nullopt;
  }

  const auto byte = [](char value) {
    return static_cast<unsigned char>(value);
  };

  const unsigned char first = byte(text[0]);
  std::uint32_t code_point = 0;
  std::size_t length = 0;
  std::uint32_t minimum = 0;

  if (first <= 0x7F) {
    code_point = first;
    length = 1;
  } else if ((first & 0xE0) == 0xC0) {
    code_point = first & 0x1F;
    length = 2;
    minimum = 0x80;
  } else if ((first & 0xF0) == 0xE0) {
    code_point = first & 0x0F;
    length = 3;
    minimum = 0x800;
  } else if ((first & 0xF8) == 0xF0) {
    code_point = first & 0x07;
    length = 4;
    minimum = 0x10000;
  } else {
    return std::nullopt;
  }

  if (text.size() != length) {
    return std::nullopt;
  }

  for (std::size_t index = 1; index < length; ++index) {
    const unsigned char continuation = byte(text[index]);
    if ((continuation & 0xC0) != 0x80) {
      return std::nullopt;
    }
    code_point = (code_point << 6) | (continuation & 0x3F);
  }

  if (code_point < minimum || code_point > 0x10FFFF ||
      (code_point >= 0xD800 && code_point <= 0xDFFF)) {
    return std::nullopt;
  }

  return code_point;
}

bool IsHangulSyllable(std::uint32_t code_point) {
  return code_point >= 0xAC00 && code_point <= 0xD7A3;
}

bool IsHangulCompatibilityJamo(std::uint32_t code_point) {
  return code_point >= 0x3130 && code_point <= 0x318F;
}

}  // namespace

std::optional<CandidateRequest> CreateCandidateRequestFromPreedit(
    std::string_view preedit) {
  const std::optional<std::uint32_t> code_point =
      DecodeSingleUtf8Scalar(preedit);
  if (!code_point.has_value()) {
    return std::nullopt;
  }

  if (IsHangulSyllable(*code_point)) {
    return CandidateRequest{std::string(preedit), CandidateKind::kHanja};
  }

  if (IsHangulCompatibilityJamo(*code_point)) {
    return CandidateRequest{std::string(preedit), CandidateKind::kSymbol};
  }

  return std::nullopt;
}

}  // namespace milkyway::engine::hanja
