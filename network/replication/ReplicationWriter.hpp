#pragma once
// 서버 쪽 복제: 클라이언트마다 확인(ack)된 기준(baseline) 대비 차분 Snapshot 과 바뀐 지형 청크를 만든다.
// docs/08-NETWORK.md 6 · 7장, ADR-0025. Simulation 스레드에서 돈다 (월드를 읽는다 — T1).
//
// 클라이언트마다 보낸 스냅숏 기록 32 개 (history). 기록 = netId 오름차순 {netId, mask, stamp, pendingSpawn}:
//   "이 스냅숏을 적용한 클라이언트는 이 엔티티를 변경 순번 stamp 까지 반영한 값으로 가지고 있다"
//   (stamp = SimulationWorld::changeStamp — 틱 번호가 아니라 tick() 마다 오르는 순번이라 일시정지 편집도 잡힌다)
// 스냅숏 만들기 (B = 가장 최근 ack 된 기록, inflight = B 이후 보냈지만 ack 안 된 기록들)
//   지금 있는 엔티티 e
//     B 에 없다(또는 pendingSpawn)           → spawn: 복제 컴포넌트 전부 + Opaque(render.* JSON)
//     B 에 있다 → 바뀐 컴포넌트: B 에 없던 것, 또는 changedAt > stamp. mask 가 B · inflight 와 다르면 mask 도.
//                 아무것도 없으면 보내지 않는다
//   (B ∪ inflight) 에는 있는데 지금 없다 → despawn (ack 될 때까지 매번 — 손실 복구, 08 7장 표)
//   잃은 스냅숏은 다시 보내지 않는다 — 다음 차분이 더 오래된 B 에서 계산되어 저절로 복구된다 (6.1)
// 예산: bytesPerSnapshot 를 넘으면 나머지 spawn · update 는 다음으로 미룬다 (B 의 기록을 그대로 이어 받아 다음에 다시
// 계산).
//       시작 위치를 돌려 가며(round-robin) 모두 차례가 온다. despawn 은 예산과 무관하게 언제나.
// 다시 맞추기(resync): ack 된 기준이 기록 밖으로 밀려났고 epoch 의 첫 스냅숏도 밀려났으면 epoch 을 올리고 처음부터
// (받는
//       쪽은 새 epoch 에서 모두 지운다). 처음 접속도 epoch 1 로 시작.
// 지형: 청크 revision 이 마지막으로 보낸 것과 다르면 TerrainChunk (Bulk — 신뢰 채널이라 보낸 것으로 친다). 한 번에
// 상한. [계획] Interest(구독 청크 · 히스테리시스 — Phase 11), 우선순위(선택 · 카메라 거리), 양자화, 필드 단위 마스크,
// Event 복제,
//        여러 클라이언트에 같은 컴포넌트 바이트 재사용.

#include <deque>
#include <map>
#include <span>
#include <vector>

#include "core/simulation/SimulationWorld.hpp"
#include "network/protocol/Messages.hpp"

namespace sbx::net {

struct ReplicationDesc {
    u32 historySize = 32;
    usize bytesPerSnapshot = 0; // 0 = 제한 없음 (Loopback 싱글플레이). 원격은 초당 예산 ÷ 스냅숏 빈도
    usize terrainChunksPerSnapshot = 64;
};

struct ReplicationClientStats {
    u32 epoch = 0;
    u32 lastSnapshotId = 0;
    u32 ackedSnapshotId = 0;
    u64 resyncs = 0;
    u64 deferred = 0;    // 예산 때문에 미룬 엔티티 (누적)
    usize lastBytes = 0; // 마지막 스냅숏의 대략적인 크기
    usize terrainSent = 0;
};

class ReplicationWriter {
public:
    explicit ReplicationWriter(const ecs::ComponentCatalog& catalog, ReplicationDesc desc = {});
    ~ReplicationWriter();
    ReplicationWriter(const ReplicationWriter&) = delete;
    ReplicationWriter& operator=(const ReplicationWriter&) = delete;
    ReplicationWriter(ReplicationWriter&&) = delete;
    ReplicationWriter& operator=(ReplicationWriter&&) = delete;

    // 복제 컴포넌트 표 (stableId 오름차순, net.identity 와 ServerOnly 제외) — Welcome.replicated
    [[nodiscard]] const std::vector<u64>& table() const noexcept { return m_table; }

    void addClient(u16 clientId);
    void removeClient(u16 clientId);
    void onAck(u16 clientId, u32 epoch, u32 snapshotId);
    // 모든 클라이언트의 이번 스냅숏 (+ 지형 청크) — (clientId, 메시지)
    void build(const sim::SimulationWorld& world, std::vector<std::pair<u16, Message>>& out);

    [[nodiscard]] const ReplicationClientStats* stats(u16 clientId) const;
    void setBytesPerSnapshot(usize bytes) noexcept { m_desc.bytesPerSnapshot = bytes; }

private:
    struct EntityRecord {
        NetEntityId netId = kInvalidNetEntityId;
        bool pendingSpawn = false; // 클라이언트가 가지고 있을 수 있지만 완전히 보낸 적이 확인되지 않음
        u64 mask = 0;
        u64 stamp = 0;
    };
    struct SnapshotRecord {
        u32 id = 0;
        std::vector<EntityRecord> entities; // netId 오름차순
    };
    struct Client {
        u32 epoch = 0;
        u32 nextId = 1;
        u32 acked = 0;
        bool needsReset = true;
        bool epochStartDropped = false;
        usize cursor = 0;
        std::deque<SnapshotRecord> history;
        std::map<std::pair<i32, i32>, u64> terrainRevision; // 보낸 청크 revision
        ReplicationClientStats stats;
    };
    struct Current {
        NetEntityId netId;
        ecs::EntityId entity;
        u64 mask;
    };
    class Refs;

    void collect(const sim::SimulationWorld& world);
    Snapshot buildFor(Client& c, const sim::SimulationWorld& world);
    void buildTerrain(Client& c, const sim::SimulationWorld& world, u16 clientId,
                      std::vector<std::pair<u16, Message>>& out);
    void resetEpoch(Client& c);

    ReplicationDesc m_desc;
    std::vector<u64> m_table;
    std::vector<const ecs::ComponentInfo*> m_infos;
    std::map<u16, Client> m_clients;
    // 이번 build 의 월드 요약 (모든 클라이언트가 같이 쓴다)
    std::vector<Current> m_current;
    std::vector<const ecs::ComponentPoolBase*> m_pools;
};

} // namespace sbx::net
