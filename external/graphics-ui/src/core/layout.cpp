#include <graphics-ui/core/layout.hpp>

#include "layout_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_map>

namespace graphics_ui::core::detail {
namespace {
constexpr auto no_index = std::numeric_limits<std::size_t>::max();
constexpr auto unbounded = std::numeric_limits<double>::infinity();

bool nonnegative(double value) noexcept { return std::isfinite(value) && value >= 0; }

bool valid(const LayoutLimits& limits) noexcept {
    return nonnegative(limits.minimum) &&
        (!limits.maximum || (nonnegative(*limits.maximum) && *limits.maximum >= limits.minimum));
}

bool valid(const LayoutConstraints& limits) noexcept { return valid(limits.width) && valid(limits.height); }

bool isFlex(LayoutKind kind) noexcept { return kind == LayoutKind::flex_row || kind == LayoutKind::flex_column; }
bool isRow(LayoutKind kind) noexcept { return kind == LayoutKind::row || kind == LayoutKind::flex_row; }

bool valid(const LayoutStyle& style) noexcept {
    return style.kind >= LayoutKind::leaf && style.kind <= LayoutKind::flex_column && valid(style.limits) &&
        (!style.preferred_width || nonnegative(*style.preferred_width)) &&
        (!style.preferred_height || nonnegative(*style.preferred_height)) &&
        nonnegative(style.padding.left) && nonnegative(style.padding.top) &&
        nonnegative(style.padding.right) && nonnegative(style.padding.bottom) && nonnegative(style.gap) &&
        (style.kind != LayoutKind::grid || style.grid_columns != 0) &&
        (!style.flex_basis || nonnegative(*style.flex_basis)) &&
        nonnegative(style.flex_grow) && nonnegative(style.flex_shrink);
}

LayoutLimits intersectLimits(const LayoutLimits& offered, const LayoutLimits& local, std::optional<double> preferred) noexcept {
    // The parent offer wins when its range and the local range are disjoint.
    const auto upper = std::max(offered.minimum, std::min(offered.maximum.value_or(unbounded), local.maximum.value_or(unbounded)));
    const auto lower = std::min(upper, std::max(offered.minimum, local.minimum));
    if (preferred) {
        const auto exact = std::clamp(*preferred, lower, upper);
        return {exact, exact};
    }
    return {lower, std::isfinite(upper) ? std::optional{upper} : std::nullopt};
}

LayoutLimits contentLimit(const LayoutLimits& box, double padding) noexcept {
    if (!box.maximum) return {};
    return {0, std::max(0.0, *box.maximum - padding)};
}

double fit(double natural, const LayoutLimits& limits) noexcept {
    return std::clamp(natural, limits.minimum, limits.maximum.value_or(unbounded));
}

LayoutConstraints boxFor(const LayoutConstraints& offer, const LayoutStyle& style) noexcept {
    return {intersectLimits(offer.width, style.limits.width, style.preferred_width),
            intersectLimits(offer.height, style.limits.height, style.preferred_height)};
}

LayoutConstraints contentFor(const LayoutConstraints& box, LayoutSize padding) noexcept {
    return {contentLimit(box.width, padding.width), contentLimit(box.height, padding.height)};
}

LayoutSize withPadding(LayoutSize content, LayoutInsets padding) noexcept {
    return {(padding.left + content.width) + padding.right, (padding.top + content.height) + padding.bottom};
}

struct FlexTrack {
    double size, minimum, maximum, grow, shrink;
    double weight{};
    int exponent{};
    bool frozen{};
};

void distributeFlex(std::span<FlexTrack> tracks, double delta) noexcept {
    if (delta == 0) return;
    const bool growing = delta > 0;
    double remaining = std::abs(delta);
    for (auto& track : tracks) {
        track.weight = std::frexp(growing ? track.grow : track.shrink, &track.exponent);
        if (!growing) {
            int basis_exponent{};
            track.weight *= std::frexp(track.size, &basis_exponent);
            track.exponent += basis_exponent;
        }
        track.frozen = track.weight == 0 || track.size == (growing ? track.maximum : track.minimum);
    }

    // Freeze items that hit their limit, then distribute the leftover space among the others.
    // Keep exponents separate so even a large shrink weight times a large basis is representable.
    while (remaining > 0) {
        auto scale = std::numeric_limits<int>::min();
        for (const auto& track : tracks)
            if (!track.frozen) scale = std::max(scale, track.exponent);
        if (scale == std::numeric_limits<int>::min()) return;
        double total{};
        for (const auto& track : tracks)
            if (!track.frozen) total += std::scalbn(track.weight, track.exponent - scale);
        int budget_exponent{};
        const auto budget = std::frexp(remaining, &budget_exponent);
        const auto share = [&](const FlexTrack& track) {
            return std::scalbn(budget * track.weight / total, budget_exponent + track.exponent - scale);
        };
        bool froze{};
        for (auto& track : tracks) {
            if (track.frozen) continue;
            const auto limit = growing ? track.maximum : track.minimum;
            const auto capacity = std::abs(limit - track.size);
            if (!std::isfinite(capacity) || share(track) < capacity) continue;
            remaining = std::max(0.0, remaining - capacity);
            track.size = limit;
            track.frozen = true;
            froze = true;
        }
        if (froze) continue;
        for (auto& track : tracks) {
            if (track.frozen) continue;
            const auto portion = std::min(remaining, share(track));
            track.size = std::clamp(track.size + (growing ? portion : -portion), track.minimum, track.maximum);
        }
        return;
    }
}

Result<LayoutSize> intrinsicSize(const LayoutNode& node, const LayoutConstraints& offer) {
    if (!node.intrinsic) return LayoutSize{};
    try {
        const auto size = node.intrinsic(offer);
        if (!size) return std::unexpected(size.error());
        if (!nonnegative(size->width) || !nonnegative(size->height)) return std::unexpected(ErrorCode::invalid_argument);
        return *size;
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    } catch (...) {
        return std::unexpected(ErrorCode::evaluation_failed);
    }
}
} // namespace

struct LayoutPass {
    static std::unexpected<ErrorCode> fail(LayoutDiagnostics* trace, LayoutFailureReason reason, ErrorCode error,
                                          std::optional<ComponentHandle> component = {}) noexcept {
        if (trace) trace->failure = LayoutFailure{error, reason, component};
        return std::unexpected(error);
    }

