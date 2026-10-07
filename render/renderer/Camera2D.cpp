#include "render/renderer/Camera2D.hpp"

#include <algorithm>

namespace sbx::render {

Vec2 Camera2D::worldToScreen(Vec2 world) const noexcept {
    const Vec2 d = (world - center) * pixelsPerUnit;
    return {d.x + static_cast<f32>(viewportWidth) * 0.5f, static_cast<f32>(viewportHeight) * 0.5f - d.y};
}

Vec2 Camera2D::screenToWorld(Vec2 screen) const noexcept {
    const f32 dx = screen.x - static_cast<f32>(viewportWidth) * 0.5f;
    const f32 dy = static_cast<f32>(viewportHeight) * 0.5f - screen.y;
    return center + Vec2{dx, dy} / pixelsPerUnit;
}

std::array<f32, 4> Camera2D::clipTransform() const noexcept {
    // ndc = (world - center) · 2·ppu / viewport
    const f32 sx = 2.f * pixelsPerUnit / static_cast<f32>(std::max(viewportWidth, 1u));
    const f32 sy = 2.f * pixelsPerUnit / static_cast<f32>(std::max(viewportHeight, 1u));
    return {sx, sy, -center.x * sx, -center.y * sy};
}

WorldRect Camera2D::visibleRect() const noexcept {
    const Vec2 half{static_cast<f32>(viewportWidth) * 0.5f / pixelsPerUnit,
                    static_cast<f32>(viewportHeight) * 0.5f / pixelsPerUnit};
    return {center - half, center + half};
}

void Camera2D::panByScreen(Vec2 screenDelta) noexcept {
    // 화면 y 는 아래 + → 월드 y 는 위 +
    center -= Vec2{screenDelta.x, -screenDelta.y} / pixelsPerUnit;
}

void Camera2D::zoomAt(Vec2 screen, f32 factor) noexcept {
    const Vec2 anchor = screenToWorld(screen);
    pixelsPerUnit = std::clamp(pixelsPerUnit * factor, kMinPixelsPerUnit, kMaxPixelsPerUnit);
    // 같은 화면 점이 같은 월드 점을 가리키도록 center 를 옮긴다
    const Vec2 after = screenToWorld(screen);
    center += anchor - after;
}

void Camera2D::fit(const WorldRect& rect, f32 margin) noexcept {
    const Vec2 size = rect.max - rect.min;
    center = (rect.min + rect.max) * 0.5f;
    const f32 w = std::max(size.x, 1e-3f) * (1.f + margin * 2.f);
    const f32 h = std::max(size.y, 1e-3f) * (1.f + margin * 2.f);
    pixelsPerUnit = std::clamp(std::min(static_cast<f32>(viewportWidth) / w, static_cast<f32>(viewportHeight) / h),
                               kMinPixelsPerUnit, kMaxPixelsPerUnit);
}

} // namespace sbx::render
