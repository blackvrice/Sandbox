#pragma once

#include <graphics-ui/core/component_tree.hpp>

#include <compare>
#include <variant>

namespace graphics_ui::core {

using PointerId = std::uint32_t;
using KeyCode = std::uint32_t;

enum class KeyModifiers : std::uint8_t { none = 0, shift = 1, control = 2, alt = 4, meta = 8 };
constexpr KeyModifiers operator|(KeyModifiers left, KeyModifiers right) noexcept {
    return static_cast<KeyModifiers>(static_cast<unsigned>(left) | static_cast<unsigned>(right));
}

enum class PointerAction { down, move, up, cancel };
enum class PointerButton { none, primary, secondary, middle, auxiliary1, auxiliary2 };
enum class KeyAction { down, up };
enum class EventPhase { capture, target, bubble };
enum class FocusDirection { forward, backward };

struct PointerEvent {
    PointerAction action = PointerAction::move;
    PointerId pointer{};
    double x{}, y{};
    PointerButton button = PointerButton::none;
    KeyModifiers modifiers = KeyModifiers::none;
};

struct KeyChord {
    KeyCode key{};
    KeyModifiers modifiers = KeyModifiers::none;
    auto operator<=>(const KeyChord&) const = default;
};

struct KeyEvent {
    KeyAction action = KeyAction::down;
    KeyChord chord;
    bool repeat{};
};

using InputEvent = std::variant<PointerEvent, KeyEvent>;

struct RoutedEvent {
    const InputEvent& input;
    EventPhase phase;
    ComponentHandle target;
    ComponentHandle receiver;
    // Actual visible hit and capture identity before a terminal pointer event released its lease.
    std::optional<ComponentHandle> pointer_hit{};
    std::uint64_t pointer_capture{};
};

struct EventReply {
    bool handled{};
    bool stop_propagation{};
};

struct DispatchResult {
    std::optional<ComponentHandle> target;
    bool handled{};
};

class InputDispatcher {
public:
    using Handler = std::function<EventReply(const RoutedEvent&)>;
    using Shortcut = std::function<bool(const KeyEvent&)>;

    // The tree and dispatcher share one owner thread. The tree may be destroyed first.
    [[nodiscard]] static Result<std::unique_ptr<InputDispatcher>> create(ComponentTree& tree);

    InputDispatcher(const InputDispatcher&) = delete;
    InputDispatcher& operator=(const InputDispatcher&) = delete;
    ~InputDispatcher() noexcept;

    // A nonempty callable replaces the old binding. An empty callable clears it.
    // Callables must not throw and receive event references for the duration of the call.
    [[nodiscard]] Result<void> setHandler(ComponentHandle target, Handler handler);
    [[nodiscard]] Result<void> setShortcut(ComponentHandle target, KeyChord chord, Shortcut shortcut);

    [[nodiscard]] Result<void> setFocusable(ComponentHandle target, bool focusable);
    [[nodiscard]] Result<void> setFocus(std::optional<ComponentHandle> target);
    [[nodiscard]] Result<std::optional<ComponentHandle>> focus() const noexcept;
    [[nodiscard]] Result<std::optional<ComponentHandle>> moveFocus(FocusDirection direction);

    [[nodiscard]] Result<void> capturePointer(PointerId pointer, ComponentHandle target);
    [[nodiscard]] Result<void> releasePointer(PointerId pointer) noexcept;
    [[nodiscard]] Result<std::optional<ComponentHandle>> capturedPointer(PointerId pointer) const noexcept;

    // hit is the host's current hit-test result. Active capture overrides that target.
    [[nodiscard]] Result<DispatchResult> dispatchPointer(PointerEvent event, std::optional<ComponentHandle> hit = {});
    [[nodiscard]] Result<DispatchResult> dispatchKey(KeyEvent event);
    [[nodiscard]] Result<void> close() noexcept;

private:
    friend class Button;
    friend class TextInput;
    explicit InputDispatcher(std::shared_ptr<detail::InputData> data) noexcept;
    [[nodiscard]] Result<std::uint64_t> acquirePointer(PointerId pointer, ComponentHandle target);
    [[nodiscard]] Result<std::uint64_t> captureIdentity(PointerId pointer) const noexcept;
    [[nodiscard]] Result<std::uint64_t> focusIdentity(ComponentHandle target) const noexcept;
    [[nodiscard]] Result<void> validateVisibleTarget(ComponentHandle target) const noexcept;

    std::shared_ptr<detail::InputData> data_;
};

} // namespace graphics_ui::core
