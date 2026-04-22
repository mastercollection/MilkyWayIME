#pragma once

#include <memory>
#include <string>

#include "adapters/libhangul/hangul_composer.h"
#include "engine/key/physical_key.h"
#include "engine/session/input_session.h"
#include "engine/shortcut/shortcut_resolver.h"
#include "engine/state/modifier_state.h"
#include "tsf/edit/text_edit_sink.h"

namespace milkyway::tsf::service {

enum class KeyEventCategory {
  kHangulAscii,
  kBackspace,
  kDelimiter,
  kModifiedShortcut,
  kUnhandled,
};

struct KeyEventResult {
  KeyEventCategory category = KeyEventCategory::kUnhandled;
  bool eaten = false;
  bool should_forward = true;
  engine::shortcut::ShortcutAction shortcut_action =
      engine::shortcut::ShortcutAction::kNone;
  std::string commit_text;
  std::string preedit_text;
};

class TextService {
 public:
  TextService(engine::session::InputSession* session,
              std::unique_ptr<adapters::libhangul::HangulComposer> composer,
              edit::TextEditSink* edit_sink);

  KeyEventResult OnKeyEvent(const engine::key::PhysicalKey& key,
                            const engine::state::ModifierState& modifiers,
                            engine::key::KeyTransition transition);
  bool OnFocusLost();
  bool OnSelectionMovedOutsideComposition();
  void OnCompositionTerminated();

 private:
  KeyEventCategory ClassifyKey(const engine::key::PhysicalKey& key,
                               const engine::state::ModifierState& modifiers) const;
  KeyEventResult HandleHangulAscii(const engine::key::PhysicalKey& key,
                                   const engine::state::ModifierState& modifiers);
  KeyEventResult HandleBackspace();
  KeyEventResult HandleDelimiterOrUnhandled(KeyEventCategory category);
  KeyEventResult HandleModifiedShortcut(
      const engine::key::PhysicalKey& key,
      const engine::state::ModifierState& modifiers);
  bool EndActiveComposition(engine::session::CompositionEndReason reason);
  void ResetInternalState(engine::session::CompositionEndReason reason);
  char ToAscii(const engine::key::PhysicalKey& key,
               const engine::state::ModifierState& modifiers) const;

  engine::session::InputSession* session_ = nullptr;
  std::unique_ptr<adapters::libhangul::HangulComposer> composer_;
  edit::TextEditSink* edit_sink_ = nullptr;
  engine::shortcut::ShortcutResolver shortcut_resolver_;
};

}  // namespace milkyway::tsf::service
