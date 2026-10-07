#pragma once
// 한 월드의 ECS 저장소. docs/02-ECS.md 5장.
//
// 구조 변경(create/destroy/emplace/remove)은 동기화 지점에서만 한다. System 실행 중에는
// StructuralLockGuard 가 잠그고, 그 동안의 구조 변경은 EntityCommandBuffer 로만 한다.
//   잠금 중 create  → Debug 단언 / Release: 오류 로그 + kNullEntity
//   잠금 중 destroy/remove → Debug 단언 / Release: 오류 로그 + 무시
//   잠금 중 emplace → 항상 중단 (참조를 돌려줘야 하므로 무시할 방법이 없다)

#include <memory>
#include <span>
#include <type_traits>
#include <vector>

#include "core/ecs/ComponentPool.hpp"
#include "core/ecs/EntityManager.hpp"
#include "core/ecs/View.hpp"
#include "foundation/assert/Assert.hpp"

namespace sbx::ecs {

namespace detail {
u32 allocateResourceTypeId() noexcept;
template <class R>
u32 resourceTypeId() noexcept {
    static const u32 id = allocateResourceTypeId();
    return id;
}
struct ResourceHolderBase {
    virtual ~ResourceHolderBase() = default;
};
template <class R>
struct ResourceHolder final : ResourceHolderBase {
    template <class... A>
    explicit ResourceHolder(A&&... a) : value(std::forward<A>(a)...) {}
    R value;
};
} // namespace detail

class Registry {
public:
    Registry() = default;
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;
    Registry(Registry&&) noexcept = default;
    Registry& operator=(Registry&&) noexcept = default;
    ~Registry() = default;

    // --- 엔티티 -----------------------------------------------------------
    [[nodiscard]] EntityId create();
    // 죽은 핸들이면 false. 모든 풀에서 제거하고 generation 을 올린 뒤 destroyedThisTick 에 기록한다.
    bool destroy(EntityId e);
    [[nodiscard]] bool alive(EntityId e) const noexcept { return m_entities.alive(e); }
    [[nodiscard]] usize aliveCount() const noexcept { return m_entities.aliveCount(); }

    // 살아 있는 엔티티를 index 오름차순으로 순회 — 해시·저장처럼 순서가 결과에 들어가는 곳에서 쓴다 (02-ECS E2).
    template <class Fn>
    void forEachEntityByIndex(Fn&& fn) const {
        const usize n = m_entities.slotCount();
        for (usize i = 0; i < n; ++i) {
            const EntityId e = m_entities.current(static_cast<u32>(i));
            if (e.valid()) {
                fn(e);
            }
        }
    }

    // --- 컴포넌트: 구조 변경 (동기화 지점 전용) ---------------------------------
    template <Component T, class... Args>
    T& emplace(EntityId e, Args&&... args) {
        SBX_VERIFY(!structureLocked(), "System 실행 중 emplace — EntityCommandBuffer 를 쓰십시오");
        SBX_VERIFY(alive(e), "죽은 엔티티에 emplace");
        return pool<T>().emplace(e, m_tick, std::forward<Args>(args)...);
    }

    template <Component T, class... Args>
    T& emplaceOrReplace(EntityId e, Args&&... args) {
        SBX_VERIFY(!structureLocked(), "System 실행 중 emplaceOrReplace — EntityCommandBuffer 를 쓰십시오");
        SBX_VERIFY(alive(e), "죽은 엔티티에 emplaceOrReplace");
        return pool<T>().emplaceOrReplace(e, m_tick, std::forward<Args>(args)...);
    }

    template <Component T>
    bool remove(EntityId e) {
        if (!checkStructuralChange("remove")) {
            return false;
        }
        auto* p = findPool<T>();
        if (p == nullptr || !p->remove(e)) {
            return false;
        }
        return true;
    }

    // --- 컴포넌트: 접근 (언제나 가능) --------------------------------------
    template <Component T>
    [[nodiscard]] bool has(EntityId e) const noexcept {
        const auto* p = findPool<T>();
        return p != nullptr && p->contains(e);
    }

    template <Component T>
    [[nodiscard]] const T* tryRead(EntityId e) const noexcept {
        const auto* p = findPool<T>();
        return p == nullptr ? nullptr : p->tryGet(e);
    }

    template <Component T>
    [[nodiscard]] const T& read(EntityId e) const noexcept {
        const T* c = tryRead<T>(e);
        SBX_VERIFY(c != nullptr, "read: 컴포넌트가 없다");
        return *c;
    }

    // 쓰기 접근 — changed 틱을 현재 틱으로 갱신한다 (복제·증분 저장의 근거)
    template <Component T>
    [[nodiscard]] T* tryWrite(EntityId e) noexcept {
        auto* p = findPool<T>();
        return p == nullptr ? nullptr : p->tryGetForWrite(e, m_tick);
    }

    template <Component T>
    [[nodiscard]] T& write(EntityId e) noexcept {
        T* c = tryWrite<T>(e);
        SBX_VERIFY(c != nullptr, "write: 컴포넌트가 없다");
        return *c;
    }

    // --- 순회 --------------------------------------------------------------
    // view<Write<A>, Read<B>, Exclude<C>>()  — 02-ECS 6장
    template <class... Access>
    [[nodiscard]] auto view() {
        return makeView<Access...>(*this);
    }

