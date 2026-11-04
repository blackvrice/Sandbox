#include "ui/input/UiInputRouter.hpp"

namespace sbx::ui {
namespace {

bool hits(const UiLayoutNode& n, i32 x, i32 y) {
    return contains(n.rect, x, y) && contains(n.clip, x, y);
}

// 포인터 아래 가장 위의 버튼 (나중 노드가 위)
const UiLayoutNode* buttonAt(const UiDocumentLayout& d, i32 x, i32 y) {
    for (auto it = d.nodes.rbegin(); it != d.nodes.rend(); ++it) {
        if (it->node->kind == UiKind::Button && hits(*it, x, y)) {
            return &*it;
        }
    }
    return nullptr;
}

} // namespace

bool contains(const UiRect& r, i32 x, i32 y) noexcept {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
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
            r = {UiPointerRoute::Owner::Modal, true, i, buttonAt(d, x, y)};
            return true;
        })) {
        return r;
    }
    if (topFirst(UiLayer::Menu, [&](usize i, const UiDocumentLayout& d) {
            if (!hits(d.nodes.front(), x, y)) {
                return false;
            }
            r = {UiPointerRoute::Owner::Menu, true, i, buttonAt(d, x, y)};
            return true;
        })) {
        return r;
    }
    if (toolsWantMouse) {
        r.owner = UiPointerRoute::Owner::Tools;
        return r;
    }
    topFirst(UiLayer::Hud, [&](usize i, const UiDocumentLayout& d) {
        const UiLayoutNode* b = buttonAt(d, x, y);
        if (b == nullptr) {
            return false;
        }
        r = {UiPointerRoute::Owner::Hud, true, i, b};
        return true;
    });
    return r;
}

} // namespace sbx::ui
