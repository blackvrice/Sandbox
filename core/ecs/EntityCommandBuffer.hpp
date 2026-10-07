#pragma once
// 구조 변경 지연 버퍼. docs/02-ECS.md 7장.
//
//   auto child = ecb.createEmpty();
//   ecb.emplace(child, Transform{pos});
//   ecb.destroy(victim);
//   …  (System 순회가 끝난 뒤, 동기화 지점에서)
//   ecb.apply(registry);
//
// 규칙
// - 적용 순서 = 기록 순서. 여러 ECB 는 System 실행 순서대로 적용한다 (결정론).
// - PendingEntity 는 그 ECB 안에서만 유효하다. apply 에서 실제 EntityId 로 바뀐다.
// - 같은 ECB 에서 만들고 파괴한 엔티티는 상쇄된다 — 만들어지지도, 파괴 로그에 남지도 않는다.
// - 이미 죽은 엔티티 destroy → 무시 (두 System 이 같은 엔티티를 죽일 수 있다).
// - 죽은 엔티티에 emplace → 무시 + Debug 로그.
// - emplace 는 "있으면 교체" 의미다 (emplaceOrReplace).
//
// [Phase 15 후보] 명령마다 힙 할당이 있다. 측정에서 문제가 되면 선형 아레나로 바꾼다.

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/ecs/Registry.hpp"

namespace sbx::ecs {

struct PendingEntity {
    u32 index = 0;
    friend constexpr bool operator==(PendingEntity, PendingEntity) noexcept = default;
};

// EntityId 또는 PendingEntity
class EntityRef {
public:
    constexpr EntityRef(EntityId e) noexcept
        : m_value(e.raw), m_pending(false) {} // NOLINT(google-explicit-constructor)
    constexpr EntityRef(PendingEntity p) noexcept
        : m_value(p.index), m_pending(true) {} // NOLINT(google-explicit-constructor)
    [[nodiscard]] constexpr bool isPending() const noexcept { return m_pending; }
    [[nodiscard]] constexpr EntityId entity() const noexcept { return EntityId{m_value}; }
    [[nodiscard]] constexpr PendingEntity pending() const noexcept { return PendingEntity{static_cast<u32>(m_value)}; }

private:
    u64 m_value;
    bool m_pending;
};

struct EcbApplyStats {
    usize created = 0;
    usize destroyed = 0;
    usize emplaced = 0;
    usize removed = 0;
    usize cancelled = 0; // 같은 ECB 안에서 생성→파괴되어 상쇄된 엔티티
    usize skipped = 0;   // 죽은 대상에 대한 명령
};

class EntityCommandBuffer {
public:
    EntityCommandBuffer() = default;
    EntityCommandBuffer(EntityCommandBuffer&&) noexcept = default;
    EntityCommandBuffer& operator=(EntityCommandBuffer&&) noexcept = default;
    EntityCommandBuffer(const EntityCommandBuffer&) = delete;
    EntityCommandBuffer& operator=(const EntityCommandBuffer&) = delete;
    ~EntityCommandBuffer() = default;

    [[nodiscard]] PendingEntity createEmpty();
    void destroy(EntityRef target);

    template <Component T>
    void emplace(EntityRef target, T value) {
        m_commands.push_back(Command{Kind::Emplace, target, std::make_unique<EmplacePayload<T>>(std::move(value))});
    }

    template <Component T>
    void remove(EntityRef target) {
        m_commands.push_back(Command{Kind::Remove, target, std::make_unique<RemovePayload<T>>()});
    }

    // 기록된 명령을 적용하고 비운다. 레지스트리는 구조 잠금 상태가 아니어야 한다.
    // resolvedOut 이 주어지면 PendingEntity.index 순으로 실제 EntityId 를 적는다 (상쇄된 것은 kNullEntity).
    EcbApplyStats apply(Registry& registry, std::vector<EntityId>* resolvedOut = nullptr);

    [[nodiscard]] bool empty() const noexcept { return m_commands.empty(); }
    [[nodiscard]] usize commandCount() const noexcept { return m_commands.size(); }
    void clear() noexcept {
        m_commands.clear();
        m_pendingCount = 0;
    }

private:
    enum class Kind : u8 { Create, Destroy, Emplace, Remove };

    struct Payload {
        virtual ~Payload() = default;
        virtual void apply(Registry& r, EntityId e) = 0;
    };
    template <class T>
    struct EmplacePayload final : Payload {
        explicit EmplacePayload(T v) : value(std::move(v)) {}
        void apply(Registry& r, EntityId e) override { r.emplaceOrReplace<T>(e, std::move(value)); }
        T value;
    };
    template <class T>
    struct RemovePayload final : Payload {
        void apply(Registry& r, EntityId e) override { r.remove<T>(e); }
    };

    struct Command {
        Kind kind;
        EntityRef target;
        std::unique_ptr<Payload> payload;
    };

    std::vector<Command> m_commands;
    u32 m_pendingCount = 0;
};

} // namespace sbx::ecs
