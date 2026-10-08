#include "foundation/io/Console.hpp"

#ifdef _WIN32
// windows.h 를 끌어오지 않으려고 (Foundation 경계, tools/check_includes.py) 필요한 함수만 선언한다. kernel32 · shell32.
extern "C" {
__declspec(dllimport) int __stdcall SetConsoleOutputCP(unsigned int codePageId);
__declspec(dllimport) wchar_t* __stdcall GetCommandLineW();
__declspec(dllimport) wchar_t** __stdcall CommandLineToArgvW(const wchar_t* cmdLine, int* numArgs);
__declspec(dllimport) int __stdcall WideCharToMultiByte(unsigned int codePage, unsigned long flags, const wchar_t* wide,
                                                       int wideChars, char* out, int outBytes, const char* defaultChar,
                                                       int* usedDefaultChar);
__declspec(dllimport) void* __stdcall LocalFree(void* mem);
}
#endif

namespace sbx::console {

void useUtf8Output() noexcept {
#ifdef _WIN32
    (void)SetConsoleOutputCP(65001u); // CP_UTF8. 콘솔이 없으면(리디렉션) 실패해도 무해하다
#endif
}

std::vector<std::string> utf8Arguments(int argc, char** argv) {
#ifdef _WIN32
    int n = 0;
    if (wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &n); wide != nullptr) {
        std::vector<std::string> out;
        out.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            const int bytes = WideCharToMultiByte(65001u, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
            std::string s(bytes > 0 ? static_cast<std::size_t>(bytes) : 1u, '\0');
            if (bytes > 0) {
                (void)WideCharToMultiByte(65001u, 0, wide[i], -1, s.data(), bytes, nullptr, nullptr);
            }
            s.resize(s.size() - 1); // 끝 NUL
            out.push_back(std::move(s));
        }
        (void)LocalFree(wide);
        if (n == argc) {
            return out;
        }
        // 개수가 다르면(런처가 바꾼 명령줄 등) argv 를 믿는다
    }
#endif
    return {argv, argv + argc};
}

} // namespace sbx::console
