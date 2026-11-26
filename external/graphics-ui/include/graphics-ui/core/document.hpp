#pragma once

#include <graphics-ui/core/controls.hpp>
#include <graphics-ui/core/state_store.hpp>

namespace graphics_ui::core {

enum class DocumentKind { window, panel, stack_panel, grid, flex_panel, text_block, text_box, button };
enum class DocumentRelation { none, child, descendant };
enum class DocumentState { none, disabled, focus, hover, pressed };

struct DocumentSelectorPart {
    std::optional<DocumentKind> type;
    std::string id;
    std::vector<std::string> classes;
    DocumentState state = DocumentState::none;
    bool root{};
    DocumentRelation relation = DocumentRelation::none; // Relation to the preceding part.
};

struct DocumentStyle {
    std::optional<double> width, height, gap, font_size;
    std::optional<LayoutInsets> padding;
    std::optional<RenderColor> color, background;
    std::optional<FontId> font;
};

struct DocumentRule {
    std::vector<DocumentSelectorPart> selector;
    DocumentStyle style;
};

struct DocumentNode {
    std::string id;
    std::optional<std::size_t> parent;
    DocumentKind type = DocumentKind::panel;
    std::vector<std::string> classes;
    // Instance roots can also match IDs from the component template.
    std::vector<std::string> template_ids;
    bool horizontal{};
    std::uint32_t columns = 1;
    DocumentStyle style;
    std::string text, text_field, enabled_field, command;
    bool enabled = true;
};

struct Document {
    std::vector<DocumentNode> nodes;
    std::vector<DocumentRule> rules;
};

struct DocumentField {
    std::string name;
    StateField field;
};

struct DocumentCommand {
    using Invoke = std::function<Result<void>()>;
    using Edit = std::function<Result<void>(std::string_view)>;
    std::string name;
    std::variant<Invoke, Edit> handler;
    // Edit handlers publish the proposed text to this registered field on success.
    std::string writes;
};

struct DocumentHost {
    std::vector<DocumentField> fields;
    std::vector<DocumentCommand> commands;
};

class DocumentSession {
public:
    using Setup = std::function<Result<DocumentHost>(StateStore&)>;

    // The session, its controls, and callbacks belong to the creating UI thread.
    [[nodiscard]] static Result<std::unique_ptr<DocumentSession>> create(Document document, Setup setup = {});
    DocumentSession(const DocumentSession&) = delete;
    DocumentSession& operator=(const DocumentSession&) = delete;
    ~DocumentSession() noexcept;

    // A failed render keeps the previous layout and display list available.
    [[nodiscard]] Result<void> render(LayoutConstraints offer = {});
    [[nodiscard]] Result<DispatchResult> pointer(PointerEvent event);
    [[nodiscard]] Result<DispatchResult> key(KeyEvent event);
    [[nodiscard]] Result<bool> editText(std::string_view id, TextEdit edit);
    [[nodiscard]] Result<std::optional<ComponentHandle>> find(std::string_view id) const noexcept;

    [[nodiscard]] const LayoutFrame& layout() const & noexcept;
    [[nodiscard]] const DisplayList& display() const & noexcept;

private:
    struct Impl;
    explicit DocumentSession(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace graphics_ui::core
