#include "tsf/service/text_service.h"

#include <cstdint>
#include <utility>

#include "tsf/edit/composition_edit_session.h"

namespace milkyway::tsf::service {
namespace {

constexpr std::uint16_t kVkBack = 0x08;
constexpr std::uint16_t kVkTab = 0x09;
constexpr std::uint16_t kVkReturn = 0x0D;
constexpr std::uint16_t kVkShift = 0x10;
constexpr std::uint16_t kVkControl = 0x11;
constexpr std::uint16_t kVkMenu = 0x12;
constexpr std::uint16_t kVkSpace = 0x20;
constexpr std::uint16_t kVkLwin = 0x5B;
constexpr std::uint16_t kVkRwin = 0x5C;
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

bool IsPureModifierVirtualKey(std::uint16_t virtual_key) {
  return virtual_key == kVkShift || virtual_key == kVkControl ||
         virtual_key == kVkMenu || virtual_key == kVkLwin ||
         virtual_key == kVkRwin;
}

}  // namespace

TextService::TextService(
    engine::session::InputSession* session,
    std::unique_ptr<adapters::libhangul::HangulComposer> composer,
    edit::TextEditSink* edit_sink,
    const engine::layout::LayoutRegistry* layout_registry)
    : session_(session),
      composer_(std::move(composer)),
      edit_sink_(edit_sink),
      layout_registry_(layout_registry) {}

bool TextService::WouldEatKey(
    const engine::key::PhysicalKey& key,
    const engine::state::ModifierState& modifiers,
    engine::key::KeyTransition transition) const {
  if (transition != engine::key::KeyTransition::kPressed) {
    return false;
  }

  const engine::key::NormalizedKeyEvent event =
      NormalizeKeyEvent(key, modifiers, transition);

  switch (ClassifyKey(event)) {
    case KeyEventCategory::kHangulAscii:
      return true;
    case KeyEventCategory::kBackspace:
      return session_->IsComposing();
    case KeyEventCategory::kDelimiter:
    case KeyEventCategory::kPureModifier:
    case KeyEventCategory::kUnhandled:
      return false;
    case KeyEventCategory::kModifiedShortcut: {
      engine::shortcut::ShortcutQuery query;
      query.physical_layout = session_->physical_layout_id();
      query.modifiers = modifiers;
      query.base_layout_key = event.base_layout_key;
      return shortcut_resolver_.Resolve(query) !=
             engine::shortcut::ShortcutAction::kNone;
    }
  }

  return false;
}

KeyEventResult TextService::OnKeyEvent(
    const engine::key::PhysicalKey& key,
    const engine::state::ModifierState& modifiers,
    engine::key::KeyTransition transition) {
  if (transition != engine::key::KeyTransition::kPressed) {
    return {};
  }

  const engine::key::NormalizedKeyEvent event =
      NormalizeKeyEvent(key, modifiers, transition);
  const KeyEventCategory category = ClassifyKey(event);

  switch (category) {
    case KeyEventCategory::kHangulAscii:
      return HandleHangulAscii(event);
    case KeyEventCategory::kBackspace:
      return HandleBackspace();
    case KeyEventCategory::kPureModifier: {
      KeyEventResult result;
      result.category = KeyEventCategory::kPureModifier;
      return result;
    }
    case KeyEventCategory::kDelimiter:
    case KeyEventCategory::kUnhandled:
      return HandleDelimiterOrUnhandled(category);
    case KeyEventCategory::kModifiedShortcut:
      return HandleModifiedShortcut(event);
  }

  return {};
}

bool TextService::PrepareImeModeToggle() {
  if (!session_->IsComposing()) {
    return false;
  }

  const std::string commit_text = composer_->Flush();
  if (!commit_text.empty()) {
    edit_sink_->Apply(edit::TextEditOperation{
        edit::TextEditOperationType::kCommitText, commit_text});
  }

  edit::EndCompositionEditSession edit_session(
      engine::session::CompositionEndReason::kImeModeToggle);
  edit_session.Apply(*session_, *edit_sink_);
  return true;
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

engine::key::NormalizedKeyEvent TextService::NormalizeKeyEvent(
    const engine::key::PhysicalKey& key,
    const engine::state::ModifierState& modifiers,
    engine::key::KeyTransition transition) const {
  if (layout_registry_ == nullptr) {
    engine::key::NormalizedKeyEvent event;
    event.key = key;
    event.modifiers = modifiers;
    event.transition = transition;
    return event;
  }

  return layout_registry_->NormalizeKeyEvent(session_->physical_layout_id(), key,
                                             modifiers, transition);
}

std::optional<char> TextService::ResolveHangulAscii(
    const engine::key::NormalizedKeyEvent& event) const {
  if (layout_registry_ == nullptr) {
    return std::nullopt;
  }

  const engine::layout::ResolvedHangulInput input =
      layout_registry_->ResolveHangulInput(
          session_->korean_layout_id(),
          engine::layout::HangulMappingKey{event.base_layout_key,
                                           event.UsesHangulShift()});
  if (!input.is_mapped) {
    return std::nullopt;
  }

  return input.ascii;
}

KeyEventCategory TextService::ClassifyKey(
    const engine::key::NormalizedKeyEvent& event) const {
  if (IsPureModifierVirtualKey(event.key.virtual_key)) {
    return KeyEventCategory::kPureModifier;
  }

  if (event.modifiers.ctrl || event.modifiers.alt || event.modifiers.win) {
    return KeyEventCategory::kModifiedShortcut;
  }

  if (event.key.virtual_key == kVkBack) {
    return KeyEventCategory::kBackspace;
  }

  if (ResolveHangulAscii(event).has_value()) {
    return KeyEventCategory::kHangulAscii;
  }

  if (event.key.virtual_key == kVkSpace || event.key.virtual_key == kVkReturn ||
      event.key.virtual_key == kVkTab ||
      IsAsciiDigitVirtualKey(event.key.virtual_key) ||
      IsNumpadVirtualKey(event.key.virtual_key) ||
      IsOemDelimiterVirtualKey(event.key.virtual_key)) {
    return KeyEventCategory::kDelimiter;
  }

  return KeyEventCategory::kUnhandled;
}

KeyEventResult TextService::HandleHangulAscii(
    const engine::key::NormalizedKeyEvent& event) {
  KeyEventResult result;
  result.category = KeyEventCategory::kHangulAscii;

  const std::optional<char> ascii = ResolveHangulAscii(event);
  if (!ascii.has_value()) {
    return HandleDelimiterOrUnhandled(KeyEventCategory::kUnhandled);
  }

  const adapters::libhangul::HangulInputResult composer_result =
      composer_->ProcessAscii(*ascii);
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
    // Clear the visible composition text before closing it so hosts do not
    // leave the last jamo in the document after an in-place backspace.
    session_->UpdateComposition({});
    edit_sink_->Apply(
        edit::TextEditOperation{edit::TextEditOperationType::kUpdateComposition,
                                {}});
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
    const engine::key::NormalizedKeyEvent& event) {
  KeyEventResult result;
  result.category = KeyEventCategory::kModifiedShortcut;

  engine::shortcut::ShortcutQuery query;
  query.physical_layout = session_->physical_layout_id();
  query.modifiers = event.modifiers;
  query.base_layout_key = event.base_layout_key;

  result.shortcut_action = shortcut_resolver_.Resolve(query);
  if (result.shortcut_action ==
      engine::shortcut::ShortcutAction::kToggleInputMode) {
    PrepareImeModeToggle();
  } else if (session_->IsComposing()) {
    EndActiveComposition(
        engine::session::CompositionEndReason::kShortcutBypass);
  }

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

#if defined(_DEBUG)
char TextService::DebugToAscii(const engine::key::PhysicalKey& key,
                               const engine::state::ModifierState& modifiers) const {
  const std::optional<char> ascii = ResolveHangulAscii(
      NormalizeKeyEvent(key, modifiers, engine::key::KeyTransition::kPressed));
  return ascii.value_or(0);
}
#endif

}  // namespace milkyway::tsf::service
