#pragma once

#include <cstdint>

namespace milkyway::adapters::dictionary::generated {

struct HanjaEntry {
  std::uint32_t key;
  std::uint32_t value;
  std::uint32_t comment;
};

struct RangeEntry {
  std::uint32_t text;
  std::uint32_t first;
  std::uint32_t count;
};

const char* StringAt(std::uint32_t offset);

extern const HanjaEntry kHanjaEntries[];
extern const std::uint32_t kHanjaEntryCount;
extern const RangeEntry kHanjaKeyRanges[];
extern const std::uint32_t kHanjaKeyRangeCount;
extern const std::uint32_t kHanjaKeyEntryOrder[];
extern const RangeEntry kHanjaValueRanges[];
extern const std::uint32_t kHanjaValueRangeCount;
extern const std::uint32_t kHanjaValueEntryOrder[];

extern const HanjaEntry kSymbolEntries[];
extern const std::uint32_t kSymbolEntryCount;
extern const RangeEntry kSymbolKeyRanges[];
extern const std::uint32_t kSymbolKeyRangeCount;
extern const std::uint32_t kSymbolKeyEntryOrder[];

}  // namespace milkyway::adapters::dictionary::generated
