#include <cassert>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#endif

#include "adapters/dictionary/libhangul_hanja_dictionary.h"
#include "adapters/libhangul/hangul_composer.h"
#include "engine/hanja/candidate_request.h"
#include "engine/key/key_analysis.h"
#include "engine/key/physical_key.h"
#include "engine/layout/base_layout_json_loader.h"
#include "engine/layout/layout_registry.h"
#include "engine/session/input_session.h"
#include "engine/shortcut/shortcut_resolver.h"
#include "engine/state/modifier_state.h"
#include "tsf/edit/text_edit_sink.h"
#include "tsf/edit/text_edit_plan.h"
#include "tsf/edit/transitory_composition_bridge.h"
#include "tsf/edit/transitory_direct_text_composition.h"
#include "tsf/service/text_service.h"
#include "tsf/settings/user_settings.h"
#include "ui/candidate/candidate_theme.h"
#include "ui/candidate/candidate_window.h"

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

std::string ReadTextFile(const char* path) {
  std::ifstream stream(path, std::ios::binary);
  assert(stream);
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

void AssertOperation(const TextEditOperation& operation,
                     TextEditOperationType type, std::string_view text) {
  assert(operation.type == type);
  assert(operation.text == text);
}

void TestInputSession() {
  milkyway::engine::session::InputSession session("us_qwerty", "libhangul:2");

  assert(!session.IsComposing());
  assert(session.last_end_reason() ==
         milkyway::engine::session::CompositionEndReason::kNone);
  assert(!session.RequestHanjaConversion().has_value());

  session.StartComposition("\xED\x95\x9C");
  assert(session.IsComposing());
  assert(session.snapshot().preedit == "\xED\x95\x9C");

  const auto hanja_request = session.RequestHanjaConversion();
  assert(hanja_request.has_value());
  assert(hanja_request->query_text == "\xED\x95\x9C");
  assert(hanja_request->kind ==
         milkyway::engine::hanja::CandidateKind::kHanjaForward);

  session.UpdateComposition("\xE3\x85\x81");
  const auto symbol_request = session.RequestHanjaConversion();
  assert(symbol_request.has_value());
  assert(symbol_request->query_text == "\xE3\x85\x81");
  assert(symbol_request->kind ==
         milkyway::engine::hanja::CandidateKind::kSymbol);

  session.UpdateComposition("\xEA\xB8\x80");
  assert(session.snapshot().preedit == "\xEA\xB8\x80");

  session.UpdateComposition("\xED\x95\x9C\xEA\xB5\xAD");
  assert(!session.RequestHanjaConversion().has_value());

  session.EndComposition(
      milkyway::engine::session::CompositionEndReason::kDelimiter);
  assert(!session.IsComposing());
  assert(session.snapshot().preedit.empty());
  assert(session.last_end_reason() ==
         milkyway::engine::session::CompositionEndReason::kDelimiter);
  assert(!session.RequestHanjaConversion().has_value());
}

bool ContainsCandidateValue(
    const std::vector<milkyway::engine::hanja::Candidate>& candidates,
    std::string_view value) {
  return std::any_of(candidates.begin(), candidates.end(),
                     [value](const milkyway::engine::hanja::Candidate& item) {
                       return item.value == value;
                     });
}

bool ContainsCandidateKey(
    const std::vector<milkyway::engine::hanja::Candidate>& candidates,
    std::string_view key) {
  return std::any_of(candidates.begin(), candidates.end(),
                     [key](const milkyway::engine::hanja::Candidate& item) {
                       return item.key == key;
                     });
}

void TestHanjaCandidateRequestValidation() {
  using milkyway::engine::hanja::CandidateKind;
  using milkyway::engine::hanja::CreateCandidateRequestFromPreedit;

  const auto hanja = CreateCandidateRequestFromPreedit("\xED\x95\x9C");
  assert(hanja.has_value());
  assert(hanja->kind == CandidateKind::kHanjaForward);

  const auto symbol = CreateCandidateRequestFromPreedit("\xE3\x85\x81");
  assert(symbol.has_value());
  assert(symbol->kind == CandidateKind::kSymbol);

  assert(!CreateCandidateRequestFromPreedit("").has_value());
  assert(!CreateCandidateRequestFromPreedit("A").has_value());
  assert(!CreateCandidateRequestFromPreedit("\xED\x95\x9C\xEA\xB5\xAD")
              .has_value());
  assert(!CreateCandidateRequestFromPreedit("\xE3\x85").has_value());
}

void TestSelectionHanjaPrefixRequests() {
  using milkyway::engine::hanja::CandidateKind;
  using milkyway::engine::hanja::CreateSelectionHanjaPrefixRequests;
  using milkyway::engine::hanja::CreateSelectionHanjaReversePrefixRequests;

  const auto korea =
      CreateSelectionHanjaPrefixRequests("\xED\x95\x9C\xEA\xB5\xAD");
  assert(korea.size() == 2);
  assert(korea[0].request.query_text == "\xED\x95\x9C\xEA\xB5\xAD");
  assert(korea[0].request.kind == CandidateKind::kHanjaForward);
  assert(korea[0].matched_byte_length == 6);
  assert(korea[0].matched_utf16_length == 2);
  assert(korea[1].request.query_text == "\xED\x95\x9C");
  assert(korea[1].matched_byte_length == 3);
  assert(korea[1].matched_utf16_length == 1);

  const auto korea_sentence = CreateSelectionHanjaPrefixRequests(
      "\xED\x95\x9C\xEA\xB5\xAD\xEC\x9E\x85\xEB\x8B\x88\xEB\x8B\xA4");
  assert(korea_sentence.size() == 5);
  assert(korea_sentence[3].request.query_text ==
         "\xED\x95\x9C\xEA\xB5\xAD");
  assert(korea_sentence[3].matched_utf16_length == 2);

  const auto han = CreateSelectionHanjaPrefixRequests("\xED\x95\x9C");
  assert(han.size() == 1);
  assert(han[0].request.query_text == "\xED\x95\x9C");
  assert(han[0].matched_utf16_length == 1);

  const auto polite_sentence = CreateSelectionHanjaPrefixRequests(
      "\xEC\xA0\x80\xEB\x8A\x94 "
      "\xED\x95\x9C\xEA\xB5\xAD\xEC\x9E\x85\xEB\x8B\x88\xEB\x8B\xA4");
  assert(polite_sentence.size() == 2);
  assert(polite_sentence[0].request.query_text ==
         "\xEC\xA0\x80\xEB\x8A\x94");
  assert(polite_sentence[1].request.query_text == "\xEC\xA0\x80");

  const auto annyeong = CreateSelectionHanjaReversePrefixRequests(
      "\xE5\xAE\x89\xE5\xAF\xA7\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94");
  assert(annyeong.size() == 2);
  assert(annyeong[0].request.query_text == "\xE5\xAE\x89\xE5\xAF\xA7");
  assert(annyeong[0].request.kind == CandidateKind::kHanjaReverse);
  assert(annyeong[0].matched_byte_length == 6);
  assert(annyeong[0].matched_utf16_length == 2);
  assert(annyeong[1].request.query_text == "\xE5\xAE\x89");
  assert(annyeong[1].matched_utf16_length == 1);

  assert(CreateSelectionHanjaPrefixRequests("").empty());
  assert(CreateSelectionHanjaPrefixRequests("A").empty());
  assert(CreateSelectionHanjaPrefixRequests("\xE3\x85\x81").empty());
  assert(CreateSelectionHanjaReversePrefixRequests("").empty());
  assert(CreateSelectionHanjaReversePrefixRequests("A").empty());
  assert(CreateSelectionHanjaReversePrefixRequests("\xED\x95\x9C").empty());
}

void TestLibhangulHanjaDictionary() {
  using milkyway::adapters::dictionary::LibhangulHanjaDictionary;
  using milkyway::engine::hanja::CaretHanjaRun;
  using milkyway::engine::hanja::CandidateKind;
  using milkyway::engine::hanja::CandidateRequest;
  using milkyway::engine::hanja::CreateCaretHanjaRun;
  using milkyway::engine::hanja::CreateSelectionHanjaPrefixRequests;
  using milkyway::engine::hanja::CreateSelectionHanjaReversePrefixRequests;
  using milkyway::engine::hanja::SelectionHanjaPrefixRequest;

  struct ResolvedSelectionTarget {
    SelectionHanjaPrefixRequest request;
    std::vector<milkyway::engine::hanja::Candidate> candidates;
  };

  struct ResolvedCaretSegment {
    SelectionHanjaPrefixRequest request;
    std::size_t start_utf16_offset = 0;
    std::vector<milkyway::engine::hanja::Candidate> candidates;
  };

  LibhangulHanjaDictionary dictionary;

  const auto hanja_candidates =
      dictionary.Lookup(
          CandidateRequest{"\xED\x95\x9C", CandidateKind::kHanjaForward});
  assert(!hanja_candidates.empty());
  assert(ContainsCandidateValue(hanja_candidates, "\xE9\x9F\x93"));

  const auto korea_candidates = dictionary.Lookup(
      CandidateRequest{"\xED\x95\x9C\xEA\xB5\xAD",
                       CandidateKind::kHanjaForward});
  assert(!korea_candidates.empty());
  assert(ContainsCandidateValue(korea_candidates,
                                "\xE9\x9F\x93\xE5\x9C\x8B"));

  const auto pokryeol_candidates = dictionary.Lookup(
      CandidateRequest{"\xED\x8F\xAD\xEB\xA0\xAC",
                       CandidateKind::kHanjaForward});
  assert(!pokryeol_candidates.empty());
  assert(ContainsCandidateValue(pokryeol_candidates,
                                "\xE7\x88\x86\xE8\xA3\x82"));

  const auto cheonsa_candidates = dictionary.Lookup(
      CandidateRequest{"\xEC\xB2\x9C\xEC\x82\xAC",
                       CandidateKind::kHanjaForward});
  assert(!cheonsa_candidates.empty());
  assert(ContainsCandidateValue(cheonsa_candidates,
                                "\xE5\xA4\xA9\xE4\xBD\xBF"));

  const auto symbol_candidates = dictionary.Lookup(
      CandidateRequest{"\xE3\x85\x81", CandidateKind::kSymbol});
  assert(!symbol_candidates.empty());
  assert(ContainsCandidateValue(symbol_candidates, "\xE2\x80\xBB"));

  const auto reverse_annyeong = dictionary.Lookup(
      CandidateRequest{"\xE5\xAE\x89\xE5\xAF\xA7",
                       CandidateKind::kHanjaReverse});
  assert(!reverse_annyeong.empty());
  assert(ContainsCandidateValue(reverse_annyeong,
                                "\xEC\x95\x88\xEB\x85\x95"));
  assert(ContainsCandidateKey(reverse_annyeong, "\xE5\xAE\x89\xE5\xAF\xA7"));

  const auto reverse_pokryeol = dictionary.Lookup(
      CandidateRequest{"\xE7\x88\x86\xE8\xA3\x82",
                       CandidateKind::kHanjaReverse});
  assert(!reverse_pokryeol.empty());
  assert(ContainsCandidateValue(reverse_pokryeol,
                                "\xED\x8F\xAD\xEB\xA0\xAC"));

  const auto reverse_cheonsa = dictionary.Lookup(
      CandidateRequest{"\xE5\xA4\xA9\xE4\xBD\xBF",
                       CandidateKind::kHanjaReverse});
  assert(!reverse_cheonsa.empty());
  assert(ContainsCandidateValue(reverse_cheonsa,
                                "\xEC\xB2\x9C\xEC\x82\xAC"));

  const auto resolve_selection_target =
      [&dictionary](std::string_view selected_text)
      -> std::optional<ResolvedSelectionTarget> {
    std::vector<SelectionHanjaPrefixRequest> requests =
        CreateSelectionHanjaPrefixRequests(selected_text);
    if (requests.empty()) {
      requests = CreateSelectionHanjaReversePrefixRequests(selected_text);
    }
    for (const SelectionHanjaPrefixRequest& request : requests) {
      std::vector<milkyway::engine::hanja::Candidate> candidates =
          dictionary.Lookup(request.request);
      if (!candidates.empty()) {
        return ResolvedSelectionTarget{request, std::move(candidates)};
      }
    }
    return std::nullopt;
  };

  const auto korea_target =
      resolve_selection_target("\xED\x95\x9C\xEA\xB5\xAD");
  assert(korea_target.has_value());
  assert(korea_target->request.request.query_text ==
         "\xED\x95\x9C\xEA\xB5\xAD");
  assert(korea_target->request.matched_utf16_length == 2);
  assert(ContainsCandidateValue(korea_target->candidates,
                                "\xE9\x9F\x93\xE5\x9C\x8B"));

  const auto korea_sentence_target = resolve_selection_target(
      "\xED\x95\x9C\xEA\xB5\xAD\xEC\x9E\x85\xEB\x8B\x88\xEB\x8B\xA4");
  assert(korea_sentence_target.has_value());
  assert(korea_sentence_target->request.request.query_text ==
         "\xED\x95\x9C\xEA\xB5\xAD");
  assert(korea_sentence_target->request.matched_utf16_length == 2);

  const auto han_target = resolve_selection_target("\xED\x95\x9C");
  assert(han_target.has_value());
  assert(han_target->request.request.query_text == "\xED\x95\x9C");
  assert(han_target->request.matched_utf16_length == 1);
  assert(ContainsCandidateValue(han_target->candidates, "\xE9\x9F\x93"));

  const auto annyeong_target = resolve_selection_target(
      "\xE5\xAE\x89\xE5\xAF\xA7\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94");
  assert(annyeong_target.has_value());
  assert(annyeong_target->request.request.query_text ==
         "\xE5\xAE\x89\xE5\xAF\xA7");
  assert(annyeong_target->request.request.kind == CandidateKind::kHanjaReverse);
  assert(annyeong_target->request.matched_utf16_length == 2);
  assert(ContainsCandidateValue(annyeong_target->candidates,
                                "\xEC\x95\x88\xEB\x85\x95"));

  const auto polite_reverse_target = resolve_selection_target(
      "\xEC\xA0\x80\xEB\x8A\x94 "
      "\xE5\xAE\x89\xE5\xAF\xA7\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94");
  assert(polite_reverse_target.has_value());
  assert(polite_reverse_target->request.request.query_text == "\xEC\xA0\x80");
  assert(polite_reverse_target->request.request.kind ==
         CandidateKind::kHanjaForward);

  const auto resolve_caret_segments =
      [&dictionary](const CaretHanjaRun& run) {
    std::vector<ResolvedCaretSegment> segments;
    std::size_t byte_offset = 0;
    std::size_t utf16_offset = 0;
    while (byte_offset < run.text.size()) {
      const std::string_view remaining(run.text.data() + byte_offset,
                                       run.text.size() - byte_offset);
      std::vector<SelectionHanjaPrefixRequest> requests =
          run.kind == CandidateKind::kHanjaReverse
              ? CreateSelectionHanjaReversePrefixRequests(remaining)
              : CreateSelectionHanjaPrefixRequests(remaining);
      bool resolved = false;
      for (const SelectionHanjaPrefixRequest& request : requests) {
        std::vector<milkyway::engine::hanja::Candidate> candidates =
            dictionary.Lookup(request.request);
        if (candidates.empty()) {
          continue;
        }
        segments.push_back(ResolvedCaretSegment{
            request, utf16_offset, std::move(candidates)});
        byte_offset += request.matched_byte_length;
        utf16_offset += request.matched_utf16_length;
        resolved = true;
        break;
      }
      if (!resolved) {
        break;
      }
    }
    return segments;
  };

  const auto pokryeol_cheonsa = CreateCaretHanjaRun(
      "\xED\x8F\xAD\xEB\xA0\xAC\xEC\xB2\x9C\xEC\x82\xAC");
  assert(pokryeol_cheonsa.has_value());
  assert(pokryeol_cheonsa->text ==
         "\xED\x8F\xAD\xEB\xA0\xAC\xEC\xB2\x9C\xEC\x82\xAC");
  assert(pokryeol_cheonsa->kind == CandidateKind::kHanjaForward);
  assert(pokryeol_cheonsa->utf16_length == 4);
  const auto forward_segments = resolve_caret_segments(*pokryeol_cheonsa);
  assert(forward_segments.size() == 2);
  assert(forward_segments[0].request.request.query_text ==
         "\xED\x8F\xAD\xEB\xA0\xAC");
  assert(forward_segments[0].start_utf16_offset == 0);
  assert(ContainsCandidateValue(forward_segments[0].candidates,
                                "\xE7\x88\x86\xE8\xA3\x82"));
  assert(forward_segments[1].request.request.query_text ==
         "\xEC\xB2\x9C\xEC\x82\xAC");
  assert(forward_segments[1].start_utf16_offset == 2);
  assert(ContainsCandidateValue(forward_segments[1].candidates,
                                "\xE5\xA4\xA9\xE4\xBD\xBF"));

  const auto hanja_cheonsa = CreateCaretHanjaRun(
      "\xE7\x88\x86\xE8\xA3\x82\xE5\xA4\xA9\xE4\xBD\xBF");
  assert(hanja_cheonsa.has_value());
  assert(hanja_cheonsa->kind == CandidateKind::kHanjaReverse);
  assert(hanja_cheonsa->utf16_length == 4);
  const auto reverse_segments = resolve_caret_segments(*hanja_cheonsa);
  assert(reverse_segments.size() == 2);
  assert(reverse_segments[0].request.request.query_text ==
         "\xE7\x88\x86\xE8\xA3\x82");
  assert(ContainsCandidateValue(reverse_segments[0].candidates,
                                "\xED\x8F\xAD\xEB\xA0\xAC"));
  assert(reverse_segments[1].request.request.query_text ==
         "\xE5\xA4\xA9\xE4\xBD\xBF");
  assert(ContainsCandidateValue(reverse_segments[1].candidates,
                                "\xEC\xB2\x9C\xEC\x82\xAC"));

  const auto sentence_run = CreateCaretHanjaRun(
      "\xED\x8F\xAD\xEB\xA0\xAC\xEC\xB2\x9C\xEC\x82\xAC\xEB\xA5\xBC "
      "\xEB\xB4\xA4\xEB\x8B\xA4");
  assert(sentence_run.has_value());
  assert(sentence_run->text == "\xEB\xB4\xA4\xEB\x8B\xA4");
  assert(sentence_run->utf16_length == 2);

  assert(!resolve_selection_target("").has_value());
  assert(!resolve_selection_target("A").has_value());
  assert(!resolve_selection_target("\xE3\x85\x81").has_value());
  const auto polite_forward_target = resolve_selection_target(
      "\xEC\xA0\x80\xEB\x8A\x94 "
      "\xED\x95\x9C\xEA\xB5\xAD\xEC\x9E\x85\xEB\x8B\x88\xEB\x8B\xA4");
  assert(polite_forward_target.has_value());
  assert(polite_forward_target->request.request.query_text == "\xEC\xA0\x80");
  assert(polite_forward_target->request.request.kind ==
         CandidateKind::kHanjaForward);
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

void TestLibhangulComposerShinsebeol() {
  {
    auto composer =
        milkyway::adapters::libhangul::CreateLibhangulComposer("3sin-1995");
    assert(composer != nullptr);

    auto step = composer->ProcessAscii('u');
    assert(step.consumed);
    assert(step.commit_text.empty());
    assert(step.preedit_text == "\xE3\x84\xB7");

    step = composer->ProcessAscii('q');
    assert(step.consumed);
    assert(step.commit_text.empty());
    assert(step.preedit_text == "\xEB\x93\xB8");
  }

  {
    auto composer =
        milkyway::adapters::libhangul::CreateLibhangulComposer("3sin-p2");
    assert(composer != nullptr);

    auto step = composer->ProcessAscii('u');
    assert(step.consumed);
    assert(step.commit_text.empty());
    assert(step.preedit_text == "\xE3\x84\xB7");

    step = composer->ProcessAscii('z');
    assert(step.consumed);
    assert(step.commit_text.empty());
    assert(step.preedit_text == "\xEB\x93\xB8");
  }
}

void TestShortcutResolver(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  milkyway::engine::shortcut::ShortcutResolver resolver;
  milkyway::engine::shortcut::ShortcutQuery query;
  query.base_layout_id = registry.DefaultBaseLayout().id;
  query.modifiers.ctrl = true;
  query.modifiers.shift = true;
  query.input_label_key =
      milkyway::engine::key::LayoutKey::kSpace;

  assert(resolver.Resolve(query) ==
         milkyway::engine::shortcut::ShortcutAction::kNone);

  milkyway::engine::shortcut::ShortcutQuery empty_layout_query = query;
  empty_layout_query.base_layout_id.clear();
  assert(resolver.Resolve(empty_layout_query) ==
         milkyway::engine::shortcut::ShortcutAction::kNone);
}

void TestLayoutRegistryEffectiveBaseLayout(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  milkyway::engine::key::PhysicalKey key;
  key.virtual_key = 'R';
  const auto event = registry.NormalizeKeyEvent(
      registry.DefaultBaseLayout().id, key, {},
      milkyway::engine::key::KeyTransition::kPressed);
  assert(event.input_label_key == milkyway::engine::key::LayoutKey::kR);

  const auto shift_input = registry.ResolveHangulInput(
      registry.DefaultKoreanLayout().id,
      {milkyway::engine::key::LayoutKey::kO, true});
  assert(shift_input.is_mapped);
  assert(shift_input.ascii == 'O');

}

void TestBuiltInKoreanLayouts(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  using milkyway::engine::key::LayoutKey;

  struct ExpectedLayout {
    const char* id;
    const char* display_name;
    const char* libhangul_keyboard_id;
    bool maps_non_letter_ascii;
  };

  const ExpectedLayout expected_layouts[] = {
      {"libhangul:2", "두벌식", "2", false},
      {"libhangul:2y", "두벌식 옛글", "2y", false},
      {"libhangul:32", "세벌식 두벌자판", "32", true},
      {"libhangul:39", "세벌식 390", "39", true},
      {"libhangul:3f", "세벌식 최종", "3f", true},
      {"libhangul:3s", "세벌식 순아래", "3s", true},
      {"libhangul:3y", "세벌식 옛글", "3y", true},
      {"libhangul:3sin-1995", "신세벌식 1995", "3sin-1995", true},
      {"libhangul:3sin-p2", "신세벌식 P2", "3sin-p2", true},
      {"libhangul:ro", "로마자", "ro", true},
      {"libhangul:ahn", "안마태", "ahn", true},
  };

  for (const ExpectedLayout& expected : expected_layouts) {
    const auto* layout = registry.FindKoreanLayout(expected.id);
    assert(layout != nullptr);
    assert(layout->display_name == expected.display_name);
    assert(layout->libhangul_keyboard_id == expected.libhangul_keyboard_id);
    assert(layout->maps_non_letter_ascii == expected.maps_non_letter_ascii);
    assert(registry.ResolveLibhangulKeyboardId(expected.id) ==
           expected.libhangul_keyboard_id);
  }

  assert(registry.FindKoreanLayout("ko_dubeolsik") == nullptr);

  const auto dubeolsik_semicolon =
      registry.ResolveHangulInput("libhangul:2", {LayoutKey::kOem1, false});
  assert(!dubeolsik_semicolon.is_mapped);

  const auto sebeolsik_semicolon =
      registry.ResolveHangulInput("libhangul:3f", {LayoutKey::kOem1, false});
  assert(sebeolsik_semicolon.is_mapped);
  assert(sebeolsik_semicolon.ascii == ';');

  const auto sebeolsik_shift_digit =
      registry.ResolveHangulInput("libhangul:3f", {LayoutKey::kDigit1, true});
  assert(sebeolsik_shift_digit.is_mapped);
  assert(sebeolsik_shift_digit.ascii == '!');

  const auto shinsebeol_semicolon = registry.ResolveHangulInput(
      "libhangul:3sin-1995", {LayoutKey::kOem1, false});
  assert(shinsebeol_semicolon.is_mapped);
  assert(shinsebeol_semicolon.ascii == ';');

  const auto shinsebeol_p2_slash =
      registry.ResolveHangulInput("libhangul:3sin-p2",
                                  {LayoutKey::kOem2, false});
  assert(shinsebeol_p2_slash.is_mapped);
  assert(shinsebeol_p2_slash.ascii == '/');
}

void TestBuiltInBaseLayouts(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  using milkyway::engine::key::LayoutKey;
  using milkyway::engine::key::KeyCategory;
  using milkyway::engine::key::KeyTransition;

  const auto* qwerty = registry.FindBaseLayout("us_qwerty");
  assert(qwerty != nullptr);
  assert(qwerty->display_name == "미국식 쿼티");

  const auto* colemak = registry.FindBaseLayout("colemak");
  assert(colemak != nullptr);
  assert(colemak->display_name == "콜맥");
  assert(registry.FindBaseLayout("colemak_dh") == nullptr);

  auto input_label_key = [&](const char* layout_id, std::uint16_t virtual_key) {
    return registry
        .NormalizeKeyEvent(layout_id, Key(virtual_key), {},
                           KeyTransition::kPressed)
        .input_label_key;
  };

  assert(input_label_key("colemak", 'R') == LayoutKey::kR);

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

  const auto colemak_r = milkyway::engine::key::AnalyzeKeyEvent(
      registry, "colemak", registry.DefaultKoreanLayout().id, Key('R'), {},
      KeyTransition::kPressed);
  assert(colemak_r.input_label_key == LayoutKey::kR);
  assert(colemak_r.hangul_token_key == LayoutKey::kS);
  assert(colemak_r.hangul_input.is_mapped);
  assert(colemak_r.hangul_input.ascii == 's');
  assert(colemak_r.category == KeyCategory::kHangulAscii);

  const auto colemak_p = milkyway::engine::key::AnalyzeKeyEvent(
      registry, "colemak", registry.DefaultKoreanLayout().id, Key('P'), {},
      KeyTransition::kPressed);
  assert(colemak_p.input_label_key == LayoutKey::kP);
  assert(colemak_p.hangul_token_key == LayoutKey::kR);
  assert(colemak_p.hangul_input.is_mapped);
  assert(colemak_p.hangul_input.ascii == 'r');
  assert(colemak_p.category == KeyCategory::kHangulAscii);

  const auto colemak_o = milkyway::engine::key::AnalyzeKeyEvent(
      registry, "colemak", registry.DefaultKoreanLayout().id, Key('O'), {},
      KeyTransition::kPressed);
  assert(colemak_o.input_label_key == LayoutKey::kO);
  assert(colemak_o.hangul_token_key == LayoutKey::kOem1);
  assert(!colemak_o.hangul_input.is_mapped);
  assert(colemak_o.category == KeyCategory::kDelimiter);

  milkyway::engine::state::ModifierState ctrl_alt;
  ctrl_alt.ctrl = true;
  ctrl_alt.alt = true;
  const auto colemak_ctrl_o = milkyway::engine::key::AnalyzeKeyEvent(
      registry, "colemak", registry.DefaultKoreanLayout().id, Key('O'),
      ctrl_alt, KeyTransition::kPressed);
  assert(colemak_ctrl_o.shortcut_action ==
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

void TestUserSettingsResolver(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  using milkyway::engine::layout::BaseLayoutDefinition;
  using milkyway::engine::layout::BaseLayoutInterpretation;
  using milkyway::engine::layout::LayoutRegistry;

  milkyway::tsf::settings::RawUserSettings empty_raw;
  const auto defaults =
      milkyway::tsf::settings::ResolveUserSettings(empty_raw, registry);
  assert(defaults.base_layout_id == registry.DefaultBaseLayout().id);
  assert(defaults.korean_layout_id == registry.DefaultKoreanLayout().id);

  milkyway::tsf::settings::RawUserSettings valid_raw;
  valid_raw.base_layout_id = "colemak";
  valid_raw.korean_layout_id = "libhangul:3f";
  const auto valid =
      milkyway::tsf::settings::ResolveUserSettings(valid_raw, registry);
  assert(valid.base_layout_id == "colemak");
  assert(valid.korean_layout_id == "libhangul:3f");

  milkyway::tsf::settings::RawUserSettings invalid_raw;
  invalid_raw.base_layout_id = "missing_base";
  invalid_raw.korean_layout_id = "missing_korean";
  const auto fallback =
      milkyway::tsf::settings::ResolveUserSettings(invalid_raw, registry);
  assert(fallback.base_layout_id == registry.DefaultBaseLayout().id);
  assert(fallback.korean_layout_id == registry.DefaultKoreanLayout().id);

  LayoutRegistry runtime_registry;
  assert(runtime_registry.AddBaseLayout(BaseLayoutDefinition{
      {"graphite", "그래파이트", BaseLayoutInterpretation::kEffectiveBaseLayout},
      {}}));
  milkyway::tsf::settings::RawUserSettings custom_raw;
  custom_raw.base_layout_id = "graphite";
  custom_raw.korean_layout_id = "libhangul:2";
  const auto custom =
      milkyway::tsf::settings::ResolveUserSettings(custom_raw,
                                                   runtime_registry);
  assert(custom.base_layout_id == "graphite");
  assert(custom.korean_layout_id == "libhangul:2");
}

void TestBaseLayoutJsonLoader(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  using milkyway::engine::key::LayoutKey;
  using milkyway::engine::layout::LoadBaseLayoutDirectory;
  using milkyway::engine::layout::LoadBaseLayoutJson;

  const auto qwerty = LoadBaseLayoutJson(
      ReadTextFile("data/layouts/base/us_qwerty.json"), "us_qwerty.json");
  assert(qwerty.ok);
  assert(qwerty.definition.layout.id == "us_qwerty");
  assert(qwerty.definition.mappings.empty());

  const auto colemak = LoadBaseLayoutJson(
      ReadTextFile("data/layouts/base/colemak.json"), "colemak.json");
  assert(colemak.ok);
  assert(colemak.definition.layout.id == "colemak");
  assert(!colemak.definition.mappings.empty());

  auto assert_load_fails = [](std::string_view json) {
    const auto result =
        milkyway::engine::layout::LoadBaseLayoutJson(json, "inline");
    assert(!result.ok);
    assert(!result.error.empty());
  };

  assert_load_fails("{");
  assert_load_fails(R"json({"displayName":"Missing ID","keys":{}})json");
  assert_load_fails(R"json({"id":"missing_name","keys":{}})json");
  assert_load_fails(R"json({"id":"missing_keys","displayName":"Missing keys"})json");
  assert_load_fails(R"json({"id":"bad_value","displayName":"Bad","keys":{"s":123}})json");
  assert_load_fails(R"json({"id":"bad_key","displayName":"Bad","keys":{"!":"a"}})json");
  assert_load_fails(R"json({"id":"dup","displayName":"Duplicate","keys":{"a":"b"}})json");

  auto custom_definition = colemak.definition;
  custom_definition.layout.id = "colemak_custom";
  custom_definition.layout.display_name = "Custom Colemak";
  milkyway::engine::layout::LayoutRegistry custom_registry;
  assert(custom_registry.AddBaseLayout(std::move(custom_definition)));
  assert(custom_registry.FindBaseLayout("colemak_custom") != nullptr);
  assert(custom_registry.ResolveBaseLayoutLabelKey("colemak_custom",
                                                   LayoutKey::kS) ==
         LayoutKey::kR);
  assert(custom_registry.ResolveHangulTokenKey("colemak_custom",
                                               LayoutKey::kR) ==
         LayoutKey::kS);

  assert(custom_registry.AddBaseLayout(
      milkyway::engine::layout::BaseLayoutDefinition{
          {registry.DefaultBaseLayout().id, "Override",
           milkyway::engine::layout::BaseLayoutInterpretation::
               kEffectiveBaseLayout},
          {{LayoutKey::kA, LayoutKey::kB},
           {LayoutKey::kB, LayoutKey::kA}}}));
  const auto* overridden_default =
      custom_registry.FindBaseLayout(registry.DefaultBaseLayout().id);
  assert(overridden_default != nullptr);
  assert(overridden_default->display_name == "Override");
  assert(custom_registry.ResolveBaseLayoutLabelKey(
             registry.DefaultBaseLayout().id, LayoutKey::kA) ==
         LayoutKey::kB);
  assert(custom_registry.ResolveHangulTokenKey(registry.DefaultBaseLayout().id,
                                               LayoutKey::kB) ==
         LayoutKey::kA);

  const std::filesystem::path temp_directory =
      std::filesystem::temp_directory_path() /
      "milkyway_base_layout_loader_test";
  std::filesystem::remove_all(temp_directory);
  std::filesystem::create_directories(temp_directory);
  {
    std::ofstream(temp_directory / "00_bad.json") << "{";
    std::ofstream(temp_directory / "10_ok.json")
        << R"json({"id":"directory_ok","displayName":"Directory OK","keys":{}})json";
  }
  const auto directory_result = LoadBaseLayoutDirectory(temp_directory);
  assert(directory_result.errors.size() == 1);
  assert(directory_result.definitions.size() == 1);
  assert(directory_result.definitions.front().layout.id == "directory_ok");
  std::filesystem::remove_all(temp_directory);
}

void TestTextServiceLifecycle(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  milkyway::engine::session::InputSession session(
      registry.DefaultBaseLayout().id, registry.DefaultKoreanLayout().id);
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

void TestTextServiceCommitCandidate(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  milkyway::engine::session::InputSession session(
      registry.DefaultBaseLayout().id, registry.DefaultKoreanLayout().id);
  RecordingEditSink sink;
  milkyway::tsf::service::TextService service(
      &session, milkyway::adapters::libhangul::CreateLibhangulComposer(),
      &sink, &registry);

  auto result =
      service.OnKeyEvent(Key('G'), {}, milkyway::engine::key::KeyTransition::kPressed);
  assert(result.preedit_text == "\xE3\x85\x8E");
  result =
      service.OnKeyEvent(Key('K'), {}, milkyway::engine::key::KeyTransition::kPressed);
  assert(result.preedit_text == "\xED\x95\x98");
  result =
      service.OnKeyEvent(Key('S'), {}, milkyway::engine::key::KeyTransition::kPressed);
  assert(result.preedit_text == "\xED\x95\x9C");

  const bool committed = service.CommitCandidate("\xE9\x9F\x93");
  assert(committed);
  assert(!session.IsComposing());
  assert(session.last_end_reason() ==
         milkyway::engine::session::CompositionEndReason::kCandidateSelected);
  assert(sink.operations.size() == 5);
  AssertOperation(sink.operations[3], TextEditOperationType::kCommitText,
                  "\xE9\x9F\x93");
  AssertOperation(sink.operations[4], TextEditOperationType::kEndComposition, "");
}

void TestTextServiceBaseLayoutPrintableDelimiter(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  {
    milkyway::engine::session::InputSession session(
        "colemak", registry.DefaultKoreanLayout().id);
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
        "colemak", registry.DefaultKoreanLayout().id);
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
        "colemak", registry.DefaultKoreanLayout().id);
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
      registry.DefaultBaseLayout().id, registry.DefaultKoreanLayout().id);
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
      registry.DefaultBaseLayout().id, registry.DefaultKoreanLayout().id);
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
        registry.DefaultBaseLayout().id, registry.DefaultKoreanLayout().id);
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
        registry.DefaultBaseLayout().id, registry.DefaultKoreanLayout().id);
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
            registry.DefaultBaseLayout().id,
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
        registry.DefaultBaseLayout().id, registry.DefaultKoreanLayout().id);
    RecordingEditSink sink;
    milkyway::tsf::service::TextService service(
        &session, milkyway::adapters::libhangul::CreateLibhangulComposer(),
        &sink, &registry);

    service.OnKeyEvent(Key('G'), {}, milkyway::engine::key::KeyTransition::kPressed);
    milkyway::engine::state::ModifierState modifiers;
    modifiers.ctrl = true;
    modifiers.shift = true;
    assert(!service.WouldEatKey(Key(kVkSpace), modifiers,
                                milkyway::engine::key::KeyTransition::kPressed));
    const auto result =
        service.OnKeyEvent(Key(kVkSpace), modifiers,
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
    assert(sink.operations.size() == 2);
    AssertOperation(sink.operations[1], TextEditOperationType::kEndComposition,
                    "");
  }

  {
    milkyway::engine::session::InputSession session(
        registry.DefaultBaseLayout().id, registry.DefaultKoreanLayout().id);
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
        registry.DefaultBaseLayout().id, registry.DefaultKoreanLayout().id);
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
        registry.DefaultBaseLayout().id, registry.DefaultKoreanLayout().id);
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
        registry.DefaultBaseLayout().id, registry.DefaultKoreanLayout().id);
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
        registry.DefaultBaseLayout().id, registry.DefaultKoreanLayout().id);
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

