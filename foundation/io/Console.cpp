#include "foundation/io/Console.hpp"

#ifdef _WIN32
// windows.h 를 끌어오지 않으려고 (Foundation 경계, tools/check_includes.py) 필요한 함수 하나만 선언한다. kernel32.
extern "C" __declspec(dllimport) int __stdcall SetConsoleOutputCP(unsigned int codePageId);
#endif

namespace sbx::console {

void useUtf8Output() noexcept {
#ifdef _WIN32
    (void)SetConsoleOutputCP(65001u); // CP_UTF8. 콘솔이 없으면(리디렉션) 실패해도 무해하다
#endif
}

} // namespace sbx::console
