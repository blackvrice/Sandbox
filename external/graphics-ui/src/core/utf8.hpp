#pragma once

#include <string_view>

namespace graphics_ui::core::detail {

// Called at the first byte of a scalar while the cursor is before the end.
inline bool decodeUtf8(std::string_view text, std::size_t& cursor, char32_t* value = nullptr) noexcept {
    // Unicode Table 3-7. Only the second byte has a restricted continuation range.
    auto index = cursor;
    const auto lead = static_cast<unsigned char>(text[index++]);
    if (lead < 0x80) {
        if (value) *value = lead;
        cursor = index;
        return true;
    }
    if (lead < 0xc2 || lead > 0xf4) return false;
    const std::size_t tails = lead <= 0xdf ? 1 : (lead <= 0xef ? 2 : 3);
    if (tails > text.size() - index) return false;
    const auto second = static_cast<unsigned char>(text[index]);
    if ((lead == 0xe0 && second < 0xa0) || (lead == 0xed && second > 0x9f) ||
        (lead == 0xf0 && second < 0x90) || (lead == 0xf4 && second > 0x8f)) return false;
    char32_t scalar = lead & (0x7fu >> (tails + 1));
    for (std::size_t tail = 0; tail < tails; ++tail) {
        const auto byte = static_cast<unsigned char>(text[index++]);
        if (byte < 0x80 || byte > 0xbf) return false;
        if (value) scalar = (scalar << 6) | (byte & 0x3fu);
    }
    if (value) *value = scalar;
    cursor = index;
    return true;
}

inline bool validUtf8(std::string_view text) noexcept {
    std::size_t cursor{};
    while (cursor < text.size())
        if (!decodeUtf8(text, cursor)) return false;
    return true;
}

} // namespace graphics_ui::core::detail
