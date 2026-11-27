#include "editor/gui/StudioPanels.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>

#include <graphics-ui/core/version.hpp>

#include "editor/gui/PanelKit.hpp"
#include "foundation/log/Log.hpp"

namespace sbx::editor::gui {

using kit::fit;
using kit::kFont;
using kit::rc;

namespace {

bool containsNoCase(std::string_view hay, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    return std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                       [&](char a, char b) { return lower(a) == lower(b); }) != hay.end();
}

bool keyDown(const gui::RoutedEvent& ev, gui::KeyCode key, gui::KeyModifiers mods = gui::KeyModifiers::none) {
    const auto* k = std::get_if<gui::KeyEvent>(&ev.input);
    return k != nullptr && k->action == gui::KeyAction::down && k->chord.key == key && k->chord.modifiers == mods;
}

// 부모 우선 게시 — 핸들은 snapshot 의 같은 차례
Expected<std::vector<gui::ComponentHandle>> commitTree(uihost::UiHost& host, std::span<const gui::ComponentSpec> specs) {
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

// 가상 목록의 한 칸 이동 · 끝 — 고르고 보이게
void moveIn(gui::ListView& list, std::ptrdiff_t delta, bool toStart, bool toEnd) {
    const usize n = list.itemCount();
    if (n == 0) {
        return;
    }
    std::ptrdiff_t at = 0;
    if (const auto key = list.selection()) {
        at = static_cast<std::ptrdiff_t>(list.indexOf(*key).value_or(0));
    }
    at = toStart ? 0 : toEnd ? static_cast<std::ptrdiff_t>(n) - 1 : at + delta;
    at = std::clamp<std::ptrdiff_t>(at, 0, static_cast<std::ptrdiff_t>(n) - 1);
    const auto key = list.itemKey(static_cast<usize>(at));
    (void)list.select(key);
    if (key) {
        (void)list.reveal(*key);
    }
}

} // namespace

// ---- ConsolePanel ----

Expected<void> ConsolePanel::mount(uihost::UiHost& host) {
    m_host = &host;
    const std::array<gui::ComponentSpec, 5> specs{
        {{std::nullopt, 1, 0, true}, {0, 2, 1, true}, {1, 3, 1, true}, {1, 4, 2, true}, {0, 5, 2, true}}};
    auto h = commitTree(host, specs);
    if (!h) {
        return std::unexpected(h.error());
    }
    m_root = (*h)[0];
    m_bar = (*h)[1];
    m_followNode = (*h)[3];
    m_listNode = (*h)[4];
    if (auto r = m_filter.mount(host, (*h)[2], [this](const std::string&) { refresh(true); }); !r) {
        return r;
    }
    m_followButton = std::make_unique<gui::Button>(host.input(), m_followNode);
    (void)m_followButton->setEnabled(true);
    (void)host.input().setHandler(m_followNode, [this](const gui::RoutedEvent& ev) -> gui::EventReply {
        const auto r = m_followButton->handle(ev);
        if (r && r->activated) {
            m_follow = !m_follow;
            refresh(true);
        }
        return r ? r->reply : gui::EventReply{};
    });
    (void)host.input().setFocusable(m_listNode, true);
    (void)host.input().setHandler(m_listNode, [this](const gui::RoutedEvent& ev) { return listEvent(ev); });
    if (auto r = host.setWheelHandler(m_listNode, [this](f32, f32 dy) {
            (void)m_list.scrollBy(-static_cast<double>(dy) * 20.0 * 3.0);
            m_follow = false; // 손으로 밀면 따라가기를 끈다 (콘솔 창과 같다)
            return true;
        });
        !r) {
        return r;
    }
    refresh(true);
    return {};
}

void ConsolePanel::refresh(bool force) {
    const std::uint64_t total = log::recentTotal();
    if (!force && total == m_total && m_applied == m_filter.value()) {
        return;
    }
    m_lines = log::recentLines();
    m_total = total;
    m_first = total >= m_lines.size() ? total - m_lines.size() : 0;
    m_applied = m_filter.value();
    std::vector<gui::ListItemKey> keys;
    for (usize i = 0; i < m_lines.size(); ++i) {
        if (containsNoCase(m_lines[i], m_applied)) {
            keys.push_back(m_first + i);
        }
    }
    const auto sel = m_list.selection();
    (void)m_list.setItems(keys);
    if (sel) {
        (void)m_list.select(sel); // 아직 있으면 남는다 (ListView 가 없는 키를 푼다)
    }
    if (m_follow && !keys.empty()) {
        (void)m_list.reveal(keys.back());
    }
}

std::optional<std::string> ConsolePanel::selectedLine() const {
    const auto key = m_list.selection();
    if (!key || *key < m_first || *key - m_first >= m_lines.size()) {
        return std::nullopt;
    }
    return m_lines[static_cast<usize>(*key - m_first)];
}

gui::EventReply ConsolePanel::listEvent(const gui::RoutedEvent& ev) {
    if (ev.phase != gui::EventPhase::target) {
        return {};
    }
    if (const auto* p = std::get_if<gui::PointerEvent>(&ev.input)) {
        if (p->action == gui::PointerAction::down && p->button == gui::PointerButton::primary) {
            (void)m_host->input().setFocus(m_listNode);
            if (const auto row = m_list.hitTest({p->x - m_listBounds.x, p->y - m_listBounds.y})) {
                (void)m_list.select(row->key);
            }
            return {true, true};
        }
        return {};
    }
    // 고른 줄 복사 (창 클립보드 — 호스트의 편집 콜백). Ctrl+C · macOS Cmd+C (meta)
    if (keyDown(ev, 'C', gui::KeyModifiers::control) || keyDown(ev, 'C', gui::KeyModifiers::meta)) {
        if (const auto line = selectedLine()) {
            const gui::TextEditCallbacks cb = m_host->editCallbacks({});
            if (cb.write_clipboard) {
                (void)cb.write_clipboard(*line);
            }
        }
        return {true, true};
    }
    const bool up = keyDown(ev, gui::up_key), down = keyDown(ev, gui::down_key);
    const bool home = keyDown(ev, gui::home_key), end = keyDown(ev, gui::end_key);
    if (up || down || home || end) {
        moveIn(m_list, up ? -1 : 1, home, end);
        m_follow = end; // 맨 끝으로 가면 다시 따라간다
        return {true, true};
    }
    return {};
}

Expected<void> ConsolePanel::layout(uihost::UiHost& host, const gui::ComponentSnapshot&,
                                    std::vector<gui::LayoutNode>& out) {
    refresh(false); // 새 로그 줄 · 거르기 (프레임마다 — 바뀔 때만 다시 읽는다)
    const gui::LayoutSize vp = host.viewport();
    const double inner = fit(vp.width - 16);
    gui::LayoutNode root;
    root.component = m_root;
    root.style.kind = gui::LayoutKind::column;
    root.style.padding = {8, 8, 8, 8};
    root.style.gap = 6;
    out.push_back(root);
    gui::LayoutNode bar;
    bar.component = m_bar;
    bar.style.kind = gui::LayoutKind::row;
    bar.style.gap = 6;
    out.push_back(bar);
    gui::LayoutNode filter;
    filter.component = m_filter.handle();
    filter.style.preferred_width = fit(inner - 130);
    filter.style.preferred_height = 28;
    out.push_back(filter);
    gui::LayoutNode follow;
    follow.component = m_followNode;
    follow.style.preferred_width = 124;
    follow.style.preferred_height = 28;
    out.push_back(follow);
    gui::LayoutNode list;
    list.component = m_listNode;
    list.style.preferred_width = inner;
    list.style.preferred_height = fit(vp.height - 16 - 28 - 6);
    out.push_back(list);
    return {};
}

Expected<void> ConsolePanel::paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) {
    const services::StudioColors& c = services::studioColors(m_ctx.theme);
    const auto focusR = host.input().focus();
    const std::optional<gui::ComponentHandle> focus = focusR ? *focusR : std::nullopt;
    for (const gui::LayoutBox& box : frame.boxes()) {
        const double w = box.bounds.width;
        const double h = box.bounds.height;
        if (box.component == m_filter.handle()) {
            m_filter.paint(host, box, focus && *focus == box.component, c, out);
        } else if (box.component == m_followNode) {
            gui::ControlVisualState vs;
            if (auto v = m_followButton->visualState(host.pointerHit())) {
                vs = *v;
            }
            out.add(box.component, kit::button(host, box, vs, m_follow ? "따라가기 켬" : "따라가기 끔", 13.0, m_follow, c));
        } else if (box.component == m_listNode) {
            m_listBounds = box.bounds;
            constexpr double rowH = 20.0;
            (void)m_list.resize({w, h}, rowH);
            std::vector<gui::DrawCommand> d;
            d.push_back(gui::FillRect{{0, 0, w, h}, rc(c.background)});
            const bool listFocused = focus && *focus == m_listNode;
            const gui::ListRange range = m_list.visibleRange();
            for (usize i = range.first; i < range.past; ++i) {
                const auto row = m_list.visibleRow(i);
                if (!row || row->key < m_first || row->key - m_first >= m_lines.size()) {
                    continue;
                }
                const std::string& line = m_lines[static_cast<usize>(row->key - m_first)];
                if (row->selected) {
                    d.push_back(gui::FillRect{row->bounds, listFocused ? rc(c.header) : rc(c.elevated)});
                }
                // 수준 색 — ImGui 콘솔과 같은 규칙 (줄의 "[warn]" · "[error]")
                const gui::RenderColor color = line.find("[error]") != std::string::npos ? rc(c.error)
                                               : line.find("[warn]") != std::string::npos ? rc(c.warning)
                                                                                          : rc(c.text);
                d.push_back(gui::DrawText{{row->bounds.x + 6, row->bounds.y + 2, fit(row->bounds.width - 12), 16}, kFont,
                                          12.5, line, color});
            }
            if (m_list.itemCount() == 0) {
                d.push_back(gui::DrawText{{8, 8, fit(w - 16), 18}, kFont, 13.0,
                                          m_lines.empty() ? "로그가 없다" : "거르기에 맞는 줄이 없다", rc(c.muted)});
            }
            out.add(box.component, std::move(d), true);
        } else if (box.component == m_root) {
            out.add(box.component, {gui::FillRect{{0, 0, w, h}, rc(c.surface)}});
        }
    }
    return {};
}

