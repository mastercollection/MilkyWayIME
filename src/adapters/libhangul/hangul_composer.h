#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace milkyway::adapters::libhangul {

struct HangulInputResult {
  bool consumed = false;
  std::string commit_text;
  std::string preedit_text;
};

class HangulComposer {
 public:
  virtual ~HangulComposer() = default;

  virtual void Reset() = 0;
  virtual HangulInputResult ProcessAscii(char ascii) = 0;
  virtual HangulInputResult ProcessBackspace() = 0;
  virtual std::string GetCommitText() const = 0;
  virtual std::string GetPreeditText() const = 0;
};

std::unique_ptr<HangulComposer> CreateLibhangulComposer(
    std::string_view keyboard_id = "2");

}  // namespace milkyway::adapters::libhangul
