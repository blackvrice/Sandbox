#include <graphics-ui/core/style.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <numeric>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

namespace graphics_ui::core {
namespace {

StyleError error(StyleErrorCode code, std::string_view name = {},
                 std::optional<std::size_t> rule = {}, ComponentHandle component = {}) {
    return {code, std::string(name), rule, component};
}

bool validName(std::string_view name) noexcept {
    return !name.empty() && std::ranges::all_of(name, [](char character) {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') || character == '.' || character == '_' || character == '-';
    });
}

bool valid(double length) noexcept { return std::isfinite(length) && length >= 0; }

bool valid(LayoutInsets padding) noexcept {
    return valid(padding.left) && valid(padding.top) && valid(padding.right) && valid(padding.bottom) &&
           std::isfinite(padding.left + padding.right) && std::isfinite(padding.top + padding.bottom);
}

bool valid(RenderColor) noexcept { return true; }
bool valid(StyleFont font) noexcept { return font.font != 0 && std::isfinite(font.em_size) && font.em_size > 0; }

template<class T>
StyleResult<void> applyValue(T& destination, const std::optional<StyleValue<T>>& source,
                             std::string_view property, const std::vector<ThemeToken>& tokens) {
    if (!source) return {};
    if (const auto* literal = std::get_if<T>(&*source)) {
        if (!valid(*literal)) return std::unexpected(error(StyleErrorCode::invalid_value, property));
        destination = *literal;
        return {};
    }
    const auto* reference = std::get_if<StyleTokenRef<T>>(&*source);
    if (!reference) return std::unexpected(error(StyleErrorCode::invalid_value, property));
    if (!validName(reference->name)) return std::unexpected(error(StyleErrorCode::invalid_name, reference->name));
    const auto token = std::ranges::lower_bound(tokens, reference->name, {}, &ThemeToken::name);
    if (token == tokens.end() || token->name != reference->name)
        return std::unexpected(error(StyleErrorCode::unknown_token, reference->name));
    if (const auto* value = std::get_if<T>(&token->value)) {
        destination = *value;
        return {};
    }
    return std::unexpected(error(StyleErrorCode::token_type_mismatch, reference->name));
}

StyleResult<void> apply(StyleProperties& properties, const StylePatch& patch, const std::vector<ThemeToken>& tokens) {
    if (auto result = applyValue(properties.padding, patch.padding, "padding", tokens); !result) return result;
    if (auto result = applyValue(properties.gap, patch.gap, "gap", tokens); !result) return result;
    if (auto result = applyValue(properties.fill, patch.fill, "fill", tokens); !result) return result;
    if (auto result = applyValue(properties.ink, patch.ink, "ink", tokens); !result) return result;
    return applyValue(properties.font, patch.font, "font", tokens);
}

StyleResult<void> validateTheme(ThemeDefinition& theme) {
    std::ranges::sort(theme.tokens, {}, &ThemeToken::name);
    for (std::size_t index = 0; index < theme.tokens.size(); ++index) {
        const auto& token = theme.tokens[index];
        if (!validName(token.name)) return std::unexpected(error(StyleErrorCode::invalid_name, token.name));
        if (index && token.name == theme.tokens[index - 1].name)
            return std::unexpected(error(StyleErrorCode::duplicate_token, token.name));
        if (token.value.valueless_by_exception() || !std::visit([](auto value) { return valid(value); }, token.value))
            return std::unexpected(error(StyleErrorCode::invalid_value, token.name));
    }

    for (std::size_t index = 0; index < theme.rules.size(); ++index) {
        const auto& rule = theme.rules[index];
        if (rule.layer < StyleLayer::role || rule.layer > StyleLayer::state)
            return std::unexpected(error(StyleErrorCode::invalid_rule, rule.name, index));
        if (!validName(rule.name))
            return std::unexpected(error(StyleErrorCode::invalid_name, rule.name, index));
        if (!valid(rule.width.minimum) || (rule.width.maximum &&
            (!valid(*rule.width.maximum) || *rule.width.maximum <= rule.width.minimum)))
            return std::unexpected(error(StyleErrorCode::invalid_width, rule.name, index));
        StyleProperties validated;
        if (auto applied = apply(validated, rule.patch, theme.tokens); !applied) {
            applied.error().rule_index = index;
            return applied;
        }
    }

    // Check exact duplicates without changing the authoring order of overlapping rules.
    std::vector<std::size_t> indices(theme.rules.size());
    std::iota(indices.begin(), indices.end(), std::size_t{0});
    const auto key = [&](std::size_t index) {
        const auto& rule = theme.rules[index];
        return std::tie(rule.layer, rule.name, rule.width.minimum, rule.width.maximum);
    };
    std::ranges::sort(indices, [&](auto left, auto right) {
        return key(left) == key(right) ? left < right : key(left) < key(right);
    });
    for (std::size_t index = 1; index < indices.size(); ++index) {
        if (key(indices[index - 1]) == key(indices[index]))
            return std::unexpected(error(StyleErrorCode::duplicate_rule, theme.rules[indices[index]].name, indices[index]));
    }
    return {};
}

StyleChange difference(const StyleProperties& before, const StyleProperties& after) noexcept {
    const bool typography = before.font != after.font;
    return {typography || before.padding != after.padding || before.gap != after.gap,
            typography || before.fill != after.fill || before.ink != after.ink};
}

} // namespace

const ResolvedStyleNode* StyleFrame::find(ComponentHandle component) const & noexcept {
    const auto found = std::ranges::lower_bound(nodes_, component.generation(), {},
        [](const ResolvedStyleNode& node) { return node.component.generation(); });
    return found != nodes_.end() && found->component == component ? &*found : nullptr;
}

StyleResult<StyleFrame> StyleEngine::resolve(const ThemeDefinition& theme, std::uint64_t theme_revision,
    const ComponentSnapshot& tree, std::span<const StyleNode> nodes, double viewport_width) const {
    if (!valid(viewport_width)) return std::unexpected(error(StyleErrorCode::invalid_width));
    std::unordered_map<std::uint64_t, const ComponentInfo*> mounted;
    mounted.reserve(tree.components.size());
    const auto owner = tree.components.empty() ? 0 : tree.components.front().handle.owner_;
    for (const auto& component : tree.components) {
        if (!owner || component.handle.owner_ != owner || !component.handle.generation() ||
            !mounted.emplace(component.handle.generation(), &component).second)
            return std::unexpected(error(StyleErrorCode::invalid_component, {}, {}, component.handle));
    }

    std::vector<const StyleNode*> ordered;
    ordered.reserve(nodes.size());
    for (const auto& node : nodes) ordered.push_back(&node);
    std::ranges::sort(ordered, {}, [](const StyleNode* node) { return node->component.generation(); });
    StyleFrame next;
    next.tree_revision_ = tree.revision;
    next.theme_revision_ = theme_revision;
    next.nodes_.reserve(nodes.size());
    std::uint64_t previous_generation{};
    for (const auto* node : ordered) {
        const auto generation = node->component.generation();
        const auto current = mounted.find(generation);
        if (current == mounted.end() || current->second->handle != node->component)
            return std::unexpected(error(StyleErrorCode::invalid_component, {}, {}, node->component));
        if (previous_generation == generation)
            return std::unexpected(error(StyleErrorCode::duplicate_component, {}, {}, node->component));
        previous_generation = generation;
        for (const auto name : {node->role, node->class_name, node->state})
            if (!name.empty() && !validName(name))
                return std::unexpected(error(StyleErrorCode::invalid_name, name, {}, node->component));
        auto properties = node->defaults;
        if (!valid(properties.padding) || !valid(properties.gap) || !valid(properties.font))
            return std::unexpected(error(StyleErrorCode::invalid_value, {}, {}, node->component));

        const std::string_view selectors[]{node->role, node->class_name, node->state};
        for (const auto layer : {StyleLayer::role, StyleLayer::class_name, StyleLayer::state}) {
            const auto selected = selectors[static_cast<std::size_t>(layer)];
            if (selected.empty()) continue;
            for (std::size_t index = 0; index < theme.rules.size(); ++index) {
                const auto& rule = theme.rules[index];
                if (rule.layer != layer || rule.name != selected || viewport_width < rule.width.minimum ||
                    (rule.width.maximum && viewport_width >= *rule.width.maximum)) continue;
                if (auto applied = apply(properties, rule.patch, theme.tokens); !applied) {
                    applied.error().rule_index = index;
                    applied.error().component = node->component;
                    return std::unexpected(std::move(applied.error()));
                }
            }
        }
        if (auto applied = apply(properties, node->overrides, theme.tokens); !applied) {
            applied.error().component = node->component;
            return std::unexpected(std::move(applied.error()));
        }
        if (!current->second->effectively_visible) continue;
        const auto* previous = frame_.find(node->component);
        const auto changed = previous ? difference(previous->properties, properties) : StyleChange{true, true};
        next.changed_.layout |= changed.layout;
        next.changed_.paint |= changed.paint;
        next.nodes_.push_back({node->component, properties, changed});
    }
    if (next.nodes_.size() < frame_.nodes_.size()) next.changed_ = {true, true};
    return next;
}

StyleResult<void> StyleEngine::update(const ComponentSnapshot& tree, std::span<const StyleNode> nodes,
                                    double viewport_width) {
    try {
        auto next = resolve(theme_, frame_.themeRevision(), tree, nodes, viewport_width);
        if (!next) return std::unexpected(std::move(next.error()));
        frame_ = std::move(*next);
        return {};
    } catch (const std::bad_alloc&) {
        return std::unexpected(StyleError{StyleErrorCode::out_of_memory, {}, {}, {}});
    } catch (const std::length_error&) {
        return std::unexpected(StyleError{StyleErrorCode::limit_exceeded, {}, {}, {}});
    }
}

StyleResult<void> StyleEngine::reload(const ThemeDefinition& theme, const ComponentSnapshot& tree,
                                    std::span<const StyleNode> nodes, double viewport_width) {
    if (frame_.themeRevision() == std::numeric_limits<std::uint64_t>::max())
        return std::unexpected(error(StyleErrorCode::limit_exceeded));
    try {
        auto candidate = theme;
        if (auto valid = validateTheme(candidate); !valid) return valid;
        auto next = resolve(candidate, frame_.themeRevision() + 1, tree, nodes, viewport_width);
        if (!next) return std::unexpected(std::move(next.error()));
        theme_ = std::move(candidate);
        frame_ = std::move(*next);
        return {};
    } catch (const std::bad_alloc&) {
        return std::unexpected(StyleError{StyleErrorCode::out_of_memory, {}, {}, {}});
    } catch (const std::length_error&) {
        return std::unexpected(StyleError{StyleErrorCode::limit_exceeded, {}, {}, {}});
    }
}

} // namespace graphics_ui::core
