#include "editor/gui/RunPanels.hpp"

#include <algorithm>
#include <format>

#include "editor/gui/PanelKit.hpp"

namespace sbx::editor::gui {

using kit::fit;
using kit::kFont;
using kit::rc;

namespace {

constexpr double kLine = 26.0;
constexpr double kInfoLineH = 18.0;
constexpr double kRowH = 22.0;

[[nodiscard]] gui::RenderColor toneColor(StatTone t, const services::StudioColors& c) {
    return t == StatTone::Section   ? rc(c.accent)
           : t == StatTone::Muted   ? rc(c.muted)
           : t == StatTone::Warning ? rc(c.warning)
           : t == StatTone::Error   ? rc(c.error)
                                    : rc(c.text);
}

} // namespace

// ---- 실행 ----

std::string SimulationPanel::pauseLabel() const {
    return m_model.paused ? "재개 (Space)" : "일시정지 (Space)";
}

std::string SimulationPanel::speedText() const {
    return m_model.available ? std::format("속도 ×{}", m_model.speed) : std::string("속도 —");
}

void SimulationPanel::emit(SimAction a) {
    m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::Simulation, {}, static_cast<int>(a));
}

void SimulationPanel::applyEnabled() {
    if (!m_pause) {
        return;
    }
    // 실행 제어는 admin 부터 (ImGui 창처럼 끈다 — 판단은 서버), 한 틱은 일시정지 중에만
    const bool control = m_model.available && m_model.canControl;
    (void)m_pause->setEnabled(control);
    (void)m_step->setEnabled(control && m_model.paused);
    (void)m_slower->setEnabled(control);
    (void)m_faster->setEnabled(control);
    for (gui::Button* b : {m_grid.get(), m_details.get(), m_fit.get()}) {
        (void)b->setEnabled(m_model.available);
    }
    (void)m_clear->setEnabled(m_model.available && !m_model.selection.empty());
}

Expected<void> SimulationPanel::mount(uihost::UiHost& host) {
    // 뿌리 · 제목 · 정보 줄 · 실행 줄 (일시정지 · 한 틱) · 속도 줄 (− 값 +) · 안내 · 보기 줄 (격자 · 자세히) · 맞춤 줄 (맞춤 · 줌) ·
    // 선택 · 선택 해제 (종류: 1 뿌리 · 2 글 · 3 줄 · 4 단추)
    std::vector<gui::ComponentSpec> specs;
    const auto add = [&specs](std::optional<usize> parent, gui::ComponentType kind, gui::ComponentKey order) {
        specs.push_back({parent, kind, order, true});
        return specs.size() - 1;
    };
    const usize root = add(std::nullopt, 1, 0);
    const usize headline = add(root, 2, 1);
    const usize info = add(root, 2, 2);
    const usize run = add(root, 3, 3);
    const usize pause = add(run, 4, 1);
    const usize step = add(run, 4, 2);
    const usize speedRow = add(root, 3, 4);
    const usize slower = add(speedRow, 4, 1);
    const usize speed = add(speedRow, 2, 2);
    const usize faster = add(speedRow, 4, 3);
    const usize note = add(root, 2, 5);
    const usize view = add(root, 3, 6);
    const usize grid = add(view, 4, 1);
    const usize details = add(view, 4, 2);
    const usize fitRow = add(root, 3, 7);
    const usize fitBtn = add(fitRow, 4, 1);
    const usize zoom = add(fitRow, 2, 2);
    const usize selection = add(root, 2, 8);
    const usize clear = add(root, 4, 9);
    auto h = kit::commitTree(host, specs);
    if (!h) {
        return std::unexpected(h.error());
    }
    const auto& H = *h;
    m_root = H[root];
    m_headline = H[headline];
    m_info = H[info];
    m_runRow = H[run];
    m_pauseNode = H[pause];
    m_stepNode = H[step];
    m_speedRow = H[speedRow];
    m_slowerNode = H[slower];
    m_speedNode = H[speed];
    m_fasterNode = H[faster];
    m_note = H[note];
    m_viewRow = H[view];
    m_gridNode = H[grid];
    m_detailsNode = H[details];
    m_fitRow = H[fitRow];
    m_fitNode = H[fitBtn];
    m_zoomNode = H[zoom];
    m_selection = H[selection];
    m_clearNode = H[clear];
    m_pause = kit::bindButton(host, m_pauseNode, [this] { emit(SimAction::TogglePause); });
    m_step = kit::bindButton(host, m_stepNode, [this] { emit(SimAction::Step); });
    m_slower = kit::bindButton(host, m_slowerNode, [this] { emit(SimAction::Slower); });
    m_faster = kit::bindButton(host, m_fasterNode, [this] { emit(SimAction::Faster); });
    m_grid = kit::bindButton(host, m_gridNode, [this] { emit(SimAction::ToggleGrid); });
    m_details = kit::bindButton(host, m_detailsNode, [this] { emit(SimAction::ToggleDetails); });
    m_fit = kit::bindButton(host, m_fitNode, [this] { emit(SimAction::FitCamera); });
    m_clear = kit::bindButton(host, m_clearNode, [this] { emit(SimAction::ClearSelection); });
    applyEnabled();
    return {};
}

