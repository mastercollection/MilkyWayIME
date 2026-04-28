#include <windows.h>
#include <tlhelp32.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Options {
  std::wstring target_process = L"nikke.exe";
  std::wstring dll_name = L"mwime_tsf.dll";
  std::wstring output_path;
  int duration_seconds = 120;
  int interval_ms = 250;
  bool print_all = false;
  bool help = false;
};

struct HookEvent {
  DWORD event = 0;
  HWND hwnd = nullptr;
  LONG object_id = 0;
  LONG child_id = 0;
  DWORD event_thread = 0;
  DWORD event_time = 0;
};

struct ProcessEntry {
  DWORD pid = 0;
  std::wstring exe_name;
};

struct WindowInfo {
  HWND hwnd = nullptr;
  DWORD thread_id = 0;
  DWORD process_id = 0;
  std::wstring process_name;
  std::wstring process_path;
  std::wstring class_name;
  std::wstring title;
};

struct TargetStatus {
  bool running = false;
  bool dll_loaded = false;
  std::wstring pids;
  std::wstring detail;
};

struct Snapshot {
  std::wstring timestamp;
  std::wstring trigger;
  HookEvent event;
  WindowInfo foreground;
  WindowInfo focus;
  HWND active_hwnd = nullptr;
  HWND caret_hwnd = nullptr;
  RECT caret_rect{};
  bool gui_thread_info_ok = false;
  DWORD gui_flags = 0;
  HKL keyboard_layout = nullptr;
  TargetStatus target;
};

std::atomic_bool g_stop = false;
std::mutex g_event_mutex;
std::vector<HookEvent> g_events;

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
  if (!EndsWithInsensitive(value, L".exe")) {
    value += L".exe";
  }
  return value;
}

std::wstring FileNameFromPath(const std::wstring& path) {
  const std::size_t slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) {
    return path;
  }
  return path.substr(slash + 1);
}

std::string ToUtf8(const std::wstring& value) {
  if (value.empty()) {
    return {};
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) {
    return {};
  }
  std::string output(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(),
                      static_cast<int>(value.size()), output.data(), size,
                      nullptr, nullptr);
  return output;
}

std::wstring FromNumber(DWORD value) {
  std::wostringstream stream;
  stream << value;
  return stream.str();
}

std::wstring FromLong(LONG value) {
  std::wostringstream stream;
  stream << value;
  return stream.str();
}

std::wstring FormatHwnd(HWND hwnd) {
  std::wostringstream stream;
  stream << L"0x" << std::uppercase << std::hex
         << reinterpret_cast<std::uintptr_t>(hwnd);
  return stream.str();
}

std::wstring FormatHkl(HKL layout) {
  std::wostringstream stream;
  stream << L"0x" << std::uppercase << std::hex
         << reinterpret_cast<std::uintptr_t>(layout);
  return stream.str();
}

std::wstring FormatRect(const RECT& rect) {
  std::wostringstream stream;
  stream << rect.left << L"," << rect.top << L"," << rect.right << L","
         << rect.bottom;
  return stream.str();
}

std::wstring FormatHexDword(DWORD value) {
  std::wostringstream stream;
  stream << L"0x" << std::uppercase << std::hex << value;
  return stream.str();
}

std::wstring NowTimestamp() {
  SYSTEMTIME time{};
  GetLocalTime(&time);
  wchar_t buffer[64] = {};
  swprintf_s(buffer, L"%04u-%02u-%02u %02u:%02u:%02u.%03u", time.wYear,
             time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
             time.wMilliseconds);
  return buffer;
}

std::wstring GetDefaultOutputPath() {
  wchar_t temp_path[MAX_PATH] = {};
  const DWORD length = GetTempPathW(MAX_PATH, temp_path);
  std::wstring output = length > 0 ? std::wstring(temp_path, length) : L".\\";
  output += L"MilkyWayIME\\focus_watch.csv";
  return output;
}

void EnsureOutputDirectory(const std::wstring& path) {
  try {
    const std::filesystem::path output_path(path);
    const std::filesystem::path directory = output_path.parent_path();
    if (!directory.empty()) {
      std::filesystem::create_directories(directory);
    }
  } catch (const std::exception&) {
  }
}

