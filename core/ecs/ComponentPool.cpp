#include "core/ecs/ComponentPool.hpp"

#include <format>
#include <unordered_set>

namespace sbx::ecs {

Expected<void> ComponentPoolBase::validateIndex(usize dataSize) const {
    const std::string ctx{name()};
    // P2
    if (m_dense.size() != dataSize || m_changed.size() != dataSize || m_added.size() != dataSize) {
        return makeError(ErrorCode::ValidationFailed, "P2: dense/data/changed/added 크기 불일치", ctx);
    }
    std::unordered_set<u32> seen; // 검사 전용 (조회만)
    for (usize i = 0; i < m_dense.size(); ++i) {
        const EntityId e = m_dense[i];
        // P3
        if (!seen.insert(e.index()).second) {
            return makeError(ErrorCode::ValidationFailed, std::format("P3: index {} 중복", e.index()), ctx);
        }
        // P1 (→ 방향)
        if (indexOf(e) != i) {
            return makeError(ErrorCode::ValidationFailed, std::format("P1: dense[{}] 의 sparse 역참조 불일치", i), ctx);
        }
    }
    // P1 (← 방향): sparse 에 기록된 모든 값이 dense 와 일치
    usize mapped = 0;
    for (usize p = 0; p < m_sparse.size(); ++p) {
        if (!m_sparse[p]) {
            continue;
        }
        for (u32 s = 0; s < kPageSize; ++s) {
            const u32 d = (*m_sparse[p])[s];
            if (d == kNone) {
                continue;
            }
            ++mapped;
            const auto index = static_cast<u32>((p << kPageShift) | s);
            if (d >= m_dense.size() || m_dense[d].index() != index) {
                return makeError(ErrorCode::ValidationFailed,
                                 std::format("P1: sparse[{}] → {} 가 dense 와 불일치", index, d), ctx);
            }
        }
    }
    if (mapped != m_dense.size()) {
        return makeError(ErrorCode::ValidationFailed, "P1: sparse 항목 수와 dense 크기 불일치", ctx);
    }
    return {};
}

} // namespace sbx::ecs
