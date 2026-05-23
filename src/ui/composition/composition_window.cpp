#include "ui/composition/composition_window.h"

#if defined(_WIN32)

#include <algorithm>
#include <cstdint>
#include <utility>

#if defined(_DEBUG)
#include "tsf/debug/debug_log.h"
#endif

namespace milkyway::ui::composition {
namespace {

constexpr wchar_t kCompositionWindowClassName[] =
    L"MilkyWayIMECompositionWindow";
constexpr int kHorizontalPadding = 10;
constexpr int kVerticalPadding = 5;
constexpr int kMinimumWidth = 36;
constexpr int kMinimumHeight = 28;

ATOM RegisterCompositionWindowClass(HINSTANCE instance) {
  static ATOM atom = 0;
  if (atom != 0) {
    return atom;
  }

  WNDCLASSEXW window_class = {};
  window_class.cbSize = sizeof(window_class);
  window_class.style = CS_HREDRAW | CS_VREDRAW;
  window_class.lpfnWndProc = CompositionWindow::WindowProc;
  window_class.cbWndExtra = sizeof(LONG_PTR);
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.lpszClassName = kCompositionWindowClassName;
  atom = RegisterClassExW(&window_class);
  return atom;
}

HFONT CreateCompositionFont() {
  LOGFONTW log_font = {};
  log_font.lfHeight = -18;
  log_font.lfWeight = FW_NORMAL;
  wcscpy_s(log_font.lfFaceName, L"Segoe UI");
  return CreateFontIndirectW(&log_font);
}

void FillRectColor(HDC dc, const RECT& rect, COLORREF color) {
  HBRUSH brush = CreateSolidBrush(color);
  FillRect(dc, &rect, brush);
  DeleteObject(brush);
}

void FrameRectColor(HDC dc, const RECT& rect, COLORREF color) {
  HBRUSH brush = CreateSolidBrush(color);
  FrameRect(dc, &rect, brush);
  DeleteObject(brush);
}

#if defined(_DEBUG)
std::wstring PointerToString(const void* pointer) {
  wchar_t buffer[32] = {};
  swprintf_s(buffer, L"%p", pointer);
  return buffer;
}

std::wstring RectToString(const RECT& rect) {
  return L"(" + std::to_wstring(rect.left) + L"," +
         std::to_wstring(rect.top) + L"," + std::to_wstring(rect.right) +
         L"," + std::to_wstring(rect.bottom) + L")";
}
#endif

}  // namespace

POINT CalculateCompositionWindowOrigin(const RECT& work_area, SIZE window_size,
                                       POINT anchor) {
  const int left = static_cast<int>(work_area.left);
  const int top = static_cast<int>(work_area.top);
  const int right = static_cast<int>(work_area.right);
  const int bottom = static_cast<int>(work_area.bottom);
  const int width = static_cast<int>(window_size.cx);
  const int height = static_cast<int>(window_size.cy);

  int x = static_cast<int>(anchor.x);
  if (width >= right - left) {
    x = left;
  } else if (x < left) {
    x = left;
  } else if (x + width > right) {
    x = right - width;
  }

  int y = static_cast<int>(anchor.y);
  if (y + height > bottom) {
    y = static_cast<int>(anchor.y) - height;
  }

  if (height >= bottom - top) {
    y = top;
  } else if (y < top) {
    y = top;
  } else if (y + height > bottom) {
    y = bottom - height;
  }

  return POINT{x, y};
}

POINT CalculateCaretRightCompositionAnchor(const RECT& caret_rect,
                                           int horizontal_offset) {
  return POINT{caret_rect.right + horizontal_offset, caret_rect.top};
}

CompositionWindow::CompositionWindow(HINSTANCE instance) : instance_(instance) {}

CompositionWindow::~CompositionWindow() {
  DestroyWindowHandle();
  if (font_ != nullptr) {
    DeleteObject(font_);
    font_ = nullptr;
  }
}

bool CompositionWindow::Show(std::wstring text, POINT anchor, HWND owner_hwnd) {
  if (text.empty()) {
    Hide();
    return true;
  }
  text_ = std::move(text);
  if (!EnsureWindow(owner_hwnd)) {
    return false;
  }

  const SIZE size = MeasureWindowSize();
  RECT work_area = {};
  HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
  MONITORINFO monitor_info = {};
  monitor_info.cbSize = sizeof(monitor_info);
  if (monitor != nullptr && GetMonitorInfoW(monitor, &monitor_info)) {
    work_area = monitor_info.rcWork;
  } else {
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
  }

  const POINT origin =
      CalculateCompositionWindowOrigin(work_area, size, anchor);

  const BOOL set_pos_ok =
      SetWindowPos(hwnd_, HWND_TOPMOST, origin.x, origin.y, size.cx, size.cy,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
  const BOOL show_ok = ShowWindow(hwnd_, SW_SHOWNA);
  InvalidateRect(hwnd_, nullptr, TRUE);
  const BOOL update_ok = UpdateWindow(hwnd_);
#if defined(_DEBUG)
  RECT rect = {};
  GetWindowRect(hwnd_, &rect);
  ::milkyway::tsf::debug::DebugLog(
      L"[MilkyWayIME][CompositionWindow][Show] hwnd=" +
      PointerToString(hwnd_) + L" owner=" + PointerToString(owner_hwnd_) +
      L" anchor=(" + std::to_wstring(anchor.x) + L"," +
      std::to_wstring(anchor.y) + L") origin=(" +
      std::to_wstring(origin.x) + L"," + std::to_wstring(origin.y) +
      L") size=(" + std::to_wstring(size.cx) + L"," +
      std::to_wstring(size.cy) + L") set_pos=" +
      std::to_wstring(set_pos_ok ? 1 : 0) + L" show_prev=" +
      std::to_wstring(show_ok ? 1 : 0) + L" update=" +
      std::to_wstring(update_ok ? 1 : 0) + L" visible=" +
      std::to_wstring(IsWindowVisible(hwnd_) ? 1 : 0) + L" rect=" +
      RectToString(rect) + L" text_len=" + std::to_wstring(text_.size()));
#endif
  return true;
}

void CompositionWindow::Hide() {
  text_.clear();
  if (hwnd_ != nullptr) {
    ShowWindow(hwnd_, SW_HIDE);
  }
}

bool CompositionWindow::IsShown() const {
  return hwnd_ != nullptr && IsWindowVisible(hwnd_);
}

LRESULT CALLBACK CompositionWindow::WindowProc(HWND hwnd, UINT message,
                                               WPARAM wparam, LPARAM lparam) {
  CompositionWindow* window = reinterpret_cast<CompositionWindow*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(lparam);
    window = static_cast<CompositionWindow*>(create_struct->lpCreateParams);
    window->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(window));
  }
  if (window != nullptr) {
    return window->HandleMessage(hwnd, message, wparam, lparam);
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT CompositionWindow::HandleMessage(HWND hwnd, UINT message, WPARAM wparam,
                                         LPARAM lparam) {
  switch (message) {
    case WM_PAINT:
#if defined(_DEBUG)
      ::milkyway::tsf::debug::DebugLog(
          L"[MilkyWayIME][CompositionWindow][Paint] hwnd=" +
          PointerToString(hwnd_) + L" text_len=" +
          std::to_wstring(text_.size()));
#endif
      Paint();
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_NCHITTEST:
      return HTTRANSPARENT;
    default:
      return DefWindowProcW(hwnd, message, wparam, lparam);
  }
}

bool CompositionWindow::EnsureWindow(HWND owner_hwnd) {
  if (owner_hwnd != nullptr && !IsWindow(owner_hwnd)) {
    owner_hwnd = nullptr;
  }
  if (hwnd_ != nullptr &&
      (owner_hwnd_ == owner_hwnd ||
       (owner_hwnd_ == nullptr && fallback_from_owner_hwnd_ == owner_hwnd))) {
    return true;
  }
  if (hwnd_ != nullptr) {
    DestroyWindowHandle();
  }
  if (instance_ == nullptr || RegisterCompositionWindowClass(instance_) == 0) {
    return false;
  }
  if (font_ == nullptr) {
    font_ = CreateCompositionFont();
  }
  const HWND requested_owner_hwnd = owner_hwnd;
  SetLastError(ERROR_SUCCESS);
  hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                          kCompositionWindowClassName, L"", WS_POPUP, 0, 0, 0,
                          0, requested_owner_hwnd, nullptr, instance_, this);
  const DWORD owner_create_error = GetLastError();
#if defined(_DEBUG)
  ::milkyway::tsf::debug::DebugLog(
      L"[MilkyWayIME][CompositionWindow][Create] hwnd=" +
      PointerToString(hwnd_) + L" owner=" +
      PointerToString(requested_owner_hwnd) + L" error=" +
      std::to_wstring(owner_create_error));
#endif
  if (hwnd_ != nullptr) {
    owner_hwnd_ = requested_owner_hwnd;
    fallback_from_owner_hwnd_ = nullptr;
    return true;
  }

  if (requested_owner_hwnd == nullptr) {
    owner_hwnd_ = nullptr;
    fallback_from_owner_hwnd_ = nullptr;
    return false;
  }

  SetLastError(ERROR_SUCCESS);
  hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                          kCompositionWindowClassName, L"", WS_POPUP, 0, 0, 0,
                          0, nullptr, nullptr, instance_, this);
  const DWORD fallback_create_error = GetLastError();
#if defined(_DEBUG)
  ::milkyway::tsf::debug::DebugLog(
      L"[MilkyWayIME][CompositionWindow][CreateFallback] hwnd=" +
      PointerToString(hwnd_) + L" requested_owner=" +
      PointerToString(requested_owner_hwnd) + L" owner_error=" +
      std::to_wstring(owner_create_error) + L" fallback_error=" +
      std::to_wstring(fallback_create_error) + L" owner_fallback=" +
      std::to_wstring(hwnd_ != nullptr ? 1 : 0));
#endif
  if (hwnd_ == nullptr) {
    owner_hwnd_ = nullptr;
    fallback_from_owner_hwnd_ = nullptr;
    return false;
  }

