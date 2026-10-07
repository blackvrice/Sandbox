#pragma once
// 컴포넌트 조합 순회. docs/02-ECS.md 6장.
//
//   for (auto [e, tr, vel] : reg.view<Write<Transform>, Read<Velocity>, Exclude<Frozen>>()) { … }
//   reg.view<Read<Health>>().each([](EntityId e, const Health& h) { … });
//
// V1 드라이버 = 포함 컴포넌트 중 가장 작은 풀. 나머지는 indexOf 로 확인한다.
// V2 Read<T> → const T&,  Write<T> → T& 이고 Write 는 그 엔티티의 changed 틱을 갱신한다.
// V3 Exclude<T> 를 가진 엔티티는 건너뛴다.
// V4 순회 중 구조 변경 금지. Debug 에서 드라이버 크기가 바뀌면 단언한다.
// V5 순회 순서는 드라이버 dense 순서다. 게임 결과가 이 순서에 의존하면 안 된다.
// V7 View 는 경량 값이다. 저장해 두지 않는다.

#include <array>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include "core/ecs/ComponentPool.hpp"
#include "foundation/assert/Assert.hpp"

namespace sbx::ecs {

template <Component T>
struct Read {
    using type = T;
    static constexpr bool kWrite = false;
};
template <Component T>
struct Write {
    using type = T;
    static constexpr bool kWrite = true;
};
template <Component T>
struct Exclude {
    using type = T;
};

namespace detail {

template <class A>
struct IsInclude : std::false_type {};
template <class T>
struct IsInclude<Read<T>> : std::true_type {};
template <class T>
struct IsInclude<Write<T>> : std::true_type {};

template <class A>
struct IsExclude : std::false_type {};
template <class T>
struct IsExclude<Exclude<T>> : std::true_type {};

template <class A>
using IncludeTuple = std::conditional_t<IsInclude<A>::value, std::tuple<A>, std::tuple<>>;
template <class A>
using ExcludeTuple = std::conditional_t<IsExclude<A>::value, std::tuple<A>, std::tuple<>>;

template <class A>
using AccessRef = std::conditional_t<A::kWrite, typename A::type&, const typename A::type&>;

} // namespace detail

template <class IncludeList, class ExcludeList>
class BasicView;

template <class... Is, class... Es>
class BasicView<std::tuple<Is...>, std::tuple<Es...>> {
    static_assert(sizeof...(Is) > 0, "view 에는 Read<T> 또는 Write<T> 가 하나 이상 있어야 한다");
    static constexpr usize kInc = sizeof...(Is);
    static constexpr usize kExc = sizeof...(Es);

public:
    using value_type = std::tuple<EntityId, detail::AccessRef<Is>...>;

    BasicView(std::tuple<ComponentPool<typename Is::type>*...> pools,
              std::array<const ComponentPoolBase*, kExc> excludes, Tick tick) noexcept
        : m_pools(pools), m_excludes(excludes), m_tick(tick) {
        // 포함 풀 중 하나라도 없으면 결과가 없다
        bool allPresent = true;
        std::apply([&](auto*... p) { ((allPresent = allPresent && p != nullptr), ...); }, m_pools);
        if (!allPresent) {
            return;
        }
        std::apply(
            [&](auto*... p) {
                ((m_driver == nullptr || p->size() < m_driver->size() ? (void)(m_driver = p) : (void)0), ...);
            },
            m_pools);
    }

    class Iterator {
    public:
        using value_type = BasicView::value_type;
        using difference_type = std::ptrdiff_t;

        Iterator() = default;
        Iterator(const BasicView* view, usize pos) noexcept : m_view(view), m_pos(pos) {
            if (m_view != nullptr && m_view->m_driver != nullptr) {
                m_startSize = m_view->m_driver->size();
            }
            skipInvalid();
        }

        value_type operator*() const {
            SBX_ASSERT(m_view->m_driver->size() == m_startSize, "view 순회 중 구조 변경 (V4)");
            return m_view->deref(m_entity, m_indices, std::index_sequence_for<Is...>{});
        }

