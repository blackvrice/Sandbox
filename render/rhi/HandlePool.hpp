#pragma once
// 세대 핸들 풀. 백엔드가 자기 객체(Dx12Texture …)를 담는다. docs/06-RENDERING.md 2장 "Render Resource Handle".
// 해제하면 generation 이 올라 옛 핸들이 무효가 된다. 빈 슬롯은 LIFO 로 다시 쓴다.

#include <optional>
#include <utility>
#include <vector>

#include "foundation/handle/Handle.hpp"

namespace sbx::rhi {

template <class Tag, class T>
class HandlePool {
public:
    using H = Handle<Tag>;

    H insert(T value) {
        u32 index;
        if (!m_free.empty()) {
            index = m_free.back();
            m_free.pop_back();
        } else {
            index = static_cast<u32>(m_slots.size());
            m_slots.push_back({});
        }
        Slot& s = m_slots[index];
        s.value = std::move(value);
        ++m_live;
        return H{index, s.generation};
    }

    [[nodiscard]] T* get(H h) noexcept {
        if (!h.valid() || h.index >= m_slots.size()) {
            return nullptr;
        }
        Slot& s = m_slots[h.index];
        return s.value && s.generation == h.generation ? &*s.value : nullptr;
    }
    [[nodiscard]] const T* get(H h) const noexcept { return const_cast<HandlePool*>(this)->get(h); }

    // 꺼내고 슬롯을 비운다 (핸들 무효화). 없으면 nullopt.
    std::optional<T> take(H h) {
        T* v = get(h);
        if (v == nullptr) {
            return std::nullopt;
        }
        Slot& s = m_slots[h.index];
        std::optional<T> out = std::move(s.value);
        s.value.reset();
        ++s.generation;
        m_free.push_back(h.index);
        --m_live;
        return out;
    }

    [[nodiscard]] usize size() const noexcept { return m_live; }

    template <class F>
    void forEach(F&& f) {
        for (u32 i = 0; i < m_slots.size(); ++i) {
            if (m_slots[i].value) {
                f(H{i, m_slots[i].generation}, *m_slots[i].value);
            }
        }
    }
    template <class F>
    void forEach(F&& f) const {
        for (u32 i = 0; i < m_slots.size(); ++i) {
            if (m_slots[i].value) {
                f(H{i, m_slots[i].generation}, *m_slots[i].value);
            }
        }
    }

private:
    struct Slot {
        std::optional<T> value;
        u32 generation = 0;
    };
    std::vector<Slot> m_slots;
    std::vector<u32> m_free;
    usize m_live = 0;
};

} // namespace sbx::rhi
