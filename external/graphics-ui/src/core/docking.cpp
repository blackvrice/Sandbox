#include <graphics-ui/core/docking.hpp>

#include "sha256.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <string_view>
#include <thread>

namespace graphics_ui::core {
namespace {
constexpr std::size_t max_panes = 128, max_depth = 32;
constexpr int ratio_scale = 10000;
constexpr std::string_view magic = "GUIDOCK\1";
std::atomic<std::uint64_t> workspace_sequence{1};

struct Failure { ErrorCode code; };

void require(bool condition, ErrorCode code = ErrorCode::invalid_argument) {
    if (!condition) throw Failure{code};
}

template<class Function>
auto guarded(Function&& function) -> decltype(function()) {
    try { return function(); }
    catch (const Failure& failure) { return std::unexpected(failure.code); }
    catch (const std::bad_alloc&) { return std::unexpected(ErrorCode::out_of_memory); }
}

struct Node {
    std::vector<DockPaneId> tabs;
    DockPaneId active{};
    DockAxis axis{};
    std::uint16_t ratio = ratio_scale / 2;
    std::unique_ptr<Node> first, second;

    bool group() const noexcept { return !first; }
};
using Tree = std::unique_ptr<Node>;

struct State {
    Tree root;
    std::vector<DockFloatingPane> floating;
    DockPaneId focused{};
};

Tree clone(const Tree& node) {
    if (!node) return {};
    return std::make_unique<Node>(Node{node->tabs, node->active, node->axis, node->ratio, clone(node->first), clone(node->second)});
}

Tree single(DockPaneId pane) {
    auto node = std::make_unique<Node>();
    node->tabs.push_back(pane);
    node->active = pane;
    return node;
}

Tree* groupLink(Tree& node, DockPaneId pane) {
    if (!node) return nullptr;
    if (node->group()) return std::ranges::find(node->tabs, pane) != node->tabs.end() ? &node : nullptr;
    if (auto* first = groupLink(node->first, pane)) return first;
    return groupLink(node->second, pane);
}

bool remove(Tree& node, DockPaneId pane) {
    if (!node) return false;
    if (node->group()) {
        const auto found = std::ranges::find(node->tabs, pane);
        if (found == node->tabs.end()) return false;
        const auto next = node->tabs.erase(found);
        if (node->tabs.empty()) node.reset();
        else if (node->active == pane) node->active = next == node->tabs.end() ? node->tabs.back() : *next;
        return true;
    }
    if (!remove(node->first, pane) && !remove(node->second, pane)) return false;
    if (!node->first || !node->second) {
        auto child = node->first ? std::move(node->first) : std::move(node->second);
        node = std::move(child);
    }
    return true;
}

auto floatingPane(State& state, DockPaneId pane) {
    return std::ranges::find(state.floating, pane, &DockFloatingPane::pane);
}

void extract(State& state, DockPaneId pane) {
    if (remove(state.root, pane)) return;
    const auto found = floatingPane(state, pane);
    require(found != state.floating.end());
    state.floating.erase(found);
}

bool valid(DockFloatingBounds bounds) {
    return bounds.x >= -1000000 && bounds.x <= 1000000 && bounds.y >= -1000000 && bounds.y <= 1000000 &&
        bounds.width > 0 && bounds.width <= 65535 && bounds.height > 0 && bounds.height <= 65535;
}

void validate(const State& state, std::span<const DockPaneId> catalog) {
    std::array<bool, max_panes> used{};
    bool focus_found{};
    const auto pane = [&](DockPaneId id, bool active) {
        const auto found = std::ranges::lower_bound(catalog, id);
        require(found != catalog.end() && *found == id);
        const auto index = static_cast<std::size_t>(found - catalog.begin());
        require(!used[index]);
        used[index] = true;
        if (active && id == state.focused) focus_found = true;
    };
    const auto visit = [&](auto&& self, const Node& node, std::size_t depth) -> void {
        require(depth <= max_depth, ErrorCode::limit_exceeded);
        if (node.group()) {
            require(!node.tabs.empty() && std::ranges::find(node.tabs, node.active) != node.tabs.end());
            for (const auto id : node.tabs) pane(id, id == node.active);
        } else {
            self(self, *node.first, depth + 1);
            self(self, *node.second, depth + 1);
        }
    };
    if (state.root) visit(visit, *state.root, 1);
    for (const auto& window : state.floating) {
        require(valid(window.bounds));
        pane(window.pane, true);
    }
    require(focus_found && std::ranges::all_of(std::span(used).first(catalog.size()), std::identity{}));
}

Node* dividerAt(Tree& node, std::size_t& ordinal) {
    if (!node || node->group()) return nullptr;
    if (ordinal == 0) return node.get();
    --ordinal;
    if (auto* first = dividerAt(node->first, ordinal)) return first;
    return dividerAt(node->second, ordinal);
}

LayoutSize minimum(const Node& node, const DockMetrics& metrics) {
    if (node.group()) return {metrics.minimum_group.width, std::max(metrics.tab_height, metrics.minimum_group.height)};
    const auto first = minimum(*node.first, metrics), second = minimum(*node.second, metrics);
    if (node.axis == DockAxis::horizontal) return {first.width + second.width + metrics.divider_width, std::max(first.height, second.height)};
    return {std::max(first.width, second.width), first.height + second.height + metrics.divider_width};
}

void arrangeNode(const Node& node, LayoutRect area, const DockMetrics& metrics, std::uint64_t workspace, DockFrame& frame) {
    if (node.group()) {
        const auto header = std::min(area.height, metrics.tab_height);
        frame.groups.push_back({node.active, area, {area.x, area.y + header, area.width, area.height - header}});
        const auto total = static_cast<double>(node.tabs.size());
        for (std::size_t index = 0; index < node.tabs.size(); ++index) {
            const auto begin = area.width * static_cast<double>(index) / total;
            const auto end = area.width * static_cast<double>(index + 1) / total;
            frame.tabs.push_back({node.tabs[index], node.active, {area.x + begin, area.y, end - begin, header}});
        }
        return;
    }
    const bool horizontal = node.axis == DockAxis::horizontal;
    const auto extent = horizontal ? area.width : area.height;
    const auto divider = std::min(metrics.divider_width, extent);
    const auto available = extent - divider;
    const auto first_min = minimum(*node.first, metrics), second_min = minimum(*node.second, metrics);
    const auto left_min = horizontal ? first_min.width : first_min.height;
    const auto right_min = horizontal ? second_min.width : second_min.height;
    const auto fraction = static_cast<double>(node.ratio) / ratio_scale;
    const auto first = left_min + right_min <= available ?
        std::clamp(available * fraction, left_min, std::max(left_min, available - right_min)) :
        available * left_min / (left_min + right_min);
    const auto second = available - first;
    const auto gap = horizontal ? LayoutRect{area.x + first, area.y, divider, area.height} :
                                  LayoutRect{area.x, area.y + first, area.width, divider};
    frame.dividers.push_back({{workspace, frame.revision, frame.dividers.size()}, node.axis, gap, area, fraction});
    if (horizontal) {
        arrangeNode(*node.first, {area.x, area.y, first, area.height}, metrics, workspace, frame);
        arrangeNode(*node.second, {gap.x + divider, area.y, second, area.height}, metrics, workspace, frame);
    } else {
        arrangeNode(*node.first, {area.x, area.y, area.width, first}, metrics, workspace, frame);
        arrangeNode(*node.second, {area.x, gap.y + divider, area.width, second}, metrics, workspace, frame);
    }
}

template<class T>
void put(std::vector<std::byte>& bytes, T value) {
    static_assert(std::is_unsigned_v<T>);
    for (std::size_t index = 0; index < sizeof(T); ++index)
        bytes.push_back(static_cast<std::byte>((value >> (index * 8)) & 0xff));
}

void writeNode(std::vector<std::byte>& bytes, const Tree& node) {
    if (!node) {
        put(bytes, std::uint8_t{0});
    } else if (node->group()) {
        put(bytes, std::uint8_t{1});
        put(bytes, static_cast<std::uint16_t>(node->tabs.size()));
        put(bytes, node->active);
        for (const auto pane : node->tabs) put(bytes, pane);
    } else {
        put(bytes, static_cast<std::uint8_t>(node->axis == DockAxis::horizontal ? 2 : 3));
        put(bytes, node->ratio);
        writeNode(bytes, node->first);
        writeNode(bytes, node->second);
    }
}

class Reader {
public:
    explicit Reader(std::span<const std::byte> input) : bytes(input) {}

