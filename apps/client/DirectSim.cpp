#include "apps/client/DirectSim.hpp"

#include <algorithm>
#include <format>

#include "core/components/RegisterCoreComponents.hpp"
#include "foundation/assert/Assert.hpp"
#include "foundation/log/Log.hpp"

namespace sbx::client {

using Clock = std::chrono::steady_clock;

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
    std::unique_ptr<DirectSim> ds(
        new DirectSim(std::move(catalog), std::move(content), desc.scenario, materials, desc));
    ds->m_runner = std::make_unique<scenario::ScenarioRunner>(*ds->m_catalog, *ds->m_content, std::move(sc), desc.seed);
    sim::SimulationWorld& w = ds->m_runner->world();
    w.setJobSystem(ds->m_jobs.get());
    ds->m_tickSeconds = static_cast<f64>(w.clock().tickIntervalNanos()) / 1e9;
    ds->m_bounds = SpriteExtraction::worldBounds(w);

    // 처음 스냅숏 (틱 0) — 첫 프레임부터 그릴 것이 있게
    auto first = std::make_shared<WorldSnapshot>();
    ds->m_extraction.capture(w, *first);
    ds->m_published = std::move(first);
    ds->m_publishedAt = Clock::now();
    ds->m_tpsWindowStart = ds->m_publishedAt;

    if (ds->m_mode == DirectSimMode::Threaded) {
        ds->m_thread = std::thread([p = ds.get()] { p->threadMain(); });
    }
    log::info("client",
              "direct-sim: 시나리오 {} seed {} — 클라이언트가 시뮬레이션을 직접 돌린다 ({}, Worker {}. 임시, Phase 10 "
              "에서 삭제)",
              desc.scenario, desc.seed, ds->m_mode == DirectSimMode::Threaded ? "Simulation 스레드" : "프레임 안에서",
              desc.workers);
    return ds;
}

DirectSim::DirectSim(std::unique_ptr<ecs::ComponentCatalog> catalog, std::unique_ptr<content::ContentDatabase> content,
                     std::string scenario, render::MaterialLibrary& materials, const DirectSimDesc& desc)
    : m_catalog(std::move(catalog)), m_content(std::move(content)), m_jobs(std::make_unique<JobSystem>(desc.workers)),
      m_name(std::move(scenario)), m_mode(desc.mode), m_extraction(materials) {}

DirectSim::~DirectSim() {
    if (m_thread.joinable()) {
        {
            std::lock_guard lk(m_mutex);
            m_stop = true;
        }
        m_cv.notify_all();
        m_thread.join();
    }
}

sim::SimulationWorld& DirectSim::world() {
    SBX_ASSERT(m_mode == DirectSimMode::Inline, "Threaded DirectSim 의 월드는 Simulation 스레드만 만진다 (T1)");
    return m_runner->world();
}

f64 DirectSim::tickInterval() const {
    return m_tickSeconds / static_cast<f64>(kSpeeds[m_speedIndex]);
}

void DirectSim::stepAndPublish() {
    const auto start = Clock::now();
    m_runner->step();

    std::shared_ptr<WorldSnapshot> buffer;
    {
        std::lock_guard lk(m_mutex);
        buffer = std::move(m_spare);
    }
    if (!buffer) {
        buffer = std::make_shared<WorldSnapshot>();
    }
    m_extraction.capture(m_runner->world(), *buffer);
    const auto end = Clock::now();
    const f64 secs = std::chrono::duration<f64>(end - start).count();

    {
        std::lock_guard lk(m_mutex);
        std::shared_ptr<const WorldSnapshot> old = std::move(m_published);
        m_published = std::move(buffer);
        m_publishedAt = end;
        // 아무도 들고 있지 않으면 다음 capture 에 다시 쓴다 (틱마다 10k × 40 바이트를 새로 잡지 않게).
        // m_published 에서 빠졌으므로 다른 스레드가 새로 잡을 수 없다 — use_count 1 이면 혼자다
        if (old && old.use_count() == 1) {
            m_spare = std::const_pointer_cast<WorldSnapshot>(old);
        }
        DirectSimStats& s = m_stats;
        ++s.ticks;
        s.tickSecondsTotal += secs;
        s.recentTickMs = s.ticks == 1 ? secs * 1000.0 : s.recentTickMs * 0.9 + secs * 100.0;
        ++m_tpsWindowTicks;
        const f64 window = std::chrono::duration<f64>(end - m_tpsWindowStart).count();
        if (window >= 1.0) {
            s.ticksPerSecond = static_cast<f64>(m_tpsWindowTicks) / window;
            m_tpsWindowTicks = 0;
            m_tpsWindowStart = end;
        }
    }
    m_cv.notify_all();
}

