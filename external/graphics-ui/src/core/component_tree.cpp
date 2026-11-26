#include <graphics-ui/core/component_tree.hpp>

#include "owner_id.hpp"

#include <exception>
#include <list>
#include <map>
#include <new>
#include <thread>
#include <utility>

namespace graphics_ui::core {
using detail::serial_limit;

struct ComponentTree::State {
    using Slot = std::pair<std::uint64_t, ComponentKey>;

    struct Tree {
        explicit Tree(std::pmr::memory_resource& resource)
            // An empty sized vector uses the throwing constructor even in MSVC Debug,
            // where allocating the iterator proxy can fail through this resource.
            : components(std::size_t{0}, &resource), by_slot(&resource), by_generation(&resource) {}

        void swap(Tree& other) noexcept {
            components.swap(other.components);
            by_slot.swap(other.by_slot);
            by_generation.swap(other.by_generation);
        }

        std::pmr::vector<ComponentInfo> components;
        std::pmr::map<Slot, std::size_t> by_slot;
        std::pmr::map<std::uint64_t, std::size_t> by_generation;
    };

    struct Notification {
        ComponentHandle handle;
        UnmountCallback callback;
        std::weak_ptr<UnmountCallback> scoped;
    };
    using Notifications = std::pmr::list<Notification>;

    State(std::uint64_t owner_id, std::pmr::memory_resource& allocator)
        : owner(owner_id), resource(allocator), tree(std::in_place, allocator), listeners(&allocator), pending(&allocator) {}

    [[nodiscard]] bool onOwnerThread() const noexcept { return thread == std::this_thread::get_id(); }

    [[nodiscard]] const ComponentInfo* find(ComponentHandle handle) const noexcept {
        if (!tree || handle.owner_ != owner) return nullptr;
        const auto found = tree->by_generation.find(handle.generation());
        return found == tree->by_generation.end() ? nullptr : &tree->components[found->second];
    }

    [[nodiscard]] Result<std::uint64_t> publish(std::span<const ComponentSpec> input) {
        auto& current = *tree;
        // Reserve a distinct final revision for close(), which cannot fail or allocate.
        if (revision == serial_limit - 1 || input.size() > current.components.max_size())
            return std::unexpected(ErrorCode::limit_exceeded);

        auto next_generation = generation;
        Tree next(resource);
        next.components.reserve(input.size());
        for (std::size_t index = 0; index < input.size(); ++index) {
            const auto& spec = input[index];
            if (spec.type == 0 || spec.parent_index.has_value() != (index != 0) ||
                (spec.parent_index && *spec.parent_index >= index))
                return std::unexpected(ErrorCode::invalid_argument);

            std::optional<ComponentHandle> parent;
            bool effectively_visible = spec.visible;
            if (spec.parent_index) {
                const auto& parent_info = next.components[*spec.parent_index];
                parent = parent_info.handle;
                effectively_visible = effectively_visible && parent_info.effectively_visible;
            }

            const Slot slot{parent ? parent->generation() : 0, spec.key};
            if (!next.by_slot.emplace(slot, index).second)
                return std::unexpected(ErrorCode::invalid_argument);

            ComponentHandle handle;
            auto visibility_revision = revision + 1;
            const auto previous = current.by_slot.find(slot);
            if (previous != current.by_slot.end() && current.components[previous->second].type == spec.type) {
                const auto& old = current.components[previous->second];
                handle = old.handle;
                if (old.effectively_visible == effectively_visible) visibility_revision = old.visibility_revision;
            } else {
                if (next_generation == serial_limit) return std::unexpected(ErrorCode::limit_exceeded);
                handle = ComponentHandle(owner, ++next_generation);
            }
            next.components.push_back({handle, parent, spec.type, spec.key, spec.visible, effectively_visible, visibility_revision});
            next.by_generation.emplace(handle.generation(), index);
        }

        // Queue all removed generations before invoking any user callback. Reverse input
        // order guarantees descendants are notified before their removed ancestors.
        for (auto previous = current.components.rbegin(); previous != current.components.rend(); ++previous) {
            const auto removed = previous->handle.generation();
            if (!next.by_generation.contains(removed)) retire(removed);
        }
        current.swap(next);
        generation = next_generation;
        return ++revision;
    }

    void retire(std::uint64_t removed) noexcept {
        const auto found = listeners.find(removed);
        if (found == listeners.end()) return;
        pending.splice(pending.end(), found->second);
        listeners.erase(found);
    }

    void addListener(Notification notification) {
        const auto target_generation = notification.handle.generation();
        Notifications staged(&resource);
        staged.push_back(std::move(notification));
        auto& registered = listeners.try_emplace(target_generation).first->second;
        if (!staged.front().callback)
            registered.remove_if([](const Notification& item) { return !item.callback && item.scoped.expired(); });
        registered.splice(registered.end(), staged);
    }

    void drain() noexcept {
        // A nested commit publishes its own revision and appends notifications here.
        // Keep the running callback alive even if it deletes its owner.
        if (draining) return;
        draining = true;
        while (!pending.empty()) {
            auto notification = std::move(pending.front());
            pending.pop_front();
            if (notification.callback) notification.callback(notification.handle);
            else if (const auto scoped = notification.scoped.lock()) (*scoped)(notification.handle);
        }
        draining = false;
    }

