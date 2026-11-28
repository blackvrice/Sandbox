#include "editor/gui/GamePanel.hpp"

#include <algorithm>
#include <format>

#include "editor/gui/PanelKit.hpp"

namespace sbx::editor::gui {

using kit::fit;
using kit::kFont;
using kit::rc;

namespace {

constexpr double kLine = 26.0;
constexpr double kRowH = 22.0;
constexpr double kTextH = 18.0;

} // namespace

void GamePanel::setModel(GameModel model) {
    m_model = std::move(model);
    kit::setIndexKeys(m_list, m_keys, m_model.actions.size()); // id 순 (콘텐츠) — 목록이 같으면 그대로
    if (m_cursor && *m_cursor >= m_model.actions.size()) {
        m_cursor.reset();
    }
}

std::string GamePanel::actionHint() const {
    if (!m_model.actionsAvailable) {
        return "행동이 없다 (콘텐츠의 PlayerAction)";
    }
    if (!m_model.canAct) {
        return "행동은 역할 player 부터";
    }
    if (m_model.armed.empty()) {
        return m_model.hasKeys ? "행동을 고르고 장면을 클릭 · [키] = 커서 자리에" : "행동을 고르고 장면을 클릭하세요";
    }
    return std::format("쥐고 있다: {} — 장면을 클릭하면 그 자리에 · 다시 누르거나 Esc 로 놓기", m_model.armed);
}

void GamePanel::command(int c) {
    m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::GameCommand, {}, c);
}

void GamePanel::toggle(usize row) {
    if (row >= m_model.actions.size() || !m_model.canAct) {
        return;
    }
    const std::string& id = m_model.actions[row].id;
    // 다시 누르면 놓는다 (ImGui 창과 같다) — 다음 보기 전에도 그 상태로
    m_model.armed = m_model.armed == id ? std::string() : id;
    m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::ArmAction, m_model.armed);
}

void GamePanel::applyEnabled() {
    if (!m_start) {
        return;
    }
    const bool control = m_model.available && m_model.canControl;
    (void)m_start->setEnabled(control && m_model.state == 0);
    (void)m_abort->setEnabled(control && (m_model.state == 1 || m_model.state == 2));
    (void)m_restart->setEnabled(control);
}

std::optional<gui::LayoutRect> GamePanel::rowBounds(usize row) const {
    if (row >= m_keys.size()) {
        return std::nullopt;
    }
    const gui::ListRange range = m_list.visibleRange();
    for (usize i = range.first; i < range.past; ++i) {
        if (const auto r = m_list.visibleRow(i); r && r->key == m_keys[row]) {
            return r->bounds;
        }
    }
    return std::nullopt;
}

gui::EventReply GamePanel::listEvent(const gui::RoutedEvent& ev) {
    if (ev.phase != gui::EventPhase::target) {
        return {};
    }
    if (const auto* p = std::get_if<gui::PointerEvent>(&ev.input)) {
        if (p->action == gui::PointerAction::down && p->button == gui::PointerButton::primary) {
            (void)m_host->input().setFocus(m_listNode);
            if (const auto row = m_list.hitTest({p->x - m_listBounds.x, p->y - m_listBounds.y});
                row && row->key < m_model.actions.size()) {
                m_cursor = static_cast<usize>(row->key);
                toggle(*m_cursor);
            }
            return {true, true};
        }
        return {};
    }
    if (m_model.actions.empty()) {
        return {};
    }
    const bool up = kit::keyDown(ev, gui::up_key), down = kit::keyDown(ev, gui::down_key);
    if (up || down) {
        usize at = m_cursor.value_or(up ? m_model.actions.size() : static_cast<usize>(-1));
        at = up ? (at == 0 ? 0 : at - 1) : std::min(at + 1, m_model.actions.size() - 1);
        m_cursor = at;
        (void)m_list.reveal(m_keys[at]);
        return {true, true};
    }
    if (m_cursor && (kit::keyDown(ev, gui::enter_key) || kit::keyDown(ev, gui::space_key))) {
        toggle(*m_cursor);
        return {true, true};
    }
    return {};
}

