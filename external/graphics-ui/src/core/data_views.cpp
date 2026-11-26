#include <graphics-ui/core/data_views.hpp>

#include "layout_geometry.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>

namespace graphics_ui::core {
namespace {
constexpr auto no_index = std::numeric_limits<std::size_t>::max();

Result<void> syncSelection(ListView& projection, std::optional<ListItemKey> selection) noexcept {
    if (selection && !projection.indexOf(*selection)) selection.reset();
    return projection.select(selection);
}

Result<void> publish(ListView& projection, std::span<const ListItemKey> keys, std::optional<ListItemKey> selection) {
    if (auto updated = projection.setItems(keys); !updated) return updated;
    return syncSelection(projection, selection);
}

bool validCell(const TableCell& cell, TableCellKind kind) noexcept {
    if (kind == TableCellKind::integer) return std::holds_alternative<std::int64_t>(cell);
    const auto* text = std::get_if<std::string>(&cell);
    return text && detail::validUtf8(*text);
}
} // namespace

Result<void> TableView::project(const Model& model, const TableQuery& query, std::optional<ListItemKey> selection) {
    if ((query.sort && query.sort->column >= model.columns.size()) ||
        (query.filter && (query.filter->column >= model.columns.size() ||
            model.columns[query.filter->column].kind != TableCellKind::text ||
            !detail::validUtf8(query.filter->substring))))
        return std::unexpected(ErrorCode::invalid_argument);

    std::vector<std::size_t> order;
    order.reserve(model.rows.size());
    for (std::size_t index = 0; index < model.rows.size(); ++index) {
        if (!query.filter || std::get<std::string>(model.rows[index].cells[query.filter->column]).contains(query.filter->substring))
            order.push_back(index);
    }
    if (query.sort) {
        const auto sort = *query.sort;
        std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
            if (sort.descending) std::swap(left, right);
            return model.rows[left].cells[sort.column] < model.rows[right].cells[sort.column];
        });
    }
    std::vector<ListItemKey> keys;
    keys.reserve(order.size());
    for (const auto index : order) keys.push_back(model.rows[index].key);
    return publish(projection_, keys, selection);
}

Result<void> TableView::setModel(std::span<const TableColumn> columns, std::span<const TableRow> rows) {
    if (columns.empty() && !rows.empty()) return std::unexpected(ErrorCode::invalid_argument);
    try {
        Model next;
        next.columns.assign(columns.begin(), columns.end());
        next.edges.push_back(0);
        for (const auto column : columns) {
            if (!std::isfinite(column.width) || column.width <= 0 ||
                (column.kind != TableCellKind::integer && column.kind != TableCellKind::text))
                return std::unexpected(ErrorCode::invalid_argument);
            const auto end = next.edges.back() + column.width;
            if (!std::isfinite(end) || end == next.edges.back()) return std::unexpected(ErrorCode::limit_exceeded);
            next.edges.push_back(end);
        }
        next.indices.reserve(rows.size());
        for (std::size_t index = 0; index < rows.size(); ++index) {
            const auto& row = rows[index];
            if (row.cells.size() != columns.size() || !next.indices.emplace(row.key, index).second)
                return std::unexpected(ErrorCode::invalid_argument);
            for (std::size_t column = 0; column < columns.size(); ++column)
                if (!validCell(row.cells[column], columns[column].kind)) return std::unexpected(ErrorCode::invalid_argument);
        }
        next.rows.assign(rows.begin(), rows.end());
        auto horizontal = horizontal_;
        const auto viewport = projection_.scroll().viewport();
        if (auto resized = horizontal.resize(viewport, {next.edges.back(), viewport.height}); !resized) return resized;
        const auto selection = selected_ && next.indices.contains(*selected_) ? selected_ : std::nullopt;
        if (auto applied = project(next, query_, selection); !applied) return applied;
        model_ = std::move(next);
        horizontal_ = horizontal;
        selected_ = selection;
        return {};
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    } catch (const std::length_error&) {
        return std::unexpected(ErrorCode::limit_exceeded);
    }
}

