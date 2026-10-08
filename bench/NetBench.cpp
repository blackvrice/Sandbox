// 네트워크 벤치: net.snapshot · net.late_join (docs/14-PERFORMANCE.md 2장, Phase 11 완료 기준 — 클라이언트당 바이트 ∝
// 가시 엔티티, 새 접속 < 2 초).
//
// random_walk 50k (24 × 24 청크) 를 예열한 뒤 ReplicationWriter 로 2 틱마다 스냅숏을 만든다 (와이어 인코딩 크기까지).
// 클라이언트 1 · 4 · 16, 관심 = 월드 전체 · 화면 크기(5 × 4 청크 — 1600 × 900 을 32 px/칸으로 본 것 + 여유) 를
// 클라이언트마다 다른 곳에 두고 30 스냅숏마다 한 청크씩 옮긴다. 예산 없음 (보내고 싶은 만큼 — 비례를 보려고), ack 는
// 바로 (손실 없음). 재는 것: 스냅숏 만들기 ms (모든 클라이언트), 클라이언트당 스냅숏 바이트 · 관심 엔티티.
#include <chrono>
#include <cstdio>
#include <memory>
#include <set>
#include <vector>

#include "BenchUtil.hpp"
#include "core/components/RegisterCoreComponents.hpp"
#include "core/scenarios/RandomWalkScenario.hpp"
#include "network/replication/ReplicationWriter.hpp"

