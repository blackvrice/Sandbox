#pragma once
// 스프라이트 → 정렬된 GPU 인스턴스 + Draw 묶음. 백엔드와 무관한 순수 로직 (모든 OS 에서 단위 테스트).
// docs/06-RENDERING.md 8.2 · 8.5, ADR-0020.
//
//   1. 컬링     카메라에 보이는 사각형과 겹치지 않으면 버린다 (회전을 생각해 외접원 반경으로)
//   2. 정렬     64비트 키 [ pass:4 | layer:8 | pipeline:12 | material:20 | depth:20 ], 같은 키는 제출 순서 (stable)
//   3. 묶음     같은 (pipeline, material) 이 이어지는 구간 = Draw 하나. 인스턴스 상한마다 나눈다
// material 은 텍스처 배열(아틀라스) 번호다. 8A 는 아틀라스가 하나라 레이어 순서와 무관하게 대개 Draw 1개.

#include <array>
#include <span>
#include <vector>

#include "render/renderer/RenderWorld.hpp"

namespace sbx::render {

// 아틀라스 안의 자리 (AssetManager 가 SpriteId 로 준다)
struct SpriteRegion {
    u32 atlas = 0;                             // 텍스처 배열 번호 (정렬 키의 material)
    u32 page = 0;                              // 배열 층
    std::array<f32, 4> uv{0.f, 0.f, 1.f, 1.f}; // (u0, v0, u1, v1), v0 = 위
};

// GPU 인스턴스 정점 (shaders/sprite.hlsl 의 InstanceIn 과 같은 배치, 48 바이트)
struct SpriteInstanceGpu {
    f32 position[2];
    f32 size[2];
    f32 rotation;
    u32 page;
    f32 uv[4];
    u32 color; // RGBA8 → 셰이더에서 float4 (R8G8B8A8_UNORM)
    u32 flags;
};
static_assert(sizeof(SpriteInstanceGpu) == 48);

struct SpriteBatch {
    u32 atlas = 0;
    u32 firstInstance = 0;
    u32 instanceCount = 0;
};

struct SpriteBatchStats {
    u32 submitted = 0;
    u32 culled = 0;
    u32 drawn = 0;
    u32 batches = 0;
};

enum class RenderPassId : u8 { Terrain = 0, WorldSprite = 1, Grid = 2, Selection = 3, Debug = 4, Ui = 5 };

// 정렬 키. depth 는 단조 변환한 float 의 상위 20 비트 (가까운 값끼리는 같아질 수 있다 — 그때는 제출 순서)
[[nodiscard]] u64 spriteSortKey(RenderPassId pass, u8 layer, u32 pipeline, u32 material, f32 depth) noexcept;

class SpriteBatcher {
public:
    // regions[spriteId]. 범위 밖 id 는 regions[kWhiteSprite]. maxPerBatch: Draw 하나의 인스턴스 상한
    void build(std::span<const SpriteDraw> sprites, std::span<const SpriteRegion> regions, const Camera2D& camera,
               u32 maxPerBatch = 1u << 16);

    [[nodiscard]] std::span<const SpriteInstanceGpu> instances() const noexcept { return m_instances; }
    [[nodiscard]] std::span<const SpriteBatch> batches() const noexcept { return m_batches; }
    [[nodiscard]] const SpriteBatchStats& stats() const noexcept { return m_stats; }

private:
    struct Keyed {
        u64 key;
        u32 index;
    };
    void radixSortByKey();

    std::vector<Keyed> m_order;
    std::vector<Keyed> m_scratch;
    std::vector<SpriteInstanceGpu> m_instances;
    std::vector<SpriteBatch> m_batches;
    SpriteBatchStats m_stats;
};

} // namespace sbx::render
