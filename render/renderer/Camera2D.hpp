#pragma once
// 2D 카메라. docs/06-RENDERING.md 8.1 · 12장, ADR-0020.
//
// 좌표:
//   월드   x 오른쪽 +, y 위 + (05-WORLD 1장, 단위 = 타일 1칸)
//   화면   픽셀, 좌상단 원점, y 아래 + (창 · 마우스와 같다)
//   NDC    y 위 + (06 12장)
//   화면 px = (월드 - center) · ppu + 뷰포트/2  (y 는 뒤집힌다)
// 줌은 pixelsPerUnit(월드 1 단위가 몇 픽셀인가)로 표현한다 — 클수록 확대.

#include <array>

#include "foundation/math/Vec2.hpp"
#include "foundation/types/Types.hpp"

namespace sbx::render {

struct WorldRect {
    Vec2 min{};
    Vec2 max{};
    [[nodiscard]] constexpr bool overlaps(const WorldRect& o) const noexcept {
        return min.x <= o.max.x && o.min.x <= max.x && min.y <= o.max.y && o.min.y <= max.y;
    }
};

struct Camera2D {
    Vec2 center{};            // 화면 가운데에 오는 월드 점
    f32 pixelsPerUnit = 32.f; // 줌
    u32 viewportWidth = 1;    // 픽셀
    u32 viewportHeight = 1;

    static constexpr f32 kMinPixelsPerUnit = 0.25f;
    static constexpr f32 kMaxPixelsPerUnit = 512.f;

    [[nodiscard]] Vec2 worldToScreen(Vec2 world) const noexcept;
    [[nodiscard]] Vec2 screenToWorld(Vec2 screen) const noexcept;
    // 월드 → NDC 를 ndc = world · scale + offset 로 (셰이더 상수): {scaleX, scaleY, offsetX, offsetY}
    [[nodiscard]] std::array<f32, 4> clipTransform() const noexcept;
    // 화면에 보이는 월드 사각형
    [[nodiscard]] WorldRect visibleRect() const noexcept;

    // 화면 픽셀 delta 만큼 끌기 (마우스가 잡은 월드 점이 커서를 따라온다)
    void panByScreen(Vec2 screenDelta) noexcept;
    // screen 픽셀 아래의 월드 점을 고정한 채 factor 배 확대 (범위로 자른다)
    void zoomAt(Vec2 screen, f32 factor) noexcept;
    // 월드 사각형 전체가 보이게 (여백 margin 비율)
    void fit(const WorldRect& rect, f32 margin = 0.05f) noexcept;
};

} // namespace sbx::render