    struct WorkNode {
        std::size_t parent = no_index;
        std::size_t output = no_index;
        std::size_t last_child = no_index;
        const LayoutNode* descriptor{};
        LayoutConstraints box;
        LayoutConstraints content;
        LayoutSize padding;
        double grid_gaps{};
    };

    static LayoutSize measureGrid(std::vector<LayoutMeasurement::Node>& nodes, const LayoutMeasurement::Node& grid,
                                  const WorkNode& work, const LayoutConstraints& box) {
        if (grid.first_child == no_index) return {};
        const auto columns = work.descriptor->style.grid_columns;
        const auto inner_minimum = std::max(0.0, box.width.minimum - work.padding.width);
        double track_width = std::max(0.0, inner_minimum - work.grid_gaps) / columns;
        for (auto index = grid.first_child; index != no_index; index = nodes[index].next_sibling)
            track_width = std::max(track_width, nodes[index].size.width);

        std::uint32_t column{};
        double row_top{}, row_height{};
        for (auto index = grid.first_child; index != no_index; index = nodes[index].next_sibling) {
            auto& child = nodes[index];
            child.grid_offset = {column * track_width + column * grid.gap, row_top};
            row_height = std::max(row_height, child.size.height);
            if (++column == columns && child.next_sibling != no_index) {
                row_top += row_height + grid.gap;
                column = 0;
                row_height = 0;
            }
        }
        return {columns * track_width + work.grid_gaps, row_top + row_height};
    }

    static LayoutSize measureStack(const std::vector<LayoutMeasurement::Node>& nodes, const LayoutMeasurement::Node& node) {
        const bool row = isRow(node.kind);
        double major{}, cross{};
        for (auto index = node.first_child; index != no_index; index = nodes[index].next_sibling) {
            const auto& child = nodes[index];
            major += row ? child.size.width : child.size.height;
            if (child.next_sibling != no_index) major += node.gap;
            cross = std::max(cross, row ? child.size.height : child.size.width);
        }
        return row ? LayoutSize{major, cross} : LayoutSize{cross, major};
    }

