#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <string>
#include <vector>

#include "ui/candidate/candidate_theme.h"

namespace milkyway::ui::candidate {

struct CandidateWindowItem {
  std::wstring text;
};

POINT CalculateCandidateWindowOrigin(const RECT& work_area, SIZE window_size,
                                     POINT anchor);

class CandidateWindowDelegate {
 public:
  virtual ~CandidateWindowDelegate() = default;
  virtual void OnCandidateWindowSelectionActivated(std::size_t index) = 0;
  virtual void OnCandidateWindowSelectionChanged(std::size_t index) = 0;
};

class CandidateWindow {
 public:
  CandidateWindow(HINSTANCE instance, CandidateWindowDelegate* delegate);
  ~CandidateWindow();

  CandidateWindow(const CandidateWindow&) = delete;
  CandidateWindow& operator=(const CandidateWindow&) = delete;

  bool Show(POINT anchor);
  void Hide();
  bool IsShown() const;
  void SetItems(std::vector<CandidateWindowItem> items);
  void SetSelection(std::size_t selection);
  void RefreshTheme();

  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam,
                                     LPARAM lparam);

 private:
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  bool EnsureWindow();
  SIZE MeasureWindowSize() const;
  void Paint();
  void ActivateItemFromPoint(POINT point);
  void SelectItemFromPoint(POINT point);
  std::size_t PageStart() const;
  std::size_t PageEnd() const;

  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  HFONT font_ = nullptr;
  CandidateWindowDelegate* delegate_ = nullptr;
  std::vector<CandidateWindowItem> items_;
  std::size_t selection_ = 0;
  CandidatePalette palette_;
};

}  // namespace milkyway::ui::candidate

#endif
