#pragma once
// Sparse set 컴포넌트 저장소. docs/02-ECS.md 4장.
//
//   sparse  : 페이지(4096 슬롯) 배열. page[index >> 12][index & 4095] = dense 인덱스 또는 kNone.
//             페이지는 처음 쓰일 때 할당한다.
//   dense   : dense[i] 의 주인 EntityId
//   data    : dense 와 같은 순서의 T (AoS)
//   changed : 마지막 Write 접근 때의 Registry::currentTick   added : 붙을 때의 값
//             (SimulationWorld 는 틱 번호가 아니라 변경 순번을 넣는다 — tick() 마다 증가, 일시정지 편집 단계 포함. 복제가
//              "기준 이후 바뀐 것" 을 고를 때 쓴다 — 08 6장, ADR-0025)
//
// 불변식 (validate() 가 검사)
//   P1. sparse[e.index] = i  ⇔  dense[i] == e
//   P2. dense.size() == data.size() == changed.size() == added.size()
//   P3. dense 에 같은 index 가 두 번 나오지 않는다
//
// remove 는 swap-and-pop 이라 dense 순서가 바뀐다. 게임 결과가 이 순서에 의존하면 안 된다 (02-ECS E1).

#include <array>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "core/ecs/Component.hpp"
#include "core/ecs/EntityId.hpp"
#include "core/simulation/SimConstants.hpp"
#include "foundation/assert/Assert.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::ecs {

using sim::Tick;

// 타입을 모르는 쪽(Registry 의 파괴, 직렬화, 해시, 에디터)이 쓰는 인터페이스.
class ComponentPoolBase {
public:
    static constexpr u32 kNone = 0xFFFF'FFFFu;

    virtual ~ComponentPoolBase() = default;

    [[nodiscard]] virtual StableId stableId() const noexcept = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual ComponentTypeId typeId() const noexcept = 0;
    [[nodiscard]] virtual ComponentFlags flags() const noexcept = 0;
    [[nodiscard]] virtual u16 version() const noexcept = 0;

    // 없으면 false
    virtual bool remove(EntityId e) = 0;
    virtual void clear() noexcept = 0;
    // 기본값으로 붙이고 그 주소를 돌려준다 (이미 있으면 기존 주소). 로드·에디터의 동적 생성용.
    virtual void* emplaceDefaultRaw(EntityId e, Tick tick) = 0;
    [[nodiscard]] virtual const void* getRaw(EntityId e) const noexcept = 0;
    [[nodiscard]] virtual void* getRawForWrite(EntityId e, Tick tick) noexcept = 0;
    [[nodiscard]] virtual Expected<void> validate() const = 0;

    [[nodiscard]] usize size() const noexcept { return m_dense.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_dense.empty(); }
    [[nodiscard]] std::span<const EntityId> entities() const noexcept { return m_dense; }
    [[nodiscard]] EntityId entityAt(usize i) const noexcept { return m_dense[i]; }
    [[nodiscard]] Tick changedAt(usize i) const noexcept { return m_changed[i]; }
    [[nodiscard]] Tick addedAt(usize i) const noexcept { return m_added[i]; }

    // e 의 dense 인덱스, 없으면 kNone. generation 까지 비교한다.
    [[nodiscard]] u32 indexOf(EntityId e) const noexcept {
        const u32 idx = e.index();
        const usize page = idx >> kPageShift;
        if (page >= m_sparse.size() || !m_sparse[page]) {
            return kNone;
        }
        const u32 d = (*m_sparse[page])[idx & kPageMask];
        return (d != kNone && m_dense[d] == e) ? d : kNone;
    }
    [[nodiscard]] bool contains(EntityId e) const noexcept { return indexOf(e) != kNone; }

    // tick 이후에 바뀐(또는 붙은) 원소에 fn(EntityId, denseIndex) 호출. dense 순서.
    template <class Fn>
    void forEachChangedSince(Tick baseline, Fn&& fn) const {
        for (usize i = 0; i < m_dense.size(); ++i) {
            if (m_changed[i] > baseline) {
                fn(m_dense[i], i);
            }
        }
    }

protected:
    static constexpr u32 kPageShift = 12;
    static constexpr u32 kPageSize = 1u << kPageShift; // 4096
    static constexpr u32 kPageMask = kPageSize - 1;
    using Page = std::array<u32, kPageSize>;

