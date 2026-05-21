#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <tlhelp32.h>

#include <chrono>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Options {
  std::wstring process;
  DWORD pid = 0;
  DWORD thread_id = 0;
  int duration_seconds = 20;
  bool clear = false;
  bool all_threads = false;
  bool log_all = false;
  bool help = false;
};

struct ProcessEntry {
  DWORD pid = 0;
  std::wstring exe_name;
};

struct WindowEntry {
  HWND hwnd = nullptr;
  DWORD thread_id = 0;
  std::wstring class_name;
  bool visible = false;
  bool owned = false;
};

struct TargetInfo {
  DWORD pid = 0;
  DWORD thread_id = 0;
  HWND hwnd = nullptr;
  std::wstring process_name;
  std::wstring process_bitness;
  std::wstring hwnd_class;
  std::vector<ProcessEntry> matched_processes;
  std::vector<ProcessEntry> process_suggestions;
  std::vector<WindowEntry> windows;
  DWORD error = 0;
};

std::wstring ToLower(std::wstring value) {
  for (wchar_t& ch : value) {
    ch = static_cast<wchar_t>(towlower(ch));
  }
  return value;
}

bool EndsWithInsensitive(const std::wstring& value,
                         const std::wstring& suffix) {
  const std::wstring lower_value = ToLower(value);
  const std::wstring lower_suffix = ToLower(suffix);
  return lower_value.size() >= lower_suffix.size() &&
         lower_value.compare(lower_value.size() - lower_suffix.size(),
                             lower_suffix.size(), lower_suffix) == 0;
}

std::wstring NormalizeProcessName(std::wstring value) {
  if (!value.empty() && !EndsWithInsensitive(value, L".exe")) {
    value += L".exe";
  }
  return value;
}

std::wstring StripExeSuffix(std::wstring value) {
  if (EndsWithInsensitive(value, L".exe")) {
    value.resize(value.size() - 4);
  }
  return value;
}

bool ParseDword(const wchar_t* text, DWORD* value) {
  if (text == nullptr || value == nullptr) {
    return false;
  }
  wchar_t* end = nullptr;
  const unsigned long parsed = wcstoul(text, &end, 0);
  if (end == text || *end != L'\0') {
    return false;
  }
  *value = static_cast<DWORD>(parsed);
  return true;
}

bool ParseInt(const wchar_t* text, int* value) {
  DWORD parsed = 0;
  if (!ParseDword(text, &parsed) || parsed > 86400) {
    return false;
  }
  *value = static_cast<int>(parsed);
  return true;
}

Options ParseArgs(int argc, wchar_t** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::wstring arg = argv[index];
    if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
      options.help = true;
    } else if (arg == L"--process" && index + 1 < argc) {
      options.process = NormalizeProcessName(argv[++index]);
    } else if (arg == L"--pid" && index + 1 < argc) {
      if (!ParseDword(argv[++index], &options.pid)) {
        options.help = true;
      }
    } else if ((arg == L"--thread" || arg == L"--tid") && index + 1 < argc) {
      if (!ParseDword(argv[++index], &options.thread_id)) {
        options.help = true;
      }
    } else if (arg == L"--duration" && index + 1 < argc) {
      if (!ParseInt(argv[++index], &options.duration_seconds)) {
        options.help = true;
      }
    } else if (arg == L"--all-threads") {
      options.all_threads = true;
    } else if (arg == L"--log-all") {
      options.log_all = true;
    } else if (arg == L"--clear") {
      options.clear = true;
    } else {
      options.help = true;
    }
  }
  return options;
}

void PrintUsage() {
  std::wcout
      << L"Usage:\n"
      << L"  mwime_message_trace.exe [--process exe | --pid pid | --thread tid]\n"
      << L"                          [--duration seconds] [--all-threads]\n"
      << L"                          [--log-all] [--clear]\n\n"
      << L"If no target is specified, the current foreground thread is traced.\n"
      << L"--all-threads hooks every top-level window thread in the selected process.\n"
      << L"--log-all records every observed message; use only for short runs.\n"
      << L"Output: %TEMP%\\MilkyWayIME\\message_trace.log\n";
}

