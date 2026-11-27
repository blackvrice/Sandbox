#include "editor/gui/HierarchyPanel.hpp"

#include <algorithm>
#include <array>
#include <format>

#include "editor/gui/PanelKit.hpp"

namespace sbx::editor::gui {

using kit::fit;
using kit::kFont;
using kit::rc;

namespace {

constexpr double kRowH = 20.0;
constexpr double kIndent = 14.0; // 그룹 안 개체 (ImGui 창의 Indent 와 같은 값)
// 목록 키 — 개체 = netId, 그룹 머리 = 위 비트 + 그룹 차례 (netId 는 32 비트)
constexpr gui::ListItemKey kGroupKey = gui::ListItemKey{1} << 63;

} // namespace

void HierarchyPanel::setView(HierarchyView view) {
    m_view = std::move(view);
    applyEnabled();
}

bool HierarchyPanel::selected(NetEntityId id) const {
    return std::ranges::binary_search(m_view.selection, id);
}

std::string HierarchyPanel::status() const {
    if (!m_view.available) {
        return "프로젝트가 열려 있지 않다 (또는 편집할 수 없는 접속) — 월드를 열면 개체 목록";
    }
    return std::format("개체 {} / {} (관심 영역 안) · 선택 {}", m_shown, m_view.rows.size(), m_view.selection.size());
}

void HierarchyPanel::applyEnabled() {
    if (m_group) {
        // ImGui 창과 같다 — 묶기는 선택 · 이름이 있을 때, 빼기는 선택이 있을 때
        (void)m_group->setEnabled(m_view.available && !m_view.selection.empty() && !m_groupName.value().empty());
        (void)m_ungroup->setEnabled(m_view.available && !m_view.selection.empty());
    }
}

void HierarchyPanel::rebuild() {
    m_lines.clear();
    m_shown = 0;
    if (m_view.available && m_view.groups != nullptr) {
        // 줄은 서비스가 만든다 (ImGui "목록" 창과 같은 buildHierarchy — 그룹 머리 · 펼친 그룹의 개체 · 그룹 없는 개체)
        std::vector<HierarchyLine> lines;
        buildHierarchy(m_view.rows, *m_view.groups, parseHierarchyQuery(m_search.value(), m_view.content), lines);
        const auto& groups = m_view.groups->groups();
        for (const HierarchyLine& l : lines) {
            Line out;
            if (l.kind == HierarchyLine::Kind::Group) {
                const EditorGroup& g = groups[l.group];
                out.group = true;
                out.groupName = g.name;
                out.text = std::format("{} {}  ({})", g.collapsed ? "▸" : "▾", g.name, l.matches);
            } else {
                const HierarchyRow& r = m_view.rows[l.row];
                out.id = r.id;
                out.inGroup = l.inGroup;
                out.text = std::format("{} #{}", r.prefab.empty() ? std::string_view("개체") : r.prefab, r.id);
                ++m_shown;
            }
            m_lines.push_back(std::move(out));
        }
    }
    std::vector<gui::ListItemKey> keys;
    keys.reserve(m_lines.size());
    usize groupIndex = 0;
    for (const Line& l : m_lines) {
        keys.push_back(l.group ? kGroupKey | groupIndex++ : static_cast<gui::ListItemKey>(l.id));
    }
    if (keys != m_keys) { // 같으면 그대로 (스크롤 · 크기 계산을 frame 마다 다시 하지 않는다)
        m_keys = std::move(keys);
        (void)m_list.setItems(m_keys);
    }
    m_cursor = m_lines.empty() ? 0 : std::min(m_cursor, m_lines.size() - 1);
}

void HierarchyPanel::selectOnly(NetEntityId id) {
    WorkspaceIntent i = WorkspaceIntent::of(WorkspaceIntent::Kind::SelectEntities);
    i.entities = {id};
    m_ctx.intent = std::move(i);
}

void HierarchyPanel::clickLine(usize line, gui::KeyModifiers mods) {
    if (line >= m_lines.size()) {
        return;
    }
    m_cursor = line;
    const Line& l = m_lines[line];
    if (l.group) { // 머리 = 접기 · 펴기 (ImGui 창과 같다)
        m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::ToggleGroup, l.groupName);
        return;
    }
    // 두 번 누르기 = 카메라 맞추기 (ImGui 창의 IsMouseDoubleClicked — 여기서는 호스트 frame 차례로)
    const u64 frame = m_host->stats().frames;
    if (m_lastClick == line && frame - m_lastClickFrame <= kDoubleClickFrames &&
        (static_cast<unsigned>(mods) & static_cast<unsigned>(gui::KeyModifiers::control)) == 0) {
        m_lastClick.reset();
        m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::FocusSelection);
        return;
    }
    m_lastClick = line;
    m_lastClickFrame = frame;
    const bool ctrl = (static_cast<unsigned>(mods) &
                       (static_cast<unsigned>(gui::KeyModifiers::control) | static_cast<unsigned>(gui::KeyModifiers::meta))) != 0;
    if (!ctrl) {
        selectOnly(l.id);
        return;
    }
    // Ctrl (macOS Cmd) = 더하기 · 빼기 (오름차순 유지)
    std::vector<u32> next(m_view.selection.begin(), m_view.selection.end());
    if (selected(l.id)) {
        std::erase(next, l.id);
    } else {
        next.insert(std::ranges::lower_bound(next, l.id), l.id);
    }
    WorkspaceIntent i = WorkspaceIntent::of(WorkspaceIntent::Kind::SelectEntities);
    i.entities = std::move(next);
    m_ctx.intent = std::move(i);
}

