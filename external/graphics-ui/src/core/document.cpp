#include <graphics-ui/core/document.hpp>

#include "document_validation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace graphics_ui::core {
namespace {
struct Failure { ErrorCode code; };

template<class T>
T checked(Result<T> result) {
    if (!result) throw Failure{result.error()};
    if constexpr (!std::is_void_v<T>) return std::move(*result);
}

void require(bool condition) {
    if (!condition) throw Failure{ErrorCode::invalid_argument};
}

template<class Function>
auto guarded(Function&& run) -> decltype(run()) {
    try { return run(); }
    catch (const Failure& failure) { return std::unexpected(failure.code); }
    catch (const std::bad_alloc&) { return std::unexpected(ErrorCode::out_of_memory); }
    catch (const std::length_error&) { return std::unexpected(ErrorCode::limit_exceeded); }
    catch (...) { return std::unexpected(ErrorCode::evaluation_failed); }
}

bool nonnegative(double value) noexcept { return std::isfinite(value) && value >= 0; }

bool valid(const DocumentStyle& style) noexcept {
    for (const auto value : {style.width, style.height, style.gap, style.font_size})
        if (value && !nonnegative(*value)) return false;
    if ((style.font && !*style.font) || (style.font_size && !*style.font_size)) return false;
    if (style.padding) {
        const auto& pad = *style.padding;
        return nonnegative(pad.left) && nonnegative(pad.right) && nonnegative(pad.top) && nonnegative(pad.bottom);
    }
    return true;
}

LayoutKind layoutKind(const DocumentNode& node) noexcept {
    switch (node.type) {
    case DocumentKind::window:
    case DocumentKind::panel: return LayoutKind::column;
    case DocumentKind::stack_panel: return node.horizontal ? LayoutKind::row : LayoutKind::column;
    case DocumentKind::grid: return LayoutKind::grid;
    case DocumentKind::flex_panel: return node.horizontal ? LayoutKind::flex_row : LayoutKind::flex_column;
    default: return LayoutKind::leaf;
    }
}

bool validType(DocumentKind kind) noexcept {
    return kind >= DocumentKind::window && kind <= DocumentKind::button;
}

void combine(DocumentStyle& selected, const DocumentStyle& patch) {
    const auto copy = [](auto& target, const auto& value) { if (value) target = value; };
    copy(selected.width, patch.width);
    copy(selected.height, patch.height);
    copy(selected.gap, patch.gap);
    copy(selected.padding, patch.padding);
    copy(selected.color, patch.color);
    copy(selected.background, patch.background);
    copy(selected.font, patch.font);
    copy(selected.font_size, patch.font_size);
}

std::array<std::size_t, 3> specificity(const DocumentRule& rule) {
    std::array<std::size_t, 3> weight{};
    for (const auto& part : rule.selector) {
        weight[0] += !part.id.empty();
        weight[1] += part.classes.size() + (part.state != DocumentState::none) + part.root;
        weight[2] += part.type.has_value();
    }
    return weight;
}

bool matches(const DocumentSelectorPart& part, const DocumentNode& node, const ControlVisualState& state) {
    if (part.type && *part.type != node.type) return false;
    if (part.root && node.parent) return false;
    const std::string_view id(node.id);
    const auto slash = id.rfind('/');
    const auto local_id = slash == id.npos ? id : id.substr(slash + 1);
    if (!part.id.empty() && part.id != local_id && std::ranges::find(node.template_ids, part.id) == node.template_ids.end())
        return false;
    for (const auto& name : part.classes)
        if (std::ranges::find(node.classes, name) == node.classes.end()) return false;
    switch (part.state) {
    case DocumentState::none: return true;
    case DocumentState::disabled: return !state.enabled;
    case DocumentState::focus: return state.focused;
    case DocumentState::hover: return state.hovered;
    case DocumentState::pressed: return state.pressed;
    }
    return false;
}
} // namespace

