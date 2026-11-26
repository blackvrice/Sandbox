#pragma once

#include <graphics-ui/core/layout.hpp>

#include <unordered_map>

namespace graphics_ui::core {

class ScrollView {
public:
    [[nodiscard]] Result<void> resize(LayoutSize viewport, LayoutSize content) noexcept;
    [[nodiscard]] Result<void> scrollTo(LayoutPoint offset) noexcept;
    [[nodiscard]] Result<void> scrollBy(LayoutPoint delta) noexcept;
    [[nodiscard]] Result<void> reveal(LayoutRect content_bounds) noexcept;

    [[nodiscard]] LayoutSize viewport() const noexcept { return viewport_; }
    [[nodiscard]] LayoutSize content() const noexcept { return content_; }
    [[nodiscard]] LayoutPoint offset() const noexcept { return offset_; }
    [[nodiscard]] std::optional<LayoutPoint> contentPoint(LayoutPoint viewport_point) const noexcept;

private:
    void clampOffset(LayoutPoint offset) noexcept;
    LayoutSize viewport_, content_;
    LayoutPoint offset_;
};

using ListItemKey = std::uint64_t;

struct ListRange {
    std::size_t first{}, past{};
    [[nodiscard]] std::size_t size() const noexcept { return past - first; }
    bool operator==(const ListRange&) const = default;
};

struct ListRow {
    ListItemKey key;
    std::size_t index;
    LayoutRect bounds;
    LayoutRect hit_bounds;
    bool selected;
};

class ListView {
public:
    ListView() = default;
    ListView(const ListView&) = delete;
    ListView& operator=(const ListView&) = delete;

    // Replace the order by stable unique keys. No row content is copied.
    [[nodiscard]] Result<void> setItems(std::span<const ListItemKey> keys);
    [[nodiscard]] Result<void> resize(LayoutSize viewport, double row_height) noexcept;
    [[nodiscard]] Result<void> scrollTo(double y) noexcept;
    [[nodiscard]] Result<void> scrollBy(double dy) noexcept;
    [[nodiscard]] Result<void> reveal(ListItemKey key) noexcept;
    [[nodiscard]] Result<void> select(std::optional<ListItemKey> key) noexcept;

    [[nodiscard]] std::size_t itemCount() const noexcept { return keys_.size(); }
    [[nodiscard]] std::optional<ListItemKey> itemKey(std::size_t index) const noexcept;
    [[nodiscard]] std::optional<std::size_t> indexOf(ListItemKey key) const noexcept;
    [[nodiscard]] std::optional<ListItemKey> selection() const noexcept { return selection_; }
    [[nodiscard]] const ScrollView& scroll() const & noexcept { return scroll_; }
    const ScrollView& scroll() const && = delete;

    [[nodiscard]] ListRange visibleRange() const noexcept;
    // Geometry is in viewport-local coordinates. Edge rows may be partially visible.
    [[nodiscard]] std::optional<ListRow> visibleRow(std::size_t index) const noexcept;
    [[nodiscard]] std::optional<ListRow> hitTest(LayoutPoint viewport_point) const noexcept;

private:
    [[nodiscard]] std::size_t rowIndex(double content_y) const noexcept;
    [[nodiscard]] ListRow row(std::size_t index) const noexcept;
    ScrollView scroll_;
    double row_height_ = 1;
    std::vector<ListItemKey> keys_;
    std::unordered_map<ListItemKey, std::size_t> indices_;
    std::optional<ListItemKey> selection_;
};

} // namespace graphics_ui::core