std::optional<gui::LayoutRect> HierarchyPanel::rowBounds(usize line) const {
    if (line >= m_keys.size()) {
        return std::nullopt;
    }
    const gui::ListRange range = m_list.visibleRange();
    for (usize i = range.first; i < range.past; ++i) {
        if (const auto row = m_list.visibleRow(i); row && row->key == m_keys[line]) {
            return row->bounds;
        }
    }
    return std::nullopt;
}

gui::EventReply HierarchyPanel::listEvent(const gui::RoutedEvent& ev) {
    if (ev.phase != gui::EventPhase::target) {
        return {};
    }
    if (const auto* p = std::get_if<gui::PointerEvent>(&ev.input)) {
        if (p->action == gui::PointerAction::down && p->button == gui::PointerButton::primary) {
            (void)m_host->input().setFocus(m_listNode);
            if (const auto row = m_list.hitTest({p->x - m_listBounds.x, p->y - m_listBounds.y})) {
                if (const auto at = std::ranges::find(m_keys, row->key); at != m_keys.end()) {
                    clickLine(static_cast<usize>(at - m_keys.begin()), p->modifiers);
                }
            }
            return {true, true};
        }
        return {};
    }
    if (m_lines.empty()) {
        return {};
    }
    const bool up = kit::keyDown(ev, gui::up_key), down = kit::keyDown(ev, gui::down_key);
    if (up || down) { // ↑↓ = 그 줄만 고르기 (그룹 머리는 건너뛴다)
        usize at = m_cursor;
        for (usize step = 0; step < m_lines.size(); ++step) {
            if (up ? at == 0 : at + 1 >= m_lines.size()) {
                break;
            }
            at = up ? at - 1 : at + 1;
            if (!m_lines[at].group) {
                m_cursor = at;
                selectOnly(m_lines[at].id);
                (void)m_list.reveal(m_keys[at]);
                break;
            }
        }
        return {true, true};
    }
    if (kit::keyDown(ev, gui::enter_key) || kit::keyDown(ev, 'F')) {
        if (m_lines[m_cursor].group) {
            m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::ToggleGroup, m_lines[m_cursor].groupName);
        } else {
            m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::FocusSelection);
        }
        return {true, true};
    }
    if (kit::keyDown(ev, gui::delete_key) && m_view.canDelete && !m_view.selection.empty()) {
        m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::DeleteSelection);
        return {true, true};
    }
    return {};
}

Expected<void> HierarchyPanel::mount(uihost::UiHost& host) {
    m_host = &host;
    // 뿌리 · 검색 · 상태 · 막대 (그룹 이름 · 묶기 · 빼기) · 목록
    const std::array<gui::ComponentSpec, 8> specs{{{std::nullopt, 1, 0, true},
                                                   {0, 2, 1, true},
                                                   {0, 3, 2, true},
                                                   {0, 4, 3, true},
                                                   {3, 2, 1, true},
                                                   {3, 5, 2, true},
                                                   {3, 5, 3, true},
                                                   {0, 6, 4, true}}};
    auto h = kit::commitTree(host, specs);
    if (!h) {
        return std::unexpected(h.error());
    }
    m_root = (*h)[0];
    m_status = (*h)[2];
    m_bar = (*h)[3];
    m_groupNode = (*h)[5];
    m_ungroupNode = (*h)[6];
    m_listNode = (*h)[7];
    if (auto r = m_search.mount(host, (*h)[1]); !r) {
        return r;
    }
    if (auto r = m_groupName.mount(host, (*h)[4], [this](const std::string&) { applyEnabled(); }); !r) {
        return r;
    }
    m_group = kit::bindButton(host, m_groupNode, [this] {
        m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::GroupSelection, m_groupName.value());
    });
    m_ungroup = kit::bindButton(host, m_ungroupNode,
                                [this] { m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::UngroupSelection); });
    applyEnabled();
    (void)host.input().setFocusable(m_listNode, true);
    (void)host.input().setHandler(m_listNode, [this](const gui::RoutedEvent& ev) { return listEvent(ev); });
    (void)host.setWheelHandler(m_listNode, [this](f32, f32 dy) {
        (void)m_list.scrollBy(-static_cast<double>(dy) * kRowH * 3.0);
        return true;
    });
    return {};
}