std::wstring GetWindowClassName(HWND hwnd) {
  if (hwnd == nullptr) {
    return {};
  }
  wchar_t class_name[256] = {};
  const int length =
      GetClassNameW(hwnd, class_name, static_cast<int>(std::size(class_name)));
  return length > 0 ? std::wstring(class_name, static_cast<std::size_t>(length))
                    : std::wstring();
}

std::wstring GetWindowTitle(HWND hwnd) {
  if (hwnd == nullptr) {
    return {};
  }
  const int length = GetWindowTextLengthW(hwnd);
  if (length <= 0) {
    return {};
  }
  std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
  const int copied =
      GetWindowTextW(hwnd, title.data(), static_cast<int>(title.size()));
  if (copied <= 0) {
    return {};
  }
  title.resize(static_cast<std::size_t>(copied));
  return title;
}

std::wstring QueryProcessPath(DWORD process_id) {
  if (process_id == 0) {
    return {};
  }
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                               process_id);
  if (process == nullptr) {
    return {};
  }

  std::wstring path(32768, L'\0');
  DWORD size = static_cast<DWORD>(path.size());
  if (!QueryFullProcessImageNameW(process, 0, path.data(), &size)) {
    CloseHandle(process);
    return {};
  }
  CloseHandle(process);
  path.resize(size);
  return path;
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

std::wstring FindProcessName(DWORD process_id) {
  const std::wstring path = QueryProcessPath(process_id);
  if (!path.empty()) {
    return FileNameFromPath(path);
  }

  for (const ProcessEntry& process : EnumerateProcesses()) {
    if (process.pid == process_id) {
      return process.exe_name;
    }
  }
  return {};
}

WindowInfo DescribeWindow(HWND hwnd) {
  WindowInfo info;
  info.hwnd = hwnd;
  if (hwnd == nullptr) {
    return info;
  }

  DWORD process_id = 0;
  info.thread_id = GetWindowThreadProcessId(hwnd, &process_id);
  info.process_id = process_id;
  info.process_path = QueryProcessPath(process_id);
  info.process_name = info.process_path.empty()
                          ? FindProcessName(process_id)
                          : FileNameFromPath(info.process_path);
  info.class_name = GetWindowClassName(hwnd);
  info.title = GetWindowTitle(hwnd);
  return info;
}

struct DllCheck {
  bool loaded = false;
  std::wstring detail;
};

DllCheck CheckDllLoaded(DWORD process_id, const std::wstring& dll_name) {
  DllCheck result;
  HANDLE snapshot = CreateToolhelp32Snapshot(
      TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id);
  if (snapshot == INVALID_HANDLE_VALUE) {
    result.detail = L"pid=" + FromNumber(process_id) +
                    L" snapshot_error=" + FromNumber(GetLastError());
    return result;
  }

  MODULEENTRY32W module{};
  module.dwSize = sizeof(module);
  if (!Module32FirstW(snapshot, &module)) {
    result.detail = L"pid=" + FromNumber(process_id) +
                    L" module_error=" + FromNumber(GetLastError());
    CloseHandle(snapshot);
    return result;
  }

  const std::wstring target = ToLower(dll_name);
  do {
    const std::wstring module_name = ToLower(module.szModule);
    const std::wstring module_path_name =
        ToLower(FileNameFromPath(module.szExePath));
    if (module_name == target || module_path_name == target) {
      result.loaded = true;
      result.detail = L"pid=" + FromNumber(process_id) + L" module=" +
                      module.szExePath;
      break;
    }
    module.dwSize = sizeof(module);
  } while (Module32NextW(snapshot, &module));

  CloseHandle(snapshot);
  if (result.detail.empty()) {
    result.detail = L"pid=" + FromNumber(process_id) + L" not_loaded";
  }
  return result;
}

