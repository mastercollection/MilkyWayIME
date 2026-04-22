#include "tsf/service/text_service.h"

#include <cstdint>
#include <utility>

#include "tsf/edit/composition_edit_session.h"

namespace milkyway::tsf::service {
namespace {

constexpr std::uint16_t kVkBack = 0x08;
constexpr std::uint16_t kVkTab = 0x09;
constexpr std::uint16_t kVkReturn = 0x0D;
constexpr std::uint16_t kVkSpace = 0x20;
constexpr std::uint16_t kVkNumpad0 = 0x60;
constexpr std::uint16_t kVkDivide = 0x6F;
constexpr std::uint16_t kVkOem1 = 0xBA;
constexpr std::uint16_t kVkOem3 = 0xC0;
constexpr std::uint16_t kVkOem4 = 0xDB;
constexpr std::uint16_t kVkOem7 = 0xDE;

bool IsAlphabeticVirtualKey(std::uint16_t virtual_key) {
  return virtual_key >= 'A' && virtual_key <= 'Z';
}

bool IsAsciiDigitVirtualKey(std::uint16_t virtual_key) {
  return virtual_key >= '0' && virtual_key <= '9';
}

bool IsNumpadVirtualKey(std::uint16_t virtual_key) {
  return virtual_key >= kVkNumpad0 && virtual_key <= kVkDivide;
}

bool IsOemDelimiterVirtualKey(std::uint16_t virtual_key) {
  return (virtual_key >= kVkOem1 && virtual_key <= kVkOem3) ||
         (virtual_key >= kVkOem4 && virtual_key <= kVkOem7);
}

}  // namespace

TextService::TextService(
    engine::session::InputSession* session,
    std::unique_ptr<adapters::libhangul::HangulComposer> composer,
    edit::TextEditSink* edit_sink)
    : session_(session),
      composer_(std::move(composer)),
      edit_sink_(edit_sink) {}

KeyEventResult TextService::OnKeyEvent(
    const engine::key::PhysicalKey& key,
    const engine::state::ModifierState& modifiers,
    engine::key::KeyTransition transition) {
  if (transition != engine::key::KeyTransition::kPressed) {
    return {};
  }

  const KeyEventCategory category = ClassifyKey(key, modifiers);

  switch (category) {
    case KeyEventCategory::kHangulAscii:
      return HandleHangulAscii(key, modifiers);
    case KeyEventCategory::kBackspace:
      return HandleBackspace();
    case KeyEventCategory::kDelimiter:
    case KeyEventCategory::kUnhandled:
      return HandleDelimiterOrUnhandled(category);
    case KeyEventCategory::kModifiedShortcut:
      return HandleModifiedShortcut(key, modifiers);
  }

  return {};
}

bool TextService::OnFocusLost() {
  return EndActiveComposition(engine::session::CompositionEndReason::kFocusLost);
}

bool TextService::OnSelectionMovedOutsideComposition() {
  return EndActiveComposition(
      engine::session::CompositionEndReason::kSelectionMoved);
}

void TextService::OnCompositionTerminated() {
  if (session_->IsComposing()) {
    ResetInternalState(
        engine::session::CompositionEndReason::kExternalTermination);
    return;
  }

  composer_->Reset();
}

KeyEventCategory TextService::ClassifyKey(
    const engine::key::PhysicalKey& key,
    const engine::state::ModifierState& modifiers) const {
  if (modifiers.ctrl || modifiers.alt || modifiers.win) {
    return KeyEventCategory::kModifiedShortcut;
  }

  if (key.virtual_key == kVkBack) {
    return KeyEventCategory::kBackspace;
  }

  if (IsAlphabeticVirtualKey(key.virtual_key)) {
    return KeyEventCategory::kHangulAscii;
  }

  if (key.virtual_key == kVkSpace || key.virtual_key == kVkReturn ||
      key.virtual_key == kVkTab || IsAsciiDigitVirtualKey(key.virtual_key) ||
      IsNumpadVirtualKey(key.virtual_key) ||
      IsOemDelimiterVirtualKey(key.virtual_key)) {
    return KeyEventCategory::kDelimiter;
  }

  return KeyEventCategory::kUnhandled;
}

KeyEventResult TextService::HandleHangulAscii(
    const engine::key::PhysicalKey& key,
    const engine::state::ModifierState& modifiers) {
  KeyEventResult result;
  result.category = KeyEventCategory::kHangulAscii;

  const adapters::libhangul::HangulInputResult composer_result =
      composer_->ProcessAscii(ToAscii(key, modifiers));
  result.commit_text = composer_result.commit_text;
  result.preedit_text = composer_result.preedit_text;
  result.eaten = composer_result.consumed;
  result.should_forward = !result.eaten;

  if (!composer_result.commit_text.empty()) {
    edit_sink_->Apply(edit::TextEditOperation{
        edit::TextEditOperationType::kCommitText, composer_result.commit_text});
  }

  if (!composer_result.preedit_text.empty()) {
    edit::StartOrUpdateCompositionEditSession edit_session(
        composer_result.preedit_text);
    edit_session.Apply(*session_, *edit_sink_);
  } else if (session_->IsComposing()) {
    EndActiveComposition(engine::session::CompositionEndReason::kCompleted);
  }

  return result;
}

KeyEventResult TextService::HandleBackspace() {
  KeyEventResult result;
  result.category = KeyEventCategory::kBackspace;

  if (!session_->IsComposing()) {
    return result;
  }

  const adapters::libhangul::HangulInputResult composer_result =
      composer_->ProcessBackspace();
  result.commit_text = composer_result.commit_text;
  result.preedit_text = composer_result.preedit_text;
  result.eaten = composer_result.consumed || !composer_result.commit_text.empty() ||
                 !composer_result.preedit_text.empty();
  result.should_forward = !result.eaten;

  if (!composer_result.commit_text.empty()) {
    edit_sink_->Apply(edit::TextEditOperation{
        edit::TextEditOperationType::kCommitText, composer_result.commit_text});
  }

  if (!composer_result.preedit_text.empty()) {
    edit::StartOrUpdateCompositionEditSession edit_session(
        composer_result.preedit_text);
    edit_session.Apply(*session_, *edit_sink_);
  } else {
    EndActiveComposition(engine::session::CompositionEndReason::kBackspace);
  }

  return result;
}

KeyEventResult TextService::HandleDelimiterOrUnhandled(
    KeyEventCategory category) {
  KeyEventResult result;
  result.category = category;

  if (session_->IsComposing()) {
    EndActiveComposition(engine::session::CompositionEndReason::kDelimiter);
  }

  return result;
}

KeyEventResult TextService::HandleModifiedShortcut(
    const engine::key::PhysicalKey& key,
    const engine::state::ModifierState& modifiers) {
  KeyEventResult result;
  result.category = KeyEventCategory::kModifiedShortcut;

  if (session_->IsComposing()) {
    EndActiveComposition(
        engine::session::CompositionEndReason::kShortcutBypass);
  }

  engine::shortcut::ShortcutQuery query;
  query.physical_layout = session_->physical_layout_id();
  query.modifiers = modifiers;
  query.key = key;

  result.shortcut_action = shortcut_resolver_.Resolve(query);
  result.eaten =
      result.shortcut_action != engine::shortcut::ShortcutAction::kNone;
  result.should_forward = !result.eaten;
  return result;
}

bool TextService::EndActiveComposition(
    engine::session::CompositionEndReason reason) {
  edit::EndCompositionEditSession edit_session(reason);
  const bool ended = edit_session.Apply(*session_, *edit_sink_);
  if (ended) {
    composer_->Reset();
  }

  return ended;
}

void TextService::ResetInternalState(
    engine::session::CompositionEndReason reason) {
  composer_->Reset();
  session_->EndComposition(reason);
}

char TextService::ToAscii(const engine::key::PhysicalKey& key,
                          const engine::state::ModifierState& modifiers) const {
  char ascii = static_cast<char>(key.virtual_key);
  if (!modifiers.shift && ascii >= 'A' && ascii <= 'Z') {
    ascii = static_cast<char>(ascii - 'A' + 'a');
  }

  return ascii;
}

}  // namespace milkyway::tsf::service