namespace sbx::bench {
namespace {

void snapshot(Context& ctx, const ecs::ComponentCatalog& catalog, u32 clients, bool interest,
              scenario::ScenarioRunner& runner) {
    net::ReplicationWriter writer(catalog);
    for (u16 c = 1; c <= clients; ++c) {
        writer.addClient(c);
    }
    const int snapshots = ctx.quick ? 8 : 60;
    // 화면 하나 = 5 × 4 청크. 클라이언트마다 다른 자리 (24 × 24 청크 안에 흩어 놓는다)
    auto viewOf = [&](u16 c, int step) {
        const i32 x = -12 + static_cast<i32>((c * 7 + step / 30) % 19);
        const i32 y = -12 + static_cast<i32>((c * 5) % 20);
        return net::Subscribe{false, x, y, x + 4, y + 3};
    };
    std::vector<std::pair<u16, net::Message>> out;
    double buildMs = 0;
    double collectMs = 0;
    double bytes = 0;
    double relevant = 0;
    int measured = 0;
    for (int i = 0; i < snapshots + 2; ++i) {
        if (interest) {
            for (u16 c = 1; c <= clients; ++c) {
                writer.setInterest(c, viewOf(c, i));
            }
        }
        runner.step();
        runner.step();
        out.clear();
        const auto t0 = std::chrono::steady_clock::now();
        writer.build(runner.world(), out);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        usize snapBytes = 0;
        for (const auto& [id, m] : out) {
            if (const auto* s = std::get_if<net::Snapshot>(&m)) {
                snapBytes += net::encodeMessage(m).size();
                writer.onAck(id, s->epoch, s->snapshotId);
            }
        }
        if (i < 2) {
            continue; // 첫 스냅숏(전부 spawn)은 빼고 잰다 — 보통 상태의 차분
        }
        buildMs += ms;
        collectMs += writer.lastCollectMs();
        bytes += static_cast<double>(snapBytes) / clients;
        for (u16 c = 1; c <= clients; ++c) {
            relevant += static_cast<double>(writer.stats(c)->relevant) / clients;
        }
        ++measured;
    }
    buildMs /= measured;
    collectMs /= measured;
    bytes /= measured;
    relevant /= measured;
    const double kbps = bytes * 15.0 / 1024.0;
    ctx.results.push_back({{"scenario", "net.snapshot"},
                           {"params",
                            {{"entities", runner.world().registry().aliveCount()},
                             {"clients", clients},
                             {"interest", interest ? "screen" : "all"}}},
                           {"metrics",
                            {{"build_ms", buildMs},
                             {"collect_ms", collectMs},
                             {"bytes_per_client", bytes},
                             {"kbps_per_client_15hz", kbps},
                             {"relevant_per_client", relevant}}}});
    std::printf("  net.snapshot entities=%zu clients=%-2u interest=%-6s  build %.2f ms (훑기 %.2f)  %.0f B/client "
                "(%.0f KB/s at 15 Hz)  relevant %.0f\n",
                runner.world().registry().aliveCount(), clients, interest ? "screen" : "all", buildMs, collectMs, bytes,
                kbps, relevant);
}

// net.late_join: 새 클라이언트가 관심 영역을 다 받기까지 (예산 256 KB/s ÷ 15 Hz, 손실 없음). Phase 11 완료 기준 < 2 초
void lateJoin(Context& ctx, const ecs::ComponentCatalog& catalog, bool screen, scenario::ScenarioRunner& runner) {
    net::ReplicationDesc d;
    d.bytesPerSnapshot = 256 * 1024 * 2 / 30;
    net::ReplicationWriter writer(catalog, d);
    writer.addClient(1);
    if (screen) {
        writer.setInterest(1, net::Subscribe{false, -3, -2, 1, 1}); // 화면 하나 (5 × 4 청크) — 접속 직후 보낸다
    }
    // "다 받았다" = 클라이언트가 가진 개체(spawn − despawn) 수가 관심 개체 수의 99 % 에 닿았다. 계속 움직이는 개체의
    // 모든 갱신이 예산에 들어가는지(complete)와는 다르다 — 1.8k 개체가 매 틱 움직이면 256 KB/s 로는 매번 다 못 보낸다
    std::vector<std::pair<u16, net::Message>> out;
    std::set<NetEntityId> known;
    int snapshots = 0;
    bool done = false;
    usize bytes = 0;
    while (!done && snapshots < 1000) {
        runner.step();
        runner.step();
        out.clear();
        writer.build(runner.world(), out);
        ++snapshots;
        for (const auto& [id, m] : out) {
            bytes += net::encodeMessage(m).size();
            if (const auto* sn = std::get_if<net::Snapshot>(&m)) {
                for (const net::EntityState& e : sn->entities) {
                    known.insert(e.netId);
                }
                for (const NetEntityId gone : sn->despawns) {
                    known.erase(gone);
                }
                writer.onAck(id, sn->epoch, sn->snapshotId);
            }
        }
        // 99 % — 개체가 계속 관심 경계를 넘나들고 태어나므로 "전부" 는 순간마다 몇 개 모자란다
        done = static_cast<double>(known.size()) >= 0.99 * static_cast<double>(writer.stats(1)->relevant);
    }
    const double seconds = snapshots / 15.0;
    ctx.results.push_back({{"scenario", "net.late_join"},
                           {"params",
                            {{"entities", runner.world().registry().aliveCount()},
                             {"interest", screen ? "screen" : "all"},
                             {"kbps", 256}}},
                           {"metrics",
                            {{"snapshots", snapshots},
                             {"seconds", seconds},
                             {"bytes", bytes},
                             {"relevant", writer.stats(1)->relevant}}}});
    std::printf("  net.late_join entities=%zu interest=%-6s  %d 스냅숏 = %.2f 초 (256 KB/s)  %zu B  relevant %zu\n",
                runner.world().registry().aliveCount(), screen ? "screen" : "all", snapshots, seconds, bytes,
                writer.stats(1)->relevant);
}

} // namespace

void runNetBenchmarks(Context& ctx) {
    const bool snap = wants(ctx, "net.snapshot");
    const bool join = wants(ctx, "net.late_join");
    if (!snap && !join) {
        return;
    }
    ecs::ComponentCatalog catalog;
    if (!comp::registerCoreComponents(catalog)) {
        std::abort();
    }
    const u32 entities = ctx.quick ? 10000 : 50000;
    scenario::RandomWalkParams p;
    p.entityCount = entities;
    p.bounds = world::GridBounds{world::ChunkCoord{-12, -12}, world::ChunkCoord{11, 11}};
    scenario::ScenarioRunner runner(catalog, content::ContentDatabase::builtin(),
                                    std::make_unique<scenario::RandomWalkScenario>("net_bench", p), 1);
    if (!runner.runUntil(30)) {
        std::fprintf(stderr, "net.snapshot: 예열 실패\n");
        std::abort();
    }
    if (join) {
        lateJoin(ctx, catalog, true, runner);
        lateJoin(ctx, catalog, false, runner);
    }
    for (const u32 clients : {1u, 4u, 16u}) {
        if (!snap || (ctx.quick && clients > 4)) {
            break;
        }
        snapshot(ctx, catalog, clients, false, runner);
        snapshot(ctx, catalog, clients, true, runner);
    }
}

} // namespace sbx::bench
