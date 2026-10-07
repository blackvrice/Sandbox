#pragma once
// 지연 해제 큐. docs/06-RENDERING.md 4.3.
//   destroy  → push(object, 마지막으로 제출한 펜스 값)
//   beginFrame → collect(completedValue): 펜스가 지난 항목만 실제로 해제 (소멸)
//   종료 → waitIdle 뒤 clear()
// 펜스 값은 단조 증가이므로 앞에서부터 꺼내면 된다.

#include <deque>
#include <utility>

#include "foundation/types/Types.hpp"

namespace sbx::rhi {

template <class T>
class DeferredDestructionQueue {
public:
    void push(T object, u64 fenceValue) { m_items.push_back({fenceValue, std::move(object)}); }

    // completedValue 이하의 펜스로 기록된 항목을 해제한다. 해제한 수를 돌려준다.
    usize collect(u64 completedValue) {
        usize n = 0;
        while (!m_items.empty() && m_items.front().fence <= completedValue) {
            m_items.pop_front(); // T 의 소멸자가 실제 해제 (ComPtr Release 등)
            ++n;
        }
        return n;
    }

    usize clear() {
        const usize n = m_items.size();
        m_items.clear();
        return n;
    }

    [[nodiscard]] usize size() const noexcept { return m_items.size(); }

private:
    struct Item {
        u64 fence;
        T object;
    };
    std::deque<Item> m_items;
};

} // namespace sbx::rhi
