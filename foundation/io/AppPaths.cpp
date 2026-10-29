#include "foundation/io/AppPaths.hpp"

#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
// windows.h 를 끌어오지 않으려고 (Foundation 경계) 필요한 함수만 선언한다. kernel32
extern "C" {
__declspec(dllimport) unsigned long __stdcall GetModuleFileNameW(void* module, wchar_t* fileName, unsigned long size);
}
#elif defined(__APPLE__)
// <mach-o/dyld.h> 대신 선언만 (libSystem)
extern "C" int _NSGetExecutablePath(char* buf, std::uint32_t* bufsize);
#endif

namespace sbx::io {

namespace fs = std::filesystem;

fs::path executablePath() {
#ifdef _WIN32
    std::vector<wchar_t> buf(1024);
    for (int attempt = 0; attempt < 6; ++attempt) {
        const unsigned long n = GetModuleFileNameW(nullptr, buf.data(), static_cast<unsigned long>(buf.size()));
        if (n == 0) {
            return {};
        }
        if (n < buf.size()) { // 잘리지 않았다 (잘리면 n == size)
            return fs::path(std::wstring(buf.data(), n));
        }
        buf.resize(buf.size() * 2);
    }
    return {};
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    (void)_NSGetExecutablePath(nullptr, &size); // 필요한 크기
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) {
        return {};
    }
    buf.resize(buf.find('\0') == std::string::npos ? buf.size() : buf.find('\0'));
    std::error_code ec;
    const fs::path p = fs::weakly_canonical(fs::path(buf), ec);
    return ec ? fs::path(buf) : p;
#else
    std::error_code ec;
    const fs::path p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::path() : p;
#endif
}

fs::path dataDir(std::string_view name, const fs::path& buildDefault) {
    const fs::path exe = executablePath();
    if (!exe.empty()) {
        const fs::path beside = exe.parent_path() / fs::path(std::u8string(name.begin(), name.end()));
        std::error_code ec;
        if (fs::is_directory(beside, ec)) {
            return beside;
        }
    }
    return buildDefault;
}

} // namespace sbx::io
