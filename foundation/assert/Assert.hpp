#pragma once
// 불변식 검사. docs/12-CODING-STANDARDS.md 5장.
//
//   SBX_ASSERT(cond, "msg")  Debug(또는 SBX_ENABLE_ASSERTS=1)에서만 평가. Release 에서는 cond 를 평가하지 않는다.
//   SBX_VERIFY(cond, "msg")  항상 평가. 실패하면 로그 후 중단.
//
// 외부 입력(파일·네트워크·콘텐츠)의 오류를 assert 로 처리하지 않는다 — Expected<T> 를 쓴다.

#include <source_location>

#ifndef SBX_ENABLE_ASSERTS
#ifdef NDEBUG
#define SBX_ENABLE_ASSERTS 0
#else
#define SBX_ENABLE_ASSERTS 1
#endif
#endif

namespace sbx {

struct AssertInfo {
    const char* expression;
    const char* message;
    std::source_location location;
    bool fatal; // SBX_VERIFY 는 true
};

// 실패 처리기. 기본 처리기는 stderr 에 출력한 뒤 std::abort() 한다.
// 처리기가 반환하면 실행이 계속된다 — 테스트에서 "단언이 발생했는가"를 확인할 때만 쓴다.
using AssertHandler = void (*)(const AssertInfo&);

// 이전 처리기를 돌려준다. nullptr 을 넘기면 기본 처리기로 돌아간다. 스레드 안전.
AssertHandler setAssertHandler(AssertHandler handler) noexcept;

namespace detail {
void assertFailed(const char* expression, const char* message, std::source_location location, bool fatal);
} // namespace detail

} // namespace sbx

#if SBX_ENABLE_ASSERTS
#define SBX_ASSERT(cond, msg)                                                                                          \
    do {                                                                                                               \
        if (!(cond)) [[unlikely]] {                                                                                    \
            ::sbx::detail::assertFailed(#cond, (msg), ::std::source_location::current(), false);                       \
        }                                                                                                              \
    } while (false)
#else
// cond 를 평가하지 않지만 문법 검사는 받게 한다 (Release 에서만 깨지는 오타 방지).
#define SBX_ASSERT(cond, msg)                                                                                          \
    do {                                                                                                               \
        (void)sizeof(!(cond));                                                                                         \
    } while (false)
#endif

#define SBX_VERIFY(cond, msg)                                                                                          \
    do {                                                                                                               \
        if (!(cond)) [[unlikely]] {                                                                                    \
            ::sbx::detail::assertFailed(#cond, (msg), ::std::source_location::current(), true);                        \
        }                                                                                                              \
    } while (false)