std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) {
    return {};
  }
  const int length =
      WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
                          static_cast<int>(value.size()), nullptr, 0, nullptr,
                          nullptr);
  if (length <= 0) {
    return {};
  }
  std::string result(static_cast<size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
                      static_cast<int>(value.size()), result.data(), length,
                      nullptr, nullptr);
  return result;
}

void AppendDiagnosticLine(const std::filesystem::path& path,
                          const std::wstring& line) {
  std::ofstream stream(path, std::ios::app | std::ios::binary);
  if (!stream) {
    return;
  }
  stream << WideToUtf8(line) << "\n";
}

std::wstring LastErrorText(DWORD error) {
  if (error == 0) {
    return L"0";
  }
  wchar_t* buffer = nullptr;
  const DWORD length = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
  std::wstring text = std::to_wstring(error);
  if (length > 0 && buffer != nullptr) {
    text += L" (";
    text.append(buffer, length);
    while (!text.empty() &&
           (text.back() == L'\r' || text.back() == L'\n' ||
            text.back() == L' ')) {
      text.pop_back();
    }
    text += L")";
  }
  if (buffer != nullptr) {
    LocalFree(buffer);
  }
  return text;
}

std::vector<ProcessEntry> EnumerateProcesses() {
  std::vector<ProcessEntry> processes;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return processes;
  }
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      processes.push_back(ProcessEntry{entry.th32ProcessID, entry.szExeFile});
      entry.dwSize = sizeof(entry);
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return processes;
}

DWORD FindProcessId(const std::wstring& process_name) {
  const std::wstring target = ToLower(process_name);
  for (const ProcessEntry& process : EnumerateProcesses()) {
    if (ToLower(process.exe_name) == target) {
      return process.pid;
    }
  }
  return 0;
}

std::vector<ProcessEntry> FindProcesses(const std::wstring& process_name) {
  std::vector<ProcessEntry> matches;
  const std::wstring target = ToLower(process_name);
  for (const ProcessEntry& process : EnumerateProcesses()) {
    if (ToLower(process.exe_name) == target) {
      matches.push_back(process);
    }
  }
  return matches;
}

std::vector<ProcessEntry> SuggestProcesses(const std::wstring& process_name) {
  std::vector<ProcessEntry> suggestions;
  const std::wstring target = ToLower(StripExeSuffix(process_name));
  if (target.empty()) {
    return suggestions;
  }
  for (const ProcessEntry& process : EnumerateProcesses()) {
    if (ToLower(process.exe_name).find(target) != std::wstring::npos) {
      suggestions.push_back(process);
    }
  }
  return suggestions;
}

struct MainWindowSearch {
  DWORD pid = 0;
  HWND hwnd = nullptr;
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lparam) {
  auto* search = reinterpret_cast<MainWindowSearch*>(lparam);
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid != search->pid || !IsWindowVisible(hwnd) ||
      GetWindow(hwnd, GW_OWNER) != nullptr) {
    return TRUE;
  }
  search->hwnd = hwnd;
  return FALSE;
}

struct WindowSearch {
  DWORD pid = 0;
  std::vector<WindowEntry>* windows = nullptr;
};

BOOL CALLBACK CollectWindowsProc(HWND hwnd, LPARAM lparam) {
  auto* search = reinterpret_cast<WindowSearch*>(lparam);
  DWORD pid = 0;
  const DWORD thread_id = GetWindowThreadProcessId(hwnd, &pid);
  if (pid != search->pid || search->windows == nullptr) {
    return TRUE;
  }

  wchar_t class_name[256] = {};
  GetClassNameW(hwnd, class_name,
                static_cast<int>(sizeof(class_name) / sizeof(class_name[0])));
  search->windows->push_back(WindowEntry{
      hwnd,
      thread_id,
      class_name,
      IsWindowVisible(hwnd) != FALSE,
      GetWindow(hwnd, GW_OWNER) != nullptr,
  });
  return TRUE;
}

std::vector<WindowEntry> EnumerateTopLevelWindowsForProcess(DWORD pid) {
  std::vector<WindowEntry> windows;
  WindowSearch search;
  search.pid = pid;
  search.windows = &windows;
  EnumWindows(CollectWindowsProc, reinterpret_cast<LPARAM>(&search));
  return windows;
}

