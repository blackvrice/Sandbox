#include <graphics-ui/core/animation.hpp>

#include "paint_geometry.hpp"

namespace graphics_ui::core {

Result<AnimationFrame> VisualAnimation::play(VisualState target, AnimationTime duration, AnimationEasing easing) noexcept {
    if (!detail::validVisual(target) || duration < AnimationTime::zero() ||
        (easing != AnimationEasing::linear && easing != AnimationEasing::smoothstep))
        return std::unexpected(ErrorCode::invalid_argument);
    if (duration == AnimationTime::zero() || target == value_) {
        const bool changed = value_ != target;
        value_ = target;
        cancel();
        return AnimationFrame{changed, false};
    }
    if (duration > AnimationTime::max() - time_) return std::unexpected(ErrorCode::limit_exceeded);
    transition_ = Transition{value_, target, time_, duration, easing};
    return AnimationFrame{false, true};
}

Result<AnimationFrame> VisualAnimation::advance(AnimationTime time) noexcept {
    if (time < time_) return std::unexpected(ErrorCode::invalid_argument);
    if (time == time_) return AnimationFrame{false, needsTick()};
    time_ = time;
    if (!transition_) return AnimationFrame{};
    const auto& [from, target, start, duration, easing] = *transition_;
    const auto elapsed = time - start;
    if (elapsed >= duration) {
        const bool changed = value_ != target;
        value_ = target;
        cancel();
        return AnimationFrame{changed, false};
    }
    auto fraction = static_cast<double>(elapsed.count()) / static_cast<double>(duration.count());
    if (easing == AnimationEasing::smoothstep) fraction = fraction * fraction * (3 - 2 * fraction);
    const auto& first = from.transform;
    const auto& last = target.transform;
    const VisualState sample{
        {{std::lerp(first.translation.x, last.translation.x, fraction),
          std::lerp(first.translation.y, last.translation.y, fraction)},
         std::lerp(first.scale, last.scale, fraction)},
        std::lerp(from.opacity, target.opacity, fraction)};
    const bool changed = value_ != sample;
    value_ = sample;
    return AnimationFrame{changed, true};
}

} // namespace graphics_ui::core
