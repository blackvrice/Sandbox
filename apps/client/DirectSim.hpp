#pragma once
// --direct-sim: 클라이언트가 SimulationWorld 를 직접 돌린다. docs/16-ROADMAP.md 8.5 (임시 — Phase 10.5 에서 삭제),
// ADR-0020.
//
//   시나리오(core/scenarios) 하나를 ScenarioRunner 로. 틱 속도는 월드 시계(30 TPS) × speed.
//   일시정지 · 한 틱 · 속도는 클라이언트 쪽 진행만 바꾼다 (월드에 명령을 넣지 않는다 — 결정론 경로와 무관).
//   한 프레임에 따라잡는 틱은 kMaxCatchUpTicks 까지 (느린 기계에서 프레임이 늘어지는 악순환을 막는다 — 남은 시간은
//   버린다). 틱마다 직전 위치를 남겨 extract 가 보간한다.

#include <filesystem>
#include <memory>
#include <string>

#include "apps/client/WorldSession.hpp"
#include "apps/client/presentation/SpriteExtraction.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "core/scenarios/Scenario.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::client {

struct DirectSimDesc {
    std::string scenario;
    u64 seed = 1;
    std::filesystem::path contentRoot;
};

class DirectSim final : public IWorldSession {
public:
    static Expected<std::unique_ptr<DirectSim>> create(const DirectSimDesc& desc, render::MaterialLibrary& materials);

    void update(f64 dtSeconds) override;
    void extract(render::RenderWorld& out) override;
    [[nodiscard]] render::WorldRect bounds() const override;
    void togglePause() override { m_paused = !m_paused; }
    void stepOnce() override;
    void changeSpeed(int dir) override;
    [[nodiscard]] std::string status() const override;

    [[nodiscard]] bool paused() const noexcept { return m_paused; }
    [[nodiscard]] f32 speed() const noexcept { return m_speed; }
    [[nodiscard]] f32 alpha() const noexcept;
    [[nodiscard]] sim::Tick tick() const;
    [[nodiscard]] sim::SimulationWorld& world() { return m_runner->world(); }
    [[nodiscard]] const ExtractionStats& extractionStats() const noexcept { return m_extraction.stats(); }

    static constexpr u32 kMaxCatchUpTicks = 4;
    static constexpr f32 kSpeeds[] = {0.25f, 0.5f, 1.f, 2.f, 4.f, 8.f};

private:
    DirectSim(std::unique_ptr<ecs::ComponentCatalog> catalog, std::unique_ptr<content::ContentDatabase> content,
              std::string scenario, render::MaterialLibrary& materials);
    void stepTick();

    std::unique_ptr<ecs::ComponentCatalog> m_catalog; // 월드보다 오래 산다
    std::unique_ptr<content::ContentDatabase> m_content;
    std::unique_ptr<scenario::ScenarioRunner> m_runner;
    std::string m_name;
    SpriteExtraction m_extraction;
    PositionHistory m_previous;
    f64 m_accum = 0; // 다음 틱까지 쌓인 시간 (초)
    bool m_paused = false;
    usize m_speedIndex = 2; // ×1
    f32 m_speed = 1.f;
    f64 m_tickSeconds = 1.0 / 30.0;
    u64 m_droppedTicks = 0;
};

} // namespace sbx::client
