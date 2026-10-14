#include "render/rhi/UploadRing.hpp"

#include "render/rhi/RhiTypes.hpp"

namespace sbx::rhi {

std::optional<u64> UploadRing::allocate(u64 size, u64 alignment) {
    if (size == 0 || size > m_capacity) {
        return std::nullopt;
    }
    if (m_used == 0 && alignUp(m_head, alignment) + size > m_capacity) {
        // 비었는데 끝에 자리가 없으면 처음부터 — 가장 긴 연속 공간. 자리가 있으면 그대로 앞으로 간다: GPU 가 프레임마다
        // 다 끝내도 위치가 돌아 감기가 생긴다 (끝냈는지에 따라 위치가 달라지지 않게 — 시험 copy: ring wrap-around)
        m_head = m_tail = 0;
    }
    const bool full = m_used != 0 && m_head == m_tail;
    if (full) {
        return std::nullopt;
    }
    u64 offset = 0;
    u64 padding = 0;
    if (m_head >= m_tail) {
        // 빈 곳: [head, capacity) 와 [0, tail)
        const u64 a = alignUp(m_head, alignment);
        if (a + size <= m_capacity) {
            offset = a;
            padding = a - m_head;
        } else if (size <= m_tail) { // 앞으로 감는다 (0 은 어떤 정렬에도 맞다)
            offset = 0;
            padding = m_capacity - m_head;
        } else {
            return std::nullopt;
        }
    } else {
        // 빈 곳: [head, tail)
        const u64 a = alignUp(m_head, alignment);
        if (a + size > m_tail) {
            return std::nullopt;
        }
        offset = a;
        padding = a - m_head;
    }
    m_head = offset + size;
    if (m_head == m_capacity) {
        m_head = 0;
    }
    m_used += padding + size;
    m_frameBytes += padding + size;
    return offset;
}

void UploadRing::endFrame(u64 fenceValue) {
    if (m_frameBytes == 0) {
        return;
    }
    m_frames.push_back({fenceValue, m_head, m_frameBytes});
    m_frameBytes = 0;
}

void UploadRing::retire(u64 completedValue) {
    while (!m_frames.empty() && m_frames.front().fence <= completedValue) {
        m_tail = m_frames.front().end;
        m_used -= m_frames.front().bytes;
        m_frames.pop_front();
    }
}

} // namespace sbx::rhi
