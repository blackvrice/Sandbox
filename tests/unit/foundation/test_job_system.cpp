// JobSystem (Phase 5B). docs/01-ARCHITECTURE.md 5장, 04-DETERMINISM D5.
#include <doctest/doctest.h>

#include <atomic>
#include <vector>

#include "foundation/job/JobSystem.hpp"

using namespace sbx;

TEST_SUITE("foundation") {

    TEST_CASE("job system: every job runs once and wait() sees all results (0, 1, 4 workers)") {
        for (const u32 workers : {0u, 1u, 4u}) {
            INFO("workers " << workers);
            JobSystem jobs(workers);
            CHECK(jobs.workerCount() == workers);
            std::vector<u64> out(500, 0);
            JobGroup group;
            for (usize i = 0; i < out.size(); ++i) {
                u64* slot = &out[i];
                jobs.submit(group, [slot, i] { *slot = static_cast<u64>(i) * static_cast<u64>(i); });
            }
            group.wait();
            CHECK(group.idle());
            for (usize i = 0; i < out.size(); ++i) {
                CHECK(out[i] == static_cast<u64>(i) * static_cast<u64>(i));
            }
        }
    }

    TEST_CASE("job system: zero workers run inline at submit") {
        JobSystem jobs(0);
        JobGroup group;
        int x = 0;
        jobs.submit(group, [&x] { x = 7; });
        CHECK(x == 7); // wait() 전에 이미 끝났다
        group.wait();
    }

    TEST_CASE("job system: groups are independent and the system drains on destruction") {
        std::atomic<int> count{0};
        {
            JobSystem jobs(2);
            JobGroup a;
            JobGroup b;
            for (int i = 0; i < 50; ++i) {
                jobs.submit(i % 2 == 0 ? a : b, [&count] { count.fetch_add(1); });
            }
            a.wait();
            b.wait();
            CHECK(count.load() == 50);
        }
        CHECK(JobSystem::kMaxWorkers == 64);
        JobSystem capped(1000);
        CHECK(capped.workerCount() == JobSystem::kMaxWorkers);
    }

} // TEST_SUITE
