#pragma once
// 클라이언트의 복제 월드 — 서버 월드의 Replicated 컴포넌트 · Opaque(render.*) · 지형 사본. docs/08-NETWORK.md 6.4 · 7 ·
// 9장, ADR-0025.
//
//   시뮬레이션하지 않는다 (System 없음, tick() 없음). 받은 Snapshot · TerrainChunk 를 적용할 뿐이다.
//   엔티티 키 = NetEntityId (재사용 없음). 로컬 EntityId 는 이 프로세스 안에서만 의미가 있다 (조회 표 — 04 4.2 위반
//   아님). Snapshot 적용 규칙
//     epoch 이 더 크면 모두 지우고 시작 (서버가 다시 맞춘다). 작으면 버린다.
//     snapshotId 가 마지막으로 적용한 것 이하면 버린다 (늦게 온 옛 패킷).
//     despawns → 파괴. entities → 없으면 만든다(spawn 이 아니어도 — 방어), mask 에 없는 복제 컴포넌트는 떼고, 받은 값을
//     쓴다. EntityRef 필드는 netId → 로컬 EntityId (모르면 null). [계획] 대기 목록 — 나중에 생기면 다시 연결 (6.4)
//   보간용 표본: core.transform 이 갱신될 때마다 (serverTick, 위치) 를 엔티티마다 둘 (직전 · 지금) 남긴다 (9장 — 그리는
//   쪽이 renderTick 으로 보간한다, sampleTransform). 직전 표본의 틱은 적어도 직전 스냅숏의 틱 (멈춰 있다 움직인 개체).
// 한 스레드에서 쓴다 (ClientSession 과 같은 스레드).

#include <optional>
#include <unordered_map>
#include <vector>

#include "core/content/ContentDatabase.hpp"
#include "core/ecs/ComponentCatalog.hpp"
#include "core/ecs/Registry.hpp"
#include "core/world/WorldGrid.hpp"
#include "network/protocol/Messages.hpp"

namespace sbx::net {

struct ClientWorldStats {
    u64 snapshotsApplied = 0;
    u64 snapshotsDropped = 0; // 옛 · 다른 epoch
    u64 resets = 0;           // 새 epoch 로 다시 맞춤
    u64 spawns = 0;
    u64 updates = 0;
    u64 despawns = 0;
    u64 unknownComponents = 0; // 표에 있지만 이 빌드가 모르는 컴포넌트 (건너뜀)
    u64 decodeErrors = 0;      // 컴포넌트 바이트를 읽지 못함 (그 컴포넌트를 뗀다)
    u64 terrainChunks = 0;
};

// core.transform 표본 (서버 틱 기준)
struct TransformSample {
    u64 tick = 0;
    Vec2 position;
    f32 rotation = 0;
};
struct TransformTrack {
    TransformSample previous;
    TransformSample current;
    u8 samples = 0; // 0 ~ 2 (previous 가 의미 있으려면 2)
};

// renderTick 에서의 위치 · 회전 (9장): 직전 · 지금 사이를 선형 보간, 구간 밖은 끝값 (외삽 없음 [계획]). 두 표본이
// 같은 틱(일시정지 편집)이거나 한 구간에 teleportDistance 넘게 움직였으면 지금 값
[[nodiscard]] TransformSample sampleTransform(const TransformTrack& track, f64 renderTick,
                                              f32 teleportDistance = 8.0f) noexcept;

class ClientWorld {
public:
    // welcome 의 경계 · 복제 표로 만든다. catalog · content 는 이 객체보다 오래 살아야 한다
    static Expected<std::unique_ptr<ClientWorld>>
    create(const ecs::ComponentCatalog& catalog, const content::ContentDatabase& content, const Welcome& welcome);

    // 적용했으면 true (옛 것 · 다른 epoch 은 false)
    bool apply(const Snapshot& snapshot);
    void apply(const TerrainChunk& chunk);

    [[nodiscard]] const ecs::Registry& registry() const noexcept { return m_registry; }
    [[nodiscard]] const world::WorldGrid& grid() const noexcept { return m_grid; }
    [[nodiscard]] const content::ContentDatabase& content() const noexcept { return m_content; }
    [[nodiscard]] const ecs::ComponentCatalog& catalog() const noexcept { return m_catalog; }
    [[nodiscard]] ecs::EntityId find(NetEntityId id) const noexcept;
    [[nodiscard]] usize entityCount() const noexcept { return m_netToEntity.size(); }
    // 서버가 해석하지 않는 컴포넌트 (spawn 때 받은 것, 없으면 nullptr) — {"render.sprite": {"version": 1, "value":
    // {…}}}
    [[nodiscard]] const ecs::Json* opaque(NetEntityId id) const noexcept;
    [[nodiscard]] const TransformTrack* transformTrack(NetEntityId id) const noexcept;
    // 모든 엔티티 (netId 오름차순 아님 — 조회 표 순서와 무관하게 쓰려면 정렬)
    [[nodiscard]] const std::unordered_map<NetEntityId, ecs::EntityId>& entities() const noexcept {
        return m_netToEntity;
    }

    [[nodiscard]] u64 serverTick() const noexcept { return m_serverTick; }
    [[nodiscard]] bool paused() const noexcept { return m_paused; }
    [[nodiscard]] f32 speed() const noexcept { return m_speed; }
    [[nodiscard]] u32 epoch() const noexcept { return m_epoch; }
    [[nodiscard]] u32 lastSnapshotId() const noexcept { return m_lastSnapshot; }
    [[nodiscard]] bool lastSnapshotComplete() const noexcept { return m_lastComplete; }
    [[nodiscard]] const ClientWorldStats& stats() const noexcept { return m_stats; }
    // 복제 표 칸 → 컴포넌트 (이 빌드가 모르면 nullptr)
    [[nodiscard]] const ecs::ComponentInfo* replicatedInfo(usize index) const noexcept {
        return index < m_table.size() ? m_table[index] : nullptr;
    }
    [[nodiscard]] usize replicatedCount() const noexcept { return m_table.size(); }

    ClientWorld(const ClientWorld&) = delete;
    ClientWorld& operator=(const ClientWorld&) = delete;
    ClientWorld(ClientWorld&&) = delete;
    ClientWorld& operator=(ClientWorld&&) = delete;
    ~ClientWorld();

private:
    class Refs;
    ClientWorld(const ecs::ComponentCatalog& catalog, const content::ContentDatabase& content, world::WorldGrid grid);
    void clearAll();
    void destroy(NetEntityId id);
    ecs::ComponentPoolBase& pool(const ecs::ComponentInfo& info);

    const ecs::ComponentCatalog& m_catalog;
    const content::ContentDatabase& m_content;
    ecs::Registry m_registry;
    world::WorldGrid m_grid;
    std::vector<const ecs::ComponentInfo*> m_table; // Welcome.replicated 순서
    usize m_transformIndex = static_cast<usize>(-1);
    std::unordered_map<NetEntityId, ecs::EntityId> m_netToEntity; // 조회 전용
    std::unordered_map<NetEntityId, ecs::Json> m_opaque;
    std::unordered_map<NetEntityId, TransformTrack> m_tracks;
    std::unique_ptr<Refs> m_refs;
    u32 m_epoch = 0;
    u32 m_lastSnapshot = 0;
    bool m_lastComplete = false;
    u64 m_serverTick = 0;
    bool m_paused = false;
    f32 m_speed = 1.0f;
    ClientWorldStats m_stats;
};

} // namespace sbx::net
