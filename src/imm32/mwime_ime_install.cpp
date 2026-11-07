// Tiny elevated helper to install/uninstall the minimal IMM32 test IME under
// the Korean (0412) input language so a CJK host (Nikke) engages it.
// ImmInstallIME insists on registering the IME under English (0409), so we
// register the keyboard-layout entry for a Korean KLID directly. Run elevated.
//
//   mwime_ime_install                 install (copy + register Korean KLID)
//   mwime_ime_install <source.ime>    install from an explicit source path
//   mwime_ime_install --uninstall     remove the file and KLID entries

#include <windows.h>

#include <cstdio>

namespace {

const wchar_t kDefaultSource[] =
    L"D:\\Git\\MilkyWayIME\\build\\MilkyWayIME.Imm32\\x64\\Debug\\mwime_ime.ime";

// E0 = IME, 20 = device slot, 0412 = Korean.
const wchar_t kKoreanKlidPath[] =
    L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts\\E0200412";
// Left over from a prior ImmInstallIME run (English); removed on uninstall.
const wchar_t kEnglishKlidPath[] =
    L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts\\E0200409";

void SystemImePath(wchar_t* path, size_t count) {
  wchar_t system_dir[MAX_PATH] = {};
  GetSystemDirectoryW(system_dir, MAX_PATH);
  swprintf_s(path, count, L"%s\\mwime_ime.ime", system_dir);
}

bool SetString(HKEY key, const wchar_t* name, const wchar_t* value) {
  const DWORD bytes = static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t));
  return RegSetValueExW(key, name, 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(value),
                        bytes) == ERROR_SUCCESS;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  wchar_t dest[MAX_PATH] = {};
  SystemImePath(dest, MAX_PATH);

  if (argc > 1 && lstrcmpiW(argv[1], L"--uninstall") == 0) {
    const BOOL deleted = DeleteFileW(dest);
    const LONG kr =
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, kKoreanKlidPath);
    const LONG en =
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, kEnglishKlidPath);
    wprintf(L"Uninstall: file=%s(err=%lu) koreanKey=%ld englishKey=%ld\n",
            deleted ? L"deleted" : L"missing", GetLastError(), kr, en);
    wprintf(L"Also remove the input method in Settings if it lingers.\n");
    return 0;
  }

  const wchar_t* source = (argc > 1) ? argv[1] : kDefaultSource;

  // A previous copy may be loaded by running processes, which blocks an
  // overwrite. Renaming a loaded file aside is allowed on NTFS, but the aside
  // target must be a fresh (unlocked, non-existent) name - a leftover .old from
  // an earlier run can itself be locked. Find a free suffix and move dest to it.
  if (GetFileAttributesW(dest) != INVALID_FILE_ATTRIBUTES) {
    bool moved = false;
    for (int i = 0; i < 100 && !moved; ++i) {
      wchar_t aside[MAX_PATH] = {};
      swprintf_s(aside, L"%s.old%d", dest, i);
      if (GetFileAttributesW(aside) == INVALID_FILE_ATTRIBUTES) {
        moved = MoveFileExW(dest, aside, 0) != 0;
        if (moved) {
          wprintf(L"Moved locked previous copy aside -> %s\n", aside);
        }
      }
    }
    if (!moved) {
      wprintf(L"Warning: could not move the existing %s aside.\n", dest);
    }
  }

  if (!CopyFileW(source, dest, FALSE)) {
    wprintf(L"CopyFile failed err=%lu (run elevated?)\n  source=%s\n  dest=%s\n",
            GetLastError(), source, dest);
    return 1;
  }
  wprintf(L"Copied %s -> %s\n", source, dest);

  HKEY key = nullptr;
  const LONG status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, kKoreanKlidPath, 0,
                                      nullptr, REG_OPTION_NON_VOLATILE,
                                      KEY_WRITE, nullptr, &key, nullptr);
  if (status != ERROR_SUCCESS) {
    wprintf(L"RegCreateKeyEx failed status=%ld\n", status);
    return 2;
  }
  const bool ime_file = SetString(key, L"IME File", L"mwime_ime.ime");
  const bool layout_text = SetString(key, L"Layout Text", L"MilkyWayIME IMM32 Test");
  const bool layout_file = SetString(key, L"Layout File", L"kbdus.dll");
  RegCloseKey(key);

  wprintf(L"Registered Korean KLID E0200412 (IME File=%d Layout Text=%d "
          L"Layout File=%d)\n",
          ime_file, layout_text, layout_file);
  wprintf(L"Next: Settings/Win+Space should now list 'MilkyWayIME IMM32 Test' "
          L"under Korean. Select it, then restart Nikke.\n");
  return 0;
}
