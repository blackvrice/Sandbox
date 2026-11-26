#pragma once

#include <graphics-ui/core/core.hpp>

#include <string_view>
#include <vector>

namespace graphics_ui::core {

inline constexpr std::string_view grapheme_unicode_version = "17.0.0";

struct TextBoundaries {
    std::vector<std::size_t> scalars;
    std::vector<std::size_t> graphemes;
};

// Offsets count bytes in the original UTF-8. Both lists include zero and the byte length.
// Uses default extended grapheme clusters and leaves normalization to the caller.
[[nodiscard]] Result<TextBoundaries> textBoundaries(std::string_view utf8);

} // namespace graphics_ui::core
