#pragma once

#include <graphics-ui/core/input_dispatcher.hpp>
#include <graphics-ui/core/style.hpp>

#include <functional>
#include <limits>

namespace graphics_ui::core {

[[nodiscard]] Result<DrawText> makeText(LayoutRect bounds, std::string_view utf8, const StyleProperties& style);

inline constexpr KeyCode backspace_key = 8;
inline constexpr KeyCode enter_key = 13;
inline constexpr KeyCode escape_key = 27;
inline constexpr KeyCode space_key = 32;
inline constexpr KeyCode end_key = 35;
inline constexpr KeyCode home_key = 36;
inline constexpr KeyCode left_key = 37;
inline constexpr KeyCode up_key = 38;
inline constexpr KeyCode right_key = 39;
inline constexpr KeyCode down_key = 40;
inline constexpr KeyCode delete_key = 46;

struct ControlVisualState {
    bool enabled{}, hovered{}, focused{}, pressed{};
    bool operator==(const ControlVisualState&) const = default;
};

struct ButtonInteraction {
    EventReply reply;
    bool activated{};
};

class Button {
public:
    // The dispatcher outlives the button. The host owns the handler binding and mounts the target.
    Button(InputDispatcher& input, ComponentHandle target) noexcept : input_(input), target_(target) {}
    Button(const Button&) = delete;
    Button& operator=(const Button&) = delete;

    [[nodiscard]] Result<void> setEnabled(bool enabled);
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    [[nodiscard]] Result<ButtonInteraction> handle(const RoutedEvent& event);
    // hit is the current host hit-test target, independent of capture.
    [[nodiscard]] Result<ControlVisualState> visualState(std::optional<ComponentHandle> hit = {}) const noexcept;

private:
    friend class Slider;
    friend class ComboBox;
    friend class TextInput;
    [[nodiscard]] bool accepts(const RoutedEvent& event) const noexcept;
    [[nodiscard]] Result<std::uint64_t> focusIdentity() const noexcept { return input_.focusIdentity(target_); }
    struct Press { PointerId pointer; std::uint64_t capture; };
    InputDispatcher& input_;
    ComponentHandle target_;
    std::optional<Press> pressed_;
    bool enabled_{};
};

struct ToggleInteraction {
    EventReply reply;
    // The host validates and publishes this proposal through its state model.
    std::optional<bool> proposed;
};

class Toggle {
public:
    Toggle(InputDispatcher& input, ComponentHandle target) noexcept : button_(input, target) {}
    [[nodiscard]] Result<void> setEnabled(bool enabled) { return button_.setEnabled(enabled); }
    [[nodiscard]] Result<ToggleInteraction> handle(const RoutedEvent& event, bool checked);
    [[nodiscard]] Result<ControlVisualState> visualState(std::optional<ComponentHandle> hit = {}) const noexcept {
        return button_.visualState(hit);
    }

private:
    Button button_;
};

struct SliderRange {
    std::int32_t minimum{}, maximum = 100, step = 1;
};

struct SliderInteraction {
    EventReply reply;
    std::optional<std::int32_t> proposed;
};

class Slider {
public:
    Slider(InputDispatcher& input, ComponentHandle target) noexcept : button_(input, target) {}
    [[nodiscard]] Result<void> setEnabled(bool enabled) { return button_.setEnabled(enabled); }
    [[nodiscard]] Result<SliderInteraction> handle(const RoutedEvent& event, LayoutRect track,
        SliderRange range, std::int32_t current);
    [[nodiscard]] Result<ControlVisualState> visualState(std::optional<ComponentHandle> hit = {}) const noexcept {
        return button_.visualState(hit);
    }

private:
    Button button_;
};

struct ComboInteraction {
    EventReply reply;
    std::optional<std::size_t> proposed;
};

class ComboBox {
public:
    ComboBox(InputDispatcher& input, ComponentHandle target) noexcept : button_(input, target) {}
    [[nodiscard]] Result<void> setEnabled(bool enabled);
    // The host uses item_hit only for a pointer release over a row in its open list.
    [[nodiscard]] Result<ComboInteraction> handle(const RoutedEvent& event, std::size_t item_count,
        std::optional<std::size_t> selected, std::optional<std::size_t> item_hit = {});
    [[nodiscard]] Result<std::optional<std::size_t>> highlighted() const noexcept;
    void dismiss() noexcept { highlight_.reset(); }
    [[nodiscard]] Result<ControlVisualState> visualState(std::optional<ComponentHandle> hit = {}) const noexcept {
        return button_.visualState(hit);
    }

private:
    Button button_;
    std::optional<std::size_t> highlight_;
    std::uint64_t open_focus_{};
};

struct TextSelection {
    std::size_t anchor{}, caret{};
    bool operator==(const TextSelection&) const = default;
};

enum class TextEditAction {
    previous, next, start, end, select_all, replace_selection, replace_all,
    erase_previous, erase_next, copy, cut, paste, undo, redo,
};

struct TextEdit {
    TextEditAction action;
    std::string_view utf8{};
    bool extend{};
    // An input method can replace an explicit grapheme range without moving the current selection.
    // Applies to replace_selection only and uses the original UTF-8 byte offsets.
    std::optional<TextSelection> replacement{};
};

struct TextEditCallbacks {
    // A failed publication must leave the host value intact. Arguments are borrowed during the call.
    std::function<Result<void>(std::string_view)> publish;
    std::function<Result<std::string>()> read_clipboard;
    std::function<Result<void>(std::string_view)> write_clipboard;
};

class TextInput {
public:
    class EditSession {
    public:
        bool operator==(const EditSession&) const = default;

