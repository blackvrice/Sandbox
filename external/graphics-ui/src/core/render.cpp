#include <graphics-ui/core/controls.hpp>

#include "paint_geometry.hpp"
#include "utf8.hpp"

#include <new>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace graphics_ui::core {
namespace {
bool validDraw(const FillRect& draw) noexcept { return detail::validRect(draw.bounds); }

bool validText(LayoutRect bounds, FontId font, double em_size, std::string_view text) noexcept {
    return detail::validRect(bounds) && font != 0 && std::isfinite(em_size) && em_size > 0 && detail::validUtf8(text);
}

bool validDraw(const DrawText& draw) noexcept {
    return validText(draw.bounds, draw.font, draw.em_size, draw.utf8);
}

bool validDraw(const LinearGradient& draw) noexcept {
    return detail::validRect(draw.bounds) && (draw.axis == GradientAxis::horizontal || draw.axis == GradientAxis::vertical);
}

bool validDraw(const DropShadow& draw) noexcept {
    return detail::validRect(draw.bounds) && draw.radius >= 0 && detail::validRect(detail::shadowExtent(draw));
}

LayoutRect place(LayoutRect bounds, const PaintTransform& placement) noexcept {
    const auto& [origin, scale] = placement;
    return {origin.x + bounds.x * scale, origin.y + bounds.y * scale, bounds.width * scale, bounds.height * scale};
}

void attenuate(RenderColor& color, double opacity) noexcept {
    color.alpha = static_cast<std::uint8_t>(std::lround(color.alpha * opacity));
}

Result<RenderCommand> translate(const DrawCommand& draw, const VisualState& placement) {
    if (draw.valueless_by_exception()) return std::unexpected(ErrorCode::invalid_argument);
    return std::visit([&](const auto& primitive) -> Result<RenderCommand> {
        if (!validDraw(primitive)) return std::unexpected(ErrorCode::invalid_argument);
        auto result = primitive;
        result.bounds = place(result.bounds, placement.transform);
        if (!detail::validRect(result.bounds)) return std::unexpected(ErrorCode::limit_exceeded);
        using Primitive = std::decay_t<decltype(result)>;
        if constexpr (std::is_same_v<Primitive, DrawText>) {
            result.em_size *= placement.transform.scale;
            if (!(result.em_size > 0 && std::isfinite(result.em_size))) return std::unexpected(ErrorCode::limit_exceeded);
        }
        if constexpr (std::is_same_v<Primitive, DropShadow>) {
            result.radius *= placement.transform.scale;
            if (!detail::validRect(detail::shadowExtent(result))) return std::unexpected(ErrorCode::limit_exceeded);
        }
        if (placement.opacity != 1) {
            if constexpr (std::is_same_v<Primitive, LinearGradient>) {
                attenuate(result.start, placement.opacity);
                attenuate(result.end, placement.opacity);
            } else {
                attenuate(result.color, placement.opacity);
            }
        }
        return result;
    }, draw);
}

template<class Draw>
bool record(const Draw& draw, std::optional<LayoutRect> clip, std::vector<RecordedDraw>& output) {
    if (!validDraw(draw)) return false;
    auto extent = draw.bounds;
    if constexpr (std::is_same_v<Draw, DrawText>) {
        if (draw.utf8.empty()) return true;
    }
    if constexpr (std::is_same_v<Draw, LinearGradient>) {
        if (draw.start.alpha == 0 && draw.end.alpha == 0) return true;
    } else {
        if (draw.color.alpha == 0) return true;
    }
    if constexpr (std::is_same_v<Draw, DropShadow>) {
        if (draw.bounds.width == 0 || draw.bounds.height == 0) return true;
        extent = detail::shadowExtent(draw);
    }
    const auto visible = clip ? detail::intersectRects(extent, *clip) : extent;
    if (visible.width > 0 && visible.height > 0) output.push_back({draw, clip, visible});
    return true;
}
} // namespace

Result<DrawText> makeText(LayoutRect bounds, std::string_view utf8, const StyleProperties& style) {
    if (!validText(bounds, style.font.font, style.font.em_size, utf8))
        return std::unexpected(ErrorCode::invalid_argument);
    try {
        return DrawText{bounds, style.font.font, style.font.em_size, std::string(utf8), style.ink};
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    } catch (const std::length_error&) {
        return std::unexpected(ErrorCode::limit_exceeded);
    }
}

