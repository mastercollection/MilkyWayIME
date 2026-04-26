#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <string_view>
#include <vector>

#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#endif

#include "adapters/libhangul/hangul_composer.h"
#include "engine/key/key_analysis.h"
#include "engine/key/physical_key.h"
#include "engine/layout/layout_registry.h"
#include "engine/session/input_session.h"
#include "engine/shortcut/shortcut_resolver.h"
#include "engine/state/modifier_state.h"
#include "tsf/edit/text_edit_sink.h"
#include "tsf/edit/text_edit_plan.h"
#include "tsf/service/text_service.h"

namespace {

using milkyway::tsf::edit::TextEditOperation;
using milkyway::tsf::edit::TextEditOperationType;

constexpr std::uint16_t kVkShift = 0x10;
constexpr std::uint16_t kVkControl = 0x11;
constexpr std::uint16_t kVkMenu = 0x12;
constexpr std::uint16_t kVkSpace = 0x20;
constexpr std::uint16_t kVkLwin = 0x5B;

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

void TestLibhangulComposerAutoReorder() {
  auto composer = milkyway::adapters::libhangul::CreateLibhangulComposer();
  assert(composer != nullptr);

  auto step = composer->ProcessAscii('k');
  assert(step.consumed);
  assert(step.commit_text.empty());
  assert(step.preedit_text == "\xE3\x85\x8F");

  step = composer->ProcessAscii('d');
  assert(step.consumed);
  assert(step.commit_text.empty());
  assert(step.preedit_text == "\xEC\x95\x84");
  assert(composer->GetPreeditText() == step.preedit_text);
}

void TestLibhangulComposerShiftFinalSsangSios() {
  auto composer = milkyway::adapters::libhangul::CreateLibhangulComposer();
  assert(composer != nullptr);

  auto step = composer->ProcessAscii('r');
  assert(step.consumed);
  assert(step.commit_text.empty());
  assert(step.preedit_text == "\xE3\x84\xB1");

  step = composer->ProcessAscii('k');
  assert(step.consumed);
  assert(step.commit_text.empty());
  assert(step.preedit_text == "\xEA\xB0\x80");

  step = composer->ProcessAscii('T');
  assert(step.consumed);
  assert(step.commit_text.empty());
  assert(step.preedit_text == "\xEA\xB0\x94");
  assert(composer->GetPreeditText() == "\xEA\xB0\x94");
}

void TestShortcutResolver(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  milkyway::engine::shortcut::ShortcutResolver resolver;
  milkyway::engine::shortcut::ShortcutQuery toggle_query;
  toggle_query.physical_layout = registry.DefaultPhysicalLayout().id;
  toggle_query.modifiers.ctrl = true;
  toggle_query.modifiers.shift = true;
  toggle_query.input_label_key =
      milkyway::engine::key::LayoutKey::kSpace;

  assert(resolver.Resolve(toggle_query) ==
         milkyway::engine::shortcut::ShortcutAction::kToggleInputMode);

  milkyway::engine::shortcut::ShortcutQuery empty_layout_query = toggle_query;
  empty_layout_query.physical_layout.clear();
  assert(resolver.Resolve(empty_layout_query) ==
         milkyway::engine::shortcut::ShortcutAction::kNone);
}

void TestLayoutRegistryEffectiveBaseLayout(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  milkyway::engine::key::PhysicalKey key;
  key.virtual_key = 'R';
  const auto event = registry.NormalizeKeyEvent(
      registry.DefaultPhysicalLayout().id, key, {},
      milkyway::engine::key::KeyTransition::kPressed);
  assert(event.input_label_key == milkyway::engine::key::LayoutKey::kR);

  const auto shift_input = registry.ResolveHangulInput(
      registry.DefaultKoreanLayout().id,
      {milkyway::engine::key::LayoutKey::kO, true});
  assert(shift_input.is_mapped);
  assert(shift_input.ascii == 'O');

}

void TestBuiltInPhysicalLayouts(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  using milkyway::engine::key::LayoutKey;
  using milkyway::engine::key::KeyCategory;
  using milkyway::engine::key::KeyTransition;

  assert(registry.FindPhysicalLayout("colemak") != nullptr);
  assert(registry.FindPhysicalLayout("colemak_dh") != nullptr);

  auto input_label_key = [&](const char* layout_id, std::uint16_t virtual_key) {
    return registry
        .NormalizeKeyEvent(layout_id, Key(virtual_key), {},
                           KeyTransition::kPressed)
        .input_label_key;
  };

  assert(input_label_key("colemak", 'R') == LayoutKey::kR);
  assert(input_label_key("colemak_dh", 'R') == LayoutKey::kR);

  assert(registry.ResolveBaseLayoutLabelKey("colemak", LayoutKey::kE) ==
         LayoutKey::kF);
  assert(registry.ResolveBaseLayoutLabelKey("colemak", LayoutKey::kR) ==
         LayoutKey::kP);
  assert(registry.ResolveBaseLayoutLabelKey("colemak", LayoutKey::kS) ==
         LayoutKey::kR);
  assert(registry.ResolveBaseLayoutLabelKey("colemak", LayoutKey::kP) ==
         LayoutKey::kOem1);
  assert(registry.ResolveBaseLayoutLabelKey("colemak", LayoutKey::kOem1) ==
         LayoutKey::kO);

  assert(registry.ResolveBaseLayoutLabelKey("colemak_dh", LayoutKey::kT) ==
         LayoutKey::kB);
  assert(registry.ResolveBaseLayoutLabelKey("colemak_dh", LayoutKey::kS) ==
         LayoutKey::kR);
  assert(registry.ResolveBaseLayoutLabelKey("colemak_dh", LayoutKey::kH) ==
         LayoutKey::kM);
  assert(registry.ResolveBaseLayoutLabelKey("colemak_dh", LayoutKey::kM) ==
         LayoutKey::kH);
  assert(registry.ResolveBaseLayoutLabelKey("colemak_dh", LayoutKey::kV) ==
         LayoutKey::kD);
  assert(registry.ResolveBaseLayoutLabelKey("colemak_dh", LayoutKey::kB) ==
         LayoutKey::kV);

  const auto colemak_dh_r = milkyway::engine::key::AnalyzeKeyEvent(
      registry, "colemak_dh", registry.DefaultKoreanLayout().id, Key('R'), {},
      KeyTransition::kPressed);
  assert(colemak_dh_r.input_label_key == LayoutKey::kR);
  assert(colemak_dh_r.hangul_token_key == LayoutKey::kS);
  assert(colemak_dh_r.hangul_input.is_mapped);
  assert(colemak_dh_r.hangul_input.ascii == 's');
  assert(colemak_dh_r.category == KeyCategory::kHangulAscii);

  const auto colemak_dh_p = milkyway::engine::key::AnalyzeKeyEvent(
      registry, "colemak_dh", registry.DefaultKoreanLayout().id, Key('P'), {},
      KeyTransition::kPressed);
  assert(colemak_dh_p.input_label_key == LayoutKey::kP);
  assert(colemak_dh_p.hangul_token_key == LayoutKey::kR);
  assert(colemak_dh_p.hangul_input.is_mapped);
  assert(colemak_dh_p.hangul_input.ascii == 'r');
  assert(colemak_dh_p.category == KeyCategory::kHangulAscii);

  const auto colemak_dh_o = milkyway::engine::key::AnalyzeKeyEvent(
      registry, "colemak_dh", registry.DefaultKoreanLayout().id, Key('O'), {},
      KeyTransition::kPressed);
  assert(colemak_dh_o.input_label_key == LayoutKey::kO);
  assert(colemak_dh_o.hangul_token_key == LayoutKey::kOem1);
  assert(!colemak_dh_o.hangul_input.is_mapped);
  assert(colemak_dh_o.category == KeyCategory::kDelimiter);

  milkyway::engine::state::ModifierState ctrl_alt;
  ctrl_alt.ctrl = true;
  ctrl_alt.alt = true;
  const auto colemak_dh_ctrl_o = milkyway::engine::key::AnalyzeKeyEvent(
      registry, "colemak_dh", registry.DefaultKoreanLayout().id, Key('O'),
      ctrl_alt, KeyTransition::kPressed);
  assert(colemak_dh_ctrl_o.shortcut_action ==
         milkyway::engine::shortcut::ShortcutAction::kNone);

  auto assert_delimiter = [&](std::uint16_t virtual_key,
                              LayoutKey expected_key) {
    const auto analysis = milkyway::engine::key::AnalyzeKeyEvent(
        registry, "us_qwerty", registry.DefaultKoreanLayout().id,
        Key(virtual_key), {}, KeyTransition::kPressed);
    assert(analysis.input_label_key == expected_key);
    assert(analysis.hangul_token_key == expected_key);
    assert(analysis.category == KeyCategory::kDelimiter);
  };

  assert_delimiter(0xBA, LayoutKey::kOem1);
  assert_delimiter(0xBB, LayoutKey::kOemPlus);
  assert_delimiter(0xBC, LayoutKey::kOemComma);
  assert_delimiter(0xBD, LayoutKey::kOemMinus);
  assert_delimiter(0xBE, LayoutKey::kOemPeriod);
}

void TestTextServiceLifecycle(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  milkyway::engine::session::InputSession session(
      registry.DefaultPhysicalLayout().id, registry.DefaultKoreanLayout().id);
  RecordingEditSink sink;
  milkyway::tsf::service::TextService service(
      &session, milkyway::adapters::libhangul::CreateLibhangulComposer(),
      &sink, &registry);

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
  assert(result.eaten);
  assert(!result.should_forward);
  assert(result.commit_text == "\xE3\x84\xB1.");
  assert(!session.IsComposing());
  assert(session.last_end_reason() ==
         milkyway::engine::session::CompositionEndReason::kDelimiter);
  assert(sink.operations.size() == 7);
  AssertOperation(sink.operations[5], TextEditOperationType::kCommitText,
                  "\xE3\x84\xB1.");
  AssertOperation(sink.operations[6], TextEditOperationType::kEndComposition, "");

  result =
      service.OnKeyEvent(Key('G'), {}, milkyway::engine::key::KeyTransition::kPressed);
  assert(session.IsComposing());
  result = service.OnKeyEvent(Key(0x20), {},  // VK_SPACE
                              milkyway::engine::key::KeyTransition::kPressed);
  assert(result.eaten);
  assert(!result.should_forward);
  assert(result.commit_text == "\xE3\x85\x8E ");
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
  assert(result.eaten);
  assert(!result.should_forward);
  assert(result.commit_text == "\xE3\x85\x8E?");
  assert(!session.IsComposing());
  assert(session.last_end_reason() ==
         milkyway::engine::session::CompositionEndReason::kDelimiter);
}

void TestTextServiceBaseLayoutPrintableDelimiter(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  {
    milkyway::engine::session::InputSession session(
        "colemak_dh", registry.DefaultKoreanLayout().id);
    RecordingEditSink sink;
    milkyway::tsf::service::TextService service(
        &session, milkyway::adapters::libhangul::CreateLibhangulComposer(),
        &sink, &registry);

    assert(service.WouldEatKey(
        Key('O'), {}, milkyway::engine::key::KeyTransition::kPressed));
    const auto result =
        service.OnKeyEvent(Key('O'), {},
                           milkyway::engine::key::KeyTransition::kPressed);

    assert(result.category ==
           milkyway::tsf::service::KeyEventCategory::kDelimiter);
    assert(result.eaten);
    assert(!result.should_forward);
    assert(result.commit_text == ";");
    assert(!session.IsComposing());
    assert(sink.operations.size() == 1);
    AssertOperation(sink.operations[0], TextEditOperationType::kCommitText, ";");
  }

  {
    milkyway::engine::session::InputSession session(
        "colemak_dh", registry.DefaultKoreanLayout().id);
    RecordingEditSink sink;
    milkyway::tsf::service::TextService service(
        &session, milkyway::adapters::libhangul::CreateLibhangulComposer(),
        &sink, &registry);

    milkyway::engine::state::ModifierState shift_modifiers;
    shift_modifiers.shift = true;
    const auto result =
        service.OnKeyEvent(Key('O'), shift_modifiers,
                           milkyway::engine::key::KeyTransition::kPressed);

    assert(result.category ==
           milkyway::tsf::service::KeyEventCategory::kDelimiter);
    assert(result.eaten);
    assert(!result.should_forward);
    assert(result.commit_text == ":");
    assert(!session.IsComposing());
    assert(sink.operations.size() == 1);
    AssertOperation(sink.operations[0], TextEditOperationType::kCommitText, ":");
  }

  {
    milkyway::engine::session::InputSession session(
        "colemak_dh", registry.DefaultKoreanLayout().id);
    RecordingEditSink sink;
    milkyway::tsf::service::TextService service(
        &session, milkyway::adapters::libhangul::CreateLibhangulComposer(),
        &sink, &registry);

    auto result =
        service.OnKeyEvent(Key('P'), {},
                           milkyway::engine::key::KeyTransition::kPressed);
    assert(result.category ==
           milkyway::tsf::service::KeyEventCategory::kHangulAscii);
    assert(result.preedit_text == "\xE3\x84\xB1");
    assert(session.IsComposing());

    result =
        service.OnKeyEvent(Key('O'), {},
                           milkyway::engine::key::KeyTransition::kPressed);

    assert(result.category ==
           milkyway::tsf::service::KeyEventCategory::kDelimiter);
    assert(result.eaten);
    assert(!result.should_forward);
    assert(result.commit_text == "\xE3\x84\xB1;");
    assert(!session.IsComposing());
    assert(session.last_end_reason() ==
           milkyway::engine::session::CompositionEndReason::kDelimiter);
    assert(sink.operations.size() == 3);
    AssertOperation(sink.operations[0], TextEditOperationType::kStartComposition,
                    "\xE3\x84\xB1");
    AssertOperation(sink.operations[1], TextEditOperationType::kCommitText,
                    "\xE3\x84\xB1;");
    AssertOperation(sink.operations[2], TextEditOperationType::kEndComposition, "");
  }
}

void TestTextServiceBackspaceClearsVisibleComposition(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  milkyway::engine::session::InputSession session(
      registry.DefaultPhysicalLayout().id, registry.DefaultKoreanLayout().id);
  RecordingEditSink sink;
  milkyway::tsf::service::TextService service(
      &session, milkyway::adapters::libhangul::CreateLibhangulComposer(),
      &sink, &registry);

  auto result =
      service.OnKeyEvent(Key('D'), {}, milkyway::engine::key::KeyTransition::kPressed);
  assert(result.preedit_text == "\xE3\x85\x87");
  result =
      service.OnKeyEvent(Key('K'), {}, milkyway::engine::key::KeyTransition::kPressed);
  assert(result.preedit_text == "\xEC\x95\x84");
  assert(session.IsComposing());
  assert(session.snapshot().preedit == "\xEC\x95\x84");

  result = service.OnKeyEvent(Key(0x08), {},  // VK_BACK
                              milkyway::engine::key::KeyTransition::kPressed);
  assert(result.category == milkyway::tsf::service::KeyEventCategory::kBackspace);
  assert(result.eaten);
  assert(!result.should_forward);
  assert(result.commit_text.empty());
  assert(result.preedit_text == "\xE3\x85\x87");
  assert(session.IsComposing());
  assert(session.snapshot().preedit == "\xE3\x85\x87");

  result = service.OnKeyEvent(Key(0x08), {},  // VK_BACK
                              milkyway::engine::key::KeyTransition::kPressed);
  assert(result.category == milkyway::tsf::service::KeyEventCategory::kBackspace);
  assert(result.eaten);
  assert(!result.should_forward);
  assert(result.commit_text.empty());
  assert(result.preedit_text.empty());
  assert(!session.IsComposing());
  assert(session.snapshot().preedit.empty());
  assert(session.last_end_reason() ==
         milkyway::engine::session::CompositionEndReason::kBackspace);
  assert(sink.operations.size() == 5);
  AssertOperation(sink.operations[2], TextEditOperationType::kUpdateComposition,
                  "\xE3\x85\x87");
  AssertOperation(sink.operations[3], TextEditOperationType::kUpdateComposition,
                  "");
  AssertOperation(sink.operations[4], TextEditOperationType::kEndComposition,
                  "");
}

void TestTextServiceAutoReorder(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  milkyway::engine::session::InputSession session(
      registry.DefaultPhysicalLayout().id, registry.DefaultKoreanLayout().id);
  RecordingEditSink sink;
  milkyway::tsf::service::TextService service(
      &session, milkyway::adapters::libhangul::CreateLibhangulComposer(),
      &sink, &registry);

  auto result =
      service.OnKeyEvent(Key('K'), {}, milkyway::engine::key::KeyTransition::kPressed);
  assert(result.category ==
         milkyway::tsf::service::KeyEventCategory::kHangulAscii);
  assert(result.eaten);
  assert(result.preedit_text == "\xE3\x85\x8F");
  assert(session.IsComposing());
  assert(session.snapshot().preedit == "\xE3\x85\x8F");
  assert(sink.operations.size() == 1);
  AssertOperation(sink.operations[0], TextEditOperationType::kStartComposition,
                  "\xE3\x85\x8F");

  result =
      service.OnKeyEvent(Key('D'), {}, milkyway::engine::key::KeyTransition::kPressed);
  assert(result.category ==
         milkyway::tsf::service::KeyEventCategory::kHangulAscii);
  assert(result.eaten);
  assert(result.commit_text.empty());
  assert(result.preedit_text == "\xEC\x95\x84");
  assert(session.IsComposing());
  assert(session.snapshot().preedit == "\xEC\x95\x84");
  assert(sink.operations.size() == 2);
  AssertOperation(sink.operations[1], TextEditOperationType::kUpdateComposition,
                  "\xEC\x95\x84");
}

void TestTextServicePrepareImeModeToggle(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  {
    milkyway::engine::session::InputSession session(
        registry.DefaultPhysicalLayout().id, registry.DefaultKoreanLayout().id);
    RecordingEditSink sink;
    milkyway::tsf::service::TextService service(
        &session, milkyway::adapters::libhangul::CreateLibhangulComposer(),
        &sink, &registry);

    service.OnKeyEvent(Key('G'), {}, milkyway::engine::key::KeyTransition::kPressed);
    service.OnKeyEvent(Key('K'), {}, milkyway::engine::key::KeyTransition::kPressed);
    service.OnKeyEvent(Key('S'), {}, milkyway::engine::key::KeyTransition::kPressed);

    assert(service.PrepareImeModeToggle());
    assert(!session.IsComposing());
    assert(session.last_end_reason() ==
           milkyway::engine::session::CompositionEndReason::kImeModeToggle);
    assert(sink.operations.size() == 5);
    AssertOperation(sink.operations[3], TextEditOperationType::kCommitText,
                    "\xED\x95\x9C");
    AssertOperation(sink.operations[4], TextEditOperationType::kEndComposition,
                    "");

    const auto result =
        service.OnKeyEvent(Key('R'), {}, milkyway::engine::key::KeyTransition::kPressed);
    assert(result.preedit_text == "\xE3\x84\xB1");
    assert(session.IsComposing());
    assert(session.snapshot().preedit == "\xE3\x84\xB1");
  }

  {
    milkyway::engine::session::InputSession session(
        registry.DefaultPhysicalLayout().id, registry.DefaultKoreanLayout().id);
    RecordingEditSink sink;
    milkyway::tsf::service::TextService service(
        &session, milkyway::adapters::libhangul::CreateLibhangulComposer(),
        &sink, &registry);

    assert(!service.PrepareImeModeToggle());
    assert(sink.operations.empty());
    assert(!session.IsComposing());
  }
}

void TestTextServiceShortcutAndTermination(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  auto assert_pure_modifier_passthrough =
      [&](std::uint16_t virtual_key,
          const milkyway::engine::state::ModifierState& modifiers) {
        milkyway::engine::session::InputSession session(
            registry.DefaultPhysicalLayout().id,
            registry.DefaultKoreanLayout().id);
        RecordingEditSink sink;
        milkyway::tsf::service::TextService service(
            &session, milkyway::adapters::libhangul::CreateLibhangulComposer(),
            &sink, &registry);

        service.OnKeyEvent(Key('G'), {},
                           milkyway::engine::key::KeyTransition::kPressed);
        const std::size_t operation_count = sink.operations.size();
        const auto result = service.OnKeyEvent(
            Key(virtual_key), modifiers,
            milkyway::engine::key::KeyTransition::kPressed);
        assert(!service.WouldEatKey(
            Key(virtual_key), modifiers,
            milkyway::engine::key::KeyTransition::kPressed));
        assert(result.category ==
               milkyway::tsf::service::KeyEventCategory::kPureModifier);
        assert(!result.eaten);
        assert(result.should_forward);
        assert(result.shortcut_action ==
               milkyway::engine::shortcut::ShortcutAction::kNone);
        assert(result.commit_text.empty());
        assert(result.preedit_text.empty());
        assert(session.IsComposing());
        assert(session.snapshot().preedit == "\xE3\x85\x8E");
        assert(sink.operations.size() == operation_count);
      };

  {
    milkyway::engine::state::ModifierState modifiers;
    modifiers.shift = true;
    assert_pure_modifier_passthrough(kVkShift, modifiers);
  }

  {
    milkyway::engine::state::ModifierState modifiers;
    modifiers.ctrl = true;
    assert_pure_modifier_passthrough(kVkControl, modifiers);
  }

  {
    milkyway::engine::state::ModifierState modifiers;
    modifiers.alt = true;
    assert_pure_modifier_passthrough(kVkMenu, modifiers);
  }

  {
    milkyway::engine::state::ModifierState modifiers;
    modifiers.win = true;
    assert_pure_modifier_passthrough(kVkLwin, modifiers);
  }

  {
    milkyway::engine::session::InputSession session(
        registry.DefaultPhysicalLayout().id, registry.DefaultKoreanLayout().id);
    RecordingEditSink sink;
    milkyway::tsf::service::TextService service(
        &session, milkyway::adapters::libhangul::CreateLibhangulComposer(),
        &sink, &registry);

    service.OnKeyEvent(Key('G'), {}, milkyway::engine::key::KeyTransition::kPressed);
    milkyway::engine::state::ModifierState modifiers;
    modifiers.ctrl = true;
    modifiers.shift = true;
    assert(service.WouldEatKey(Key(kVkSpace), modifiers,
                               milkyway::engine::key::KeyTransition::kPressed));
    const auto result =
        service.OnKeyEvent(Key(kVkSpace), modifiers,
                           milkyway::engine::key::KeyTransition::kPressed);
    assert(result.category ==
           milkyway::tsf::service::KeyEventCategory::kModifiedShortcut);
    assert(result.eaten);
    assert(!result.should_forward);
    assert(result.shortcut_action ==
           milkyway::engine::shortcut::ShortcutAction::kToggleInputMode);
    assert(!session.IsComposing());
    assert(session.last_end_reason() ==
           milkyway::engine::session::CompositionEndReason::kImeModeToggle);
    assert(sink.operations.size() == 3);
    AssertOperation(sink.operations[1], TextEditOperationType::kCommitText,
                    "\xE3\x85\x8E");
    AssertOperation(sink.operations[2], TextEditOperationType::kEndComposition,
                    "");
  }

  {
    milkyway::engine::session::InputSession session(
        registry.DefaultPhysicalLayout().id, registry.DefaultKoreanLayout().id);
    RecordingEditSink sink;
    milkyway::tsf::service::TextService service(
        &session, milkyway::adapters::libhangul::CreateLibhangulComposer(),
        &sink, &registry);

    service.OnKeyEvent(Key('G'), {}, milkyway::engine::key::KeyTransition::kPressed);
    assert(service.PrepareLayoutChange());
    assert(!session.IsComposing());
    assert(session.last_end_reason() ==
           milkyway::engine::session::CompositionEndReason::kLayoutChanged);
    assert(sink.operations.size() == 3);
    AssertOperation(sink.operations[1], TextEditOperationType::kCommitText,
                    "\xE3\x85\x8E");
    AssertOperation(sink.operations[2], TextEditOperationType::kEndComposition,
                    "");
  }

  {
    milkyway::engine::session::InputSession session(
        registry.DefaultPhysicalLayout().id, registry.DefaultKoreanLayout().id);
    RecordingEditSink sink;
    milkyway::tsf::service::TextService service(
        &session, milkyway::adapters::libhangul::CreateLibhangulComposer(),
        &sink, &registry);

    service.OnKeyEvent(Key('G'), {}, milkyway::engine::key::KeyTransition::kPressed);
    milkyway::engine::state::ModifierState modifiers;
    modifiers.ctrl = true;
    modifiers.alt = true;
    assert(!service.WouldEatKey(Key('O'), modifiers,
                                milkyway::engine::key::KeyTransition::kPressed));
    const auto result =
        service.OnKeyEvent(Key('O'), modifiers,
                           milkyway::engine::key::KeyTransition::kPressed);
    assert(result.category ==
           milkyway::tsf::service::KeyEventCategory::kModifiedShortcut);
    assert(!result.eaten);
    assert(result.should_forward);
    assert(result.shortcut_action ==
           milkyway::engine::shortcut::ShortcutAction::kNone);
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
        &sink, &registry);

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
        &sink, &registry);

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
        &sink, &registry);

    service.OnKeyEvent(Key('G'), {}, milkyway::engine::key::KeyTransition::kPressed);
    const std::size_t operation_count = sink.operations.size();
    service.OnCompositionTerminated();
    assert(!session.IsComposing());
    assert(session.last_end_reason() ==
           milkyway::engine::session::CompositionEndReason::kExternalTermination);
    assert(sink.operations.size() == operation_count);
  }
}