Result<void> detail::validateDocument(const Document& document) {
    return guarded([&]() -> Result<void> {
        if (document.nodes.size() > max_document_nodes || document.rules.size() > max_document_rules)
            return std::unexpected(ErrorCode::limit_exceeded);
        require(!document.nodes.empty());
        std::unordered_set<std::string_view> ids;
        for (std::size_t index = 0; index < document.nodes.size(); ++index) {
            const auto& node = document.nodes[index];
            require(validType(node.type) && valid(node.style) && !node.id.empty() && ids.insert(node.id).second);
            require((node.type == DocumentKind::window) == (index == 0) && node.parent.has_value() == (index != 0));
            if (node.parent) require(*node.parent < index && layoutKind(document.nodes[*node.parent]) != LayoutKind::leaf);
            require(node.type != DocumentKind::grid || node.columns > 0);
            const bool input_node = node.type == DocumentKind::text_box || node.type == DocumentKind::button;
            require(input_node || (node.command.empty() && node.enabled_field.empty() && node.enabled));
            if (layoutKind(node) != LayoutKind::leaf) require(node.text.empty() && node.text_field.empty());
        }
        for (const auto& rule : document.rules) {
            require(valid(rule.style) && !rule.selector.empty() && rule.selector.size() <= max_selector_parts);
            for (std::size_t index = 0; index < rule.selector.size(); ++index) {
                const auto& part = rule.selector[index];
                require(!part.type || validType(*part.type));
                require(part.state >= DocumentState::none && part.state <= DocumentState::pressed && part.classes.size() <= 8);
                require(index == 0 ? part.relation == DocumentRelation::none :
                    (part.relation == DocumentRelation::child || part.relation == DocumentRelation::descendant));
                require(part.type || !part.id.empty() || !part.classes.empty() || part.state != DocumentState::none || part.root);
            }
        }
        return {};
    });
}

struct DocumentSession::Impl {
    struct Control {
        ComponentHandle handle;
        std::optional<StateField> text, enabled_field;
        const DocumentCommand* command{};
        std::unique_ptr<Button> button;
        std::unique_ptr<TextInput> editor;
        bool enabled{};
    };

    explicit Impl(Document input_document) : document(std::move(input_document)) {}

