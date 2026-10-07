#pragma once
// 프레임마다 다시 채우는 그릴 거리 목록. docs/06-RENDERING.md 8.1, ADR-0020.
// Renderer 의 유일한 입력이다 (R1 — SandboxRender 는 ECS 를 모른다). 채우는 쪽은 SandboxClient/presentation 의
// Extraction. 지속 상태(텍스처 · 아틀라스)는 AssetManager 에 있다.
//
// Phase 8A: 카메라 · 배경색 · 스프라이트. [계획] 지형 청크(8B) · DebugDraw · 오버레이(8B) · UI(8C).

#include <vector>

#include "foundation/math/Vec2.hpp"
#include "render/renderer/Camera2D.hpp"
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

struct RenderWorld {
    Camera2D camera;
    rhi::ClearColor clear{0.08f, 0.09f, 0.10f, 1.f};
    std::vector<SpriteDraw> sprites;

    void reset() { sprites.clear(); }
};

} // namespace sbx::render
