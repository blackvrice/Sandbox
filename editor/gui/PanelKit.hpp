#pragma once
// editor/gui 화면 · 패널이 같이 쓰는 작은 도우미 (UI-R2-05) — 토큰 색 · 0 이상 크기 · 글자 측정 intrinsic · 단추 그리기.

#include <algorithm>
#include <string>
#include <vector>

#include <graphics-ui/core/controls.hpp>

#include "editor/services/StudioTokens.hpp"
#include "uihost/UiHost.hpp"

namespace sbx::editor::gui::kit {

namespace gui = graphics_ui::core;

inline constexpr gui::FontId kFont = 1;

[[nodiscard]] inline gui::RenderColor rc(services::Rgba8 c, u8 alpha = 255) {
    return {c.r, c.g, c.b, alpha};
}

// GraphicsUI 그리기 명령은 0 이상 크기만 받는다 — 좁은 칸에서 여백을 빼면 음수가 된다
[[nodiscard]] inline double fit(double v) {
    return v > 0.0 ? v : 0.0;
}

[[nodiscard]] inline gui::LayoutNode::Intrinsic textIntrinsic(const uihost::ITextMeasure& text, std::string s, double em) {
    return [&text, s = std::move(s), em](const gui::LayoutConstraints&) -> gui::Result<gui::LayoutSize> {
        return text.measure(s, em);
    };
}

// 단추 하나 (가운데 글자). accent = 주 단추 (강조 채움 · 강조 위 글자색)
[[nodiscard]] inline std::vector<gui::DrawCommand> button(const uihost::UiHost& host, const gui::LayoutBox& box,
                                                          const gui::ControlVisualState& vs, const std::string& label,
                                                          double em, bool accent, const services::StudioColors& c) {
    const double w = box.bounds.width;
    const double h = box.bounds.height;
    const bool hot = vs.hovered || vs.pressed;
    std::vector<gui::DrawCommand> d;
    d.push_back(gui::FillRect{{0, 0, w, h}, accent ? (hot ? rc(c.accentHover) : rc(c.accent))
                                         : vs.pressed ? rc(c.controlActive)
                                         : hot        ? rc(c.controlHover)
                                                      : rc(c.elevated)});
    if (vs.focused) {
        d.push_back(gui::FillRect{{0, fit(h - 2), w, 2}, accent ? rc(c.accentText) : rc(c.accent)});
    }
    const double tw = std::min(host.text().measure(label, em).width, w);
    d.push_back(gui::DrawText{{fit((w - tw) / 2), fit((h - em * 1.25) / 2), fit(tw), em * 1.25}, kFont, em, label,
                              accent ? rc(c.accentText) : rc(c.text)});
    return d;
}

} // namespace sbx::editor::gui::kit
