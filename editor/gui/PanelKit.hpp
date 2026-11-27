#pragma once
// editor/gui 화면 · 패널이 같이 쓰는 작은 도우미 (UI-R2-05) — 토큰 색 · 0 이상 크기 · 글자 측정 intrinsic · 단추 그리기.

#include <algorithm>
#include <functional>
#include <memory>
#include <span>
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

// 부모 우선 게시 — 핸들은 snapshot 의 같은 차례
[[nodiscard]] inline Expected<std::vector<gui::ComponentHandle>> commitTree(uihost::UiHost& host,
                                                                           std::span<const gui::ComponentSpec> specs) {
    if (auto r = host.tree().commit(specs); !r) {
        return std::unexpected(uihost::toError(r.error(), "패널 트리 게시"));
    }
    auto snap = host.tree().snapshot();
    if (!snap || snap->components.size() != specs.size()) {
        return makeError(ErrorCode::Unknown, "패널 트리를 읽지 못했다");
    }
    std::vector<gui::ComponentHandle> out;
    for (const auto& c : snap->components) {
        out.push_back(c.handle);
    }
    return out;
}

// 키를 눌렀나 (그 수정자 그대로)
[[nodiscard]] inline bool keyDown(const gui::RoutedEvent& ev, gui::KeyCode key,
                                  gui::KeyModifiers mods = gui::KeyModifiers::none) {
    const auto* k = std::get_if<gui::KeyEvent>(&ev.input);
    return k != nullptr && k->action == gui::KeyAction::down && k->chord.key == key && k->chord.modifiers == mods;
}

// 단추 컨트롤 하나를 컴포넌트에 건다 — 눌리면 (포인터 · Enter · Space) onActivate. 단추는 돌려준 것이 소유 (호스트의 핸들러가
// 가리킨다 — 화면이 호스트보다 오래 살거나, 화면 바꾸기가 핸들러를 먼저 지운다)
[[nodiscard]] inline std::unique_ptr<gui::Button> bindButton(uihost::UiHost& host, gui::ComponentHandle node,
                                                             std::function<void()> onActivate) {
    auto b = std::make_unique<gui::Button>(host.input(), node);
    (void)b->setEnabled(true);
    gui::Button* raw = b.get();
    (void)host.input().setHandler(node, [raw, f = std::move(onActivate)](const gui::RoutedEvent& ev) -> gui::EventReply {
        const auto r = raw->handle(ev);
        if (r && r->activated && f) {
            f();
        }
        return r ? r->reply : gui::EventReply{};
    });
    return b;
}

[[nodiscard]] inline gui::ControlVisualState visualOf(const gui::Button& b, const uihost::UiHost& host) {
    auto v = b.visualState(host.pointerHit());
    return v ? *v : gui::ControlVisualState{};
}

// 쓸 수 없는 단추 (흐린 글자 · 바탕만)
[[nodiscard]] inline std::vector<gui::DrawCommand> disabledButton(const uihost::UiHost& host, const gui::LayoutBox& box,
                                                                  const std::string& label, double em,
                                                                  const services::StudioColors& c) {
    const double w = box.bounds.width;
    const double h = box.bounds.height;
    const double tw = std::min(host.text().measure(label, em).width, w);
    return {gui::FillRect{{0, 0, w, h}, rc(c.surface)},
            gui::DrawText{{fit((w - tw) / 2), fit((h - em * 1.25) / 2), fit(tw), em * 1.25}, kFont, em, label, rc(c.muted)}};
}

} // namespace sbx::editor::gui::kit
