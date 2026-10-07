#include "apps/client/DirectSim.hpp"

#include <algorithm>
#include <format>

#include "core/components/RegisterCoreComponents.hpp"
#include "core/components/core/Transform.hpp"
#include "foundation/log/Log.hpp"

namespace sbx::client {

Expected<std::unique_ptr<DirectSim>> DirectSim::create(const DirectSimDesc& desc, render::MaterialLibrary& materials) {
    auto catalog = std::make_unique<ecs::ComponentCatalog>();
    if (auto r = comp::registerCoreComponents(*catalog); !r) {
        return std::unexpected(r.error());
    }
    auto sc = scenario::makeScenario(desc.scenario);
    if (sc == nullptr) {
        std::string names;
        for (const auto& e : scenario::scenarioList()) {
            names += names.empty() ? "" : ", ";
            names += e.name;
        }
        return makeError(ErrorCode::NotFound,
                         std::format("알 수 없는 시나리오 '{}' (있는 것: {})", desc.scenario, names));
    }
    auto db = scenario::loadScenarioContent(*sc, desc.contentRoot, *catalog);
    if (!db) {
        return std::unexpected(db.error());
    }
    auto content = std::make_unique<content::ContentDatabase>(std::move(*db));
    std::unique_ptr<DirectSim> ds(new DirectSim(std::move(catalog), std::move(content), desc.scenario, materials));
    ds->m_runner = std::make_unique<scenario::ScenarioRunner>(*ds->m_catalog, *ds->m_content, std::move(sc), desc.seed);
    ds->m_tickSeconds = static_cast<f64>(ds->world().clock().tickIntervalNanos()) / 1e9;
    log::info("client",
              "direct-sim: 시나리오 {} seed {} — 클라이언트가 시뮬레이션을 직접 돌린다 (임시, Phase 10 에서 삭제)",
              desc.scenario, desc.seed);
    return ds;
}

DirectSim::DirectSim(std::unique_ptr<ecs::ComponentCatalog> catalog, std::unique_ptr<content::ContentDatabase> content,
                     std::string scenario, render::MaterialLibrary& materials)
    : m_catalog(std::move(catalog)), m_content(std::move(content)), m_name(std::move(scenario)),
      m_extraction(materials) {}

sim::Tick DirectSim::tick() const {
    return m_runner->world().currentTick();
}

void DirectSim::stepTick() {
    // 보간용: 이번 틱 직전 위치
    m_previous.clear();
    for (auto [e, t, p] :
         m_runner->world().registry().view<ecs::Read<comp::Transform>, ecs::Read<comp::Persistence>>()) {
        (void)e;
        m_previous[p.saveId] = t.position;
    }
    m_runner->step();
}

void DirectSim::update(f64 dtSeconds) {
    if (m_paused) {
        m_accum = 0;
        return;
    }
    m_accum += std::clamp(dtSeconds, 0.0, 0.25) * m_speed;
    u32 steps = 0;
    while (m_accum >= m_tickSeconds && steps < kMaxCatchUpTicks) {
        stepTick();
        m_accum -= m_tickSeconds;
        ++steps;
    }
    if (m_accum >= m_tickSeconds) {
        // 따라잡기 상한: 밀린 시간은 통째로 버린다 (실시간보다 느려질 뿐 결과는 같다. 나머지를 남기면 부동소수 오차로
        // 다음 프레임에 틱이 하나 더 끼어든다)
        m_droppedTicks += static_cast<u64>(m_accum / m_tickSeconds);
        m_accum = 0;
    }
}

void DirectSim::stepOnce() {
    if (m_paused) {
        stepTick();
    }
}

void DirectSim::changeSpeed(int dir) {
    const usize n = std::size(kSpeeds);
    if (dir > 0 && m_speedIndex + 1 < n) {
        ++m_speedIndex;
    } else if (dir < 0 && m_speedIndex > 0) {
        --m_speedIndex;
    }
    m_speed = kSpeeds[m_speedIndex];
}

f32 DirectSim::alpha() const noexcept {
    return m_paused ? 1.f : static_cast<f32>(std::clamp(m_accum / m_tickSeconds, 0.0, 1.0));
}

void DirectSim::extract(render::RenderWorld& out) {
    m_extraction.extract(m_runner->world(), m_previous, alpha(), out);
}

render::WorldRect DirectSim::bounds() const {
    return SpriteExtraction::worldBounds(m_runner->world());
}

std::string DirectSim::status() const {
    return std::format("{} tick {} · 개체 {} · ×{}{}", m_name, tick(), m_extraction.stats().entities, m_speed,
                       m_paused ? " · 일시정지" : "");
}

} // namespace sbx::client
