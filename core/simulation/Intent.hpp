#pragma once
// 상호작용 의도와 saveId 조회. docs/03-SIMULATION.md 6.2, 02-ECS E1.
//
// IntentBuffer: Stage 10(Interaction)이 Rule 매칭 결과를 쌓고 Stage 11(ResolveIntents)이 정렬해 적용한다.
//   쌓는 순서(dense 순회)는 결과에 영향이 없다 — 적용 전에 (target saveId, priority 내림, rule 정의 순, source saveId)
//   전순서로 정렬한다. BeginTick 에 비운다.
// SaveIndex: saveId → EntityId (조회 전용 — 순회하지 않는다, 04 4.2). SimulationWorld 가 정체성 장부와 함께 맞춘다.

#include <unordered_map>
#include <vector>

#include "core/components/core/Identity.hpp"
#include "core/ecs/EntityId.hpp"

namespace sbx::content {
struct Rule;
}

namespace sbx::sim {

struct Intent {
    const content::Rule* rule = nullptr;
    ecs::EntityId source{};
    ecs::EntityId target{};
    SaveId sourceSave = kInvalidSaveId;
    SaveId targetSave = kInvalidSaveId;
};

class IntentBuffer {
public:
    void push(const Intent& i) { m_items.push_back(i); }
    void clear() noexcept { m_items.clear(); }
    [[nodiscard]] std::vector<Intent>& items() noexcept { return m_items; }
    [[nodiscard]] const std::vector<Intent>& items() const noexcept { return m_items; }
    [[nodiscard]] usize size() const noexcept { return m_items.size(); }

private:
    std::vector<Intent> m_items;
};

class SaveIndex {
public:
    [[nodiscard]] ecs::EntityId find(SaveId id) const noexcept {
        const auto it = m_map.find(id);
        return it == m_map.end() ? ecs::kNullEntity : it->second;
    }
    void set(SaveId id, ecs::EntityId e) { m_map[id] = e; }
    void erase(SaveId id) { m_map.erase(id); }
    void clear() noexcept { m_map.clear(); }
    [[nodiscard]] usize size() const noexcept { return m_map.size(); }

private:
    std::unordered_map<SaveId, ecs::EntityId> m_map;
};

} // namespace sbx::sim