void TestTextServiceShiftFinalSsangSios(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  milkyway::engine::session::InputSession session(
      registry.DefaultPhysicalLayout().id, registry.DefaultKoreanLayout().id);
  RecordingEditSink sink;
  milkyway::tsf::service::TextService service(
      &session, milkyway::adapters::libhangul::CreateLibhangulComposer(),
      &sink, &registry);

  auto result =
      service.OnKeyEvent(Key('R'), {}, milkyway::engine::key::KeyTransition::kPressed);
  assert(result.preedit_text == "\xE3\x84\xB1");
  assert(session.snapshot().preedit == "\xE3\x84\xB1");

  result =
      service.OnKeyEvent(Key('K'), {}, milkyway::engine::key::KeyTransition::kPressed);
  assert(result.preedit_text == "\xEA\xB0\x80");
  assert(session.snapshot().preedit == "\xEA\xB0\x80");

  milkyway::engine::state::ModifierState shift_modifiers;
  shift_modifiers.shift = true;
  const std::size_t operation_count = sink.operations.size();
  result = service.OnKeyEvent(Key(kVkShift), shift_modifiers,
                              milkyway::engine::key::KeyTransition::kPressed);
  assert(result.category ==
         milkyway::tsf::service::KeyEventCategory::kPureModifier);
  assert(!result.eaten);
  assert(result.should_forward);
  assert(session.IsComposing());
  assert(session.snapshot().preedit == "\xEA\xB0\x80");
  assert(sink.operations.size() == operation_count);

  result = service.OnKeyEvent(Key('T'), shift_modifiers,
                              milkyway::engine::key::KeyTransition::kPressed);
  assert(result.category ==
         milkyway::tsf::service::KeyEventCategory::kHangulAscii);
  assert(result.eaten);
  assert(result.commit_text.empty());
  assert(result.preedit_text == "\xEA\xB0\x94");
  assert(session.IsComposing());
  assert(session.snapshot().preedit == "\xEA\xB0\x94");
  assert(sink.operations.size() == 3);
  AssertOperation(sink.operations[2], TextEditOperationType::kUpdateComposition,
                  "\xEA\xB0\x94");
}

