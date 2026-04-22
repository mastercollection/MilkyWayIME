#include <cassert>

#include "adapters/libhangul/hangul_composer.h"
#include "engine/key/physical_key.h"
#include "engine/layout/layout_registry.h"
#include "engine/session/input_session.h"
#include "engine/shortcut/shortcut_resolver.h"
#include "engine/state/modifier_state.h"

int main() {
  milkyway::engine::layout::LayoutRegistry registry;

  assert(registry.DefaultPhysicalLayout().id == "us_qwerty");
  assert(registry.DefaultKoreanLayout().id == "ko_dubeolsik");
  assert(registry.DefaultKoreanLayout().base_physical_layout == "us_qwerty");

  milkyway::engine::session::InputSession session(
      registry.DefaultPhysicalLayout().id, registry.DefaultKoreanLayout().id);

  assert(!session.IsComposing());
  assert(!session.RequestHanjaConversion().has_value());

  session.StartComposition();
  session.UpdateComposition("gksrmf");

  assert(session.IsComposing());
  assert(session.snapshot().preedit == "gksrmf");

  const auto hanja_request = session.RequestHanjaConversion();
  assert(hanja_request.has_value());
  assert(hanja_request->composing_text == "gksrmf");

  session.EndComposition();

  assert(!session.IsComposing());
  assert(session.snapshot().preedit.empty());
  assert(!session.RequestHanjaConversion().has_value());

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

  auto composer = milkyway::adapters::libhangul::CreateLibhangulComposer();
  assert(composer != nullptr);
  assert(composer->ProcessKeySequence("gksrmf") ==
         "\xED\x95\x9C\xEA\xB8\x80");

  return 0;
}
