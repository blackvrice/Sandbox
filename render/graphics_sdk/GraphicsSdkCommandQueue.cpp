#include "render/graphics_sdk/GraphicsSdkCommandQueue.hpp"

#include <chrono>
#include <vector>

#include "render/graphics_sdk/GraphicsSdkCommandList.hpp"
#include "render/graphics_sdk/GraphicsSdkDevice.hpp"

namespace sbx::rhi::graphics_sdk {

FenceValue GraphicsSdkCommandQueue::submit(std::span<ICommandList* const> lists) {
    std::vector<gfx::CommandList*> sdkLists;
    sdkLists.reserve(lists.size());
    for (ICommandList* list : lists) {
        if (list == nullptr) {
            continue;
        }
        if (gfx::CommandList* prepared = prepareForSubmit(*list); prepared != nullptr) {
            sdkLists.push_back(prepared);
        }
    }

    if (sdkLists.empty()) {
        // 빈 제출일 때도 단조 증가하는 타임라인 계약을 유지하기 위해 빈 커맨드 리스트를 하나 제출한다.
        auto emptyCmd = m_dev.sdkDevice().createCommandList(gfx::QueueType::Graphics);
        if (emptyCmd) {
            if ((*emptyCmd)->begin() && (*emptyCmd)->end()) {
                auto fence = m_dev.sdkDevice().queue().submit(**emptyCmd);
                if (fence) {
                    m_lastFence = *fence;
                    return m_lastFence.value;
                }
            }
        }
        return m_lastFence.value;
    }

    auto fence = sdkLists.size() == 1
                     ? m_dev.sdkDevice().queue().submit(*sdkLists[0])
                     : m_dev.sdkDevice().queue().submit(std::span<gfx::CommandList* const>(sdkLists));
    if (!fence) {
        m_dev.reportSdkError("CommandQueue::submit", fence.error());
        return m_lastFence.value;
    }

    m_lastFence = *fence;
    m_dev.noteFrameSubmission(m_lastFence);
    for (ICommandList* list : lists) {
        if (list != nullptr) {
            notifySubmitted(*list, m_lastFence);
        }
    }
    return m_lastFence.value;
}

FenceValue GraphicsSdkCommandQueue::completedValue() const {
    auto res = m_dev.sdkDevice().queue().completedValue();
    if (!res) {
        return 0;
    }
    m_dev.syncCompletedReadbacks(*res);
    return *res;
}

void GraphicsSdkCommandQueue::wait(FenceValue value) {
    if (value == 0 || m_lastFence.value == 0) {
        return;
    }
    const FenceValue maxTarget = std::min(value, m_lastFence.value);
    FenceValue cur = 0;
    if (auto c = m_dev.sdkDevice().queue().completedValue()) {
        cur = *c;
    }
    while (cur < maxTarget) {
        const gfx::Fence stepTarget{cur + 1, m_lastFence.device};
        // GraphicsSDK 의 queue().wait(..., milliseconds) 는 최대 ~4,294,967 ms 이하 유한 범위만 허용하므로 5000 ms 를 쓴다.
        if (auto res = m_dev.sdkDevice().queue().wait(stepTarget, std::chrono::milliseconds(5000)); !res) {
            m_dev.reportSdkError("CommandQueue::wait", res.error());
            return;
        }
        if (auto next = m_dev.sdkDevice().queue().completedValue()) {
            if (*next <= cur) {
                // vkWaitForFences 완료 직후 vkGetFenceStatus 가 아직 VK_NOT_READY 를 돌려주는 드라이버 레이스 시
                // waitIdle() 로 큐 전체를 확정 은퇴시킨다.
                (void)m_dev.sdkDevice().queue().waitIdle();
                if (auto afterIdle = m_dev.sdkDevice().queue().completedValue()) {
                    cur = *afterIdle;
                }
            } else {
                cur = *next;
            }
        } else {
            break;
        }
    }
    m_dev.syncCompletedReadbacks(cur);
}

void GraphicsSdkCommandQueue::waitIdle() {
    if (auto res = m_dev.sdkDevice().queue().waitIdle(); !res) {
        m_dev.reportSdkError("CommandQueue::waitIdle", res.error());
        return;
    }
    if (m_lastFence.value > 0) {
        wait(m_lastFence.value);
    }
}

} // namespace sbx::rhi::graphics_sdk