void TestTextServiceShiftDoubleVowels(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  {
    milkyway::engine::session::InputSession session(
        registry.DefaultPhysicalLayout().id, registry.DefaultKoreanLayout().id);
    RecordingEditSink sink;
    milkyway::tsf::service::TextService service(
        &session, milkyway::adapters::libhangul::CreateLibhangulComposer(),
        &sink, &registry);

    milkyway::engine::state::ModifierState shift_modifiers;
    shift_modifiers.shift = true;
    const auto result =
        service.OnKeyEvent(Key('O'), shift_modifiers,
                           milkyway::engine::key::KeyTransition::kPressed);
    assert(result.category ==
           milkyway::tsf::service::KeyEventCategory::kHangulAscii);
    assert(result.eaten);
    assert(result.preedit_text == "\xE3\x85\x92");
    assert(session.IsComposing());
    assert(session.snapshot().preedit == "\xE3\x85\x92");
  }

  {
    milkyway::engine::session::InputSession session(
        registry.DefaultPhysicalLayout().id, registry.DefaultKoreanLayout().id);
    RecordingEditSink sink;
    milkyway::tsf::service::TextService service(
        &session, milkyway::adapters::libhangul::CreateLibhangulComposer(),
        &sink, &registry);

    milkyway::engine::state::ModifierState shift_modifiers;
    shift_modifiers.shift = true;
    const auto result =
        service.OnKeyEvent(Key('P'), shift_modifiers,
                           milkyway::engine::key::KeyTransition::kPressed);
    assert(result.category ==
           milkyway::tsf::service::KeyEventCategory::kHangulAscii);
    assert(result.eaten);
    assert(result.preedit_text == "\xE3\x85\x96");
    assert(session.IsComposing());
    assert(session.snapshot().preedit == "\xE3\x85\x96");
  }
}