std::wstring WindowClassName(HWND hwnd) {
  wchar_t class_name[256] = {};
  GetClassNameW(hwnd, class_name,
                static_cast<int>(sizeof(class_name) / sizeof(class_name[0])));
  return class_name;
}

std::wstring DescribeProcessBitness(DWORD pid) {
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (process == nullptr) {
    return L"unknown(open_error=" + LastErrorText(GetLastError()) + L")";
  }

  BOOL wow64 = FALSE;
  if (!IsWow64Process(process, &wow64)) {
    const DWORD error = GetLastError();
    CloseHandle(process);
    return L"unknown(iswow64_error=" + LastErrorText(error) + L")";
  }
  CloseHandle(process);

  SYSTEM_INFO native_info = {};
  GetNativeSystemInfo(&native_info);
  const bool native_64 =
      native_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ||
      native_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64;
  if (!native_64) {
    return L"x86";
  }
  return wow64 ? L"x86" : L"x64";
}

DWORD ResolveThreadId(const Options& options, HWND* target_hwnd) {
  if (options.thread_id != 0) {
    return options.thread_id;
  }

  if (options.pid != 0 || !options.process.empty()) {
    const DWORD pid =
        options.pid != 0 ? options.pid : FindProcessId(options.process);
    if (pid == 0) {
      return 0;
    }
    MainWindowSearch search;
    search.pid = pid;
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&search));
    if (target_hwnd != nullptr) {
      *target_hwnd = search.hwnd;
    }
    if (search.hwnd != nullptr) {
      DWORD window_pid = 0;
      return GetWindowThreadProcessId(search.hwnd, &window_pid);
    }
    return 0;
  }

  HWND foreground = GetForegroundWindow();
  if (target_hwnd != nullptr) {
    *target_hwnd = foreground;
  }
  DWORD pid = 0;
  return GetWindowThreadProcessId(foreground, &pid);
}

TargetInfo ResolveTargetInfo(const Options& options) {
  TargetInfo target;
  if (options.thread_id != 0) {
    target.thread_id = options.thread_id;
    return target;
  }

  if (options.pid != 0 || !options.process.empty()) {
    if (!options.process.empty()) {
      target.matched_processes = FindProcesses(options.process);
      if (target.matched_processes.empty()) {
        target.process_suggestions = SuggestProcesses(options.process);
        return target;
      }
      target.pid = target.matched_processes.front().pid;
      target.process_name = target.matched_processes.front().exe_name;
    } else {
      target.pid = options.pid;
    }

    target.process_bitness = DescribeProcessBitness(target.pid);
    target.windows = EnumerateTopLevelWindowsForProcess(target.pid);
    for (const WindowEntry& window : target.windows) {
      if (window.visible && !window.owned) {
        target.hwnd = window.hwnd;
        target.thread_id = window.thread_id;
        target.hwnd_class = window.class_name;
        return target;
      }
    }
    return target;
  }

  target.hwnd = GetForegroundWindow();
  target.thread_id = GetWindowThreadProcessId(target.hwnd, &target.pid);
  target.hwnd_class = WindowClassName(target.hwnd);
  target.process_bitness = DescribeProcessBitness(target.pid);
  return target;
}

std::wstring FormatHwnd(HWND hwnd) {
  wchar_t buffer[32] = {};
  swprintf_s(buffer, L"0x%p", hwnd);
  return buffer;
}

std::wstring FormatHook(HHOOK hook) {
  wchar_t buffer[32] = {};
  swprintf_s(buffer, L"0x%p", hook);
  return buffer;
}

std::filesystem::path ExeDirectory() {
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
  return std::filesystem::path(path).parent_path();
}

std::filesystem::path HookDllPath() {
  return ExeDirectory() / L"mwime_message_trace_hook.dll";
}

std::filesystem::path LogPath() {
  wchar_t temp[MAX_PATH] = {};
  DWORD length = GetTempPathW(MAX_PATH, temp);
  std::filesystem::path directory =
      length > 0 ? std::filesystem::path(std::wstring(temp, length))
                 : std::filesystem::path(L".");
  directory /= L"MilkyWayIME";
  std::filesystem::create_directories(directory);
  return directory / L"message_trace.log";
}

