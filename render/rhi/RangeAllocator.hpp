#pragma once
// 연속 구간 할당기 (first-fit + 이웃 병합). shader-visible 디스크립터 힙의 BindGroup 자리에 쓴다 (06 5.1, ADR-0019).
// 크기 단위는 호출자가 정한다 (디스크립터 개수). 스레드 안전하지 않다 — Render 스레드 전용.

#include <cstddef>
#include <optional>
#include <vector>

#include "foundation/types/Types.hpp"

namespace sbx::rhi {

class RangeAllocator {
public:
    RangeAllocator() = default;
    explicit RangeAllocator(u32 capacity) { reset(capacity); }

    void reset(u32 capacity) {
        m_capacity = capacity;
        m_used = 0;
        m_free.clear();
        if (capacity > 0) {
            m_free.push_back({0, capacity});
        }
    }

    // 가장 앞의 맞는 빈 구간에서 count 개. 자리가 없으면 nullopt. count 0 은 nullopt.
    [[nodiscard]] std::optional<u32> allocate(u32 count) {
        if (count == 0) {
            return std::nullopt;
        }
        for (auto it = m_free.begin(); it != m_free.end(); ++it) {
            if (it->count >= count) {
                const u32 start = it->start;
                it->start += count;
                it->count -= count;
                if (it->count == 0) {
                    m_free.erase(it);
                }
                m_used += count;
                return start;
            }
        }
        return std::nullopt;
    }

    // allocate 가 준 구간을 돌려준다. 이웃 빈 구간과 합친다.
    void release(u32 start, u32 count) {
        if (count == 0) {
            return;
        }
        usize i = 0;
        while (i < m_free.size() && m_free[i].start < start) {
            ++i;
        }
        m_free.insert(m_free.begin() + static_cast<std::ptrdiff_t>(i), Range{start, count});
        m_used -= count;
        // 뒤와 합치기
        if (i + 1 < m_free.size() && m_free[i].start + m_free[i].count == m_free[i + 1].start) {
            m_free[i].count += m_free[i + 1].count;
            m_free.erase(m_free.begin() + static_cast<std::ptrdiff_t>(i + 1));
        }
        // 앞과 합치기
        if (i > 0 && m_free[i - 1].start + m_free[i - 1].count == m_free[i].start) {
            m_free[i - 1].count += m_free[i].count;
            m_free.erase(m_free.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }

    [[nodiscard]] u32 capacity() const noexcept { return m_capacity; }
    [[nodiscard]] u32 used() const noexcept { return m_used; }
    [[nodiscard]] usize freeRanges() const noexcept { return m_free.size(); }

private:
    struct Range {
        u32 start;
        u32 count;
    };
    std::vector<Range> m_free; // start 순
    u32 m_capacity = 0;
    u32 m_used = 0;
};

} // namespace sbx::rhi