Expected<void> SimulationPanel::layout(uihost::UiHost& host, const gui::ComponentSnapshot&, std::vector<gui::LayoutNode>& out) {
    applyEnabled();
    const gui::LayoutSize vp = host.viewport();
    const double inner = fit(vp.width - 16);
    constexpr double kGap = 6;
    const auto leaf = [&](gui::ComponentHandle c, double w, double h) {
        gui::LayoutNode n;
        n.component = c;
        n.style.preferred_width = w;
        n.style.preferred_height = h;
        out.push_back(n);
    };
    const auto row = [&](gui::ComponentHandle c) {
        gui::LayoutNode n;
        n.component = c;
        n.style.kind = gui::LayoutKind::row;
        n.style.gap = kGap;
        out.push_back(n);
    };
    gui::LayoutNode root;
    root.component = m_root;
    root.style.kind = gui::LayoutKind::column;
    root.style.padding = {8, 8, 8, 8};
    root.style.gap = kGap;
    out.push_back(root);
    const double half = fit((inner - kGap) / 2);
    leaf(m_headline, inner, 16);
    leaf(m_info, inner, std::max<double>(1, static_cast<double>(m_model.lines.size())) * kInfoLineH);
    row(m_runRow);
    leaf(m_pauseNode, half, kLine);
    leaf(m_stepNode, half, kLine);
    row(m_speedRow);
    leaf(m_slowerNode, 28, kLine);
    leaf(m_speedNode, fit(inner - 2 * 28 - 2 * kGap), kLine);
    leaf(m_fasterNode, 28, kLine);
    leaf(m_note, inner, 16);
    row(m_viewRow);
    leaf(m_gridNode, half, kLine);
    leaf(m_detailsNode, half, kLine);
    row(m_fitRow);
    leaf(m_fitNode, half, kLine);
    leaf(m_zoomNode, half, kLine);
    leaf(m_selection, inner, 2 * kInfoLineH);
    leaf(m_clearNode, half, kLine);
    return {};
}

