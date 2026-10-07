#pragma once
// ClientWorld(지금은 --direct-sim 의 SimulationWorld) → RenderWorld. docs/01-ARCHITECTURE.md 3장 (Presentation) ·
// 5장(T1 · T2), docs/06-RENDERING.md 8.1 · 9장, ADR-0020 · ADR-0021 · ADR-0022.
//
// 두 단계로 나뉜다 — 월드를 읽는 쪽과 그리는 쪽이 다른 스레드일 수 있다 (T1: 월드는 Simulation 스레드만 읽는다):
//   capture  (Simulation 스레드, 틱마다)  월드 → WorldSnapshot (불변 복사본 — T2)
//     엔티티 (core.transform + persist.persistence)
//       render.sprite 가 있으면 (Opaque: {"material", "size": [w, h], "layer"}) → 머티리얼의 스프라이트 · 색
//       없으면 "debug/entity" 머티리얼의 작은 표식 (0.5 크기, layer 5) — 콘텐츠가 그림을 정하지 않은 엔티티도 보이게
//     직전 capture 의 위치를 함께 담는다 (보간용). render.sprite 해석은 saveId 별로 캐시 (JSON 을 틱마다 읽지 않게)
//     지형 (8B): 청크마다 머티리얼 번호를 revision 이 바뀔 때만 새로 복사한다 — 바뀌지 않은 청크는 이전 스냅숏과 공유.
//       팔레트 = 콘텐츠 머티리얼 순서대로 MaterialLibrary 의 "terrain/<id>" 색
//     선택한 개체 (8B): 프리팹 · 행동 상태 · 에너지 · 체력 · 감지 반경 · 남은 경로 · 대상 · 속도 (SelectedDetail)
//   emit     (Main 스레드, 프레임마다)  WorldSnapshot + alpha → 지형 · SpriteDraw
//     위치 = 직전과 지금 사이를 alpha 로 보간 (렌더 프레임이 30 TPS 보다 촘촘해도 매끄럽게), depth = -y (아래쪽이 앞)
//   선택 외곽선 · 디버그 선은 presentation/SelectionOverlay 가 스냅숏에서 만든다.
//
// 시뮬레이션 상태를 바꾸지 않는다 (R4 — 읽기만). MaterialLibrary::find 는 capture 에서만 부른다 (경고 기록이 바뀌므로
// 한 스레드에서만).

#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/components/core/Identity.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "foundation/math/Vec2.hpp"
#include "render/asset/MaterialLibrary.hpp"
#include "render/renderer/RenderWorld.hpp"

namespace sbx::client {

struct ExtractionStats {
    u32 entities = 0;
    u32 withSprite = 0;    // render.sprite 가 있는 것
    u32 cached = 0;        // 캐시 크기
    u32 terrainCopied = 0; // 이번 capture 에서 새로 복사한 지형 청크
};

// 엔티티 하나의 그릴 거리 (48 바이트)
struct SnapshotSprite {
    SaveId id = kInvalidSaveId;
    Vec2 previous; // 직전 capture 의 위치 (처음 보이면 current 와 같다)
    Vec2 current;
    Vec2 size{1, 1};
    f32 rotation = 0;
    render::SpriteId sprite = render::kWhiteSprite;
    u32 color = 0xFFFF'FFFFu;
    u8 layer = 0;
};

// 선택한 개체의 자세한 상태 (capture 때 월드에서 읽는다 — 상한 kMaxSelectedDetails)
struct SelectedDetail {
    SaveId id = kInvalidSaveId;
    std::string prefab; // core.prefab (없으면 빈 문자열)
    std::string state;  // ai.behavior 의 지금 상태 id (없으면 빈 문자열)
    Vec2 position;
    Vec2 velocity;
    std::optional<f32> energy, energyMax, health, healthMax;
    f32 sensorRadius = 0;       // ai.sensor (없으면 0)
    std::vector<Vec2> path;     // 남은 경유점 (cursor 부터)
    std::optional<Vec2> goal;   // ai.path 의 목표 (따라가는 중 · 대기 중일 때)
    std::optional<Vec2> target; // ai.behavior.target 의 위치 (살아 있으면)
};

// 한 틱의 그릴 거리. capture 뒤에는 바꾸지 않는다 (다른 스레드가 shared_ptr<const> 로 읽는다)
struct WorldSnapshot {
    sim::Tick tick = 0;
    render::TerrainView terrain;
    std::vector<SnapshotSprite> sprites;
    std::vector<SelectedDetail> selected; // capture 때의 선택 순서 (saveId 오름차순)
    ExtractionStats stats;
};

class SpriteExtraction {
public:
    static constexpr usize kMaxSelectedDetails = 32;

    explicit SpriteExtraction(render::MaterialLibrary& materials);

    // 월드 → out (out 의 이전 내용은 지운다 — 용량은 재사용). selection = 자세히 담을 saveId (오름차순)
    void capture(sim::SimulationWorld& world, std::span<const SaveId> selection, WorldSnapshot& out);
    // out 에 지형 · 스프라이트를 채운다 (스프라이트는 비우지 않고 더한다 — 호출자가 reset). alpha 0 = 직전, 1 = 지금
    static void emit(const WorldSnapshot& snapshot, f32 alpha, render::RenderWorld& out);

    // 월드 경계 (월드 단위 — 타일)
    [[nodiscard]] static render::WorldRect worldBounds(const sim::SimulationWorld& world);

private:
    struct Resolved {
        render::Material material;
        Vec2 size{1, 1};
        u8 layer = 0;
        bool fromContent = false;
    };
    const Resolved& resolve(const sim::SimulationWorld& world, SaveId saveId);
    void captureTerrain(const sim::SimulationWorld& world, WorldSnapshot& out);
    void captureSelected(sim::SimulationWorld& world, std::span<const SaveId> selection, WorldSnapshot& out);

    render::MaterialLibrary& m_materials;
    u64 m_worldId; // 이 Extraction 이 보는 월드 (TerrainPass 캐시 구분)
    std::unordered_map<SaveId, Resolved> m_cache;
    std::unordered_map<SaveId, Vec2> m_lastPositions; // 직전 capture 의 위치
    std::unordered_map<SaveId, Vec2> m_nextPositions; // 이번 capture 에서 채워 바꿔 끼운다 (할당 재사용)
    std::vector<render::TerrainChunk> m_terrain;      // 청크마다 마지막으로 복사한 것 (grid.chunks() 순서)
    std::shared_ptr<const std::vector<u32>> m_palette;
    u64 m_captures = 0;
};

} // namespace sbx::client
