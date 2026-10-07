#include "core/path/PathfindingService.hpp"

#include <utility>

#include "foundation/assert/Assert.hpp"

namespace sbx::path {

PathfindingService::~PathfindingService() {
    m_group->wait();
}

void PathfindingService::refreshSnapshot(const world::WorldGrid& grid) {
    m_snapshot = PathGridSnapshot::build(grid, m_snapshot);
}

void PathfindingService::submit(SaveId saveId, ecs::EntityId entity, sim::Tick tick, Vec2 start, Vec2 goal) {
    SBX_VERIFY(m_snapshot != nullptr, "PathfindingService::submit 전에 refreshSnapshot 을 불러야 한다");
    auto job = std::make_unique<PathJob>();
    job->saveId = saveId;
    job->entity = entity;
    job->tick = tick;
    job->query = PathQuery{start, goal, m_maxExpansions};
    PathJob* slot = job.get();
    m_batch.push_back(std::move(job));
    // 입력·스냅샷을 값으로 붙잡는다. Job 은 자기 칸(slot->result)에만 쓴다.
    std::shared_ptr<const PathGridSnapshot> snap = m_snapshot;
    auto work = [slot, snap = std::move(snap)] { slot->result = findPath(*snap, slot->query); };
    if (m_jobs == nullptr) {
        work();
    } else {
        m_jobs->submit(*m_group, std::move(work));
    }
}

std::vector<std::unique_ptr<PathJob>> PathfindingService::collect() {
    m_group->wait();
    return std::exchange(m_batch, {});
}

void PathfindingService::discard() {
    m_group->wait();
    m_batch.clear();
}

} // namespace sbx::path