Expected<void> SimulationPanel::paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) {
    const services::StudioColors& c = services::studioColors(m_ctx.theme);
    const auto btn = [&](const gui::LayoutBox& box, const gui::Button& b, const std::string& label) {
        out.add(box.component, b.enabled() ? kit::button(host, box, kit::visualOf(b, host), label, 13.0, false, c)
                                           : kit::disabledButton(host, box, label, 13.0, c));
    };
    const auto text = [&](const gui::LayoutBox& box, const std::string& s, double em, gui::RenderColor color) {
        out.add(box.component, {gui::DrawText{{0, fit((box.bounds.height - 16) / 2), box.bounds.width, 16}, kFont, em, s, color}},
                true);
    };
    for (const gui::LayoutBox& box : frame.boxes()) {
        const double w = box.bounds.width;
        const double h = box.bounds.height;
        if (box.component == m_root) {
            out.add(box.component, {gui::FillRect{{0, 0, w, h}, rc(c.surface)}});
        } else if (box.component == m_headline) {
            text(box, m_model.available ? m_model.headline : std::string("월드 없음 — 프로젝트를 열면 실행 제어"), 13.0,
                 m_model.available ? rc(c.text) : rc(c.muted));
        } else if (box.component == m_info) {
            std::vector<gui::DrawCommand> d;
            for (usize i = 0; i < m_model.lines.size(); ++i) {
                const StatLine& l = m_model.lines[i];
                const double y = static_cast<double>(i) * kInfoLineH;
                if (l.label.empty()) {
                    d.push_back(gui::DrawText{{0, y, w, 16}, kFont, 12.5, l.value, toneColor(l.tone, c)});
                } else {
                    const double split = std::min(110.0, std::floor(w * 0.4));
                    d.push_back(gui::DrawText{{0, y, fit(split - 6), 16}, kFont, 12.5, l.label, rc(c.muted)});
                    d.push_back(gui::DrawText{{split, y, fit(w - split), 16}, kFont, 12.5, l.value, toneColor(l.tone, c)});
                }
            }
            out.add(box.component, std::move(d), true);
        } else if (box.component == m_pauseNode) {
            btn(box, *m_pause, pauseLabel());
        } else if (box.component == m_stepNode) {
            btn(box, *m_step, "한 틱 (.)");
        } else if (box.component == m_slowerNode) {
            btn(box, *m_slower, "−");
        } else if (box.component == m_fasterNode) {
            btn(box, *m_faster, "+");
        } else if (box.component == m_speedNode) {
            out.add(box.component, {gui::FillRect{{0, 0, w, h}, rc(c.elevated)},
                                    gui::DrawText{{8, fit((h - 16) / 2), fit(w - 16), 16}, kFont, 13.0, speedText(),
                                                  m_model.available ? rc(c.text) : rc(c.muted)}});
        } else if (box.component == m_note) {
            text(box, m_model.available && !m_model.canControl ? std::string("일시정지 · 속도는 admin 부터") : std::string(),
                 12.5, rc(c.muted));
        } else if (box.component == m_gridNode) {
            btn(box, *m_grid, m_model.grid ? "격자: 켬 (G)" : "격자: 끔 (G)");
        } else if (box.component == m_detailsNode) {
            btn(box, *m_details, m_model.details ? "자세히: 켬 (V)" : "자세히: 끔 (V)");
        } else if (box.component == m_fitNode) {
            btn(box, *m_fit, "월드 맞춤 (Home)");
        } else if (box.component == m_zoomNode) {
            text(box, m_model.available ? std::format("줌 {:.1f} px/칸", m_model.zoom) : std::string(), 12.5, rc(c.muted));
        } else if (box.component == m_selection) {
            const std::string s = !m_model.available         ? std::string()
                                  : m_model.selection.empty() ? std::string("선택 없음 — 클릭 · 끌기 (Shift = 더하기)")
                                                              : m_model.selection;
            out.add(box.component,
                    {gui::DrawText{{0, 0, w, h}, kFont, 12.5, s, m_model.selection.empty() ? rc(c.muted) : rc(c.text)}}, true);
        } else if (box.component == m_clearNode) {
            btn(box, *m_clear, "선택 해제 (Esc)");
        }
    }
    return {};
}

// ---- 접속자 ----

void PlayersPanel::setModel(PlayersModel model) {
    m_model = std::move(model);
    std::vector<gui::ListItemKey> keys;
    for (const PlayerEntry& p : m_model.players) {
        keys.push_back(p.clientId); // 키 = 접속 번호 (사람이 들고 나도 같은 사람은 같은 줄)
    }
    if (keys != m_keys) {
        m_keys = std::move(keys);
        (void)m_list.setItems(m_keys);
    }
    if (m_selected && !selectedRow()) {
        m_selected.reset(); // 나갔다
    }
}

std::optional<usize> PlayersPanel::selectedRow() const {
    if (!m_selected) {
        return std::nullopt;
    }
    for (usize i = 0; i < m_model.players.size(); ++i) {
        if (m_model.players[i].clientId == *m_selected) {
            return i;
        }
    }
    return std::nullopt;
}

std::string PlayersPanel::hint() const {
    if (!m_model.available) {
        return "접속자 정보가 없다 — 서버에 들어가면 보인다";
    }
    const auto row = selectedRow();
    if (!row) {
        return "줄을 고르면 그 사람의 역할을 바꾼다 (판단은 서버)";
    }
    const PlayerEntry& p = m_model.players[*row];
    if (p.self) {
        return "자기 역할은 바꿀 수 없다";
    }
    return p.assignable == 0 ? std::format("{} — 지금 역할로는 바꿀 수 없다", p.who) : std::format("{} 의 역할", p.who);
}

void PlayersPanel::select(std::optional<usize> row) {
    m_selected = row ? std::optional<u16>(m_model.players[*row].clientId) : std::nullopt;
    if (row) {
        (void)m_list.reveal(m_keys[*row]);
    }
    applyEnabled();
}