void DirectSim::threadMain() {
    std::unique_lock lk(m_mutex);
    auto next = Clock::now();
    m_tpsWindowStart = next;
    m_tpsWindowTicks = 0;
    while (!m_stop) {
        if (m_paused) {
            if (m_stepRequests == 0) {
                m_cv.wait(lk);
                next = Clock::now(); // 다시 진행할 때 쉬는 동안의 시간을 몰아 돌지 않게
                m_tpsWindowStart = next;
                m_tpsWindowTicks = 0;
                continue;
            }
            --m_stepRequests;
        } else {
            m_stepRequests = 0; // 진행 중의 "한 틱" 은 무시 (Inline 과 같다)
            const auto now = Clock::now();
            if (now < next) {
                m_cv.wait_until(lk, next); // 제어가 바뀌어도 깬다
                continue;
            }
            const f64 interval = tickInterval();
            next += std::chrono::duration_cast<Clock::duration>(std::chrono::duration<f64>(interval));
            const f64 lag = std::chrono::duration<f64>(now - next).count();
            if (lag > kMaxLagSeconds) {
                // 틱이 틱 간격보다 오래 걸린다 — 밀린 시간은 버리고 지금부터 다시 센다 (시뮬레이션만 느려진다)
                m_stats.droppedTicks += static_cast<u64>(lag / interval);
                next = now;
            }
        }
        lk.unlock();
        stepAndPublish();
        lk.lock();
    }
}

void DirectSim::update(f64 dtSeconds) {
    if (m_mode == DirectSimMode::Threaded) {
        return; // Simulation 스레드가 스스로 진행한다
    }
    f64 speed = 1;
    {
        std::lock_guard lk(m_mutex);
        if (m_paused) {
            m_accum = 0;
            return;
        }
        speed = kSpeeds[m_speedIndex];
    }
    // m_accum 은 Inline 에서 이 스레드만 만진다
    m_accum += std::clamp(dtSeconds, 0.0, 0.25) * speed;
    u32 steps = 0;
    while (m_accum >= m_tickSeconds && steps < kMaxCatchUpTicks) {
        stepAndPublish();
        m_accum -= m_tickSeconds;
        ++steps;
    }
    if (m_accum >= m_tickSeconds) {
        // 따라잡기 상한: 밀린 시간은 통째로 버린다 (실시간보다 느려질 뿐 결과는 같다. 나머지를 남기면 부동소수 오차로
        // 다음 프레임에 틱이 하나 더 끼어든다)
        std::lock_guard lk(m_mutex);
        m_stats.droppedTicks += static_cast<u64>(m_accum / m_tickSeconds);
        m_accum = 0;
    }
}

void DirectSim::togglePause() {
    {
        std::lock_guard lk(m_mutex);
        m_paused = !m_paused;
    }
    m_cv.notify_all();
}

void DirectSim::stepOnce() {
    if (m_mode == DirectSimMode::Inline) {
        if (paused()) {
            stepAndPublish();
        }
        return;
    }
    {
        std::lock_guard lk(m_mutex);
        if (!m_paused) {
            return;
        }
        ++m_stepRequests;
    }
    m_cv.notify_all();
}

