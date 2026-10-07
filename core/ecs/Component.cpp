#include "core/ecs/Component.hpp"

#include <atomic>

#include "foundation/assert/Assert.hpp"

namespace sbx::ecs::detail {

ComponentTypeId allocateComponentTypeId() noexcept {
    static std::atomic<u32> next{0};
    const u32 id = next.fetch_add(1, std::memory_order_relaxed);
    SBX_VERIFY(id < kInvalidComponentTypeId, "컴포넌트 타입 수 초과");
    return static_cast<ComponentTypeId>(id);
}

} // namespace sbx::ecs::detail