void TestTextEditPlanPreservesReorderedSyllableCommit() {
  using milkyway::tsf::edit::PlannedEditActionType;
  using milkyway::tsf::edit::PlanTextEditActions;

  const std::vector<TextEditOperation> operations = {
      {TextEditOperationType::kCommitText, "\xEC\x9D\xB4"},
      {TextEditOperationType::kUpdateComposition, "\xEA\xB2\x8C"},
  };

  const auto plan = PlanTextEditActions(true, operations);
  assert(plan.size() == 3);
  assert(plan[0].type == PlannedEditActionType::kUpdateComposition);
  assert(plan[0].text == "\xEC\x9D\xB4");
  assert(plan[1].type == PlannedEditActionType::kCompleteComposition);
  assert(plan[2].type == PlannedEditActionType::kStartComposition);
  assert(plan[2].text == "\xEA\xB2\x8C");
}

void TestTextEditPlanCompletesCommittedCompositionText() {
  using milkyway::tsf::edit::PlannedEditActionType;
  using milkyway::tsf::edit::PlanTextEditActions;

  const std::vector<TextEditOperation> operations = {
      {TextEditOperationType::kCommitText, "\xEC\x9D\xB4"},
      {TextEditOperationType::kEndComposition, {}},
  };

  const auto plan = PlanTextEditActions(true, operations);
  assert(plan.size() == 2);
  assert(plan[0].type == PlannedEditActionType::kUpdateComposition);
  assert(plan[0].text == "\xEC\x9D\xB4");
  assert(plan[1].type == PlannedEditActionType::kCompleteComposition);
}

