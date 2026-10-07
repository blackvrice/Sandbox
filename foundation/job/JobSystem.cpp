#include "foundation/job/JobSystem.hpp"

#include <algorithm>
#include <utility>

#include "foundation/assert/Assert.hpp"

namespace sbx {

JobGroup::~JobGroup() {
    SBX_ASSERT(idle(), "진행 중인 Job 이 있는 JobGroup 을 파괴했다 (wait() 를 먼저)");
}

void JobGroup::wait() {
    std::unique_lock lock(m_mutex);
    m_cv.wait(lock, [this] { return m_pending.load(std::memory_order_acquire) == 0; });
}

void JobGroup::done() {
    // 마지막 Job 이 끝났을 때만 깨운다. 잠금 안에서 줄여야 wait() 가 검사와 잠들기 사이에 신호를 놓치지 않는다.
    std::lock_guard lock(m_mutex);
    if (m_pending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        m_cv.notify_all();
    }
}

JobSystem::JobSystem(u32 workers) {
    const u32 n = std::min(workers, kMaxWorkers);
    m_threads.reserve(n);
    for (u32 i = 0; i < n; ++i) {
        m_threads.emplace_back([this] { workerLoop(); });
    }
}

JobSystem::~JobSystem() {
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    for (std::thread& t : m_threads) {
        t.join();
    }
}

void JobSystem::submit(JobGroup& group, std::function<void()> job) {
    if (m_threads.empty()) {
        job(); // Worker 0 개: 그 자리에서 (D5 의 기준 실행)
        return;
    }
    group.add();
    {
        std::lock_guard lock(m_mutex);
        m_queue.push_back(Task{&group, std::move(job)});
    }
    m_cv.notify_one();
}

void JobSystem::workerLoop() {
    for (;;) {
        Task task{nullptr, {}};
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stop || !m_queue.empty(); });
            if (m_queue.empty()) {
                return; // m_stop 이고 할 일이 없다
            }
            task = std::move(m_queue.front());
            m_queue.pop_front();
        }
        task.fn();
        task.group->done();
    }
}

} // namespace sbx
