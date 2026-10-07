#pragma once
// 고정 순서 System 실행기. docs/03-SIMULATION.md 2장.
//
// 순서 = (Stage, 같은 Stage 안에서는 등록 순서). 실행 중 System 마다 전용 ECB 를 주고 구조 잠금을 건다.
// ECB 는 실행이 끝난 뒤 applyBuffers() 가 System 실행 순서대로 적용한다 (Stage 17).
// Phase 3 은 단일 스레드다. [Phase 15] access() 선언을 근거로 같은 Stage 안의 System 을 병렬화한다.

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include "core/simulation/System.hpp"

namespace sbx::sim {

class SystemScheduler {
public:
    // 같은 이름의 System 을 두 번 등록하면 단언한다 (메트릭·로그가 이름으로 구분하므로).
    ISystem& add(Stage stage, std::unique_ptr<ISystem> system);

    template <class S, class... Args>
    S& emplace(Stage stage, Args&&... args) {
        return static_cast<S&>(add(stage, std::make_unique<S>(std::forward<Args>(args)...)));
    }

    // base 의 ecb 를 제외한 모든 참조를 System 마다 그대로 쓰고, ecb 만 System 전용으로 바꿔 끼운다.
    void run(const SystemContext& base, ISystemProfiler* profiler = nullptr);

    // 모든 System 의 ECB 를 실행 순서대로 적용한다. afterEach 는 ECB 하나를 적용할 때마다 불린다
    // (SimulationWorld 가 정체성 부여를 하나씩 따라가도록).
    void applyBuffers(ecs::Registry& reg, const std::function<void()>& afterEach = {});

    [[nodiscard]] usize size() const noexcept { return m_entries.size(); }
    [[nodiscard]] std::string_view nameAt(usize i) const noexcept { return m_entries[i].system->name(); }
    [[nodiscard]] Stage stageAt(usize i) const noexcept { return m_entries[i].stage; }
    [[nodiscard]] ISystem* find(std::string_view name) noexcept;

private:
    struct Entry {
        Stage stage;
        std::unique_ptr<ISystem> system;
        ecs::EntityCommandBuffer ecb;
    };
    std::vector<Entry> m_entries; // 항상 실행 순서로 정렬되어 있다
};

} // namespace sbx::sim
