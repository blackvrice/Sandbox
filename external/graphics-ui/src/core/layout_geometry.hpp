#pragma once

#include <graphics-ui/core/layout.hpp>

#include <algorithm>
#include <cmath>

namespace graphics_ui::core::detail {

inline bool validRect(const LayoutRect& rect) noexcept {
    return std::isfinite(rect.x) && std::isfinite(rect.y) && rect.width >= 0 && rect.height >= 0 &&
        std::isfinite(rect.x + rect.width) && std::isfinite(rect.y + rect.height);
}

inline LayoutRect intersectRects(const LayoutRect& a, const LayoutRect& b) noexcept {
    const auto x = std::max(a.x, b.x);
    const auto y = std::max(a.y, b.y);
    return {x, y, std::max(0.0, std::min(a.x + a.width, b.x + b.width) - x),
                  std::max(0.0, std::min(a.y + a.height, b.y + b.height) - y)};
}

} // namespace graphics_ui::core::detail