// ---- ShortcutPanel ----

Expected<void> ShortcutPanel::mount(uihost::UiHost& host) {
    m_host = &host;
    const std::array<gui::ComponentSpec, 3> specs{{{std::nullopt, 1, 0, true}, {0, 2, 1, true}, {0, 3, 2, true}}};
    auto h = commitTree(host, specs);
    if (!h) {
        return std::unexpected(h.error());
    }
    m_root = (*h)[0];
    m_listNode = (*h)[2];
    const auto keys = [this](const gui::KeyEvent& k) {
        if (k.action != gui::KeyAction::down || k.chord.modifiers != gui::KeyModifiers::none) {
            return false;
        }
        if (k.chord.key == gui::enter_key) { // 검색 칸에서 Enter = 고른 명령 실행 (팔레트와 같다)
            runSelected();
            return true;
        }
        if (k.chord.key == gui::down_key || k.chord.key == gui::up_key) {
            moveIn(m_list, k.chord.key == gui::down_key ? 1 : -1, false, false);
            return true;
        }
        return false;
    };
    if (auto r = m_search.mount(host, (*h)[1], [this](const std::string&) { refilter(); }, keys); !r) {
        return r;
    }
    (void)host.input().setFocusable(m_listNode, true);
    (void)host.input().setHandler(m_listNode, [this](const gui::RoutedEvent& ev) { return listEvent(ev); });
    (void)host.setWheelHandler(m_listNode, [this](f32, f32 dy) {
        (void)m_list.scrollBy(-static_cast<double>(dy) * 26.0 * 3.0);
        return true;
    });
    refilter();
    return {};
}

