#pragma once

namespace milkyway::tsf::service {

enum class CompositionSurfaceKind {
  kNone,
  kTsfTracked,
  kHostTextBacked,
  kOverlayOnly,
};

enum class CompositionExitIntent {
  kAcceptAndForwardKey,
  kFocusTransfer,
  kSelectionMovedOutsideComposition,
  kExternalTermination,
  kHostSelectionBackspaceBypass,
};

enum class CompositionSurfaceExitAction {
  kNone,
  kEndOnly,
  kCommitPendingPreeditAndEnd,
  kBackendFinalizeOnly,
  kResetOnly,
};

inline CompositionSurfaceExitAction DecideCompositionSurfaceExitAction(
    CompositionSurfaceKind surface, CompositionExitIntent intent) {
  if (surface == CompositionSurfaceKind::kNone) {
    return CompositionSurfaceExitAction::kNone;
  }

  switch (intent) {
    case CompositionExitIntent::kAcceptAndForwardKey:
    case CompositionExitIntent::kFocusTransfer:
      switch (surface) {
        case CompositionSurfaceKind::kOverlayOnly:
          return CompositionSurfaceExitAction::kCommitPendingPreeditAndEnd;
        case CompositionSurfaceKind::kTsfTracked:
          return CompositionSurfaceExitAction::kEndOnly;
        case CompositionSurfaceKind::kHostTextBacked:
          return CompositionSurfaceExitAction::kBackendFinalizeOnly;
        case CompositionSurfaceKind::kNone:
          return CompositionSurfaceExitAction::kNone;
      }
      break;
    case CompositionExitIntent::kSelectionMovedOutsideComposition:
      return surface == CompositionSurfaceKind::kTsfTracked
                 ? CompositionSurfaceExitAction::kEndOnly
                 : CompositionSurfaceExitAction::kNone;
    case CompositionExitIntent::kExternalTermination:
      return CompositionSurfaceExitAction::kNone;
    case CompositionExitIntent::kHostSelectionBackspaceBypass:
      return CompositionSurfaceExitAction::kResetOnly;
  }

  return CompositionSurfaceExitAction::kNone;
}

}  // namespace milkyway::tsf::service