    private:
        friend class TextInput;
        explicit EditSession(std::uint64_t focus) noexcept : focus_(focus) {}
        std::uint64_t focus_{};
    };

    TextInput(InputDispatcher& input, ComponentHandle target,
        std::size_t max_bytes = std::numeric_limits<std::size_t>::max()) noexcept
        : button_(input, target), max_bytes_(max_bytes) {}
    [[nodiscard]] Result<void> setEnabled(bool enabled) { return button_.setEnabled(enabled); }
    [[nodiscard]] Result<EventReply> handle(const RoutedEvent& event);
    // Routes standard editing keys through the current focus session and host callbacks.
    [[nodiscard]] Result<EventReply> handle(const RoutedEvent& event, const TextEditCallbacks& callbacks);
    // A successful result contains a session only while enabled and focused.
    [[nodiscard]] Result<std::optional<EditSession>> beginInput() const noexcept;
    // Validate and copy a complete single-line UTF-8 value from the host.
    [[nodiscard]] Result<std::string> commit(EditSession session, std::string_view utf8) const;
    // Synchronize an external value, putting the caret at the end and discarding edit history.
    [[nodiscard]] Result<void> setText(std::string_view utf8);
    [[nodiscard]] std::string_view text() const & noexcept { return current_.text; }
    std::string_view text() const && = delete;
    [[nodiscard]] TextSelection selection() const noexcept { return current_.selection; }
    // Both offsets are UTF-8 byte offsets at extended grapheme boundaries.
    [[nodiscard]] Result<bool> setSelection(EditSession session, TextSelection selection) noexcept;
    // Reports any change to the value or selection, including undo and redo.
    [[nodiscard]] Result<bool> edit(EditSession session, TextEdit edit, const TextEditCallbacks& callbacks = {});
    // Host actions can replace the value of a mounted, visible and enabled input without moving focus.
    // Uses the same publication and undo history as a keyboard edit.
    [[nodiscard]] Result<bool> replaceValue(std::string_view utf8, const TextEditCallbacks& callbacks = {});
    [[nodiscard]] Result<ControlVisualState> visualState(std::optional<ComponentHandle> hit = {}) const noexcept {
        return button_.visualState(hit);
    }

private:
    struct EditState {
        std::string text;
        std::vector<std::size_t> graphemes;
        TextSelection selection;
    };

    [[nodiscard]] Result<void> validateSession(EditSession session) const noexcept;
    [[nodiscard]] Result<bool> applyEdit(TextEdit request, const TextEditCallbacks& callbacks);
    [[nodiscard]] bool isBoundary(std::size_t offset) const noexcept;
    [[nodiscard]] Result<EditState> analyze(std::string text, std::size_t target) const;
    [[nodiscard]] Result<bool> restore(std::vector<EditState>& from, std::vector<EditState>& to,
        const TextEditCallbacks& callbacks);
    void remember(std::vector<EditState>& history) noexcept;

    Button button_;
    std::size_t max_bytes_;
    EditState current_;
    std::vector<EditState> undo_, redo_;
    bool editing_{};
};

} // namespace graphics_ui::core