    void mount(const Setup& setup) {
        checked(detail::validateDocument(document));
        tree = checked(ComponentTree::create());
        std::vector<ComponentSpec> topology;
        topology.reserve(document.nodes.size());
        for (std::size_t index = 0; index < document.nodes.size(); ++index) {
            const auto& node = document.nodes[index];
            topology.push_back({node.parent, static_cast<ComponentType>(node.type) + 1, index + 1});
        }
        order.resize(document.rules.size());
        std::iota(order.begin(), order.end(), std::size_t{});
        std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
            return specificity(document.rules[left]) < specificity(document.rules[right]);
        });

        checked(tree->commit(topology));
        mounted = checked(tree->snapshot());
        store = checked(StateStore::create(*tree));
        input = checked(InputDispatcher::create(*tree));
        if (setup) host = checked(setup(*store));
        const auto bindings = checked(store->inspectBindings());
        std::unordered_set<std::string_view> field_names, commands;
        for (const auto& field : host.fields) {
            require(!field.name.empty() && field_names.insert(field.name).second);
            checked(store->read(field.field));
        }
        for (const auto& command : host.commands)
            require(!command.name.empty() && commands.insert(command.name).second &&
                std::visit([](const auto& handler) { return bool(handler); }, command.handler));

        const auto resolve = [&](const std::string& name, StateType type, bool writable) -> std::optional<StateField> {
            if (name.empty()) return {};
            const auto field = std::ranges::find(host.fields, name, &DocumentField::name);
            require(field != host.fields.end());
            const auto entry = std::ranges::find(bindings.fields, field->field, &StateBindingField::field);
            require(entry != bindings.fields.end() && entry->type == type && (!writable || !entry->computed));
            return field->field;
        };
        controls.resize(document.nodes.size());
        index_by_generation.reserve(controls.size());
        for (std::size_t index = 0; index < controls.size(); ++index) {
            const auto& node = document.nodes[index];
            auto& control = controls[index];
            control.handle = mounted.components[index].handle;
            index_by_generation.emplace(control.handle.generation(), index);
            const bool editable = node.type == DocumentKind::text_box;
            control.text = resolve(node.text_field, StateType::text, editable);
            control.enabled_field = resolve(node.enabled_field, StateType::boolean, false);
            if (!node.command.empty()) {
                const auto command = std::ranges::find(host.commands, node.command, &DocumentCommand::name);
                require(command != host.commands.end() && editable == std::holds_alternative<DocumentCommand::Edit>(command->handler));
                control.command = &*command;
            }
            if (editable) {
                require(control.text && control.command && control.command->writes == node.text_field);
                control.editor = std::make_unique<TextInput>(*input, control.handle);
            }
            if (node.type == DocumentKind::button) control.button = std::make_unique<Button>(*input, control.handle);
            if (control.button || control.editor) bind(index);
        }
        synchronize();
    }

    TextEditCallbacks callbacks(const Control& control) const {
        return {[command = control.command](std::string_view proposal) {
            return guarded([&] { return std::get<DocumentCommand::Edit>(command->handler)(proposal); });
        }, {}, {}};
    }

    void bind(std::size_t index) {
        auto& control = controls[index];
        checked(input->setHandler(control.handle, [this, index](const RoutedEvent& event) noexcept {
            auto& receiver = controls[index];
            auto result = guarded([&]() -> Result<EventReply> {
                if (receiver.editor) return receiver.editor->handle(event, callbacks(receiver));
                const auto action = checked(receiver.button->handle(event));
                if (action.activated && receiver.command) checked(std::get<DocumentCommand::Invoke>(receiver.command->handler)());
                return action.reply;
            });
            if (!result) input_error = result.error();
            return result.value_or(EventReply{});
        }));
    }

    void synchronize() {
        for (std::size_t index = 0; index < controls.size(); ++index) {
            auto& control = controls[index];
            if (!control.button && !control.editor) continue;
            const bool enabled = control.enabled_field ? std::get<bool>(checked(store->read(*control.enabled_field))) :
                document.nodes[index].enabled;
            if (control.enabled != enabled) {
                checked(control.button ? control.button->setEnabled(enabled) : control.editor->setEnabled(enabled));
                control.enabled = enabled;
            }
            if (control.editor) {
                auto value = std::get<std::string>(checked(store->read(*control.text)));
                if (value != control.editor->text()) checked(control.editor->setText(value));
            }
        }
    }

    std::vector<DocumentStyle> cascade() const {
        const auto count = document.nodes.size();
        std::vector<DocumentStyle> selected;
        std::vector<ControlVisualState> visual;
        selected.reserve(count);
        visual.reserve(count);
        const auto hit = pointer_at ? frame.hitTest(*pointer_at) : std::nullopt;
        for (std::size_t index = 0; index < count; ++index) {
            selected.push_back(document.nodes[index].style);
            const auto& control = controls[index];
            if (control.button) visual.push_back(checked(control.button->visualState(hit)));
            else if (control.editor) visual.push_back(checked(control.editor->visualState(hit)));
            else visual.push_back({true, false, false, false});
        }
        std::vector<char> previous(count), current(count), ancestors(count);
        for (const auto index : order) {
            const auto& rule = document.rules[index];
            for (const auto& part : rule.selector) {
                for (std::size_t node = 0; node < count; ++node) {
                    const auto parent = document.nodes[node].parent;
                    ancestors[node] = parent && (previous[*parent] || ancestors[*parent]);
                    const bool related = part.relation == DocumentRelation::none ||
                        (part.relation == DocumentRelation::child && parent && previous[*parent]) ||
                        (part.relation == DocumentRelation::descendant && ancestors[node]);
                    current[node] = related && matches(part, document.nodes[node], visual[node]);
                }
                previous.swap(current);
            }
            for (std::size_t node = 0; node < count; ++node)
                if (previous[node]) combine(selected[node], rule.style);
        }
        return selected;
    }

    void draw(LayoutConstraints offer) {
        synchronize();
        const auto styled = cascade();
        const auto count = controls.size();
        std::vector<LayoutNode> layout;
        std::vector<StyleProperties> styles;
        layout.reserve(count);
        styles.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const auto& node = document.nodes[index];
            const auto& patch = styled[index];
            StyleProperties style;
            if (node.parent) {
                style.ink = styles[*node.parent].ink;
                style.font = styles[*node.parent].font;
            }
            if (patch.color) style.ink = *patch.color;
            if (patch.background) style.fill = *patch.background;
            if (patch.font) style.font.font = *patch.font;
            if (patch.font_size) style.font.em_size = *patch.font_size;
            style.padding = patch.padding.value_or(LayoutInsets{});
            style.gap = patch.gap.value_or(0);
            LayoutStyle box;
            box.kind = layoutKind(node);
            box.grid_columns = node.columns;
            box.preferred_width = patch.width;
            box.preferred_height = patch.height;
            box.padding = style.padding;
            box.gap = style.gap;
            if (box.kind == LayoutKind::leaf) {
                if (!box.preferred_width) box.preferred_width = 160;
                if (!box.preferred_height) box.preferred_height = 24;
            }
            styles.push_back(style);
            layout.push_back({controls[index].handle, box, {}, {}});
        }
        auto positioned = checked(arrangeLayout(checked(measureLayout(mounted, layout, offer))));
        std::vector<std::array<DrawCommand, 2>> draws(count);
        std::vector<RenderNode> paint;
        paint.reserve(count);
        for (const auto& item : positioned.boxes()) {
            const auto index = index_by_generation.at(item.component.generation());
            const auto& style = styles[index];
            const auto& bounds = item.bounds;
            const auto& control = controls[index];
            std::size_t used{};
            if (style.fill.alpha)
                draws[index][used++] = FillRect{{0, 0, bounds.width, bounds.height}, style.fill};
            auto text = control.text ? std::get<std::string>(checked(store->read(*control.text))) : document.nodes[index].text;
            if (!text.empty()) {
                const LayoutRect content{style.padding.left, style.padding.top,
                    std::max(0.0, bounds.width - style.padding.left - style.padding.right),
                    std::max(0.0, bounds.height - style.padding.top - style.padding.bottom)};
                draws[index][used++] = checked(makeText(content, text, style));
            }
            paint.push_back({control.handle, true, std::span(draws[index]).first(used)});
        }
        auto recorded = checked(buildDisplayList(positioned, paint));
        frame = std::move(positioned);
        display = std::move(recorded);
    }

    template<class Function>
    auto operate(Function&& run) -> decltype(run()) {
        if (owner != std::this_thread::get_id()) return std::unexpected(ErrorCode::wrong_thread);
        if (busy) return std::unexpected(ErrorCode::busy);
        busy = true;
        struct Reset { bool& flag; ~Reset() { flag = false; } } reset{busy};
        return guarded(std::forward<Function>(run));
    }

    std::optional<std::size_t> indexOf(std::string_view id) const noexcept {
        const auto node = std::ranges::find(document.nodes, id, &DocumentNode::id);
        if (node == document.nodes.end()) return {};
        return static_cast<std::size_t>(node - document.nodes.begin());
    }

    DispatchResult delivered(Result<DispatchResult> result) {
        auto action = checked(std::move(result));
        if (const auto error = std::exchange(input_error, {})) throw Failure{*error};
        return action;
    }

    const std::thread::id owner = std::this_thread::get_id();
    Document document;
    std::vector<std::size_t> order;
    std::unique_ptr<ComponentTree> tree;
    std::unique_ptr<StateStore> store;
    std::unique_ptr<InputDispatcher> input;
    ComponentSnapshot mounted;
    DocumentHost host;
    std::vector<Control> controls;
    std::unordered_map<std::uint64_t, std::size_t> index_by_generation;
    LayoutFrame frame;
    DisplayList display;
    std::optional<LayoutPoint> pointer_at;
    std::optional<ErrorCode> input_error;
    bool busy{};
};

