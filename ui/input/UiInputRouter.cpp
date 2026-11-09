#include "ui/input/UiInputRouter.hpp"

#include <algorithm>
#include <array>

namespace sbx::ui {
namespace {

bool hits(const UiLayoutNode& n, i32 x, i32 y) {
    return contains(n.rect, x, y) && contains(n.clip, x, y);
}

// 포인터 아래 가장 위의 컨트롤 (나중 노드가 위). tabs 는 머리 칸 위일 때만 (몸통은 자식의 것 — v0.7 NG-06 (3))
const UiLayoutNode* controlAt(const UiDocumentLayout& d, i32 x, i32 y) {
    for (auto it = d.nodes.rbegin(); it != d.nodes.rend(); ++it) {
        if (isControl(it->node->kind) && hits(*it, x, y) &&
            (it->node->kind != UiKind::Tabs || partAt(*it, x, y) >= 0)) {
            return &*it;
        }
    }
    return nullptr;
}

} // namespace

bool contains(const UiRect& r, i32 x, i32 y) noexcept {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

i32 partAt(const UiLayoutNode& n, i32 x, i32 y) noexcept {
    if (!contains(n.clip, x, y)) {
        return -1;
    }
    for (usize i = 0; i < n.parts.size(); ++i) {
        if (contains(n.parts[i], x, y)) {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

UiPointerRoute routePointer(std::span<const UiDocumentLayout> docs, i32 x, i32 y, bool toolsWantMouse) {
    UiPointerRoute r;
    const auto topFirst = [&](UiLayer layer, auto&& visit) -> bool {
        for (usize i = docs.size(); i-- > 0;) {
            const UiDocumentLayout& d = docs[i];
            if (d.visible && d.document != nullptr && d.document->layer == layer && !d.nodes.empty() && visit(i, d)) {
                return true;
            }
        }
        return false;
    };
    // modal — 보이면 전부 잡는다 (가장 위 modal 의 버튼만)
    if (topFirst(UiLayer::Modal, [&](usize i, const UiDocumentLayout& d) {
            r = {UiPointerRoute::Owner::Modal, true, i, controlAt(d, x, y)};
            return true;
        })) {
        return r;
    }
    if (topFirst(UiLayer::Menu, [&](usize i, const UiDocumentLayout& d) {
            if (!hits(d.nodes.front(), x, y)) {
                return false;
            }
            r = {UiPointerRoute::Owner::Menu, true, i, controlAt(d, x, y)};
            return true;
        })) {
        return r;
    }
    if (toolsWantMouse) {
        r.owner = UiPointerRoute::Owner::Tools;
        return r;
    }
    topFirst(UiLayer::Hud, [&](usize i, const UiDocumentLayout& d) {
        const UiLayoutNode* b = controlAt(d, x, y);
        if (b == nullptr) {
            return false;
        }
        r = {UiPointerRoute::Owner::Hud, true, i, b};
        return true;
    });
    return r;
}

std::vector<UiFocusable> focusOrder(std::span<const UiDocumentLayout> docs,
                                    const std::function<bool(const UiNode& node, i32 listIndex)>& disabled) {
    std::vector<UiFocusable> out;
    for (usize d = 0; d < docs.size(); ++d) {
        if (!docs[d].visible) {
            continue;
        }
        for (const UiLayoutNode& n : docs[d].nodes) {
            if (isControl(n.node->kind) && n.rect.w > 0 && n.rect.h > 0 &&
                !(disabled && disabled(*n.node, n.listIndex))) {
                out.push_back({&n, d});
            }
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const UiFocusable& a, const UiFocusable& b) {
        return a.node->node->tabIndex < b.node->node->tabIndex;
    });
    return out;
}

i32 nearestFocus(std::span<const UiFocusable> order, usize from, UiKey direction) {
    if (from >= order.size()) {
        return -1;
    }
    const auto center = [](const UiRect& r) {
        return std::array<i64, 2>{2 * static_cast<i64>(r.x) + r.w, 2 * static_cast<i64>(r.y) + r.h}; // 두 배 (정수)
    };
    const std::array<i64, 2> c0 = center(order[from].node->rect);
    i32 best = -1;
    i64 bestScore = 0;
    for (usize i = 0; i < order.size(); ++i) {
        if (i == from) {
            continue;
        }
        const std::array<i64, 2> c = center(order[i].node->rect);
        const i64 dx = c[0] - c0[0];
        const i64 dy = c[1] - c0[1];
        i64 along = 0;
        i64 side = 0;
        switch (direction) {
        case UiKey::Up:
            along = -dy;
            side = dx;
            break;
        case UiKey::Down:
            along = dy;
            side = dx;
            break;
        case UiKey::Left:
            along = -dx;
            side = dy;
            break;
        case UiKey::Right:
            along = dx;
            side = dy;
            break;
        default:
            return -1;
        }
        if (along <= 0) {
            continue; // 그 방향이 아니다
        }
        const i64 score = along + 2 * (side < 0 ? -side : side);
        if (best < 0 || score < bestScore) {
            best = static_cast<i32>(i);
            bestScore = score;
        }
    }
    return best;
}

} // namespace sbx::ui