    static LayoutConstraints childOffer(LayoutKind kind, LayoutConstraints offer, const WorkNode& work) noexcept {
        if (kind == LayoutKind::grid && offer.width.maximum)
            offer.width.maximum = std::max(0.0, *offer.width.maximum - work.grid_gaps) /
                work.descriptor->style.grid_columns;
        return offer;
    }

    static Result<LayoutSize> measureLeaf(const WorkNode& work, const ComponentInfo& component, const LayoutConstraints& offer,
                                         LayoutCache* cache,
                                         std::vector<LayoutCache::IntrinsicEntry>& next, LayoutDiagnostics* trace) {
        const auto& descriptor = *work.descriptor;
        const auto evaluate = [&]() -> Result<LayoutSize> {
            auto size = intrinsicSize(descriptor, offer);
            if (!size) return fail(trace, LayoutFailureReason::intrinsic_measurement, size.error(), component.handle);
            return size;
        };
        if (!descriptor.intrinsic) return LayoutSize{};
        if (!cache || !descriptor.intrinsic_revision) {
            if (trace) trace->intrinsic.push_back({component.handle, offer, LayoutCacheDecision::uncached});
            return evaluate();
        }

        const auto generation = component.handle.generation();
        auto saved = std::ranges::lower_bound(cache->entries_, generation, {},
            [](const auto& entry) { return entry.component.generation(); });
        const auto previous = saved;
        for (; saved != cache->entries_.end() && saved->component == component.handle; ++saved) {
            if (saved->visibility_revision == component.visibility_revision &&
                saved->revision == *descriptor.intrinsic_revision && saved->offer == offer) {
                next.push_back(*saved);
                if (trace) trace->intrinsic.push_back({component.handle, offer, LayoutCacheDecision::hit});
                return saved->size;
            }
        }
        if (trace) {
            auto decision = LayoutCacheDecision::cold;
            if (previous != saved) {
                if (previous->visibility_revision != component.visibility_revision) decision = LayoutCacheDecision::visibility_changed;
                else if (previous->revision != *descriptor.intrinsic_revision) decision = LayoutCacheDecision::revision_changed;
                else decision = LayoutCacheDecision::offer_changed;
            }
            trace->intrinsic.push_back({component.handle, offer, decision});
        }
        auto size = evaluate();
        if (size)
            next.push_back({component.handle, component.visibility_revision, *descriptor.intrinsic_revision, offer, *size});
        return size;
    }

    struct FlexPass {
        struct Evaluation {
            LayoutConstraints box;
            LayoutSize size, natural;
            std::size_t previous, children;
        };

        struct Choice {
            std::size_t evaluation = no_index;
            double extent{};
        };

        struct Task {
            std::size_t node, evaluation, child, slot;
            LayoutConstraints offer;
            std::optional<double> allocated_main;
        };

        using Node = LayoutMeasurement::Node;
        std::vector<Node>& nodes;
        std::span<const WorkNode> work;
        std::span<const ComponentInfo> components;
        LayoutCache* cache;
        std::vector<LayoutCache::IntrinsicEntry>& next;
        LayoutDiagnostics* trace;
        std::vector<std::size_t> sources{}, heads{};
        std::vector<Evaluation> evaluations{};
        std::vector<Choice> choices{};
        std::vector<Task> tasks{};
        std::vector<FlexTrack> tracks{};

        const WorkNode& source(std::size_t index) const noexcept { return work[sources[index]]; }

        std::optional<double> basis(std::size_t child, bool row) const noexcept {
            const auto& style = source(child).descriptor->style;
            if (style.flex_basis) return style.flex_basis;
            return row ? style.preferred_width : style.preferred_height;
        }