        Iterator& operator++() {
            ++m_pos;
            skipInvalid();
            return *this;
        }
        void operator++(int) { ++*this; }

        friend bool operator==(const Iterator& a, const Iterator& b) noexcept { return a.m_pos == b.m_pos; }

    private:
        void skipInvalid() {
            if (m_view == nullptr || m_view->m_driver == nullptr) {
                return;
            }
            const usize n = m_view->m_driver->size();
            while (m_pos < n) {
                m_entity = m_view->m_driver->entityAt(m_pos);
                if (m_view->matches(m_entity, m_indices)) {
                    return;
                }
                ++m_pos;
            }
        }

        const BasicView* m_view = nullptr;
        usize m_pos = 0;
        usize m_startSize = 0;
        EntityId m_entity{};
        std::array<u32, kInc> m_indices{};
    };

    [[nodiscard]] Iterator begin() const noexcept { return Iterator(this, 0); }
    [[nodiscard]] Iterator end() const noexcept { return Iterator(this, m_driver == nullptr ? 0 : m_driver->size()); }

    // fn(EntityId, AccessRef<Is>...)
    template <class Fn>
    void each(Fn&& fn) const {
        for (auto&& tuple : *this) {
            std::apply(fn, tuple);
        }
    }

    // 결과 개수 (순회해서 센다)
    [[nodiscard]] usize count() const {
        usize n = 0;
        for (auto it = begin(); it != end(); ++it) {
            ++n;
        }
        return n;
    }

private:
    bool matches(EntityId e, std::array<u32, kInc>& indices) const noexcept {
        bool ok = true;
        usize k = 0;
        std::apply(
            [&](auto*... p) { ((ok = ok && ((indices[k] = p->indexOf(e)) != ComponentPoolBase::kNone), ++k), ...); },
            m_pools);
        if (!ok) {
            return false;
        }
        for (const ComponentPoolBase* ex : m_excludes) {
            if (ex != nullptr && ex->contains(e)) {
                return false;
            }
        }
        return true;
    }

    template <usize... K>
    value_type deref(EntityId e, const std::array<u32, kInc>& indices, std::index_sequence<K...>) const {
        return value_type(e, access<Is>(std::get<K>(m_pools), indices[K])...);
    }

    template <class A>
    detail::AccessRef<A> access(ComponentPool<typename A::type>* pool, u32 index) const {
        if constexpr (A::kWrite) {
            return pool->dataAtForWrite(index, m_tick);
        } else {
            return pool->dataAt(index);
        }
    }

    std::tuple<ComponentPool<typename Is::type>*...> m_pools;
    std::array<const ComponentPoolBase*, kExc> m_excludes;
    const ComponentPoolBase* m_driver = nullptr;
    Tick m_tick;
};

// Registry::view 가 호출한다. Reg 는 Registry (순환 include 를 피하려고 템플릿 인자로 받는다).
template <class... Access, class Reg>
auto makeView(Reg& reg) {
    static_assert(((detail::IsInclude<Access>::value || detail::IsExclude<Access>::value) && ...),
                  "view 인자는 Read<T> / Write<T> / Exclude<T> 만 허용한다");
    using Inc = decltype(std::tuple_cat(std::declval<detail::IncludeTuple<Access>>()...));
    using Exc = decltype(std::tuple_cat(std::declval<detail::ExcludeTuple<Access>>()...));
    return [&]<class... I, class... E>(std::tuple<I...>*, std::tuple<E...>*) {
        return BasicView<Inc, Exc>(
            std::tuple<ComponentPool<typename I::type>*...>(reg.template findPool<typename I::type>()...),
            std::array<const ComponentPoolBase*, sizeof...(E)>{
                static_cast<const ComponentPoolBase*>(reg.template findPool<typename E::type>())...},
            reg.currentTick());
    }(static_cast<Inc*>(nullptr), static_cast<Exc*>(nullptr));
}

} // namespace sbx::ecs
