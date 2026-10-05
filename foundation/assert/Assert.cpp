#include "foundation/assert/Assert.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace sbx {
namespace {

void defaultHandler(const AssertInfo& info) {
    std::fprintf(stderr, "%s failed: %s\n  message: %s\n  at %s:%u (%s)\n", info.fatal ? "SBX_VERIFY" : "SBX_ASSERT",
                 info.expression, info.message ? info.message : "", info.location.file_name(),
                 static_cast<unsigned>(info.location.line()), info.location.function_name());
    std::fflush(stderr);
    std::abort();
}

std::atomic<AssertHandler> g_handler{&defaultHandler};

} // namespace

AssertHandler setAssertHandler(AssertHandler handler) noexcept {
    return g_handler.exchange(handler ? handler : &defaultHandler, std::memory_order_acq_rel);
}

namespace detail {

void assertFailed(const char* expression, const char* message, std::source_location location, bool fatal) {
    const AssertInfo info{expression, message, location, fatal};
    g_handler.load(std::memory_order_acquire)(info);
}

} // namespace detail
} // namespace sbx
