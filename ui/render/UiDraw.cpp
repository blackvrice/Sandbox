#include "ui/render/UiDraw.hpp"

#include <algorithm>
#include <cmath>

#include "ui/input/UiTextEdit.hpp"

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

UiRect intersect(const UiRect& a, const UiRect& b) {
    const i32 x0 = std::max(a.x, b.x);
    const i32 y0 = std::max(a.y, b.y);
    const i32 x1 = std::min(a.x + a.w, b.x + b.w);
    const i32 y1 = std::min(a.y + a.h, b.y + b.h);
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

i32 roundPx(f32 v) {
    return static_cast<i32>(std::floor(v + 0.5f));
}

// 고른 번호 — 반올림, 범위 밖 · NaN 이면 -1
i32 indexOf(f32 value, usize count) {
    if (!(value > -0.5f) || !(value < static_cast<f32>(count) - 0.5f)) {
        return -1;
    }
    return roundPx(value);
}

// 글자 칸 (v0.7 NG-06 (2)): 상자 · 테두리 → 글자 영역 (상자 안 여백) 에 줄마다. 포커스면 캐럿이 보이게 민다 — 세로는
// 캐럿 줄이 마지막 보이는 줄이 되게, 가로는 캐럿 x 가 영역의 오른 끝 안에 들게 (모든 줄을 같은 만큼)
void drawTextInput(const UiLayoutNode& ln, const UiDrawOptions& options, IUiRenderer& renderer, bool off, bool focus,
                   const std::string& placeholder) {
    const auto tone = [&](UiColor c) { return off ? dim(c) : c; };
    const UiRect box = inset(ln.rect, static_cast<i32>(ln.padding + 0.5f));
    renderer.fillRect(box, tone(kControlColor), ln.clip);
    renderer.strokeRect(box, tone(kFieldBorderColor), options.scale, ln.clip);
    const UiRect area = inset(box, static_cast<i32>(textInputMargin(ln.fontSize)));
    const UiRect clip = intersect(ln.clip, area);
    const f32 lh = ln.lineHeight > 0 ? ln.lineHeight : ln.fontSize * 1.25f;
    const i32 lineH = static_cast<i32>(std::ceil(lh));
    const std::string value = options.textValueOf ? options.textValueOf(*ln.node, ln.listIndex) : std::string();
    if (value.empty() && !placeholder.empty()) {
        renderer.text({area.x, area.y, area.w, lineH}, placeholder, ln.fontSize, tone(kPlaceholderColor), clip);
    }
    const UiLineColumn at = lineColumnOf(value, focus ? options.caret : 0);
    const usize visible = std::max<usize>(1, static_cast<usize>(static_cast<f32>(area.h) / lh));
    const usize first = focus && at.line >= visible ? at.line - visible + 1 : 0;
    // 캐럿 줄의 캐럿 앞 글자 너비 → 가로 밀기
    std::string_view caretLine = value;
    for (usize i = 0; i < at.line; ++i) {
        caretLine.remove_prefix(caretLine.find('\n') + 1);
    }
    caretLine = caretLine.substr(0, caretLine.find('\n'));
    f32 caretX = 0;
    if (focus && options.measure != nullptr) {
        caretX = options.measure->measure(caretLine.substr(0, byteOffsetOf(caretLine, at.column)), ln.fontSize)[0];
    }
    const i32 caretW = std::max(1, roundPx(options.scale));
    const i32 scroll = std::max(0, roundPx(caretX) + caretW - area.w);
    std::string_view rest = value;
    for (usize i = 0;; ++i) {
        const usize nl = rest.find('\n');
        const std::string_view line = rest.substr(0, nl);
        rest = nl == std::string_view::npos ? std::string_view() : rest.substr(nl + 1);
        if (i >= first) {
            const i32 y = area.y + roundPx(static_cast<f32>(i - first) * lh);
            if (y >= area.y + area.h) {
                break;
            }
            if (!line.empty()) {
                renderer.text({area.x - scroll, y, area.w + scroll, lineH}, line, ln.fontSize, tone(kTextColor), clip);
            }
        }
        if (nl == std::string_view::npos) {
            break;
        }
    }
    if (focus && !off && options.measure != nullptr) {
        const i32 y = area.y + roundPx(static_cast<f32>(at.line - first) * lh);
        renderer.fillRect({area.x + roundPx(caretX) - scroll, y, caretW, lineH}, kTextColor, clip);
    }
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
        const auto option = [&](usize i) { return options.optionOf ? options.optionOf(n, i) : n.options[i]; };
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
        case UiKind::TextInput:
            drawTextInput(ln, options, renderer, off, options.focused == &n && options.focusedIndex == ln.listIndex,
                          label());
            break;
        case UiKind::Dropdown: {
            const UiRect box = inset(ln.rect, pad);
            renderer.fillRect(box, tone(kControlColor), ln.clip);
            renderer.strokeRect(box, tone(kFieldBorderColor), options.scale, ln.clip);
            const UiRect area = inset(box, static_cast<i32>(textInputMargin(ln.fontSize)));
            const i32 arrowCol = static_cast<i32>(controlBox(ln.fontSize));
            if (const i32 sel = indexOf(valueNow(), n.options.size()); sel >= 0) {
                const i32 lineH = static_cast<i32>(std::ceil(ln.lineHeight > 0 ? ln.lineHeight : ln.fontSize * 1.25f));
                renderer.text({area.x, area.y, std::max(0, area.w - arrowCol), lineH}, option(static_cast<usize>(sel)),
                              ln.fontSize, tone(kTextColor), intersect(ln.clip, area));
            }
            // 화살표 — 오른쪽 칸 가운데의 납작한 사각형 (글꼴에 ▼ 가 없을 수 있다)
            const i32 aw = std::max(2, arrowCol / 2);
            const i32 ah = std::max(2, arrowCol / 4);
            renderer.fillRect({area.x + area.w - arrowCol + (arrowCol - aw) / 2, area.y + (area.h - ah) / 2, aw, ah},
                              tone(kTextColor), ln.clip);
            break;
        }
        case UiKind::Tabs: {
            if (style != nullptr && style->color) {
                renderer.fillRect(ln.rect, *style->color, ln.clip);
            }
            border();
            const i32 sel = indexOf(valueNow(), n.options.size());
            const i32 margin = static_cast<i32>(textInputMargin(ln.fontSize));
            const i32 line = std::max(1, static_cast<i32>(2 * options.scale + 0.5f));
            for (usize i = 0; i < ln.parts.size() && i < n.options.size(); ++i) {
                const UiRect& part = ln.parts[i];
                const bool chosen = static_cast<i32>(i) == std::max(0, sel);
                renderer.fillRect(part, tone(chosen ? brighter(kControlColor) : kControlColor), ln.clip);
                renderer.text(inset(part, margin), option(i), ln.fontSize, tone(kTextColor), ln.clip);
                if (chosen) {
                    renderer.fillRect({part.x, part.y + part.h - line, part.w, line}, tone(kCheckColor), ln.clip);
                }
            }
            break;
        }
        }
        if (options.focused == &n && options.focusedIndex == ln.listIndex) {
            // 포커스 고리 (v0.7 NG-06 (1)) — tabs 는 머리 칸들을 (몸통의 컨트롤과 겹치지 않게)
            UiRect ring = ln.rect;
            if (n.kind == UiKind::Tabs && !ln.parts.empty()) {
                const UiRect& a = ln.parts.front();
                const UiRect& b = ln.parts.back();
                ring = {a.x, a.y, b.x + b.w - a.x, a.h};
            }
            renderer.strokeRect(ring, kFocusColor, 2 * options.scale, ln.clip);
        }
    }
}

void drawOptionList(const UiRect& list, const std::vector<std::string>& texts, i32 selected, i32 highlighted,
                    f32 fontSize, f32 scale, IUiRenderer& renderer) {
    if (texts.empty() || list.h <= 0) {
        return;
    }
    renderer.fillRect(list, kListColor, list);
    const i32 row = list.h / static_cast<i32>(texts.size());
    const i32 margin = static_cast<i32>(textInputMargin(fontSize));
    for (usize i = 0; i < texts.size(); ++i) {
        const UiRect r{list.x, list.y + static_cast<i32>(i) * row, list.w, row};
        if (static_cast<i32>(i) == highlighted) {
            renderer.fillRect(r, kHighlightColor, list);
        } else if (static_cast<i32>(i) == selected) {
            renderer.fillRect(r, brighter(kListColor), list);
        }
        renderer.text(inset(r, margin), texts[i], fontSize, kTextColor, list);
    }
    renderer.strokeRect(list, kFieldBorderColor, scale, list);
}

} // namespace sbx::ui
