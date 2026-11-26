#pragma once

#include <graphics-ui/core/core.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace graphics_ui::core {

namespace detail { struct InputData; struct LayoutPass; struct StateData; }

using ComponentType = std::uint64_t;
using ComponentKey = std::uint64_t;

class ComponentTree;

class ComponentHandle {
public:
    ComponentHandle() = default;
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    bool operator==(const ComponentHandle&) const = default;

private:
    friend class ComponentTree;
    friend class StyleEngine;
    friend struct detail::LayoutPass;
    ComponentHandle(std::uint64_t owner, std::uint64_t generation) noexcept
        : owner_(owner), generation_(generation) {}

    std::uint64_t owner_{};
    std::uint64_t generation_{};
};

struct ComponentSpec {
    // A nonempty tree starts with one root. Every other parent is an earlier index.
    std::optional<std::size_t> parent_index;
    ComponentType type{};
    ComponentKey key{};
    bool visible = true;
};

struct ComponentInfo {
    ComponentHandle handle;
    std::optional<ComponentHandle> parent;
    ComponentType type{};
    ComponentKey key{};
    bool visible{};
    bool effectively_visible{};
    // The revision at mount or the last effective visibility transition.
    std::uint64_t visibility_revision{};
};

struct ComponentSnapshot {
    std::uint64_t revision{};
    std::vector<ComponentInfo> components;
};

class ComponentTree {
public:
    using UnmountCallback = std::function<void(ComponentHandle)>;

    // The creating thread owns every tree operation and destruction. The borrowed resource
    // must outlive the tree and any active call, including callbacks that destroy the tree.
    [[nodiscard]] static Result<std::unique_ptr<ComponentTree>> create(
        std::pmr::memory_resource& resource = *std::pmr::new_delete_resource());

    ComponentTree(const ComponentTree&) = delete;
    ComponentTree& operator=(const ComponentTree&) = delete;
    ~ComponentTree() noexcept;

    // An empty declaration unmounts the tree. Successful commits return their own revision.
    [[nodiscard]] Result<std::uint64_t> commit(std::span<const ComponentSpec> components);
    [[nodiscard]] Result<ComponentSnapshot> snapshot() const;
    [[nodiscard]] Result<ComponentInfo> resolve(ComponentHandle handle) const noexcept;

    // Runs once after unmount publication. The callback must not throw.
    [[nodiscard]] Result<void> onUnmount(ComponentHandle handle, UnmountCallback callback);
    [[nodiscard]] Result<void> close() noexcept;

private:
    friend struct detail::InputData;
    friend struct detail::StateData;
    struct State;
    explicit ComponentTree(std::shared_ptr<State> state) noexcept;
    [[nodiscard]] Result<void> checkBindable() const noexcept;
    static std::optional<ComponentInfo> locate(const std::weak_ptr<State>& state, ComponentHandle handle) noexcept;
    static std::optional<ComponentInfo> inputRoot(const std::weak_ptr<State>& state) noexcept;
    static Result<ComponentSnapshot> readSnapshot(const std::weak_ptr<State>& state);
    static bool isMounted(const std::weak_ptr<State>& state, ComponentHandle handle) noexcept;
    static Result<std::shared_ptr<UnmountCallback>> watchUnmount(
        const std::weak_ptr<State>& state, ComponentHandle handle, UnmountCallback callback);

    std::shared_ptr<State> state_;
};

} // namespace graphics_ui::core
