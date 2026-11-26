#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

namespace graphics_ui::core::detail {

inline constexpr auto serial_limit = std::numeric_limits<std::uint64_t>::max();

inline std::uint64_t allocateOwner() noexcept {
    static std::atomic<std::uint64_t> last_owner{};
    auto current = last_owner.load(std::memory_order_relaxed);
    while (current != serial_limit) {
        if (last_owner.compare_exchange_weak(current, current + 1, std::memory_order_relaxed))
            return current + 1;
    }
    return 0;
}

} // namespace graphics_ui::core::detail
