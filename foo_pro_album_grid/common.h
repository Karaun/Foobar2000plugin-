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
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <foobar2000/SDK/foobar2000.h>

inline std::wstring pro_utf8_to_wide_album(const char* s) {
    if (!s || !*s) return {};
    int n = MultiByteToWideChar(
        CP_UTF8, 0, s, -1, nullptr, 0);
    if (n <= 0) return {};

    std::wstring out((size_t)n, L'\0');

    MultiByteToWideChar(
        CP_UTF8, 0, s, -1, out.data(), n);

    if (!out.empty() && out.back() == L'\0')
        out.pop_back();

    return out;
}

inline std::string pro_wide_to_utf8_album(
    const std::wstring& s) {

    if (s.empty()) return {};

    int n = WideCharToMultiByte(
        CP_UTF8, 0,
        s.c_str(), -1,
        nullptr, 0,
        nullptr, nullptr);

    if (n <= 0) return {};

    std::string out((size_t)n, '\0');

    WideCharToMultiByte(
        CP_UTF8, 0,
        s.c_str(), -1,
        out.data(), n,
        nullptr, nullptr);

    if (!out.empty() && out.back() == '\0')
        out.pop_back();

    return out;
}
