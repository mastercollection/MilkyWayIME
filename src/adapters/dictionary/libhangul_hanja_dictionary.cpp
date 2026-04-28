#include "adapters/dictionary/libhangul_hanja_dictionary.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "adapters/dictionary/generated_hanja_data.h"

namespace milkyway::adapters::dictionary {
namespace {

using engine::hanja::Candidate;
using engine::hanja::CandidateKind;
using engine::hanja::CandidateRequest;
using generated::HanjaEntry;
using generated::RangeEntry;

const RangeEntry* FindRange(const RangeEntry* ranges, std::uint32_t count,
                            const std::string& query) {
  std::uint32_t low = 0;
  std::uint32_t high = count;
  while (low < high) {
    const std::uint32_t mid = low + (high - low) / 2;
    const int comparison =
        std::strcmp(generated::StringAt(ranges[mid].text), query.c_str());
    if (comparison < 0) {
      low = mid + 1;
    } else if (comparison > 0) {
      high = mid;
    } else {
      return ranges + mid;
    }
  }
  return nullptr;
}

std::vector<Candidate> CollectCandidates(const HanjaEntry* entries,
                                         const std::uint32_t* order,
                                         const RangeEntry& range,
                                         CandidateKind kind) {
  std::vector<Candidate> candidates;
  candidates.reserve(range.count);
  for (std::uint32_t index = 0; index < range.count; ++index) {
    const HanjaEntry& entry = entries[order[range.first + index]];
    const char* key = generated::StringAt(entry.key);
    const char* value = generated::StringAt(entry.value);
    const char* comment = generated::StringAt(entry.comment);
    if (value[0] == '\0') {
      continue;
    }

    if (kind == CandidateKind::kHanjaReverse) {
      candidates.push_back(Candidate{value, key, comment, kind});
    } else {
      candidates.push_back(Candidate{key, value, comment, kind});
    }
  }
  return candidates;
}

}  // namespace

std::vector<Candidate> LibhangulHanjaDictionary::Lookup(
    const CandidateRequest& request) {
  switch (request.kind) {
    case CandidateKind::kSymbol: {
      const RangeEntry* range =
          FindRange(generated::kSymbolKeyRanges,
                    generated::kSymbolKeyRangeCount, request.query_text);
      if (range == nullptr) {
        return {};
      }
      return CollectCandidates(generated::kSymbolEntries,
                               generated::kSymbolKeyEntryOrder, *range,
                               CandidateKind::kSymbol);
    }
    case CandidateKind::kHanjaReverse: {
      const RangeEntry* range =
          FindRange(generated::kHanjaValueRanges,
                    generated::kHanjaValueRangeCount, request.query_text);
      if (range == nullptr) {
        return {};
      }
      return CollectCandidates(generated::kHanjaEntries,
                               generated::kHanjaValueEntryOrder, *range,
                               CandidateKind::kHanjaReverse);
    }
    case CandidateKind::kHanjaForward: {
      const RangeEntry* range =
          FindRange(generated::kHanjaKeyRanges,
                    generated::kHanjaKeyRangeCount, request.query_text);
      if (range == nullptr) {
        return {};
      }
      return CollectCandidates(generated::kHanjaEntries,
                               generated::kHanjaKeyEntryOrder, *range,
                               CandidateKind::kHanjaForward);
    }
  }

  return {};
}

}  // namespace milkyway::adapters::dictionary