void TestTextServiceReplaceComposerForKoreanLayout(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  milkyway::engine::session::InputSession session(
      registry.DefaultBaseLayout().id, registry.DefaultKoreanLayout().id);
  RecordingEditSink sink;
  milkyway::tsf::service::TextService service(
      &session, milkyway::adapters::libhangul::CreateLibhangulComposer(),
      &sink, &registry);

  assert(service.ReplaceComposer(
      milkyway::adapters::libhangul::CreateLibhangulComposer("3f")));
  session.SetLayouts(registry.DefaultBaseLayout().id, "libhangul:3f");

  const auto result =
      service.OnKeyEvent(Key('F'), {}, milkyway::engine::key::KeyTransition::kPressed);
  assert(result.category ==
         milkyway::tsf::service::KeyEventCategory::kHangulAscii);
  assert(result.eaten);
  assert(result.preedit_text == "\xE3\x85\x8F");
  assert(session.IsComposing());
  assert(session.snapshot().preedit == "\xE3\x85\x8F");
}

void TestTextServiceShinsebeolLayout(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  {
    milkyway::engine::session::InputSession session(
        registry.DefaultBaseLayout().id, "libhangul:3sin-1995");
    RecordingEditSink sink;
    milkyway::tsf::service::TextService service(
        &session,
        milkyway::adapters::libhangul::CreateLibhangulComposer("3sin-1995"),
        &sink, &registry);

    auto result = service.OnKeyEvent(
        Key('U'), {}, milkyway::engine::key::KeyTransition::kPressed);
    assert(result.category ==
           milkyway::tsf::service::KeyEventCategory::kHangulAscii);
    assert(result.eaten);
    assert(result.preedit_text == "\xE3\x84\xB7");

    result = service.OnKeyEvent(
        Key('Q'), {}, milkyway::engine::key::KeyTransition::kPressed);
    assert(result.category ==
           milkyway::tsf::service::KeyEventCategory::kHangulAscii);
    assert(result.eaten);
    assert(result.preedit_text == "\xEB\x93\xB8");
    assert(session.snapshot().preedit == "\xEB\x93\xB8");
  }

  {
    milkyway::engine::session::InputSession session(
        registry.DefaultBaseLayout().id, "libhangul:3sin-p2");
    RecordingEditSink sink;
    milkyway::tsf::service::TextService service(
        &session,
        milkyway::adapters::libhangul::CreateLibhangulComposer("3sin-p2"),
        &sink, &registry);

    auto result = service.OnKeyEvent(
        Key('U'), {}, milkyway::engine::key::KeyTransition::kPressed);
    assert(result.category ==
           milkyway::tsf::service::KeyEventCategory::kHangulAscii);
    assert(result.eaten);
    assert(result.preedit_text == "\xE3\x84\xB7");

    result = service.OnKeyEvent(
        Key('Z'), {}, milkyway::engine::key::KeyTransition::kPressed);
    assert(result.category ==
           milkyway::tsf::service::KeyEventCategory::kHangulAscii);
    assert(result.eaten);
    assert(result.preedit_text == "\xEB\x93\xB8");
    assert(session.snapshot().preedit == "\xEB\x93\xB8");
  }
}