    void setSparse(EntityId e, u32 denseIndex) {
        const u32 idx = e.index();
        const usize page = idx >> kPageShift;
        if (page >= m_sparse.size()) {
            m_sparse.resize(page + 1);
        }
        if (!m_sparse[page]) {
            m_sparse[page] = std::make_unique<Page>();
            m_sparse[page]->fill(kNone);
        }
        (*m_sparse[page])[idx & kPageMask] = denseIndex;
    }

    void clearSparse(EntityId e) noexcept {
        const u32 idx = e.index();
        (*m_sparse[idx >> kPageShift])[idx & kPageMask] = kNone;
    }

    // 공통 불변식 검사 (P1~P3 중 데이터 배열을 제외한 부분)
    [[nodiscard]] Expected<void> validateIndex(usize dataSize) const;

    std::vector<std::unique_ptr<Page>> m_sparse;
    std::vector<EntityId> m_dense;
    std::vector<Tick> m_changed;
    std::vector<Tick> m_added;
};

template <Component T>
class ComponentPool final : public ComponentPoolBase {
public:
    using Traits = ComponentTraits<T>;

    [[nodiscard]] StableId stableId() const noexcept override { return Traits::kStableId; }
    [[nodiscard]] std::string_view name() const noexcept override { return Traits::kName; }
    [[nodiscard]] ComponentTypeId typeId() const noexcept override { return componentTypeId<T>(); }
    [[nodiscard]] ComponentFlags flags() const noexcept override { return Traits::kFlags; }
    [[nodiscard]] u16 version() const noexcept override { return Traits::kVersion; }

    template <class... Args>
    T& emplace(EntityId e, Tick tick, Args&&... args) {
        SBX_ASSERT(!contains(e), "컴포넌트가 이미 있다 — emplaceOrReplace 를 쓰십시오");
        const auto i = static_cast<u32>(m_dense.size());
        m_data.emplace_back(std::forward<Args>(args)...);
        m_dense.push_back(e);
        m_changed.push_back(tick);
        m_added.push_back(tick);
        setSparse(e, i);
        return m_data.back();
    }

    template <class... Args>
    T& emplaceOrReplace(EntityId e, Tick tick, Args&&... args) {
        const u32 i = indexOf(e);
        if (i == kNone) {
            return emplace(e, tick, std::forward<Args>(args)...);
        }
        m_data[i] = T(std::forward<Args>(args)...);
        m_changed[i] = tick;
        return m_data[i];
    }

    bool remove(EntityId e) override {
        const u32 i = indexOf(e);
        if (i == kNone) {
            return false;
        }
        const usize last = m_dense.size() - 1;
        if (i != last) {
            m_data[i] = std::move(m_data[last]);
            m_dense[i] = m_dense[last];
            m_changed[i] = m_changed[last];
            m_added[i] = m_added[last];
            setSparse(m_dense[i], i);
        }
        clearSparse(e);
        m_data.pop_back();
        m_dense.pop_back();
        m_changed.pop_back();
        m_added.pop_back();
        return true;
    }

    void clear() noexcept override {
        m_sparse.clear();
        m_dense.clear();
        m_data.clear();
        m_changed.clear();
        m_added.clear();
    }

    [[nodiscard]] const T* tryGet(EntityId e) const noexcept {
        const u32 i = indexOf(e);
        return i == kNone ? nullptr : &m_data[i];
    }
    // 쓰기 접근: changed 를 tick 으로 갱신한다
    [[nodiscard]] T* tryGetForWrite(EntityId e, Tick tick) noexcept {
        const u32 i = indexOf(e);
        if (i == kNone) {
            return nullptr;
        }
        m_changed[i] = tick;
        return &m_data[i];
    }

    [[nodiscard]] const T& dataAt(usize i) const noexcept { return m_data[i]; }
    [[nodiscard]] T& dataAtForWrite(usize i, Tick tick) noexcept {
        m_changed[i] = tick;
        return m_data[i];
    }
    [[nodiscard]] std::span<const T> data() const noexcept { return m_data; }

    void* emplaceDefaultRaw(EntityId e, Tick tick) override {
        if (T* existing = tryGetForWrite(e, tick)) {
            return existing;
        }
        return &emplace(e, tick);
    }
    [[nodiscard]] const void* getRaw(EntityId e) const noexcept override { return tryGet(e); }
    [[nodiscard]] void* getRawForWrite(EntityId e, Tick tick) noexcept override { return tryGetForWrite(e, tick); }

    [[nodiscard]] Expected<void> validate() const override { return validateIndex(m_data.size()); }

private:
    std::vector<T> m_data;
};

} // namespace sbx::ecs