Expected<void> GamePanel::mount(uihost::UiHost& host) {
    m_host = &host;
    // 뿌리 · 게임 정보 (여러 줄) · 막대 (시작 · 중단 · 재시작) · 안내 / 결과 · "행동" · 행동 목록 · 행동 안내 / 결과
    // (종류: 1 뿌리 · 2 글 · 3 줄 · 4 단추 · 5 목록)
    std::vector<gui::ComponentSpec> specs;
    const auto add = [&specs](std::optional<usize> parent, gui::ComponentType kind, gui::ComponentKey order) {
        specs.push_back({parent, kind, order, true});
        return specs.size() - 1;
    };
    const usize root = add(std::nullopt, 1, 0);
    const usize info = add(root, 2, 1);
    const usize bar = add(root, 3, 2);
    const usize start = add(bar, 4, 1);
    const usize abort = add(bar, 4, 2);
    const usize restart = add(bar, 4, 3);
    const usize note = add(root, 2, 3);
    const usize title = add(root, 2, 4);
    const usize list = add(root, 5, 5);
    const usize hint = add(root, 2, 6);
    auto h = kit::commitTree(host, specs);
    if (!h) {
        return std::unexpected(h.error());
    }
    const auto& H = *h;
    m_root = H[root];
    m_info = H[info];
    m_bar = H[bar];
    m_startNode = H[start];
    m_abortNode = H[abort];
    m_restartNode = H[restart];
    m_note = H[note];
    m_actionsTitle = H[title];
    m_listNode = H[list];
    m_hint = H[hint];
    m_start = kit::bindButton(host, m_startNode, [this] { command(GameModel::kStart); });
    m_abort = kit::bindButton(host, m_abortNode, [this] { command(GameModel::kAbort); });
    m_restart = kit::bindButton(host, m_restartNode, [this] { command(GameModel::kRestart); });
    applyEnabled();
    (void)host.input().setFocusable(m_listNode, true);
    (void)host.input().setHandler(m_listNode, [this](const gui::RoutedEvent& ev) { return listEvent(ev); });
    (void)host.setWheelHandler(m_listNode, [this](f32, f32 dy) {
        (void)m_list.scrollBy(-static_cast<double>(dy) * kRowH * 3.0);
        return true;
    });
    return {};
}

Expected<void> GamePanel::layout(uihost::UiHost& host, const gui::ComponentSnapshot&, std::vector<gui::LayoutNode>& out) {
    applyEnabled();
    const gui::LayoutSize vp = host.viewport();
    const double inner = fit(vp.width - 16);
    constexpr double kGap = 6;
    constexpr double kInfoH = 5 * kTextH; // 이름 · 정보 · 상태 · 점수 · 자동 재시작
    const auto leaf = [&](gui::ComponentHandle c, double w, double h) {
        gui::LayoutNode n;
        n.component = c;
        n.style.preferred_width = w;
        n.style.preferred_height = h;
        out.push_back(n);
    };
    gui::LayoutNode root;
    root.component = m_root;
    root.style.kind = gui::LayoutKind::column;
    root.style.padding = {8, 8, 8, 8};
    root.style.gap = kGap;
    out.push_back(root);
    leaf(m_info, inner, kInfoH);
    gui::LayoutNode bar;
    bar.component = m_bar;
    bar.style.kind = gui::LayoutKind::row;
    bar.style.gap = kGap;
    out.push_back(bar);
    const double third = fit((inner - 2 * kGap) / 3);
    leaf(m_startNode, third, kLine);
    leaf(m_abortNode, third, kLine);
    leaf(m_restartNode, third, kLine);
    leaf(m_note, inner, 2 * kTextH);
    leaf(m_actionsTitle, inner, 16);
    leaf(m_listNode, inner, fit(vp.height - 16 - kInfoH - kLine - 2 * kTextH - 16 - 2 * kTextH - 5 * kGap));
    leaf(m_hint, inner, 2 * kTextH);
    return {};
}

