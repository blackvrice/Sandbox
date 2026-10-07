#include "core/ecs/EntityCommandBuffer.hpp"

#include "foundation/log/Log.hpp"

namespace sbx::ecs {

PendingEntity EntityCommandBuffer::createEmpty() {
    const PendingEntity p{m_pendingCount++};
    m_commands.push_back(Command{Kind::Create, EntityRef(p), nullptr});
    return p;
}

void EntityCommandBuffer::destroy(EntityRef target) {
    m_commands.push_back(Command{Kind::Destroy, target, nullptr});
}

EcbApplyStats EntityCommandBuffer::apply(Registry& registry, std::vector<EntityId>* resolvedOut) {
    SBX_VERIFY(!registry.structureLocked(), "ECB 는 구조 잠금이 풀린 동기화 지점에서 적용한다");
    EcbApplyStats stats;

    // 1. 같은 ECB 안에서 파괴되는 pending 엔티티는 상쇄 대상
    std::vector<u8> cancelled(m_pendingCount, 0);
    for (const Command& c : m_commands) {
        if (c.kind == Kind::Destroy && c.target.isPending()) {
            cancelled[c.target.pending().index] = 1;
        }
    }

    // 2. 기록 순서대로 적용
    std::vector<EntityId> resolved(m_pendingCount, kNullEntity);
    const auto resolve = [&](EntityRef ref) -> EntityId {
        return ref.isPending() ? resolved[ref.pending().index] : ref.entity();
    };

    for (Command& c : m_commands) {
        const bool pendingCancelled = c.target.isPending() && cancelled[c.target.pending().index] != 0;
        switch (c.kind) {
        case Kind::Create:
            if (pendingCancelled) {
                ++stats.cancelled;
            } else {
                resolved[c.target.pending().index] = registry.create();
                ++stats.created;
            }
            break;
        case Kind::Destroy:
            if (pendingCancelled) {
                break; // 생성과 함께 상쇄
            }
            if (registry.destroy(resolve(c.target))) {
                ++stats.destroyed;
            } else {
                ++stats.skipped;
            }
            break;
        case Kind::Emplace:
        case Kind::Remove: {
            if (pendingCancelled) {
                break;
            }
            const EntityId e = resolve(c.target);
            if (!registry.alive(e)) {
                log::debug("ecs", "ECB: 죽은 엔티티 {:#x} 에 대한 {} 를 건너뜁니다", e.raw,
                           c.kind == Kind::Emplace ? "emplace" : "remove");
                ++stats.skipped;
                break;
            }
            c.payload->apply(registry, e);
            if (c.kind == Kind::Emplace) {
                ++stats.emplaced;
            } else {
                ++stats.removed;
            }
            break;
        }
        }
    }

    if (resolvedOut != nullptr) {
        *resolvedOut = std::move(resolved);
    }
    clear();
    return stats;
}

} // namespace sbx::ecs
