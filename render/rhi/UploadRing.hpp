#pragma once
// 업로드 링의 구간 관리 (메모리 자체는 백엔드가 가진다). docs/06-RENDERING.md 4.2.
//
//   allocate(size, align) → 오프셋 (정렬 후 bump, 끝에 안 맞으면 앞으로 감는다). 자리가 없으면 nullopt.
//   endFrame(fence)       → 이번 프레임에 할당한 구간을 그 펜스에 묶는다
//   retire(completed)     → 펜스가 지난 프레임 구간을 돌려받는다
// 한 번에 링 전체보다 큰 요청은 실패한다. 감을 때 버린 꼬리도 그 프레임의 사용량으로 센다.

#include <deque>
#include <optional>

#include "foundation/types/Types.hpp"

namespace sbx::rhi {

class UploadRing {
public:
    explicit UploadRing(u64 capacity) : m_capacity(capacity) {}

    [[nodiscard]] std::optional<u64> allocate(u64 size, u64 alignment);
    void endFrame(u64 fenceValue);
    void retire(u64 completedValue);

    [[nodiscard]] u64 capacity() const noexcept { return m_capacity; }
    [[nodiscard]] u64 used() const noexcept { return m_used; } // 아직 돌려받지 못한 바이트 (정렬·감기 여분 포함)
    [[nodiscard]] usize pendingFrames() const noexcept { return m_frames.size(); }

private:
    struct FrameMark {
        u64 fence;
        u64 end;   // 이 프레임 마지막 할당의 끝 (돌려받으면 tail 이 여기로)
        u64 bytes; // 이 프레임이 차지한 바이트
    };
    u64 m_capacity;
    u64 m_head = 0; // 다음 쓰기 위치
    u64 m_tail = 0; // 살아 있는 가장 오래된 바이트
    u64 m_used = 0;
    u64 m_frameBytes = 0; // 아직 endFrame 하지 않은 할당
    std::deque<FrameMark> m_frames;
};

} // namespace sbx::rhi
