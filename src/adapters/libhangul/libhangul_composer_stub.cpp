#include "adapters/libhangul/hangul_composer.h"

#include <memory>
#include <string>
#include <string_view>

namespace milkyway::adapters::libhangul {
namespace {

class LibhangulComposerStub final : public HangulComposer {
 public:
  void Reset() override { last_sequence_.clear(); }

  std::string ProcessKeySequence(std::string_view key_sequence) override {
    last_sequence_ = std::string(key_sequence);
    return last_sequence_;
  }

 private:
  std::string last_sequence_;
};

}  // namespace

std::unique_ptr<HangulComposer> CreateLibhangulComposerStub() {
  return std::make_unique<LibhangulComposerStub>();
}

}  // namespace milkyway::adapters::libhangul
