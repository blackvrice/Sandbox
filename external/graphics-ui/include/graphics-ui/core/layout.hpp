#pragma once

#include <graphics-ui/core/component_tree.hpp>

namespace graphics_ui::core {

struct LayoutSize {
    double width{}, height{};
    bool operator==(const LayoutSize&) const = default;
};

struct LayoutPoint {
    double x{}, y{};
    bool operator==(const LayoutPoint&) const = default;
};

struct LayoutRect {
    double x{}, y{}, width{}, height{};
    bool operator==(const LayoutRect&) const = default;
};

struct LayoutLimits {
    double minimum{};
    std::optional<double> maximum;
    bool operator==(const LayoutLimits&) const = default;
};

struct LayoutConstraints {
    LayoutLimits width, height;
    bool operator==(const LayoutConstraints&) const = default;
};

struct LayoutInsets {
    double left{}, top{}, right{}, bottom{};
    bool operator==(const LayoutInsets&) const = default;
};

enum class LayoutKind { leaf, row, column, grid, flex_row, flex_column };

struct LayoutStyle {
    LayoutKind kind = LayoutKind::leaf;
    LayoutConstraints limits;
    std::optional<double> preferred_width, preferred_height;
    LayoutInsets padding;
    double gap{};
    std::uint32_t grid_columns = 1; // Equal-width columns. Visible children fill rows in order.
    // These values apply when this node is an item in a flex container.
    std::optional<double> flex_basis;
    double flex_grow{};
    double flex_shrink = 1;
};

struct LayoutNode {
    using Intrinsic = std::function<Result<LayoutSize>(const LayoutConstraints& content_offer)>;

    ComponentHandle component;
    LayoutStyle style;
    // Can report content larger than its offer.
    Intrinsic intrinsic;
    // Opt in to LayoutCache. Change this value when the intrinsic function or its inputs change.
    std::optional<std::uint64_t> intrinsic_revision;
};

enum class LayoutCacheDecision { uncached, cold, visibility_changed, revision_changed, offer_changed, hit, same_pass };

struct LayoutIntrinsicDiagnostic {
    ComponentHandle component;
    LayoutConstraints content_offer;
    LayoutCacheDecision decision;
};

struct LayoutNodeDiagnostic {
    ComponentHandle component;
    LayoutStyle style;
    LayoutConstraints parent_offer, box_limits, content_offer;
    LayoutSize natural, measured;
};

enum class LayoutFailureReason {
    invalid_viewport, descriptor_count, invalid_tree, stale_descriptor, duplicate_descriptor,
    invalid_style, intrinsic_on_container, leaf_with_children, intrinsic_measurement, measurement,
};

struct LayoutFailure {
    ErrorCode error;
    LayoutFailureReason reason;
    std::optional<ComponentHandle> component;
};

// A successful trace owns the final node measurements and intrinsic cache decisions in evaluation order.
// A failed trace contains the location and error, with both collections empty.
// A reentrant LayoutCache call rejected as busy leaves the trace untouched.
struct LayoutDiagnostics {
    std::uint64_t tree_revision{};
    LayoutConstraints viewport;
    std::vector<LayoutNodeDiagnostic> nodes;
    std::vector<LayoutIntrinsicDiagnostic> intrinsic;
    std::optional<LayoutFailure> failure;
};

class LayoutMeasurement {
public:
    [[nodiscard]] std::uint64_t treeRevision() const noexcept { return tree_revision_; }
    [[nodiscard]] LayoutSize size() const noexcept { return nodes_.empty() ? LayoutSize{} : nodes_.front().size; }

private:
    friend struct detail::LayoutPass;
    struct Node {
        ComponentHandle component;
        std::size_t parent, first_child, next_sibling;
        LayoutKind kind;
        LayoutInsets padding;
        double gap;
        LayoutSize size, natural;
        LayoutPoint grid_offset;
    };

    std::uint64_t tree_revision_{};
    std::vector<Node> nodes_;
};

class LayoutCache {
public:
    LayoutCache() = default;
    LayoutCache(const LayoutCache&) = delete;
    LayoutCache& operator=(const LayoutCache&) = delete;

    // Use on a single thread. The cache retains dimensions from the last successful measurement.
    [[nodiscard]] Result<LayoutMeasurement> measure(const ComponentSnapshot& tree,
        std::span<const LayoutNode> nodes, LayoutConstraints viewport = {});
    [[nodiscard]] Result<LayoutMeasurement> measure(const ComponentSnapshot& tree,
        std::span<const LayoutNode> nodes, LayoutConstraints viewport, LayoutDiagnostics& diagnostics);

private:
    friend struct detail::LayoutPass;
    [[nodiscard]] Result<LayoutMeasurement> measureImpl(const ComponentSnapshot& tree,
        std::span<const LayoutNode> nodes, LayoutConstraints viewport, LayoutDiagnostics* diagnostics);
    struct IntrinsicEntry {
        ComponentHandle component;
        std::uint64_t visibility_revision, revision;
        LayoutConstraints offer;
        LayoutSize size;
    };
    std::vector<IntrinsicEntry> entries_;
    bool measuring_{};
};

struct LayoutBox {
    ComponentHandle component;
    std::optional<std::size_t> parent_index;
    LayoutRect bounds;
    LayoutSize overflow;
    LayoutRect hit_bounds;
    bool operator==(const LayoutBox&) const = default;
};

class LayoutFrame {
public:
    [[nodiscard]] std::uint64_t treeRevision() const noexcept { return tree_revision_; }
    [[nodiscard]] std::span<const LayoutBox> boxes() const & noexcept { return boxes_; }
    std::span<const LayoutBox> boxes() const && = delete;
    [[nodiscard]] std::optional<ComponentHandle> hitTest(LayoutPoint point) const noexcept;

private:
    friend struct detail::LayoutPass;
    std::uint64_t tree_revision_{};
    std::vector<LayoutBox> boxes_;
};

// Supply one descriptor per mounted component. Descriptors can be in any order.
[[nodiscard]] Result<LayoutMeasurement> measureLayout(const ComponentSnapshot& tree, std::span<const LayoutNode> nodes,
                                                      LayoutConstraints viewport = {});
[[nodiscard]] Result<LayoutMeasurement> measureLayout(const ComponentSnapshot& tree, std::span<const LayoutNode> nodes,
                                                      LayoutConstraints viewport, LayoutDiagnostics& diagnostics);
// Arrange a measurement at a logical origin. Re-measure if size constraints change.
[[nodiscard]] Result<LayoutFrame> arrangeLayout(const LayoutMeasurement& measurement, LayoutPoint origin = {});

} // namespace graphics_ui::core
