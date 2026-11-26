#include <graphics-ui/core/text.hpp>

#include "utf8.hpp"

#include <algorithm>
#include <new>
#include <stdexcept>

namespace graphics_ui::core {
namespace {

enum class Gcb : unsigned char { other, cr, lf, control, extend, zwj, regional_indicator, prepend, spacingmark, l, v, t, lv, lvt };
enum class Incb : unsigned char { none, consonant, extend, linker };

struct Properties {
    Gcb break_class{};
    Incb indic{};
    bool pictographic{};
};

struct PropertyRange {
    char32_t first, last;
    Properties properties;
};

constexpr PropertyRange ranges[] = {
#include "unicode_graphemes.inc"
};

Properties propertiesOf(char32_t scalar) noexcept {
    if (scalar >= U' ' && scalar <= U'~') return {};
    // Hangul syllables alternate one LV with twenty-seven LVT syllables.
    if (scalar >= 0xac00 && scalar <= 0xd7a3) return {(scalar - 0xac00) % 28 == 0 ? Gcb::lv : Gcb::lvt, Incb::none, false};
    const auto past = std::ranges::upper_bound(ranges, scalar, {}, &PropertyRange::first);
    return past != std::begin(ranges) && scalar <= (past - 1)->last ? (past - 1)->properties : Properties{};
}

bool isControl(Gcb value) noexcept {
    return value == Gcb::cr || value == Gcb::lf || value == Gcb::control;
}

struct Segmentation {
    enum class Indic { none, consonant, linked };
    enum class Emoji { none, pictograph, joined };

    bool boundaryBefore(Properties next) const noexcept {
        const auto current = next.break_class;
        if (previous == Gcb::cr && current == Gcb::lf) return false; // GB3
        if (isControl(previous) || isControl(current)) return true; // GB4, GB5
        if (previous == Gcb::l && (current == Gcb::l || current == Gcb::v || current == Gcb::lv || current == Gcb::lvt))
            return false; // GB6
        if ((previous == Gcb::lv || previous == Gcb::v) && (current == Gcb::v || current == Gcb::t))
            return false; // GB7
        if ((previous == Gcb::lvt || previous == Gcb::t) && current == Gcb::t) return false; // GB8
        if (current == Gcb::extend || current == Gcb::zwj || current == Gcb::spacingmark || previous == Gcb::prepend)
            return false; // GB9, GB9a, GB9b
        if (next.indic == Incb::consonant && indic == Indic::linked) return false; // GB9c
        if (next.pictographic && emoji == Emoji::joined) return false; // GB11
        return current != Gcb::regional_indicator || !odd_regional_count; // GB12, GB13, GB999
    }

    void advance(Properties next) noexcept {
        previous = next.break_class;
        odd_regional_count = previous == Gcb::regional_indicator && !odd_regional_count;
        switch (next.indic) {
        case Incb::consonant: indic = Indic::consonant; break;
        case Incb::linker: if (indic != Indic::none) indic = Indic::linked; break;
        case Incb::extend: break;
        case Incb::none: indic = Indic::none; break;
        }
        if (next.pictographic) emoji = Emoji::pictograph;
        else if (previous == Gcb::zwj && emoji == Emoji::pictograph) emoji = Emoji::joined;
        else if (previous != Gcb::extend || emoji != Emoji::pictograph) emoji = Emoji::none;
    }

    Gcb previous{};
    Indic indic{};
    Emoji emoji{};
    bool odd_regional_count{};
};

} // namespace

Result<TextBoundaries> textBoundaries(std::string_view utf8) {
    try {
        TextBoundaries result{{0}, {0}};
        Segmentation state;
        for (std::size_t cursor = 0; cursor < utf8.size();) {
            const auto start = cursor;
            char32_t scalar{};
            if (!detail::decodeUtf8(utf8, cursor, &scalar)) return std::unexpected(ErrorCode::invalid_argument);
            const auto properties = propertiesOf(scalar);
            if (start != 0 && state.boundaryBefore(properties)) result.graphemes.push_back(start);
            state.advance(properties);
            result.scalars.push_back(cursor);
        }
        if (!utf8.empty()) result.graphemes.push_back(utf8.size());
        return result;
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    } catch (const std::length_error&) {
        return std::unexpected(ErrorCode::limit_exceeded);
    }
}

} // namespace graphics_ui::core
