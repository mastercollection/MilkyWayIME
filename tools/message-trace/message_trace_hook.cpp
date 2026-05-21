#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <cstdint>
#include <cstdio>

namespace {

HINSTANCE g_instance = nullptr;

void GetTraceDirectory(wchar_t* path, DWORD path_count) {
  wchar_t temp[MAX_PATH] = {};
  DWORD length = GetTempPathW(MAX_PATH, temp);
  if (length == 0 || length >= MAX_PATH) {
    wcscpy_s(temp, L".\\");
  }
  swprintf_s(path, path_count, L"%sMilkyWayIME", temp);
  CreateDirectoryW(path, nullptr);
}

bool ShouldLogAllMessages() {
  wchar_t directory[MAX_PATH] = {};
  GetTraceDirectory(directory,
                    static_cast<DWORD>(sizeof(directory) / sizeof(directory[0])));
  wchar_t path[MAX_PATH] = {};
  swprintf_s(path, L"%s\\message_trace_log_all.flag", directory);
  const DWORD attributes = GetFileAttributesW(path);
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool IsInterestingMessage(UINT message) {
  switch (message) {
    case WM_INPUT:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_UNICHAR:
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_ENDCOMPOSITION:
    case WM_IME_COMPOSITION:
    case WM_IME_SETCONTEXT:
    case WM_IME_NOTIFY:
    case WM_IME_CONTROL:
    case WM_IME_COMPOSITIONFULL:
    case WM_IME_SELECT:
    case WM_IME_CHAR:
    case WM_IME_REQUEST:
    case WM_SETTEXT:
    case WM_GETTEXT:
    case WM_GETTEXTLENGTH:
      return true;
    default:
      return false;
  }
}

const char* MessageName(UINT message) {
  switch (message) {
    case WM_INPUT:
      return "WM_INPUT";
    case WM_KEYDOWN:
      return "WM_KEYDOWN";
    case WM_KEYUP:
      return "WM_KEYUP";
    case WM_SYSKEYDOWN:
      return "WM_SYSKEYDOWN";
    case WM_SYSKEYUP:
      return "WM_SYSKEYUP";
    case WM_CHAR:
      return "WM_CHAR";
    case WM_SYSCHAR:
      return "WM_SYSCHAR";
    case WM_UNICHAR:
      return "WM_UNICHAR";
    case WM_IME_STARTCOMPOSITION:
      return "WM_IME_STARTCOMPOSITION";
    case WM_IME_ENDCOMPOSITION:
      return "WM_IME_ENDCOMPOSITION";
    case WM_IME_COMPOSITION:
      return "WM_IME_COMPOSITION";
    case WM_IME_SETCONTEXT:
      return "WM_IME_SETCONTEXT";
    case WM_IME_NOTIFY:
      return "WM_IME_NOTIFY";
    case WM_IME_CONTROL:
      return "WM_IME_CONTROL";
    case WM_IME_COMPOSITIONFULL:
      return "WM_IME_COMPOSITIONFULL";
    case WM_IME_SELECT:
      return "WM_IME_SELECT";
    case WM_IME_CHAR:
      return "WM_IME_CHAR";
    case WM_IME_REQUEST:
      return "WM_IME_REQUEST";
    case WM_SETTEXT:
      return "WM_SETTEXT";
    case WM_GETTEXT:
      return "WM_GETTEXT";
    case WM_GETTEXTLENGTH:
      return "WM_GETTEXTLENGTH";
    default:
      return "UNKNOWN";
  }
}

bool IsTextPayloadMessage(UINT message) {
  return message == WM_CHAR || message == WM_SYSCHAR || message == WM_UNICHAR ||
         message == WM_IME_CHAR || message == WM_SETTEXT;
}

void AppendText(char* target, size_t target_size, const char* text) {
  const size_t used = strnlen_s(target, target_size);
  if (used >= target_size) {
    return;
  }
  strcat_s(target + used, target_size - used, text);
}

void AppendImeCompositionFlags(char* target, size_t target_size, LPARAM flags) {
  struct FlagName {
    LPARAM flag;
    const char* name;
  };
  constexpr FlagName kFlags[] = {
      {GCS_COMPREADSTR, "GCS_COMPREADSTR"},
      {GCS_COMPREADATTR, "GCS_COMPREADATTR"},
      {GCS_COMPREADCLAUSE, "GCS_COMPREADCLAUSE"},
      {GCS_COMPSTR, "GCS_COMPSTR"},
      {GCS_COMPATTR, "GCS_COMPATTR"},
      {GCS_COMPCLAUSE, "GCS_COMPCLAUSE"},
      {GCS_CURSORPOS, "GCS_CURSORPOS"},
      {GCS_DELTASTART, "GCS_DELTASTART"},
      {GCS_RESULTREADSTR, "GCS_RESULTREADSTR"},
      {GCS_RESULTREADCLAUSE, "GCS_RESULTREADCLAUSE"},
      {GCS_RESULTSTR, "GCS_RESULTSTR"},
      {GCS_RESULTCLAUSE, "GCS_RESULTCLAUSE"},
      {CS_INSERTCHAR, "CS_INSERTCHAR"},
      {CS_NOMOVECARET, "CS_NOMOVECARET"},
  };

  bool first = true;
  for (const FlagName& item : kFlags) {
    if ((flags & item.flag) == 0) {
      continue;
    }
    if (!first) {
      AppendText(target, target_size, "|");
    }
    AppendText(target, target_size, item.name);
    first = false;
  }
  if (first) {
    AppendText(target, target_size, "-");
  }
}

void GetLogPath(wchar_t* path, DWORD path_count) {
  wchar_t directory[MAX_PATH] = {};
  GetTraceDirectory(directory,
                    static_cast<DWORD>(sizeof(directory) / sizeof(directory[0])));
  swprintf_s(path, path_count, L"%s\\message_trace.log", directory);
}

void WriteLine(const char* line) {
  wchar_t path[MAX_PATH] = {};
  GetLogPath(path, static_cast<DWORD>(sizeof(path) / sizeof(path[0])));

  HANDLE file = CreateFileW(path, FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  DWORD written = 0;
  WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
  CloseHandle(file);
}

void LogMessage(const char* source, HWND hwnd, UINT message, WPARAM wparam,
                LPARAM lparam) {
  if (!ShouldLogAllMessages() && !IsInterestingMessage(message)) {
    return;
  }

  DWORD pid = 0;
  DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
  if (tid == 0) {
    tid = GetCurrentThreadId();
    pid = GetCurrentProcessId();
  }

  char class_name[256] = {};
  GetClassNameA(hwnd, class_name,
                static_cast<int>(sizeof(class_name) / sizeof(class_name[0])));

  SYSTEMTIME time = {};
  GetLocalTime(&time);

  char flags[512] = {};
  if (message == WM_IME_COMPOSITION) {
    AppendImeCompositionFlags(flags, sizeof(flags) / sizeof(flags[0]), lparam);
  } else {
    strcpy_s(flags, "-");
  }

  char text_info[64] = {};
  if (IsTextPayloadMessage(message)) {
    strcpy_s(text_info, "redacted=1");
  } else {
    sprintf_s(text_info, "wparam=0x%llX",
              static_cast<unsigned long long>(wparam));
  }

  char line[2048] = {};
  sprintf_s(line,
            "%04u-%02u-%02u %02u:%02u:%02u.%03u source=%s pid=%lu tid=%lu "
            "hwnd=0x%p class=%s msg=0x%04X name=%s %s lparam=0x%llX flags=%s\n",
            time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
            time.wSecond, time.wMilliseconds, source, pid, tid, hwnd,
            class_name, message, MessageName(message), text_info,
            static_cast<unsigned long long>(lparam), flags);
  WriteLine(line);
}

}  // namespace

extern "C" __declspec(dllexport) LRESULT CALLBACK MwimeGetMessageProc(
    int code, WPARAM wparam, LPARAM lparam) {
  if (code >= 0 && lparam != 0) {
    const MSG* message = reinterpret_cast<const MSG*>(lparam);
    if (message != nullptr) {
      LogMessage(wparam == PM_REMOVE ? "getmsg_remove" : "getmsg",
                 message->hwnd, message->message, message->wParam,
                 message->lParam);
    }
  }
  return CallNextHookEx(nullptr, code, wparam, lparam);
}

extern "C" __declspec(dllexport) LRESULT CALLBACK MwimeCallWndProc(
    int code, WPARAM wparam, LPARAM lparam) {
  if (code >= 0 && lparam != 0) {
    const CWPSTRUCT* message = reinterpret_cast<const CWPSTRUCT*>(lparam);
    if (message != nullptr) {
      LogMessage(wparam != 0 ? "callwnd_sent_by_current" : "callwnd",
                 message->hwnd, message->message, message->wParam,
                 message->lParam);
    }
  }
  return CallNextHookEx(nullptr, code, wparam, lparam);
}

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_instance = instance;
    DisableThreadLibraryCalls(instance);
  }
  return TRUE;
}
