#include "editor/gui/StatusBar.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

#include "editor/gui/PanelKit.hpp"

namespace sbx::editor::gui {

using kit::fit;
using kit::kFont;
using kit::rc;

namespace {

constexpr double kEm = 12.5;
constexpr double kNoticeEm = 13.0;
constexpr double kLineH = 19.0;
constexpr double kGap = 8.0;

// 배속 — ImGui 의 "%g" 와 같은 모양 (1 · 0.5 · 2)
std::string speedText(f32 speed) {
    std::string s = std::format("{:.3f}", static_cast<double>(speed));
    while (!s.empty() && s.back() == '0') {
        s.pop_back();
    }
    if (!s.empty() && s.back() == '.') {
        s.pop_back();
    }
    return s;
}

// UTF-8 코드 포인트 하나의 바이트 수 (잘린 바이트는 1)
usize codePointLength(std::string_view s, usize at) {
    const auto c = static_cast<unsigned char>(s[at]);
    const usize n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
    return std::min(n, s.size() - at);
}

} // namespace

// ---- StatusBar ----

std::string StatusBar::summary() const {
    if (!m_model.inWorld) {
        return std::format("프로젝트가 열려 있지 않다 · 배치 {}", m_model.layout);
    }
    std::string s = std::format("{} · 배치 {} · 선택 {} · tick {}", m_model.place, m_model.layout, m_model.selected, m_model.tick);
    if (m_model.paused) {
        s += " · 일시정지";
    } else if (m_model.tps > 0) {
        s += std::format(" · {:.0f} TPS · ×{}", m_model.tps, speedText(m_model.speed));
    }
    return s;
}

std::string StatusBar::noticesLabel() const {
    return m_model.notices == 0 ? std::string() : std::format("알림 {}", m_model.notices);
}

Expected<void> StatusBar::mount(uihost::UiHost& host) {
    // 뿌리 · 글 · 알림 단추 · 모드 배지
    const std::array<gui::ComponentSpec, 4> specs{{{std::nullopt, 1, 0, true}, {0, 2, 1, true}, {0, 3, 2, true}, {0, 2, 3, true}}};
    auto h = kit::commitTree(host, specs);
    if (!h) {
        return std::unexpected(h.error());
    }
    m_root = (*h)[0];
    m_text = (*h)[1];
    m_noticesNode = (*h)[2];
    m_mode = (*h)[3];
    m_notices = kit::bindButton(host, m_noticesNode, [this] {
        if (m_model.notices > 0 && m_model.noticesPane != 0) {
            m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::ShowPane, {}, static_cast<int>(m_model.noticesPane));
        }
    });
    return {};
}

Expected<void> StatusBar::layout(uihost::UiHost& host, const gui::ComponentSnapshot&, std::vector<gui::LayoutNode>& out) {
    const gui::LayoutSize vp = host.viewport();
    const std::string notices = noticesLabel();
    const double noticesW = notices.empty() ? 0.0 : host.text().measure(notices, kEm).width + 16;
    const double modeW = m_model.mode.empty() ? 0.0 : host.text().measure(m_model.mode, kEm).width + 20;
    gui::LayoutNode root;
    root.component = m_root;
    root.style.kind = gui::LayoutKind::row;
    root.style.padding = {kGap, 2, kGap, 2};
    root.style.gap = kGap;
    out.push_back(root);
    const double inner = fit(vp.width - 2 * kGap);
    const double textW = fit(inner - noticesW - modeW - 2 * kGap);
    const std::array<std::pair<gui::ComponentHandle, double>, 3> parts{{{m_text, textW}, {m_noticesNode, noticesW}, {m_mode, modeW}}};
    for (const auto& [node, w] : parts) {
        gui::LayoutNode n;
        n.component = node;
        n.style.preferred_width = w;
        n.style.preferred_height = fit(vp.height - 4);
        out.push_back(n);
    }
    return {};
}

Expected<void> StatusBar::paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) {
    const services::StudioColors& c = services::studioColors(m_ctx.theme);
    for (const gui::LayoutBox& box : frame.boxes()) {
        const double w = box.bounds.width;
        const double h = box.bounds.height;
        const double ty = fit((h - kEm * 1.25) / 2);
        if (box.component == m_root) {
            // 바탕 · 위 테두리 (ImGui 상태 표시줄의 background 띠)
            out.add(box.component, {gui::FillRect{{0, 0, w, h}, rc(c.background)}, gui::FillRect{{0, 0, w, 1}, rc(c.border)}});
        } else if (box.component == m_text) {
            // 일시정지는 경고색 (ImGui 와 같다) — 앞 글은 흐리게
            std::vector<gui::DrawCommand> d;
            const std::string all = summary();
            const std::string_view pausedTag = " · 일시정지";
            if (m_model.inWorld && m_model.paused && all.ends_with(pausedTag)) {
                const std::string head = all.substr(0, all.size() - pausedTag.size());
                const double hw = std::min(host.text().measure(head, kEm).width, w);
                d.push_back(gui::DrawText{{0, ty, hw, kEm * 1.25}, kFont, kEm, head, rc(c.muted)});
                d.push_back(gui::DrawText{{hw, ty, fit(w - hw), kEm * 1.25}, kFont, kEm, std::string(pausedTag), rc(c.warning)});
            } else {
                d.push_back(gui::DrawText{{0, ty, w, kEm * 1.25}, kFont, kEm, all, rc(c.muted)});
            }
            out.add(box.component, std::move(d), true);
        } else if (box.component == m_noticesNode) {
            if (w > 0) {
                out.add(box.component, kit::button(host, box, kit::visualOf(*m_notices, host), noticesLabel(), kEm, false, c));
            }
        } else if (box.component == m_mode) {
            if (w > 0) {
                out.add(box.component, {gui::FillRect{{0, 1, w, fit(h - 2)}, m_model.live ? rc(c.accent) : rc(c.elevated)},
                                        gui::DrawText{{10, ty, fit(w - 20), kEm * 1.25}, kFont, kEm, m_model.mode,
                                                      m_model.live ? rc(c.accentText) : rc(c.muted)}},
                        true);
            }
        }
    }
    return {};
}

