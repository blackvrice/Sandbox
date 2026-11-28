#include "editor/gui/ToolPanel.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string_view>

#include "editor/gui/PanelKit.hpp"

namespace sbx::editor::gui {

using kit::fit;
using kit::kFont;
using kit::rc;

namespace {

constexpr double kRowH = 20.0;
constexpr double kLine = 26.0;

// ImGui "편집" 창의 거르기와 같은 규칙 (ASCII 대소문자 무시 · 부분 글자)
[[nodiscard]] bool containsIgnoreCase(std::string_view text, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    const auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
    return std::ranges::search(text, needle, [&](char a, char b) { return lower(a) == lower(b); }).begin() != text.end();
}

} // namespace

// ---- 보기 ----

void ToolPanel::setView(ToolPanelView view) {
    m_view = std::move(view);
}

std::string ToolPanel::title() const {
    if (!m_view.available) {
        return "편집 — 프로젝트 안에서 쓴다";
    }
    const usize t = static_cast<usize>(std::clamp(m_view.tool, 0, static_cast<int>(kTools) - 1));
    return std::format("툴: {} ({}){}", m_view.toolNames[t], t + 1, m_view.canEdit ? "" : "  — 보기만");
}

std::string ToolPanel::hint() const {
    if (!m_view.available) {
        return "편집할 월드가 없다 — 프로젝트를 열면 툴 · 배치 Prefab · 붓";
    }
    if (!m_view.canEdit) {
        return "지금 역할로는 보기만 할 수 있다 (접속자 패널의 역할)";
    }
    switch (m_view.tool) { // ImGui 창과 같은 안내 (툴 고르기는 위 단추 · 툴바 · 숫자 키 1 ~ 5)
    case kSelect:
        return "클릭 · 끌기 = 선택 (Shift = 더하기) · Delete = 선택 지우기";
    case kMove:
        return "선택한 개체를 끌어 옮긴다 (빈 곳 = 선택)";
    case kPlace:
        return "장면을 누르면 고른 Prefab 을 놓는다 · 끌면 간격마다";
    case kTerrain:
        return "왼쪽 = 칠하기 · 오른쪽 = 바탕으로 지우기";
    case kErase:
        return "클릭 = 그 개체 · 끌기 = 박스 안 전부 지우기";
    default:
        return {};
    }
}

bool ToolPanel::stepEnabled() const {
    return m_view.available && (m_view.tool == kPlace || m_view.tool == kTerrain);
}

bool ToolPanel::toggleEnabled() const {
    return m_view.available && (m_view.tool == kMove || m_view.tool == kPlace || m_view.tool == kTerrain);
}

std::string ToolPanel::stepLabel() const {
    if (!m_view.available) {
        return "—";
    }
    return m_view.tool == kPlace ? "끌기 간격 (칸)" : m_view.tool == kTerrain ? "붓 반지름" : "—";
}

std::string ToolPanel::stepValue() const {
    if (!stepEnabled()) {
        return {};
    }
    if (m_view.tool == kPlace) {
        return std::format("{:.1f}", m_view.settings.placeSpacing);
    }
    return m_view.settings.brushRadius == 0 ? std::string("0 (한 칸)") : std::to_string(m_view.settings.brushRadius);
}

std::string ToolPanel::toggleLabel() const {
    if (!toggleEnabled()) {
        return "—";
    }
    if (m_view.tool == kTerrain) {
        return m_view.settings.circle ? "붓 모양: 원 (눌러서 사각형)" : "붓 모양: 사각형 (눌러서 원)";
    }
    return m_view.settings.snap ? "격자 맞춤: 켬" : "격자 맞춤: 끔";
}

std::string ToolPanel::countLine() const {
    if (!m_view.available) {
        return {};
    }
    return std::format("명령 {} · 수락 {} · 거절 {} — 기록 {} · {}", m_view.sent, m_view.accepted, m_view.rejected,
                       m_view.undoDepth, m_view.redoDepth);
}

std::optional<usize> ToolPanel::selectedLine() const {
    for (usize i = 0; i < m_lines.size(); ++i) {
        const Line& l = m_lines[i];
        if ((l.kind == Line::Kind::Prefab && l.text == m_view.settings.prefab) ||
            (l.kind == Line::Kind::Material && l.text == m_view.settings.material)) {
            return i;
        }
    }
    return std::nullopt;
}

void ToolPanel::rebuild() {
    m_lines.clear();
    if (m_view.available && m_view.tool == kPlace) {
        // 팩별 묶음 (ImGui 창과 같다 — 거른 뒤 묶음이 둘 이상이면 머리). 묶음 차례 · 안의 차례는 앱이 준 그대로
        const std::string& filter = m_filter.value();
        std::vector<std::pair<const ToolPrefabGroup*, std::vector<const std::string*>>> shown;
        for (const ToolPrefabGroup& g : m_view.prefabGroups) {
            std::vector<const std::string*> ids;
            for (const std::string& id : g.ids) {
                if (containsIgnoreCase(id, filter)) {
                    ids.push_back(&id);
                }
            }
            if (!ids.empty()) {
                shown.emplace_back(&g, std::move(ids));
            }
        }
        for (const auto& [g, ids] : shown) {
            if (shown.size() > 1) {
                m_lines.push_back({Line::Kind::Header, g->label});
            }
            for (const std::string* id : ids) {
                m_lines.push_back({Line::Kind::Prefab, *id});
            }
        }
    } else if (m_view.available && m_view.tool == kTerrain) {
        for (const std::string& m : m_view.materials) {
            m_lines.push_back({Line::Kind::Material, m});
        }
    }
    kit::setIndexKeys(m_list, m_keys, m_lines.size()); // 줄 차례 — 같은 툴 · 같은 거르기 동안 그대로 (스크롤 유지)
    applyEnabled();
}

void ToolPanel::applyEnabled() {
    if (!m_minus) {
        return;
    }
    for (usize i = 0; i < kTools; ++i) {
        (void)m_tools[i]->setEnabled(m_view.available && (m_view.toolMask & (1u << i)) != 0);
    }
    m_filter.setEnabled(m_view.available && m_view.tool == kPlace);
    (void)m_minus->setEnabled(stepEnabled());
    (void)m_plus->setEnabled(stepEnabled());
    (void)m_toggle->setEnabled(toggleEnabled());
    (void)m_undo->setEnabled(m_view.available && m_view.canEdit && m_view.canUndo);
    (void)m_redo->setEnabled(m_view.available && m_view.canEdit && m_view.canRedo);
}

// ---- 일 ----

void ToolPanel::emit(const ToolSettingsValues& s) {
    m_view.settings = s; // 다음 setView 전에도 고른 값으로 (같은 frame 의 다음 키 · 그리기)
    WorkspaceIntent i = WorkspaceIntent::of(WorkspaceIntent::Kind::ToolSettings);
    i.tools = s;
    m_ctx.intent = std::move(i);
}

void ToolPanel::pick(usize line) {
    if (line >= m_lines.size() || !m_view.available) {
        return;
    }
    const Line& l = m_lines[line];
    ToolSettingsValues s = m_view.settings;
    if (l.kind == Line::Kind::Prefab) {
        s.prefab = l.text;
    } else if (l.kind == Line::Kind::Material) {
        s.material = l.text;
    } else {
        return; // 팩 머리
    }
    (void)m_list.reveal(m_keys[line]);
    if (s != m_view.settings) {
        emit(s);
    }
}

void ToolPanel::pickFirst() {
    for (usize i = 0; i < m_lines.size(); ++i) {
        if (m_lines[i].kind != Line::Kind::Header) {
            pick(i);
            return;
        }
    }
}

void ToolPanel::step(int dir) {
    if (!stepEnabled()) {
        return;
    }
    ToolSettingsValues s = m_view.settings;
    if (m_view.tool == kPlace) {
        // 0.5 칸 눈금에 맞춘 뒤 한 눈금 (ImGui 슬라이더는 끌면 아무 값 — 그 값도 가까운 눈금에서 이어 간다)
        const double ticks = std::round(s.placeSpacing / kSpacingStep) + dir;
        s.placeSpacing = std::clamp(ticks * kSpacingStep, kSpacingMin, kSpacingMax);
    } else {
        s.brushRadius = std::clamp(s.brushRadius + dir, 0, std::max(0, m_view.maxRadius));
    }
    if (s != m_view.settings) {
        emit(s);
    }
}

void ToolPanel::toggle() {
    if (!toggleEnabled()) {
        return;
    }
    ToolSettingsValues s = m_view.settings;
    if (m_view.tool == kTerrain) {
        s.circle = !s.circle;
    } else {
        s.snap = !s.snap;
    }
    emit(s);
}

std::optional<gui::LayoutRect> ToolPanel::rowBounds(usize line) const {
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

gui::EventReply ToolPanel::listEvent(const gui::RoutedEvent& ev) {
    if (ev.phase != gui::EventPhase::target) {
        return {};
    }
    if (const auto* p = std::get_if<gui::PointerEvent>(&ev.input)) {
        if (p->action == gui::PointerAction::down && p->button == gui::PointerButton::primary) {
            (void)m_host->input().setFocus(m_listNode);
            if (const auto row = m_list.hitTest({p->x - m_listBounds.x, p->y - m_listBounds.y})) {
                pick(static_cast<usize>(row->key));
            }
            return {true, true};
        }
        return {};
    }
    // 되돌리기 · 다시 — 툴바 · 단축키와 같은 명령
    for (const gui::KeyModifiers mod : {gui::KeyModifiers::control, gui::KeyModifiers::meta}) {
        if (kit::keyDown(ev, 'Z', mod)) {
            m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::RunCommand, "edit.undo");
            return {true, true};
        }
        if (kit::keyDown(ev, 'Y', mod) || kit::keyDown(ev, 'Z', mod | gui::KeyModifiers::shift)) {
            m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::RunCommand, "edit.redo");
            return {true, true};
        }
    }
    const bool up = kit::keyDown(ev, gui::up_key), down = kit::keyDown(ev, gui::down_key);
    if ((up || down) && !m_lines.empty()) {
        // 고른 줄에서 위 · 아래로 다음 Prefab · 머티리얼 (머리는 건너뛴다) — 고르면 바로 설정 (ImGui 목록의 클릭처럼)
        const std::optional<usize> at = selectedLine();
        std::optional<usize> next;
        if (down) {
            for (usize i = at ? *at + 1 : 0; i < m_lines.size(); ++i) {
                if (m_lines[i].kind != Line::Kind::Header) {
                    next = i;
                    break;
                }
            }
        } else {
            for (usize i = at ? *at : m_lines.size(); i-- > 0;) {
                if (m_lines[i].kind != Line::Kind::Header) {
                    next = i;
                    break;
                }
            }
        }
        if (next) {
            pick(*next);
        }
        return {true, true};
    }
    return {};
}

