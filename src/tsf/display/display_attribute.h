#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <msctf.h>

namespace milkyway::tsf::display {

extern const GUID kComposingLastSyllableDisplayAttributeGuid;

HRESULT CreateEnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** enum_info);
HRESULT CreateDisplayAttributeInfo(REFGUID guid,
                                   ITfDisplayAttributeInfo** info);
HRESULT RegisterComposingLastSyllableDisplayAttributeAtom(TfGuidAtom* atom);

}  // namespace milkyway::tsf::display

#endif