Expected<void> HierarchyPanel::layout(uihost::UiHost& host, const gui::ComponentSnapshot&, std::vector<gui::LayoutNode>& out) {
    rebuild(); // 줄 · 검색 (frame 마다 — ImGui 창도 frame 마다 만든다)
    const gui::LayoutSize vp = host.viewport();
    const double inner = fit(vp.width - 16);
    gui::LayoutNode root;
    root.component = m_root;
    root.style.kind = gui::LayoutKind::column;
    root.style.padding = {8, 8, 8, 8};
    root.style.gap = 6;
    out.push_back(root);
    const std::array<std::pair<gui::ComponentHandle, double>, 2> top{{{m_search.handle(), 28}, {m_status, 16}}};
    for (const auto& [node, hgt] : top) {
        gui::LayoutNode n;
        n.component = node;
        n.style.preferred_width = inner;
        n.style.preferred_height = hgt;
        out.push_back(n);
    }
    gui::LayoutNode bar;
    bar.component = m_bar;
    bar.style.kind = gui::LayoutKind::row;
    bar.style.gap = 6;
    out.push_back(bar);
    const std::array<std::pair<gui::ComponentHandle, double>, 3> parts{
        {{m_groupName.handle(), std::max(80.0, inner - 2 * (56 + 6))}, {m_groupNode, 56}, {m_ungroupNode, 56}}};
    for (const auto& [node, w] : parts) {
        gui::LayoutNode n;
        n.component = node;
        n.style.preferred_width = w;
        n.style.preferred_height = 26;
        out.push_back(n);
    }
    gui::LayoutNode list;
    list.component = m_listNode;
    list.style.preferred_width = inner;
    list.style.preferred_height = fit(vp.height - 16 - 28 - 16 - 26 - 18);
    out.push_back(list);
    return {};
}

Expected<void> HierarchyPanel::paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) {
    const services::StudioColors& c = services::studioColors(m_ctx.theme);
    const auto focusR = host.input().focus();
    const std::optional<gui::ComponentHandle> focus = focusR ? *focusR : std::nullopt;
    for (const gui::LayoutBox& box : frame.boxes()) {
        const double w = box.bounds.width;
        const double h = box.bounds.height;
        if (box.component == m_root) {
            out.add(box.component, {gui::FillRect{{0, 0, w, h}, rc(c.surface)}});
        } else if (box.component == m_search.handle()) {
            m_search.paint(host, box, focus && *focus == box.component, c, out);
        } else if (box.component == m_groupName.handle()) {
            m_groupName.paint(host, box, focus && *focus == box.component, c, out);
        } else if (box.component == m_status) {
            out.add(box.component, {gui::DrawText{{0, 0, w, h}, kFont, 12.5, status(), rc(c.muted)}}, true);
        } else if (box.component == m_groupNode || box.component == m_ungroupNode) {
            const bool grp = box.component == m_groupNode;
            const bool enabled = m_view.available && !m_view.selection.empty() && (!grp || !m_groupName.value().empty());
            const std::string label = grp ? "묶기" : "빼기";
            out.add(box.component, enabled ? kit::button(host, box, kit::visualOf(grp ? *m_group : *m_ungroup, host),
                                                         label, 13.0, false, c)
                                           : kit::disabledButton(host, box, label, 13.0, c));
        } else if (box.component == m_listNode) {
            m_listBounds = box.bounds;
            (void)m_list.resize({w, h}, kRowH);
            std::vector<gui::DrawCommand> d;
            d.push_back(gui::FillRect{{0, 0, w, h}, rc(c.background)});
            const bool listFocused = focus && *focus == m_listNode;
            const gui::ListRange range = m_list.visibleRange();
            for (usize i = range.first; i < range.past; ++i) {
                const auto row = m_list.visibleRow(i);
                if (!row) {
                    continue;
                }
                const auto at = std::ranges::find(m_keys, row->key);
                if (at == m_keys.end()) {
                    continue;
                }
                const usize index = static_cast<usize>(at - m_keys.begin());
                const Line& l = m_lines[index];
                const gui::LayoutRect b = row->bounds;
                if (!l.group && selected(l.id)) {
                    d.push_back(gui::FillRect{b, listFocused ? rc(c.header) : rc(c.elevated)});
                }
                if (listFocused && index == m_cursor) {
                    d.push_back(gui::FillRect{{b.x, b.y, 2, b.height}, rc(c.accent)});
                }
                const double x = b.x + 6 + (l.inGroup ? kIndent : 0.0);
                d.push_back(gui::DrawText{{x, b.y + 2, fit(b.x + b.width - x - 6), 16}, kFont, l.group ? 13.0 : 12.5, l.text,
                                          l.group ? rc(c.accent) : rc(c.text)});
            }
            if (m_lines.empty()) {
                d.push_back(gui::DrawText{{8, 8, fit(w - 16), 18}, kFont, 13.0,
                                          m_view.available ? "맞는 개체가 없다" : "개체가 없다", rc(c.muted)});
            }
            out.add(box.component, std::move(d), true);
        }
    }
    return {};
}

} // namespace sbx::editor::gui
