#pragma once

#include <graphics-ui/core/render.hpp>

#include <chrono>

namespace graphics_ui::core {

using AnimationTime = std::chrono::nanoseconds;
enum class AnimationEasing { linear, smoothstep };

struct AnimationFrame {
    bool changed{};
    bool needs_tick{};
};

class VisualAnimation {
public:
    // Play from the last sampled value. Zero duration publishes the target immediately.
    [[nodiscard]] Result<AnimationFrame> play(VisualState target, AnimationTime duration,
        AnimationEasing easing = AnimationEasing::linear) noexcept;
    // Timestamps are monotonic and relative to a host-selected epoch.
    [[nodiscard]] Result<AnimationFrame> advance(AnimationTime time) noexcept;
    // Keep the last sampled value. The host can stop scheduling when this track is idle.
    void cancel() noexcept { transition_.reset(); }
    [[nodiscard]] const VisualState& value() const noexcept { return value_; }
    [[nodiscard]] bool needsTick() const noexcept { return transition_.has_value(); }

private:
    struct Transition {
        VisualState from, target;
        AnimationTime start, duration;
        AnimationEasing easing;
    };

    VisualState value_;
    AnimationTime time_{};
    std::optional<Transition> transition_;
};

} // namespace graphics_ui::core
