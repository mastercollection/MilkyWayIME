#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <string>

namespace milkyway::ui::composition {

POINT CalculateCompositionWindowOrigin(const RECT& work_area, SIZE window_size,
                                       POINT anchor);
POINT CalculateCaretRightCompositionAnchor(const RECT& caret_rect,
                                           int horizontal_offset = 6);

class CompositionWindow final {
 public:
  explicit CompositionWindow(HINSTANCE instance);
  ~CompositionWindow();

  CompositionWindow(const CompositionWindow&) = delete;
  CompositionWindow& operator=(const CompositionWindow&) = delete;

  bool Show(std::wstring text, POINT anchor, HWND owner_hwnd);
  void Hide();
  bool IsShown() const;

  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam,
                                     LPARAM lparam);

 private:
  LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  bool EnsureWindow(HWND owner_hwnd);
  void DestroyWindowHandle();
  SIZE MeasureWindowSize() const;
  void Paint();

  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  HWND owner_hwnd_ = nullptr;
  HWND fallback_from_owner_hwnd_ = nullptr;
  HFONT font_ = nullptr;
  std::wstring text_;
};

}  // namespace milkyway::ui::composition

#endif