void TestTextServiceShiftFinalSsangSios(
    const milkyway::engine::layout::LayoutRegistry& registry) {
  milkyway::engine::session::InputSession session(
      registry.DefaultBaseLayout().id, registry.DefaultKoreanLayout().id);
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
        registry.DefaultBaseLayout().id, registry.DefaultKoreanLayout().id);
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
        registry.DefaultBaseLayout().id, registry.DefaultKoreanLayout().id);
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

void TestTransitoryDirectTextCompositionGateAndPlan() {
  using milkyway::tsf::edit::BuildTransitoryDirectTextOperationPlan;
  using milkyway::tsf::edit::CanUseRichEditRangeReplacementForTransitoryDirectText;
  using milkyway::tsf::edit::CanUseWin32SelectionReplacementForTransitoryDirectText;
  using milkyway::tsf::edit::ShouldUseTransitoryDirectTextComposition;
  using milkyway::tsf::edit::ShouldAppendTransitoryRepeatCommit;
  using milkyway::tsf::edit::TransitoryDirectTextTarget;

  const TransitoryDirectTextTarget rider_target{
      L"rider64.exe", L"SunAwtDialog", true};
  const TransitoryDirectTextTarget other_popup{
      L"rider64.exe", L"SunAwtFrame", true};
  const TransitoryDirectTextTarget rider_not_transitory{
      L"rider64.exe", L"SunAwtDialog", false};
  const TransitoryDirectTextTarget game_popup{
      L"game.exe", L"GameWindowClass", true};
  const TransitoryDirectTextTarget perforce_popup{
      L"p4v.exe", L"Qt5152QWindowIcon", true};
  const TransitoryDirectTextTarget edit_popup{
      L"host.exe", L"Edit", true};
  const TransitoryDirectTextTarget rich_edit_target{
      L"host.exe", L"RICHEDIT50W", true};

  const std::vector<TextEditOperation> commit_only = {
      {TextEditOperationType::kCommitText, "a"}};
  assert(!ShouldUseTransitoryDirectTextComposition(rider_target, commit_only,
                                                   false));
  assert(ShouldUseTransitoryDirectTextComposition(rider_target, commit_only,
                                                  true));
  assert(ShouldUseTransitoryDirectTextComposition(other_popup, commit_only,
                                                  true));
  assert(!ShouldUseTransitoryDirectTextComposition(rider_not_transitory,
                                                   commit_only, true));
  assert(ShouldUseTransitoryDirectTextComposition(game_popup, commit_only,
                                                  true));
  assert(ShouldUseTransitoryDirectTextComposition(perforce_popup, commit_only,
                                                  true));
  assert(ShouldUseTransitoryDirectTextComposition(edit_popup, commit_only,
                                                  true));
  assert(ShouldUseTransitoryDirectTextComposition(rich_edit_target,
                                                  commit_only, true));
  assert(CanUseWin32SelectionReplacementForTransitoryDirectText(edit_popup));
  assert(!CanUseWin32SelectionReplacementForTransitoryDirectText(
      rich_edit_target));
  assert(!CanUseWin32SelectionReplacementForTransitoryDirectText(
      rider_target));
  assert(CanUseRichEditRangeReplacementForTransitoryDirectText(
      rich_edit_target));
  assert(!CanUseRichEditRangeReplacementForTransitoryDirectText(edit_popup));
  assert(!CanUseRichEditRangeReplacementForTransitoryDirectText(
      rider_target));

  constexpr const char* kPieup = "\xE3\x85\x8D";
  constexpr const char* kPo = "\xED\x8F\xAC";
  constexpr const char* kPon = "\xED\x8F\xB0";
  constexpr const char* kTieut = "\xE3\x85\x8C";
  constexpr const char* kTeu = "\xED\x8A\xB8";

  const std::vector<TextEditOperation> start_preedit = {
      {TextEditOperationType::kStartComposition, kPieup}};
  assert(ShouldUseTransitoryDirectTextComposition(rider_target, start_preedit,
                                                  false));
  assert(ShouldUseTransitoryDirectTextComposition(game_popup, start_preedit,
                                                  false));
  assert(ShouldUseTransitoryDirectTextComposition(perforce_popup,
                                                  start_preedit, false));
  assert(!ShouldUseTransitoryDirectTextComposition(rider_not_transitory,
                                                   start_preedit, false));
  assert(ShouldUseTransitoryDirectTextComposition(edit_popup, start_preedit,
                                                  false));
  auto start_plan = BuildTransitoryDirectTextOperationPlan(start_preedit);
  assert(start_plan.commit_text.empty());
  assert(start_plan.preedit_text == kPieup);
  assert(start_plan.has_preedit);
  assert(start_plan.has_composition_operation);

  const std::vector<TextEditOperation> update_po = {
      {TextEditOperationType::kUpdateComposition, kPo}};
  auto update_po_plan = BuildTransitoryDirectTextOperationPlan(update_po);
  assert(update_po_plan.commit_text.empty());
  assert(update_po_plan.preedit_text == kPo);
  assert(update_po_plan.has_preedit);

  const std::vector<TextEditOperation> update_pon = {
      {TextEditOperationType::kUpdateComposition, kPon}};
  auto update_pon_plan = BuildTransitoryDirectTextOperationPlan(update_pon);
  assert(update_pon_plan.commit_text.empty());
  assert(update_pon_plan.preedit_text == kPon);
  assert(update_pon_plan.has_preedit);

  const std::vector<TextEditOperation> commit_and_next_preedit = {
      {TextEditOperationType::kCommitText, kPon},
      {TextEditOperationType::kUpdateComposition, kTieut}};
  auto commit_and_next_preedit_plan =
      BuildTransitoryDirectTextOperationPlan(commit_and_next_preedit);
  assert(commit_and_next_preedit_plan.commit_text == kPon);
  assert(commit_and_next_preedit_plan.preedit_text == kTieut);
  assert(commit_and_next_preedit_plan.has_preedit);

  assert(ShouldAppendTransitoryRepeatCommit(L"\u314B", L"\u314B", true,
                                            L"\u314B"));
  assert(!ShouldAppendTransitoryRepeatCommit(L"\u314B", L"\u314B", true,
                                             L"\u314C"));
  assert(!ShouldAppendTransitoryRepeatCommit(L"\uD3F0", L"\uD3F0", true,
                                             L"\u314C"));
  assert(!ShouldAppendTransitoryRepeatCommit(L"\u314B", L"\u314B", false,
                                             L"\u314B"));

  const std::vector<TextEditOperation> update_teu = {
      {TextEditOperationType::kUpdateComposition, kTeu}};
  auto update_teu_plan = BuildTransitoryDirectTextOperationPlan(update_teu);
  assert(update_teu_plan.commit_text.empty());
  assert(update_teu_plan.preedit_text == kTeu);
  assert(update_teu_plan.has_preedit);

  const std::vector<TextEditOperation> end_only = {
      {TextEditOperationType::kEndComposition, {}}};
  assert(ShouldUseTransitoryDirectTextComposition(rider_target, end_only,
                                                  true));
  assert(ShouldUseTransitoryDirectTextComposition(edit_popup, end_only, true));
  auto end_plan = BuildTransitoryDirectTextOperationPlan(end_only);
  assert(end_plan.commit_text.empty());
  assert(end_plan.preedit_text.empty());
  assert(!end_plan.has_preedit);
  assert(end_plan.end_requested);
}