// ---- 화면 ----

Expected<void> ToolPanel::mount(uihost::UiHost& host) {
    m_host = &host;
    // 뿌리 · 제목 · 툴 막대 (단추 5) · 안내 · 거르기 · 목록 · 값 줄 (이름 · − · 값 · +) · 켜고 끄기 · 되돌리기 줄 (둘) · 수 · 사유
    // (종류: 1 뿌리 · 2 글 · 3 줄 · 4 단추 · 5 목록 · 6 글 칸)
    const std::array<gui::ComponentSpec, 22> specs{{{std::nullopt, 1, 0, true},
                                                    {0, 2, 1, true},
                                                    {0, 3, 2, true},
                                                    {2, 4, 1, true},
                                                    {2, 4, 2, true},
                                                    {2, 4, 3, true},
                                                    {2, 4, 4, true},
                                                    {2, 4, 5, true},
                                                    {0, 2, 3, true},
                                                    {0, 6, 4, true},
                                                    {0, 5, 5, true},
                                                    {0, 3, 6, true},
                                                    {11, 2, 1, true},
                                                    {11, 4, 2, true},
                                                    {11, 2, 3, true},
                                                    {11, 4, 4, true},
                                                    {0, 4, 7, true},
                                                    {0, 3, 8, true},
                                                    {17, 4, 1, true},
                                                    {17, 4, 2, true},
                                                    {0, 2, 9, true},
                                                    {0, 2, 10, true}}};
    auto h = kit::commitTree(host, specs);
    if (!h) {
        return std::unexpected(h.error());
    }
    m_root = (*h)[0];
    m_title = (*h)[1];
    m_bar = (*h)[2];
    for (usize i = 0; i < kTools; ++i) {
        m_toolNodes[i] = (*h)[3 + i];
        m_tools[i] = kit::bindButton(host, m_toolNodes[i], [this, i] {
            if (m_view.available && (m_view.toolMask & (1u << i)) != 0) {
                m_view.tool = static_cast<int>(i); // 다음 setView 전에도 (앱이 Editor::setTool)
                m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::SelectTool, {}, static_cast<int>(i));
            }
        });
    }
    m_hint = (*h)[8];
    m_listNode = (*h)[10];
    m_stepRow = (*h)[11];
    m_stepLabel = (*h)[12];
    m_minusNode = (*h)[13];
    m_valueNode = (*h)[14];
    m_plusNode = (*h)[15];
    m_toggleNode = (*h)[16];
    m_undoRow = (*h)[17];
    m_undoNode = (*h)[18];
    m_redoNode = (*h)[19];
    m_count = (*h)[20];
    m_message = (*h)[21];
    const auto filterKeys = [this](const gui::KeyEvent& k) {
        if (k.action == gui::KeyAction::down && k.chord.key == gui::enter_key && k.chord.modifiers == gui::KeyModifiers::none) {
            pickFirst(); // 거른 것 중 첫 Prefab
            return true;
        }
        return false;
    };
    if (auto r = m_filter.mount(host, (*h)[9], {}, filterKeys); !r) {
        return r;
    }
    m_minus = kit::bindButton(host, m_minusNode, [this] { step(-1); });
    m_plus = kit::bindButton(host, m_plusNode, [this] { step(+1); });
    m_toggle = kit::bindButton(host, m_toggleNode, [this] { toggle(); });
    m_undo = kit::bindButton(host, m_undoNode, [this] {
        m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::RunCommand, "edit.undo");
    });
    m_redo = kit::bindButton(host, m_redoNode, [this] {
        m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::RunCommand, "edit.redo");
    });
    applyEnabled();
    (void)host.input().setFocusable(m_listNode, true);
    (void)host.input().setHandler(m_listNode, [this](const gui::RoutedEvent& ev) { return listEvent(ev); });
    (void)host.setWheelHandler(m_listNode, [this](f32, f32 dy) {
        (void)m_list.scrollBy(-static_cast<double>(dy) * kRowH * 3.0);
        return true;
    });
    return {};
}