void ShortcutPanel::refilter() {
    m_rows = m_commands.search(m_search.value());
    std::vector<gui::ListItemKey> keys;
    for (usize i = 0; i < m_rows.size(); ++i) {
        keys.push_back(i);
    }
    (void)m_list.setItems(keys);
    (void)m_list.select(keys.empty() ? std::nullopt : std::optional<gui::ListItemKey>(0));
    (void)m_list.scrollTo(0);
}

void ShortcutPanel::runSelected() {
    const auto key = m_list.selection();
    if (key && *key < m_rows.size()) {
        m_ctx.intent = WorkspaceIntent{WorkspaceIntent::Kind::RunCommand, m_rows[static_cast<usize>(*key)]->id};
    }
}

gui::EventReply ShortcutPanel::listEvent(const gui::RoutedEvent& ev) {
    if (ev.phase != gui::EventPhase::target) {
        return {};
    }
    if (const auto* p = std::get_if<gui::PointerEvent>(&ev.input)) {
        if (p->action == gui::PointerAction::down && p->button == gui::PointerButton::primary) {
            (void)m_host->input().setFocus(m_listNode);
            if (const auto row = m_list.hitTest({p->x - m_listBounds.x, p->y - m_listBounds.y})) {
                (void)m_list.select(row->key);
            }
            return {true, true};
        }
        return {};
    }
    if (keyDown(ev, gui::enter_key)) {
        runSelected();
        return {true, true};
    }
    const bool up = keyDown(ev, gui::up_key), down = keyDown(ev, gui::down_key);
    if (up || down) {
        moveIn(m_list, up ? -1 : 1, false, false);
        return {true, true};
    }
    return {};
}

