#pragma once
// ClientWorld(서버 월드의 복제본) → RenderWorld. docs/01-ARCHITECTURE.md 3장 (Presentation), docs/06-RENDERING.md 8.1 ·
// 9장, docs/08-NETWORK.md 9장, ADR-0020 · ADR-0022 · ADR-0026.
//
// Main 스레드에서 프레임마다 (ClientWorld 는 NetworkSession 이 같은 스레드에서 적용한다 — 복사본이 필요 없다):
//   capture  ClientWorld + renderTick → WorldSnapshot (이번 프레임의 그릴 거리)
//     엔티티 (core.transform + net.identity): 위치 · 회전 = transform 표본을 renderTick 으로 보간 (sampleTransform)
//       render.sprite 가 있으면 (Opaque: {"material", "size": [w, h], "layer"}) → 머티리얼의 스프라이트 · 색
//       없으면 "debug/entity" 머티리얼의 작은 표식 (0.5 크기, layer 5) — 콘텐츠가 그림을 정하지 않은 엔티티도 보이게
//       render.sprite 해석은 netId 별로 캐시 (JSON 을 프레임마다 읽지 않게. netId 는 재사용되지 않는다)
//     지형 (8B): 청크마다 머티리얼 번호를 revision 이 바뀔 때만 새로 복사한다 — 바뀌지 않은 청크는 이전 것과 공유.
//       팔레트 = 콘텐츠 머티리얼 순서대로 MaterialLibrary 의 "terrain/<id>" 색
//     선택한 개체 (8B): 프리팹 · 에너지 · 체력 · 위치 · 속도는 ClientWorld 에서, 행동 상태 · 감지 반경 · 경로 · 대상은
//       서버의 InspectResult 에서 (서버 전용 컴포넌트 — 10B)
//   emit     WorldSnapshot → 지형 · SpriteDraw (depth = -y, 아래쪽이 앞)
//   선택 외곽선 · 디버그 선은 presentation/SelectionOverlay 가 스냅숏에서 만든다.
//
// 월드를 바꾸지 않는다 (R4 — 읽기만). MaterialLibrary::find 는 capture 에서만 부른다 (경고 기록이 바뀌므로 한 스레드).

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/components/core/Identity.hpp"
#include "foundation/math/Vec2.hpp"
#include "network/client/ClientWorld.hpp"
#include "render/asset/MaterialLibrary.hpp"
#include "render/renderer/RenderWorld.hpp"

namespace sbx::client {

struct ExtractionStats {
    u32 entities = 0;
    u32 withSprite = 0;    // render.sprite 가 있는 것
    u32 cached = 0;        // 캐시 크기
    u32 terrainCopied = 0; // 이번 capture 에서 새로 복사한 지형 청크
};

// 엔티티 하나의 그릴 거리
struct SnapshotSprite {
    NetEntityId id = kInvalidNetEntityId;
    Vec2 position; // renderTick 의 위치
    Vec2 size{1, 1};
    f32 rotation = 0;
    render::SpriteId sprite = render::kWhiteSprite;
    u32 color = 0xFFFF'FFFFu;
    u8 layer = 0;
};

// 선택한 개체의 자세한 상태 (상한 kMaxSelectedDetails)
struct SelectedDetail {
    NetEntityId id = kInvalidNetEntityId;
    std::string prefab; // core.prefab (없으면 빈 문자열)
    std::string state;  // ai.behavior 의 지금 상태 id (서버가 아직 안 알려 줬으면 빈 문자열)
    Vec2 position;
    Vec2 velocity;
    std::optional<f32> energy, energyMax, health, healthMax;
    f32 sensorRadius = 0;       // ai.sensor (없으면 0)
    std::vector<Vec2> path;     // 남은 경유점
    std::optional<Vec2> goal;   // ai.path 의 목표 (따라가는 중 · 대기 중일 때)
    std::optional<Vec2> target; // ai.behavior.target 의 위치 (보이는 개체면)
};

// 한 프레임의 그릴 거리
struct WorldSnapshot {
    u64 serverTick = 0; // 마지막으로 적용한 스냅숏의 서버 틱
    f64 renderTick = 0; // 그린 틱 (보간)
    render::TerrainView terrain;
    std::vector<SnapshotSprite> sprites;
    std::vector<SelectedDetail> selected; // selection 순서 (netId 오름차순)
    ExtractionStats stats;
};

class SpriteExtraction {
public:
    static constexpr usize kMaxSelectedDetails = 32;

    explicit SpriteExtraction(render::MaterialLibrary& materials);

    // 월드 → out (out 의 이전 내용은 지운다 — 용량은 재사용). selection = 자세히 담을 netId (오름차순).
    // inspect = 서버가 준 선택 상세 (없으면 null)
    void capture(const net::ClientWorld& world, f64 renderTick, std::span<const NetEntityId> selection,
                 const net::InspectResult* inspect, WorldSnapshot& out);
    // out 에 지형 · 스프라이트를 채운다 (스프라이트는 비우지 않고 더한다 — 호출자가 reset)
    static void emit(const WorldSnapshot& snapshot, render::RenderWorld& out);
    // 다른 월드(다시 접속)를 보기 시작한다 — 캐시를 비우고 지형 캐시 id 를 새로
    void reset();

    // 월드 경계 (월드 단위 — 타일)
    [[nodiscard]] static render::WorldRect worldBounds(const net::ClientWorld& world);

private:
    struct Resolved {
        render::Material material;
        Vec2 size{1, 1};
        u8 layer = 0;
        bool fromContent = false;
    };
    const Resolved& resolve(const net::ClientWorld& world, NetEntityId id);
    void captureTerrain(const net::ClientWorld& world, WorldSnapshot& out);
    void captureSelected(const net::ClientWorld& world, f64 renderTick, std::span<const NetEntityId> selection,
                         const net::InspectResult* inspect, WorldSnapshot& out);

    render::MaterialLibrary& m_materials;
    u64 m_worldId; // 이 Extraction 이 보는 월드 (TerrainPass 캐시 구분)
    std::unordered_map<NetEntityId, Resolved> m_cache;
    std::vector<render::TerrainChunk> m_terrain; // 청크마다 마지막으로 복사한 것 (grid.chunks() 순서)
    std::shared_ptr<const std::vector<u32>> m_palette;
    u64 m_captures = 0;
};

} // namespace sbx::client