    void close() noexcept {
        if (!tree) return;
        ++revision;
        for (auto previous = tree->components.rbegin(); previous != tree->components.rend(); ++previous)
            retire(previous->handle.generation());
        tree.reset();
        drain();
    }

    const std::uint64_t owner;
    const std::thread::id thread = std::this_thread::get_id();
    std::pmr::memory_resource& resource;
    std::optional<Tree> tree;
    std::pmr::map<std::uint64_t, Notifications> listeners;
    Notifications pending;
    std::uint64_t generation{};
    std::uint64_t revision{};
    bool draining{};
};

ComponentTree::ComponentTree(std::shared_ptr<State> state) noexcept : state_(std::move(state)) {}

Result<std::unique_ptr<ComponentTree>> ComponentTree::create(std::pmr::memory_resource& resource) {
    const auto owner = detail::allocateOwner();
    if (owner == 0) return std::unexpected(ErrorCode::limit_exceeded);
    try {
        auto state = std::allocate_shared<State>(std::pmr::polymorphic_allocator<State>(&resource), owner, resource);
        return std::unique_ptr<ComponentTree>(new ComponentTree(std::move(state)));
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    }
}

ComponentTree::~ComponentTree() noexcept {
    if (!state_->onOwnerThread()) std::terminate();
    state_->close();
}

Result<std::uint64_t> ComponentTree::commit(std::span<const ComponentSpec> components) {
    const auto state = state_;
    if (!state->onOwnerThread()) return std::unexpected(ErrorCode::wrong_thread);
    if (!state->tree) return std::unexpected(ErrorCode::closed);
    try {
        const auto revision = state->publish(components);
        if (revision) state->drain();
        return revision;
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    }
}

Result<ComponentSnapshot> ComponentTree::snapshot() const {
    return readSnapshot(state_);
}

Result<ComponentSnapshot> ComponentTree::readSnapshot(const std::weak_ptr<State>& reference) {
    const auto state = reference.lock();
    if (!state) return std::unexpected(ErrorCode::closed);
    if (!state->onOwnerThread()) return std::unexpected(ErrorCode::wrong_thread);
    try {
        if (!state->tree) return ComponentSnapshot{state->revision, {}};
        const auto& components = state->tree->components;
        return ComponentSnapshot{state->revision, {components.begin(), components.end()}};
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    }
}

Result<ComponentInfo> ComponentTree::resolve(ComponentHandle handle) const noexcept {
    if (!state_->onOwnerThread()) return std::unexpected(ErrorCode::wrong_thread);
    const auto* component = state_->find(handle);
    if (!component) return std::unexpected(ErrorCode::stale_handle);
    return *component;
}

Result<void> ComponentTree::onUnmount(ComponentHandle handle, UnmountCallback callback) {
    const auto state = state_;
    if (!state->onOwnerThread()) return std::unexpected(ErrorCode::wrong_thread);
    if (!state->tree) return std::unexpected(ErrorCode::closed);
    if (!callback) return std::unexpected(ErrorCode::invalid_argument);
    if (!state->find(handle)) return std::unexpected(ErrorCode::stale_handle);
    try {
        state->addListener({handle, std::move(callback), {}});
        return {};
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    }
}

bool ComponentTree::isMounted(const std::weak_ptr<State>& reference, ComponentHandle handle) noexcept {
    const auto state = reference.lock();
    return state && state->onOwnerThread() && state->find(handle);
}

std::optional<ComponentInfo> ComponentTree::locate(const std::weak_ptr<State>& reference, ComponentHandle handle) noexcept {
    const auto state = reference.lock();
    if (state && state->onOwnerThread()) {
        if (const auto* found = state->find(handle)) return *found;
    }
    return std::nullopt;
}

std::optional<ComponentInfo> ComponentTree::inputRoot(const std::weak_ptr<State>& reference) noexcept {
    const auto state = reference.lock();
    if (!state || !state->onOwnerThread() || !state->tree || state->tree->components.empty()) return std::nullopt;
    return state->tree->components.front();
}

Result<void> ComponentTree::checkBindable() const noexcept {
    if (!state_->onOwnerThread()) return std::unexpected(ErrorCode::wrong_thread);
    if (!state_->tree) return std::unexpected(ErrorCode::closed);
    return {};
}

Result<std::shared_ptr<ComponentTree::UnmountCallback>> ComponentTree::watchUnmount(
    const std::weak_ptr<State>& reference, ComponentHandle handle, UnmountCallback callback) {
    const auto state = reference.lock();
    if (!state || !state->tree || !state->find(handle)) return std::unexpected(ErrorCode::stale_handle);
    try {
        auto scoped = std::make_shared<UnmountCallback>(std::move(callback));
        state->addListener({handle, {}, scoped});
        return scoped;
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    }
}

Result<void> ComponentTree::close() noexcept {
    const auto state = state_;
    if (!state->onOwnerThread()) return std::unexpected(ErrorCode::wrong_thread);
    state->close();
    return {};
}

} // namespace graphics_ui::core