void PlayersPanel::applyEnabled() {
    if (!m_roles[0]) {
        return;
    }
    const auto row = selectedRow();
    for (usize r = 0; r < kRoles; ++r) {
        const bool ok = m_model.available && row && (m_model.players[*row].assignable & (1u << r)) != 0;
        (void)m_roles[r]->setEnabled(ok);
    }
}

std::optional<gui::LayoutRect> PlayersPanel::rowBounds(usize row) const {
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

gui::EventReply PlayersPanel::listEvent(const gui::RoutedEvent& ev) {
    if (ev.phase != gui::EventPhase::target) {
        return {};
    }
    if (const auto* p = std::get_if<gui::PointerEvent>(&ev.input)) {
        if (p->action == gui::PointerAction::down && p->button == gui::PointerButton::primary) {
            (void)m_host->input().setFocus(m_listNode);
            if (const auto row = m_list.hitTest({p->x - m_listBounds.x, p->y - m_listBounds.y})) {
                for (usize i = 0; i < m_model.players.size(); ++i) {
                    if (m_model.players[i].clientId == row->key) {
                        select(i);
                    }
                }
            }
            return {true, true};
        }
        return {};
    }
    const bool up = kit::keyDown(ev, gui::up_key), down = kit::keyDown(ev, gui::down_key);
    if ((up || down) && !m_model.players.empty()) {
        usize at = selectedRow().value_or(up ? m_model.players.size() : static_cast<usize>(-1));
        at = up ? (at == 0 ? 0 : at - 1) : std::min(at + 1, m_model.players.size() - 1);
        select(at);
        return {true, true};
    }
    return {};
}

Expected<void> PlayersPanel::mount(uihost::UiHost& host) {
    m_host = &host;
    // 뿌리 · 내 역할 · 목록 · 안내 · 역할 단추 줄 (5) · 결과 (종류: 1 뿌리 · 2 글 · 3 줄 · 4 단추 · 5 목록)
    std::vector<gui::ComponentSpec> specs;
    specs.push_back({std::nullopt, 1, 0, true});
    specs.push_back({0, 2, 1, true});
    specs.push_back({0, 5, 2, true});
    specs.push_back({0, 2, 3, true});
    specs.push_back({0, 3, 4, true});
    for (usize r = 0; r < kRoles; ++r) {
        specs.push_back({4, 4, r + 1, true});
    }
    specs.push_back({0, 2, 5, true});
    auto h = kit::commitTree(host, specs);
    if (!h) {
        return std::unexpected(h.error());
    }
    const auto& H = *h;
    m_root = H[0];
    m_me = H[1];
    m_listNode = H[2];
    m_hint = H[3];
    m_roleRow = H[4];
    for (usize r = 0; r < kRoles; ++r) {
        m_roleNodes[r] = H[5 + r];
        m_roles[r] = kit::bindButton(host, m_roleNodes[r], [this, r] {
            const auto row = selectedRow();
            if (!m_model.available || !row || (m_model.players[*row].assignable & (1u << r)) == 0) {
                return;
            }
            WorkspaceIntent i = WorkspaceIntent::of(WorkspaceIntent::Kind::SetRole, {}, m_model.players[*row].clientId);
            i.role = static_cast<int>(r);
            m_ctx.intent = std::move(i);
        });
    }
    m_message = H[5 + kRoles];
    applyEnabled();
    (void)host.input().setFocusable(m_listNode, true);
    (void)host.input().setHandler(m_listNode, [this](const gui::RoutedEvent& ev) { return listEvent(ev); });
    (void)host.setWheelHandler(m_listNode, [this](f32, f32 dy) {
        (void)m_list.scrollBy(-static_cast<double>(dy) * kRowH * 3.0);
        return true;
    });
    return {};
}

Expected<void> PlayersPanel::layout(uihost::UiHost& host, const gui::ComponentSnapshot&, std::vector<gui::LayoutNode>& out) {
    applyEnabled();
    const gui::LayoutSize vp = host.viewport();
    const double inner = fit(vp.width - 16);
    constexpr double kGap = 6;
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
    leaf(m_me, inner, 16);
    leaf(m_listNode, inner, fit(vp.height - 16 - 3 * 16 - kLine - 4 * kGap));
    leaf(m_hint, inner, 16);
    gui::LayoutNode row;
    row.component = m_roleRow;
    row.style.kind = gui::LayoutKind::row;
    row.style.gap = 4;
    out.push_back(row);
    const double roleW = fit((inner - 4 * 4) / static_cast<double>(kRoles));
    for (const gui::ComponentHandle r : m_roleNodes) {
        leaf(r, roleW, kLine);
    }
    leaf(m_message, inner, 16);
    return {};
}

Expected<void> PlayersPanel::paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) {
    const services::StudioColors& c = services::studioColors(m_ctx.theme);
    const auto focusR = host.input().focus();
    const bool listFocused = focusR && *focusR && **focusR == m_listNode;
    const auto row = selectedRow();
    for (const gui::LayoutBox& box : frame.boxes()) {
        const double w = box.bounds.width;
        const double h = box.bounds.height;
        if (const auto r = std::ranges::find(m_roleNodes, box.component); r != m_roleNodes.end()) {
            const usize i = static_cast<usize>(r - m_roleNodes.begin());
            // 역할 이름의 앞 낱말 (단추가 좁다 — "owner (주인)" → "owner")
            std::string label = m_model.roleNames[i];
            label = label.substr(0, label.find(' '));
            const bool current = m_model.available && row && m_model.players[*row].role == i;
            out.add(box.component, m_roles[i]->enabled() || current
                                       ? kit::button(host, box, kit::visualOf(*m_roles[i], host), label, 12.5, current, c)
                                       : kit::disabledButton(host, box, label, 12.5, c));
            continue;
        }
        if (box.component == m_root) {
            out.add(box.component, {gui::FillRect{{0, 0, w, h}, rc(c.surface)}});
        } else if (box.component == m_me) {
            out.add(box.component,
                    {gui::DrawText{{0, 0, w, h}, kFont, 13.0,
                                   m_model.available ? "내 역할: " + m_model.roleNames[std::min<usize>(m_model.myRole, kRoles - 1)]
                                                     : std::string("접속자 — 서버에 들어가면"),
                                   m_model.available ? rc(c.text) : rc(c.muted)}},
                    true);
        } else if (box.component == m_hint) {
            out.add(box.component, {gui::DrawText{{0, 0, w, h}, kFont, 12.5, hint(), rc(c.muted)}}, true);
        } else if (box.component == m_message) {
            out.add(box.component, {gui::DrawText{{0, 0, w, h}, kFont, 12.5, m_model.message, rc(c.warning)}}, true);
        } else if (box.component == m_listNode) {
            m_listBounds = box.bounds;
            (void)m_list.resize({w, h}, kRowH);
            std::vector<gui::DrawCommand> d;
            d.push_back(gui::FillRect{{0, 0, w, h}, rc(c.background)});
            const gui::ListRange range = m_list.visibleRange();
            for (usize i = range.first; i < range.past; ++i) {
                const auto vr = m_list.visibleRow(i);
                if (!vr) {
                    continue;
                }
                const auto at = std::ranges::find(m_model.players, static_cast<u16>(vr->key), &PlayerEntry::clientId);
                if (at == m_model.players.end()) {
                    continue;
                }
                const gui::LayoutRect b = vr->bounds;
                if (m_selected == at->clientId) {
                    d.push_back(gui::FillRect{b, listFocused ? rc(c.header) : rc(c.headerHover)});
                    d.push_back(gui::FillRect{{b.x, b.y, 2, b.height}, rc(c.accent)});
                }
                const double split = std::floor(b.width * 0.55);
                d.push_back(gui::DrawText{{b.x + 8, b.y + 3, fit(split - 12), 16}, kFont, 13.0, at->who,
                                          at->self ? rc(c.accent) : rc(c.text)});
                d.push_back(gui::DrawText{{b.x + split, b.y + 3, fit(b.width - split - 6), 16}, kFont, 12.5,
                                          m_model.roleNames[std::min<usize>(at->role, kRoles - 1)], rc(c.muted)});
            }
            if (m_model.players.empty()) {
                d.push_back(gui::DrawText{{8, 8, fit(w - 16), 18}, kFont, 13.0, "접속자가 없다", rc(c.muted)});
            }
            out.add(box.component, std::move(d), true);
        }
    }
    return {};
}


