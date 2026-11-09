#include "ui/render/UiDraw.hpp"

#include <algorithm>
#include <cmath>

namespace sbx::ui {
namespace {

// 꺼진 컨트롤 — 알파를 절반
UiColor dim(UiColor c) {
    c[3] = static_cast<u8>(c[3] / 2);
    return c;
}

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
        const bool off = options.disabledOf && options.disabledOf(n, ln.listIndex);
        const auto tone = [&](UiColor c) { return off ? dim(c) : c; };
        const auto valueNow = [&] { return options.valueOf ? options.valueOf(n, ln.listIndex) : n.value.number; };
        const i32 pad = static_cast<i32>(ln.padding + 0.5f);
        // overflow wrap 이면 배치가 나눈 줄을 줄마다 (v0.7 M4-T2), 아니면 한 줄
        const auto drawText = [&](i32 inner) {
            const UiRect r = inset(ln.rect, inner);
            if (ln.lines.empty()) {
                renderer.text(r, label(), ln.fontSize, tone(kTextColor), ln.clip);
                return;
            }
            for (usize i = 0; i < ln.lines.size(); ++i) {
                const i32 y = r.y + static_cast<i32>(std::floor(static_cast<f32>(i) * ln.lineHeight + 0.5f));
                const i32 h = static_cast<i32>(std::ceil(ln.lineHeight));
                renderer.text({r.x, y, r.w, h}, ln.lines[i], ln.fontSize, tone(kTextColor), ln.clip);
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
            if (options.pressed == &n && options.pressedIndex == ln.listIndex && !off) {
                fill = brighter(fill);
            }
            renderer.fillRect(ln.rect, tone(fill), ln.clip);
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
        case UiKind::Checkbox:
        case UiKind::Toggle: {
            const UiRect r = inset(ln.rect, pad);
            const i32 box = static_cast<i32>(controlBox(ln.fontSize));
            const i32 y = r.y + (r.h - box) / 2;
            const bool on = valueNow() != 0.0f;
            if (n.kind == UiKind::Checkbox) {
                const UiRect b{r.x, y, box, box};
                renderer.fillRect(b, tone(kControlColor), ln.clip);
                renderer.strokeRect(b, tone(kTextColor), options.scale, ln.clip);
                if (on) {
                    renderer.fillRect(inset(b, std::max(2, box / 4)), tone(kCheckColor), ln.clip);
                }
            } else {
                renderer.fillRect({r.x, y, box * 2, box}, tone(on ? kCheckColor : kControlColor), ln.clip);
                renderer.fillRect(inset({on ? r.x + box : r.x, y, box, box}, std::max(1, box / 8)), tone(kTextColor),
                                  ln.clip);
            }
            const i32 used =
                (n.kind == UiKind::Toggle ? box * 2 : box) + static_cast<i32>(std::ceil(ln.fontSize * 0.5f));
            if (const std::string t = label(); !t.empty()) {
                renderer.text({r.x + used, r.y, std::max(0, r.w - used), r.h}, t, ln.fontSize, tone(kTextColor),
                              ln.clip);
            }
            break;
        }
        case UiKind::Slider: {
            const UiRect r = inset(ln.rect, pad);
            const f32 span = n.max.number - n.min;
            f32 t = span > 0 ? (valueNow() - n.min) / span : 0.0f;
            t = t >= 0.0f ? std::min(t, 1.0f) : 0.0f; // NaN · 음수 = 0
            const i32 track = std::max(2, r.h / 4);
            const UiRect line{r.x, r.y + (r.h - track) / 2, r.w, track};
            renderer.fillRect(line, tone(kControlColor), ln.clip);
            const i32 knob = std::max(4, r.h / 2);
            const i32 knobX = r.x + static_cast<i32>(static_cast<f32>(std::max(0, r.w - knob)) * t + 0.5f);
            if (knobX > r.x) {
                renderer.fillRect({r.x, line.y, knobX - r.x, track}, tone(kCheckColor), ln.clip);
            }
            renderer.fillRect({knobX, r.y, knob, r.h}, tone(kTextColor), ln.clip);
            break;
        }
        }
        if (options.focused == &n && options.focusedIndex == ln.listIndex) {
            renderer.strokeRect(ln.rect, kFocusColor, 2 * options.scale, ln.clip); // 포커스 고리 (v0.7 NG-06 (1))
        }
    }
}

} // namespace sbx::ui
