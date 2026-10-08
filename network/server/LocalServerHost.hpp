#pragma once
// 싱글플레이 = 같은 프로세스 안의 서버 (Phase 10B, docs/16-ROADMAP.md 10.5, docs/01-ARCHITECTURE.md 4장, ADR-0026).
//
//   ServerHost + LoopbackTransport 둘(허브 하나). 클라이언트는 원격 서버와 똑같이 ClientSession 으로 접속한다 — 명령 ·
//   복제 · 선택 상세가 모두 같은 길 (--direct-sim 처럼 클라이언트가 월드를 직접 만지는 길이 없다).
//   차이: 예산 없음(Loopback), 역할 Owner (혼자니까), 월드 = 시나리오 이름 또는 세이브 폴더 (openWorldSource).
//   Threaded 면 Simulation · Net IO 스레드가 돈다 (창 실행). Inline 이면 update(dt) 가 그 자리에서 (헤드리스 · 시험 —
//   같은 dt 열이면 같은 결과). 클라이언트 쪽 Transport 는 clientTransport() — 호출자 스레드 하나에서 쓴다.

#include <filesystem>
#include <memory>
#include <string>

#include "core/scenarios/WorldSource.hpp"
#include "foundation/job/JobSystem.hpp"
#include "network/server/ServerHost.hpp"
#include "network/transport/LoopbackTransport.hpp"

namespace sbx::net {

struct LocalServerDesc {
    std::string world;                 // 시나리오 이름 또는 세이브 폴더
    std::filesystem::path contentRoot; // 콘텐츠 팩 루트
    u64 seed = 1;
    u32 workers = 0; // 시뮬레이션 Job Worker 수 (0 = 없음 — 결과는 같다, D5)
    ServerMode mode = ServerMode::Threaded;
    u32 snapshotIntervalSteps = 2;
};

class LocalServerHost {
public:
    // catalog 는 이 객체보다 오래 살아야 한다
    static Expected<std::unique_ptr<LocalServerHost>> create(const ecs::ComponentCatalog& catalog,
                                                             const LocalServerDesc& desc);
    ~LocalServerHost(); // 서버를 멈춘다 (접속자에게 Disconnect{ServerShutdown})

    LocalServerHost(const LocalServerHost&) = delete;
    LocalServerHost& operator=(const LocalServerHost&) = delete;
    LocalServerHost(LocalServerHost&&) = delete;
    LocalServerHost& operator=(LocalServerHost&&) = delete;

    [[nodiscard]] INetworkTransport& clientTransport() noexcept { return *m_clientTransport; }
    [[nodiscard]] Endpoint endpoint() const { return {"local", 0}; }
    // 서버 월드의 콘텐츠 (클라이언트 복제 월드도 같은 것을 읽는다 — 둘 다 읽기만)
    [[nodiscard]] const content::ContentDatabase& content() const noexcept { return *m_content; }
    [[nodiscard]] const std::string& name() const noexcept { return m_name; }
    [[nodiscard]] const std::vector<std::string>& warnings() const noexcept { return m_warnings; }
    // Inline 전용
    void update(f64 dtSeconds);
    [[nodiscard]] ServerHostStats stats() const { return m_host->stats(); }
    [[nodiscard]] ServerHost& host() noexcept { return *m_host; }

private:
    LocalServerHost() = default;

    LoopbackNetwork m_hub;
    std::unique_ptr<LoopbackTransport> m_serverTransport;
    std::unique_ptr<LoopbackTransport> m_clientTransport;
    std::unique_ptr<JobSystem> m_jobs;                   // 월드보다 오래 산다
    std::unique_ptr<content::ContentDatabase> m_content; // 월드보다 오래 산다
    std::string m_name;
    std::vector<std::string> m_warnings;
    std::unique_ptr<ServerHost> m_host; // 마지막에 — 먼저 사라진다
};

} // namespace sbx::net
