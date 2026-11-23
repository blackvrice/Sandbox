#include "render/graphics_sdk/GraphicsSdkCommandQueue.hpp"

#include <array>
#include <chrono>
#include <vector>

#include "render/graphics_sdk/GraphicsSdkCommandList.hpp"
#include "render/graphics_sdk/GraphicsSdkDevice.hpp"
#include "render/graphics_sdk/GraphicsSdkTypes.hpp"

namespace sbx::rhi::graphics_sdk {

gfx::CommandQueue& GraphicsSdkCommandQueue::targetQueue() const noexcept {
    return m_sdkQueue != nullptr ? *m_sdkQueue : m_dev.sdkDevice().queue();
}

FenceValue GraphicsSdkCommandQueue::submit(std::span<ICommandList* const> lists) {
    // 일반적인 1~16개 커맨드 리스트 배치 제출에서는 스택 배열을 사용해 힙 할당을 제거한다.
    constexpr usize kInlineLimit = 16;
    std::array<gfx::CommandList*, kInlineLimit> inlineLists{};
    usize inlineCount = 0;
    std::vector<gfx::CommandList*> overflowLists;

    for (ICommandList* list : lists) {
        if (list == nullptr) {
            continue;
        }
        if (gfx::CommandList* prepared = prepareForSubmit(*list); prepared != nullptr) {
            if (overflowLists.empty() && inlineCount < kInlineLimit) {
                inlineLists[inlineCount++] = prepared;
            } else {
                if (overflowLists.empty()) {
                    overflowLists.reserve(lists.size());
                    overflowLists.insert(overflowLists.end(), inlineLists.begin(), inlineLists.begin() + inlineCount);
                }
                overflowLists.push_back(prepared);
            }
        }
    }

    const std::span<gfx::CommandList* const> batch =
        !overflowLists.empty() ? std::span<gfx::CommandList* const>(overflowLists)
                               : std::span<gfx::CommandList* const>(inlineLists.data(), inlineCount);

    gfx::CommandQueue& q = targetQueue();

    if (batch.empty()) {
        // 빈 제출일 때도 단조 증가하는 타임라인 계약을 유지하기 위해 빈 커맨드 리스트를 하나 제출한다.
        auto emptyCmd = m_dev.sdkDevice().createCommandList(mapQueueType(m_type));
        if (emptyCmd) {
            if ((*emptyCmd)->begin() && (*emptyCmd)->end()) {
                auto fence = q.submit(**emptyCmd);
                if (fence) {
                    m_lastFence = *fence;
                    return m_lastFence.value;
                }
            }
        }
        return m_lastFence.value;
    }

    auto fence = batch.size() == 1 ? q.submit(*batch[0]) : q.submit(batch);
    if (!fence) {
        m_dev.reportSdkError("CommandQueue::submit", fence.error());
        return m_lastFence.value;
    }

    m_lastFence = *fence;
    if (m_type == QueueType::Graphics || !m_dedicated) {
        m_dev.noteFrameSubmission(m_lastFence);
    } else {
        m_dev.noteSecondarySubmission(m_type, m_lastFence);
    }
    for (ICommandList* list : lists) {
        if (list != nullptr) {
            notifySubmitted(*list, m_lastFence);
        }
    }
    return m_lastFence.value;
}

FenceValue GraphicsSdkCommandQueue::completedValue() const {
    auto res = targetQueue().completedValue();
    if (!res) {
        return 0;
    }
    if (m_type == QueueType::Graphics || !m_dedicated) {
        m_dev.syncCompletedReadbacks(*res);
    }
    return *res;
}

void GraphicsSdkCommandQueue::wait(FenceValue value) {
    if (value == 0 || m_lastFence.value == 0) {
        return;
    }
    gfx::CommandQueue& q = targetQueue();
    const FenceValue maxTarget = std::min(value, m_lastFence.value);
    FenceValue cur = 0;
    if (auto c = q.completedValue()) {
        cur = *c;
    }
    while (cur < maxTarget) {
        const gfx::Fence stepTarget{cur + 1, m_lastFence.device};
        // GraphicsSDK 의 queue().wait(..., milliseconds) 는 최대 ~4,294,967 ms 이하 유한 범위만 허용하므로 5000 ms 를 쓴다.
        if (auto res = q.wait(stepTarget, std::chrono::milliseconds(5000)); !res) {
            m_dev.reportSdkError("CommandQueue::wait", res.error());
            return;
        }
        if (auto next = q.completedValue()) {
            if (*next <= cur) {
                // vkWaitForFences 완료 직후 vkGetFenceStatus 가 아직 VK_NOT_READY 를 돌려주는 드라이버 레이스 시
                // waitIdle() 로 큐 전체를 확정 은퇴시킨다.
                (void)q.waitIdle();
                if (auto afterIdle = q.completedValue()) {
                    cur = *afterIdle;
                }
            } else {
                cur = *next;
            }
        } else {
            break;
        }
    }
    if (m_type == QueueType::Graphics || !m_dedicated) {
        m_dev.syncCompletedReadbacks(cur);
    }
}

void GraphicsSdkCommandQueue::waitIdle() {
    if (auto res = targetQueue().waitIdle(); !res) {
        m_dev.reportSdkError("CommandQueue::waitIdle", res.error());
        return;
    }
    if (m_lastFence.value > 0) {
        wait(m_lastFence.value);
    }
}

} // namespace sbx::rhi::graphics_sdk
