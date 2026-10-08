#include "network/server/LocalServerHost.hpp"

#include "foundation/log/Log.hpp"

namespace sbx::net {

Expected<std::unique_ptr<LocalServerHost>> LocalServerHost::create(const ecs::ComponentCatalog& catalog,
                                                                   const LocalServerDesc& desc) {
    auto src = scenario::openWorldSource(desc.world, desc.contentRoot, desc.seed, catalog);
    if (!src) {
        return std::unexpected(src.error());
    }
    std::unique_ptr<LocalServerHost> h(new LocalServerHost());
    h->m_serverTransport = std::make_unique<LoopbackTransport>(h->m_hub);
    h->m_clientTransport = std::make_unique<LoopbackTransport>(h->m_hub);
    h->m_jobs = std::make_unique<JobSystem>(desc.workers);
    h->m_content = std::move(src->content);
    h->m_name = src->name;
    h->m_warnings = std::move(src->warnings);
    src->runner->world().setJobSystem(h->m_jobs.get());

    ServerHostDesc sd;
    sd.worldName = src->name;
    sd.maxClients = 4;
    sd.defaultRole = Role::Owner; // 혼자 하는 게임 — 일시정지 · 속도 · 편집 모두
    sd.buildId = localBuildId();
    sd.packs = src->packs;
    sd.mode = desc.mode;
    sd.snapshotIntervalSteps = desc.snapshotIntervalSteps;
    sd.snapshotBytesPerSecond = 0; // Loopback — 예산 없음
    h->m_host = std::make_unique<ServerHost>(*h->m_serverTransport, std::move(src->runner), sd);
    if (auto r = h->m_host->start(h->endpoint()); !r) {
        return std::unexpected(r.error());
    }
    log::info("net", "로컬 서버: {} ({}, Worker {})", h->m_name,
              desc.mode == ServerMode::Threaded ? "Simulation · Net 스레드" : "프레임 안에서", desc.workers);
    return h;
}

LocalServerHost::~LocalServerHost() {
    if (m_host) {
        m_host->stop();
    }
}

void LocalServerHost::update(f64 dtSeconds) {
    m_host->update(dtSeconds);
}

} // namespace sbx::net
