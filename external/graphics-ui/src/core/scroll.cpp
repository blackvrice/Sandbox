#include <graphics-ui/core/scroll.hpp>

#include "layout_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <new>
#include <stdexcept>

namespace graphics_ui::core {
namespace {
bool validSize(LayoutSize size) noexcept {
    return std::isfinite(size.width) && size.width >= 0 && std::isfinite(size.height) && size.height >= 0;
}

bool finitePoint(LayoutPoint point) noexcept { return std::isfinite(point.x) && std::isfinite(point.y); }

double revealAxis(double offset, double viewport, double first, double extent) noexcept {
    if (extent >= viewport || first < offset) return first;
    return std::max(offset, (first + extent) - viewport);
}
} // namespace

void ScrollView::clampOffset(LayoutPoint offset) noexcept {
    offset_ = {std::clamp(offset.x, 0.0, std::max(0.0, content_.width - viewport_.width)),
               std::clamp(offset.y, 0.0, std::max(0.0, content_.height - viewport_.height))};
}

Result<void> ScrollView::resize(LayoutSize viewport, LayoutSize content) noexcept {
    if (!validSize(viewport) || !validSize(content)) return std::unexpected(ErrorCode::invalid_argument);
    viewport_ = viewport;
    content_ = content;
    clampOffset(offset_);
    return {};
}

Result<void> ScrollView::scrollTo(LayoutPoint offset) noexcept {
    if (!finitePoint(offset)) return std::unexpected(ErrorCode::invalid_argument);
    clampOffset(offset);
    return {};
}

Result<void> ScrollView::scrollBy(LayoutPoint delta) noexcept {
    if (!finitePoint(delta)) return std::unexpected(ErrorCode::invalid_argument);
    clampOffset({offset_.x + delta.x, offset_.y + delta.y});
    return {};
}

Result<void> ScrollView::reveal(LayoutRect content_bounds) noexcept {
    if (!detail::validRect(content_bounds)) return std::unexpected(ErrorCode::invalid_argument);
    clampOffset({revealAxis(offset_.x, viewport_.width, content_bounds.x, content_bounds.width),
                 revealAxis(offset_.y, viewport_.height, content_bounds.y, content_bounds.height)});
    return {};
}

std::optional<LayoutPoint> ScrollView::contentPoint(LayoutPoint point) const noexcept {
    if (!finitePoint(point) || point.x < 0 || point.y < 0 ||
        point.x >= viewport_.width || point.y >= viewport_.height)
        return std::nullopt;
    const LayoutPoint translated{offset_.x + point.x, offset_.y + point.y};
    if (translated.x >= content_.width || translated.y >= content_.height) return std::nullopt;
    return translated;
}

Result<void> ListView::setItems(std::span<const ListItemKey> keys) {
    const auto extent = static_cast<double>(keys.size()) * row_height_;
    if (!std::isfinite(extent)) return std::unexpected(ErrorCode::limit_exceeded);
    try {
        decltype(indices_) next_indices;
        next_indices.reserve(keys.size());
        for (std::size_t index = 0; index < keys.size(); ++index)
            if (!next_indices.emplace(keys[index], index).second)
                return std::unexpected(ErrorCode::invalid_argument);
        std::vector<ListItemKey> next_keys(keys.begin(), keys.end());
        auto next_scroll = scroll_;
        const auto viewport = scroll_.viewport();
        if (auto resized = next_scroll.resize(viewport, {viewport.width, extent}); !resized) return resized;

        // Retain the top row and its intra-row offset when that key survives the update.
        const auto top = rowIndex(scroll_.offset().y);
        if (top < keys_.size()) {
            const auto anchor = next_indices.find(keys_[top]);
            if (anchor != next_indices.end()) {
                const auto inset = scroll_.offset().y - static_cast<double>(top) * row_height_;
                const auto y = static_cast<double>(anchor->second) * row_height_ + inset;
                if (auto moved = next_scroll.scrollTo({0, y}); !moved) return moved;
            }
        }
        if (selection_ && !next_indices.contains(*selection_)) selection_.reset();
        keys_.swap(next_keys);
        indices_.swap(next_indices);
        scroll_ = next_scroll;
        return {};
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    } catch (const std::length_error&) {
        return std::unexpected(ErrorCode::limit_exceeded);
    }
}

Result<void> ListView::resize(LayoutSize viewport, double row_height) noexcept {
    if (!validSize(viewport) || !std::isfinite(row_height) || row_height <= 0)
        return std::unexpected(ErrorCode::invalid_argument);
    const auto extent = static_cast<double>(keys_.size()) * row_height;
    if (!std::isfinite(extent)) return std::unexpected(ErrorCode::limit_exceeded);
    auto next_scroll = scroll_;
    if (auto resized = next_scroll.resize(viewport, {viewport.width, extent}); !resized) return resized;
    if (row_height != row_height_) {
        // Express the previous position in rows before applying the new height.
        const auto offset = std::min(extent, (scroll_.offset().y / row_height_) * row_height);
        if (auto moved = next_scroll.scrollTo({0, offset}); !moved) return moved;
    }
    scroll_ = next_scroll;
    row_height_ = row_height;
    return {};
}

Result<void> ListView::scrollTo(double y) noexcept { return scroll_.scrollTo({0, y}); }
Result<void> ListView::scrollBy(double dy) noexcept { return scroll_.scrollBy({0, dy}); }

Result<void> ListView::reveal(ListItemKey key) noexcept {
    const auto index = indexOf(key);
    if (!index) return std::unexpected(ErrorCode::stale_handle);
    return scroll_.reveal({0, static_cast<double>(*index) * row_height_, scroll_.viewport().width, row_height_});
}

Result<void> ListView::select(std::optional<ListItemKey> key) noexcept {
    if (key && !indices_.contains(*key)) return std::unexpected(ErrorCode::stale_handle);
    selection_ = key;
    return {};
}

std::optional<ListItemKey> ListView::itemKey(std::size_t index) const noexcept {
    if (index >= keys_.size()) return std::nullopt;
    return keys_[index];
}

std::optional<std::size_t> ListView::indexOf(ListItemKey key) const noexcept {
    const auto found = indices_.find(key);
    if (found == indices_.end()) return std::nullopt;
    return found->second;
}

std::size_t ListView::rowIndex(double y) const noexcept {
    if (y >= scroll_.content().height) return keys_.size();
    const auto index = y / row_height_;
    auto candidate = index >= static_cast<double>(keys_.size()) ? keys_.size() - 1 : static_cast<std::size_t>(index);
    // Compare the reconstructed edge when division rounded across a fractional row boundary.
    if (candidate && y < static_cast<double>(candidate) * row_height_) --candidate;
    else if (candidate + 1 < keys_.size() && y >= static_cast<double>(candidate + 1) * row_height_) ++candidate;
    return candidate;
}

ListRange ListView::visibleRange() const noexcept {
    const auto viewport = scroll_.viewport();
    if (keys_.empty() || viewport.width == 0 || viewport.height == 0) return {};
    const auto top = scroll_.offset().y;
    const auto bottom = std::min(scroll_.content().height, top + viewport.height);
    const auto first = rowIndex(top);
    if (first == keys_.size()) return {first, first};
    auto past = rowIndex(bottom);
    if (past < keys_.size() && static_cast<double>(past) * row_height_ - top < viewport.height) ++past;
    return {first, std::max(first + 1, past)};
}

ListRow ListView::row(std::size_t index) const noexcept {
    const auto viewport = scroll_.viewport();
    const LayoutRect bounds{0, static_cast<double>(index) * row_height_ - scroll_.offset().y, viewport.width, row_height_};
    const auto key = keys_[index];
    return {key, index, bounds, detail::intersectRects(bounds, {0, 0, viewport.width, viewport.height}),
            selection_ == key};
}

std::optional<ListRow> ListView::visibleRow(std::size_t index) const noexcept {
    const auto range = visibleRange();
    if (index < range.first || index >= range.past) return std::nullopt;
    return row(index);
}

std::optional<ListRow> ListView::hitTest(LayoutPoint point) const noexcept {
    const auto viewport = scroll_.viewport();
    if (!finitePoint(point) || point.x < 0 || point.y < 0 ||
        point.x >= viewport.width || point.y >= viewport.height) return std::nullopt;
    const auto range = visibleRange();
    const auto at = rowIndex(std::min(scroll_.content().height, scroll_.offset().y + point.y));
    const auto hit = [&](std::size_t index) -> std::optional<ListRow> {
        if (index < range.first || index >= range.past) return std::nullopt;
        const auto candidate = row(index);
        const auto& bounds = candidate.hit_bounds;
        if (point.y >= bounds.y && point.y < bounds.y + bounds.height) return candidate;
        return std::nullopt;
    };
    // Adding the offset can round a point onto a content edge. The viewport-local
    // rectangles are authoritative for the half-open hit at either side of that edge.
    if (const auto result = hit(at)) return result;
    if (at > range.first)
        if (const auto previous = hit(at - 1)) return previous;
    return at < range.past ? hit(at + 1) : std::nullopt;
}

} // namespace graphics_ui::core