        Result<std::size_t> request(std::size_t index, LayoutConstraints offer) {
            const auto& node = nodes[index];
            const auto& data = source(index);
            const auto box = boxFor(offer, data.descriptor->style);
            const auto content = contentFor(box, data.padding);
            std::optional<LayoutSize> saved_leaf;
            for (auto candidate = heads[index]; candidate != no_index; candidate = evaluations[candidate].previous) {
                const auto& evaluated = evaluations[candidate];
                if (evaluated.box == box) return candidate;
                if (node.kind == LayoutKind::leaf && contentFor(evaluated.box, data.padding) == content)
                    saved_leaf = evaluated.natural;
            }

            const auto id = evaluations.size();
            const auto first = choices.size();
            evaluations.push_back({box, {}, {}, heads[index], first});
            heads[index] = id;
            if (node.kind == LayoutKind::leaf) {
                if (!saved_leaf) {
                    const auto size = measureLeaf(data, components[sources[index]], content, cache, next, trace);
                    if (!size) return std::unexpected(size.error());
                    saved_leaf = withPadding(*size, node.padding);
                } else if (trace && data.descriptor->intrinsic) {
                    trace->intrinsic.push_back({node.component, content, LayoutCacheDecision::same_pass});
                }
                if (!nonnegative(saved_leaf->width) || !nonnegative(saved_leaf->height))
                    return std::unexpected(ErrorCode::limit_exceeded);
                evaluations[id].natural = *saved_leaf;
                evaluations[id].size = {fit(saved_leaf->width, box.width), fit(saved_leaf->height, box.height)};
                return id;
            }

            for (auto child = node.first_child; child != no_index; child = nodes[child].next_sibling)
                choices.emplace_back();
            auto child_offer = childOffer(node.kind, content, data);
            if (isFlex(node.kind)) (isRow(node.kind) ? child_offer.width : child_offer.height) = {};
            tasks.push_back({index, id, node.first_child, first, child_offer, {}});
            return id;
        }

        Result<void> allocate(Task& task) {
            const auto& node = nodes[task.node];
            const auto& data = source(task.node);
            const auto& evaluation = evaluations[task.evaluation];
            const bool row = isRow(node.kind);
            const auto padding = row ? data.padding.width : data.padding.height;
            const auto& limits = row ? evaluation.box.width : evaluation.box.height;
            double bases{};
            tracks.clear();
            auto slot = evaluation.children;
            for (auto child = node.first_child; child != no_index; child = nodes[child].next_sibling, ++slot) {
                const auto& style = source(child).descriptor->style;
                const auto& local = row ? style.limits.width : style.limits.height;
                auto initial = basis(child, row);
                if (!initial) {
                    const auto measured = evaluations[choices[slot].evaluation].size;
                    initial = row ? measured.width : measured.height;
                }
                const auto size = fit(*initial, local);
                bases += size;
                tracks.push_back({size, local.minimum, local.maximum.value_or(unbounded), style.flex_grow, style.flex_shrink});
            }
            const auto gaps = tracks.empty() ? 0.0 : node.gap * (tracks.size() - 1);
            const auto desired = padding + gaps + bases;
            if (!std::isfinite(desired)) return std::unexpected(ErrorCode::limit_exceeded);
            task.allocated_main = fit(desired, limits);
            if (*task.allocated_main != desired) {
                const auto capacity = std::max(0.0, *task.allocated_main - padding - gaps);
                distributeFlex(tracks, capacity - bases);
            }
            task.child = node.first_child;
            task.slot = evaluation.children;
            task.offer = contentFor(evaluation.box, data.padding);
            for (const auto& track : tracks) choices[task.slot++].extent = track.size;
            task.slot = evaluation.children;
            return {};
        }

        Result<void> finish(const Task& task) {
            auto& node = nodes[task.node];
            auto& result = evaluations[task.evaluation];
            auto slot = result.children;
            for (auto child = node.first_child; child != no_index; child = nodes[child].next_sibling)
                nodes[child].size = evaluations[choices[slot++].evaluation].size;
            const auto content = node.kind == LayoutKind::grid ? measureGrid(nodes, node, source(task.node), result.box) :
                measureStack(nodes, node);
            result.natural = withPadding(content, node.padding);
            if (!nonnegative(result.natural.width) || !nonnegative(result.natural.height))
                return std::unexpected(ErrorCode::limit_exceeded);
            result.size = {fit(result.natural.width, result.box.width), fit(result.natural.height, result.box.height)};
            if (task.allocated_main)
                (isRow(node.kind) ? result.size.width : result.size.height) = *task.allocated_main;
            return {};
        }

