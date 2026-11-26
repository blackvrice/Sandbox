#pragma once

#include <graphics-ui/core/component_tree.hpp>

#include <string>
#include <utility>
#include <variant>

namespace graphics_ui::core {

namespace detail {
struct StateInbox;
struct StateEvaluation;
struct StateObserver;
}

enum class StateType { boolean, integer, number, text };
using StateValue = std::variant<bool, std::int64_t, double, std::string>;

class StateField {
public:
    StateField() = default;
    bool operator==(const StateField&) const = default;

private:
    friend struct detail::StateData;
    StateField(std::uint64_t owner, std::size_t index) noexcept : owner_(owner), index_(index) {}

    std::uint64_t owner_{};
    std::size_t index_{};
};

struct StateWrite {
    StateField field;
    StateValue value;
};

class StateReader {
public:
    StateReader(const StateReader&) = delete;
    StateReader& operator=(const StateReader&) = delete;
    [[nodiscard]] Result<StateValue> read(StateField field) const;

private:
    friend struct detail::StateEvaluation;
    StateReader(detail::StateEvaluation& evaluation, std::vector<std::size_t>& dependencies) noexcept
        : evaluation_(evaluation), dependencies_(dependencies) {}

    detail::StateEvaluation& evaluation_;
    std::vector<std::size_t>& dependencies_;
};

struct StateChange {
    StateField field;
    std::uint64_t revision;
    StateValue value;
};

enum class StateSubscriptionStatus { active, target_unmounted, disconnected };

struct StateBindingSubscription {
    ComponentHandle target;
    StateSubscriptionStatus status;
};

struct StateBindingField {
    StateField field;
    StateType type;
    bool computed;
    std::vector<StateField> dependencies;
    std::vector<StateBindingSubscription> subscriptions;
    std::size_t expired_subscriptions{};
};

struct StateBindingSnapshot {
    std::uint64_t revision{};
    std::vector<StateBindingField> fields;
};

class StateSubscription {
public:
    // Connected subscriptions are released on the store's UI thread.
    StateSubscription() = default;
    StateSubscription(const StateSubscription&) = delete;
    StateSubscription& operator=(const StateSubscription&) = delete;
    StateSubscription(StateSubscription&& other) noexcept;
    StateSubscription& operator=(StateSubscription&& other) noexcept;
    ~StateSubscription() noexcept;

    void reset() noexcept;
    [[nodiscard]] bool active() const noexcept;

private:
    friend class StateStore;
    explicit StateSubscription(std::shared_ptr<detail::StateObserver> observer) noexcept;

    std::shared_ptr<detail::StateObserver> observer_;
};

class StateSender {
public:
    StateSender() = default;
    // Thread-safe. Copies the writes. The owner validates component and field handles on drain.
    [[nodiscard]] Result<void> post(ComponentHandle target, std::span<const StateWrite> writes) const;

private:
    friend class StateStore;
    explicit StateSender(std::weak_ptr<detail::StateInbox> inbox) noexcept : inbox_(std::move(inbox)) {}

    std::weak_ptr<detail::StateInbox> inbox_;
};

class StateStore {
public:
    using Validator = std::function<bool(const StateValue&)>;
    using Computation = std::function<Result<StateValue>(const StateReader&)>;
    using Observer = std::function<void(const StateChange&)>;

    // The tree and store must be created on the same UI thread. The tree may be destroyed first.
    [[nodiscard]] static Result<std::unique_ptr<StateStore>> create(ComponentTree& tree, std::size_t queue_capacity = 64);

    StateStore(const StateStore&) = delete;
    StateStore& operator=(const StateStore&) = delete;
    ~StateStore() noexcept;

    [[nodiscard]] Result<StateField> define(StateValue initial, Validator validate = {});
    [[nodiscard]] Result<StateField> defineComputed(StateType type, Computation compute);
    [[nodiscard]] Result<StateValue> read(StateField field) const;
    // Owner-thread copy of the published graph, in field definition order. Does not evaluate or notify.
    [[nodiscard]] Result<StateBindingSnapshot> inspectBindings() const;

    // Each field may be written once per transaction. No-op transactions keep the revision.
    [[nodiscard]] Result<std::uint64_t> transact(std::span<const StateWrite> writes);
    // Observers are invoked after publication and must not throw.
    [[nodiscard]] Result<StateSubscription> subscribe(StateField field, ComponentHandle target, Observer observer);

    [[nodiscard]] Result<StateSender> sender() const noexcept;
    // Processes a FIFO snapshot. Each result belongs to one dequeued message.
    [[nodiscard]] Result<std::vector<Result<std::uint64_t>>> drain();
    [[nodiscard]] Result<void> close() noexcept;

private:
    explicit StateStore(std::shared_ptr<detail::StateData> data) noexcept;

    std::shared_ptr<detail::StateData> data_;
};

} // namespace graphics_ui::core
