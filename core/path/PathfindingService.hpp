#pragma once
// 경로 Job 의 제출·수거. docs/03-SIMULATION.md 7.1 (결정론의 핵심).
//
//   tick T   Stage 8  PathRequestSystem: saveId 오름차순으로 예산(budget)만큼 submit(). 각 Job 은 입력과
//                     그 순간의 PathGridSnapshot(불변)을 값으로 붙잡는다.
//   tick T+1 Stage 5  PathCollectSystem: collect() 가 T 의 Job 을 **모두 기다린 뒤** 제출 순서로 돌려준다.
//
// 결과가 적용되는 틱이 Job 의 완료 시점과 무관하므로 Worker 수(0, 1, N)와 무관하다 (D5).
// JobSystem 이 없거나 Worker 가 0 이면 submit() 이 그 자리에서 계산한다.
//
// 세이브: 진행 중인 Job 은 저장하지 않는다. ai.path 가 Submitted 상태로 저장되고, 로드가 끝날 때
// resubmit 으로 같은 입력·같은 지형(아직 다음 틱의 명령 전)에서 다시 계산한다 → 같은 결과 (D2).

#include <memory>
#include <vector>

#include "core/ecs/EntityId.hpp"
#include "core/path/Pathfinder.hpp"
#include "core/simulation/SimConstants.hpp"
#include "foundation/job/JobSystem.hpp"

namespace sbx::path {

struct PathJob {
    SaveId saveId = kInvalidSaveId;
    ecs::EntityId entity{};
    sim::Tick tick = 0; // 제출 틱 (ai.path.submittedTick 과 맞춰 본다)
    PathQuery query;
    PathResult result;
};

class PathfindingService {
public:
    PathfindingService() = default;
    PathfindingService(const PathfindingService&) = delete;
    PathfindingService& operator=(const PathfindingService&) = delete;
    PathfindingService(PathfindingService&&) = delete;
    PathfindingService& operator=(PathfindingService&&) = delete;
    ~PathfindingService();

    void setJobSystem(JobSystem* jobs) noexcept { m_jobs = jobs; }
    [[nodiscard]] JobSystem* jobSystem() const noexcept { return m_jobs; }
    void setBudget(u32 perTick) noexcept { m_budget = perTick; }
    [[nodiscard]] u32 budget() const noexcept { return m_budget; }
    void setMaxExpansions(u32 n) noexcept { m_maxExpansions = n; }

    // 지형이 바뀐 청크만 반영한 스냅샷으로 갈아 끼운다 (제출 전에 한 번)
    void refreshSnapshot(const world::WorldGrid& grid);
    [[nodiscard]] const std::shared_ptr<const PathGridSnapshot>& snapshot() const noexcept { return m_snapshot; }

    void submit(SaveId saveId, ecs::EntityId entity, sim::Tick tick, Vec2 start, Vec2 goal);
    // 진행 중인 Job 을 모두 기다려 제출 순서로 돌려주고 비운다
    [[nodiscard]] std::vector<std::unique_ptr<PathJob>> collect();
    [[nodiscard]] usize inFlight() const noexcept { return m_batch.size(); }
    // 기다린 뒤 버린다 (월드 재설정용)
    void discard();

    static constexpr u32 kDefaultBudget = 64;

private:
    JobSystem* m_jobs = nullptr;
    u32 m_budget = kDefaultBudget;
    u32 m_maxExpansions = 4096;
    std::shared_ptr<const PathGridSnapshot> m_snapshot;
    std::unique_ptr<JobGroup> m_group = std::make_unique<JobGroup>();
    std::vector<std::unique_ptr<PathJob>> m_batch; // 제출 순서. 주소가 고정되어야 Job 이 결과를 쓴다
};

} // namespace sbx::path
