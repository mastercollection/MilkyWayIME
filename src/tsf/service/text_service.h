#pragma once

#include <memory>
#include <optional>
#include <string>

#include "adapters/libhangul/hangul_composer.h"
#include "engine/key/physical_key.h"
#include "engine/key/normalized_key_event.h"
#include "engine/layout/layout_registry.h"
#include "engine/session/input_session.h"
#include "engine/shortcut/shortcut_resolver.h"
#include "engine/state/modifier_state.h"
#include "tsf/edit/text_edit_sink.h"

namespace milkyway::tsf::service {

enum class KeyEventCategory {
  kHangulAscii,
  kBackspace,
  kDelimiter,
  kPureModifier,
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
              edit::TextEditSink* edit_sink,
              const engine::layout::LayoutRegistry* layout_registry);

  bool WouldEatKey(const engine::key::PhysicalKey& key,
                   const engine::state::ModifierState& modifiers,
                   engine::key::KeyTransition transition) const;
  KeyEventResult OnKeyEvent(const engine::key::PhysicalKey& key,
                            const engine::state::ModifierState& modifiers,
                            engine::key::KeyTransition transition);
  bool PrepareImeModeToggle();
  bool OnFocusLost();
  bool OnSelectionMovedOutsideComposition();
  void OnCompositionTerminated();
#if defined(_DEBUG)
  char DebugToAscii(const engine::key::PhysicalKey& key,
                    const engine::state::ModifierState& modifiers) const;
#endif

 private:
  engine::key::NormalizedKeyEvent NormalizeKeyEvent(
      const engine::key::PhysicalKey& key,
      const engine::state::ModifierState& modifiers,
      engine::key::KeyTransition transition) const;
  std::optional<char> ResolveHangulAscii(
      const engine::key::NormalizedKeyEvent& event) const;
  KeyEventCategory ClassifyKey(const engine::key::NormalizedKeyEvent& event) const;
  KeyEventResult HandleHangulAscii(const engine::key::NormalizedKeyEvent& event);
  KeyEventResult HandleBackspace();
  KeyEventResult HandleDelimiterOrUnhandled(KeyEventCategory category);
  KeyEventResult HandleModifiedShortcut(
      const engine::key::NormalizedKeyEvent& event);
  bool EndActiveComposition(engine::session::CompositionEndReason reason);
  void ResetInternalState(engine::session::CompositionEndReason reason);

  engine::session::InputSession* session_ = nullptr;
  std::unique_ptr<adapters::libhangul::HangulComposer> composer_;
  edit::TextEditSink* edit_sink_ = nullptr;
  const engine::layout::LayoutRegistry* layout_registry_ = nullptr;
  engine::shortcut::ShortcutResolver shortcut_resolver_;
};

}  // namespace milkyway::tsf::service
