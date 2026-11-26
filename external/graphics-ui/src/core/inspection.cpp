#include <graphics-ui/core/inspection.hpp>

#include <algorithm>
#include <cmath>
#include <new>
#include <stdexcept>
#include <unordered_map>

namespace graphics_ui::core {

Result<void> ComponentInspector::refresh(const ComponentTree& tree, const LayoutFrame* layout) try {
    const auto snapshot = tree.snapshot();
    if (!snapshot) return std::unexpected(snapshot.error());
    if (layout && layout->treeRevision() != snapshot->revision) return std::unexpected(ErrorCode::stale_handle);

    using Index = std::unordered_map<std::uint64_t, std::size_t>;
    Index indices;
    indices.reserve(snapshot->components.size());
    std::vector<InspectedComponent> components;
    components.reserve(snapshot->components.size());
    std::size_t visible_count{};
    for (const auto& component : snapshot->components) {
        InspectedComponent node{component, {}, {}, 0};
        if (component.parent) {
            const auto parent = indices.at(component.parent->generation());
            node.parent_index = parent;
            node.depth = components[parent].depth + 1;
        }
        visible_count += component.effectively_visible;
        indices.emplace(component.handle.generation(), components.size());
        components.push_back(node);
    }

    std::optional<LayoutFrame> captured_layout;
    if (layout) {
        const auto boxes = layout->boxes();
        if (boxes.size() != visible_count) return std::unexpected(ErrorCode::stale_handle);
        for (std::size_t index = 0; index < boxes.size(); ++index) {
            const auto& box = boxes[index];
            const auto found = indices.find(box.component.generation());
            if (found == indices.end()) return std::unexpected(ErrorCode::stale_handle);
            auto& node = components[found->second];
            const auto parent = node.parent_index ? components[*node.parent_index].layout_index : std::nullopt;
            if (node.component.handle != box.component || !node.component.effectively_visible || node.layout_index ||
                (node.parent_index && !parent) || parent != box.parent_index) return std::unexpected(ErrorCode::stale_handle);
            node.layout_index = index;
        }
        captured_layout.emplace(*layout);
    }

    // Publish only after validation and allocations. A reused key does not restore a retired generation.
    components_.swap(components);
    layout_.swap(captured_layout);
    revision_ = snapshot->revision;
    if (selection_ && !find(*selection_)) selection_.reset();
    return {};
} catch (const std::bad_alloc&) {
    return std::unexpected(ErrorCode::out_of_memory);
} catch (const std::length_error&) {
    return std::unexpected(ErrorCode::limit_exceeded);
}

const InspectedComponent* ComponentInspector::find(ComponentHandle target) const & noexcept {
    const auto found = std::ranges::find_if(components_, [target](const auto& node) { return node.component.handle == target; });
    return found == components_.end() ? nullptr : &*found;
}

Result<void> ComponentInspector::select(ComponentHandle target) noexcept {
    if (!find(target)) return std::unexpected(ErrorCode::stale_handle);
    selection_ = target;
    return {};
}

Result<std::optional<ComponentHandle>> ComponentInspector::pick(LayoutPoint point) noexcept {
    if (!layout_ || !std::isfinite(point.x) || !std::isfinite(point.y)) return std::unexpected(ErrorCode::invalid_argument);
    selection_ = layout_->hitTest(point);
    return selection_;
}

} // namespace graphics_ui::core