Expected<void> GamePanel::paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) {
    const services::StudioColors& c = services::studioColors(m_ctx.theme);
    const auto focusR = host.input().focus();
    const bool listFocused = focusR && *focusR && **focusR == m_listNode;
    const auto btn = [&](const gui::LayoutBox& box, const gui::Button& b, const std::string& label) {
        out.add(box.component, b.enabled() ? kit::button(host, box, kit::visualOf(b, host), label, 13.0, false, c)
                                           : kit::disabledButton(host, box, label, 13.0, c));
    };
    // 글 줄 몇 개를 한 상자에 (빈 줄은 건너뛴다)
    const auto lines = [&](const gui::LayoutBox& box, const std::vector<std::pair<std::string, gui::RenderColor>>& ls) {
        std::vector<gui::DrawCommand> d;
        double y = 0;
        for (const auto& [t, color] : ls) {
            if (t.empty()) {
                continue;
            }
            d.push_back(gui::DrawText{{0, y, box.bounds.width, 16}, kFont, 12.5, t, color});
            y += kTextH;
        }
        out.add(box.component, std::move(d), true);
    };
    for (const gui::LayoutBox& box : frame.boxes()) {
        const double w = box.bounds.width;
        const double h = box.bounds.height;
        if (box.component == m_root) {
            out.add(box.component, {gui::FillRect{{0, 0, w, h}, rc(c.surface)}});
        } else if (box.component == m_info) {
            if (!m_model.available) {
                lines(box, {{"게임 — 게임 모드가 있는 월드에서", rc(c.muted)}});
            } else {
                lines(box, {{m_model.modeName, rc(c.text)},
                            {m_model.modeInfo, rc(c.muted)},
                            {m_model.stateLine, m_model.state == 3 ? rc(c.accent) : rc(c.text)},
                            {m_model.scoreLine, rc(c.text)},
                            {m_model.restartLine, rc(c.muted)}});
            }
        } else if (box.component == m_startNode) {
            btn(box, *m_start, "시작");
        } else if (box.component == m_abortNode) {
            btn(box, *m_abort, "중단");
        } else if (box.component == m_restartNode) {
            btn(box, *m_restart, "재시작");
        } else if (box.component == m_note) {
            lines(box, {{m_model.available && !m_model.canControl ? std::string("시작 · 중단 · 재시작은 admin 부터") : std::string(),
                         rc(c.muted)},
                        {m_model.message, rc(c.warning)}});
        } else if (box.component == m_actionsTitle) {
            out.add(box.component,
                    {gui::DrawText{{0, 0, w, h}, kFont, 13.0,
                                   m_model.armed.empty() ? std::string("행동") : std::format("행동 — {}", m_model.armed),
                                   rc(c.accent)}},
                    true);
        } else if (box.component == m_hint) {
            lines(box, {{actionHint(), m_model.armed.empty() ? rc(c.muted) : rc(c.text)}, {m_model.actionMessage, rc(c.warning)}});
        } else if (box.component == m_listNode) {
            m_listBounds = box.bounds;
            (void)m_list.resize({w, h}, kRowH);
            std::vector<gui::DrawCommand> d;
            d.push_back(gui::FillRect{{0, 0, w, h}, rc(c.background)});
            const gui::ListRange range = m_list.visibleRange();
            for (usize i = range.first; i < range.past; ++i) {
                const auto vr = m_list.visibleRow(i);
                if (!vr || vr->key >= m_model.actions.size()) {
                    continue;
                }
                const usize index = static_cast<usize>(vr->key);
                const ActionEntry& a = m_model.actions[index];
                const gui::LayoutRect b = vr->bounds;
                const bool armed = a.id == m_model.armed;
                if (armed) { // 쥔 행동 = 강조 채움 (ImGui 의 고른 Selectable)
                    d.push_back(gui::FillRect{b, rc(c.header)});
                    d.push_back(gui::FillRect{{b.x, b.y, 3, b.height}, rc(c.accent)});
                } else if (listFocused && m_cursor == index) {
                    d.push_back(gui::FillRect{b, rc(c.headerHover)});
                }
                d.push_back(gui::DrawText{{b.x + 8, b.y + 3, fit(b.width - 14), 16}, kFont, 12.5, a.text,
                                          m_model.canAct ? rc(c.text) : rc(c.muted)});
            }
            if (m_model.actions.empty()) {
                d.push_back(gui::DrawText{{8, 8, fit(w - 16), 18}, kFont, 13.0, "행동이 없다", rc(c.muted)});
            }
            out.add(box.component, std::move(d), true);
        }
    }
    return {};
}

} // namespace sbx::editor::gui