Expected<void> ShortcutPanel::layout(uihost::UiHost& host, const gui::ComponentSnapshot&,
                                     std::vector<gui::LayoutNode>& out) {
    const gui::LayoutSize vp = host.viewport();
    const double inner = fit(vp.width - 16);
    gui::LayoutNode root;
    root.component = m_root;
    root.style.kind = gui::LayoutKind::column;
    root.style.padding = {8, 8, 8, 8};
    root.style.gap = 6;
    out.push_back(root);
    gui::LayoutNode search;
    search.component = m_search.handle();
    search.style.preferred_width = inner;
    search.style.preferred_height = 28;
    out.push_back(search);
    gui::LayoutNode list;
    list.component = m_listNode;
    list.style.preferred_width = inner;
    list.style.preferred_height = fit(vp.height - 16 - 28 - 6);
    out.push_back(list);
    return {};
}

Expected<void> ShortcutPanel::paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) {
    const services::StudioColors& c = services::studioColors(m_ctx.theme);
    const auto focusR = host.input().focus();
    const std::optional<gui::ComponentHandle> focus = focusR ? *focusR : std::nullopt;
    for (const gui::LayoutBox& box : frame.boxes()) {
        const double w = box.bounds.width;
        const double h = box.bounds.height;
        if (box.component == m_search.handle()) {
            m_search.paint(host, box, focus && *focus == box.component, c, out);
        } else if (box.component == m_listNode) {
            m_listBounds = box.bounds;
            constexpr double rowH = 26.0;
            (void)m_list.resize({w, h}, rowH);
            std::vector<gui::DrawCommand> d;
            d.push_back(gui::FillRect{{0, 0, w, h}, rc(c.background)});
            const gui::ListRange range = m_list.visibleRange();
            for (usize i = range.first; i < range.past; ++i) {
                const auto row = m_list.visibleRow(i);
                if (!row || row->key >= m_rows.size()) {
                    continue;
                }
                const services::CommandSpec& cmd = *m_rows[static_cast<usize>(row->key)];
                const gui::LayoutRect r = row->bounds;
                if (row->selected) {
                    d.push_back(gui::FillRect{r, rc(c.header)});
                }
                const bool enabled = !cmd.when || cmd.when();
                const std::string key = !cmd.shortcut.empty() ? cmd.shortcut : cmd.keyHint;
                const double kw = key.empty() ? 0.0 : host.text().measure(key, 12.5).width;
                d.push_back(gui::DrawText{{r.x + 8, r.y + 5, fit(r.width - 24 - kw), 17}, kFont, 13.0, cmd.title,
                                          enabled ? rc(c.text) : rc(c.muted)});
                if (!key.empty()) {
                    d.push_back(gui::DrawText{{fit(r.x + r.width - 8 - kw), r.y + 5, kw, 17}, kFont, 12.5, key,
                                              rc(c.accent)});
                }
            }
            if (m_rows.empty()) {
                d.push_back(gui::DrawText{{8, 8, fit(w - 16), 18}, kFont, 13.0, "맞는 명령이 없다", rc(c.muted)});
            }
            out.add(box.component, std::move(d), true);
        } else if (box.component == m_root) {
            out.add(box.component, {gui::FillRect{{0, 0, w, h}, rc(c.surface)}});
        }
    }
    return {};
}

// ---- InfoPanel ----

std::vector<std::string> InfoPanel::lines() const {
    const auto& v = graphics_ui::core::version;
    return {
        "Sandbox Studio — GraphicsUI 작업 공간 (미리보기 · UI-R2-05)",
        std::format("엔진 {} · GraphicsUI Core {}.{}.{} · 테마 {} · 간격 {} · 배율 {:.2f}", m_version, v.major, v.minor,
                    v.patch, services::studioThemeName(m_ctx.theme), m_ctx.compact ? "촘촘" : "보통", m_ctx.scale),
        "탭을 끌어 옮긴다 — 탭 줄 = 그 탭 앞 · 뒤, 칸 가장자리 = 위 · 아래 · 왼쪽 · 오른쪽으로 나누기, 가운데 = 그 칸의 탭",
        "분할선을 끌어 크기를 바꾼다 · Esc = 끌기 취소 · Ctrl+PageUp · PageDown = 칸 안의 탭 바꾸기",
        "콘솔: 거르기 · 따라가기 · ↑↓ · Ctrl+C (고른 줄 복사)   단축키: 검색 · ↑↓ · Enter (그 명령 실행)",
    };
}