Expected<void> ToolPanel::layout(uihost::UiHost& host, const gui::ComponentSnapshot&, std::vector<gui::LayoutNode>& out) {
    rebuild(); // 줄 · 단추 (frame 마다 — 툴 · 거르기 · 콘텐츠가 바뀐다)
    const gui::LayoutSize vp = host.viewport();
    const double inner = fit(vp.width - 16);
    gui::LayoutNode root;
    root.component = m_root;
    root.style.kind = gui::LayoutKind::column;
    root.style.padding = {8, 8, 8, 8};
    root.style.gap = 6;
    out.push_back(root);
    const auto leaf = [&](gui::ComponentHandle c, double w, double h) {
        gui::LayoutNode n;
        n.component = c;
        n.style.preferred_width = w;
        n.style.preferred_height = h;
        out.push_back(n);
    };
    const auto row = [&](gui::ComponentHandle c, double gap) {
        gui::LayoutNode n;
        n.component = c;
        n.style.kind = gui::LayoutKind::row;
        n.style.gap = gap;
        out.push_back(n);
    };
    leaf(m_title, inner, 16);
    row(m_bar, 4);
    const double toolW = fit((inner - 4 * 4) / static_cast<double>(kTools));
    for (const gui::ComponentHandle t : m_toolNodes) {
        leaf(t, toolW, kLine);
    }
    leaf(m_hint, inner, 16);
    leaf(m_filter.handle(), inner, kLine);
    // 고정 높이: 여백 16 · 글 줄 4 개 (16) · 칸 줄 5 개 (26) · 사이 9 개
    leaf(m_listNode, inner, fit(vp.height - 16 - 4 * 16 - 5 * kLine - 9 * 6));
    row(m_stepRow, 6);
    const double stepW = 28;
    const double valueW = 80;
    leaf(m_stepLabel, fit(inner - 2 * stepW - valueW - 3 * 6), kLine);
    leaf(m_minusNode, stepW, kLine);
    leaf(m_valueNode, valueW, kLine);
    leaf(m_plusNode, stepW, kLine);
    leaf(m_toggleNode, inner, kLine);
    row(m_undoRow, 6);
    const double half = fit((inner - 6) / 2);
    leaf(m_undoNode, half, kLine);
    leaf(m_redoNode, half, kLine);
    leaf(m_count, inner, 16);
    leaf(m_message, inner, 16);
    return {};
}

