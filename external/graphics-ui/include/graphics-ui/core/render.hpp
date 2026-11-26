#pragma once

#include <graphics-ui/core/layout.hpp>

#include <string>
#include <variant>

namespace graphics_ui::core {

using FontId = std::uint64_t;

struct RenderColor {
    std::uint8_t red{}, green{}, blue{}, alpha = 255;
    bool operator==(const RenderColor&) const = default;
};

struct FillRect {
    LayoutRect bounds;
    RenderColor color;
    bool operator==(const FillRect&) const = default;
};

struct DrawText {
    LayoutRect bounds;
    FontId font{};
    double em_size{};
    std::string utf8;
    RenderColor color;
    bool operator==(const DrawText&) const = default;
};

enum class GradientAxis { horizontal, vertical };

struct LinearGradient {
    LayoutRect bounds;
    RenderColor start, end;
    GradientAxis axis = GradientAxis::horizontal;
    bool operator==(const LinearGradient&) const = default;
};

struct DropShadow {
    // Bounds describe the shifted solid area. The feather extends radius units outside it.
    LayoutRect bounds;
    RenderColor color;
    double radius{};
    bool operator==(const DropShadow&) const = default;
};

struct PushClip {
    LayoutRect bounds;
    bool operator==(const PushClip&) const = default;
};

struct PopClip {
    bool operator==(const PopClip&) const = default;
};

using DrawCommand = std::variant<FillRect, DrawText, LinearGradient, DropShadow>;
using RenderCommand = std::variant<PushClip, PopClip, FillRect, DrawText, LinearGradient, DropShadow>;

struct PaintTransform {
    LayoutPoint translation;
    double scale = 1;
    bool operator==(const PaintTransform&) const = default;
};

struct VisualState {
    PaintTransform transform;
    double opacity = 1;
    bool operator==(const VisualState&) const = default;
};

struct RenderNode {
    ComponentHandle component;
    bool clip_to_bounds{};
    // Commands are in component-local logical coordinates and copied during recording.
    std::span<const DrawCommand> draws;
    // The transform and per-draw opacity apply to this component and its descendants.
    VisualState visual{};
};

class DisplayList {
public:
    [[nodiscard]] std::uint64_t treeRevision() const noexcept { return tree_revision_; }
    [[nodiscard]] std::span<const RenderCommand> commands() const & noexcept { return commands_; }
    std::span<const RenderCommand> commands() const && = delete;

private:
    friend Result<DisplayList> buildDisplayList(const LayoutFrame&, std::span<const RenderNode>);
    std::uint64_t tree_revision_{};
    std::vector<RenderCommand> commands_{std::size_t{0}};
};

// Optional paint descriptions refer to visible components in the supplied frame.
[[nodiscard]] Result<DisplayList> buildDisplayList(const LayoutFrame& frame, std::span<const RenderNode> nodes);

struct RecordedDraw {
    DrawCommand command;
    std::optional<LayoutRect> clip;
    LayoutRect visible_bounds;
    bool operator==(const RecordedDraw&) const = default;
};

class RenderRecording {
public:
    [[nodiscard]] std::uint64_t treeRevision() const noexcept { return tree_revision_; }
    [[nodiscard]] std::span<const RecordedDraw> draws() const & noexcept { return draws_; }
    std::span<const RecordedDraw> draws() const && = delete;

private:
    friend class MockRenderer;
    std::uint64_t tree_revision_{};
    std::vector<RecordedDraw> draws_{std::size_t{0}};
};

class MockRenderer {
public:
    // A failed replay leaves the previous recording available.
    [[nodiscard]] Result<void> replay(const DisplayList& list);
    [[nodiscard]] Result<void> replay(std::span<const RenderCommand> commands, std::uint64_t tree_revision = 0);
    // The recording remains valid until the next successful replay or destruction.
    [[nodiscard]] const RenderRecording* recording() const noexcept { return last_.get(); }

private:
    std::unique_ptr<const RenderRecording> last_;
};

} // namespace graphics_ui::core
