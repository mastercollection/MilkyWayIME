// Minimal IMM32 IME used only to de-risk approach H1: prove that a restarted
// host (Nikke / UnityWndClass) loads a third-party IMM32 IME at all and routes
// keystrokes to it. It does NOT implement Hangul composition. For any letter
// key it emits a fixed WM_IME_CHAR ('가', U+AC00) so the result is unambiguous,
// and it logs every IME export call (with pid/tid) to
// %TEMP%\MilkyWayIME\imm32_test.log so we can confirm the export is invoked
// inside the host process.
//
// If this proves loading + routing, Stage 2 replaces the echo with a real
// COMPOSITIONSTRING (GCS_COMPSTR/GCS_RESULTSTR) driven by the shared engine.

#include <windows.h>

#include <imm.h>
#include <immdev.h>

#include <cstdio>

namespace {

HINSTANCE g_instance = nullptr;
bool g_ui_class_registered = false;

// IME UI window class name. Kept short to fit the fixed buffer the system
// passes to ImeInquire.
const wchar_t kUiClassName[] = L"MwimeImmTestUI";

void GetLogPath(wchar_t* path, DWORD count) {
  wchar_t temp[MAX_PATH] = {};
  const DWORD length = GetTempPathW(MAX_PATH, temp);
  if (length == 0 || length >= MAX_PATH) {
    wcscpy_s(temp, L".\\");
  }
  wchar_t directory[MAX_PATH] = {};
  swprintf_s(directory, L"%sMilkyWayIME", temp);
  CreateDirectoryW(directory, nullptr);
  swprintf_s(path, count, L"%s\\imm32_test.log", directory);
}

void Log(const char* message) {
  wchar_t path[MAX_PATH] = {};
  GetLogPath(path, static_cast<DWORD>(sizeof(path) / sizeof(path[0])));

  HANDLE file = CreateFileW(path, FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }

  SYSTEMTIME time = {};
  GetLocalTime(&time);
  char line[512] = {};
  const int written = sprintf_s(
      line,
      "%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu tid=%lu %s\n", time.wYear,
      time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
      time.wMilliseconds, GetCurrentProcessId(), GetCurrentThreadId(), message);
  if (written > 0) {
    DWORD bytes = 0;
    WriteFile(file, line, static_cast<DWORD>(written), &bytes, nullptr);
  }
  CloseHandle(file);
}

LRESULT CALLBACK UiWndProc(HWND hwnd, UINT message, WPARAM wparam,
                           LPARAM lparam) {
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

void EnsureUiClassRegistered() {
  if (g_ui_class_registered) {
    return;
  }
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_IME | CS_GLOBALCLASS;
  wc.lpfnWndProc = UiWndProc;
  wc.hInstance = g_instance;
  wc.lpszClassName = kUiClassName;
  // IME UI windows require extra window bytes for the system to store state.
  wc.cbWndExtra = 2 * static_cast<int>(sizeof(LONG_PTR));
  RegisterClassExW(&wc);
  g_ui_class_registered = true;
}

}  // namespace

extern "C" {

BOOL WINAPI ImeInquire(LPIMEINFO ime_info, LPWSTR ui_class_name,
                       DWORD system_info_flags) {
  (void)system_info_flags;
  Log("ImeInquire");
  if (ime_info == nullptr) {
    return FALSE;
  }
  EnsureUiClassRegistered();
  ime_info->dwPrivateDataSize = 0;
  ime_info->fdwProperty =
      IME_PROP_UNICODE | IME_PROP_AT_CARET | IME_PROP_KBD_CHAR_FIRST;
  ime_info->fdwConversionCaps = IME_CMODE_NATIVE;
  ime_info->fdwSentenceCaps = 0;
  ime_info->fdwUICaps = 0;
  ime_info->fdwSCSCaps = 0;
  ime_info->fdwSelectCaps = 0;
  if (ui_class_name != nullptr) {
    lstrcpyW(ui_class_name, kUiClassName);
  }
  return TRUE;
}

BOOL WINAPI ImeConfigure(HKL hkl, HWND hwnd, DWORD mode, LPVOID data) {
  (void)hkl;
  (void)hwnd;
  (void)mode;
  (void)data;
  Log("ImeConfigure");
  return FALSE;
}

DWORD WINAPI ImeConversionList(HIMC himc, LPCWSTR source,
                               LPCANDIDATELIST candidate_list, DWORD buffer_len,
                               UINT flag) {
  (void)himc;
  (void)source;
  (void)candidate_list;
  (void)buffer_len;
  (void)flag;
  return 0;
}

BOOL WINAPI ImeDestroy(UINT reserved) {
  (void)reserved;
  Log("ImeDestroy");
  return TRUE;
}

LRESULT WINAPI ImeEscape(HIMC himc, UINT sub_func, LPVOID data) {
  (void)himc;
  (void)sub_func;
  (void)data;
  return 0;
}

BOOL WINAPI ImeSetActiveContext(HIMC himc, BOOL flag) {
  (void)himc;
  char buffer[64] = {};
  sprintf_s(buffer, "ImeSetActiveContext flag=%d", flag);
  Log(buffer);
  return TRUE;
}

BOOL WINAPI ImeProcessKey(HIMC himc, UINT virtual_key, LPARAM key_data,
                          CONST LPBYTE key_state) {
  (void)himc;
  (void)key_data;
  (void)key_state;
  const BOOL want = (virtual_key >= 'A' && virtual_key <= 'Z') ? TRUE : FALSE;
  char buffer[64] = {};
  sprintf_s(buffer, "ImeProcessKey vKey=0x%02X want=%d",
            static_cast<unsigned>(virtual_key), want);
  Log(buffer);
  return want;
}

UINT WINAPI ImeToAsciiEx(UINT virtual_key, UINT scan_code,
                         CONST LPBYTE key_state, LPTRANSMSGLIST trans_buffer,
                         UINT state, HIMC himc) {
  (void)scan_code;
  (void)key_state;
  (void)state;
  (void)himc;

  LPTRANSMSGLIST list = trans_buffer;
  UINT count = 0;
  if (list != nullptr && list->uMsgCount >= 1) {
    list->TransMsg[0].message = WM_IME_CHAR;
    list->TransMsg[0].wParam = 0xAC00;  // '가' - unambiguous proof of our IME.
    list->TransMsg[0].lParam = 1;
    count = 1;
  }
  char buffer[80] = {};
  sprintf_s(buffer, "ImeToAsciiEx vKey=0x%02X capacity=%u emit=%u",
            static_cast<unsigned>(virtual_key),
            list != nullptr ? list->uMsgCount : 0u, count);
  Log(buffer);
  return count;
}

BOOL WINAPI ImeSetCompositionString(HIMC himc, DWORD index, LPVOID comp,
                                    DWORD comp_len, LPVOID read, DWORD read_len) {
  (void)himc;
  (void)index;
  (void)comp;
  (void)comp_len;
  (void)read;
  (void)read_len;
  return FALSE;
}

BOOL WINAPI NotifyIME(HIMC himc, DWORD action, DWORD index, DWORD value) {
  (void)himc;
  (void)action;
  (void)index;
  (void)value;
  return FALSE;
}

BOOL WINAPI ImeSelect(HIMC himc, BOOL select) {
  if (select && himc != nullptr) {
    // Open in native (Hangul) conversion mode immediately so the system routes
    // character keys to ImeProcessKey/ImeToAsciiEx without needing a Hangul-key
    // toggle. Without this the IME stays closed and keys pass through.
    const BOOL set_conversion =
        ImmSetConversionStatus(himc, IME_CMODE_NATIVE, IME_SMODE_NONE);
    const BOOL set_open = ImmSetOpenStatus(himc, TRUE);
    char buffer[96] = {};
    sprintf_s(buffer, "ImeSelect select=1 setConversion=%d setOpen=%d",
              set_conversion, set_open);
    Log(buffer);
  } else {
    char buffer[64] = {};
    sprintf_s(buffer, "ImeSelect select=%d", select);
    Log(buffer);
  }
  return TRUE;
}

BOOL WINAPI ImeRegisterWord(LPCWSTR reading, DWORD style, LPCWSTR string) {
  (void)reading;
  (void)style;
  (void)string;
  return FALSE;
}

BOOL WINAPI ImeUnregisterWord(LPCWSTR reading, DWORD style, LPCWSTR string) {
  (void)reading;
  (void)style;
  (void)string;
  return FALSE;
}

UINT WINAPI ImeGetRegisterWordStyle(UINT item, LPSTYLEBUFW style_buffer) {
  (void)item;
  (void)style_buffer;
  return 0;
}

UINT WINAPI ImeEnumRegisterWord(REGISTERWORDENUMPROCW enum_proc, LPCWSTR reading,
                                DWORD style, LPCWSTR string, LPVOID data) {
  (void)enum_proc;
  (void)reading;
  (void)style;
  (void)string;
  (void)data;
  return 0;
}

DWORD WINAPI ImeGetImeMenuItems(HIMC himc, DWORD flags, DWORD type,
                                LPIMEMENUITEMINFOW parent_menu,
                                LPIMEMENUITEMINFOW menu, DWORD size) {
  (void)himc;
  (void)flags;
  (void)type;
  (void)parent_menu;
  (void)menu;
  (void)size;
  return 0;
}

}  // extern "C"

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
  (void)reserved;
  if (reason == DLL_PROCESS_ATTACH) {
    g_instance = instance;
    DisableThreadLibraryCalls(instance);
    Log("DllMain attach");
  }
  return TRUE;
}
