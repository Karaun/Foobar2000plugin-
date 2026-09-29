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

// IMPORTANT:
// windows.h normally pulls in the legacy Winsock 1 header (winsock.h).
// foobar2000 SDK uses Winsock 2 in parts of its headers.
// Prevent winsock.h from being included by windows.h, then include
// Winsock 2 FIRST. COM/OLE headers are included explicitly afterwards.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <windows.h>

#include <objbase.h>
#include <objidl.h>
#include <ole2.h>
#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <foobar2000/SDK/foobar2000.h>

namespace eas {

std::wstring utf8_to_wide(const char* text);
std::string wide_to_utf8(const wchar_t* text);

void show_audio_settings_window();

} // namespace eas
