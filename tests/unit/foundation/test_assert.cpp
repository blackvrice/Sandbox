#include <doctest/doctest.h>

#include <string>

#include "foundation/assert/Assert.hpp"

namespace {
int g_fired = 0;
bool g_lastFatal = false;
std::string g_lastExpr;

void recordingHandler(const sbx::AssertInfo& info) {
    ++g_fired;
    g_lastFatal = info.fatal;
    g_lastExpr = info.expression;
}

// 기록용 처리기를 설치하고, 범위를 벗어나면 원래 처리기로 되돌린다.
struct ScopedHandler {
    sbx::AssertHandler previous;
    ScopedHandler() : previous(sbx::setAssertHandler(&recordingHandler)) { g_fired = 0; }
    ~ScopedHandler() { sbx::setAssertHandler(previous); }
};
} // namespace

TEST_SUITE("foundation") {

    TEST_CASE("assert: verify fires on false condition regardless of build type") {
        ScopedHandler scope;
        const int x = 3;
        SBX_VERIFY(x == 4, "x must be 4");
        CHECK(g_fired == 1);
        CHECK(g_lastFatal);
        CHECK(g_lastExpr == "x == 4");
    }

    TEST_CASE("assert: verify does not fire on true condition") {
        ScopedHandler scope;
        SBX_VERIFY(1 + 1 == 2, "math");
        CHECK(g_fired == 0);
    }

    TEST_CASE("assert: assert fires only when enabled") {
        ScopedHandler scope;
        int evaluated = 0;
        SBX_ASSERT((++evaluated, false), "should fire in debug");
#if SBX_ENABLE_ASSERTS
        CHECK(g_fired == 1);
        CHECK_FALSE(g_lastFatal);
        CHECK(evaluated == 1);
#else
        // Release: 조건식을 평가하지 않는다
        CHECK(g_fired == 0);
        CHECK(evaluated == 0);
#endif
    }

} // TEST_SUITE