void TestTransitoryCompositionBridgeGateAndState() {
  using milkyway::tsf::edit::GetTransitoryCompositionBridgeTargetKind;
  using milkyway::tsf::edit::ShouldSuppressTransitoryCompositionEngineReset;
  using milkyway::tsf::edit::TransitoryCompositionBridge;
  using milkyway::tsf::edit::TransitoryCompositionBridgeSnapshot;
  using milkyway::tsf::edit::TransitoryCompositionBridgeTarget;
  using milkyway::tsf::edit::TransitoryCompositionBridgeTargetKind;

  const TransitoryCompositionBridgeTarget rider_target{
      L"rider64.exe", L"SunAwtDialog", true};
  const TransitoryCompositionBridgeTarget other_popup{
      L"rider64.exe", L"SunAwtFrame", true};
  const TransitoryCompositionBridgeTarget rider_not_transitory{
      L"rider64.exe", L"SunAwtDialog", false};
  const TransitoryCompositionBridgeTarget game_popup{
      L"game.exe", L"GameWindowClass", true};
  const TransitoryCompositionBridgeTarget perforce_popup{
      L"p4v.exe", L"Qt5152QWindowIcon", true};

  assert(GetTransitoryCompositionBridgeTargetKind(rider_target) ==
         TransitoryCompositionBridgeTargetKind::kSuppressEngineReset);
  assert(GetTransitoryCompositionBridgeTargetKind(other_popup) ==
         TransitoryCompositionBridgeTargetKind::kSuppressEngineReset);
  assert(GetTransitoryCompositionBridgeTargetKind(rider_not_transitory) ==
         TransitoryCompositionBridgeTargetKind::kNone);
  assert(GetTransitoryCompositionBridgeTargetKind(game_popup) ==
         TransitoryCompositionBridgeTargetKind::kSuppressEngineReset);
  assert(GetTransitoryCompositionBridgeTargetKind(perforce_popup) ==
         TransitoryCompositionBridgeTargetKind::kSuppressEngineReset);

  TransitoryCompositionBridgeSnapshot snapshot;
  snapshot.target = rider_target;
  snapshot.context = reinterpret_cast<ITfContext*>(0x1234);
  snapshot.view_hwnd = reinterpret_cast<HWND>(0x5678);
  snapshot.internal_composing = true;
  snapshot.has_tracked_tsf_composition = true;
  snapshot.preedit = L"포";

  assert(ShouldSuppressTransitoryCompositionEngineReset(snapshot));

  TransitoryCompositionBridgeSnapshot not_composing = snapshot;
  not_composing.internal_composing = false;
  assert(!ShouldSuppressTransitoryCompositionEngineReset(not_composing));

  TransitoryCompositionBridgeSnapshot no_tracked_composition = snapshot;
  no_tracked_composition.has_tracked_tsf_composition = false;
  assert(!ShouldSuppressTransitoryCompositionEngineReset(
      no_tracked_composition));

  TransitoryCompositionBridgeSnapshot not_transitory = snapshot;
  not_transitory.target = rider_not_transitory;
  assert(!ShouldSuppressTransitoryCompositionEngineReset(not_transitory));

  TransitoryCompositionBridgeSnapshot game_snapshot = snapshot;
  game_snapshot.target = game_popup;
  assert(ShouldSuppressTransitoryCompositionEngineReset(game_snapshot));

  TransitoryCompositionBridge bridge;
  assert(!bridge.IsActive());
  assert(bridge.suppressed_termination_count() == 0);
  assert(bridge.ShouldSuppressEngineReset(snapshot));
  assert(bridge.ShouldSuppressEngineReset(game_snapshot));
  assert(!bridge.ShouldObserveTermination(game_snapshot));

  bridge.NoteSuppressedEngineReset(
      snapshot, reinterpret_cast<ITfComposition*>(0x9999));
  assert(bridge.IsActive());
  assert(bridge.suppressed_termination_count() == 1);

  bridge.Reset(L"unit_test");
  assert(!bridge.IsActive());
  assert(bridge.suppressed_termination_count() == 0);
}

