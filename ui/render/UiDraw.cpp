#include "ui/render/UiDraw.hpp"

#include <algorithm>
#include <cmath>

namespace sbx::ui {
namespace {

UiColor brighter(UiColor c) {
    for (usize i = 0; i < 3; ++i) {
        c[i] = static_cast<u8>(std::min(255, c[i] + 40));
    }
    return c;
}

UiColor darker(UiColor c) {
    for (usize i = 0; i < 3; ++i) {
        c[i] = static_cast<u8>(c[i] / 3);
    }
    return c;
}

UiRect inset(const UiRect& r, i32 by) {
    return {r.x + by, r.y + by, std::max(0, r.w - 2 * by), std::max(0, r.h - 2 * by)};
}

} // namespace

void drawLayout(const std::vector<UiLayoutNode>& nodes, const UiDrawOptions& options, IUiRenderer& renderer) {
    for (const UiLayoutNode& ln : nodes) {
        const UiNode& n = *ln.node;
        const UiStyle* style = nullptr;
        if (options.styles != nullptr && !n.style.empty()) {
            if (const auto it = options.styles->find(n.style); it != options.styles->end()) {
                style = &it->second;
            }
        }
        const auto border = [&] {
            if (style != nullptr && style->borderColor && style->borderWidth > 0) {
                renderer.strokeRect(ln.rect, *style->borderColor, style->borderWidth * options.scale, ln.clip);
            }
        };
        const auto label = [&]() -> std::string { return options.textOf ? options.textOf(n, ln.listIndex) : n.text; };
        const i32 pad = static_cast<i32>(ln.padding + 0.5f);
        // overflow wrap 이면 배치가 나눈 줄을 줄마다 (v0.7 M4-T2), 아니면 한 줄
        const auto drawText = [&](i32 inner) {
            const UiRect r = inset(ln.rect, inner);
            if (ln.lines.empty()) {
                renderer.text(r, label(), ln.fontSize, kTextColor, ln.clip);
                return;
            }
            for (usize i = 0; i < ln.lines.size(); ++i) {
                const i32 y = r.y + static_cast<i32>(std::floor(static_cast<f32>(i) * ln.lineHeight + 0.5f));
                const i32 h = static_cast<i32>(std::ceil(ln.lineHeight));
                renderer.text({r.x, y, r.w, h}, ln.lines[i], ln.fontSize, kTextColor, ln.clip);
            }
        };
        switch (n.kind) {
        case UiKind::Panel:
        case UiKind::List:
            if (style != nullptr && style->color) {
                renderer.fillRect(ln.rect, *style->color, ln.clip);
            }
            border();
            break;
        case UiKind::Button: {
            UiColor fill = style != nullptr && style->color ? *style->color : kButtonColor;
            if (options.pressed == &n && options.pressedIndex == ln.listIndex) {
                fill = brighter(fill);
            }
            renderer.fillRect(ln.rect, fill, ln.clip);
            border();
            drawText(pad);
            break;
        }
        case UiKind::Text:
            drawText(pad);
            break;
        case UiKind::Progress: {
            const UiColor fill = style != nullptr && style->color ? *style->color : kProgressColor;
            f32 t = 0;
            if (options.progressOf) {
                t = options.progressOf(n, ln.listIndex);
            } else if (n.value.bind.empty() && n.max.bind.empty() && n.max.number > 0) {
                t = n.value.number / n.max.number;
            }
            t = t >= 0.0f ? std::min(t, 1.0f) : 0.0f; // NaN · 음수 = 0
            renderer.fillRect(ln.rect, darker(fill), ln.clip);
            const UiRect done{ln.rect.x, ln.rect.y, static_cast<i32>(static_cast<f32>(ln.rect.w) * t + 0.5f),
                              ln.rect.h};
            if (done.w > 0) {
                renderer.fillRect(done, fill, ln.clip);
            }
            border();
            break;
        }
        case UiKind::Image:
            renderer.image(ln.rect, n.material, ln.clip);
            border();
            break;
        }
    }
}

} // namespace sbx::ui