void TestTextEditPlanPreservesEmptyCompositionUpdateBeforeCompletion() {
  using milkyway::tsf::edit::PlannedEditActionType;
  using milkyway::tsf::edit::PlanTextEditActions;

  const std::vector<TextEditOperation> operations = {
      {TextEditOperationType::kUpdateComposition, {}},
      {TextEditOperationType::kEndComposition, {}},
  };

  const auto plan = PlanTextEditActions(true, operations);
  assert(plan.size() == 2);
  assert(plan[0].type == PlannedEditActionType::kUpdateComposition);
  assert(plan[0].text.empty());
  assert(plan[1].type == PlannedEditActionType::kCompleteComposition);
}

}  // namespace

int main() {
#if defined(_WIN32) && defined(_DEBUG)
  _set_error_mode(_OUT_TO_STDERR);
  _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

  milkyway::engine::layout::LayoutRegistry registry;

  assert(registry.DefaultPhysicalLayout().id == "us_qwerty");
  assert(registry.DefaultKoreanLayout().id == "ko_dubeolsik");

  TestInputSession();
  TestLibhangulComposer();
  TestLibhangulComposerAutoReorder();
  TestLibhangulComposerShiftFinalSsangSios();
  TestShortcutResolver(registry);
  TestLayoutRegistryEffectiveBaseLayout(registry);
  TestBuiltInPhysicalLayouts(registry);
  TestTextServiceLifecycle(registry);
  TestTextServiceBaseLayoutPrintableDelimiter(registry);
  TestTextServiceBackspaceClearsVisibleComposition(registry);
  TestTextServiceAutoReorder(registry);
  TestTextServicePrepareImeModeToggle(registry);
  TestTextServiceShortcutAndTermination(registry);
  TestTextServiceShiftFinalSsangSios(registry);
  TestTextServiceShiftDoubleVowels(registry);
  TestTextEditPlanPreservesReorderedSyllableCommit();
  TestTextEditPlanCompletesCommittedCompositionText();
  TestTextEditPlanPreservesEmptyCompositionUpdateBeforeCompletion();

  return 0;
}
