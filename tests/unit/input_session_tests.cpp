#include <cassert>
#include <cstdint>
#include <string_view>
#include <vector>

#include "adapters/libhangul/hangul_composer.h"
#include "engine/key/physical_key.h"
#include "engine/layout/layout_registry.h"
#include "engine/session/input_session.h"
#include "engine/shortcut/shortcut_resolver.h"
#include "engine/state/modifier_state.h"
#include "tsf/edit/text_edit_sink.h"
#include "tsf/service/text_service.h"

namespace {

using milkyway::tsf::edit::TextEditOperation;
using milkyway::tsf::edit::TextEditOperationType;

struct RecordingEditSink final : milkyway::tsf::edit::TextEditSink {
  void Apply(const TextEditOperation& operation) override {
    operations.push_back(operation);
  }

  std::vector<TextEditOperation> operations;
};

milkyway::engine::key::PhysicalKey Key(std::uint16_t virtual_key) {
  milkyway::engine::key::PhysicalKey key;
  key.virtual_key = virtual_key;
  return key;
}

void AssertOperation(const TextEditOperation& operation,
                     TextEditOperationType type, std::string_view text) {
  assert(operation.type == type);
  assert(operation.text == text);
}

void TestInputSession() {
  milkyway::engine::session::InputSession session("us_qwerty", "ko_dubeolsik");

  assert(!session.IsComposing());
  assert(session.last_end_reason() ==
         milkyway::engine::session::CompositionEndReason::kNone);
  assert(!session.RequestHanjaConversion().has_value());

  session.StartComposition("\xED\x95\x9C");
  assert(session.IsComposing());
  assert(session.snapshot().preedit == "\xED\x95\x9C");

  const auto hanja_request = session.RequestHanjaConversion();
  assert(hanja_request.has_value());
  assert(hanja_request->composing_syllable == "\xED\x95\x9C");

  session.UpdateComposition("\xEA\xB8\x80");
  assert(session.snapshot().preedit == "\xEA\xB8\x80");

  session.EndComposition(
      milkyway::engine::session::CompositionEndReason::kDelimiter);
  assert(!session.IsComposing());
  assert(session.snapshot().preedit.empty());
  assert(session.last_end_reason() ==
         milkyway::engine::session::CompositionEndReason::kDelimiter);
  assert(!session.RequestHanjaConversion().has_value());
}

void TestLibhangulComposer() {
  auto composer = milkyway::adapters::libhangul::CreateLibhangulComposer();
  assert(composer != nullptr);

  auto step = composer->ProcessAscii('g');
  assert(step.consumed);
  assert(step.commit_text.empty());
  assert(step.preedit_text == "\xE3\x85\x8E");
  assert(composer->GetPreeditText() == step.preedit_text);

  step = composer->ProcessAscii('k');
  assert(step.consumed);
  assert(step.commit_text.empty());
  assert(step.preedit_text == "\xED\x95\x98");

  step = composer->ProcessAscii('s');
  assert(step.consumed);
  assert(step.commit_text.empty());
  assert(step.preedit_text == "\xED\x95\x9C");

  step = composer->ProcessAscii('r');
  assert(step.consumed);
  assert(step.commit_text == "\xED\x95\x9C");
  assert(step.preedit_text == "\xE3\x84\xB1");
  assert(composer->GetCommitText() == step.commit_text);

  step = composer->ProcessBackspace();
  assert(step.consumed);
  assert(step.commit_text.empty());
  assert(step.preedit_text.empty());
  assert(composer->GetPreeditText().empty());
}

void TestShortcutResolver(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  milkyway::engine::shortcut::ShortcutResolver resolver;
  milkyway::engine::shortcut::ShortcutQuery toggle_query;
  toggle_query.physical_layout = registry.DefaultPhysicalLayout().id;
  toggle_query.modifiers.ctrl = true;
  toggle_query.modifiers.shift = true;
  toggle_query.key.virtual_key = 0x20;

  assert(resolver.Resolve(toggle_query) ==
         milkyway::engine::shortcut::ShortcutAction::kToggleInputMode);

  milkyway::engine::shortcut::ShortcutQuery empty_layout_query = toggle_query;
  empty_layout_query.physical_layout.clear();
  assert(resolver.Resolve(empty_layout_query) ==
         milkyway::engine::shortcut::ShortcutAction::kNone);
}

void TestTextServiceLifecycle(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  milkyway::engine::session::InputSession session(
      registry.DefaultPhysicalLayout().id, registry.DefaultKoreanLayout().id);
  RecordingEditSink sink;
  milkyway::tsf::service::TextService service(
      &session, milkyway::adapters::libhangul::CreateLibhangulComposer(),
      &sink);

  auto result =
      service.OnKeyEvent(Key('G'), {}, milkyway::engine::key::KeyTransition::kPressed);
  assert(result.category ==
         milkyway::tsf::service::KeyEventCategory::kHangulAscii);
  assert(result.eaten);
  assert(!result.should_forward);
  assert(result.preedit_text == "\xE3\x85\x8E");
  assert(session.IsComposing());
  assert(session.snapshot().preedit == "\xE3\x85\x8E");
  assert(sink.operations.size() == 1);
  AssertOperation(sink.operations[0], TextEditOperationType::kStartComposition,
                  "\xE3\x85\x8E");

  result =
      service.OnKeyEvent(Key('K'), {}, milkyway::engine::key::KeyTransition::kPressed);
  assert(result.preedit_text == "\xED\x95\x98");
  assert(session.snapshot().preedit == "\xED\x95\x98");
  assert(sink.operations.size() == 2);
  AssertOperation(sink.operations[1], TextEditOperationType::kUpdateComposition,
                  "\xED\x95\x98");

  result =
      service.OnKeyEvent(Key('S'), {}, milkyway::engine::key::KeyTransition::kPressed);
  assert(result.preedit_text == "\xED\x95\x9C");
  assert(session.snapshot().preedit == "\xED\x95\x9C");
  assert(sink.operations.size() == 3);
  AssertOperation(sink.operations[2], TextEditOperationType::kUpdateComposition,
                  "\xED\x95\x9C");

  result =
      service.OnKeyEvent(Key('R'), {}, milkyway::engine::key::KeyTransition::kPressed);
  assert(result.commit_text == "\xED\x95\x9C");
  assert(result.preedit_text == "\xE3\x84\xB1");
  assert(session.IsComposing());
  assert(session.snapshot().preedit == "\xE3\x84\xB1");
  assert(sink.operations.size() == 5);
  AssertOperation(sink.operations[3], TextEditOperationType::kCommitText,
                  "\xED\x95\x9C");
  AssertOperation(sink.operations[4], TextEditOperationType::kUpdateComposition,
                  "\xE3\x84\xB1");

  result = service.OnKeyEvent(Key(0xBE), {},  // VK_OEM_PERIOD
                              milkyway::engine::key::KeyTransition::kPressed);
  assert(result.category ==
         milkyway::tsf::service::KeyEventCategory::kDelimiter);
  assert(!result.eaten);
  assert(result.should_forward);
  assert(!session.IsComposing());
  assert(session.last_end_reason() ==
         milkyway::engine::session::CompositionEndReason::kDelimiter);
  assert(sink.operations.size() == 6);
  AssertOperation(sink.operations[5], TextEditOperationType::kEndComposition, "");

  result =
      service.OnKeyEvent(Key('G'), {}, milkyway::engine::key::KeyTransition::kPressed);
  assert(session.IsComposing());
  result = service.OnKeyEvent(Key(0x20), {},  // VK_SPACE
                              milkyway::engine::key::KeyTransition::kPressed);
  assert(!result.eaten);
  assert(result.should_forward);
  assert(!session.IsComposing());
  assert(session.last_end_reason() ==
         milkyway::engine::session::CompositionEndReason::kDelimiter);

  result =
      service.OnKeyEvent(Key('G'), {}, milkyway::engine::key::KeyTransition::kPressed);
  result = service.OnKeyEvent(Key(0x0D), {},  // VK_RETURN
                              milkyway::engine::key::KeyTransition::kPressed);
  assert(!result.eaten);
  assert(result.should_forward);
  assert(!session.IsComposing());
  assert(session.last_end_reason() ==
         milkyway::engine::session::CompositionEndReason::kDelimiter);

  result =
      service.OnKeyEvent(Key('G'), {}, milkyway::engine::key::KeyTransition::kPressed);
  milkyway::engine::state::ModifierState question_modifiers;
  question_modifiers.shift = true;
  result = service.OnKeyEvent(Key(0xBF), question_modifiers,  // VK_OEM_2
                              milkyway::engine::key::KeyTransition::kPressed);
  assert(!result.eaten);
  assert(result.should_forward);
  assert(!session.IsComposing());
  assert(session.last_end_reason() ==
         milkyway::engine::session::CompositionEndReason::kDelimiter);
}

void TestTextServiceShortcutAndTermination(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  {
    milkyway::engine::session::InputSession session(
        registry.DefaultPhysicalLayout().id, registry.DefaultKoreanLayout().id);
    RecordingEditSink sink;
    milkyway::tsf::service::TextService service(
        &session, milkyway::adapters::libhangul::CreateLibhangulComposer(),
        &sink);

    service.OnKeyEvent(Key('G'), {}, milkyway::engine::key::KeyTransition::kPressed);
    milkyway::engine::state::ModifierState modifiers;
    modifiers.ctrl = true;
    modifiers.shift = true;
    const auto result =
        service.OnKeyEvent(Key(0x20), modifiers,
                           milkyway::engine::key::KeyTransition::kPressed);
    assert(result.category ==
           milkyway::tsf::service::KeyEventCategory::kModifiedShortcut);
    assert(result.eaten);
    assert(!result.should_forward);
    assert(result.shortcut_action ==
           milkyway::engine::shortcut::ShortcutAction::kToggleInputMode);
    assert(!session.IsComposing());
    assert(session.last_end_reason() ==
           milkyway::engine::session::CompositionEndReason::kShortcutBypass);
    assert(sink.operations.back().type == TextEditOperationType::kEndComposition);
  }

  {
    milkyway::engine::session::InputSession session(
        registry.DefaultPhysicalLayout().id, registry.DefaultKoreanLayout().id);
    RecordingEditSink sink;
    milkyway::tsf::service::TextService service(
        &session, milkyway::adapters::libhangul::CreateLibhangulComposer(),
        &sink);

    service.OnKeyEvent(Key('G'), {}, milkyway::engine::key::KeyTransition::kPressed);
    const std::size_t operation_count = sink.operations.size();
    assert(service.OnFocusLost());
    assert(!session.IsComposing());
    assert(session.last_end_reason() ==
           milkyway::engine::session::CompositionEndReason::kFocusLost);
    assert(sink.operations.size() == operation_count + 1);
    service.OnCompositionTerminated();
    assert(session.last_end_reason() ==
           milkyway::engine::session::CompositionEndReason::kFocusLost);
    assert(sink.operations.size() == operation_count + 1);
  }

  {
    milkyway::engine::session::InputSession session(
        registry.DefaultPhysicalLayout().id, registry.DefaultKoreanLayout().id);
    RecordingEditSink sink;
    milkyway::tsf::service::TextService service(
        &session, milkyway::adapters::libhangul::CreateLibhangulComposer(),
        &sink);

    service.OnKeyEvent(Key('G'), {}, milkyway::engine::key::KeyTransition::kPressed);
    const std::size_t operation_count = sink.operations.size();
    assert(service.OnSelectionMovedOutsideComposition());
    assert(!session.IsComposing());
    assert(session.last_end_reason() ==
           milkyway::engine::session::CompositionEndReason::kSelectionMoved);
    assert(sink.operations.size() == operation_count + 1);
    service.OnCompositionTerminated();
    assert(session.last_end_reason() ==
           milkyway::engine::session::CompositionEndReason::kSelectionMoved);
    assert(sink.operations.size() == operation_count + 1);
  }

  {
    milkyway::engine::session::InputSession session(
        registry.DefaultPhysicalLayout().id, registry.DefaultKoreanLayout().id);
    RecordingEditSink sink;
    milkyway::tsf::service::TextService service(
        &session, milkyway::adapters::libhangul::CreateLibhangulComposer(),
        &sink);

    service.OnKeyEvent(Key('G'), {}, milkyway::engine::key::KeyTransition::kPressed);
    const std::size_t operation_count = sink.operations.size();
    service.OnCompositionTerminated();
    assert(!session.IsComposing());
    assert(session.last_end_reason() ==
           milkyway::engine::session::CompositionEndReason::kExternalTermination);
    assert(sink.operations.size() == operation_count);
  }
}

}  // namespace

int main() {
  milkyway::engine::layout::LayoutRegistry registry;

  assert(registry.DefaultPhysicalLayout().id == "us_qwerty");
  assert(registry.DefaultKoreanLayout().id == "ko_dubeolsik");
  assert(registry.DefaultKoreanLayout().base_physical_layout == "us_qwerty");

  TestInputSession();
  TestLibhangulComposer();
  TestShortcutResolver(registry);
  TestTextServiceLifecycle(registry);
  TestTextServiceShortcutAndTermination(registry);

  return 0;
}
