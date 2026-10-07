#pragma once
// Worker 스레드 풀. docs/01-ARCHITECTURE.md 5장 (T2·T3), docs/04-DETERMINISM.md D5.
//
//   JobSystem jobs(4);           // 0 이면 Worker 없음 — submit 이 그 자리에서 실행한다
//   JobGroup group;
//   jobs.submit(group, [slot] { slot->result = compute(slot->input); });
//   group.wait();                // 그룹의 모든 Job 이 끝날 때까지
//
// 계약 (결정론은 호출자가 지킨다 — 이 클래스는 순서를 보장하지 않는다)
//   - Job 은 값으로 캡처한 입력과 불변 스냅샷만 읽고, 자기 결과 칸에만 쓴다 (공유 가변 상태 없음).
//   - 결과는 제출한 스레드가 wait() 뒤에 **제출 순서대로** 읽는다. 완료 순서는 의미가 없다.
//   - 그래서 Worker 수(0, 1, N)가 결과를 바꾸지 않는다 (D5).
//   - Job 안에서 예외를 던지지 않는다 (던지면 std::terminate).

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "foundation/types/Types.hpp"

namespace sbx {

// 같이 기다릴 Job 묶음. JobSystem 보다 먼저 사라지면 안 되고, 진행 중인 Job 이 있을 때 파괴하면 안 된다(단언).
class JobGroup {
public:
    JobGroup() = default;
    JobGroup(const JobGroup&) = delete;
    JobGroup& operator=(const JobGroup&) = delete;
    JobGroup(JobGroup&&) = delete;
    JobGroup& operator=(JobGroup&&) = delete;
    ~JobGroup();

    // 모든 Job 이 끝날 때까지 막는다
    void wait();
    [[nodiscard]] bool idle() const noexcept { return m_pending.load(std::memory_order_acquire) == 0; }

private:
    friend class JobSystem;
    void add() noexcept { m_pending.fetch_add(1, std::memory_order_relaxed); }
    void done();

    std::atomic<u32> m_pending{0};
    std::mutex m_mutex;
    std::condition_variable m_cv;
};

class JobSystem {
public:
    // workers = 0 이면 스레드를 만들지 않는다. 상한 64.
    explicit JobSystem(u32 workers);
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&) = delete;
    JobSystem& operator=(JobSystem&&) = delete;
    ~JobSystem(); // 남은 Job 을 모두 실행한 뒤 스레드를 멈춘다

    [[nodiscard]] u32 workerCount() const noexcept { return static_cast<u32>(m_threads.size()); }

    void submit(JobGroup& group, std::function<void()> job);

    static constexpr u32 kMaxWorkers = 64;

private:
    struct Task {
        JobGroup* group = nullptr;
        std::function<void()> fn;
    };
    void workerLoop();

    std::vector<std::thread> m_threads;
    std::deque<Task> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stop = false;
};

// [0, count) 를 조각으로 나눠 fn(begin, end) 를 Worker 와 호출 스레드가 나눠 실행하고 모두 끝날 때까지 기다린다.
// jobs 가 없거나 Worker 가 0 이거나 count < minPerChunk × 2 면 호출 스레드에서 fn(0, count) 한 번.
// 결정론: fn 은 자기 범위의 결과 칸에만 쓰고 공유 상태를 바꾸지 않아야 한다 — 그러면 조각 수와 무관하다 (D5).
template <class Fn>
void parallelFor(JobSystem* jobs, usize count, usize minPerChunk, Fn&& fn) {
    const u32 workers = jobs != nullptr ? jobs->workerCount() : 0u;
    if (workers == 0 || count < minPerChunk * 2) {
        fn(usize{0}, count);
        return;
    }
    const usize chunks = std::min<usize>(static_cast<usize>(workers) + 1, count / minPerChunk);
    const usize per = (count + chunks - 1) / chunks;
    JobGroup group;
    for (usize c = 1; c < chunks; ++c) { // 조각 0 은 호출 스레드가
        const usize b = c * per;
        const usize e = std::min(count, b + per);
        if (b < e) {
            jobs->submit(group, [&fn, b, e] { fn(b, e); });
        }
    }
    fn(usize{0}, std::min(count, per));
    group.wait();
}

} // namespace sbx
