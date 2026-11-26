#pragma once

#include <graphics-ui/core/scroll.hpp>

#include <string>
#include <string_view>
#include <variant>

namespace graphics_ui::core {

enum class TableCellKind { integer, text };
using TableCell = std::variant<std::int64_t, std::string>;

struct TableColumn {
    TableCellKind kind;
    double width;
};

struct TableRow {
    ListItemKey key;
    std::vector<TableCell> cells;
};

struct TableSort {
    std::size_t column;
    bool descending{};
};

struct TableFilter {
    std::size_t column;
    std::string substring;
};

struct TableQuery {
    std::optional<TableSort> sort;
    std::optional<TableFilter> filter;
};

struct TableCellView {
    const TableCell* value;
    LayoutRect bounds, hit_bounds;
};

class TableView {
public:
    [[nodiscard]] Result<void> setModel(std::span<const TableColumn> columns, std::span<const TableRow> rows);
    [[nodiscard]] Result<void> setQuery(const TableQuery& query);
    [[nodiscard]] Result<void> resize(LayoutSize viewport, double row_height) noexcept;
    [[nodiscard]] Result<void> scrollTo(LayoutPoint offset) noexcept;
    [[nodiscard]] Result<void> select(std::optional<ListItemKey> key) noexcept;
    [[nodiscard]] Result<void> reveal(ListItemKey key) noexcept { return projection_.reveal(key); }

    [[nodiscard]] std::optional<ListItemKey> selection() const noexcept { return selected_; }
    [[nodiscard]] const ListView& rows() const & noexcept { return projection_; }
    const ListView& rows() const && = delete;
    [[nodiscard]] double horizontalOffset() const noexcept { return horizontal_.offset().x; }
    [[nodiscard]] std::size_t columnCount() const noexcept { return model_.columns.size(); }
    // row is an index in the current filtered and sorted view.
    [[nodiscard]] std::optional<TableCellView> visibleCell(std::size_t row, std::size_t column) const & noexcept;
    std::optional<TableCellView> visibleCell(std::size_t, std::size_t) const && = delete;

private:
    struct Model {
        std::vector<TableColumn> columns;
        std::vector<double> edges;
        std::vector<TableRow> rows;
        std::unordered_map<ListItemKey, std::size_t> indices;
    };
    [[nodiscard]] Result<void> project(const Model& model, const TableQuery& query, std::optional<ListItemKey> selection);
    Model model_;
    TableQuery query_;
    ListView projection_;
    ScrollView horizontal_;
    std::optional<ListItemKey> selected_;
};

struct TreeItem {
    ListItemKey key;
    std::optional<ListItemKey> parent;
    std::string label;
};

struct TreeRowView {
    ListRow row;
    std::string_view label;
    std::size_t depth;
    bool has_children, expanded;
};

class TreeView {
public:
    [[nodiscard]] Result<void> setModel(std::span<const TreeItem> items);
    [[nodiscard]] Result<void> setFilter(std::string_view substring);
    [[nodiscard]] Result<void> setExpanded(ListItemKey key, bool expanded);
    [[nodiscard]] Result<void> select(std::optional<ListItemKey> key) noexcept;
    [[nodiscard]] Result<void> resize(LayoutSize viewport, double row_height) noexcept { return projection_.resize(viewport, row_height); }
    [[nodiscard]] Result<void> scrollTo(double y) noexcept { return projection_.scrollTo(y); }
    [[nodiscard]] Result<void> reveal(ListItemKey key) noexcept { return projection_.reveal(key); }

    [[nodiscard]] std::optional<ListItemKey> selection() const noexcept { return selected_; }
    [[nodiscard]] const ListView& rows() const & noexcept { return projection_; }
    const ListView& rows() const && = delete;
    [[nodiscard]] std::optional<TreeRowView> visibleRow(std::size_t row) const & noexcept;
    std::optional<TreeRowView> visibleRow(std::size_t) const && = delete;

private:
    struct Node {
        TreeItem item;
        std::size_t parent, first_child, next_sibling;
        std::size_t depth{}, subtree_end{};
        bool expanded{};
    };
    struct Model {
        std::vector<Node> nodes;
        std::vector<std::size_t> preorder;
        std::unordered_map<ListItemKey, std::size_t> indices;
    };
    [[nodiscard]] Result<void> project(const Model& model, std::string_view substring, std::optional<ListItemKey> selection);
    Model model_;
    std::string filter_;
    ListView projection_;
    std::optional<ListItemKey> selected_;
};

} // namespace graphics_ui::core