Expected<void> ToolPanel::paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) {
    const services::StudioColors& c = services::studioColors(m_ctx.theme);
    const auto focusR = host.input().focus();
    const std::optional<gui::ComponentHandle> focus = focusR ? *focusR : std::nullopt;
    const auto btn = [&](const gui::LayoutBox& box, const gui::Button& b, const std::string& label, bool accent) {
        out.add(box.component, b.enabled() ? kit::button(host, box, kit::visualOf(b, host), label, 13.0, accent, c)
                                           : kit::disabledButton(host, box, label, 13.0, c));
    };
    const auto text = [&](const gui::LayoutBox& box, const std::string& s, double em, services::Rgba8 color) {
        const double w = box.bounds.width;
        const double h = box.bounds.height;
        out.add(box.component, {gui::DrawText{{0, fit((h - 16) / 2), w, 16}, kFont, em, s, rc(color)}}, true);
    };
    for (const gui::LayoutBox& box : frame.boxes()) {
        const double w = box.bounds.width;
        const double h = box.bounds.height;
        if (box.component == m_root) {
            out.add(box.component, {gui::FillRect{{0, 0, w, h}, rc(c.surface)}});
            continue;
        }
        if (const auto t = std::ranges::find(m_toolNodes, box.component); t != m_toolNodes.end()) {
            const usize i = static_cast<usize>(t - m_toolNodes.begin());
            const std::string label =
                std::format("{} {}", i + 1, m_view.toolNames[i].empty() ? std::string("?") : m_view.toolNames[i]);
            btn(box, *m_tools[i], label, m_view.available && m_view.tool == static_cast<int>(i)); // 지금 툴 = 강조
            continue;
        }
        if (box.component == m_title) {
            text(box, title(), 13.0, m_view.available ? c.text : c.muted);
        } else if (box.component == m_hint) {
            text(box, hint(), 12.5, m_view.available && !m_view.canEdit ? c.warning : c.muted);
        } else if (box.component == m_filter.handle()) {
            m_filter.paint(host, box, focus && *focus == box.component, c, out);
        } else if (box.component == m_stepLabel) {
            text(box, stepLabel(), 13.0, stepEnabled() ? c.text : c.muted);
        } else if (box.component == m_valueNode) {
            out.add(box.component, {gui::FillRect{{0, 0, w, h}, rc(c.elevated)},
                                    gui::DrawText{{6, fit((h - 16) / 2), fit(w - 12), 16}, kFont, 13.0, stepValue(),
                                                  stepEnabled() ? rc(c.text) : rc(c.muted)}});
        } else if (box.component == m_minusNode) {
            btn(box, *m_minus, "−", false);
        } else if (box.component == m_plusNode) {
            btn(box, *m_plus, "+", false);
        } else if (box.component == m_toggleNode) {
            btn(box, *m_toggle, toggleLabel(), false);
        } else if (box.component == m_undoNode) {
            btn(box, *m_undo, m_view.undoLabel.empty() ? std::string("되돌리기") : "되돌리기 " + m_view.undoLabel, false);
        } else if (box.component == m_redoNode) {
            btn(box, *m_redo, m_view.redoLabel.empty() ? std::string("다시") : "다시 " + m_view.redoLabel, false);
        } else if (box.component == m_count) {
            text(box, countLine(), 12.5, c.muted);
        } else if (box.component == m_message) {
            text(box, m_view.available ? m_view.message : std::string(), 12.5, c.warning); // 가장 최근 거절 사유
        } else if (box.component == m_listNode) {
            m_listBounds = box.bounds;
            (void)m_list.resize({w, h}, kRowH);
            std::vector<gui::DrawCommand> d;
            d.push_back(gui::FillRect{{0, 0, w, h}, rc(c.background)});
            const bool listFocused = focus && *focus == m_listNode;
            const std::optional<usize> sel = selectedLine();
            const gui::ListRange range = m_list.visibleRange();
            for (usize i = range.first; i < range.past; ++i) {
                const auto vr = m_list.visibleRow(i);
                if (!vr || vr->key >= m_lines.size()) {
                    continue;
                }
                const usize index = static_cast<usize>(vr->key);
                const Line& l = m_lines[index];
                const gui::LayoutRect b = vr->bounds;
                if (l.kind == Line::Kind::Header) {
                    d.push_back(gui::FillRect{{b.x, b.y + 1, b.width, fit(b.height - 2)}, rc(c.elevated)});
                    d.push_back(gui::DrawText{{b.x + 6, b.y + 2, fit(b.width - 12), 16}, kFont, 12.5, l.text, rc(c.accent)});
                    continue;
                }
                if (sel == index) {
                    d.push_back(gui::FillRect{b, listFocused ? rc(c.header) : rc(c.headerHover)});
                    d.push_back(gui::FillRect{{b.x, b.y, 2, b.height}, rc(c.accent)});
                }
                d.push_back(gui::DrawText{{b.x + 12, b.y + 2, fit(b.width - 18), 16}, kFont, 13.0, l.text, rc(c.text)});
            }
            if (m_lines.empty()) {
                std::string empty;
                if (!m_view.available) {
                    empty = "프로젝트 밖";
                } else if (m_view.tool == kPlace) {
                    empty = m_filter.value().empty() ? "Prefab 이 없다" : "거르기에 맞는 Prefab 이 없다";
                } else if (m_view.tool == kTerrain) {
                    empty = "지형 머티리얼이 없다";
                } else {
                    empty = "이 툴은 고를 목록이 없다 — 배치 (3) · 지형 (4) 툴에서 쓴다";
                }
                d.push_back(gui::DrawText{{8, 8, fit(w - 16), 18}, kFont, 13.0, empty, rc(c.muted)});
            }
            out.add(box.component, std::move(d), true);
        }
    }
    return {};
}

} // namespace sbx::editor::gui
