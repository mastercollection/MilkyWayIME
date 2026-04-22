#include "adapters/libhangul/hangul_composer.h"

#include <memory>
#include <string>
#include <string_view>

#include "hangul.h"

namespace milkyway::adapters::libhangul {
namespace {

struct HangulInputContextDeleter {
  void operator()(HangulInputContext* context) const {
    if (context != nullptr) {
      hangul_ic_delete(context);
    }
  }
};

void AppendUtf8(std::string& output, ucschar code_point) {
  if (code_point <= 0x7F) {
    output.push_back(static_cast<char>(code_point));
    return;
  }

  if (code_point <= 0x7FF) {
    output.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
    output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    return;
  }

  if (code_point <= 0xFFFF) {
    output.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
    output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    return;
  }

  if (code_point <= 0x10FFFF) {
    output.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
    output.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    return;
  }

  output.push_back('?');
}

std::string ToUtf8(const ucschar* text) {
  std::string output;
  if (text == nullptr) {
    return output;
  }

  for (const ucschar* cursor = text; *cursor != 0; ++cursor) {
    AppendUtf8(output, *cursor);
  }

  return output;
}

class LibhangulComposer final : public HangulComposer {
 public:
  explicit LibhangulComposer(HangulInputContext* context) : context_(context) {}

  void Reset() override { hangul_ic_reset(context_.get()); }

  std::string ProcessKeySequence(std::string_view key_sequence) override {
    std::string output;

    for (const unsigned char key : key_sequence) {
      const bool consumed = hangul_ic_process(context_.get(), key);
      output += ToUtf8(hangul_ic_get_commit_string(context_.get()));

      if (!consumed) {
        output += ToUtf8(hangul_ic_flush(context_.get()));
        output.push_back(static_cast<char>(key));
      }
    }

    output += ToUtf8(hangul_ic_flush(context_.get()));
    return output;
  }

 private:
  std::unique_ptr<HangulInputContext, HangulInputContextDeleter> context_;
};

}  // namespace

std::unique_ptr<HangulComposer> CreateLibhangulComposer(
    std::string_view keyboard_id) {
  const std::string selected_keyboard =
      keyboard_id.empty() ? std::string("2") : std::string(keyboard_id);
  HangulInputContext* context = hangul_ic_new(selected_keyboard.c_str());
  if (context == nullptr) {
    return nullptr;
  }

  return std::make_unique<LibhangulComposer>(context);
}

}  // namespace milkyway::adapters::libhangul
