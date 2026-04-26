#include "ui/candidate/candidate_window.h"

#if defined(_WIN32)

#include <algorithm>
#include <utility>

#include <windowsx.h>

namespace milkyway::ui::candidate {
namespace {

constexpr wchar_t kCandidateWindowClassName[] =
    L"MilkyWayIMECandidateWindow";
constexpr int kPageSize = 9;
constexpr int kRowHeight = 28;
constexpr int kHorizontalPadding = 10;
constexpr int kVerticalPadding = 6;
constexpr int kNumberColumnWidth = 24;
constexpr int kMinimumWidth = 128;
constexpr int kBorderWidth = 1;

ATOM RegisterCandidateWindowClass(HINSTANCE instance) {
  static ATOM atom = 0;
  if (atom != 0) {
    return atom;
  }

  WNDCLASSEXW window_class = {};
  window_class.cbSize = sizeof(window_class);
  window_class.style = CS_HREDRAW | CS_VREDRAW;
  window_class.lpfnWndProc = CandidateWindow::WindowProc;
  window_class.cbWndExtra = sizeof(LONG_PTR);
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.lpszClassName = kCandidateWindowClassName;
  atom = RegisterClassExW(&window_class);
  return atom;
}

HFONT CreateCandidateFont() {
  LOGFONTW log_font = {};
  log_font.lfHeight = -16;
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

}  // namespace

POINT CalculateCandidateWindowOrigin(const RECT& work_area, SIZE window_size,
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

CandidateWindow::CandidateWindow(HINSTANCE instance,
                                 CandidateWindowDelegate* delegate)
    : instance_(instance), delegate_(delegate) {
  RefreshTheme();
}

CandidateWindow::~CandidateWindow() {
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  if (font_ != nullptr) {
    DeleteObject(font_);
    font_ = nullptr;
  }
}

bool CandidateWindow::Show(POINT anchor) {
  if (items_.empty() || !EnsureWindow()) {
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
      CalculateCandidateWindowOrigin(work_area, size, anchor);

  SetWindowPos(hwnd_, HWND_TOPMOST, origin.x, origin.y, size.cx, size.cy,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
  InvalidateRect(hwnd_, nullptr, TRUE);
  return true;
}

void CandidateWindow::Hide() {
  if (hwnd_ != nullptr) {
    ShowWindow(hwnd_, SW_HIDE);
  }
}

bool CandidateWindow::IsShown() const {
  return hwnd_ != nullptr && IsWindowVisible(hwnd_);
}

void CandidateWindow::SetItems(std::vector<CandidateWindowItem> items) {
  items_ = std::move(items);
  if (selection_ >= items_.size()) {
    selection_ = items_.empty() ? 0 : items_.size() - 1;
  }
  if (hwnd_ != nullptr) {
    InvalidateRect(hwnd_, nullptr, TRUE);
  }
}

void CandidateWindow::SetSelection(std::size_t selection) {
  if (items_.empty()) {
    selection_ = 0;
  } else {
    selection_ = std::min(selection, items_.size() - 1);
  }
  if (hwnd_ != nullptr) {
    InvalidateRect(hwnd_, nullptr, TRUE);
  }
}

void CandidateWindow::RefreshTheme() {
  palette_ = CandidatePaletteFor(DetectCandidateThemeMode(),
                                 IsHighContrastEnabled());
  if (hwnd_ != nullptr) {
    InvalidateRect(hwnd_, nullptr, TRUE);
  }
}

LRESULT CALLBACK CandidateWindow::WindowProc(HWND hwnd, UINT message,
                                             WPARAM wparam, LPARAM lparam) {
  CandidateWindow* window = reinterpret_cast<CandidateWindow*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const CREATESTRUCTW* create_struct =
        reinterpret_cast<const CREATESTRUCTW*>(lparam);
    window = static_cast<CandidateWindow*>(create_struct->lpCreateParams);
    window->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
  }

  if (window == nullptr) {
    return DefWindowProcW(hwnd, message, wparam, lparam);
  }

  return window->HandleMessage(message, wparam, lparam);
}

LRESULT CandidateWindow::HandleMessage(UINT message, WPARAM wparam,
                                       LPARAM lparam) {
  switch (message) {
    case WM_PAINT:
      Paint();
      return 0;
    case WM_MOUSEMOVE: {
      POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      SelectItemFromPoint(point);
      return 0;
    }
    case WM_LBUTTONUP: {
      POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      ActivateItemFromPoint(point);
      return 0;
    }
    case WM_SETTINGCHANGE:
      RefreshTheme();
      return 0;
    case WM_ERASEBKGND:
      return 1;
    default:
      break;
  }

  return DefWindowProcW(hwnd_, message, wparam, lparam);
}

bool CandidateWindow::EnsureWindow() {
  if (hwnd_ != nullptr) {
    return true;
  }

  if (instance_ == nullptr || RegisterCandidateWindowClass(instance_) == 0) {
    return false;
  }

  font_ = CreateCandidateFont();
  hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                          kCandidateWindowClassName, L"", WS_POPUP, 0, 0, 0, 0,
                          nullptr, nullptr, instance_, this);
  return hwnd_ != nullptr;
}

SIZE CandidateWindow::MeasureWindowSize() const {
  int width = kMinimumWidth;
  HDC dc = GetDC(hwnd_);
  HFONT old_font = nullptr;
  if (dc != nullptr && font_ != nullptr) {
    old_font = static_cast<HFONT>(SelectObject(dc, font_));
  }

  for (std::size_t index = PageStart(); index < PageEnd(); ++index) {
    SIZE text_size = {};
    if (dc != nullptr) {
      GetTextExtentPoint32W(
          dc, items_[index].text.c_str(),
          static_cast<int>(items_[index].text.size()), &text_size);
    }
    width = std::max<int>(
        width, static_cast<int>(text_size.cx) + kNumberColumnWidth +
                   (kHorizontalPadding * 2));
  }

  if (old_font != nullptr) {
    SelectObject(dc, old_font);
  }
  if (dc != nullptr) {
    ReleaseDC(hwnd_, dc);
  }

  const int visible_count =
      static_cast<int>(PageEnd() > PageStart() ? PageEnd() - PageStart() : 1);
  const int height =
      (kVerticalPadding * 2) + (visible_count * kRowHeight) + kBorderWidth;
  return SIZE{width, height};
}

void CandidateWindow::Paint() {
  PAINTSTRUCT paint = {};
  HDC dc = BeginPaint(hwnd_, &paint);
  if (dc == nullptr) {
    return;
  }

  RECT client_rect = {};
  GetClientRect(hwnd_, &client_rect);
  FillRectColor(dc, client_rect, palette_.background);
  FrameRectColor(dc, client_rect, palette_.border);

  HFONT old_font = nullptr;
  if (font_ != nullptr) {
    old_font = static_cast<HFONT>(SelectObject(dc, font_));
  }
  SetBkMode(dc, TRANSPARENT);

  int y = kVerticalPadding;
  for (std::size_t index = PageStart(); index < PageEnd(); ++index) {
    RECT row_rect = {kBorderWidth, y, client_rect.right - kBorderWidth,
                     y + kRowHeight};
    const bool selected = index == selection_;
    if (selected) {
      FillRectColor(dc, row_rect, palette_.selected_background);
    }

    const int page_index = static_cast<int>(index - PageStart()) + 1;
    wchar_t number_text[4] = {};
    swprintf_s(number_text, L"%d", page_index);

    RECT number_rect = {kHorizontalPadding, y, kHorizontalPadding + 18,
                        y + kRowHeight};
    SetTextColor(dc, selected ? palette_.selected_text : palette_.number);
    DrawTextW(dc, number_text, -1, &number_rect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT text_rect = {kHorizontalPadding + kNumberColumnWidth, y,
                      client_rect.right - kHorizontalPadding, y + kRowHeight};
    SetTextColor(dc, selected ? palette_.selected_text : palette_.text);
    DrawTextW(dc, items_[index].text.c_str(),
              static_cast<int>(items_[index].text.size()), &text_rect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    y += kRowHeight;
  }

  if (old_font != nullptr) {
    SelectObject(dc, old_font);
  }
  EndPaint(hwnd_, &paint);
}

void CandidateWindow::ActivateItemFromPoint(POINT point) {
  const int row =
      std::max(0, (static_cast<int>(point.y) - kVerticalPadding) / kRowHeight);
  const std::size_t index =
      PageStart() + static_cast<std::size_t>(row);
  if (index < PageEnd() && delegate_ != nullptr) {
    delegate_->OnCandidateWindowSelectionActivated(index);
  }
}

void CandidateWindow::SelectItemFromPoint(POINT point) {
  const int row =
      std::max(0, (static_cast<int>(point.y) - kVerticalPadding) / kRowHeight);
  const std::size_t index =
      PageStart() + static_cast<std::size_t>(row);
  if (index < PageEnd() && delegate_ != nullptr && index != selection_) {
    delegate_->OnCandidateWindowSelectionChanged(index);
  }
}

std::size_t CandidateWindow::PageStart() const {
  return selection_ / kPageSize * kPageSize;
}

std::size_t CandidateWindow::PageEnd() const {
  return std::min(items_.size(), PageStart() + kPageSize);
}

}  // namespace milkyway::ui::candidate

#endif
