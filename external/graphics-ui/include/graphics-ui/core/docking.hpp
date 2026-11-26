#pragma once

#include <graphics-ui/core/layout.hpp>

namespace graphics_ui::core {

using DockPaneId = std::uint32_t;
enum class DockSide { tab_before, tab_after, left, right, top, bottom };
enum class DockAxis { horizontal, vertical };

struct DockFloatingBounds {
    std::int32_t x{}, y{}, width{}, height{};
    bool operator==(const DockFloatingBounds&) const = default;
};

struct DockFloatingPane {
    DockPaneId pane{};
    DockFloatingBounds bounds;
    bool operator==(const DockFloatingPane&) const = default;
};

struct DockDividerKey {
    std::uint64_t workspace{}, revision{};
    std::size_t ordinal{};
    bool operator==(const DockDividerKey&) const = default;
};

struct DockTabBox {
    DockPaneId pane{}, group_active{};
    LayoutRect bounds;
};

struct DockGroupBox {
    DockPaneId active{};
    LayoutRect bounds, content;
};

struct DockDividerBox {
    DockDividerKey key;
    DockAxis axis{};
    LayoutRect bounds, track;
    double fraction{};
};

struct DockMetrics {
    double tab_height = 28;
    double divider_width = 6;
    LayoutSize minimum_group{72, 62};
};

struct DockFrame {
    std::uint64_t revision{};
    DockPaneId focused{};
    std::vector<DockGroupBox> groups;
    std::vector<DockTabBox> tabs;
    std::vector<DockDividerBox> dividers;
    std::vector<DockFloatingPane> floating;
};

inline constexpr std::size_t dock_layout_byte_limit = 16 * 1024;

class DockLayout {
public:
    // Stable, nonzero host pane IDs start in one ordered tab group. The layout belongs to its creating thread.
    [[nodiscard]] static Result<std::unique_ptr<DockLayout>> create(std::span<const DockPaneId> panes);
    DockLayout(const DockLayout&) = delete;
    DockLayout& operator=(const DockLayout&) = delete;
    ~DockLayout() noexcept;

    [[nodiscard]] Result<void> select(DockPaneId pane);
    // A zero target with a tab placement starts a group in an empty workspace.
    [[nodiscard]] Result<void> dock(DockPaneId pane, DockPaneId target, DockSide side);
    // A floating pane owns one host window. Bounds are integer logical units.
    [[nodiscard]] Result<void> undock(DockPaneId pane, DockFloatingBounds bounds);
    [[nodiscard]] Result<void> moveFloating(DockPaneId pane, DockFloatingBounds bounds);
    [[nodiscard]] Result<void> resize(DockDividerKey divider, double fraction);
    [[nodiscard]] Result<DockFrame> arrange(LayoutSize viewport, DockMetrics metrics = {}) const;

    // Save owns its bytes. Restore accepts exactly the pane catalog supplied at creation.
    [[nodiscard]] Result<std::vector<std::byte>> save() const;
    [[nodiscard]] Result<void> restore(std::span<const std::byte> bytes);

private:
    struct Impl;
    explicit DockLayout(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace graphics_ui::core