TargetStatus CaptureTargetStatus(const Options& options) {
  TargetStatus status;
  const std::wstring target_process = ToLower(options.target_process);
  std::vector<std::wstring> pids;
  std::vector<std::wstring> details;

  for (const ProcessEntry& process : EnumerateProcesses()) {
    if (ToLower(process.exe_name) != target_process) {
      continue;
    }

    status.running = true;
    pids.push_back(FromNumber(process.pid));

    const DllCheck dll = CheckDllLoaded(process.pid, options.dll_name);
    details.push_back(dll.detail);
    if (dll.loaded) {
      status.dll_loaded = true;
    }
  }

  for (std::size_t index = 0; index < pids.size(); ++index) {
    if (index != 0) {
      status.pids += L";";
    }
    status.pids += pids[index];
  }

  for (std::size_t index = 0; index < details.size(); ++index) {
    if (index != 0) {
      status.detail += L" | ";
    }
    status.detail += details[index];
  }
  return status;
}

std::wstring EventName(DWORD event) {
  switch (event) {
    case EVENT_SYSTEM_FOREGROUND:
      return L"foreground";
    case EVENT_OBJECT_FOCUS:
      return L"focus";
    default:
      return L"event_" + FromNumber(event);
  }
}

Snapshot CaptureSnapshot(const Options& options, const std::wstring& trigger,
                         const HookEvent& event) {
  Snapshot snapshot;
  snapshot.timestamp = NowTimestamp();
  snapshot.trigger = trigger;
  snapshot.event = event;

  HWND foreground = GetForegroundWindow();
  snapshot.foreground = DescribeWindow(foreground);

  GUITHREADINFO gui{};
  gui.cbSize = static_cast<DWORD>(sizeof(gui));
  if (snapshot.foreground.thread_id != 0 &&
      GetGUIThreadInfo(snapshot.foreground.thread_id, &gui)) {
    snapshot.gui_thread_info_ok = true;
    snapshot.gui_flags = gui.flags;
    snapshot.active_hwnd = gui.hwndActive;
    snapshot.caret_hwnd = gui.hwndCaret;
    snapshot.caret_rect = gui.rcCaret;
    snapshot.focus = DescribeWindow(gui.hwndFocus);
  }

  if (snapshot.foreground.thread_id != 0) {
    snapshot.keyboard_layout = GetKeyboardLayout(snapshot.foreground.thread_id);
  }

  snapshot.target = CaptureTargetStatus(options);
  return snapshot;
}

std::string CsvEscape(const std::wstring& value) {
  std::string utf8 = ToUtf8(value);
  bool needs_quote = false;
  for (const char ch : utf8) {
    if (ch == '"' || ch == ',' || ch == '\n' || ch == '\r') {
      needs_quote = true;
      break;
    }
  }
  if (!needs_quote) {
    return utf8;
  }

  std::string escaped = "\"";
  for (const char ch : utf8) {
    if (ch == '"') {
      escaped += "\"\"";
    } else {
      escaped += ch;
    }
  }
  escaped += "\"";
  return escaped;
}

void WriteHeader(std::ofstream* output) {
  *output << "time,trigger,event,event_hwnd,event_object,event_child,"
          << "event_thread,event_time,fg_hwnd,fg_tid,fg_pid,fg_process,"
          << "fg_class,fg_title,fg_path,focus_hwnd,focus_tid,focus_pid,"
          << "focus_process,focus_class,focus_title,active_hwnd,caret_hwnd,"
          << "caret_rect,gui_ok,gui_flags,keyboard_layout,target_running,"
          << "target_pids,target_dll_loaded,target_dll_detail\n";
}

