#pragma once
// --direct-sim: 클라이언트 프로세스 안에서 SimulationWorld 를 직접 돌린다. docs/16-ROADMAP.md 8.5 (임시 — Phase 10.5
// 에서 삭제), docs/01-ARCHITECTURE.md 5장, ADR-0020 · ADR-0021.
//
//   시나리오(core/scenarios) 하나를 ScenarioRunner 로. 틱 속도는 월드 시계(30 TPS) × speed.
//   일시정지 · 한 틱 · 속도는 클라이언트 쪽 진행만 바꾼다 (월드에 명령을 넣지 않는다 — 결정론 경로와 무관).
//   틱마다 SpriteExtraction::capture 로 WorldSnapshot(불변)을 만들어 내놓고, extract 는 최신 스냅숏을 보간해 그린다.
//
// 두 가지 진행 방식 (ADR-0021)
//   Threaded  창 실행. Simulation 스레드가 월드를 소유하고 30 TPS × speed 에 맞춰 틱을 돈다 (T1). 틱이 느리면(Debug
//             빌드, 큰 월드) 시뮬레이션만 실시간보다 느려지고 화면 · 카메라는 제 속도로 그린다. 밀린 시간이
//             kMaxLagSeconds 를 넘으면 버린다 (따라잡으려 몰아 돌지 않는다). update() 는 아무것도 하지 않는다.
//   Inline    --headless · 단위 테스트. update(dt) 가 그 자리에서 틱을 돈다 — 같은 dt 열이면 같은 틱 수.
//             한 프레임에 따라잡는 틱은 kMaxCatchUpTicks 까지 (남은 시간은 버린다).
// 시뮬레이션의 Worker(경로 · 감지 Job)는 DirectSim 이 소유한다. 결과는 Worker 수와 무관하다 (D5).

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "apps/client/WorldSession.hpp"
#include "apps/client/presentation/SpriteExtraction.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "core/scenarios/Scenario.hpp"
#include "foundation/job/JobSystem.hpp"
#include "foundation/types/Error.hpp"

namespace sbx::client {

enum class DirectSimMode : u8 { Inline = 0, Threaded };

struct DirectSimDesc {
    std::string scenario;
    u64 seed = 1;
    std::filesystem::path contentRoot;
    DirectSimMode mode = DirectSimMode::Inline;
    u32 workers = 0; // 시뮬레이션 Job Worker 수 (0 = 없음)
};

// 진행 상태 (제목 줄 · 끝 요약). Simulation 스레드가 쓰고 다른 스레드는 복사본을 읽는다
struct DirectSimStats {
    u64 ticks = 0;            // 이 세션에서 돈 틱 수
    f64 tickSecondsTotal = 0; // 그 틱들의 실제 시간 합 (step + capture)
    f64 recentTickMs = 0;     // 최근 틱 시간 (지수 평균)
    f64 ticksPerSecond = 0;   // 최근 1 초의 실제 진행 속도 (Threaded)
    u64 droppedTicks = 0;     // 따라잡지 않고 버린 틱 몫
};

class DirectSim final : public IWorldSession {
public:
    static Expected<std::unique_ptr<DirectSim>> create(const DirectSimDesc& desc, render::MaterialLibrary& materials);
    ~DirectSim() override; // Threaded: 스레드를 멈추고 기다린다

    DirectSim(const DirectSim&) = delete;
    DirectSim& operator=(const DirectSim&) = delete;
    DirectSim(DirectSim&&) = delete;
    DirectSim& operator=(DirectSim&&) = delete;

    void update(f64 dtSeconds) override;
    void extract(render::RenderWorld& out) override;
    [[nodiscard]] render::WorldRect bounds() const override { return m_bounds; }
    void togglePause() override;
    void stepOnce() override;
    void changeSpeed(int dir) override;
    [[nodiscard]] std::string status() const override;

    [[nodiscard]] DirectSimMode mode() const noexcept { return m_mode; }
    [[nodiscard]] bool paused() const;
    [[nodiscard]] f32 speed() const;
    // 다음 extract 의 보간 계수 (Inline: 쌓인 시간 / 틱 간격, Threaded: 마지막 스냅숏 뒤 경과 / 틱 간격)
    [[nodiscard]] f32 alpha() const;
    // 최신 스냅숏의 틱 (Threaded 에서도 안전)
    [[nodiscard]] sim::Tick tick() const;
    [[nodiscard]] ExtractionStats extractionStats() const;
    [[nodiscard]] DirectSimStats stats() const;
    [[nodiscard]] f64 averageTickMs() const;
    // Threaded: 최신 스냅숏의 틱이 target 이상이 될 때까지 (시험용). 시간이 넘으면 false
    bool waitForTick(sim::Tick target, std::chrono::milliseconds timeout) const;
    // 월드 직접 접근 — Inline 에서만 (Threaded 는 Simulation 스레드가 소유한다, T1)
    [[nodiscard]] sim::SimulationWorld& world();

    static constexpr u32 kMaxCatchUpTicks = 4;  // Inline
    static constexpr f64 kMaxLagSeconds = 0.25; // Threaded: 이보다 밀리면 버린다
    static constexpr f32 kSpeeds[] = {0.25f, 0.5f, 1.f, 2.f, 4.f, 8.f};

private:
    DirectSim(std::unique_ptr<ecs::ComponentCatalog> catalog, std::unique_ptr<content::ContentDatabase> content,
              std::string scenario, render::MaterialLibrary& materials, const DirectSimDesc& desc);
    // 틱 하나 + capture + 내놓기. Inline 은 호출 스레드, Threaded 는 Simulation 스레드에서
    void stepAndPublish();
    void threadMain();
    [[nodiscard]] f64 tickInterval() const; // 실제 초 (speed 반영), m_mutex 안에서

    std::unique_ptr<ecs::ComponentCatalog> m_catalog; // 월드보다 오래 산다
    std::unique_ptr<content::ContentDatabase> m_content;
    std::unique_ptr<JobSystem> m_jobs; // 월드보다 오래 산다
    std::unique_ptr<scenario::ScenarioRunner> m_runner;
    std::string m_name;
    DirectSimMode m_mode;
    SpriteExtraction m_extraction; // Simulation 쪽만 쓴다
    render::WorldRect m_bounds;
    f64 m_tickSeconds = 1.0 / 30.0; // 월드 시계의 틱 간격 (speed 미반영)

    // 아래는 m_mutex 로 지킨다 (Inline 에서는 한 스레드라 다툼이 없지만 같은 경로를 쓴다)
    mutable std::mutex m_mutex;
    mutable std::condition_variable m_cv; // 제어 변경 · 새 스냅숏
    std::shared_ptr<const WorldSnapshot> m_published;
    std::chrono::steady_clock::time_point m_publishedAt;
    std::shared_ptr<WorldSnapshot> m_spare; // 다시 쓸 버퍼 (아무도 안 잡고 있을 때만)
    DirectSimStats m_stats;
    bool m_paused = false;
    u32 m_stepRequests = 0;
    usize m_speedIndex = 2; // ×1
    bool m_stop = false;
    f64 m_accum = 0; // Inline: 다음 틱까지 쌓인 시간 (초, speed 반영)
    // Threaded: 속도 측정 (1 초 창)
    u64 m_tpsWindowTicks = 0;
    std::chrono::steady_clock::time_point m_tpsWindowStart;

    std::thread m_thread; // 마지막에 — 나머지가 다 만들어진 뒤 시작한다
};

} // namespace sbx::client
