#include "tsf/service/text_service.h"

#include <utility>

#include "tsf/edit/composition_edit_session.h"

namespace milkyway::tsf::service {
namespace {

KeyEventCategory ToServiceCategory(engine::key::KeyCategory category) {
  switch (category) {
    case engine::key::KeyCategory::kHangulAscii:
      return KeyEventCategory::kHangulAscii;
    case engine::key::KeyCategory::kBackspace:
      return KeyEventCategory::kBackspace;
    case engine::key::KeyCategory::kDelimiter:
      return KeyEventCategory::kDelimiter;
    case engine::key::KeyCategory::kPureModifier:
      return KeyEventCategory::kPureModifier;
    case engine::key::KeyCategory::kModifiedShortcut:
      return KeyEventCategory::kModifiedShortcut;
    case engine::key::KeyCategory::kUnhandled:
      return KeyEventCategory::kUnhandled;
  }

  return KeyEventCategory::kUnhandled;
}

std::string PrintableTokenText(engine::key::LayoutKey key, bool shift) {
  using engine::key::LayoutKey;

  if (key >= LayoutKey::kDigit0 && key <= LayoutKey::kDigit9) {
    constexpr char kNormalDigits[] = "0123456789";
    constexpr char kShiftDigits[] = ")!@#$%^&*(";
    const int index = static_cast<int>(key) - static_cast<int>(LayoutKey::kDigit0);
    return std::string(1, shift ? kShiftDigits[index] : kNormalDigits[index]);
  }

  switch (key) {
    case LayoutKey::kSpace:
      return " ";
    case LayoutKey::kOem1:
      return shift ? ":" : ";";
    case LayoutKey::kOemPlus:
      return shift ? "+" : "=";
    case LayoutKey::kOemComma:
      return shift ? "<" : ",";
    case LayoutKey::kOemMinus:
      return shift ? "_" : "-";
    case LayoutKey::kOemPeriod:
      return shift ? ">" : ".";
    case LayoutKey::kOem2:
      return shift ? "?" : "/";
    case LayoutKey::kOem3:
      return shift ? "~" : "`";
    case LayoutKey::kOem4:
      return shift ? "{" : "[";
    case LayoutKey::kOem5:
      return shift ? "|" : "\\";
    case LayoutKey::kOem6:
      return shift ? "}" : "]";
    case LayoutKey::kOem7:
      return shift ? "\"" : "'";
    default:
      return {};
  }
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

  const engine::key::KeyAnalysis analysis =
      AnalyzeKeyEvent(key, modifiers, transition);
  switch (ToServiceCategory(analysis.category)) {
    case KeyEventCategory::kHangulAscii:
      return true;
    case KeyEventCategory::kBackspace:
      return session_->IsComposing();
    case KeyEventCategory::kDelimiter:
      return !PrintableTokenText(analysis.hangul_token_key,
                                 analysis.event.UsesHangulShift())
                  .empty();
    case KeyEventCategory::kPureModifier:
    case KeyEventCategory::kUnhandled:
      return false;
    case KeyEventCategory::kModifiedShortcut: {
      return analysis.shortcut_action != engine::shortcut::ShortcutAction::kNone;
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

  const engine::key::KeyAnalysis analysis =
      AnalyzeKeyEvent(key, modifiers, transition);
  const KeyEventCategory category = ToServiceCategory(analysis.category);

  switch (category) {
    case KeyEventCategory::kHangulAscii:
      return HandleHangulAscii(analysis);
    case KeyEventCategory::kBackspace:
      return HandleBackspace();
    case KeyEventCategory::kPureModifier: {
      KeyEventResult result;
      result.category = KeyEventCategory::kPureModifier;
      return result;
    }
    case KeyEventCategory::kDelimiter:
    case KeyEventCategory::kUnhandled:
      return HandleDelimiterOrUnhandled(analysis, category);
    case KeyEventCategory::kModifiedShortcut:
      return HandleModifiedShortcut(analysis);
  }

  return {};
}

bool TextService::ReplaceComposer(
    std::unique_ptr<adapters::libhangul::HangulComposer> composer) {
  if (composer == nullptr) {
    return false;
  }

  composer_ = std::move(composer);
  return true;
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

bool TextService::PrepareLayoutChange() {
  if (!session_->IsComposing()) {
    composer_->Reset();
    return false;
  }

  const std::string commit_text = composer_->Flush();
  if (!commit_text.empty()) {
    edit_sink_->Apply(edit::TextEditOperation{
        edit::TextEditOperationType::kCommitText, commit_text});
  }

  edit::EndCompositionEditSession edit_session(
      engine::session::CompositionEndReason::kLayoutChanged);
  const bool ended = edit_session.Apply(*session_, *edit_sink_);
  composer_->Reset();
  return ended;
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

engine::key::KeyAnalysis TextService::AnalyzeKeyEvent(
    const engine::key::PhysicalKey& key,
    const engine::state::ModifierState& modifiers,
    engine::key::KeyTransition transition) const {
  if (layout_registry_ == nullptr || session_ == nullptr) {
    static const engine::layout::LayoutRegistry fallback_registry;
    return engine::key::AnalyzeKeyEvent(fallback_registry, {}, {}, key,
                                        modifiers, transition);
  }

  return engine::key::AnalyzeKeyEvent(*layout_registry_,
                                      session_->physical_layout_id(),
                                      session_->korean_layout_id(), key,
                                      modifiers, transition);
}

KeyEventResult TextService::HandleHangulAscii(
    const engine::key::KeyAnalysis& analysis) {
  KeyEventResult result;
  result.category = KeyEventCategory::kHangulAscii;

  if (!analysis.hangul_input.is_mapped) {
    return HandleDelimiterOrUnhandled(analysis, KeyEventCategory::kUnhandled);
  }

  const adapters::libhangul::HangulInputResult composer_result =
      composer_->ProcessAscii(analysis.hangul_input.ascii);
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
    const engine::key::KeyAnalysis& analysis, KeyEventCategory category) {
  KeyEventResult result;
  result.category = category;

  if (category == KeyEventCategory::kDelimiter) {
    const std::string printable_text = PrintableTokenText(
        analysis.hangul_token_key, analysis.event.UsesHangulShift());
    if (!printable_text.empty()) {
      std::string text_to_commit;
      if (session_->IsComposing()) {
        if (composer_ != nullptr) {
          text_to_commit = composer_->Flush();
          composer_->Reset();
        }
        if (text_to_commit.empty()) {
          text_to_commit = session_->snapshot().preedit;
        }
      }
      text_to_commit += printable_text;

      if (!text_to_commit.empty()) {
        edit_sink_->Apply(edit::TextEditOperation{
            edit::TextEditOperationType::kCommitText, text_to_commit});
      }

      if (session_->IsComposing()) {
        edit::EndCompositionEditSession edit_session(
            engine::session::CompositionEndReason::kDelimiter);
        edit_session.Apply(*session_, *edit_sink_);
      }

      result.commit_text = text_to_commit;
      result.eaten = true;
      result.should_forward = false;
      return result;
    }
  }

  if (session_->IsComposing()) {
    EndActiveComposition(engine::session::CompositionEndReason::kDelimiter);
  }

  return result;
}

KeyEventResult TextService::HandleModifiedShortcut(
    const engine::key::KeyAnalysis& analysis) {
  KeyEventResult result;
  result.category = KeyEventCategory::kModifiedShortcut;

  result.shortcut_action = analysis.shortcut_action;
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
  const engine::key::KeyAnalysis analysis =
      AnalyzeKeyEvent(key, modifiers, engine::key::KeyTransition::kPressed);
  return analysis.hangul_input.is_mapped ? analysis.hangul_input.ascii : 0;
}
#endif

}  // namespace milkyway::tsf::service