    // --- 풀 ----------------------------------------------------------------
    template <Component T>
    [[nodiscard]] ComponentPool<T>* findPool() noexcept {
        const ComponentTypeId id = componentTypeId<T>();
        return id < m_pools.size() ? static_cast<ComponentPool<T>*>(m_pools[id].get()) : nullptr;
    }
    template <Component T>
    [[nodiscard]] const ComponentPool<T>* findPool() const noexcept {
        const ComponentTypeId id = componentTypeId<T>();
        return id < m_pools.size() ? static_cast<const ComponentPool<T>*>(m_pools[id].get()) : nullptr;
    }
    // 없으면 만든다
    template <Component T>
    ComponentPool<T>& pool() {
        const ComponentTypeId id = componentTypeId<T>();
        if (id >= m_pools.size()) {
            m_pools.resize(static_cast<usize>(id) + 1);
        }
        if (!m_pools[id]) {
            m_pools[id] = std::make_unique<ComponentPool<T>>();
        }
        return static_cast<ComponentPool<T>&>(*m_pools[id]);
    }

    // 동적 접근 (직렬화·해시·에디터). 없는 슬롯은 nullptr.
    [[nodiscard]] ComponentPoolBase* poolByStableId(StableId id) noexcept;
    [[nodiscard]] const ComponentPoolBase* poolByStableId(StableId id) const noexcept;
    // 존재하는 풀을 stableId 오름차순으로 — 순서가 결과에 들어가는 곳(해시·저장)은 이것을 쓴다.
    [[nodiscard]] std::vector<const ComponentPoolBase*> poolsByStableId() const;
    // 동적 풀 등록 (카탈로그가 타입을 아는 경우). 이미 있으면 기존 풀.
    ComponentPoolBase& adoptPool(std::unique_ptr<ComponentPoolBase> pool);

    // --- 리소스 (월드 단위 싱글턴) -------------------------------------------
    template <class R, class... Args>
    R& emplaceResource(Args&&... args) {
        const u32 id = detail::resourceTypeId<R>();
        if (id >= m_resources.size()) {
            m_resources.resize(static_cast<usize>(id) + 1);
        }
        m_resources[id] = std::make_unique<detail::ResourceHolder<R>>(std::forward<Args>(args)...);
        return static_cast<detail::ResourceHolder<R>&>(*m_resources[id]).value;
    }
    template <class R>
    [[nodiscard]] R* tryResource() noexcept {
        const u32 id = detail::resourceTypeId<R>();
        if (id >= m_resources.size() || !m_resources[id]) {
            return nullptr;
        }
        return &static_cast<detail::ResourceHolder<R>&>(*m_resources[id]).value;
    }
    template <class R>
    [[nodiscard]] R& resource() noexcept {
        R* r = tryResource<R>();
        SBX_VERIFY(r != nullptr, "리소스가 등록되지 않았다");
        return *r;
    }

    // --- 틱 · 로그 ---------------------------------------------------------
    void setCurrentTick(Tick t) noexcept { m_tick = t; }
    [[nodiscard]] Tick currentTick() const noexcept { return m_tick; }
    // 이번 틱에 만들어진/파괴된 엔티티 (기록 순서). 같은 틱에 만들고 파괴한 엔티티는 둘 다에 나온다.
    [[nodiscard]] std::span<const EntityId> createdThisTick() const noexcept { return m_created; }
    [[nodiscard]] std::span<const EntityId> destroyedThisTick() const noexcept { return m_destroyed; }
    void clearTickLogs() noexcept {
        m_created.clear();
        m_destroyed.clear();
    }

    // --- 구조 변경 잠금 -----------------------------------------------------
    void lockStructure() noexcept { ++m_structureLock; }
    void unlockStructure() noexcept {
        SBX_ASSERT(m_structureLock > 0, "unlockStructure without lock");
        --m_structureLock;
    }
    [[nodiscard]] bool structureLocked() const noexcept { return m_structureLock > 0; }

    // 모든 엔티티·컴포넌트·리소스 제거. 풀 슬롯과 타입 인덱스는 유지한다.
    void clear();

    // 테스트 전용
    [[nodiscard]] EntityManager& debugEntityManager() noexcept { return m_entities; }

private:
    // 구조 변경 허용 여부. 잠겨 있으면 Debug 단언, Release 는 로그 후 false.
    bool checkStructuralChange(const char* what) const;

    EntityManager m_entities;
    std::vector<std::unique_ptr<ComponentPoolBase>> m_pools; // ComponentTypeId 인덱스
    std::vector<std::unique_ptr<detail::ResourceHolderBase>> m_resources;
    std::vector<EntityId> m_created;
    std::vector<EntityId> m_destroyed;
    Tick m_tick = 0;
    u32 m_structureLock = 0;
};

// RAII 구조 변경 잠금. SystemScheduler 가 System 실행 동안 건다.
class StructuralLockGuard {
public:
    explicit StructuralLockGuard(Registry& r) noexcept : m_registry(r) { m_registry.lockStructure(); }
    ~StructuralLockGuard() { m_registry.unlockStructure(); }
    StructuralLockGuard(const StructuralLockGuard&) = delete;
    StructuralLockGuard& operator=(const StructuralLockGuard&) = delete;

private:
    Registry& m_registry;
};

} // namespace sbx::ecs
