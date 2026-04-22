#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace milkyway::adapters::libhangul {

class HangulComposer {
 public:
  virtual ~HangulComposer() = default;

  virtual void Reset() = 0;
  virtual std::string ProcessKeySequence(std::string_view key_sequence) = 0;
};

std::unique_ptr<HangulComposer> CreateLibhangulComposer(
    std::string_view keyboard_id = "2");

}  // namespace milkyway::adapters::libhangul
