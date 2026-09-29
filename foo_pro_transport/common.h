#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <windows.h>
#include <windowsx.h>

// foobar2000 SDK exposes COM/OLE based types in playlist/ui headers.
// These declarations MUST be visible before foobar2000.h is included.
// Without them VS2019 reports:
//   interface / IUnknown / IDataObject not declared.
#include <objbase.h>
#include <objidl.h>
#include <ole2.h>
#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <foobar2000/SDK/foobar2000.h>

inline std::wstring pro_transport_utf8_to_wide(const char* s) {
    if (!s || !*s) return {};

    const int n = MultiByteToWideChar(
        CP_UTF8, 0, s, -1, nullptr, 0);

    if (n <= 0) return {};

    std::wstring out((size_t)n, L'\0');

    MultiByteToWideChar(
        CP_UTF8, 0, s, -1,
        out.data(), n);

    if (!out.empty() && out.back() == L'\0')
        out.pop_back();

    return out;
}