// ---- NoticesPanel ----

void NoticesPanel::setNotices(std::vector<std::string> notices) {
    if (notices != m_notices) {
        m_notices = std::move(notices);
        m_wrapWidth = -1; // 다시 감싼다
    }
}

std::vector<std::string> NoticesPanel::wrap(const uihost::ITextMeasure& text, std::string_view line, double em, double width) {
    std::vector<std::string> out;
    std::string cur;
    const auto fits = [&](std::string_view s) { return text.measure(s, em).width <= width; };
    usize i = 0;
    while (i < line.size()) {
        // 다음 낱말 (앞 공백 포함)
        usize j = i;
        while (j < line.size() && line[j] == ' ') {
            ++j;
        }
        while (j < line.size() && line[j] != ' ') {
            ++j;
        }
        const std::string_view word = line.substr(i, j - i);
        i = j;
        if (fits(cur + std::string(word))) {
            cur += word;
            continue;
        }
        if (!cur.empty()) {
            out.push_back(std::move(cur));
            cur.clear();
        }
        // 새 줄은 앞 공백 없이. 낱말 하나가 넓으면 글자 단위로
        std::string_view rest = word.substr(std::min(word.find_first_not_of(' '), word.size()));
        while (!rest.empty()) {
            if (fits(rest)) {
                cur = std::string(rest);
                break;
            }
            usize cut = 0;
            while (cut < rest.size()) {
                const usize n = codePointLength(rest, cut);
                if (cut > 0 && !fits(rest.substr(0, cut + n))) {
                    break;
                }
                cut += n;
            }
            out.emplace_back(rest.substr(0, cut));
            rest = rest.substr(cut);
        }
    }
    if (!cur.empty() || out.empty()) {
        out.push_back(std::move(cur));
    }
    return out;
}

Expected<void> NoticesPanel::mount(uihost::UiHost& host) {
    const std::array<gui::ComponentSpec, 2> specs{{{std::nullopt, 1, 0, true}, {0, 2, 1, true}}};
    auto h = kit::commitTree(host, specs);
    if (!h) {
        return std::unexpected(h.error());
    }
    m_root = (*h)[0];
    m_list = (*h)[1];
    (void)host.setWheelHandler(m_list, [this](f32, f32 dy) {
        const double content = static_cast<double>(m_wrapped.size()) * kLineH;
        m_scroll = std::clamp(m_scroll - static_cast<double>(dy) * kLineH * 3.0, 0.0, std::max(0.0, content - m_viewH));
        return true;
    });
    return {};
}

Expected<void> NoticesPanel::layout(uihost::UiHost& host, const gui::ComponentSnapshot&, std::vector<gui::LayoutNode>& out) {
    const gui::LayoutSize vp = host.viewport();
    const double width = fit(vp.width - 24);
    if (width != m_wrapWidth) { // 폭 · 알림이 바뀐 때만 감싼다 (글꼴 측정)
        m_wrapWidth = width;
        m_wrapped.clear();
        const double bulletW = host.text().measure("• ", kNoticeEm).width;
        for (const std::string& n : m_notices) {
            const auto lines = wrap(host.text(), n, kNoticeEm, fit(width - bulletW));
            for (usize i = 0; i < lines.size(); ++i) {
                m_wrapped.push_back((i == 0 ? "• " : "  ") + lines[i]);
            }
        }
    }
    gui::LayoutNode root;
    root.component = m_root;
    root.style.kind = gui::LayoutKind::column;
    root.style.padding = {12, 10, 12, 10};
    out.push_back(root);
    gui::LayoutNode list;
    list.component = m_list;
    list.style.preferred_width = width;
    list.style.preferred_height = fit(vp.height - 20);
    out.push_back(list);
    m_viewH = fit(vp.height - 20);
    m_scroll = std::clamp(m_scroll, 0.0, std::max(0.0, static_cast<double>(m_wrapped.size()) * kLineH - m_viewH));
    return {};
}

Expected<void> NoticesPanel::paint(uihost::UiHost&, const gui::LayoutFrame& frame, uihost::PaintList& out) {
    const services::StudioColors& c = services::studioColors(m_ctx.theme);
    for (const gui::LayoutBox& box : frame.boxes()) {
        const double w = box.bounds.width;
        const double h = box.bounds.height;
        if (box.component == m_root) {
            out.add(box.component, {gui::FillRect{{0, 0, w, h}, rc(c.surface)}});
        } else if (box.component == m_list) {
            std::vector<gui::DrawCommand> d;
            if (m_wrapped.empty()) {
                d.push_back(gui::DrawText{{0, 0, w, 18}, kFont, kNoticeEm, "알림이 없다", rc(c.muted)});
            }
            // 보이는 줄만
            const auto first = static_cast<usize>(std::floor(m_scroll / kLineH));
            for (usize i = first; i < m_wrapped.size(); ++i) {
                const double y = static_cast<double>(i) * kLineH - m_scroll;
                if (y > h) {
                    break;
                }
                d.push_back(gui::DrawText{{0, y, w, kNoticeEm * 1.25}, kFont, kNoticeEm, m_wrapped[i], rc(c.text)});
            }
            out.add(box.component, std::move(d), true);
        }
    }
    return {};
}

} // namespace sbx::editor::gui