Expected<void> InfoPanel::mount(uihost::UiHost& host) {
    const std::array<gui::ComponentSpec, 5> specs{
        {{std::nullopt, 1, 0, true}, {0, 2, 1, true}, {0, 3, 2, true}, {2, 4, 1, true}, {2, 4, 2, true}}};
    auto h = commitTree(host, specs);
    if (!h) {
        return std::unexpected(h.error());
    }
    m_root = (*h)[0];
    m_text = (*h)[1];
    m_bar = (*h)[2];
    m_homeNode = (*h)[3];
    m_resetNode = (*h)[4];
    m_home = std::make_unique<gui::Button>(host.input(), m_homeNode);
    m_reset = std::make_unique<gui::Button>(host.input(), m_resetNode);
    (void)m_home->setEnabled(true);
    (void)m_reset->setEnabled(true);
    (void)host.input().setHandler(m_homeNode, [this](const gui::RoutedEvent& ev) -> gui::EventReply {
        const auto r = m_home->handle(ev);
        if (r && r->activated) {
            m_ctx.intent = WorkspaceIntent{WorkspaceIntent::Kind::Home, {}};
        }
        return r ? r->reply : gui::EventReply{};
    });
    (void)host.input().setHandler(m_resetNode, [this](const gui::RoutedEvent& ev) -> gui::EventReply {
        const auto r = m_reset->handle(ev);
        if (r && r->activated) {
            m_ctx.intent = WorkspaceIntent{WorkspaceIntent::Kind::ResetLayout, {}};
        }
        return r ? r->reply : gui::EventReply{};
    });
    return {};
}

Expected<void> InfoPanel::layout(uihost::UiHost& host, const gui::ComponentSnapshot&, std::vector<gui::LayoutNode>& out) {
    const gui::LayoutSize vp = host.viewport();
    gui::LayoutNode root;
    root.component = m_root;
    root.style.kind = gui::LayoutKind::column;
    root.style.padding = {12, 12, 12, 12};
    root.style.gap = 10;
    out.push_back(root);
    gui::LayoutNode text;
    text.component = m_text;
    text.style.preferred_width = fit(vp.width - 24);
    text.style.preferred_height = static_cast<double>(lines().size()) * 20.0;
    out.push_back(text);
    gui::LayoutNode bar;
    bar.component = m_bar;
    bar.style.kind = gui::LayoutKind::row;
    bar.style.gap = 8;
    out.push_back(bar);
    for (const gui::ComponentHandle b : {m_homeNode, m_resetNode}) {
        gui::LayoutNode n;
        n.component = b;
        n.style.preferred_width = 120;
        n.style.preferred_height = 30;
        out.push_back(n);
    }
    return {};
}

Expected<void> InfoPanel::paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) {
    const services::StudioColors& c = services::studioColors(m_ctx.theme);
    for (const gui::LayoutBox& box : frame.boxes()) {
        const double w = box.bounds.width;
        const double h = box.bounds.height;
        if (box.component == m_root) {
            out.add(box.component, {gui::FillRect{{0, 0, w, h}, rc(c.surface)}});
        } else if (box.component == m_text) {
            std::vector<gui::DrawCommand> d;
            const auto ls = lines();
            for (usize i = 0; i < ls.size(); ++i) {
                d.push_back(gui::DrawText{{0, static_cast<double>(i) * 20.0, w, 18}, kFont, i == 0 ? 15.0 : 13.0, ls[i],
                                          i == 0 ? rc(c.text) : rc(c.muted)});
            }
            out.add(box.component, std::move(d), true);
        } else if (box.component == m_homeNode || box.component == m_resetNode) {
            const bool home = box.component == m_homeNode;
            gui::ControlVisualState vs;
            if (auto v = (home ? m_home : m_reset)->visualState(host.pointerHit())) {
                vs = *v;
            }
            out.add(box.component, kit::button(host, box, vs, home ? "홈으로" : "배치 초기화", 14.0, home, c));
        }
    }
    return {};
}

} // namespace sbx::editor::gui
