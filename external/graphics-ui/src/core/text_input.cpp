#include <graphics-ui/core/controls.hpp>
#include <graphics-ui/core/text.hpp>

#include "utf8.hpp"

#include <algorithm>
#include <new>
#include <stdexcept>

namespace graphics_ui::core {
namespace {
constexpr std::size_t history_limit = 32;
constexpr EventReply consumed{true, true};

bool singleLine(std::string_view text) noexcept {
    return text.find_first_of(std::string_view("\0\r\n\v\f", 5)) == std::string_view::npos &&
        !text.contains("\xc2\x85") && !text.contains("\xe2\x80\xa8") && !text.contains("\xe2\x80\xa9");
}

std::optional<TextEdit> editingKey(KeyChord chord) noexcept {
    using Action = TextEditAction;
    if (chord.modifiers == KeyModifiers::control) {
        switch (chord.key) {
        case 'A': return TextEdit{Action::select_all};
        case 'C': return TextEdit{Action::copy};
        case 'X': return TextEdit{Action::cut};
        case 'V': return TextEdit{Action::paste};
        case 'Z': return TextEdit{Action::undo};
        case 'Y': return TextEdit{Action::redo};
        default: return {};
        }
    }
    if (chord == KeyChord{'Z', KeyModifiers::control | KeyModifiers::shift})
        return TextEdit{Action::redo};
    const bool extend = chord.modifiers == KeyModifiers::shift;
    if (!extend && chord.modifiers != KeyModifiers::none) return {};
    switch (chord.key) {
    case left_key: return TextEdit{Action::previous, {}, extend};
    case right_key: return TextEdit{Action::next, {}, extend};
    case home_key: return TextEdit{Action::start, {}, extend};
    case end_key: return TextEdit{Action::end, {}, extend};
    case backspace_key: if (!extend) return TextEdit{Action::erase_previous}; break;
    case delete_key: if (!extend) return TextEdit{Action::erase_next}; break;
    }
    return {};
}
} // namespace

Result<EventReply> TextInput::handle(const RoutedEvent& event) {
    if (!std::holds_alternative<PointerEvent>(event.input)) return EventReply{};
    const auto press = button_.handle(event);
    if (!press) return std::unexpected(press.error());
    return press->reply;
}

Result<EventReply> TextInput::handle(const RoutedEvent& event, const TextEditCallbacks& callbacks) {
    const auto* key = std::get_if<KeyEvent>(&event.input);
    if (!key) return handle(event);
    if (!button_.accepts(event)) return EventReply{};
    const auto request = editingKey(key->chord);
    if (!request) return EventReply{};
    const auto session = beginInput();
    if (!session) return std::unexpected(session.error());
    if (!*session) return EventReply{};
    if (key->action == KeyAction::up || (key->repeat && (key->chord.modifiers == KeyModifiers::control ||
        key->chord.modifiers == (KeyModifiers::control | KeyModifiers::shift)))) return consumed;
    if (const auto edited = edit(**session, *request, callbacks); !edited) return std::unexpected(edited.error());
    return consumed;
}

Result<std::optional<TextInput::EditSession>> TextInput::beginInput() const noexcept {
    if (!button_.enabled()) return std::nullopt;
    const auto focus = button_.focusIdentity();
    if (!focus) return std::unexpected(focus.error());
    if (!*focus) return std::nullopt;
    return std::optional{EditSession(*focus)};
}

Result<void> TextInput::validateSession(EditSession session) const noexcept {
    const auto current = beginInput();
    if (!current) return std::unexpected(current.error());
    if (*current != session) return std::unexpected(ErrorCode::stale_handle);
    if (editing_) return std::unexpected(ErrorCode::busy);
    return {};
}

Result<std::string> TextInput::commit(EditSession session, std::string_view utf8) const {
    if (const auto valid = validateSession(session); !valid) return std::unexpected(valid.error());
    if (utf8.size() > max_bytes_) return std::unexpected(ErrorCode::limit_exceeded);
    if (!singleLine(utf8) || !detail::validUtf8(utf8)) return std::unexpected(ErrorCode::invalid_argument);
    try {
        return std::string(utf8);
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    } catch (const std::length_error&) {
        return std::unexpected(ErrorCode::limit_exceeded);
    }
}

Result<TextInput::EditState> TextInput::analyze(std::string text, std::size_t target) const {
    if (!singleLine(text)) return std::unexpected(ErrorCode::invalid_argument);
    auto boundaries = textBoundaries(text);
    if (!boundaries) return std::unexpected(boundaries.error());
    // A splice can join the following scalars to the inserted text. Keep the caret on the far edge of the new cluster.
    const auto caret = *std::ranges::lower_bound(boundaries->graphemes, target);
    return EditState{std::move(text), std::move(boundaries->graphemes), {caret, caret}};
}

Result<void> TextInput::setText(std::string_view utf8) try {
    if (editing_) return std::unexpected(ErrorCode::busy);
    if (const auto available = button_.focusIdentity(); !available) return std::unexpected(available.error());
    if (utf8.size() > max_bytes_) return std::unexpected(ErrorCode::limit_exceeded);
    auto next = analyze(std::string(utf8), utf8.size());
    if (!next) return std::unexpected(next.error());
    current_ = std::move(*next);
    undo_.clear();
    redo_.clear();
    return {};
} catch (const std::bad_alloc&) {
    return std::unexpected(ErrorCode::out_of_memory);
} catch (const std::length_error&) {
    return std::unexpected(ErrorCode::limit_exceeded);
}

Result<bool> TextInput::setSelection(EditSession session, TextSelection selection) noexcept {
    if (const auto valid = validateSession(session); !valid) return std::unexpected(valid.error());
    if (!isBoundary(selection.anchor) || !isBoundary(selection.caret)) return std::unexpected(ErrorCode::invalid_argument);
    const bool changed = current_.selection != selection;
    current_.selection = selection;
    return changed;
}

bool TextInput::isBoundary(std::size_t offset) const noexcept {
    return offset == 0 || std::ranges::binary_search(current_.graphemes, offset);
}

void TextInput::remember(std::vector<EditState>& history) noexcept {
    if (history.size() == history_limit) history.erase(history.begin());
    history.push_back(std::move(current_));
}

Result<bool> TextInput::restore(std::vector<EditState>& from, std::vector<EditState>& to,
                              const TextEditCallbacks& callbacks) {
    if (from.empty()) return false;
    to.reserve(history_limit);
    if (callbacks.publish) {
        if (const auto result = callbacks.publish(from.back().text); !result) return std::unexpected(result.error());
    }
    remember(to);
    current_ = std::move(from.back());
    from.pop_back();
    return true;
}

Result<bool> TextInput::edit(EditSession session, TextEdit request, const TextEditCallbacks& callbacks) {
    if (const auto valid = validateSession(session); !valid) return std::unexpected(valid.error());
    return applyEdit(request, callbacks);
}

Result<bool> TextInput::replaceValue(std::string_view utf8, const TextEditCallbacks& callbacks) {
    if (const auto target = button_.input_.validateVisibleTarget(button_.target_); !target)
        return std::unexpected(target.error());
    if (!button_.enabled()) return std::unexpected(ErrorCode::invalid_argument);
    if (editing_) return std::unexpected(ErrorCode::busy);
    return applyEdit({TextEditAction::replace_all, utf8}, callbacks);
}

Result<bool> TextInput::applyEdit(TextEdit request, const TextEditCallbacks& callbacks) try {
    editing_ = true;
    struct Editing { bool& active; ~Editing() { active = false; } } editing{editing_};

    using Action = TextEditAction;
    const auto selection = current_.selection;
    const auto [first_selected, past_selected] = std::minmax(selection.anchor, selection.caret);
    auto first = first_selected, past = past_selected;
    if (request.replacement) {
        const auto range = *request.replacement;
        if (request.action != Action::replace_selection || !isBoundary(range.anchor) || !isBoundary(range.caret))
            return std::unexpected(ErrorCode::invalid_argument);
        first = std::min(range.anchor, range.caret);
        past = std::max(range.anchor, range.caret);
    }
    const auto old_caret = selection.caret;
    const auto length = current_.text.size();
    const auto previous = [&] {
        const auto found = std::ranges::lower_bound(current_.graphemes, old_caret);
        return found == current_.graphemes.begin() ? std::size_t{0} : *std::prev(found);
    };
    const auto next = [&] {
        const auto found = std::ranges::upper_bound(current_.graphemes, old_caret);
        return found == current_.graphemes.end() ? length : *found;
    };
    const auto select = [&](TextSelection target) {
        const auto changed = current_.selection != target;
        current_.selection = target;
        return changed;
    };
    const auto move = [&](std::size_t target) { return select({request.extend ? selection.anchor : target, target}); };
    const auto copy = [&]() -> Result<void> {
        if (!callbacks.write_clipboard) return std::unexpected(ErrorCode::invalid_argument);
        return callbacks.write_clipboard(std::string_view(current_.text).substr(first, past - first));
    };

    std::string pasted;
    std::string_view insertion;
    switch (request.action) {
    case Action::previous: return move(first != past && !request.extend ? first : previous());
    case Action::next: return move(first != past && !request.extend ? past : next());
    case Action::start: return move(0);
    case Action::end: return move(length);
    case Action::select_all: return select({0, length});
    case Action::undo: return restore(undo_, redo_, callbacks);
    case Action::redo: return restore(redo_, undo_, callbacks);
    case Action::copy:
        if (first == past) return false;
        if (const auto copied = copy(); !copied) return std::unexpected(copied.error());
        return false;
    case Action::cut: if (first == past) return false; break;
    case Action::paste: {
        if (!callbacks.read_clipboard) return std::unexpected(ErrorCode::invalid_argument);
        auto source = callbacks.read_clipboard();
        if (!source) return std::unexpected(source.error());
        if (source->empty()) return false;
        pasted = std::move(*source);
        insertion = pasted;
        break;
    }
    case Action::erase_previous: if (first == past) first = previous(); break;
    case Action::erase_next: if (first == past) past = next(); break;
    case Action::replace_all: first = 0; past = length; [[fallthrough]];
    case Action::replace_selection: insertion = request.utf8; break;
    default: return std::unexpected(ErrorCode::invalid_argument);
    }

    const auto removed = past - first;
    if (insertion.size() > max_bytes_ - (length - removed)) return std::unexpected(ErrorCode::limit_exceeded);
    const auto desired = first + insertion.size();
    if (std::string_view(current_.text).substr(first, removed) == insertion)
        return select({desired, desired});

    auto text = current_.text;
    text.replace(first, removed, insertion);
    auto updated = analyze(std::move(text), desired);
    if (!updated) return std::unexpected(updated.error());
    undo_.reserve(history_limit);
    if (request.action == Action::cut) {
        if (const auto copied = copy(); !copied) return std::unexpected(copied.error());
    }
    if (callbacks.publish) {
        if (const auto result = callbacks.publish(updated->text); !result) return std::unexpected(result.error());
    }
    // After the host accepts the value, the remaining moves use already allocated storage.
    remember(undo_);
    redo_.clear();
    current_ = std::move(*updated);
    return true;
} catch (const std::bad_alloc&) {
    return std::unexpected(ErrorCode::out_of_memory);
} catch (const std::length_error&) {
    return std::unexpected(ErrorCode::limit_exceeded);
} catch (...) {
    return std::unexpected(ErrorCode::evaluation_failed);
}

} // namespace graphics_ui::core
