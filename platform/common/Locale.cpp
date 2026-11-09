// OS 로케일의 POSIX 환경 변수 차례 (v0.7 M4-T1). Windows 의 userLocale 은 Win32Platform.cpp (API).
#include <cstdlib>

#include "platform/common/Platform.hpp"

namespace sbx::platform {

std::string localeFromEnvironment() {
    // setlocale 과 같은 차례 — 비거나 "C" · "POSIX" 면 다음 (C 는 로케일이 아니다)
    for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
#if defined(_MSC_VER)
        char* value = nullptr;
        usize size = 0;
        if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
            continue;
        }
        const std::string v(value);
        std::free(value);
#else
        const char* raw = std::getenv(name);
        if (raw == nullptr) {
            continue;
        }
        const std::string v(raw);
#endif
        if (!v.empty() && v != "C" && v != "POSIX" && !v.starts_with("C.")) {
            return v;
        }
    }
    return {};
}

} // namespace sbx::platform