  owner_hwnd_ = nullptr;
  fallback_from_owner_hwnd_ = requested_owner_hwnd;
  return true;
}

void CompositionWindow::DestroyWindowHandle() {
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  owner_hwnd_ = nullptr;
  fallback_from_owner_hwnd_ = nullptr;
}

SIZE CompositionWindow::MeasureWindowSize() const {
  SIZE text_size = {};
  HDC dc = GetDC(hwnd_);
  HGDIOBJ old_font = nullptr;
  if (font_ != nullptr) {
    old_font = SelectObject(dc, font_);
  }
  if (!text_.empty()) {
    GetTextExtentPoint32W(dc, text_.c_str(), static_cast<int>(text_.size()),
                          &text_size);
  }
  if (old_font != nullptr) {
    SelectObject(dc, old_font);
  }
  ReleaseDC(hwnd_, dc);

  return SIZE{std::max(kMinimumWidth,
                       static_cast<int>(text_size.cx) + kHorizontalPadding * 2),
              std::max(kMinimumHeight,
                       static_cast<int>(text_size.cy) + kVerticalPadding * 2)};
}

void CompositionWindow::Paint() {
  PAINTSTRUCT paint = {};
  HDC dc = BeginPaint(hwnd_, &paint);
  RECT rect = {};
  GetClientRect(hwnd_, &rect);

  FillRectColor(dc, rect, RGB(32, 32, 32));
  FrameRectColor(dc, rect, RGB(88, 88, 88));

  HGDIOBJ old_font = nullptr;
  if (font_ != nullptr) {
    old_font = SelectObject(dc, font_);
  }
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(255, 255, 255));
  RECT text_rect = rect;
  text_rect.left += kHorizontalPadding;
  text_rect.right -= kHorizontalPadding;
  DrawTextW(dc, text_.c_str(), static_cast<int>(text_.size()), &text_rect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  if (old_font != nullptr) {
    SelectObject(dc, old_font);
  }

  EndPaint(hwnd_, &paint);
}

}  // namespace milkyway::ui::composition

#endif
