#include <graphics-ui/core/state_store.hpp>

#include "owner_id.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <list>
#include <map>
#include <mutex>
#include <new>
#include <set>
#include <thread>

namespace graphics_ui::core::detail {
namespace {
bool validValue(const StateValue& value) noexcept {
    return !value.valueless_by_exception() &&
        (!std::holds_alternative<double>(value) || std::isfinite(std::get<double>(value)));
}

template<class Callable, class... Args>
auto evaluate(const Callable& callback, const Args&... args) -> Result<std::invoke_result_t<Callable, const Args&...>> {
    try {
        return callback(args...);
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    } catch (...) {
        return std::unexpected(ErrorCode::evaluation_failed);
    }
}

using DrainResults = std::vector<Result<std::uint64_t>>;
} // namespace

struct StateInbox {
    struct Message {
        ComponentHandle target;
        std::vector<StateWrite> writes;
    };

    explicit StateInbox(std::size_t limit) : capacity(limit) {}

    Result<void> post(ComponentHandle target, std::span<const StateWrite> writes) {
        const std::scoped_lock guard(mutex);
        if (closed) return std::unexpected(ErrorCode::closed);
        if (writes.empty()) return std::unexpected(ErrorCode::invalid_argument);
        if (messages.size() == capacity) return std::unexpected(ErrorCode::queue_full);
        messages.push_back({target, {writes.begin(), writes.end()}});
        return {};
    }

    void take(std::list<Message>& batch, DrainResults& results) {
        const std::scoped_lock guard(mutex);
        results.reserve(messages.size());
        batch.splice(batch.end(), messages);
    }

    void close() noexcept {
        const std::scoped_lock guard(mutex);
        closed = true;
        messages.clear();
    }

    const std::size_t capacity;
    std::mutex mutex;
    std::list<Message> messages;
    bool closed{};
};

struct StateObserver {
    void disconnect() noexcept {
        auto held = std::move(callback);
        watch.reset();
    }

    [[nodiscard]] bool active() const noexcept;

    std::weak_ptr<StateData> owner;
    ComponentHandle target;
    std::shared_ptr<StateStore::Observer> callback;
    std::shared_ptr<ComponentTree::UnmountCallback> watch;
};

struct StateData {
    using Edge = std::pair<std::size_t, std::size_t>;
    struct Field {
        StateValue value;
        StateStore::Validator validate;
        StateStore::Computation compute;
        std::vector<std::size_t> dependencies;
        std::vector<std::weak_ptr<StateObserver>> observers;
    };
    struct Delivery {
        StateChange change;
        std::vector<std::weak_ptr<StateObserver>> observers;
    };

    StateData(ComponentTree& component_tree, std::uint64_t owner_id, std::size_t queue_capacity)
        : owner(owner_id), tree(component_tree.state_), inbox(std::make_shared<StateInbox>(queue_capacity)) {}

    [[nodiscard]] static Result<void> checkTree(const ComponentTree& tree) noexcept { return tree.checkBindable(); }
    [[nodiscard]] bool onOwnerThread() const noexcept { return thread == std::this_thread::get_id(); }
    [[nodiscard]] bool mounted(ComponentHandle handle) const noexcept { return ComponentTree::isMounted(tree, handle); }
    [[nodiscard]] Result<std::shared_ptr<ComponentTree::UnmountCallback>> watchUnmount(
        ComponentHandle handle, ComponentTree::UnmountCallback callback) {
        return ComponentTree::watchUnmount(tree, handle, std::move(callback));
    }

    [[nodiscard]] std::optional<ErrorCode> access() const noexcept {
        if (!onOwnerThread()) return ErrorCode::wrong_thread;
        if (closed) return ErrorCode::closed;
        if (preparing) return ErrorCode::busy;
        return std::nullopt;
    }

    [[nodiscard]] Result<std::size_t> index(StateField field) const noexcept {
        if (field.owner_ != owner || field.index_ >= fields.size()) return std::unexpected(ErrorCode::stale_handle);
        return field.index_;
    }