std::filesystem::path TraceDirectory() {
  wchar_t temp[MAX_PATH] = {};
  DWORD length = GetTempPathW(MAX_PATH, temp);
  std::filesystem::path directory =
      length > 0 ? std::filesystem::path(std::wstring(temp, length))
                 : std::filesystem::path(L".");
  directory /= L"MilkyWayIME";
  std::filesystem::create_directories(directory);
  return directory;
}

std::filesystem::path LogAllFlagPath() {
  return TraceDirectory() / L"message_trace_log_all.flag";
}

void PrintTargetDiagnostics(const TargetInfo& target,
                            const std::filesystem::path& log_path) {
  auto emit = [&](const std::wstring& line) {
    std::wcout << line << L"\n";
    AppendDiagnosticLine(log_path, L"# " + line);
  };

  emit(L"Target diagnostics:");
  if (!target.matched_processes.empty()) {
    emit(L"  matched_processes=" +
         std::to_wstring(target.matched_processes.size()));
    for (const ProcessEntry& process : target.matched_processes) {
      emit(L"    pid=" + std::to_wstring(process.pid) + L" exe=" +
           process.exe_name);
    }
  }
  if (!target.process_suggestions.empty()) {
    emit(L"  no exact process match; substring suggestions:");
    for (const ProcessEntry& process : target.process_suggestions) {
      emit(L"    pid=" + std::to_wstring(process.pid) + L" exe=" +
           process.exe_name);
    }
  }
  emit(L"  selected_pid=" + std::to_wstring(target.pid));
  emit(L"  selected_tid=" + std::to_wstring(target.thread_id));
  emit(L"  selected_hwnd=" + FormatHwnd(target.hwnd));
  emit(L"  selected_class=" + target.hwnd_class);
  emit(L"  selected_bitness=" + target.process_bitness);
  if (!target.windows.empty()) {
    emit(L"  top_level_windows=" + std::to_wstring(target.windows.size()));
    for (const WindowEntry& window : target.windows) {
      emit(L"    hwnd=" + FormatHwnd(window.hwnd) +
           L" tid=" + std::to_wstring(window.thread_id) + L" class=" +
           window.class_name + L" visible=" +
           (window.visible ? L"1" : L"0") + L" owned=" +
           (window.owned ? L"1" : L"0"));
    }
  }
}