Result<void> TableView::setQuery(const TableQuery& query) {
    try {
        auto next = query;
        if (auto applied = project(model_, next, selected_); !applied) return applied;
        query_ = std::move(next);
        return {};
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    } catch (const std::length_error&) {
        return std::unexpected(ErrorCode::limit_exceeded);
    }
}

Result<void> TableView::resize(LayoutSize viewport, double row_height) noexcept {
    auto horizontal = horizontal_;
    if (auto resized = horizontal.resize(viewport, {horizontal_.content().width, viewport.height}); !resized) return resized;
    if (auto resized = projection_.resize(viewport, row_height); !resized) return resized;
    horizontal_ = horizontal;
    return {};
}

Result<void> TableView::scrollTo(LayoutPoint offset) noexcept {
    auto horizontal = horizontal_;
    if (auto moved = horizontal.scrollTo({offset.x, 0}); !moved) return moved;
    if (auto moved = projection_.scrollTo(offset.y); !moved) return moved;
    horizontal_ = horizontal;
    return {};
}

Result<void> TableView::select(std::optional<ListItemKey> key) noexcept {
    if (key && !model_.indices.contains(*key)) return std::unexpected(ErrorCode::stale_handle);
    if (auto synced = syncSelection(projection_, key); !synced) return synced;
    selected_ = key;
    return {};
}

std::optional<TableCellView> TableView::visibleCell(std::size_t row, std::size_t column) const & noexcept {
    if (column >= model_.columns.size()) return std::nullopt;
    const auto visible = projection_.visibleRow(row);
    if (!visible) return std::nullopt;
    const auto index = model_.indices.find(visible->key)->second;
    const auto left = model_.edges[column] - horizontal_.offset().x;
    const auto right = model_.edges[column + 1] - horizontal_.offset().x;
    const LayoutRect bounds{left, visible->bounds.y, right - left, visible->bounds.height};
    const auto viewport = horizontal_.viewport();
    const auto hit = detail::intersectRects(bounds, {0, 0, viewport.width, viewport.height});
    if (hit.width <= 0 || hit.height <= 0) return std::nullopt;
    return TableCellView{&model_.rows[index].cells[column], bounds, hit};
}

Result<void> TreeView::project(const Model& model, std::string_view substring, std::optional<ListItemKey> selection) {
    try {
        std::vector<ListItemKey> keys;
        if (substring.empty()) {
            for (std::size_t index = 0; index < model.preorder.size();) {
                const auto& node = model.nodes[model.preorder[index]];
                keys.push_back(node.item.key);
                index = node.expanded ? index + 1 : node.subtree_end;
            }
        } else {
            std::vector<unsigned char> included(model.nodes.size());
            // Propagate matching descendants to their ancestors before emitting the filtered preorder.
            for (auto position = model.preorder.rbegin(); position != model.preorder.rend(); ++position) {
                const auto index = *position;
                const auto& node = model.nodes[index];
                if (node.item.label.contains(substring)) included[index] = true;
                if (included[index] && node.parent != no_index) included[node.parent] = true;
            }
            for (const auto index : model.preorder)
                if (included[index]) keys.push_back(model.nodes[index].item.key);
        }
        return publish(projection_, keys, selection);
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    } catch (const std::length_error&) {
        return std::unexpected(ErrorCode::limit_exceeded);
    }
}

