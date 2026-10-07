#pragma once
// 프레임마다 다시 채우는 그릴 거리 목록. docs/06-RENDERING.md 8.1, ADR-0020.
// Renderer 의 유일한 입력이다 (R1 — SandboxRender 는 ECS 를 모른다). 채우는 쪽은 SandboxClient/presentation 의
// Extraction. 지속 상태(텍스처 · 아틀라스)는 AssetManager 에 있다.
//
// Phase 8A: 카메라 · 배경색 · 스프라이트. 8B: 지형(TerrainView) · 선택 외곽선 · DebugDraw · 격자. [계획] UI(8C).

#include <memory>
#include <vector>

#include "foundation/math/Vec2.hpp"
#include "render/renderer/Camera2D.hpp"
#include "render/renderer/DebugDraw.hpp"
#include "render/rhi/RhiTypes.hpp"

namespace sbx::render {

// AssetManager 가 준 스프라이트 번호. 0 = 흰색 1 텍셀 (색만 있는 사각형). 8A 는 해제하지 않는다 (번호 재사용 없음).
using SpriteId = u32;
inline constexpr SpriteId kWhiteSprite = 0;

// RGBA8 (r 이 최하위 바이트 — 메모리 순서 R, G, B, A)
[[nodiscard]] constexpr u32 packRgba8(u8 r, u8 g, u8 b, u8 a = 255) noexcept {
    return static_cast<u32>(r) | (static_cast<u32>(g) << 8) | (static_cast<u32>(b) << 16) | (static_cast<u32>(a) << 24);
}

enum SpriteFlags : u32 {
    kSpriteFlipX = 1u << 0,
    kSpriteFlipY = 1u << 1,
};

struct SpriteDraw {
    Vec2 position{};  // 월드, 사각형 가운데
    Vec2 size{1, 1};  // 월드 단위
    f32 rotation = 0; // 라디안, 반시계
    SpriteId sprite = kWhiteSprite;
    u32 color = 0xFFFF'FFFFu; // 곱하는 색 (RGBA8)
    u32 flags = 0;            // SpriteFlags
    u8 layer = 0;             // 큰 값이 위
    // 같은 레이어 안의 순서: 작은 값이 먼저(아래) 그려진다. 탑다운이면 -y 를 넣어 아래쪽(앞)이 위에 오게 한다.
    f32 depth = 0;
};

// ---- 8B: 지형 (TerrainPass, ADR-0022) ----
// 월드 타일 격자 = 청크(chunkSize² 타일)의 직사각형. 타일 하나 = 월드 1 단위, 청크 (cx, cy) 의 왼쪽 아래 타일 =
// (cx · chunkSize, cy · chunkSize). 타일 값은 머티리얼 번호 (palette 의 칸).
struct TerrainChunk {
    i32 x = 0; // 청크 좌표
    i32 y = 0;
    u64 revision = 0; // 바뀌면 다시 올린다 (같은 revision 이면 내용이 같다고 믿는다)
    // chunkSize² 개, 행 우선 · 행 = 청크 안 y (아래부터). Extraction 이 revision 이 바뀔 때만 새로 만든다 (공유)
    std::shared_ptr<const std::vector<u16>> tiles;
};

struct TerrainView {
    u64 worldId = 0;   // 다른 월드(세션)면 캐시를 버린다. 0 = 지형 없음
    i32 chunkSize = 0; // 타일
    i32 minChunkX = 0;
    i32 minChunkY = 0;
    i32 chunksX = 0;
    i32 chunksY = 0;
    std::vector<TerrainChunk> chunks; // 순서 무관, 빠진 청크는 이전 내용 그대로
    // 머티리얼 번호 → RGBA8. paletteVersion 이 바뀌면 다시 올린다
    std::shared_ptr<const std::vector<u32>> palette;
    u64 paletteVersion = 0;

    [[nodiscard]] bool empty() const noexcept { return worldId == 0 || chunkSize <= 0 || chunksX <= 0 || chunksY <= 0; }
    [[nodiscard]] Vec2 worldMin() const noexcept {
        return {static_cast<f32>(minChunkX * chunkSize), static_cast<f32>(minChunkY * chunkSize)};
    }
    [[nodiscard]] Vec2 worldSize() const noexcept {
        return {static_cast<f32>(chunksX * chunkSize), static_cast<f32>(chunksY * chunkSize)};
    }
};

// ---- 8B: 오버레이 ----
struct OverlayOptions {
    bool grid = false; // GridPass: 타일 · 청크 격자 + 월드 경계 (확대할수록 타일 선이 보인다)
};

struct RenderWorld {
    Camera2D camera;
    rhi::ClearColor clear{0.08f, 0.09f, 0.10f, 1.f};
    TerrainView terrain;             // TerrainPass (스프라이트 아래)
    std::vector<SpriteDraw> sprites; // WorldSpritePass
    OverlayOptions overlay;          // GridPass
    DebugDrawList selection;         // SelectionPass: 선택 외곽선 · 박스 선택 사각형
    DebugDrawList debug;             // DebugPass: 경로 · 감지 반경 · 청크 경계

    // 프레임마다 다시 채우는 것만 비운다 (카메라 · 지형 · 오버레이 설정은 그대로)
    void reset() {
        sprites.clear();
        selection.clear();
        debug.clear();
    }
};

} // namespace sbx::render