DocumentSession::DocumentSession(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
DocumentSession::~DocumentSession() noexcept = default;

Result<std::unique_ptr<DocumentSession>> DocumentSession::create(Document document, Setup setup) {
    return guarded([&]() -> Result<std::unique_ptr<DocumentSession>> {
        auto impl = std::make_unique<Impl>(std::move(document));
        impl->mount(setup);
        return std::unique_ptr<DocumentSession>(new DocumentSession(std::move(impl)));
    });
}

Result<void> DocumentSession::render(LayoutConstraints offer) {
    return impl_->operate([&]() -> Result<void> {
        for (const auto& limit : {offer.width, offer.height})
            require(nonnegative(limit.minimum) && (!limit.maximum || (nonnegative(*limit.maximum) && *limit.maximum >= limit.minimum)));
        impl_->draw(offer);
        return {};
    });
}

Result<DispatchResult> DocumentSession::pointer(PointerEvent event) {
    return impl_->operate([&]() -> Result<DispatchResult> {
        require(!impl_->frame.boxes().empty());
        impl_->synchronize();
        impl_->input_error.reset();
        const LayoutPoint point{event.x, event.y};
        const auto action = impl_->delivered(impl_->input->dispatchPointer(event, impl_->frame.hitTest(point)));
        impl_->pointer_at = event.action == PointerAction::cancel ? std::nullopt : std::optional(point);
        return action;
    });
}

Result<DispatchResult> DocumentSession::key(KeyEvent event) {
    return impl_->operate([&]() -> Result<DispatchResult> {
        impl_->synchronize();
        impl_->input_error.reset();
        return impl_->delivered(impl_->input->dispatchKey(event));
    });
}

Result<bool> DocumentSession::editText(std::string_view id, TextEdit edit) {
    return impl_->operate([&]() -> Result<bool> {
        const auto index = impl_->indexOf(id);
        if (!index) return std::unexpected(ErrorCode::stale_handle);
        const auto& control = impl_->controls[*index];
        require(bool(control.editor));
        impl_->synchronize();
        const auto session = checked(control.editor->beginInput());
        if (!session) return std::unexpected(ErrorCode::stale_handle);
        return control.editor->edit(*session, edit, impl_->callbacks(control));
    });
}

Result<std::optional<ComponentHandle>> DocumentSession::find(std::string_view id) const noexcept {
    if (impl_->owner != std::this_thread::get_id()) return std::unexpected(ErrorCode::wrong_thread);
    const auto index = impl_->indexOf(id);
    return index ? std::optional(impl_->controls[*index].handle) : std::nullopt;
}

const LayoutFrame& DocumentSession::layout() const & noexcept { return impl_->frame; }
const DisplayList& DocumentSession::display() const & noexcept { return impl_->display; }

} // namespace graphics_ui::core