        Result<void> run(const LayoutConstraints& viewport) {
            sources.reserve(nodes.size());
            for (std::size_t index = 0; index < work.size(); ++index)
                if (work[index].output != no_index) sources.push_back(index);
            heads.resize(nodes.size(), no_index);
            evaluations.reserve(nodes.size());
            choices.reserve(nodes.size() - 1);
            const auto root = request(0, viewport);
            if (!root) return std::unexpected(root.error());
            while (!tasks.empty()) {
                auto& task = tasks.back();
                const auto kind = nodes[task.node].kind;
                if (task.child != no_index) {
                    const auto child = task.child;
                    const auto slot = task.slot++;
                    task.child = nodes[child].next_sibling;
                    auto offer = task.offer;
                    if (isFlex(kind)) {
                        const bool row = isRow(kind);
                        if (task.allocated_main) {
                            const auto extent = choices[slot].extent;
                            (row ? offer.width : offer.height) = {extent, extent};
                        } else if (basis(child, row)) continue;
                    }
                    const auto evaluated = request(child, offer);
                    if (!evaluated) return std::unexpected(evaluated.error());
                    choices[slot].evaluation = *evaluated;
                } else if (isFlex(kind) && !task.allocated_main) {
                    if (auto allocated = allocate(task); !allocated) return allocated;
                } else {
                    if (auto finished = finish(task); !finished) return finished;
                    tasks.pop_back();
                }
            }

            // The alternative offers were measured against the same topology. Publish only the
            // measurements selected by the root, then compute grid offsets from those sizes.
            heads.front() = *root;
            if (trace) trace->nodes.front().parent_offer = viewport;
            for (std::size_t index = 0; index < nodes.size(); ++index) {
                auto& node = nodes[index];
                const auto& selected = evaluations[heads[index]];
                node.size = selected.size;
                node.natural = selected.natural;
                auto slot = selected.children;
                if (trace) {
                    auto& entry = trace->nodes[index];
                    const auto& data = source(index);
                    entry = {node.component, data.descriptor->style, entry.parent_offer, selected.box,
                             contentFor(selected.box, data.padding), selected.natural, selected.size};
                    const auto offer = childOffer(node.kind, entry.content_offer, data);
                    for (auto child = node.first_child; child != no_index; child = nodes[child].next_sibling) {
                        auto& child_offer = trace->nodes[child].parent_offer;
                        child_offer = offer;
                        if (isFlex(node.kind)) {
                            const auto extent = choices[slot].extent;
                            (isRow(node.kind) ? child_offer.width : child_offer.height) = {extent, extent};
                        }
                        heads[child] = choices[slot++].evaluation;
                    }
                } else {
                    for (auto child = node.first_child; child != no_index; child = nodes[child].next_sibling)
                        heads[child] = choices[slot++].evaluation;
                }
            }
            for (std::size_t index = 0; index < nodes.size(); ++index)
                if (nodes[index].kind == LayoutKind::grid)
                    measureGrid(nodes, nodes[index], source(index), evaluations[heads[index]].box);
            return {};
        }
    };