    template<class T>
    T number() {
        static_assert(std::is_unsigned_v<T> && sizeof(T) <= 4);
        require(bytes.size() >= sizeof(T));
        std::uint32_t value{};
        for (std::size_t index = 0; index < sizeof(T); ++index)
            value |= std::to_integer<std::uint32_t>(bytes[index]) << (index * 8);
        bytes = bytes.subspan(sizeof(T));
        return static_cast<T>(value);
    }

    std::int32_t coordinate() { return std::bit_cast<std::int32_t>(number<std::uint32_t>()); }

    Tree readNode(std::size_t depth, bool optional) {
        require(depth <= max_depth, ErrorCode::limit_exceeded);
        const auto tag = number<std::uint8_t>();
        if (tag == 0 && optional) return {};
        require(tag >= 1 && tag <= 3);
        require(++nodes <= max_panes * 2 - 1, ErrorCode::limit_exceeded);
        auto node = std::make_unique<Node>();
        if (tag == 1) {
            const auto count = number<std::uint16_t>();
            require(count > 0 && count <= max_panes);
            node->active = number<DockPaneId>();
            for (std::size_t index = 0; index < count; ++index) node->tabs.push_back(number<DockPaneId>());
        } else {
            node->axis = tag == 2 ? DockAxis::horizontal : DockAxis::vertical;
            node->ratio = number<std::uint16_t>();
            require(node->ratio > 0 && node->ratio < ratio_scale);
            node->first = readNode(depth + 1, false);
            node->second = readNode(depth + 1, false);
        }
        return node;
    }

