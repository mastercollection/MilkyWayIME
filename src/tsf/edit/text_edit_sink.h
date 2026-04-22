#pragma once

#include <string>

namespace milkyway::tsf::edit {

enum class TextEditOperationType {
  kCommitText,
  kStartComposition,
  kUpdateComposition,
  kEndComposition,
};

struct TextEditOperation {
  TextEditOperationType type = TextEditOperationType::kCommitText;
  std::string text;
};

class TextEditSink {
 public:
  virtual ~TextEditSink() = default;

  virtual void Apply(const TextEditOperation& operation) = 0;
};

}  // namespace milkyway::tsf::edit
