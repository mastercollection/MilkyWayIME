#include "tsf/service/text_service.h"

namespace milkyway::tsf::service {

TextService::TextService(engine::session::InputSession* session)
    : session_(session) {}

void TextService::OnKeyEvent(const engine::key::PhysicalKey& key,
                             const engine::state::ModifierState& modifiers,
                             engine::key::KeyTransition transition) {
  (void)key;
  (void)modifiers;
  (void)transition;
  (void)session_;
}

}  // namespace milkyway::tsf::service