    std::span<const std::byte> bytes;

private:
    std::size_t nodes{};
};

std::vector<std::byte> encode(const State& state) {
    const auto prefix = std::as_bytes(std::span(magic));
    std::vector<std::byte> bytes(prefix.begin(), prefix.end());
    writeNode(bytes, state.root);
    put(bytes, static_cast<std::uint16_t>(state.floating.size()));
    for (const auto& pane : state.floating) {
        put(bytes, pane.pane);
        for (const auto value : {pane.bounds.x, pane.bounds.y, pane.bounds.width, pane.bounds.height})
            put(bytes, std::bit_cast<std::uint32_t>(value));
    }
    put(bytes, state.focused);
    const auto digest = detail::sha256(bytes);
    bytes.insert(bytes.end(), digest.begin(), digest.end());
    return bytes;
}

State decode(std::span<const std::byte> bytes) {
    constexpr std::size_t hash_size = 32;
    require(bytes.size() <= dock_layout_byte_limit, ErrorCode::limit_exceeded);
    require(bytes.size() >= magic.size() + hash_size);
    require(std::ranges::equal(bytes.first(magic.size()), std::as_bytes(std::span(magic))));
    const auto payload = bytes.first(bytes.size() - hash_size);
    require(std::ranges::equal(detail::sha256(payload), bytes.last(hash_size)));
    Reader reader(payload.subspan(magic.size()));
    State state;
    state.root = reader.readNode(1, true);
    const auto count = reader.number<std::uint16_t>();
    require(count <= max_panes, ErrorCode::limit_exceeded);
    for (std::size_t index = 0; index < count; ++index) {
        const auto pane = reader.number<DockPaneId>();
        state.floating.push_back({pane, {reader.coordinate(), reader.coordinate(), reader.coordinate(), reader.coordinate()}});
    }
    state.focused = reader.number<DockPaneId>();
    require(reader.bytes.empty());
    return state;
}
} // namespace

struct DockLayout::Impl {
    const std::thread::id owner = std::this_thread::get_id();
    const std::uint64_t identity = workspace_sequence.fetch_add(1, std::memory_order_relaxed);
    std::uint64_t revision = 1;
    std::vector<DockPaneId> catalog;
    State state;

    void checkThread() const { require(owner == std::this_thread::get_id(), ErrorCode::wrong_thread); }

    void change() {
        require(revision != std::numeric_limits<std::uint64_t>::max(), ErrorCode::limit_exceeded);
        ++revision;
    }

    void publish(State next) {
        validate(next, catalog);
        change();
        state = std::move(next);
    }