std::vector<DWORD> ResolveTraceThreads(const Options& options,
                                       const TargetInfo& target) {
  std::vector<DWORD> threads;
  if (!options.all_threads) {
    if (target.thread_id != 0) {
      threads.push_back(target.thread_id);
    }
    return threads;
  }

  std::set<DWORD> seen;
  for (const WindowEntry& window : target.windows) {
    if (window.thread_id == 0 || seen.find(window.thread_id) != seen.end()) {
      continue;
    }
    seen.insert(window.thread_id);
    threads.push_back(window.thread_id);
  }
  if (threads.empty() && target.thread_id != 0) {
    threads.push_back(target.thread_id);
  }
  return threads;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);

  const Options options = ParseArgs(argc, argv);
  if (options.help) {
    PrintUsage();
    return 1;
  }

  const std::filesystem::path log_path = LogPath();
  const std::filesystem::path log_all_flag_path = LogAllFlagPath();
  if (options.clear) {
    std::error_code error;
    std::filesystem::remove(log_path, error);
  }
  {
    std::error_code error;
    if (options.log_all) {
      std::ofstream flag(log_all_flag_path, std::ios::binary);
      flag << "1\n";
    } else {
      std::filesystem::remove(log_all_flag_path, error);
    }
  }

  const TargetInfo target = ResolveTargetInfo(options);
  PrintTargetDiagnostics(target, log_path);
  AppendDiagnosticLine(log_path,
                       L"# log_all=" + std::wstring(options.log_all ? L"1" : L"0") +
                           L" flag=" + log_all_flag_path.wstring());

  const std::vector<DWORD> thread_ids = ResolveTraceThreads(options, target);
  if (thread_ids.empty()) {
    std::wcerr << L"Could not resolve target thread.\n";
    AppendDiagnosticLine(log_path, L"# Could not resolve target thread.");
    return 2;
  }

  const std::filesystem::path dll_path = HookDllPath();
  AppendDiagnosticLine(log_path, L"# hook_dll=" + dll_path.wstring());
  HMODULE hook_module = LoadLibraryW(dll_path.c_str());
  if (hook_module == nullptr) {
    const DWORD error = GetLastError();
    std::wcerr << L"Failed to load hook DLL: " << dll_path
               << L" error=" << LastErrorText(error) << L"\n";
    AppendDiagnosticLine(log_path,
                         L"# LoadLibrary failed error=" + LastErrorText(error));
    return 3;
  }

  HOOKPROC get_message_proc =
      reinterpret_cast<HOOKPROC>(GetProcAddress(hook_module, "MwimeGetMessageProc"));
  HOOKPROC call_wnd_proc =
      reinterpret_cast<HOOKPROC>(GetProcAddress(hook_module, "MwimeCallWndProc"));
  if (get_message_proc == nullptr || call_wnd_proc == nullptr) {
    std::wcerr << L"Hook exports not found.\n";
    AppendDiagnosticLine(log_path, L"# Hook exports not found.");
    FreeLibrary(hook_module);
    return 4;
  }

  AppendDiagnosticLine(log_path,
                       L"# trace_thread_count=" +
                           std::to_wstring(thread_ids.size()));
  std::vector<HHOOK> hooks;
  int hook_failures = 0;
  for (DWORD thread_id : thread_ids) {
    SetLastError(0);
    HHOOK get_message_hook = SetWindowsHookExW(
        WH_GETMESSAGE, get_message_proc, hook_module, thread_id);
    const DWORD get_message_error = GetLastError();
    SetLastError(0);
    HHOOK call_wnd_hook = SetWindowsHookExW(
        WH_CALLWNDPROC, call_wnd_proc, hook_module, thread_id);
    const DWORD call_wnd_error = GetLastError();

    AppendDiagnosticLine(log_path,
                         L"# hook thread=" + std::to_wstring(thread_id) +
                             L" getmessage=" + FormatHook(get_message_hook) +
                             L" get_error=" +
                             LastErrorText(get_message_error) +
                             L" callwnd=" + FormatHook(call_wnd_hook) +
                             L" call_error=" +
                             LastErrorText(call_wnd_error));

    if (get_message_hook == nullptr || call_wnd_hook == nullptr) {
      ++hook_failures;
      std::wcerr << L"SetWindowsHookEx failed for thread=" << thread_id
                 << L" get_hook=" << get_message_hook
                 << L" call_hook=" << call_wnd_hook
                 << L" get_error=" << LastErrorText(get_message_error)
                 << L" call_error=" << LastErrorText(call_wnd_error) << L"\n";
      if (get_message_hook != nullptr) {
        UnhookWindowsHookEx(get_message_hook);
      }
      if (call_wnd_hook != nullptr) {
        UnhookWindowsHookEx(call_wnd_hook);
      }
      continue;
    }

    hooks.push_back(get_message_hook);
    hooks.push_back(call_wnd_hook);
  }

  if (hooks.empty()) {
    FreeLibrary(hook_module);
    return 5;
  }

  std::wcout << L"Tracing threads=" << thread_ids.size()
             << L" installed_hooks=" << hooks.size()
             << L" hook_failures=" << hook_failures
             << L" hwnd=" << FormatHwnd(target.hwnd)
             << L" duration=" << options.duration_seconds << L"s\n";
  std::wcout << L"Log: " << log_path << L"\n";
  std::wcout << L"Type with MS IME or MilkyWayIME now.\n";

  const auto start = std::chrono::steady_clock::now();
  while (true) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
    if (options.duration_seconds > 0 && elapsed >= options.duration_seconds) {
      break;
    }
    Sleep(20);
  }

  for (HHOOK hook : hooks) {
    UnhookWindowsHookEx(hook);
  }
  FreeLibrary(hook_module);
  if (options.log_all) {
    std::error_code error;
    std::filesystem::remove(log_all_flag_path, error);
  }

  std::wcout << L"Done. Log: " << log_path << L"\n";
  return 0;
}