    [[nodiscard]] StateField handle(std::size_t index) const noexcept { return StateField(owner, index); }

    void deliver() noexcept {
        if (delivering) return;
        delivering = true;
        while (!pending.empty()) {
            // Splice instead of moving the container and allocating an iterator proxy in Debug.
            // The active entry lives outside pending, which close() may clear in a callback.
            current.splice(current.end(), pending, pending.begin());
            const auto& delivery = current.front();
            for (const auto& weak : delivery.observers) {
                const auto observer = weak.lock();
                if (!observer || !observer->active()) continue;
                const auto callback = observer->callback;
                (*callback)(delivery.change);
            }
            current.pop_front();
        }
        delivering = false;
    }

    void clear() noexcept {
        pending.clear();
        for (const auto& field : fields)
            for (const auto& weak : field.observers)
                if (const auto observer = weak.lock()) observer->disconnect();
        fields.clear();
        dependents.clear();
    }

    void shutdown() noexcept {
        if (closed) return;
        closed = true;
        inbox->close();
        if (!preparing) clear();
    }

    struct Preparation {
        explicit Preparation(StateData& data) noexcept : data(data) { data.preparing = true; }
        ~Preparation() {
            data.preparing = false;
            if (data.closed) data.clear();
        }
        StateData& data;
    };

    [[nodiscard]] Result<std::uint64_t> publish(std::span<const StateWrite> writes);
    [[nodiscard]] Result<std::uint64_t> apply(std::span<const StateWrite> writes);

    const std::uint64_t owner;
    const std::thread::id thread = std::this_thread::get_id();
    const std::weak_ptr<ComponentTree::State> tree;
    const std::shared_ptr<StateInbox> inbox;
    std::vector<Field> fields;
    std::set<Edge> dependents;
    std::list<Delivery> pending;
    std::list<Delivery> current;
    std::uint64_t revision{};
    bool preparing{};
    bool delivering{};
    bool draining{};
    bool closed{};
};

bool StateObserver::active() const noexcept {
    const auto data = owner.lock();
    return data && data->onOwnerThread() && callback && !data->closed && data->mounted(target);
}

struct StateEvaluation {
    static constexpr unsigned max_depth = 128;
    enum class Phase { waiting, visiting, ready };
    struct Attempt {
        std::optional<StateValue> value;
        std::vector<std::size_t> dependencies;
        Phase phase = Phase::waiting;
    };

    explicit StateEvaluation(StateData& data) : data(data) {}

    [[nodiscard]] Result<StateValue> compute(const StateStore::Computation& computation,
                                            std::vector<std::size_t>& dependencies) {
        const StateReader reader(*this, dependencies);
        auto evaluated = evaluate(computation, reader);
        if (error) return std::unexpected(*error);
        if (!evaluated) return std::unexpected(evaluated.error());
        if (!*evaluated) return std::unexpected(evaluated->error());
        std::ranges::sort(dependencies);
        dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
        return std::move(evaluated->value());
    }

    [[nodiscard]] Result<const StateValue*> resolve(std::size_t index) {
        const auto found = attempts.find(index);
        const auto& field = data.fields[index];
        if (found == attempts.end()) return &field.value;

        auto& candidate = found->second;
        if (candidate.phase == Phase::ready) return &*candidate.value;
        if (candidate.phase == Phase::visiting) return std::unexpected(ErrorCode::dependency_cycle);
        if (depth == max_depth) return std::unexpected(ErrorCode::limit_exceeded);
        candidate.phase = Phase::visiting;
        ++depth;
        auto value = compute(field.compute, candidate.dependencies);
        --depth;
        if (!value) return std::unexpected(value.error());
        if (!validValue(*value) || value->index() != field.value.index())
            return std::unexpected(ErrorCode::invalid_argument);
        candidate.value.emplace(std::move(*value));
        candidate.phase = Phase::ready;
        return &*candidate.value;
    }