    State copy() const { return {clone(state.root), state.floating, state.focused}; }
};

DockLayout::DockLayout(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
DockLayout::~DockLayout() noexcept = default;

Result<std::unique_ptr<DockLayout>> DockLayout::create(std::span<const DockPaneId> panes) {
    return guarded([&]() -> Result<std::unique_ptr<DockLayout>> {
        require(!panes.empty());
        require(panes.size() <= max_panes, ErrorCode::limit_exceeded);
        auto impl = std::make_unique<Impl>();
        require(impl->identity != 0, ErrorCode::limit_exceeded);
        impl->catalog.assign(panes.begin(), panes.end());
        std::ranges::sort(impl->catalog);
        require(impl->catalog.front() != 0 && std::ranges::adjacent_find(impl->catalog) == impl->catalog.end());
        impl->state.root = single(panes.front());
        impl->state.root->tabs.assign(panes.begin(), panes.end());
        impl->state.focused = panes.front();
        return std::unique_ptr<DockLayout>(new DockLayout(std::move(impl)));
    });
}

Result<void> DockLayout::select(DockPaneId pane) {
    return guarded([&]() -> Result<void> {
        impl_->checkThread();
        auto& state = impl_->state;
        auto* link = groupLink(state.root, pane);
        require(link || floatingPane(state, pane) != state.floating.end());
        if (state.focused != pane) {
            impl_->change();
            if (link) (*link)->active = pane;
            state.focused = pane;
        }
        return {};
    });
}

Result<void> DockLayout::dock(DockPaneId pane, DockPaneId target, DockSide side) {
    return guarded([&]() -> Result<void> {
        impl_->checkThread();
        const bool tab = side == DockSide::tab_before || side == DockSide::tab_after;
        const bool horizontal = side == DockSide::left || side == DockSide::right;
        require(pane != 0 && pane != target && (tab || horizontal || side == DockSide::top || side == DockSide::bottom));
        require(target == 0 ? !impl_->state.root && tab : groupLink(impl_->state.root, target) != nullptr);
        auto next = impl_->copy();
        extract(next, pane);
        if (target == 0) {
            next.root = single(pane);
        } else {
            auto& location = *groupLink(next.root, target);
            if (tab) {
                const auto position = std::ranges::find(location->tabs, target);
                location->tabs.insert(side == DockSide::tab_before ? position : position + 1, pane);
                location->active = pane;
            } else {
                auto split = std::make_unique<Node>();
                auto incoming = single(pane);
                split->axis = horizontal ? DockAxis::horizontal : DockAxis::vertical;
                const bool preceding = side == DockSide::left || side == DockSide::top;
                split->first = preceding ? std::move(incoming) : std::move(location);
                split->second = preceding ? std::move(location) : std::move(incoming);
                location = std::move(split);
            }
        }
        next.focused = pane;
        impl_->publish(std::move(next));
        return {};
    });
}

Result<void> DockLayout::undock(DockPaneId pane, DockFloatingBounds bounds) {
    return guarded([&]() -> Result<void> {
        impl_->checkThread();
        require(valid(bounds));
        auto& state = impl_->state;
        if (auto found = floatingPane(state, pane); found != state.floating.end()) {
            if (found->bounds != bounds || state.focused != pane) {
                impl_->change();
                found->bounds = bounds;
                state.focused = pane;
            }
            return {};
        }
        auto next = impl_->copy();
        extract(next, pane);
        next.floating.push_back({pane, bounds});
        next.focused = pane;
        impl_->publish(std::move(next));
        return {};
    });
}

Result<void> DockLayout::resize(DockDividerKey divider, double fraction) {
    return guarded([&]() -> Result<void> {
        impl_->checkThread();
        require(divider.workspace == impl_->identity && divider.revision == impl_->revision, ErrorCode::stale_handle);
        auto* node = dividerAt(impl_->state.root, divider.ordinal);
        require(node != nullptr, ErrorCode::stale_handle);
        require(std::isfinite(fraction) && fraction > 0 && fraction < 1);
        const auto ratio = static_cast<std::uint16_t>(std::clamp(std::lround(fraction * ratio_scale), 1L, ratio_scale - 1L));
        if (ratio != node->ratio) {
            impl_->change();
            node->ratio = ratio;
        }
        return {};
    });
}

Result<void> DockLayout::moveFloating(DockPaneId pane, DockFloatingBounds bounds) {
    return guarded([&]() -> Result<void> {
        impl_->checkThread();
        require(valid(bounds));
        auto& state = impl_->state;
        const auto found = floatingPane(state, pane);
        require(found != state.floating.end());
        if (found->bounds != bounds) {
            impl_->change();
            found->bounds = bounds;
        }
        return {};
    });
}

Result<DockFrame> DockLayout::arrange(LayoutSize viewport, DockMetrics metrics) const {
    return guarded([&]() -> Result<DockFrame> {
        impl_->checkThread();
        const auto dimension = [](double value) { return std::isfinite(value) && value >= 0 && value <= 1000000; };
        require(dimension(viewport.width) && dimension(viewport.height) && dimension(metrics.tab_height) &&
            dimension(metrics.divider_width) && dimension(metrics.minimum_group.width) && dimension(metrics.minimum_group.height));
        DockFrame frame;
        frame.revision = impl_->revision;
        frame.focused = impl_->state.focused;
        frame.floating = impl_->state.floating;
        if (impl_->state.root) arrangeNode(*impl_->state.root, {0, 0, viewport.width, viewport.height}, metrics, impl_->identity, frame);
        return frame;
    });
}

Result<std::vector<std::byte>> DockLayout::save() const {
    return guarded([&]() -> Result<std::vector<std::byte>> {
        impl_->checkThread();
        return encode(impl_->state);
    });
}

Result<void> DockLayout::restore(std::span<const std::byte> bytes) {
    return guarded([&]() -> Result<void> {
        impl_->checkThread();
        impl_->publish(decode(bytes));
        return {};
    });
}

} // namespace graphics_ui::core
