#include "ui/layout/UiLayout.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace sbx::ui {
namespace {

constexpr usize kMaxLayoutNodes = 4096;

struct Box {
    f32 x0 = 0;
    f32 y0 = 0;
    f32 x1 = 0;
    f32 y1 = 0;
};

i32 roundEdge(f32 v) {
    return static_cast<i32>(std::floor(v + 0.5f));
}

UiRect toRect(const Box& b) {
    const i32 x0 = roundEdge(b.x0);
    const i32 y0 = roundEdge(b.y0);
    return {x0, y0, std::max(0, roundEdge(b.x1) - x0), std::max(0, roundEdge(b.y1) - y0)};
}

UiRect intersect(const UiRect& a, const UiRect& b) {
    const i32 x0 = std::max(a.x, b.x);
    const i32 y0 = std::max(a.y, b.y);
    const i32 x1 = std::min(a.x + a.w, b.x + b.w);
    const i32 y1 = std::min(a.y + a.h, b.y + b.h);
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

class Layouter {
public:
    Layouter(const UiLayoutOptions& options, const ITextMeasure& measure, std::vector<UiLayoutNode>& out)
        : m_opt(options), m_measure(measure), m_out(out), m_scale(std::clamp(options.scale, 0.5f, 4.0f)) {}

    void place(const UiNode& n, const Box& parent, const UiRect& clip, u32 depth, i32 listIndex, const Box* fixed) {
        if (m_out.size() >= kMaxLayoutNodes || !shown(n)) {
            return;
        }
        Box b;
        if (fixed != nullptr) {
            b = *fixed; // 쌓는 부모가 정한 자리
        } else {
            std::array<f32, 2> fit = outerSize(n, listIndex, std::nullopt);
            axis(parent.x0, parent.x1, n.anchorMin[0], n.anchorMax[0], n.offset[0], n.offset[2],
                 n.size ? std::optional<f32>((*n.size)[0]) : std::nullopt, fit[0], b.x0, b.x1);
            if (textual(n) && n.overflow != UiOverflow::Clip) {
                // 쓸 수 있는 너비 = 정해진 가로, 내용에 맞춘 가로면 부모 안쪽 오른 끝까지 (v0.7 M4-T2)
                b.x1 = std::min(b.x1, std::max(b.x0, parent.x1));
                fit = outerSize(n, listIndex, b.x1 - b.x0);
            }
            axis(parent.y0, parent.y1, n.anchorMin[1], n.anchorMax[1], n.offset[1], n.offset[3],
                 n.size ? std::optional<f32>((*n.size)[1]) : std::nullopt, fit[1], b.y0, b.y1);
        }
        const UiRect rect = toRect(b);
        UiLayoutNode out{&n, rect, clip, depth, listIndex, fontSize(n), padding(n), {}, 0};
        if (textual(n) && n.overflow != UiOverflow::Clip) {
            // 넘침은 확정된 사각형의 안쪽 너비로 (재기에서 같은 너비를 썼으면 같은 결과)
            const Shaped t = shape(n, listIndex, static_cast<f32>(rect.w) - 2 * padding(n));
            out.fontSize = t.fontSize;
            out.lines = t.lines;
            out.lineHeight = t.lineHeight;
        }
        m_out.push_back(std::move(out));
        const UiRect inner = intersect(clip, rect);
        // 자식의 부모 사각형 = 반올림한 사각형에서 여백만큼 안쪽 (반올림 전 값을 쓰면 자식이 1 픽셀씩 어긋난다)
        const f32 pad = padding(n);
        const Box content{static_cast<f32>(rect.x) + pad, static_cast<f32>(rect.y) + pad,
                          static_cast<f32>(rect.x + rect.w) - pad, static_cast<f32>(rect.y + rect.h) - pad};
        if (n.kind == UiKind::List) {
            if (n.children.empty()) {
                return;
            }
            const usize count = m_opt.listCount ? m_opt.listCount(n) : 0;
            stackChildren(n, content, inner, depth, count, listIndex);
            return;
        }
        if (n.stack != UiStack::None) {
            stackChildren(n, content, inner, depth, 0, listIndex);
            return;
        }
        for (const UiNode& c : n.children) {
            place(c, content, inner, depth + 1, listIndex, nullptr);
        }
    }

private:
    // 한 축의 가장자리 (위 머리말 규칙)
    void axis(f32 p0, f32 p1, f32 aMin, f32 aMax, f32 offMin, f32 offMax, std::optional<f32> size, f32 fit, f32& e0,
              f32& e1) const {
        const f32 len = p1 - p0;
        e0 = p0 + aMin * len + offMin * m_scale;
        e1 = p0 + aMax * len + offMax * m_scale;
        if (size) {
            e1 = e0 + *size * m_scale;
        } else if (e1 <= e0) {
            e1 = e0 + fit;
        }
    }

    // 쌓기 · list: children (list 면 item 을 count 번) 을 주 축으로 차례로
    void stackChildren(const UiNode& n, const Box& content, const UiRect& clip, u32 depth, usize count,
                       i32 outerIndex) {
        const bool row = n.stack == UiStack::Row;
        const f32 gap = n.gap * m_scale;
        f32 cursor = row ? content.x0 : content.y0;
        const f32 cross0 = row ? content.y0 : content.x0;
        const f32 cross1 = row ? content.y1 : content.x1;
        const auto one = [&](const UiNode& c, i32 index) {
            if (!shown(c)) {
                return;
            }
            std::optional<f32> limit;
            if (textual(c) && c.overflow != UiOverflow::Clip) { // v0.7 M4-T2 — row 는 남은 자리, column 은 안쪽 너비
                limit = std::max(0.0f, row ? content.x1 - cursor : cross1 - cross0);
            }
            const std::array<f32, 2> s = outerSize(c, index, limit);
            const f32 along = row ? s[0] : s[1];
            const f32 cross = row ? s[1] : s[0];
            f32 c0 = cross0;
            f32 c1 = cross0 + cross;
            switch (n.align) {
            case UiAlign::Start:
                break;
            case UiAlign::Center:
                c0 = cross0 + (cross1 - cross0 - cross) * 0.5f;
                c1 = c0 + cross;
                break;
            case UiAlign::End:
                c0 = cross1 - cross;
                c1 = cross1;
                break;
            case UiAlign::Stretch:
                c1 = cross1;
                break;
            }
            const Box fixed = row ? Box{cursor, c0, cursor + along, c1} : Box{c0, cursor, c1, cursor + along};
            place(c, fixed, clip, depth + 1, index, &fixed);
            cursor += along + gap;
        };
        if (n.kind == UiKind::List) {
            for (usize i = 0; i < count && m_out.size() < kMaxLayoutNodes; ++i) {
                one(n.children.front(), static_cast<i32>(i));
            }
            return;
        }
        for (const UiNode& c : n.children) {
            one(c, outerIndex); // list 항목 안의 쌓기 — 그 항목 번호를 이어 준다
        }
    }

    // 노드의 바깥 크기 — size 가 있으면 그것, 없으면 내용
    // maxWidth = 넘침 글자가 쓸 수 있는 바깥 너비 (v0.7 M4-T2 — 없으면 자연 크기)
    std::array<f32, 2> outerSize(const UiNode& n, i32 listIndex, std::optional<f32> maxWidth) const {
        if (n.size) {
            const std::array<f32, 2> fixed{(*n.size)[0] * m_scale, (*n.size)[1] * m_scale};
            return fixed;
        }
        return contentSize(n, listIndex, maxWidth);
    }

    struct Shaped {
        f32 width = 0;
        f32 height = 0;
        f32 fontSize = 0;
        std::vector<std::string> lines; // wrap 만
        f32 lineHeight = 0;
    };

    // 글자 노드의 모양 — innerWidth = 여백을 뺀 쓸 수 있는 너비 (없으면 자연 크기)
    Shaped shape(const UiNode& n, i32 listIndex, std::optional<f32> innerWidth) const {
        const std::string text = m_opt.textOf ? m_opt.textOf(n, listIndex) : n.text;
        Shaped out;
        out.fontSize = fontSize(n);
        const std::array<f32, 2> natural = m_measure.measure(text, out.fontSize);
        out.width = natural[0];
        out.height = natural[1];
        if (!innerWidth || n.overflow == UiOverflow::Clip) {
            return out;
        }
        const f32 limit = std::max(0.0f, *innerWidth);
        if (n.overflow == UiOverflow::Fit) {
            if (natural[0] > limit && natural[0] > 0) {
                out.fontSize = out.fontSize * std::max(kMinFitScale, limit / natural[0]);
                const std::array<f32, 2> m = m_measure.measure(text, out.fontSize);
                out.width = m[0];
                out.height = m[1];
            }
            return out;
        }
        out.lines = wrapText(text, out.fontSize, limit, m_measure);
        out.lineHeight = m_measure.measure("", out.fontSize)[1];
        out.width = 0;
        for (const std::string& line : out.lines) {
            out.width = std::max(out.width, m_measure.measure(line, out.fontSize)[0]);
        }
        out.height = out.lineHeight * static_cast<f32>(std::max<usize>(1, out.lines.size()));
        return out;
    }

    std::array<f32, 2> contentSize(const UiNode& n, i32 listIndex, std::optional<f32> maxWidth) const {
        const f32 pad = padding(n) * 2;
        switch (n.kind) {
        case UiKind::Text:
        case UiKind::Button: {
            const Shaped t = shape(n, listIndex, maxWidth ? std::optional<f32>(*maxWidth - pad) : std::nullopt);
            return {std::ceil(t.width) + pad, std::ceil(t.height) + pad};
        }
        case UiKind::Image:
        case UiKind::Progress:
            return {pad, pad};
        case UiKind::Panel:
        case UiKind::List:
            break;
        }
        const bool row = n.stack == UiStack::Row;
        const f32 gap = n.gap * m_scale;
        f32 along = 0;
        f32 cross = 0;
        usize k = 0;
        const auto add = [&](const UiNode& c, i32 index) {
            if (!shown(c)) {
                return;
            }
            const std::array<f32, 2> s = outerSize(c, index, std::nullopt);
            if (n.stack == UiStack::None && n.kind != UiKind::List) { // 겹쳐 놓는 panel — 가장 큰 자식
                along = std::max(along, s[0]);
                cross = std::max(cross, s[1]);
                return;
            }
            along += (k++ > 0 ? gap : 0) + (row ? s[0] : s[1]);
            cross = std::max(cross, row ? s[1] : s[0]);
        };
        if (n.kind == UiKind::List) {
            const usize count = m_opt.listCount && !n.children.empty() ? m_opt.listCount(n) : 0;
            for (usize i = 0; i < std::min(count, kMaxLayoutNodes); ++i) {
                add(n.children.front(), static_cast<i32>(i));
            }
        } else {
            for (const UiNode& c : n.children) {
                add(c, listIndex);
            }
        }
        if (n.stack == UiStack::None && n.kind != UiKind::List) {
            return {along + pad, cross + pad};
        }
        return row ? std::array<f32, 2>{along + pad, cross + pad} : std::array<f32, 2>{cross + pad, along + pad};
    }

    [[nodiscard]] bool shown(const UiNode& n) const { return !m_opt.hidden || !m_opt.hidden(n); }
    [[nodiscard]] static bool textual(const UiNode& n) { return n.kind == UiKind::Text || n.kind == UiKind::Button; }
    [[nodiscard]] const UiStyle* style(const UiNode& n) const {
        if (m_opt.styles == nullptr || n.style.empty()) {
            return nullptr;
        }
        const auto it = m_opt.styles->find(n.style);
        return it == m_opt.styles->end() ? nullptr : &it->second;
    }
    [[nodiscard]] f32 padding(const UiNode& n) const {
        const UiStyle* s = style(n);
        return std::max(n.padding, s != nullptr ? s->padding : 0.0f) * m_scale;
    }
    [[nodiscard]] f32 fontSize(const UiNode& n) const {
        const UiStyle* s = style(n);
        return (s != nullptr && s->fontSize > 0 ? s->fontSize : kDefaultFontSize) * m_scale;
    }

    const UiLayoutOptions& m_opt;
    const ITextMeasure& m_measure;
    std::vector<UiLayoutNode>& m_out;
    f32 m_scale;
};

} // namespace

std::array<f32, 2> FixedTextMeasure::measure(std::string_view text, f32 fontSize) const {
    usize codePoints = 0;
    for (const char c : text) {
        codePoints += (static_cast<unsigned char>(c) & 0xC0u) != 0x80u ? 1 : 0; // UTF-8 이어지는 바이트가 아닌 것
    }
    return {static_cast<f32>(codePoints) * m_advance * fontSize, m_lineHeight * fontSize};
}

std::vector<std::string> wrapText(std::string_view text, f32 fontSize, f32 maxWidth, const ITextMeasure& measure) {
    std::vector<std::string> lines;
    const auto width = [&](std::string_view s) { return measure.measure(s, fontSize)[0]; };
    // 코드 포인트 경계 (UTF-8 이어지는 바이트가 아닌 자리)
    const auto nextCp = [](std::string_view s, usize i) {
        ++i;
        while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0u) == 0x80u) {
            ++i;
        }
        return i;
    };
    usize start = 0;
    while (start <= text.size()) {
        usize end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string_view paragraph = text.substr(start, end - start);
        std::string line;
        usize i = 0;
        while (i < paragraph.size()) {
            usize sp = paragraph.find(' ', i);
            if (sp == std::string_view::npos) {
                sp = paragraph.size();
            }
            const std::string_view word = paragraph.substr(i, sp - i);
            const std::string candidate = line.empty() ? std::string(word) : line + ' ' + std::string(word);
            if (width(candidate) <= maxWidth || (line.empty() && word.empty())) {
                line = candidate;
            } else if (!line.empty() && width(word) <= maxWidth) {
                lines.push_back(std::move(line));
                line = std::string(word);
            } else {
                // 한 낱말이 한 줄보다 길다 — 코드 포인트마다 (앞 줄을 먼저 닫는다)
                if (!line.empty()) {
                    lines.push_back(std::move(line));
                    line.clear();
                }
                usize c = 0;
                while (c < word.size()) {
                    const usize next = nextCp(word, c);
                    const std::string more = line + std::string(word.substr(c, next - c));
                    if (!line.empty() && width(more) > maxWidth) {
                        lines.push_back(std::move(line));
                        line = std::string(word.substr(c, next - c));
                    } else {
                        line = more;
                    }
                    c = next;
                }
            }
            i = sp + 1;
        }
        lines.push_back(std::move(line));
        start = end + 1;
    }
    return lines;
}

std::vector<UiLayoutNode> layoutDocument(const UiDocument& document, const UiLayoutOptions& options,
                                         const ITextMeasure& measure) {
    std::vector<UiLayoutNode> out;
    const Box screen{0, 0, static_cast<f32>(std::max(0, options.width)), static_cast<f32>(std::max(0, options.height))};
    Layouter(options, measure, out).place(document.root, screen, toRect(screen), 0, -1, nullptr);
    return out;
}

} // namespace sbx::ui