    [[nodiscard]] Result<StateValue> read(StateField field, std::vector<std::size_t>& dependencies) {
        if (error) return std::unexpected(*error);
        const auto index = data.index(field);
        if (!index) {
            error = index.error();
            return std::unexpected(*error);
        }
        const auto value = resolve(*index);
        if (!value) {
            error = value.error();
            return std::unexpected(*error);
        }
        dependencies.push_back(*index);
        return **value;
    }

    // Start from changed input fields and follow the previous reverse edges. Computations
    // resolve their actual reads against staged values, so old conditional edges cannot force a cycle.
    void markDependents(std::vector<std::size_t>& dirty) {
        for (std::size_t cursor = 0; cursor < dirty.size(); ++cursor) {
            const auto dependency = dirty[cursor];
            for (auto edge = data.dependents.lower_bound({dependency, 0});
                 edge != data.dependents.end() && edge->first == dependency; ++edge) {
                if (attempts.try_emplace(edge->second).second) dirty.push_back(edge->second);
            }
        }
    }

    StateData& data;
    std::map<std::size_t, Attempt> attempts;
    std::optional<ErrorCode> error;
    unsigned depth{};
};

Result<std::uint64_t> StateData::publish(std::span<const StateWrite> writes) {
    StateEvaluation evaluation(*this);
    std::set<std::size_t> written;
    std::vector<std::size_t> dirty;
    for (const auto& write : writes) {
        const auto slot = index(write.field);
        if (!slot) return std::unexpected(slot.error());
        const auto& field = fields[*slot];
        if (field.compute || !validValue(write.value) || field.value.index() != write.value.index() ||
            !written.insert(*slot).second)
            return std::unexpected(ErrorCode::invalid_argument);
        if (field.validate) {
            const auto valid = evaluate(field.validate, write.value);
            if (!valid) return std::unexpected(valid.error());
            if (!*valid) return std::unexpected(ErrorCode::invalid_argument);
            if (closed) return std::unexpected(ErrorCode::closed);
        }
        if (field.value != write.value) {
            evaluation.attempts.emplace(*slot, StateEvaluation::Attempt{write.value, {}, StateEvaluation::Phase::ready});
            dirty.push_back(*slot);
        }
    }
    if (dirty.empty()) return revision;
    if (revision == serial_limit) return std::unexpected(ErrorCode::limit_exceeded);
    evaluation.markDependents(dirty);
    std::set<Edge> added_edges;
    std::vector<Edge> removed_edges;
    std::list<Delivery> staged;
    for (const auto& [slot, candidate] : evaluation.attempts) {
        const auto result = evaluation.resolve(slot);
        if (!result) return std::unexpected(result.error());
        if (closed) return std::unexpected(ErrorCode::closed);
        const auto& field = fields[slot];
        for (const auto old : field.dependencies)
            if (!std::ranges::binary_search(candidate.dependencies, old)) removed_edges.emplace_back(old, slot);
        for (const auto next : candidate.dependencies)
            if (!std::ranges::binary_search(field.dependencies, next)) added_edges.emplace(next, slot);
        if (field.value != **result && !field.observers.empty())
            staged.push_back({{handle(slot), revision + 1, **result}, field.observers});
    }

    // Publish only after allocating the new edges and every notice. Merging the staged nodes,
    // moving the values and splicing the notices do not allocate.
    for (const auto& edge : removed_edges) dependents.erase(edge);
    dependents.merge(added_edges);
    for (auto& [slot, candidate] : evaluation.attempts) {
        auto& field = fields[slot];
        field.dependencies = std::move(candidate.dependencies);
        field.value = std::move(*candidate.value);
        std::erase_if(field.observers, [](const auto& observer) { return observer.expired(); });
    }
    pending.splice(pending.end(), staged);
    return ++revision;
}

Result<std::uint64_t> StateData::apply(std::span<const StateWrite> writes) {
    if (const auto error = access()) return std::unexpected(*error);
    try {
        const auto result = [&] {
            const Preparation preparing(*this);
            return publish(writes);
        }();
        if (result) deliver();
        return result;
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    }
}

} // namespace graphics_ui::core::detail