void WriteSnapshot(std::ofstream* output, const Snapshot& snapshot) {
  const std::wstring gui_ok = snapshot.gui_thread_info_ok ? L"1" : L"0";
  const std::wstring target_running = snapshot.target.running ? L"1" : L"0";
  const std::wstring target_dll_loaded =
      snapshot.target.dll_loaded ? L"1" : L"0";

  *output << CsvEscape(snapshot.timestamp) << ","
          << CsvEscape(snapshot.trigger) << ","
          << CsvEscape(EventName(snapshot.event.event)) << ","
          << CsvEscape(FormatHwnd(snapshot.event.hwnd)) << ","
          << CsvEscape(FromLong(snapshot.event.object_id)) << ","
          << CsvEscape(FromLong(snapshot.event.child_id)) << ","
          << snapshot.event.event_thread << ","
          << snapshot.event.event_time << ","
          << CsvEscape(FormatHwnd(snapshot.foreground.hwnd)) << ","
          << snapshot.foreground.thread_id << ","
          << snapshot.foreground.process_id << ","
          << CsvEscape(snapshot.foreground.process_name) << ","
          << CsvEscape(snapshot.foreground.class_name) << ","
          << CsvEscape(snapshot.foreground.title) << ","
          << CsvEscape(snapshot.foreground.process_path) << ","
          << CsvEscape(FormatHwnd(snapshot.focus.hwnd)) << ","
          << snapshot.focus.thread_id << "," << snapshot.focus.process_id
          << "," << CsvEscape(snapshot.focus.process_name) << ","
          << CsvEscape(snapshot.focus.class_name) << ","
          << CsvEscape(snapshot.focus.title) << ","
          << CsvEscape(FormatHwnd(snapshot.active_hwnd)) << ","
          << CsvEscape(FormatHwnd(snapshot.caret_hwnd)) << ","
          << CsvEscape(FormatRect(snapshot.caret_rect)) << ","
          << CsvEscape(gui_ok) << "," << CsvEscape(FormatHexDword(snapshot.gui_flags))
          << "," << CsvEscape(FormatHkl(snapshot.keyboard_layout)) << ","
          << CsvEscape(target_running) << ","
          << CsvEscape(snapshot.target.pids) << ","
          << CsvEscape(target_dll_loaded) << ","
          << CsvEscape(snapshot.target.detail) << "\n";
  output->flush();
}

std::wstring ConsoleSummary(const Snapshot& snapshot) {
  std::wostringstream stream;
  stream << L"[" << snapshot.timestamp << L"] " << snapshot.trigger
         << L" fg=" << snapshot.foreground.process_name << L"/"
         << snapshot.foreground.class_name << L" "
         << FormatHwnd(snapshot.foreground.hwnd) << L" focus="
         << snapshot.focus.class_name << L" " << FormatHwnd(snapshot.focus.hwnd)
         << L" kl=" << FormatHkl(snapshot.keyboard_layout) << L" target_pids="
         << (snapshot.target.pids.empty() ? L"-" : snapshot.target.pids)
         << L" dll=" << (snapshot.target.dll_loaded ? L"1" : L"0");
  return stream.str();
}

std::wstring StateKey(const Snapshot& snapshot) {
  std::wostringstream stream;
  stream << FormatHwnd(snapshot.foreground.hwnd) << L"|"
         << snapshot.foreground.process_id << L"|"
         << FormatHwnd(snapshot.focus.hwnd) << L"|" << snapshot.focus.process_id
         << L"|" << snapshot.target.pids << L"|"
         << (snapshot.target.dll_loaded ? L"1" : L"0") << L"|"
         << FormatHkl(snapshot.keyboard_layout);
  return stream.str();
}

void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
                           LONG object_id, LONG child_id, DWORD event_thread,
                           DWORD event_time) {
  std::lock_guard<std::mutex> lock(g_event_mutex);
  g_events.push_back(
      HookEvent{event, hwnd, object_id, child_id, event_thread, event_time});
  if (g_events.size() > 128) {
    g_events.erase(g_events.begin());
  }
}

BOOL WINAPI ConsoleCtrlHandler(DWORD control_type) {
  (void)control_type;
  g_stop = true;
  return TRUE;
}

std::vector<HookEvent> DrainEvents() {
  std::lock_guard<std::mutex> lock(g_event_mutex);
  std::vector<HookEvent> events;
  events.swap(g_events);
  return events;
}

bool ParseInt(const wchar_t* text, int* value) {
  if (text == nullptr || value == nullptr) {
    return false;
  }
  wchar_t* end = nullptr;
  const long parsed = wcstol(text, &end, 10);
  if (end == text || *end != L'\0' || parsed < 0 || parsed > 86400000) {
    return false;
  }
  *value = static_cast<int>(parsed);
  return true;
}

void PrintUsage() {
  std::wcout
      << L"Usage:\n"
      << L"  mwime_focus_watch.exe [--process nikke.exe] [--dll mwime_tsf.dll]\n"
      << L"                         [--duration seconds] [--interval ms]\n"
      << L"                         [--out path] [--all]\n\n"
      << L"Defaults:\n"
      << L"  --duration 120, --interval 250, --out %TEMP%\\MilkyWayIME\\focus_watch.csv\n"
      << L"  Use --duration 0 to run until Ctrl+C.\n";
}