    static Result<LayoutMeasurement> measure(const ComponentSnapshot& tree, std::span<const LayoutNode> descriptors,
                                             const LayoutConstraints& viewport, LayoutCache* cache, LayoutDiagnostics* trace) {
        using Reason = LayoutFailureReason;
        if (!valid(viewport)) return fail(trace, Reason::invalid_viewport, ErrorCode::invalid_argument);
        if (tree.components.size() != descriptors.size()) return fail(trace, Reason::descriptor_count, ErrorCode::invalid_argument);
        LayoutMeasurement result;
        result.tree_revision_ = tree.revision;
        if (descriptors.empty()) {
            if (cache) cache->entries_.clear();
            return result;
        }

        const auto& components = tree.components;
        const auto owner = components.front().handle.owner_;
        std::unordered_map<std::uint64_t, std::size_t> indices;
        indices.reserve(components.size());
        std::vector<WorkNode> work(components.size());
        std::size_t visible_count{};
        for (std::size_t index = 0; index < components.size(); ++index) {
            const auto& component = components[index];
            const auto generation = component.handle.generation();
            if (owner == 0 || component.handle.owner_ != owner || generation == 0 ||
                component.parent.has_value() != (index != 0) || !indices.emplace(generation, index).second)
                return fail(trace, Reason::invalid_tree, ErrorCode::invalid_argument, component.handle);
            bool effective = component.visible;
            if (component.parent) {
                const auto parent = indices.find(component.parent->generation());
                if (parent == indices.end() || parent->second >= index || components[parent->second].handle != *component.parent)
                    return fail(trace, Reason::invalid_tree, ErrorCode::invalid_argument, component.handle);
                work[index].parent = parent->second;
                effective = effective && components[parent->second].effectively_visible;
            }
            if (component.effectively_visible != effective) return fail(trace, Reason::invalid_tree, ErrorCode::invalid_argument, component.handle);
            if (effective) ++visible_count;
        }
        for (const auto& descriptor : descriptors) {
            const auto found = indices.find(descriptor.component.generation());
            if (found == indices.end() || components[found->second].handle != descriptor.component)
                return fail(trace, Reason::stale_descriptor, ErrorCode::stale_handle, descriptor.component);
            auto& node = work[found->second];
            const auto& style = descriptor.style;
            if (node.descriptor) return fail(trace, Reason::duplicate_descriptor, ErrorCode::invalid_argument, descriptor.component);
            if (!valid(style)) return fail(trace, Reason::invalid_style, ErrorCode::invalid_argument, descriptor.component);
            if (style.kind != LayoutKind::leaf && (descriptor.intrinsic || descriptor.intrinsic_revision))
                return fail(trace, Reason::intrinsic_on_container, ErrorCode::invalid_argument, descriptor.component);
            node.descriptor = &descriptor;
            node.padding = {style.padding.left + style.padding.right, style.padding.top + style.padding.bottom};
            if (!std::isfinite(node.padding.width) || !std::isfinite(node.padding.height))
                return std::unexpected(ErrorCode::limit_exceeded);
        }

        // Grid cells divide the available width before leaves evaluate width-dependent content.
        result.nodes_.reserve(visible_count);
        bool has_flex{};
        for (std::size_t index = 0; index < components.size(); ++index) {
            auto& node = work[index];
            auto* parent = node.parent == no_index ? nullptr : &work[node.parent];
            if (parent && parent->descriptor->style.kind == LayoutKind::leaf)
                return fail(trace, Reason::leaf_with_children, ErrorCode::invalid_argument, parent->descriptor->component);
            if (!components[index].effectively_visible) continue;
            const auto& style = node.descriptor->style;
            has_flex |= isFlex(style.kind);
            const auto offer = parent ? childOffer(parent->descriptor->style.kind, parent->content, *parent) : viewport;
            node.box = boxFor(offer, style);
            node.content = contentFor(node.box, node.padding);
            if (style.kind == LayoutKind::grid) {
                node.grid_gaps = style.gap * (style.grid_columns - 1);
                if (!std::isfinite(node.grid_gaps)) return std::unexpected(ErrorCode::limit_exceeded);
            }
            node.output = result.nodes_.size();
            result.nodes_.push_back({components[index].handle, parent ? parent->output : no_index, no_index, no_index,
                                     style.kind, style.padding, style.gap, {}, {}, {}});
            if (parent) {
                if (parent->last_child == no_index) result.nodes_[parent->output].first_child = node.output;
                else result.nodes_[parent->last_child].next_sibling = node.output;
                parent->last_child = node.output;
            }
        }

        std::vector<LayoutCache::IntrinsicEntry> next;
        if (cache) next.reserve(std::min(cache->entries_.size(), visible_count));
        if (trace) trace->nodes.resize(visible_count);
        if (has_flex) {
            // Flex needs both natural and assigned offers. Keep alternative measurements local
            // to this pass so the ordinary Row/Column/Grid path keeps its single traversal.
            FlexPass flex{result.nodes_, work, components, cache, next, trace};
            if (auto measured = flex.run(viewport); !measured) return std::unexpected(measured.error());
        } else {
            for (std::size_t work_index = work.size(); work_index-- > 0;) {
                const auto& work_node = work[work_index];
                if (work_node.output == no_index) continue;
                auto& node = result.nodes_[work_node.output];
                LayoutSize content;
                if (node.kind == LayoutKind::leaf) {
                    const auto size = measureLeaf(work_node, components[work_index], work_node.content, cache, next, trace);
                    if (!size) return std::unexpected(size.error());
                    content = *size;
                } else if (node.kind == LayoutKind::grid) {
                    content = measureGrid(result.nodes_, node, work_node, work_node.box);
                } else {
                    content = measureStack(result.nodes_, node);
                }
                node.natural = withPadding(content, node.padding);
                if (!std::isfinite(node.natural.width) || !std::isfinite(node.natural.height))
                    return std::unexpected(ErrorCode::limit_exceeded);
                node.size = {fit(node.natural.width, work_node.box.width), fit(node.natural.height, work_node.box.height)};
                if (trace) {
                    const auto* parent = work_node.parent == no_index ? nullptr : &work[work_node.parent];
                    const auto offer = parent ? childOffer(parent->descriptor->style.kind, parent->content, *parent) : viewport;
                    trace->nodes[work_node.output] = {node.component, work_node.descriptor->style, offer,
                        work_node.box, work_node.content, node.natural, node.size};
                }
            }
        }
        if (cache) {
            std::ranges::sort(next, {}, [](const auto& entry) { return entry.component.generation(); });
            cache->entries_.swap(next);
        }
        return result;
    }