namespace graphics_ui::core {

Result<StateValue> StateReader::read(StateField field) const {
    return evaluation_.read(field, dependencies_);
}

StateSubscription::StateSubscription(std::shared_ptr<detail::StateObserver> observer) noexcept
    : observer_(std::move(observer)) {}

StateSubscription::StateSubscription(StateSubscription&& other) noexcept = default;

StateSubscription& StateSubscription::operator=(StateSubscription&& other) noexcept {
    if (this != &other) {
        reset();
        observer_ = std::move(other.observer_);
    }
    return *this;
}

StateSubscription::~StateSubscription() noexcept { reset(); }

void StateSubscription::reset() noexcept {
    const auto observer = std::move(observer_);
    if (!observer) return;
    if (const auto owner = observer->owner.lock(); owner && !owner->onOwnerThread()) std::terminate();
    observer->disconnect();
}

bool StateSubscription::active() const noexcept { return observer_ && observer_->active(); }

Result<void> StateSender::post(ComponentHandle target, std::span<const StateWrite> writes) const {
    const auto inbox = inbox_.lock();
    if (!inbox) return std::unexpected(ErrorCode::closed);
    try {
        return inbox->post(target, writes);
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    }
}

StateStore::StateStore(std::shared_ptr<detail::StateData> data) noexcept : data_(std::move(data)) {}

Result<std::unique_ptr<StateStore>> StateStore::create(ComponentTree& tree, std::size_t queue_capacity) {
    const auto bindable = detail::StateData::checkTree(tree);
    if (!bindable) return std::unexpected(bindable.error());
    if (queue_capacity == 0) return std::unexpected(ErrorCode::invalid_argument);
    const auto owner = detail::allocateOwner();
    if (owner == 0) return std::unexpected(ErrorCode::limit_exceeded);
    try {
        auto data = std::make_shared<detail::StateData>(tree, owner, queue_capacity);
        return std::unique_ptr<StateStore>(new StateStore(std::move(data)));
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    }
}

StateStore::~StateStore() noexcept {
    if (!data_->onOwnerThread()) std::terminate();
    data_->shutdown();
}

Result<StateField> StateStore::define(StateValue initial, Validator validate) {
    const auto data = data_;
    if (const auto error = data->access()) return std::unexpected(*error);
    if (!detail::validValue(initial)) return std::unexpected(ErrorCode::invalid_argument);
    try {
        const detail::StateData::Preparation preparing(*data);
        if (validate) {
            const auto valid = detail::evaluate(validate, initial);
            if (!valid) return std::unexpected(valid.error());
            if (!*valid) return std::unexpected(ErrorCode::invalid_argument);
            if (data->closed) return std::unexpected(ErrorCode::closed);
        }
        const auto slot = data->fields.size();
        data->fields.push_back({std::move(initial), std::move(validate), {}, {}, {}});
        return data->handle(slot);
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    }
}

Result<StateField> StateStore::defineComputed(StateType type, Computation compute) {
    const auto data = data_;
    if (const auto error = data->access()) return std::unexpected(*error);
    const auto type_index = static_cast<std::size_t>(type);
    if (!compute || type_index >= std::variant_size_v<StateValue>) return std::unexpected(ErrorCode::invalid_argument);
    try {
        const detail::StateData::Preparation preparing(*data);
        detail::StateEvaluation evaluation(*data);
        std::vector<std::size_t> dependencies;
        auto value = evaluation.compute(compute, dependencies);
        if (!value) return std::unexpected(value.error());
        if (!detail::validValue(*value) || value->index() != type_index)
            return std::unexpected(ErrorCode::invalid_argument);
        if (data->closed) return std::unexpected(ErrorCode::closed);

        const auto slot = data->fields.size();
        std::set<detail::StateData::Edge> added;
        for (const auto dependency : dependencies) added.emplace(dependency, slot);
        data->fields.push_back({std::move(*value), {}, std::move(compute), std::move(dependencies), {}});
        data->dependents.merge(added);
        return data->handle(slot);
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    }
}

Result<StateValue> StateStore::read(StateField field) const {
    if (const auto error = data_->access()) return std::unexpected(*error);
    const auto index = data_->index(field);
    if (!index) return std::unexpected(index.error());
    try {
        return data_->fields[*index].value;
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    }
}

Result<StateBindingSnapshot> StateStore::inspectBindings() const {
    const auto& data = *data_;
    if (const auto error = data.access()) return std::unexpected(*error);
    try {
        StateBindingSnapshot snapshot{data.revision, {}};
        snapshot.fields.reserve(data.fields.size());
        for (std::size_t index = 0; index < data.fields.size(); ++index) {
            const auto& source = data.fields[index];
            StateBindingField field{data.handle(index), static_cast<StateType>(source.value.index()), bool(source.compute), {}, {}};
            field.dependencies.reserve(source.dependencies.size());
            for (const auto dependency : source.dependencies) field.dependencies.push_back(data.handle(dependency));
            field.subscriptions.reserve(source.observers.size());
            for (const auto& weak : source.observers) {
                if (const auto observer = weak.lock()) {
                    auto status = StateSubscriptionStatus::active;
                    if (!data.mounted(observer->target)) status = StateSubscriptionStatus::target_unmounted;
                    else if (!observer->callback) status = StateSubscriptionStatus::disconnected;
                    field.subscriptions.push_back({observer->target, status});
                } else {
                    ++field.expired_subscriptions;
                }
            }
            snapshot.fields.push_back(std::move(field));
        }
        return snapshot;
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    }
}

Result<std::uint64_t> StateStore::transact(std::span<const StateWrite> writes) {
    const auto data = data_;
    return data->apply(writes);
}

Result<StateSubscription> StateStore::subscribe(StateField field, ComponentHandle target, Observer observer) {
    const auto data = data_;
    if (const auto error = data->access()) return std::unexpected(*error);
    if (!observer) return std::unexpected(ErrorCode::invalid_argument);
    const auto index = data->index(field);
    if (!index) return std::unexpected(index.error());
    if (!data->mounted(target)) return std::unexpected(ErrorCode::stale_handle);
    try {
        auto entry = std::make_shared<detail::StateObserver>();
        entry->owner = data;
        entry->target = target;
        entry->callback = std::make_shared<Observer>(std::move(observer));
        auto& subscribers = data->fields[*index].observers;
        std::erase_if(subscribers, [](const auto& weak) { return weak.expired(); });
        subscribers.emplace_back(entry);
        const std::weak_ptr<detail::StateObserver> weak = entry;
        auto watched = data->watchUnmount(target, [weak](ComponentHandle) noexcept {
            if (const auto observer = weak.lock()) observer->disconnect();
        });
        if (!watched) return std::unexpected(watched.error());
        entry->watch = std::move(*watched);
        return StateSubscription(std::move(entry));
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    }
}

Result<StateSender> StateStore::sender() const noexcept {
    if (const auto error = data_->access()) return std::unexpected(*error);
    return StateSender(data_->inbox);
}

Result<std::vector<Result<std::uint64_t>>> StateStore::drain() {
    const auto data = data_;
    if (const auto error = data->access()) return std::unexpected(*error);
    if (data->draining || data->delivering) return std::unexpected(ErrorCode::busy);
    try {
        std::list<detail::StateInbox::Message> batch;
        detail::DrainResults results(0);
        data->inbox->take(batch, results);
        data->draining = true;
        for (const auto& message : batch) {
            if (data->closed) results.emplace_back(std::unexpected(ErrorCode::closed));
            else if (!data->mounted(message.target)) results.emplace_back(std::unexpected(ErrorCode::stale_handle));
            else results.push_back(data->apply(message.writes));
        }
        data->draining = false;
        return results;
    } catch (const std::bad_alloc&) {
        data->draining = false;
        return std::unexpected(ErrorCode::out_of_memory);
    }
}

Result<void> StateStore::close() noexcept {
    const auto data = data_;
    if (!data->onOwnerThread()) return std::unexpected(ErrorCode::wrong_thread);
    if (data->preparing) return std::unexpected(ErrorCode::busy);
    data->shutdown();
    return {};
}

} // namespace graphics_ui::core
