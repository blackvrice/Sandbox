#pragma once
// platform/windows 내부 공용. windows.h 를 여기서만 끌어온다 (platform/windows 밖에서 include 금지).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <string_view>

namespace sbx::platform::win32 {

[[nodiscard]] inline std::wstring widen(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) {
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    }
    return out;
}

[[nodiscard]] inline std::string narrow(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    const int n =
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), n, nullptr, nullptr);
    }
    return out;
}

// GetLastError 를 사람이 읽는 문장으로 ("(5) 액세스가 거부되었습니다.")
[[nodiscard]] inline std::string lastErrorText(DWORD code = GetLastError()) {
    wchar_t* buf = nullptr;
    const DWORD n =
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, code, 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    std::string text = "(" + std::to_string(code) + ")";
    if (n > 0 && buf != nullptr) {
        std::wstring_view w(buf, n);
        while (!w.empty() && (w.back() == L'\n' || w.back() == L'\r')) {
            w.remove_suffix(1);
        }
        text += " " + narrow(w);
    }
    if (buf != nullptr) {
        LocalFree(buf);
    }
    return text;
}

} // namespace sbx::platform::win32
