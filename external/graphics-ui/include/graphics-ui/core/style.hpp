#pragma once

#include <graphics-ui/core/render.hpp>

#include <string_view>

namespace graphics_ui::core {

struct StyleFont {
    FontId font = 1;
    double em_size = 12;
    bool operator==(const StyleFont&) const = default;
};

template<class T>
struct StyleTokenRef { std::string name; };

template<class T>
using StyleValue = std::variant<T, StyleTokenRef<T>>;

struct ThemeToken {
    std::string name;
    std::variant<double, LayoutInsets, RenderColor, StyleFont> value;
};

struct StyleProperties {
    LayoutInsets padding;
    double gap{};
    RenderColor fill{0, 0, 0, 0};
    RenderColor ink{255, 255, 255};
    StyleFont font;
};

struct StylePatch {
    std::optional<StyleValue<LayoutInsets>> padding;
    std::optional<StyleValue<double>> gap;
    std::optional<StyleValue<RenderColor>> fill, ink;
    std::optional<StyleValue<StyleFont>> font;
};

enum class StyleLayer { role, class_name, state };

struct StyleWidth {
    double minimum{};
    std::optional<double> maximum; // Exclusive logical viewport width.
    bool operator==(const StyleWidth&) const = default;
};

struct StyleRule {
    StyleLayer layer;
    std::string name;
    StyleWidth width;
    StylePatch patch;
};

struct ThemeDefinition {
    std::vector<ThemeToken> tokens;
    std::vector<StyleRule> rules;
};

struct StyleNode {
    ComponentHandle component;
    std::string_view role, class_name, state;
    StyleProperties defaults;
    StylePatch overrides;
};

enum class StyleErrorCode {
    invalid_width, invalid_name, invalid_value, invalid_rule, duplicate_rule,
    duplicate_token, unknown_token, token_type_mismatch, invalid_component, duplicate_component,
    out_of_memory, limit_exceeded,
};

struct StyleError {
    StyleErrorCode code;
    std::string name;
    std::optional<std::size_t> rule_index;
    ComponentHandle component;
};

template<class T>
using StyleResult = std::expected<T, StyleError>;

struct StyleChange {
    bool layout{}, paint{};
    bool operator==(const StyleChange&) const = default;
};

struct ResolvedStyleNode {
    ComponentHandle component;
    StyleProperties properties;
    StyleChange changed;
};

class StyleFrame {
public:
    [[nodiscard]] std::uint64_t treeRevision() const noexcept { return tree_revision_; }
    [[nodiscard]] std::uint64_t themeRevision() const noexcept { return theme_revision_; }
    [[nodiscard]] StyleChange changed() const noexcept { return changed_; }
    [[nodiscard]] std::span<const ResolvedStyleNode> nodes() const & noexcept { return nodes_; }
    std::span<const ResolvedStyleNode> nodes() const && = delete;
    [[nodiscard]] const ResolvedStyleNode* find(ComponentHandle component) const & noexcept;
    const ResolvedStyleNode* find(ComponentHandle) const && = delete;

private:
    friend class StyleEngine;
    std::uint64_t tree_revision_{}, theme_revision_{};
    StyleChange changed_;
    std::vector<ResolvedStyleNode> nodes_;
};

class StyleEngine {
public:
    // Use on one thread. Each successful call replaces the frame and invalidates borrowed node references.
    [[nodiscard]] StyleResult<void> update(const ComponentSnapshot& tree, std::span<const StyleNode> nodes,
        double viewport_width);
    // Publish a copied theme and its resolved frame together, after validating all rules and supplied nodes.
    [[nodiscard]] StyleResult<void> reload(const ThemeDefinition& theme, const ComponentSnapshot& tree,
        std::span<const StyleNode> nodes, double viewport_width);
    [[nodiscard]] const StyleFrame& frame() const & noexcept { return frame_; }
    const StyleFrame& frame() const && = delete;

private:
    [[nodiscard]] StyleResult<StyleFrame> resolve(const ThemeDefinition& theme, std::uint64_t theme_revision,
        const ComponentSnapshot& tree, std::span<const StyleNode> nodes, double viewport_width) const;
    ThemeDefinition theme_;
    StyleFrame frame_;
};

} // namespace graphics_ui::core
