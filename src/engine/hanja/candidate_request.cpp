#include "engine/hanja/candidate_request.h"

#include <cstdint>
#include <vector>

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

struct Utf8Scalar {
  std::uint32_t code_point = 0;
  std::size_t byte_start = 0;
  std::size_t byte_end = 0;
  std::size_t utf16_length = 0;
};

std::optional<Utf8Scalar> DecodeNextUtf8Scalar(std::string_view text,
                                               std::size_t offset) {
  if (offset >= text.size()) {
    return std::nullopt;
  }

  const auto byte = [](char value) {
    return static_cast<unsigned char>(value);
  };

  const unsigned char first = byte(text[offset]);
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

  if (offset + length > text.size()) {
    return std::nullopt;
  }

  for (std::size_t index = 1; index < length; ++index) {
    const unsigned char continuation = byte(text[offset + index]);
    if ((continuation & 0xC0) != 0x80) {
      return std::nullopt;
    }
    code_point = (code_point << 6) | (continuation & 0x3F);
  }

  if (code_point < minimum || code_point > 0x10FFFF ||
      (code_point >= 0xD800 && code_point <= 0xDFFF)) {
    return std::nullopt;
  }

  return Utf8Scalar{
      code_point,
      offset,
      offset + length,
      code_point > 0xFFFF ? std::size_t{2} : std::size_t{1},
  };
}

bool IsHangulSyllable(std::uint32_t code_point) {
  return code_point >= 0xAC00 && code_point <= 0xD7A3;
}

bool IsHangulCompatibilityJamo(std::uint32_t code_point) {
  return code_point >= 0x3130 && code_point <= 0x318F;
}

bool IsCjkIdeograph(std::uint32_t code_point) {
  return (code_point >= 0x3400 && code_point <= 0x9FFF) ||
         (code_point >= 0xF900 && code_point <= 0xFAFF) ||
         (code_point >= 0x20000 && code_point <= 0x2FA1F);
}

std::vector<SelectionHanjaPrefixRequest> CreatePrefixRequests(
    std::string_view selected_text, CandidateKind kind,
    bool (*predicate)(std::uint32_t)) {
  std::vector<std::size_t> prefix_byte_ends;
  std::vector<std::size_t> prefix_utf16_lengths;

  std::size_t offset = 0;
  std::size_t utf16_length = 0;
  while (offset < selected_text.size()) {
    const std::optional<Utf8Scalar> scalar =
        DecodeNextUtf8Scalar(selected_text, offset);
    if (!scalar.has_value() || !predicate(scalar->code_point)) {
      break;
    }

    offset = scalar->byte_end;
    utf16_length += scalar->utf16_length;
    prefix_byte_ends.push_back(offset);
    prefix_utf16_lengths.push_back(utf16_length);
  }

  std::vector<SelectionHanjaPrefixRequest> requests;
  if (prefix_byte_ends.empty()) {
    return requests;
  }

  requests.reserve(prefix_byte_ends.size());
  for (std::size_t length = prefix_byte_ends.size(); length >= 1; --length) {
    requests.push_back(SelectionHanjaPrefixRequest{
        CandidateRequest{
            std::string(selected_text.substr(0, prefix_byte_ends[length - 1])),
            kind,
        },
        prefix_byte_ends[length - 1],
        prefix_utf16_lengths[length - 1],
    });

    if (length == 1) {
      break;
    }
  }

  return requests;
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
    return CandidateRequest{std::string(preedit), CandidateKind::kHanjaForward};
  }

  if (IsHangulCompatibilityJamo(*code_point)) {
    return CandidateRequest{std::string(preedit), CandidateKind::kSymbol};
  }

  return std::nullopt;
}

std::vector<SelectionHanjaPrefixRequest> CreateSelectionHanjaPrefixRequests(
    std::string_view selected_text) {
  return CreatePrefixRequests(selected_text, CandidateKind::kHanjaForward,
                              IsHangulSyllable);
}

std::vector<SelectionHanjaPrefixRequest>
CreateSelectionHanjaReversePrefixRequests(std::string_view selected_text) {
  return CreatePrefixRequests(selected_text, CandidateKind::kHanjaReverse,
                              IsCjkIdeograph);
}

std::optional<CaretHanjaRun> CreateCaretHanjaRun(
    std::string_view text_before_caret) {
  if (text_before_caret.empty()) {
    return std::nullopt;
  }

  std::vector<Utf8Scalar> scalars;
  std::size_t offset = 0;
  while (offset < text_before_caret.size()) {
    std::optional<Utf8Scalar> scalar =
        DecodeNextUtf8Scalar(text_before_caret, offset);
    if (!scalar.has_value()) {
      return std::nullopt;
    }
    offset = scalar->byte_end;
    scalars.push_back(*scalar);
  }

  if (scalars.empty()) {
    return std::nullopt;
  }

  const Utf8Scalar& last = scalars.back();
  CandidateKind kind = CandidateKind::kHanjaForward;
  bool (*predicate)(std::uint32_t) = nullptr;
  if (IsHangulSyllable(last.code_point)) {
    kind = CandidateKind::kHanjaForward;
    predicate = IsHangulSyllable;
  } else if (IsCjkIdeograph(last.code_point)) {
    kind = CandidateKind::kHanjaReverse;
    predicate = IsCjkIdeograph;
  } else {
    return std::nullopt;
  }

  std::size_t first_scalar = scalars.size() - 1;
  std::size_t utf16_length = 0;
  while (true) {
    const Utf8Scalar& scalar = scalars[first_scalar];
    if (!predicate(scalar.code_point)) {
      ++first_scalar;
      break;
    }
    utf16_length += scalar.utf16_length;
    if (first_scalar == 0) {
      break;
    }
    --first_scalar;
  }

  if (first_scalar >= scalars.size()) {
    return std::nullopt;
  }

  return CaretHanjaRun{
      std::string(text_before_caret.substr(scalars[first_scalar].byte_start)),
      kind,
      utf16_length,
  };
}

}  // namespace milkyway::engine::hanja
