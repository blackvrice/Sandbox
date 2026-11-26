#include <graphics-ui/core/controls.hpp>

#include "layout_geometry.hpp"

#include <cmath>
#include <new>
#include <stdexcept>

namespace graphics_ui::core {
namespace {
constexpr ButtonInteraction handled{{true, true}, false};
constexpr ButtonInteraction activated{{true, true}, true};

std::int32_t sliderAt(LayoutRect track, SliderRange range, double x) noexcept {
    if (x <= track.x) return range.minimum;
    if (x >= track.x + track.width) return range.maximum;
    const std::int64_t span = std::int64_t{range.maximum} - range.minimum;
    const double offset = ((x - track.x) / track.width) * static_cast<double>(span);
    const auto low = std::min(span, static_cast<std::int64_t>(offset / range.step) * range.step);
    const auto high = std::min(span, low + range.step);
    const auto nearest = offset - static_cast<double>(low) < static_cast<double>(high) - offset ? low : high;
    return static_cast<std::int32_t>(range.minimum + nearest);
}

std::optional<std::int32_t> sliderKey(KeyCode key, SliderRange range, std::int32_t current) noexcept {
    const auto offset = std::int64_t{current} - range.minimum;
    switch (key) {
    case home_key: return range.minimum;
    case end_key: return range.maximum;
    case right_key:
    case up_key:
        return static_cast<std::int32_t>(std::min<std::int64_t>(range.maximum,
            range.minimum + (offset / range.step + 1) * range.step));
    case left_key:
    case down_key:
        return offset ? static_cast<std::int32_t>(range.minimum + ((offset - 1) / range.step) * range.step) : range.minimum;
    default: return {};
    }
}
} // namespace

Result<void> Button::setEnabled(bool enabled) {
    if (auto result = input_.setFocusable(target_, enabled); !result) return result;
    enabled_ = enabled;
    if (!enabled && pressed_) {
        const auto captor = input_.captureIdentity(pressed_->pointer);
        if (!captor) return std::unexpected(captor.error());
        if (*captor == pressed_->capture) {
            if (auto result = input_.releasePointer(pressed_->pointer); !result) return result;
        }
        pressed_.reset();
    }
    return {};
}

bool Button::accepts(const RoutedEvent& event) const noexcept {
    return enabled_ && event.phase == EventPhase::target && event.target == target_ && event.receiver == target_;
}

Result<ButtonInteraction> Button::handle(const RoutedEvent& event) {
    if (!accepts(event)) return ButtonInteraction{};
    if (const auto* key = std::get_if<KeyEvent>(&event.input)) {
        if (key->chord.modifiers != KeyModifiers::none ||
            (key->chord.key != enter_key && key->chord.key != space_key))
            return ButtonInteraction{};
        return key->action == KeyAction::down && !key->repeat ? activated : handled;
    }

    const auto& pointer = std::get<PointerEvent>(event.input);
    switch (pointer.action) {
    case PointerAction::down: {
        if (pointer.button != PointerButton::primary || event.pointer_hit != target_) return ButtonInteraction{};
        if (pressed_) {
            const auto captor = input_.captureIdentity(pressed_->pointer);
            if (!captor) return std::unexpected(captor.error());
            if (*captor == pressed_->capture)
                return pressed_->pointer == pointer.pointer ? handled : ButtonInteraction{};
            pressed_.reset();
        }
        if (event.pointer_capture) return ButtonInteraction{};
        const auto capture = input_.acquirePointer(pointer.pointer, target_);
        if (!capture) return std::unexpected(capture.error());
        if (auto focus = input_.setFocus(target_); !focus) {
            (void) input_.releasePointer(pointer.pointer);
            return std::unexpected(focus.error());
        }
        pressed_ = Press{pointer.pointer, *capture};
        return handled;
    }
    case PointerAction::move:
        return pressed_ && pressed_->pointer == pointer.pointer && pressed_->capture == event.pointer_capture ?
            handled : ButtonInteraction{};
    case PointerAction::up:
    case PointerAction::cancel: {
        if (!pressed_ || pressed_->pointer != pointer.pointer) return ButtonInteraction{};
        const bool owns_capture = pressed_->capture == event.pointer_capture;
        pressed_.reset();
        if (!owns_capture) return ButtonInteraction{};
        return pointer.action == PointerAction::up && pointer.button == PointerButton::primary &&
            event.pointer_hit == target_ ? activated : handled;
    }
    }
    return ButtonInteraction{};
}

Result<ControlVisualState> Button::visualState(std::optional<ComponentHandle> hit) const noexcept {
    if (!enabled_) return ControlVisualState{};
    const auto focus = input_.focus();
    if (!focus) return std::unexpected(focus.error());
    const bool hovered = hit == target_;
    bool pressed{};
    if (hovered && pressed_) {
        const auto capture = input_.captureIdentity(pressed_->pointer);
        if (!capture) return std::unexpected(capture.error());
        pressed = *capture == pressed_->capture;
    }
    return ControlVisualState{true, hovered, *focus == target_, pressed};
}

Result<ToggleInteraction> Toggle::handle(const RoutedEvent& event, bool checked) {
    const auto press = button_.handle(event);
    if (!press) return std::unexpected(press.error());
    ToggleInteraction result{press->reply, {}};
    if (press->activated) result.proposed = !checked;
    return result;
}

Result<SliderInteraction> Slider::handle(const RoutedEvent& event, LayoutRect track, SliderRange range,
                                         std::int32_t current) {
    if (!button_.accepts(event)) return SliderInteraction{};
    if (range.minimum > range.maximum || range.step <= 0 || current < range.minimum || current > range.maximum)
        return std::unexpected(ErrorCode::invalid_argument);
    if (const auto* key = std::get_if<KeyEvent>(&event.input)) {
        if (key->action != KeyAction::down || key->chord.modifiers != KeyModifiers::none) return SliderInteraction{};
        const auto focus = button_.focusIdentity();
        if (!focus) return std::unexpected(focus.error());
        if (!*focus) return SliderInteraction{};
        auto value = sliderKey(key->chord.key, range, current);
        if (!value) return SliderInteraction{};
        if (*value == current) value.reset();
        return SliderInteraction{handled.reply, value};
    }

    const auto& pointer = std::get<PointerEvent>(event.input);
    if (!detail::validRect(track) || !std::isfinite(pointer.x)) return std::unexpected(ErrorCode::invalid_argument);
    const auto interaction = button_.handle(event);
    if (!interaction) return std::unexpected(interaction.error());
    SliderInteraction result{interaction->reply, {}};
    if (result.reply.handled && track.width > 0 &&
        (pointer.action == PointerAction::move || pointer.action == PointerAction::down ||
         (pointer.action == PointerAction::up && pointer.button == PointerButton::primary))) {
        const auto value = sliderAt(track, range, pointer.x);
        if (value != current) result.proposed = value;
    }
    return result;
}

Result<void> ComboBox::setEnabled(bool enabled) {
    auto changed = button_.setEnabled(enabled);
    if (changed && !enabled) dismiss();
    return changed;
}

Result<std::optional<std::size_t>> ComboBox::highlighted() const noexcept {
    if (!button_.enabled() || !highlight_) return std::nullopt;
    const auto focus = button_.focusIdentity();
    if (!focus) return std::unexpected(focus.error());
    return *focus == open_focus_ ? highlight_ : std::nullopt;
}

Result<ComboInteraction> ComboBox::handle(const RoutedEvent& event, std::size_t item_count,
    std::optional<std::size_t> selected, std::optional<std::size_t> item_hit) {
    if (!button_.accepts(event)) return ComboInteraction{};
    if ((selected && *selected >= item_count) || (item_hit && *item_hit >= item_count))
        return std::unexpected(ErrorCode::invalid_argument);
    const auto press = button_.handle(event);
    if (!press) return std::unexpected(press.error());
    const auto* key = std::get_if<KeyEvent>(&event.input);
    const auto focus = button_.focusIdentity();
    if (!focus) return std::unexpected(focus.error());
    if (!*focus || *focus != open_focus_ || (highlight_ && *highlight_ >= item_count)) dismiss();
    ComboInteraction result{press->reply, {}};
    if (!*focus || !item_count) return result;

    if (press->activated) {
        if (!highlight_) {
            open_focus_ = *focus;
            highlight_ = selected.value_or(0);
        } else {
            const auto selection = key ? highlight_ : item_hit;
            if (selection != selected) result.proposed = selection;
            dismiss();
        }
        return result;
    }

    if (!key || key->action != KeyAction::down || key->chord.modifiers != KeyModifiers::none) return result;
    if (key->chord.key == escape_key) {
        if (highlight_) result.reply = handled.reply;
        dismiss();
        return result;
    }
    auto next = highlight_.value_or(selected.value_or(0));
    switch (key->chord.key) {
    case home_key: next = 0; break;
    case end_key: next = item_count - 1; break;
    case up_key: if (next) --next; break;
    case down_key: if (next < item_count - 1) ++next; break;
    default: return result;
    }
    open_focus_ = *focus;
    highlight_ = next;
    result.reply = handled.reply;
    return result;
}

} // namespace graphics_ui::core
