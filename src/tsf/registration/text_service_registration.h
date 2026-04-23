#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <msctf.h>

#include <string>

namespace milkyway::tsf::registration {

inline constexpr CLSID kTextServiceClsid = {
    0x8f3909b2,
    0x9f46,
    0x4e0d,
    {0x92, 0x5a, 0x53, 0xa4, 0x72, 0x0f, 0x86, 0x71},
};

inline constexpr GUID kTextServiceProfileGuid = {
    0x127070df,
    0x9d35,
    0x4b2c,
    {0xaf, 0x2c, 0xea, 0x08, 0xd6, 0x4d, 0x5b, 0x11},
};

inline constexpr LANGID kTextServiceLangid =
    MAKELANGID(LANG_KOREAN, SUBLANG_DEFAULT);

inline constexpr wchar_t kTextServiceDescription[] = L"MilkyWayIME";
inline constexpr wchar_t kThreadingModel[] = L"Apartment";

HRESULT RegisterTextService();
HRESULT UnregisterTextService();
std::wstring ModulePath(HINSTANCE instance);

}  // namespace milkyway::tsf::registration

#endif
