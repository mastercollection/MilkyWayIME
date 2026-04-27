#pragma once

#if defined(_WIN32) && defined(_DEBUG)

#include <windows.h>

#include <cstdio>
#include <iterator>
#include <string>
#include <string_view>

namespace milkyway::tsf::debug {

inline std::wstring GetDebugLogDirectory() {
  wchar_t temp_path[MAX_PATH] = {};
  const DWORD length = GetTempPathW(static_cast<DWORD>(std::size(temp_path)),
                                    temp_path);
  if (length == 0 || length >= std::size(temp_path)) {
    return L".";
  }

  std::wstring directory(temp_path, temp_path + length);
  if (!directory.empty() &&
      (directory.back() == L'\\' || directory.back() == L'/')) {
    directory.pop_back();
  }

  directory += L"\\MilkyWayIME";
  return directory;
}

inline std::wstring GetDebugLogPath() {
  return GetDebugLogDirectory() + L"\\debug.log";
}

inline std::string WideToUtf8(std::wstring_view text) {
  if (text.empty()) {
    return {};
  }

  const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr,
                                         0, nullptr, nullptr);
  if (length <= 0) {
    return {};
  }

  std::string utf8(static_cast<std::size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      utf8.data(), length, nullptr, nullptr);
  return utf8;
}

inline std::wstring FormatLogLine(std::wstring_view message) {
  SYSTEMTIME local_time = {};
  GetLocalTime(&local_time);

  wchar_t prefix[128] = {};
  swprintf_s(prefix, L"[%04u-%02u-%02u %02u:%02u:%02u.%03u][pid=%lu][tid=%lu] ",
             local_time.wYear, local_time.wMonth, local_time.wDay,
             local_time.wHour, local_time.wMinute, local_time.wSecond,
             local_time.wMilliseconds, GetCurrentProcessId(),
             GetCurrentThreadId());

  std::wstring line(prefix);
  line.append(message);
  line.append(L"\r\n");
  return line;
}

inline void AppendDebugLogFile(std::wstring_view line) {
  const std::wstring directory = GetDebugLogDirectory();
  CreateDirectoryW(directory.c_str(), nullptr);

  const std::wstring path = GetDebugLogPath();
  HANDLE handle = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE |
                                  FILE_SHARE_DELETE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return;
  }

  const std::string utf8 = WideToUtf8(line);
  if (!utf8.empty()) {
    DWORD written = 0;
    WriteFile(handle, utf8.data(), static_cast<DWORD>(utf8.size()), &written,
              nullptr);
  }

  CloseHandle(handle);
}

inline void DebugLog(std::wstring message) {
  const std::wstring line = FormatLogLine(message);
  OutputDebugStringW(line.c_str());
  AppendDebugLogFile(line);
}

}  // namespace milkyway::tsf::debug

#endif