// ---- 스크립트 프로파일 ----

void ScriptProfilePanel::setModel(ScriptProfileModel model) {
    if (model == m_model) {
        return;
    }
    m_model = std::move(model);
    kit::setIndexKeys(m_list, m_keys, m_model.lines.size()); // 줄 차례 — 표 모양 (시스템 수) 이 같으면 그대로 (값만 바뀐다)
    if (m_selected && *m_selected >= m_model.lines.size()) {
        m_selected.reset();
    }
}

std::string ScriptProfilePanel::lineText(usize line) const {
    if (line >= m_model.lines.size()) {
        return {};
    }
    std::string out;
    for (const std::string& c : m_model.lines[line].cells) {
        out += (out.empty() ? "" : "\t") + c;
    }
    return out;
}

usize ScriptProfilePanel::visibleColumns(double width, usize cells) const {
    // 첫 열은 적어도 120 — 그 뒤 고정 폭 열이 들어가는 만큼
    if (cells == 0) {
        return 0;
    }
    const double rest = width - 120.0 - 12.0;
    const usize more = rest <= 0 ? 0 : static_cast<usize>(rest / kCellW);
    return std::min(cells, 1 + more);
}

std::optional<gui::LayoutRect> ScriptProfilePanel::rowBounds(usize line) const {
    if (line >= m_keys.size()) {
        return std::nullopt;
    }
    const gui::ListRange range = m_list.visibleRange();
    for (usize i = range.first; i < range.past; ++i) {
        if (const auto r = m_list.visibleRow(i); r && r->key == m_keys[line]) {
            return r->bounds;
        }
    }
    return std::nullopt;
}

