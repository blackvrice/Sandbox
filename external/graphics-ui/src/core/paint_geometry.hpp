#pragma once

#include <graphics-ui/core/render.hpp>

#include "layout_geometry.hpp"

namespace graphics_ui::core::detail {

inline bool validVisual(const VisualState& visual) noexcept {
    const auto& [translation, scale] = visual.transform;
    return std::isfinite(translation.x) && std::isfinite(translation.y) && std::isfinite(scale) && scale > 0 &&
        visual.opacity >= 0 && visual.opacity <= 1;
}

inline LayoutRect shadowExtent(const DropShadow& shadow) noexcept {
    const auto& [x, y, width, height] = shadow.bounds;
    const auto radius = shadow.radius;
    const auto left = x - radius, top = y - radius;
    return {left, top, (x + width + radius) - left, (y + height + radius) - top};
}

} // namespace graphics_ui::core::detail
