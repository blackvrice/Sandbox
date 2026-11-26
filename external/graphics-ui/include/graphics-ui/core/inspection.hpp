#pragma once

#include <graphics-ui/core/layout.hpp>

namespace graphics_ui::core {

struct InspectedComponent {
    ComponentInfo component;
    // Parent refers to components(), and layout_index refers to layout()->boxes().
    std::optional<std::size_t> parent_index;
    std::optional<std::size_t> layout_index;
    std::size_t depth{};
};

class ComponentInspector {
public:
    // Capture on the tree's owner thread. The inspector owns the result and borrows neither input.
    // A failed capture leaves the previous view and selection intact.
    [[nodiscard]] Result<void> refresh(const ComponentTree& tree, const LayoutFrame* layout = nullptr);

    [[nodiscard]] std::uint64_t treeRevision() const noexcept { return revision_; }
    [[nodiscard]] std::span<const InspectedComponent> components() const & noexcept { return components_; }
    std::span<const InspectedComponent> components() const && = delete;
    [[nodiscard]] const LayoutFrame* layout() const & noexcept { return layout_ ? &*layout_ : nullptr; }
    const LayoutFrame* layout() const && = delete;
    [[nodiscard]] const InspectedComponent* find(ComponentHandle target) const & noexcept;
    const InspectedComponent* find(ComponentHandle) const && = delete;

    // Direct selection includes hidden nodes. Picking follows LayoutFrame's clipped, frontmost hit.
    // A pick outside the frame clears selection. A pick requires a captured layout.
    [[nodiscard]] Result<void> select(ComponentHandle target) noexcept;
    [[nodiscard]] Result<std::optional<ComponentHandle>> pick(LayoutPoint point) noexcept;
    void clearSelection() noexcept { selection_.reset(); }
    [[nodiscard]] std::optional<ComponentHandle> selection() const noexcept { return selection_; }

private:
    std::vector<InspectedComponent> components_;
    std::optional<LayoutFrame> layout_;
    std::optional<ComponentHandle> selection_;
    std::uint64_t revision_{};
};

} // namespace graphics_ui::core