    static Result<LayoutMeasurement> run(const ComponentSnapshot& tree, std::span<const LayoutNode> descriptors,
                                         const LayoutConstraints& viewport, LayoutCache* cache, LayoutDiagnostics* trace) {
        if (trace) *trace = LayoutDiagnostics{tree.revision, viewport, {}, {}, {}};
        auto result = [&]() -> Result<LayoutMeasurement> {
            try {
                return measure(tree, descriptors, viewport, cache, trace);
            } catch (const std::bad_alloc&) {
                return std::unexpected(ErrorCode::out_of_memory);
            } catch (const std::length_error&) {
                return std::unexpected(ErrorCode::limit_exceeded);
            }
        }();
        if (trace && !result) {
            trace->nodes.clear();
            trace->intrinsic.clear();
            if (!trace->failure) fail(trace, LayoutFailureReason::measurement, result.error());
        }
        return result;
    }

    static Result<LayoutFrame> arrange(const LayoutMeasurement& measurement, LayoutPoint origin) {
        if (!std::isfinite(origin.x) || !std::isfinite(origin.y)) return std::unexpected(ErrorCode::invalid_argument);
        LayoutFrame frame;
        frame.tree_revision_ = measurement.tree_revision_;
        const auto& nodes = measurement.nodes_;
        if (nodes.empty()) return frame;

        struct Placed { std::size_t box_index{}; double cursor{}; };
        std::vector<Placed> placed(nodes.size());
        std::vector<LayoutPoint> offsets;
        offsets.reserve(nodes.size());
        frame.boxes_.reserve(nodes.size());
        for (std::size_t index = 0; index != no_index;) {
            const auto& node = nodes[index];
            LayoutPoint offset;
            LayoutPoint position = origin;
            std::optional<std::size_t> parent_index;
            if (node.parent != no_index) {
                const auto& parent = nodes[node.parent];
                auto& parent_placement = placed[node.parent];
                parent_index = parent_placement.box_index;
                offset = {parent.padding.left, parent.padding.top};
                if (parent.kind == LayoutKind::grid) {
                    offset.x += node.grid_offset.x;
                    offset.y += node.grid_offset.y;
                } else {
                    const bool row = isRow(parent.kind);
                    (row ? offset.x : offset.y) += parent_placement.cursor;
                    if (node.next_sibling != no_index) {
                        parent_placement.cursor += row ? node.size.width : node.size.height;
                        parent_placement.cursor += parent.gap;
                    }
                }
                const auto& parent_rect = frame.boxes_[*parent_index].bounds;
                position = {parent_rect.x + offset.x, parent_rect.y + offset.y};
            }
            const LayoutRect bounds{position.x, position.y, node.size.width, node.size.height};
            if (!validRect(bounds))
                return std::unexpected(ErrorCode::limit_exceeded);
            const auto hit = parent_index ? intersectRects(frame.boxes_[*parent_index].hit_bounds, bounds) : bounds;
            placed[index].box_index = frame.boxes_.size();
            frame.boxes_.push_back({node.component, parent_index, bounds,
                                    {std::max(0.0, node.natural.width - node.size.width),
                                     std::max(0.0, node.natural.height - node.size.height)}, hit});
            offsets.push_back(offset);

            // Parent-first depth-first paint order, with the original sibling order.
            if (node.first_child != no_index) index = node.first_child;
            else {
                while (index != no_index && nodes[index].next_sibling == no_index) index = nodes[index].parent;
                if (index != no_index) index = nodes[index].next_sibling;
            }
        }

        for (std::size_t index = frame.boxes_.size(); index-- > 0;) {
            const auto& box = frame.boxes_[index];
            const LayoutSize extent{box.bounds.width + box.overflow.width, box.bounds.height + box.overflow.height};
            if (!std::isfinite(box.bounds.x + extent.width) || !std::isfinite(box.bounds.y + extent.height))
                return std::unexpected(ErrorCode::limit_exceeded);
            if (!box.parent_index) continue;
            auto& parent = frame.boxes_[*box.parent_index];
            parent.overflow.width = std::max(parent.overflow.width, (offsets[index].x + extent.width) - parent.bounds.width);
            parent.overflow.height = std::max(parent.overflow.height, (offsets[index].y + extent.height) - parent.bounds.height);
        }
        return frame;
    }
};
} // namespace graphics_ui::core::detail