void DirectSim::changeSpeed(int dir) {
    {
        std::lock_guard lk(m_mutex);
        const usize n = std::size(kSpeeds);
        if (dir > 0 && m_speedIndex + 1 < n) {
            ++m_speedIndex;
        } else if (dir < 0 && m_speedIndex > 0) {
            --m_speedIndex;
        }
    }
    m_cv.notify_all();
}

bool DirectSim::paused() const {
    std::lock_guard lk(m_mutex);
    return m_paused;
}

f32 DirectSim::speed() const {
    std::lock_guard lk(m_mutex);
    return kSpeeds[m_speedIndex];
}

namespace {

// 보간 계수. Threaded 는 스냅숏이 나온 뒤 흐른 시간 / 실제 틱 간격 (시뮬레이션이 실시간보다 느리면 틱 시간 쪽)
f32 alphaOf(DirectSimMode mode, bool paused, f64 accum, f64 tickSeconds, f64 interval, f64 recentTickMs,
            Clock::time_point publishedAt) {
    if (paused) {
        return 1.f;
    }
    if (mode == DirectSimMode::Inline) {
        return static_cast<f32>(std::clamp(accum / tickSeconds, 0.0, 1.0));
    }
    const f64 elapsed = std::chrono::duration<f64>(Clock::now() - publishedAt).count();
    const f64 span = std::max(interval, recentTickMs / 1000.0);
    return static_cast<f32>(std::clamp(elapsed / span, 0.0, 1.0));
}

} // namespace

f32 DirectSim::alpha() const {
    std::lock_guard lk(m_mutex);
    return alphaOf(m_mode, m_paused, m_accum, m_tickSeconds, tickInterval(), m_stats.recentTickMs, m_publishedAt);
}

void DirectSim::extract(render::RenderWorld& out) {
    std::shared_ptr<const WorldSnapshot> snap;
    f32 a = 1;
    {
        std::lock_guard lk(m_mutex);
        snap = m_published;
        a = alphaOf(m_mode, m_paused, m_accum, m_tickSeconds, tickInterval(), m_stats.recentTickMs, m_publishedAt);
    }
    SpriteExtraction::emit(*snap, a, out); // 락 밖에서 (Simulation 스레드를 막지 않게)
}

sim::Tick DirectSim::tick() const {
    std::lock_guard lk(m_mutex);
    return m_published->tick;
}

ExtractionStats DirectSim::extractionStats() const {
    std::lock_guard lk(m_mutex);
    return m_published->stats;
}

DirectSimStats DirectSim::stats() const {
    std::lock_guard lk(m_mutex);
    return m_stats;
}

f64 DirectSim::averageTickMs() const {
    const DirectSimStats s = stats();
    return s.ticks == 0 ? 0.0 : s.tickSecondsTotal * 1000.0 / static_cast<f64>(s.ticks);
}

bool DirectSim::waitForTick(sim::Tick target, std::chrono::milliseconds timeout) const {
    std::unique_lock lk(m_mutex);
    return m_cv.wait_for(lk, timeout, [&] { return m_published->tick >= target; });
}

std::string DirectSim::status() const {
    std::lock_guard lk(m_mutex);
    std::string s = std::format("{} tick {} · 개체 {} · ×{}", m_name, m_published->tick, m_published->stats.entities,
                                kSpeeds[m_speedIndex]);
    if (m_paused) {
        s += " · 일시정지";
    } else if (m_mode == DirectSimMode::Threaded && m_stats.ticksPerSecond > 0) {
        // 목표(30 × 속도)보다 낮으면 시뮬레이션이 실시간을 못 따라가는 것 — 화면은 따로 제 속도로 그린다
        s += std::format(" · {:.1f}/{:.0f} TPS", m_stats.ticksPerSecond, 1.0 / tickInterval());
    }
    s += std::format(" · 틱 {:.1f} ms", m_stats.recentTickMs);
    return s;
}

} // namespace sbx::client
