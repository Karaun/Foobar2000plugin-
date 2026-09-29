#include "common.h"

namespace eas {

std::wstring utf8_to_wide(const char* text) {
    if (text == nullptr || *text == 0) return std::wstring();
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (needed <= 1) return std::wstring();
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, out.data(), needed);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

std::string wide_to_utf8(const wchar_t* text) {
    if (text == nullptr || *text == 0) return std::string();
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return std::string();
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), needed, nullptr, nullptr);
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

} // namespace eas
