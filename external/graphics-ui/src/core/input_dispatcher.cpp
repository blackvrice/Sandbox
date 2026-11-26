#include <graphics-ui/core/input_dispatcher.hpp>

#include "owner_id.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <new>
#include <thread>
#include <utility>

namespace graphics_ui::core::detail {
namespace {
bool validModifiers(KeyModifiers modifiers) noexcept {
    constexpr auto mask = KeyModifiers::shift | KeyModifiers::control | KeyModifiers::alt | KeyModifiers::meta;
    return (static_cast<unsigned>(modifiers) & ~static_cast<unsigned>(mask)) == 0;
}

bool validChord(KeyChord chord) noexcept {
    return chord.key != 0 && validModifiers(chord.modifiers);
}
} // namespace

struct InputData : std::enable_shared_from_this<InputData> {
    struct Lease {
        ComponentHandle handle;
        std::uint64_t visibility_revision;
    };
    struct Capture {
        Lease target;
        std::uint64_t identity;
    };
    struct Bindings {
        std::shared_ptr<ComponentTree::UnmountCallback> watch;
        std::shared_ptr<InputDispatcher::Handler> handler;
        std::map<KeyChord, std::shared_ptr<InputDispatcher::Shortcut>> shortcuts;
        bool focusable{};
    };
    using Registrations = std::map<std::uint64_t, Bindings>;

    explicit InputData(ComponentTree& component_tree) : tree(component_tree.state_) {}

    static Result<void> checkTree(const ComponentTree& tree) noexcept { return tree.checkBindable(); }

    [[nodiscard]] bool onOwnerThread() const noexcept { return thread == std::this_thread::get_id(); }