Result<DisplayList> buildDisplayList(const LayoutFrame& frame, std::span<const RenderNode> nodes) {
    try {
        const auto boxes = frame.boxes();
        DisplayList list;
        list.tree_revision_ = frame.treeRevision();
        if (boxes.empty()) {
            if (!nodes.empty()) return std::unexpected(ErrorCode::stale_handle);
            return list;
        }

        std::unordered_map<std::uint64_t, std::size_t> indices;
        indices.reserve(boxes.size());
        for (std::size_t index = 0; index < boxes.size(); ++index) {
            indices.emplace(boxes[index].component.generation(), index);
        }
        std::vector<const RenderNode*> paint(boxes.size());
        for (const auto& node : nodes) {
            const auto found = indices.find(node.component.generation());
            if (found == indices.end() || boxes[found->second].component != node.component)
                return std::unexpected(ErrorCode::stale_handle);
            if (paint[found->second]) return std::unexpected(ErrorCode::invalid_argument);
            paint[found->second] = &node;
        }

        struct Scope {
            std::size_t index;
            bool clip;
            VisualState placement;
        };
        std::vector<Scope> stack(0);
        const auto closeScope = [&] {
            if (stack.back().clip) list.commands_.emplace_back(PopClip{});
            stack.pop_back();
        };
        for (std::size_t index = 0; index < boxes.size(); ++index) {
            const auto& box = boxes[index];
            const auto* node = paint[index];
            while (!stack.empty() && box.parent_index != stack.back().index) closeScope();
            const auto local = node ? node->visual : VisualState{};
            if (!detail::validVisual(local)) return std::unexpected(ErrorCode::invalid_argument);
            const LayoutPoint layout_origin{box.bounds.x, box.bounds.y};
            auto placement = local;
            auto& [origin, scale] = placement.transform;
            origin = {layout_origin.x + origin.x, layout_origin.y + origin.y};
            if (!stack.empty()) {
                const auto& parent = stack.back();
                const auto& [parent_origin, parent_scale] = parent.placement.transform;
                const auto& layout = boxes[parent.index].bounds;
                if (parent_scale != 1 || parent_origin != LayoutPoint{layout.x, layout.y}) {
                    origin = {parent_origin.x + (layout_origin.x - layout.x + local.transform.translation.x) * parent_scale,
                              parent_origin.y + (layout_origin.y - layout.y + local.transform.translation.y) * parent_scale};
                }
                scale *= parent_scale;
                placement.opacity *= parent.placement.opacity;
            }
            if (!detail::validVisual(placement)) return std::unexpected(ErrorCode::limit_exceeded);
            // The root viewport stays in layout coordinates while child clips follow their paint transform.
            const bool clip = index == 0 || (node && node->clip_to_bounds);
            if (clip) {
                const auto bounds = index == 0 ? box.bounds : place({0, 0, box.bounds.width, box.bounds.height}, placement.transform);
                if (!detail::validRect(bounds)) return std::unexpected(ErrorCode::limit_exceeded);
                list.commands_.emplace_back(PushClip{bounds});
            }
            if (node) {
                for (const auto& draw : node->draws) {
                    auto command = translate(draw, placement);
                    if (!command) return std::unexpected(command.error());
                    list.commands_.push_back(std::move(*command));
                }
            }
            stack.push_back({index, clip, placement});
        }
        while (!stack.empty()) closeScope();
        return list;
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    } catch (const std::length_error&) {
        return std::unexpected(ErrorCode::limit_exceeded);
    }
}

Result<void> MockRenderer::replay(const DisplayList& list) {
    return replay(list.commands(), list.treeRevision());
}

Result<void> MockRenderer::replay(std::span<const RenderCommand> commands, std::uint64_t tree_revision) {
    try {
        auto next = std::make_unique<RenderRecording>();
        next->tree_revision_ = tree_revision;
        std::vector<LayoutRect> clips(0);
        for (const auto& command : commands) {
            if (command.valueless_by_exception()) return std::unexpected(ErrorCode::invalid_argument);
            if (const auto* clip = std::get_if<PushClip>(&command)) {
                if (!detail::validRect(clip->bounds)) return std::unexpected(ErrorCode::invalid_argument);
                clips.push_back(clips.empty() ? clip->bounds : detail::intersectRects(clips.back(), clip->bounds));
            } else if (std::holds_alternative<PopClip>(command)) {
                if (clips.empty()) return std::unexpected(ErrorCode::invalid_argument);
                clips.pop_back();
            } else {
                const auto active = clips.empty() ? std::nullopt : std::optional{clips.back()};
                const bool valid = std::visit([&](const auto& draw) {
                    using Draw = std::decay_t<decltype(draw)>;
                    if constexpr (std::is_same_v<Draw, PushClip> || std::is_same_v<Draw, PopClip>)
                        return false;
                    else
                        return record(draw, active, next->draws_);
                }, command);
                if (!valid) return std::unexpected(ErrorCode::invalid_argument);
            }
        }
        if (!clips.empty()) return std::unexpected(ErrorCode::invalid_argument);
        last_ = std::move(next);
        return {};
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    } catch (const std::length_error&) {
        return std::unexpected(ErrorCode::limit_exceeded);
    }
}

} // namespace graphics_ui::core