Result<void> TreeView::setModel(std::span<const TreeItem> items) {
    try {
        Model next;
        next.nodes.reserve(items.size());
        next.indices.reserve(items.size());
        for (std::size_t index = 0; index < items.size(); ++index) {
            const auto& item = items[index];
            if (!detail::validUtf8(item.label) || !next.indices.emplace(item.key, index).second)
                return std::unexpected(ErrorCode::invalid_argument);
            const auto previous = model_.indices.find(item.key);
            const bool expanded = previous != model_.indices.end() && model_.nodes[previous->second].expanded;
            next.nodes.push_back({item, no_index, no_index, no_index, 0, 0, expanded});
        }
        auto first_root = no_index, last_root = no_index;
        std::vector<std::size_t> tails(items.size(), no_index);
        for (std::size_t index = 0; index < items.size(); ++index) {
            auto& node = next.nodes[index];
            if (node.item.parent) {
                const auto parent = next.indices.find(*node.item.parent);
                if (parent == next.indices.end() || parent->second == index)
                    return std::unexpected(ErrorCode::invalid_argument);
                node.parent = parent->second;
            }
            auto& first = node.parent == no_index ? first_root : next.nodes[node.parent].first_child;
            auto& last = node.parent == no_index ? last_root : tails[node.parent];
            if (last == no_index) first = index;
            else next.nodes[last].next_sibling = index;
            last = index;
        }

        // A parent cycle cannot be reached from a root. Count the full iterative preorder before publication.
        next.preorder.reserve(items.size());
        for (auto index = first_root; index != no_index;) {
            auto& node = next.nodes[index];
            node.depth = node.parent == no_index ? 0 : next.nodes[node.parent].depth + 1;
            next.preorder.push_back(index);
            if (node.first_child != no_index) {
                index = node.first_child;
                continue;
            }
            node.expanded = false;
            while (index != no_index) {
                auto& finished = next.nodes[index];
                finished.subtree_end = next.preorder.size();
                if (finished.next_sibling != no_index) {
                    index = finished.next_sibling;
                    break;
                }
                index = finished.parent;
            }
        }
        if (next.preorder.size() != items.size()) return std::unexpected(ErrorCode::dependency_cycle);
        const auto selection = selected_ && next.indices.contains(*selected_) ? selected_ : std::nullopt;
        if (auto applied = project(next, filter_, selection); !applied) return applied;
        model_ = std::move(next);
        selected_ = selection;
        return {};
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    } catch (const std::length_error&) {
        return std::unexpected(ErrorCode::limit_exceeded);
    }
}

Result<void> TreeView::setFilter(std::string_view substring) {
    if (!detail::validUtf8(substring)) return std::unexpected(ErrorCode::invalid_argument);
    if (substring == filter_) return {};
    try {
        std::string next(substring);
        if (auto applied = project(model_, next, selected_); !applied) return applied;
        filter_.swap(next);
        return {};
    } catch (const std::bad_alloc&) {
        return std::unexpected(ErrorCode::out_of_memory);
    } catch (const std::length_error&) {
        return std::unexpected(ErrorCode::limit_exceeded);
    }
}

Result<void> TreeView::setExpanded(ListItemKey key, bool expanded) {
    const auto index = model_.indices.find(key);
    if (index == model_.indices.end()) return std::unexpected(ErrorCode::stale_handle);
    auto& node = model_.nodes[index->second];
    if (node.expanded == expanded) return {};
    if (node.first_child == no_index) return std::unexpected(ErrorCode::invalid_argument);
    node.expanded = expanded;
    if (filter_.empty() && projection_.indexOf(key)) {
        if (auto updated = project(model_, filter_, selected_); !updated) {
            node.expanded = !expanded;
            return updated;
        }
    }
    return {};
}

Result<void> TreeView::select(std::optional<ListItemKey> key) noexcept {
    if (key && !model_.indices.contains(*key)) return std::unexpected(ErrorCode::stale_handle);
    if (auto synced = syncSelection(projection_, key); !synced) return synced;
    selected_ = key;
    return {};
}

std::optional<TreeRowView> TreeView::visibleRow(std::size_t row) const & noexcept {
    const auto visible = projection_.visibleRow(row);
    if (!visible) return std::nullopt;
    const auto index = model_.indices.find(visible->key)->second;
    const auto& node = model_.nodes[index];
    const auto next = projection_.itemKey(row + 1);
    const bool expanded = next && model_.nodes[model_.indices.find(*next)->second].parent == index;
    return TreeRowView{*visible, node.item.label, node.depth, node.first_child != no_index, expanded};
}

} // namespace graphics_ui::core