namespace graphics_ui::core {

std::optional<ComponentHandle> LayoutFrame::hitTest(LayoutPoint point) const noexcept {
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) return std::nullopt;
    for (auto box = boxes_.rbegin(); box != boxes_.rend(); ++box) {
        const auto& hit = box->hit_bounds;
        if (point.x >= hit.x && point.y >= hit.y && point.x < hit.x + hit.width && point.y < hit.y + hit.height)
            return box->component;
    }
    return std::nullopt;
}

Result<LayoutMeasurement> measureLayout(const ComponentSnapshot& tree, std::span<const LayoutNode> nodes,
                                        LayoutConstraints viewport) {
    return detail::LayoutPass::run(tree, nodes, viewport, nullptr, nullptr);
}

Result<LayoutMeasurement> measureLayout(const ComponentSnapshot& tree, std::span<const LayoutNode> nodes,
                                        LayoutConstraints viewport, LayoutDiagnostics& diagnostics) {
    return detail::LayoutPass::run(tree, nodes, viewport, nullptr, &diagnostics);
}

Result<LayoutFrame> arrangeLayout(const LayoutMeasurement& measurement, LayoutPoint origin) {
    try {
        return detail::LayoutPass::arrange(measurement, origin);
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    } catch (const std::length_error&) {
        return std::unexpected(ErrorCode::limit_exceeded);
    }
}

Result<LayoutMeasurement> LayoutCache::measure(const ComponentSnapshot& tree, std::span<const LayoutNode> nodes,
                                               LayoutConstraints viewport) {
    return measureImpl(tree, nodes, viewport, nullptr);
}

Result<LayoutMeasurement> LayoutCache::measure(const ComponentSnapshot& tree, std::span<const LayoutNode> nodes,
                                               LayoutConstraints viewport, LayoutDiagnostics& diagnostics) {
    return measureImpl(tree, nodes, viewport, &diagnostics);
}

Result<LayoutMeasurement> LayoutCache::measureImpl(const ComponentSnapshot& tree, std::span<const LayoutNode> nodes,
                                                   LayoutConstraints viewport, LayoutDiagnostics* diagnostics) {
    if (measuring_) return std::unexpected(ErrorCode::busy);
    measuring_ = true;
    struct Reset {
        bool& measuring;
        ~Reset() { measuring = false; }
    } reset{measuring_};
    return detail::LayoutPass::run(tree, nodes, viewport, this, diagnostics);
}

} // namespace graphics_ui::core