Options ParseArgs(int argc, wchar_t** argv) {
  Options options;
  options.output_path = GetDefaultOutputPath();

  for (int index = 1; index < argc; ++index) {
    const std::wstring arg = argv[index];
    if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
      options.help = true;
    } else if ((arg == L"--process" || arg == L"--target") &&
               index + 1 < argc) {
      options.target_process = NormalizeProcessName(argv[++index]);
    } else if (arg == L"--dll" && index + 1 < argc) {
      options.dll_name = argv[++index];
    } else if (arg == L"--out" && index + 1 < argc) {
      options.output_path = argv[++index];
    } else if (arg == L"--duration" && index + 1 < argc) {
      if (!ParseInt(argv[++index], &options.duration_seconds)) {
        options.help = true;
      }
    } else if (arg == L"--interval" && index + 1 < argc) {
      if (!ParseInt(argv[++index], &options.interval_ms) ||
          options.interval_ms < 50) {
        options.help = true;
      }
    } else if (arg == L"--all") {
      options.print_all = true;
    } else {
      options.help = true;
    }
  }
  return options;
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

  EnsureOutputDirectory(options.output_path);
  std::ofstream output(std::filesystem::path(options.output_path),
                       std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    std::wcerr << L"Failed to open output file: " << options.output_path << L"\n";
    return 1;
  }
  WriteHeader(&output);

  SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
  HWINEVENTHOOK foreground_hook =
      SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                      WinEventProc, 0, 0,
                      WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
  HWINEVENTHOOK focus_hook =
      SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS, nullptr,
                      WinEventProc, 0, 0,
                      WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

  std::wcout << L"Writing " << options.output_path << L"\n";
  std::wcout << L"Watching process=" << options.target_process
             << L" dll=" << options.dll_name << L" duration="
             << options.duration_seconds << L"s interval=" << options.interval_ms
             << L"ms\n";
  if (foreground_hook == nullptr || focus_hook == nullptr) {
    std::wcout << L"WinEvent hook setup failed; polling will still run. error="
               << GetLastError() << L"\n";
  }

  const auto start = std::chrono::steady_clock::now();
  auto next_poll = start;
  std::wstring previous_state;

  const HookEvent initial_event{};
  const Snapshot initial = CaptureSnapshot(options, L"start", initial_event);
  WriteSnapshot(&output, initial);
  std::wcout << ConsoleSummary(initial) << L"\n";
  previous_state = StateKey(initial);

  while (!g_stop.load()) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }

    for (const HookEvent& event : DrainEvents()) {
      const Snapshot snapshot =
          CaptureSnapshot(options, EventName(event.event), event);
      WriteSnapshot(&output, snapshot);
      const std::wstring state = StateKey(snapshot);
      if (options.print_all || state != previous_state ||
          snapshot.foreground.process_name == options.target_process ||
          snapshot.target.dll_loaded) {
        std::wcout << ConsoleSummary(snapshot) << L"\n";
      }
      previous_state = state;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now >= next_poll) {
      const Snapshot snapshot = CaptureSnapshot(options, L"poll", HookEvent{});
      WriteSnapshot(&output, snapshot);
      const std::wstring state = StateKey(snapshot);
      if (options.print_all || state != previous_state ||
          snapshot.foreground.process_name == options.target_process ||
          snapshot.target.dll_loaded) {
        std::wcout << ConsoleSummary(snapshot) << L"\n";
      }
      previous_state = state;
      next_poll =
          now + std::chrono::milliseconds(static_cast<long long>(options.interval_ms));
    }

    if (options.duration_seconds > 0) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
          now - start);
      if (elapsed.count() >= options.duration_seconds) {
        break;
      }
    }
    Sleep(20);
  }

  if (focus_hook != nullptr) {
    UnhookWinEvent(focus_hook);
  }
  if (foreground_hook != nullptr) {
    UnhookWinEvent(foreground_hook);
  }

  std::wcout << L"Done. CSV: " << options.output_path << L"\n";
  return 0;
}