void TestCandidateThemeSelection() {
  using milkyway::ui::candidate::CandidateThemeMode;
  using milkyway::ui::candidate::ChooseCandidateThemeMode;
  using milkyway::ui::candidate::ThemeModeFromLightThemeRegistryValue;

  assert(ThemeModeFromLightThemeRegistryValue(0).value() ==
         CandidateThemeMode::kDark);
  assert(ThemeModeFromLightThemeRegistryValue(1).value() ==
         CandidateThemeMode::kLight);
  assert(!ThemeModeFromLightThemeRegistryValue(2).has_value());

  assert(ChooseCandidateThemeMode(0, 1) == CandidateThemeMode::kDark);
  assert(ChooseCandidateThemeMode(1, 0) == CandidateThemeMode::kLight);
  assert(ChooseCandidateThemeMode(std::nullopt, 0) ==
         CandidateThemeMode::kDark);
  assert(ChooseCandidateThemeMode(std::nullopt, std::nullopt) ==
         CandidateThemeMode::kLight);
}

void TestCandidateWindowPlacement() {
  using milkyway::ui::candidate::CalculateCandidateWindowOrigin;

  const RECT work_area = {100, 100, 500, 500};
  const SIZE window_size = {120, 90};

  POINT origin =
      CalculateCandidateWindowOrigin(work_area, window_size, POINT{180, 220});
  assert(origin.x == 180);
  assert(origin.y == 220);

  origin =
      CalculateCandidateWindowOrigin(work_area, window_size, POINT{180, 460});
  assert(origin.x == 180);
  assert(origin.y == 370);

  origin =
      CalculateCandidateWindowOrigin(work_area, window_size, POINT{40, 220});
  assert(origin.x == 100);
  assert(origin.y == 220);

  origin =
      CalculateCandidateWindowOrigin(work_area, window_size, POINT{450, 220});
  assert(origin.x == 380);
  assert(origin.y == 220);
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

  assert(registry.DefaultBaseLayout().id == "us_qwerty");
  assert(registry.DefaultKoreanLayout().id == "libhangul:2");

  TestInputSession();
  TestHanjaCandidateRequestValidation();
  TestSelectionHanjaPrefixRequests();
  TestLibhangulHanjaDictionary();
  TestLibhangulComposer();
  TestLibhangulComposerAutoReorder();
  TestLibhangulComposerShiftFinalSsangSios();
  TestLibhangulComposerShinsebeol();
  TestShortcutResolver(registry);
  TestLayoutRegistryEffectiveBaseLayout(registry);
  TestBuiltInKoreanLayouts(registry);
  TestBuiltInBaseLayouts(registry);
  TestUserSettingsResolver(registry);
  TestBaseLayoutJsonLoader(registry);
  TestTextServiceLifecycle(registry);
  TestTextServiceCommitCandidate(registry);
  TestTextServiceBaseLayoutPrintableDelimiter(registry);
  TestTextServiceBackspaceClearsVisibleComposition(registry);
  TestTextServiceAutoReorder(registry);
  TestTextServicePrepareImeModeToggle(registry);
  TestTextServiceShortcutAndTermination(registry);
  TestTextServiceReplaceComposerForKoreanLayout(registry);
  TestTextServiceShinsebeolLayout(registry);
  TestTextServiceShiftFinalSsangSios(registry);
  TestTextServiceShiftDoubleVowels(registry);
  TestTextEditPlanPreservesReorderedSyllableCommit();
  TestTextEditPlanCompletesCommittedCompositionText();
  TestTextEditPlanPreservesEmptyCompositionUpdateBeforeCompletion();
  TestTransitoryDirectTextCompositionGateAndPlan();
  TestTransitoryCompositionBridgeGateAndState();
  TestCandidateThemeSelection();
  TestCandidateWindowPlacement();

  return 0;
}