    [[nodiscard]] std::optional<ErrorCode> access() const noexcept {
        if (!onOwnerThread()) return ErrorCode::wrong_thread;
        if (closed) return ErrorCode::closed;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<ComponentInfo> locate(ComponentHandle target) const noexcept {
        return ComponentTree::locate(tree, target);
    }

    [[nodiscard]] std::optional<ComponentInfo> root() const noexcept { return ComponentTree::inputRoot(tree); }
    [[nodiscard]] Result<ComponentSnapshot> snapshot() const { return ComponentTree::readSnapshot(tree); }

    [[nodiscard]] bool alive(const Lease& target) const noexcept {
        const auto current = locate(target.handle);
        return current && current->effectively_visible && current->visibility_revision == target.visibility_revision;
    }

    [[nodiscard]] static Lease lease(const ComponentInfo& info) noexcept {
        return {info.handle, info.visibility_revision};
    }

    [[nodiscard]] Bindings* find(ComponentHandle target) noexcept {
        // Callers first validate the handle against this tree. Generations are unique within it.
        const auto found = registrations->find(target.generation());
        return found == registrations->end() ? nullptr : &found->second;
    }

    [[nodiscard]] Result<Bindings*> ensure(ComponentHandle target) {
        if (auto* existing = find(target)) return existing;
        const auto watched = ComponentTree::watchUnmount(tree, target, [weak = weak_from_this()](ComponentHandle removed) noexcept {
            if (const auto data = weak.lock()) data->forget(removed);
        });
        if (!watched) return std::unexpected(watched.error());
        auto& entry = registrations->try_emplace(target.generation()).first->second;
        entry.watch = *watched;
        return &entry;
    }

    void forget(ComponentHandle removed) noexcept {
        if (closed) return;
        if (focused && focused->handle == removed) focused.reset();
        std::erase_if(captures, [removed](const auto& current) { return current.second.target.handle == removed; });
        // A capture's destructor can call the dispatcher again. Unlink before releasing it.
        const auto retired = registrations->extract(removed.generation());
    }

    void prune(ComponentHandle target, const Bindings& entry) noexcept {
        if (!entry.handler && entry.shortcuts.empty() && !entry.focusable)
            registrations->erase(target.generation());
    }

    [[nodiscard]] bool focusable(ComponentHandle target) noexcept {
        const auto* entry = find(target);
        return entry && entry->focusable;
    }

    [[nodiscard]] std::optional<Lease> currentFocus() const noexcept {
        if (focused && alive(*focused)) return focused;
        return std::nullopt;
    }

    [[nodiscard]] Result<void> focusTarget(const ComponentInfo& info) noexcept {
        if (const auto current = currentFocus(); current && current->handle == info.handle) return {};
        const auto identity = allocateOwner();
        if (!identity) return std::unexpected(ErrorCode::limit_exceeded);
        focused = lease(info);
        focus_identity = identity;
        return {};
    }

    [[nodiscard]] std::optional<Capture> capture(PointerId pointer) const noexcept {
        const auto entry = captures.find(pointer);
        if (entry != captures.end() && alive(entry->second.target)) return entry->second;
        return std::nullopt;
    }

    [[nodiscard]] std::vector<ComponentHandle> path(ComponentHandle target) const {
        std::vector<ComponentHandle> up(0);
        for (auto current = locate(target); current; current = current->parent ? locate(*current->parent) : std::nullopt)
            up.push_back(current->handle);
        return up;
    }

    struct Dispatching {
        explicit Dispatching(InputData& owner) noexcept : data(owner) { data.dispatching = true; }
        ~Dispatching() { data.dispatching = false; }
        InputData& data;
    };

    [[nodiscard]] EventReply route(const Lease& target, const InputEvent& input, const std::vector<ComponentHandle>& up,
                                   std::optional<ComponentHandle> hit = {}, std::uint64_t capture_identity = 0) noexcept {
        EventReply result;
        const auto visit = [&](ComponentHandle receiver, EventPhase phase) {
            if (closed || !alive(target)) return false;
            const auto* binding = find(receiver);
            if (!binding || !binding->handler) return true;
            const auto handler = binding->handler;
            const auto reply = (*handler)({input, phase, target.handle, receiver, hit, capture_identity});
            result.handled |= reply.handled;
            result.stop_propagation |= reply.stop_propagation;
            return !result.stop_propagation;
        };
        for (auto position = up.size(); position > 1; --position)
            if (!visit(up[position - 1], EventPhase::capture)) return result;
        if (!visit(target.handle, EventPhase::target)) return result;
        for (std::size_t position = 1; position < up.size(); ++position)
            if (!visit(up[position], EventPhase::bubble)) break;
        return result;
    }

    [[nodiscard]] bool invokeShortcut(ComponentHandle target, const KeyEvent& event) noexcept {
        const auto* binding = find(target);
        if (!binding) return false;
        const auto slot = binding->shortcuts.find(event.chord);
        if (slot == binding->shortcuts.end()) return false;
        const auto shortcut = slot->second;
        return (*shortcut)(event);
    }

    void shutdown() noexcept {
        if (closed) return;
        closed = true;
        focused.reset();
        captures.clear();
        // Mark closed before destructors of handler captures can reenter the dispatcher.
        registrations.reset();
    }

    const std::thread::id thread = std::this_thread::get_id();
    const std::weak_ptr<ComponentTree::State> tree;
    std::optional<Registrations> registrations{std::in_place};
    std::optional<Lease> focused;
    std::uint64_t focus_identity{};
    std::map<PointerId, Capture> captures;
    std::uint64_t last_capture{};
    bool dispatching{};
    bool closed{};
};

} // namespace graphics_ui::core::detail

namespace graphics_ui::core {

InputDispatcher::InputDispatcher(std::shared_ptr<detail::InputData> data) noexcept : data_(std::move(data)) {}

Result<std::unique_ptr<InputDispatcher>> InputDispatcher::create(ComponentTree& tree) {
    const auto bindable = detail::InputData::checkTree(tree);
    if (!bindable) return std::unexpected(bindable.error());
    try {
        return std::unique_ptr<InputDispatcher>(new InputDispatcher(std::make_shared<detail::InputData>(tree)));
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    }
}

InputDispatcher::~InputDispatcher() noexcept {
    if (!data_->onOwnerThread()) std::terminate();
    data_->shutdown();
}

Result<void> InputDispatcher::setHandler(ComponentHandle target, Handler handler) {
    const auto data = data_;
    if (const auto error = data->access()) return std::unexpected(*error);
    if (!data->locate(target)) return std::unexpected(ErrorCode::stale_handle);
    try {
        if (handler) {
            auto next = std::make_shared<Handler>(std::move(handler));
            const auto binding = data->ensure(target);
            if (!binding) return std::unexpected(binding.error());
            (*binding)->handler.swap(next);
        } else if (auto* binding = data->find(target)) {
            const auto retired = std::move(binding->handler);
            data->prune(target, *binding);
        }
        return {};
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    }
}

Result<void> InputDispatcher::setShortcut(ComponentHandle target, KeyChord chord, Shortcut shortcut) {
    const auto data = data_;
    if (const auto error = data->access()) return std::unexpected(*error);
    if (!detail::validChord(chord)) return std::unexpected(ErrorCode::invalid_argument);
    if (!data->locate(target)) return std::unexpected(ErrorCode::stale_handle);
    try {
        if (shortcut) {
            // Allocate the new callable and its map node before adding the component binding.
            decltype(detail::InputData::Bindings::shortcuts) staged;
            staged.emplace(chord, std::make_shared<Shortcut>(std::move(shortcut)));
            const auto binding = data->ensure(target);
            if (!binding) return std::unexpected(binding.error());
            auto& shortcuts = (*binding)->shortcuts;
            const auto retired = shortcuts.extract(chord);
            shortcuts.merge(staged);
        } else if (auto* binding = data->find(target)) {
            const auto retired = binding->shortcuts.extract(chord);
            data->prune(target, *binding);
        }
        return {};
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    }
}

Result<void> InputDispatcher::setFocusable(ComponentHandle target, bool focusable) {
    const auto data = data_;
    if (const auto error = data->access()) return std::unexpected(*error);
    if (!data->locate(target)) return std::unexpected(ErrorCode::stale_handle);
    try {
        if (focusable) {
            const auto binding = data->ensure(target);
            if (!binding) return std::unexpected(binding.error());
            (*binding)->focusable = true;
        } else if (auto* binding = data->find(target)) {
            binding->focusable = false;
            if (data->focused && data->focused->handle == target) data->focused.reset();
            data->prune(target, *binding);
        }
        return {};
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    }
}

Result<void> InputDispatcher::setFocus(std::optional<ComponentHandle> target) {
    if (const auto error = data_->access()) return std::unexpected(*error);
    if (!target) {
        data_->focused.reset();
        return {};
    }
    const auto info = data_->locate(*target);
    if (!info) return std::unexpected(ErrorCode::stale_handle);
    if (!info->effectively_visible || !data_->focusable(*target)) return std::unexpected(ErrorCode::invalid_argument);
    return data_->focusTarget(*info);
}

Result<std::optional<ComponentHandle>> InputDispatcher::focus() const noexcept {
    if (const auto error = data_->access()) return std::unexpected(*error);
    const auto current = data_->currentFocus();
    return current ? std::optional{current->handle} : std::nullopt;
}

Result<std::optional<ComponentHandle>> InputDispatcher::moveFocus(FocusDirection direction) {
    if (const auto error = data_->access()) return std::unexpected(*error);
    if (direction != FocusDirection::forward && direction != FocusDirection::backward)
        return std::unexpected(ErrorCode::invalid_argument);
    const auto snapshot = data_->snapshot();
    if (!snapshot && snapshot.error() != ErrorCode::closed) return std::unexpected(snapshot.error());
    const auto current = data_->currentFocus();
    if (snapshot && !snapshot->components.empty()) {
        const auto& components = snapshot->components;
        const auto size = components.size();
        const bool forwards = direction == FocusDirection::forward;
        std::size_t index = forwards ? size - 1 : 0;
        if (current) {
            const auto found = std::ranges::find(components, current->handle, &ComponentInfo::handle);
            if (found != components.end()) index = static_cast<std::size_t>(found - components.begin());
        }
        for (std::size_t visited = 0; visited < size; ++visited) {
            index = forwards ? (index + 1) % size : (index == 0 ? size - 1 : index - 1);
            const auto& candidate = components[index];
            if (!candidate.effectively_visible || !data_->focusable(candidate.handle)) continue;
            if (auto focused = data_->focusTarget(candidate); !focused) return std::unexpected(focused.error());
            return std::optional{candidate.handle};
        }
    }
    data_->focused.reset();
    return std::optional<ComponentHandle>{};
}

Result<void> InputDispatcher::capturePointer(PointerId pointer, ComponentHandle target) {
    if (auto capture = acquirePointer(pointer, target); !capture) return std::unexpected(capture.error());
    return {};
}

Result<std::uint64_t> InputDispatcher::acquirePointer(PointerId pointer, ComponentHandle target) {
    if (const auto error = data_->access()) return std::unexpected(*error);
    const auto info = data_->locate(target);
    if (!info) return std::unexpected(ErrorCode::stale_handle);
    if (!info->effectively_visible) return std::unexpected(ErrorCode::invalid_argument);
    if (data_->last_capture == std::numeric_limits<std::uint64_t>::max())
        return std::unexpected(ErrorCode::limit_exceeded);
    try {
        const auto identity = data_->last_capture + 1;
        data_->captures.insert_or_assign(pointer, detail::InputData::Capture{detail::InputData::lease(*info), identity});
        data_->last_capture = identity;
        std::erase_if(data_->captures, [&](const auto& entry) { return !data_->alive(entry.second.target); });
        return identity;
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    }
}

Result<void> InputDispatcher::releasePointer(PointerId pointer) noexcept {
    if (const auto error = data_->access()) return std::unexpected(*error);
    data_->captures.erase(pointer);
    return {};
}

Result<std::optional<ComponentHandle>> InputDispatcher::capturedPointer(PointerId pointer) const noexcept {
    if (const auto error = data_->access()) return std::unexpected(*error);
    const auto captor = data_->capture(pointer);
    return captor ? std::optional{captor->target.handle} : std::nullopt;
}

Result<std::uint64_t> InputDispatcher::captureIdentity(PointerId pointer) const noexcept {
    if (const auto error = data_->access()) return std::unexpected(*error);
    const auto captor = data_->capture(pointer);
    return captor ? captor->identity : 0;
}

Result<std::uint64_t> InputDispatcher::focusIdentity(ComponentHandle target) const noexcept {
    if (const auto error = data_->access()) return std::unexpected(*error);
    const auto focused = data_->currentFocus();
    return focused && focused->handle == target ? data_->focus_identity : 0;
}

Result<void> InputDispatcher::validateVisibleTarget(ComponentHandle target) const noexcept {
    if (const auto error = data_->access()) return std::unexpected(*error);
    const auto info = data_->locate(target);
    if (!info || !info->effectively_visible) return std::unexpected(ErrorCode::stale_handle);
    return {};
}

Result<DispatchResult> InputDispatcher::dispatchPointer(PointerEvent event, std::optional<ComponentHandle> hit) {
    const auto data = data_;
    if (const auto error = data->access()) return std::unexpected(*error);
    if (data->dispatching) return std::unexpected(ErrorCode::busy);
    if (event.action < PointerAction::down || event.action > PointerAction::cancel ||
        event.button < PointerButton::none || event.button > PointerButton::auxiliary2 ||
        !detail::validModifiers(event.modifiers) || !std::isfinite(event.x) || !std::isfinite(event.y))
        return std::unexpected(ErrorCode::invalid_argument);

    const auto capture = data->capture(event.pointer);
    auto target = capture ? std::optional{capture->target} : std::nullopt;
    std::optional<ComponentHandle> visible_hit;
    if (hit) {
        const auto info = data->locate(*hit);
        if (!info) return std::unexpected(ErrorCode::stale_handle);
        if (info->effectively_visible) {
            visible_hit = info->handle;
            if (!target) target = detail::InputData::lease(*info);
        }
    }
    const bool terminal = event.action == PointerAction::up || event.action == PointerAction::cancel;
    if (!target) {
        if (terminal) data->captures.erase(event.pointer);
        return DispatchResult{};
    }
    try {
        const auto path = data->path(target->handle);
        // Terminal events release the old lease before callbacks. A callback can capture a new one.
        if (terminal) data->captures.erase(event.pointer);
        const detail::InputData::Dispatching dispatching(*data);
        const auto reply = data->route(*target, InputEvent{event}, path, visible_hit, capture ? capture->identity : 0);
        return DispatchResult{target->handle, reply.handled};
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    }
}

Result<DispatchResult> InputDispatcher::dispatchKey(KeyEvent event) {
    const auto data = data_;
    if (const auto error = data->access()) return std::unexpected(*error);
    if (data->dispatching) return std::unexpected(ErrorCode::busy);
    if (!detail::validChord(event.chord) || (event.action != KeyAction::down && event.action != KeyAction::up) ||
        (event.action == KeyAction::up && event.repeat))
        return std::unexpected(ErrorCode::invalid_argument);

    const bool shortcut_eligible = event.action == KeyAction::down && !event.repeat;
    const detail::InputData::Dispatching dispatching(*data);
    const auto target = data->currentFocus();
    if (!target) {
        const auto root = data->root();
        if (shortcut_eligible && root && root->effectively_visible && data->invokeShortcut(root->handle, event))
            return DispatchResult{root->handle, true};
        return DispatchResult{};
    }
    try {
        const auto path = data->path(target->handle);
        const auto reply = data->route(*target, InputEvent{event}, path);
        if (!reply.handled && !reply.stop_propagation && shortcut_eligible) {
            for (const auto receiver : path) {
                if (data->closed || !data->alive(*target)) break;
                if (data->invokeShortcut(receiver, event)) return DispatchResult{target->handle, true};
            }
        }
        return DispatchResult{target->handle, reply.handled};
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    }
}

Result<void> InputDispatcher::close() noexcept {
    const auto data = data_;
    if (!data->onOwnerThread()) return std::unexpected(ErrorCode::wrong_thread);
    data->shutdown();
    return {};
}

} // namespace graphics_ui::core