gui::EventReply ScriptProfilePanel::listEvent(const gui::RoutedEvent& ev) {
    if (ev.phase != gui::EventPhase::target) {
        return {};
    }
    if (const auto* p = std::get_if<gui::PointerEvent>(&ev.input)) {
        if (p->action == gui::PointerAction::down && p->button == gui::PointerButton::primary) {
            (void)m_host->input().setFocus(m_listNode);
            if (const auto row = m_list.hitTest({p->x - m_listBounds.x, p->y - m_listBounds.y})) {
                m_selected = static_cast<usize>(row->key);
            }
            return {true, true};
        }
        return {};
    }
    for (const gui::KeyModifiers mod : {gui::KeyModifiers::control, gui::KeyModifiers::meta}) {
        if (kit::keyDown(ev, 'C', mod) && m_selected) { // 고른 줄을 탭으로 (표 계산기에 붙일 수 있다)
            const gui::TextEditCallbacks cb = m_host->editCallbacks({});
            if (cb.write_clipboard) {
                (void)cb.write_clipboard(lineText(*m_selected));
            }
            return {true, true};
        }
    }
    const bool up = kit::keyDown(ev, gui::up_key), down = kit::keyDown(ev, gui::down_key);
    if ((up || down) && !m_model.lines.empty()) {
        usize at = m_selected.value_or(up ? m_model.lines.size() : static_cast<usize>(-1));
        at = up ? (at == 0 ? 0 : at - 1) : std::min(at + 1, m_model.lines.size() - 1);
        m_selected = at;
        (void)m_list.reveal(m_keys[at]);
        return {true, true};
    }
    return {};
}

Expected<void> ScriptProfilePanel::mount(uihost::UiHost& host) {
    m_host = &host;
    const std::array<gui::ComponentSpec, 3> specs{{{std::nullopt, 1, 0, true}, {0, 2, 1, true}, {0, 5, 2, true}}};
    auto h = kit::commitTree(host, specs);
    if (!h) {
        return std::unexpected(h.error());
    }
    m_root = (*h)[0];
    m_title = (*h)[1];
    m_listNode = (*h)[2];
    (void)host.input().setFocusable(m_listNode, true);
    (void)host.input().setHandler(m_listNode, [this](const gui::RoutedEvent& ev) { return listEvent(ev); });
    (void)host.setWheelHandler(m_listNode, [this](f32, f32 dy) {
        (void)m_list.scrollBy(-static_cast<double>(dy) * kRowH * 3.0);
        return true;
    });
    return {};
}

Expected<void> ScriptProfilePanel::layout(uihost::UiHost& host, const gui::ComponentSnapshot&, std::vector<gui::LayoutNode>& out) {
    const gui::LayoutSize vp = host.viewport();
    const double inner = fit(vp.width - 16);
    gui::LayoutNode root;
    root.component = m_root;
    root.style.kind = gui::LayoutKind::column;
    root.style.padding = {8, 8, 8, 8};
    root.style.gap = 6;
    out.push_back(root);
    for (const auto& [node, height] : std::array<std::pair<gui::ComponentHandle, double>, 2>{
             {{m_title, 16.0}, {m_listNode, fit(vp.height - 16 - 16 - 6)}}}) {
        gui::LayoutNode n;
        n.component = node;
        n.style.preferred_width = inner;
        n.style.preferred_height = height;
        out.push_back(n);
    }
    return {};
}

