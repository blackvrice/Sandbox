#pragma once
// ClientWorld(지금은 --direct-sim 의 SimulationWorld) → RenderWorld 스프라이트. docs/01-ARCHITECTURE.md 3장
// (Presentation), docs/06-RENDERING.md 8.1 · 9장, ADR-0020.
//
//   엔티티 (core.transform + persist.persistence)
//     render.sprite 가 있으면 (Opaque: {"material", "size": [w, h], "layer"}) → 머티리얼의 스프라이트 · 색
//     없으면 "debug/entity" 머티리얼의 작은 표식 (0.5 크기, layer 5) — 콘텐츠가 그림을 정하지 않은 엔티티도 보이게
//   위치는 직전 틱과 지금 틱 사이를 alpha 로 보간 (렌더 프레임이 30 TPS 보다 촘촘해도 매끄럽게)
//   depth = -y (아래쪽이 앞 — 탑다운에서 겹칠 때 자연스러운 순서)
//   월드 배경: 경계 크기의 사각형 하나, 머티리얼 "terrain/<fillMaterial>" (지형 패스는 8B [계획])
//
// 시뮬레이션 상태를 바꾸지 않는다 (R4 — 읽기만). render.sprite 해석은 saveId 별로 캐시한다 (JSON 을 프레임마다 읽지
// 않게).

#include <unordered_map>

#include "core/components/core/Identity.hpp"
#include "core/simulation/SimulationWorld.hpp"
#include "foundation/math/Vec2.hpp"
#include "render/asset/MaterialLibrary.hpp"
#include "render/renderer/RenderWorld.hpp"

namespace sbx::client {

using PositionHistory = std::unordered_map<SaveId, Vec2>;

struct ExtractionStats {
    u32 entities = 0;
    u32 withSprite = 0; // render.sprite 가 있는 것
    u32 cached = 0;     // 캐시 크기
};

class SpriteExtraction {
public:
    explicit SpriteExtraction(render::MaterialLibrary& materials) : m_materials(materials) {}

    // out.sprites 를 채운다 (비우지 않는다 — 호출자가 reset). previous 는 직전 틱 위치 (없으면 보간하지 않는다)
    void extract(sim::SimulationWorld& world, const PositionHistory& previous, f32 alpha, render::RenderWorld& out);

    // 월드 경계 (월드 단위 — 타일)
    [[nodiscard]] static render::WorldRect worldBounds(const sim::SimulationWorld& world);

    [[nodiscard]] const ExtractionStats& stats() const noexcept { return m_stats; }

private:
    struct Resolved {
        render::Material material;
        Vec2 size{1, 1};
        u8 layer = 0;
        bool fromContent = false;
    };
    const Resolved& resolve(const sim::SimulationWorld& world, SaveId saveId);

    render::MaterialLibrary& m_materials;
    std::unordered_map<SaveId, Resolved> m_cache;
    u64 m_frame = 0;
    ExtractionStats m_stats;
};

} // namespace sbx::client