Expected<void> ScriptProfilePanel::paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) {
    const services::StudioColors& c = services::studioColors(m_ctx.theme);
    const auto focusR = host.input().focus();
    const bool listFocused = focusR && *focusR && **focusR == m_listNode;
    for (const gui::LayoutBox& box : frame.boxes()) {
        const double w = box.bounds.width;
        const double h = box.bounds.height;
        if (box.component == m_root) {
            out.add(box.component, {gui::FillRect{{0, 0, w, h}, rc(c.surface)}});
        } else if (box.component == m_title) {
            const std::string t = !m_model.available       ? std::string("스크립트 프로파일 — 월드에 들어가면 (서버 · 클라이언트 시스템)")
                                  : m_model.lines.empty() ? std::string("스크립트 프로파일 — 아직 표가 없다")
                                                          : std::string("스크립트 프로파일 — 줄을 고르고 Ctrl+C = 그 줄 (탭으로)");
            out.add(box.component, {gui::DrawText{{0, 0, w, h}, kFont, 12.5, t, rc(c.muted)}}, true);
        } else if (box.component == m_listNode) {
            m_listBounds = box.bounds;
            (void)m_list.resize({w, h}, kRowH);
            std::vector<gui::DrawCommand> d;
            d.push_back(gui::FillRect{{0, 0, w, h}, rc(c.background)});
            const gui::ListRange range = m_list.visibleRange();
            for (usize i = range.first; i < range.past; ++i) {
                const auto vr = m_list.visibleRow(i);
                if (!vr || vr->key >= m_model.lines.size()) {
                    continue;
                }
                const usize index = static_cast<usize>(vr->key);
                const ProfileLine& l = m_model.lines[index];
                const gui::LayoutRect b = vr->bounds;
                if (l.kind == ProfileLine::Kind::Section) {
                    d.push_back(gui::FillRect{{b.x, b.y + 1, b.width, fit(b.height - 2)}, rc(c.elevated)});
                }
                if (m_selected == index) {
                    d.push_back(gui::FillRect{b, listFocused ? rc(c.header) : rc(c.headerHover)});
                    d.push_back(gui::FillRect{{b.x, b.y, 2, b.height}, rc(c.accent)});
                }
                if (l.kind == ProfileLine::Kind::Section || l.kind == ProfileLine::Kind::Note) {
                    d.push_back(gui::DrawText{{b.x + 6, b.y + 3, fit(b.width - 12), 16}, kFont, 12.5,
                                              l.cells.empty() ? std::string() : l.cells.front(),
                                              l.kind == ProfileLine::Kind::Section ? rc(c.accent) : rc(c.muted)});
                    continue;
                }
                // 열 — 첫 열은 남은 폭, 나머지는 고정 폭 (들어가는 만큼)
                const usize cols = visibleColumns(b.width, l.cells.size());
                const double firstW = fit(b.width - 12 - static_cast<double>(cols > 0 ? cols - 1 : 0) * kCellW);
                const gui::RenderColor color = l.kind == ProfileLine::Kind::Columns ? rc(c.muted) : toneColor(l.tone, c);
                const double em = 12.0;
                for (usize k = 0; k < cols; ++k) {
                    const double x = k == 0 ? b.x + 6 : b.x + 6 + firstW + static_cast<double>(k - 1) * kCellW;
                    const double cw = k == 0 ? fit(firstW - 6) : kCellW - 6;
                    d.push_back(gui::DrawText{{x, b.y + 3, fit(cw), 16}, kFont, em, l.cells[k], color});
                }
            }
            if (m_model.lines.empty()) {
                d.push_back(gui::DrawText{{8, 8, fit(w - 16), 18}, kFont, 13.0,
                                          m_model.available ? "스크립트 · 엔진 시스템 표가 오면 보인다" : "월드 없음",
                                          rc(c.muted)});
            }
            out.add(box.component, std::move(d), true);
        }
    }
    return {};
}

} // namespace sbx::editor::gui
