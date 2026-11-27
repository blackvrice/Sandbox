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

int ConsolePanel::lineLevel(std::string_view line) noexcept {
    // ImGui 콘솔 (editor/ui consoleLineLevel) 과 같은 규칙 — 줄 머리의 수준 (Log 의 "[수준] [분류] 글")
    if (line.starts_with("[error]")) {
        return 2;
    }
    if (line.starts_with("[warn]")) {
        return 1;
    }
    return 0;
}

Expected<void> ConsolePanel::mount(uihost::UiHost& host) {
    m_host = &host;
    // 뿌리 · 막대 (거르기 · 수준 · 지우기 · 복사 · 따라가기) · 목록
    const std::array<gui::ComponentSpec, 8> specs{{{std::nullopt, 1, 0, true},
                                                   {0, 2, 1, true},
                                                   {1, 3, 1, true},
                                                   {1, 4, 2, true},
                                                   {1, 4, 3, true},
                                                   {1, 4, 4, true},
                                                   {1, 4, 5, true},
                                                   {0, 5, 2, true}}};
    auto h = commitTree(host, specs);
    if (!h) {
        return std::unexpected(h.error());
    }
    m_root = (*h)[0];
    m_bar = (*h)[1];
    m_levelNode = (*h)[3];
    m_clearNode = (*h)[4];
    m_copyNode = (*h)[5];
    m_followNode = (*h)[6];
    m_listNode = (*h)[7];
    if (auto r = m_filter.mount(host, (*h)[2], [this](const std::string&) { refresh(true); }); !r) {
        return r;
    }
    m_levelButton = kit::bindButton(host, m_levelNode, [this] {
        m_level = (m_level + 1) % 3; // 전부 → 경고 이상 → 오류만
        refresh(true);
    });
    m_clearButton = kit::bindButton(host, m_clearNode, [this] {
        m_clearedAt = log::recentTotal(); // 지금까지의 줄을 숨긴다 (로그 자체는 그대로 — ImGui 콘솔과 같다)
        refresh(true);
    });
    m_copyButton = kit::bindButton(host, m_copyNode, [this] { copyShown(); });
    m_followButton = kit::bindButton(host, m_followNode, [this] {
        m_follow = !m_follow;
        refresh(true);
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
        const std::uint64_t key = m_first + i;
        if (key < m_clearedAt || lineLevel(m_lines[i]) < m_level || !containsNoCase(m_lines[i], m_applied)) {
            continue;
        }
        keys.push_back(key);
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

std::optional<std::string> ConsolePanel::lineOf(gui::ListItemKey key) const {
    if (key < m_first || key - m_first >= m_lines.size()) {
        return std::nullopt;
    }
    return m_lines[static_cast<usize>(key - m_first)];
}

std::optional<std::string> ConsolePanel::selectedLine() const {
    const auto key = m_list.selection();
    return key ? lineOf(*key) : std::nullopt;
}

void ConsolePanel::writeClipboard(const std::string& text) const {
    const gui::TextEditCallbacks cb = m_host->editCallbacks({});
    if (cb.write_clipboard) {
        (void)cb.write_clipboard(text);
    }
}

void ConsolePanel::copyShown() const {
    // 거른 줄 전부 (보이는 줄만이 아니다 — ImGui 콘솔의 "복사" 와 같다)
    std::string text;
    for (usize i = 0; i < m_list.itemCount(); ++i) {
        if (const auto key = m_list.itemKey(i)) {
            if (const auto line = lineOf(*key)) {
                text += *line;
                text += '\n';
            }
        }
    }
    writeClipboard(text);
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
            writeClipboard(*line);
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

std::string ConsolePanel::levelLabel() const {
    return m_level == 0 ? "수준: 전부" : m_level == 1 ? "수준: 경고 이상" : "수준: 오류만";
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
    // 단추 너비 (수준 · 지우기 · 복사 · 따라가기) — 거르기 칸이 나머지 (좁으면 80 까지)
    constexpr std::array<double, 4> widths{116, 64, 56, 104};
    double buttons = 0;
    for (const double w : widths) {
        buttons += w + 6;
    }
    gui::LayoutNode filter;
    filter.component = m_filter.handle();
    filter.style.preferred_width = std::max(80.0, inner - buttons);
    filter.style.preferred_height = 28;
    out.push_back(filter);
    const std::array<gui::ComponentHandle, 4> nodes{m_levelNode, m_clearNode, m_copyNode, m_followNode};
    for (usize i = 0; i < nodes.size(); ++i) {
        gui::LayoutNode b;
        b.component = nodes[i];
        b.style.preferred_width = widths[i];
        b.style.preferred_height = 28;
        out.push_back(b);
    }
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
        } else if (box.component == m_levelNode) {
            out.add(box.component, kit::button(host, box, kit::visualOf(*m_levelButton, host), levelLabel(), 13.0,
                                               m_level != 0, c));
        } else if (box.component == m_clearNode) {
            out.add(box.component, kit::button(host, box, kit::visualOf(*m_clearButton, host), "지우기", 13.0, false, c));
        } else if (box.component == m_copyNode) {
            out.add(box.component, kit::button(host, box, kit::visualOf(*m_copyButton, host), "복사", 13.0, false, c));
        } else if (box.component == m_followNode) {
            out.add(box.component, kit::button(host, box, kit::visualOf(*m_followButton, host),
                                               m_follow ? "따라가기 켬" : "따라가기 끔", 13.0, m_follow, c));
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
                const auto line = row ? lineOf(row->key) : std::nullopt;
                if (!line) {
                    continue;
                }
                if (row->selected) {
                    d.push_back(gui::FillRect{row->bounds, listFocused ? rc(c.header) : rc(c.elevated)});
                }
                const int level = lineLevel(*line);
                const gui::RenderColor color = level == 2 ? rc(c.error) : level == 1 ? rc(c.warning) : rc(c.text);
                d.push_back(gui::DrawText{{row->bounds.x + 6, row->bounds.y + 2, fit(row->bounds.width - 12), 16}, kFont,
                                          12.5, *line, color});
            }
            if (m_list.itemCount() == 0) {
                // ImGui 콘솔과 같은 빈 줄 안내
                d.push_back(gui::DrawText{{8, 8, fit(w - 16), 18}, kFont, 13.0,
                                          m_lines.empty() || m_clearedAt >= m_total
                                              ? "줄이 없다 — 빌드 · 테스트 · 저장 · 리플레이 결과가 여기 온다"
                                              : "거르기에 맞는 줄이 없다",
                                          rc(c.muted)});
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
        m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::RunCommand, m_rows[static_cast<usize>(*key)]->id, 0);
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
            m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::Home, {}, 0);
        }
        return r ? r->reply : gui::EventReply{};
    });
    (void)host.input().setHandler(m_resetNode, [this](const gui::RoutedEvent& ev) -> gui::EventReply {
        const auto r = m_reset->handle(ev);
        if (r && r->activated) {
            m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::ResetLayout, {}, 0);
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

// ---- BuildPanel ----

std::vector<BuildPanel::Line> BuildPanel::reportLines(const services::BuildReport& r) {
    std::vector<Line> out;
    const char* what = r.checkOnly ? "검사" : "빌드";
    if (r.ok) {
        out.push_back({std::format("{} 통과 — 오류 0 · 경고 {} ({:.2f} 초)", what, r.warnings.size(), r.seconds), Tone::Ok});
    } else {
        out.push_back({std::format("{} 실패 — 오류 {} · 경고 {}", what, r.errors.size(), r.warnings.size()), Tone::Error});
    }
    if (!r.checkOnly && r.ok) {
        const std::u8string f = r.file.generic_u8string();
        out.push_back({"패키지 " + std::string(f.begin(), f.end()), Tone::Text});
        out.push_back({std::format("{} {} · 파일 {} · {} 바이트 · digest {}…", r.gameId, r.version, r.files, r.bytes,
                                   std::string_view(r.digest).substr(0, 16)),
                       Tone::Muted});
    }
    for (const std::string& g : r.groups) {
        out.push_back({"에셋 그룹 " + g, Tone::Text});
    }
    for (const std::string& e : r.errors) {
        out.push_back({e, Tone::Error});
    }
    for (const std::string& w : r.warnings) {
        out.push_back({w, Tone::Warning});
    }
    return out;
}

void BuildPanel::setModel(BuildPanelModel model) {
    if (model == m_model) {
        return;
    }
    const bool reportChanged = model.last != m_model.last;
    m_model = std::move(model);
    m_model.profileIndex = std::clamp(m_model.profileIndex, 0, static_cast<int>(m_model.profiles.size()));
    if (reportChanged) {
        m_lines = m_model.last ? reportLines(*m_model.last) : std::vector<Line>{};
        std::vector<gui::ListItemKey> keys;
        for (usize i = 0; i < m_lines.size(); ++i) {
            keys.push_back(i);
        }
        (void)m_list.setItems(keys);
        (void)m_list.scrollTo(0);
    }
    applyEnabled();
}

void BuildPanel::applyEnabled() {
    // 검사 · 빌드 — 프로젝트가 있고 돌고 있지 않을 때 (ImGui 창의 BeginDisabled(running) · 명령의 when 과 같다)
    const bool can = m_model.available && !m_model.running;
    if (m_check) {
        (void)m_check->setEnabled(can);
        (void)m_build->setEnabled(can);
        (void)m_prev->setEnabled(can && !m_model.profiles.empty());
        (void)m_next->setEnabled(can && !m_model.profiles.empty());
    }
}

std::string BuildPanel::profileLabel() const {
    if (m_model.profileIndex <= 0 || static_cast<usize>(m_model.profileIndex) > m_model.profiles.size()) {
        return "(프로필 없이)";
    }
    return m_model.profiles[static_cast<usize>(m_model.profileIndex) - 1];
}

void BuildPanel::stepProfile(int step) {
    const int n = static_cast<int>(m_model.profiles.size()) + 1; // 0 = 프로필 없이
    m_model.profileIndex = ((m_model.profileIndex + step) % n + n) % n;
    m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::SelectProfile, {}, m_model.profileIndex);
}

Expected<void> BuildPanel::mount(uihost::UiHost& host) {
    m_host = &host;
    // 뿌리 · 제목 · 막대 (‹ 프로필 › · 검사 · 빌드 · 상태) · 보고 목록
    const std::array<gui::ComponentSpec, 10> specs{{{std::nullopt, 1, 0, true},
                                                    {0, 2, 1, true},
                                                    {0, 3, 2, true},
                                                    {2, 4, 1, true},
                                                    {2, 2, 2, true},
                                                    {2, 4, 3, true},
                                                    {2, 4, 4, true},
                                                    {2, 4, 5, true},
                                                    {2, 2, 6, true},
                                                    {0, 5, 3, true}}};
    auto h = commitTree(host, specs);
    if (!h) {
        return std::unexpected(h.error());
    }
    m_root = (*h)[0];
    m_title = (*h)[1];
    m_bar = (*h)[2];
    m_prevNode = (*h)[3];
    m_profileNode = (*h)[4];
    m_nextNode = (*h)[5];
    m_checkNode = (*h)[6];
    m_buildNode = (*h)[7];
    m_status = (*h)[8];
    m_listNode = (*h)[9];
    m_prev = kit::bindButton(host, m_prevNode, [this] { stepProfile(-1); });
    m_next = kit::bindButton(host, m_nextNode, [this] { stepProfile(+1); });
    // 일은 ImGui 창과 같은 명령 — 검사는 곧바로, 빌드는 일꾼 스레드 (BuildService) 에서
    m_check = kit::bindButton(host, m_checkNode, [this] {
        m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::RunCommand, "studio.check", 0);
    });
    m_build = kit::bindButton(host, m_buildNode, [this] {
        m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::RunCommand, "studio.build", 0);
    });
    applyEnabled();
    (void)host.setWheelHandler(m_listNode, [this](f32, f32 dy) {
        (void)m_list.scrollBy(-static_cast<double>(dy) * 20.0 * 3.0);
        return true;
    });
    return {};
}

Expected<void> BuildPanel::layout(uihost::UiHost& host, const gui::ComponentSnapshot&, std::vector<gui::LayoutNode>& out) {
    const gui::LayoutSize vp = host.viewport();
    const double inner = fit(vp.width - 16);
    gui::LayoutNode root;
    root.component = m_root;
    root.style.kind = gui::LayoutKind::column;
    root.style.padding = {8, 8, 8, 8};
    root.style.gap = 6;
    out.push_back(root);
    gui::LayoutNode title;
    title.component = m_title;
    title.style.preferred_width = inner;
    title.style.preferred_height = 18;
    out.push_back(title);
    gui::LayoutNode bar;
    bar.component = m_bar;
    bar.style.kind = gui::LayoutKind::row;
    bar.style.gap = 6;
    out.push_back(bar);
    const std::array<std::pair<gui::ComponentHandle, double>, 6> parts{
        {{m_prevNode, 28}, {m_profileNode, 150}, {m_nextNode, 28}, {m_checkNode, 64}, {m_buildNode, 64}, {m_status, 120}}};
    for (const auto& [node, w] : parts) {
        gui::LayoutNode n;
        n.component = node;
        n.style.preferred_width = w;
        n.style.preferred_height = 28;
        out.push_back(n);
    }
    gui::LayoutNode list;
    list.component = m_listNode;
    list.style.preferred_width = inner;
    list.style.preferred_height = fit(vp.height - 16 - 18 - 28 - 12);
    out.push_back(list);
    return {};
}

Expected<void> BuildPanel::paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) {
    const services::StudioColors& c = services::studioColors(m_ctx.theme);
    const bool can = m_model.available && !m_model.running;
    for (const gui::LayoutBox& box : frame.boxes()) {
        const double w = box.bounds.width;
        const double h = box.bounds.height;
        if (box.component == m_root) {
            out.add(box.component, {gui::FillRect{{0, 0, w, h}, rc(c.surface)}});
        } else if (box.component == m_title) {
            out.add(box.component,
                    {gui::DrawText{{0, 0, w, h}, kFont, 13.0,
                                   m_model.available ? "프로젝트 " + m_model.project
                                                     : std::string("프로젝트가 열려 있지 않다 — 프로젝트를 열면 검사 · 빌드"),
                                   m_model.available ? rc(c.text) : rc(c.muted)}},
                    true);
        } else if (box.component == m_profileNode) {
            out.add(box.component,
                    {gui::FillRect{{0, 0, w, h}, rc(c.elevated)},
                     gui::DrawText{{6, fit((h - 16) / 2), fit(w - 12), 16}, kFont, 13.0, profileLabel(), rc(c.text)}},
                    true);
        } else if (box.component == m_status) {
            out.add(box.component, {gui::DrawText{{0, fit((h - 16) / 2), w, 16}, kFont, 13.0,
                                                  m_model.running ? "빌드 중…" : "", rc(c.muted)}});
        } else if (box.component == m_prevNode || box.component == m_nextNode || box.component == m_checkNode ||
                   box.component == m_buildNode) {
            const bool prev = box.component == m_prevNode;
            const bool next = box.component == m_nextNode;
            const bool build = box.component == m_buildNode;
            const std::string label = prev ? "‹" : next ? "›" : build ? "빌드" : "검사";
            const gui::Button& b = prev ? *m_prev : next ? *m_next : build ? *m_build : *m_check;
            const bool enabled = can && (!(prev || next) || !m_model.profiles.empty());
            out.add(box.component, enabled ? kit::button(host, box, kit::visualOf(b, host), label, 13.0, build, c)
                                           : kit::disabledButton(host, box, label, 13.0, c));
        } else if (box.component == m_listNode) {
            constexpr double rowH = 20.0;
            (void)m_list.resize({w, h}, rowH);
            std::vector<gui::DrawCommand> d;
            d.push_back(gui::FillRect{{0, 0, w, h}, rc(c.background)});
            const gui::ListRange range = m_list.visibleRange();
            for (usize i = range.first; i < range.past; ++i) {
                const auto row = m_list.visibleRow(i);
                if (!row || row->key >= m_lines.size()) {
                    continue;
                }
                const Line& line = m_lines[static_cast<usize>(row->key)];
                const gui::RenderColor color = line.tone == Tone::Ok        ? rc(c.success)
                                               : line.tone == Tone::Error   ? rc(c.error)
                                               : line.tone == Tone::Warning ? rc(c.warning)
                                               : line.tone == Tone::Muted   ? rc(c.muted)
                                                                            : rc(c.text);
                d.push_back(gui::DrawText{{row->bounds.x + 6, row->bounds.y + 2, fit(row->bounds.width - 12), 16}, kFont,
                                          12.5, line.text, color});
            }
            if (m_lines.empty()) {
                d.push_back(gui::DrawText{{8, 8, fit(w - 16), 18}, kFont, 13.0, "보고가 없다 — 검사 · 빌드 결과가 여기 온다",
                                          rc(c.muted)});
            }
            out.add(box.component, std::move(d), true);
        }
    }
    return {};
}


// ---- TestPanel ----

namespace {

struct FieldSpec {
    const char* label;
    double step;
    double min;
    double max;
};

// ImGui 창 (SliderInt · DragFloat) 과 같은 범위 — applyTestPanel 이 같은 값으로 다시 자른다
constexpr std::array<FieldSpec, TestPanel::kFields> kTestFields{{
    {"클라이언트", 1.0, 1.0, static_cast<double>(services::TestService::kMaxClients)},
    {"지연 ms (한 방향)", 10.0, 0.0, 2000.0},
    {"흔들림 ms", 5.0, 0.0, 2000.0},
    {"손실 %", 0.5, 0.0, 50.0},
}};

double& fieldOf(TestPlanValues& v, TestPanel::Field f) {
    switch (f) {
    case TestPanel::Field::Latency:
        return v.latencyMs;
    case TestPanel::Field::Jitter:
        return v.jitterMs;
    case TestPanel::Field::Loss:
        return v.lossPercent;
    case TestPanel::Field::Clients:
        break;
    }
    return v.latencyMs; // Clients 는 int — step 이 따로 다룬다
}

} // namespace

std::string TestPanel::processLine(const services::TestProcess& p) {
    const char* what = p.state == services::TestProcessState::Running   ? "도는 중"
                       : p.state == services::TestProcessState::Stopped ? "끝냄"
                                                                        : "끝남";
    if (p.state == services::TestProcessState::Exited) {
        return std::format("{} — {} (종료 코드 {})", p.label, what, p.exitCode);
    }
    return std::format("{} — {}", p.label, what);
}

std::string TestPanel::valueText(Field f) const {
    TestPlanValues v = m_model.plan;
    switch (f) {
    case Field::Clients:
        return std::format("{}", v.clients);
    case Field::Latency:
        return std::format("{:.0f} ms", v.latencyMs);
    case Field::Jitter:
        return std::format("{:.0f} ms", v.jitterMs);
    case Field::Loss:
        return std::format("{:.1f} %", v.lossPercent);
    }
    return {};
}

void TestPanel::setModel(TestPanelModel model) {
    if (model == m_model) {
        return;
    }
    const bool listChanged = model.processes.size() != m_model.processes.size();
    m_model = std::move(model);
    if (listChanged) {
        std::vector<gui::ListItemKey> keys;
        for (usize i = 0; i < m_model.processes.size(); ++i) {
            keys.push_back(i);
        }
        (void)m_list.setItems(keys);
    }
    applyEnabled();
}

void TestPanel::applyEnabled() {
    // ImGui 창과 같다 — 도는 동안 값 · 시작은 끄고, 끝내기는 도는 동안만
    const bool idle = m_model.available && !m_model.running;
    if (m_start) {
        for (Row& r : m_rows) {
            (void)r.minusButton->setEnabled(idle);
            (void)r.plusButton->setEnabled(idle);
        }
        (void)m_start->setEnabled(idle);
        (void)m_stop->setEnabled(m_model.available && m_model.running);
    }
}

void TestPanel::step(Field f, int dir) {
    const FieldSpec& s = kTestFields[static_cast<usize>(f)];
    if (f == Field::Clients) {
        m_model.plan.clients = std::clamp(m_model.plan.clients + dir, static_cast<int>(s.min), static_cast<int>(s.max));
    } else {
        double& v = fieldOf(m_model.plan, f);
        v = std::clamp(v + s.step * dir, s.min, s.max);
    }
    WorkspaceIntent i = WorkspaceIntent::of(WorkspaceIntent::Kind::TestPlan);
    i.test = m_model.plan;
    m_ctx.intent = std::move(i);
}

Expected<void> TestPanel::mount(uihost::UiHost& host) {
    // 뿌리 · 값 줄 넷 (줄 · 이름 · − · 값 · +) · 막대 (시작 · 끝내기) · 안내 · 프로세스 목록 — 부모 우선
    std::vector<gui::ComponentSpec> specs;
    specs.push_back({std::nullopt, 1, 0, true});
    for (usize i = 0; i < kFields; ++i) {
        const usize row = specs.size();
        specs.push_back({0, 2, i + 1, true});
        specs.push_back({row, 3, 1, true});
        specs.push_back({row, 4, 2, true});
        specs.push_back({row, 3, 3, true});
        specs.push_back({row, 4, 4, true});
    }
    const usize bar = specs.size();
    specs.push_back({0, 2, 10, true});
    specs.push_back({bar, 4, 1, true});
    specs.push_back({bar, 4, 2, true});
    specs.push_back({0, 3, 11, true});
    specs.push_back({0, 5, 12, true});
    auto h = commitTree(host, specs);
    if (!h) {
        return std::unexpected(h.error());
    }
    m_root = (*h)[0];
    for (usize i = 0; i < kFields; ++i) {
        Row& r = m_rows[i];
        const usize at = 1 + i * 5;
        r.row = (*h)[at];
        r.label = (*h)[at + 1];
        r.minus = (*h)[at + 2];
        r.value = (*h)[at + 3];
        r.plus = (*h)[at + 4];
        const Field f = static_cast<Field>(i);
        r.minusButton = kit::bindButton(host, r.minus, [this, f] { step(f, -1); });
        r.plusButton = kit::bindButton(host, r.plus, [this, f] { step(f, +1); });
    }
    m_bar = (*h)[bar];
    m_startNode = (*h)[bar + 1];
    m_stopNode = (*h)[bar + 2];
    m_note = (*h)[bar + 3];
    m_listNode = (*h)[bar + 4];
    m_start = kit::bindButton(host, m_startNode, [this] {
        m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::RunCommand, "studio.test-start");
    });
    m_stop = kit::bindButton(host, m_stopNode, [this] {
        m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::RunCommand, "studio.test-stop");
    });
    applyEnabled();
    return {};
}

Expected<void> TestPanel::layout(uihost::UiHost& host, const gui::ComponentSnapshot&, std::vector<gui::LayoutNode>& out) {
    const gui::LayoutSize vp = host.viewport();
    const double inner = fit(vp.width - 16);
    gui::LayoutNode root;
    root.component = m_root;
    root.style.kind = gui::LayoutKind::column;
    root.style.padding = {8, 8, 8, 8};
    root.style.gap = 4;
    out.push_back(root);
    for (const Row& r : m_rows) {
        gui::LayoutNode row;
        row.component = r.row;
        row.style.kind = gui::LayoutKind::row;
        row.style.gap = 6;
        out.push_back(row);
        const std::array<std::pair<gui::ComponentHandle, double>, 4> parts{
            {{r.label, 150}, {r.minus, 28}, {r.value, 80}, {r.plus, 28}}};
        for (const auto& [node, w] : parts) {
            gui::LayoutNode n;
            n.component = node;
            n.style.preferred_width = w;
            n.style.preferred_height = 26;
            out.push_back(n);
        }
    }
    gui::LayoutNode bar;
    bar.component = m_bar;
    bar.style.kind = gui::LayoutKind::row;
    bar.style.gap = 6;
    out.push_back(bar);
    for (const auto& [node, w] : std::array<std::pair<gui::ComponentHandle, double>, 2>{{{m_startNode, 180}, {m_stopNode, 110}}}) {
        gui::LayoutNode n;
        n.component = node;
        n.style.preferred_width = w;
        n.style.preferred_height = 28;
        out.push_back(n);
    }
    gui::LayoutNode note;
    note.component = m_note;
    note.style.preferred_width = inner;
    note.style.preferred_height = 18;
    out.push_back(note);
    gui::LayoutNode list;
    list.component = m_listNode;
    list.style.preferred_width = inner;
    list.style.preferred_height = fit(vp.height - 16 - 4 * (26 + 4) - (28 + 4) - (18 + 4));
    out.push_back(list);
    return {};
}

Expected<void> TestPanel::paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) {
    const services::StudioColors& c = services::studioColors(m_ctx.theme);
    const bool idle = m_model.available && !m_model.running;
    const auto btn = [&](const gui::LayoutBox& box, const gui::Button& b, const std::string& label, bool enabled,
                         bool accent) {
        out.add(box.component, enabled ? kit::button(host, box, kit::visualOf(b, host), label, 13.0, accent, c)
                                       : kit::disabledButton(host, box, label, 13.0, c));
    };
    for (const gui::LayoutBox& box : frame.boxes()) {
        const double w = box.bounds.width;
        const double h = box.bounds.height;
        if (box.component == m_root) {
            out.add(box.component, {gui::FillRect{{0, 0, w, h}, rc(c.surface)}});
            continue;
        }
        bool done = false;
        for (usize i = 0; i < kFields && !done; ++i) {
            const Row& r = m_rows[i];
            if (box.component == r.label) {
                out.add(box.component, {gui::DrawText{{0, fit((h - 16) / 2), w, 16}, kFont, 13.0, kTestFields[i].label,
                                                      rc(c.text)}},
                        true);
            } else if (box.component == r.value) {
                out.add(box.component, {gui::FillRect{{0, 0, w, h}, rc(c.elevated)},
                                        gui::DrawText{{6, fit((h - 16) / 2), fit(w - 12), 16}, kFont, 13.0,
                                                      valueText(static_cast<Field>(i)), idle ? rc(c.text) : rc(c.muted)}});
            } else if (box.component == r.minus) {
                btn(box, *r.minusButton, "−", idle, false);
            } else if (box.component == r.plus) {
                btn(box, *r.plusButton, "+", idle, false);
            } else {
                continue;
            }
            done = true;
        }
        if (done) {
            continue;
        }
        if (box.component == m_startNode) {
            btn(box, *m_start, "서버 + 클라이언트 시작", idle, true);
        } else if (box.component == m_stopNode) {
            btn(box, *m_stop, "모두 끝내기", m_model.available && m_model.running, false);
        } else if (box.component == m_note) {
            out.add(box.component,
                    {gui::DrawText{{0, 0, w, h}, kFont, 12.5,
                                   m_model.available
                                       ? std::string("저장한 프로젝트로 시작한다 (Ctrl+S) — 저장하지 않은 편집은 들어가지 않는다")
                                       : std::string("프로젝트가 열려 있지 않다 — 프로젝트를 열면 서버 + 클라이언트 테스트"),
                                   rc(c.muted)}},
                    true);
        } else if (box.component == m_listNode) {
            constexpr double rowH = 22.0;
            (void)m_list.resize({w, h}, rowH);
            std::vector<gui::DrawCommand> d;
            d.push_back(gui::FillRect{{0, 0, w, h}, rc(c.background)});
            const gui::ListRange range = m_list.visibleRange();
            for (usize i = range.first; i < range.past; ++i) {
                const auto row = m_list.visibleRow(i);
                if (!row || row->key >= m_model.processes.size()) {
                    continue;
                }
                const services::TestProcess& p = m_model.processes[static_cast<usize>(row->key)];
                const gui::LayoutRect b = row->bounds;
                const std::string line = processLine(p);
                const double lw = std::min(host.text().measure(line, 13.0).width, fit(b.width - 12));
                d.push_back(gui::DrawText{{b.x + 6, b.y + 3, lw, 16}, kFont, 13.0, line,
                                          p.state == services::TestProcessState::Running ? rc(c.success)
                                          : p.state == services::TestProcessState::Exited && p.exitCode != 0
                                              ? rc(c.error)
                                              : rc(c.text)});
                // 로그 파일 (ImGui 창은 툴팁 — GraphicsUI 는 툴팁이 없어 줄 오른쪽에 흐리게, GUI-UP-03)
                const std::u8string log = p.log.generic_u8string();
                d.push_back(gui::DrawText{{b.x + 6 + lw + 12, b.y + 4, fit(b.width - lw - 24), 15}, kFont, 11.5,
                                          std::string(log.begin(), log.end()), rc(c.muted)});
            }
            if (m_model.processes.empty()) {
                d.push_back(gui::DrawText{{8, 8, fit(w - 16), 18}, kFont, 13.0, "프로세스가 없다", rc(c.muted)});
            }
            out.add(box.component, std::move(d), true);
        }
    }
    return {};
}

// ---- StatsPanel ----

void StatsPanel::setModel(StatsModel model) {
    if (model == m_model) {
        return;
    }
    const bool countChanged = model.lines.size() != m_model.lines.size();
    m_model = std::move(model);
    if (countChanged) { // 줄 수가 같으면 키도 같다 — 스크롤을 지킨다 (값만 frame 마다 바뀐다)
        std::vector<gui::ListItemKey> keys;
        for (usize i = 0; i < m_model.lines.size(); ++i) {
            keys.push_back(i);
        }
        (void)m_list.setItems(keys);
    }
}

Expected<void> StatsPanel::mount(uihost::UiHost& host) {
    const std::array<gui::ComponentSpec, 4> specs{
        {{std::nullopt, 1, 0, true}, {0, 2, 1, true}, {0, 3, 2, true}, {0, 4, 3, true}}};
    auto h = commitTree(host, specs);
    if (!h) {
        return std::unexpected(h.error());
    }
    m_root = (*h)[0];
    m_headline = (*h)[1];
    m_graph = (*h)[2];
    m_listNode = (*h)[3];
    (void)host.setWheelHandler(m_listNode, [this](f32, f32 dy) {
        (void)m_list.scrollBy(-static_cast<double>(dy) * 20.0 * 3.0);
        return true;
    });
    return {};
}

Expected<void> StatsPanel::layout(uihost::UiHost& host, const gui::ComponentSnapshot&, std::vector<gui::LayoutNode>& out) {
    const gui::LayoutSize vp = host.viewport();
    const double inner = fit(vp.width - 16);
    gui::LayoutNode root;
    root.component = m_root;
    root.style.kind = gui::LayoutKind::column;
    root.style.padding = {8, 8, 8, 8};
    root.style.gap = 6;
    out.push_back(root);
    const std::array<std::pair<gui::ComponentHandle, double>, 2> top{{{m_headline, 18}, {m_graph, 52}}};
    for (const auto& [node, hgt] : top) {
        gui::LayoutNode n;
        n.component = node;
        n.style.preferred_width = inner;
        n.style.preferred_height = hgt;
        out.push_back(n);
    }
    gui::LayoutNode list;
    list.component = m_listNode;
    list.style.preferred_width = inner;
    list.style.preferred_height = fit(vp.height - 16 - 18 - 52 - 12);
    out.push_back(list);
    return {};
}

Expected<void> StatsPanel::paint(uihost::UiHost&, const gui::LayoutFrame& frame, uihost::PaintList& out) {
    const services::StudioColors& c = services::studioColors(m_ctx.theme);
    for (const gui::LayoutBox& box : frame.boxes()) {
        const double w = box.bounds.width;
        const double h = box.bounds.height;
        if (box.component == m_root) {
            out.add(box.component, {gui::FillRect{{0, 0, w, h}, rc(c.surface)}});
        } else if (box.component == m_headline) {
            out.add(box.component,
                    {gui::DrawText{{0, 0, w, h}, kFont, 14.0,
                                   m_model.headline.empty() ? std::string("프레임 기록이 없다") : m_model.headline,
                                   rc(c.text)}},
                    true);
        } else if (box.component == m_graph) {
            // 프레임 시간 막대 (ImGui PlotLines 와 같은 범위 — 최댓값 × 1.1, 1 ms 아래로 줄이지 않는다)
            std::vector<gui::DrawCommand> d;
            d.push_back(gui::FillRect{{0, 0, w, h}, rc(c.elevated)});
            const usize n = m_model.frameMs.size();
            float maxMs = 1.f;
            for (const float ms : m_model.frameMs) {
                maxMs = std::max(maxMs, ms);
            }
            m_bars = 0;
            if (n > 0 && w > 0) {
                const double bw = w / static_cast<double>(n);
                for (usize i = 0; i < n; ++i) {
                    const double bh = fit(std::min(1.0, static_cast<double>(m_model.frameMs[i]) / (maxMs * 1.1)) * (h - 2));
                    d.push_back(gui::FillRect{{static_cast<double>(i) * bw, h - bh, std::max(1.0, bw - 0.5), bh}, rc(c.accent)});
                    ++m_bars;
                }
            }
            out.add(box.component, std::move(d), true);
        } else if (box.component == m_listNode) {
            constexpr double rowH = 20.0;
            (void)m_list.resize({w, h}, rowH);
            std::vector<gui::DrawCommand> d;
            d.push_back(gui::FillRect{{0, 0, w, h}, rc(c.background)});
            const double labelW = std::min(150.0, w * 0.4);
            const gui::ListRange range = m_list.visibleRange();
            for (usize i = range.first; i < range.past; ++i) {
                const auto row = m_list.visibleRow(i);
                if (!row || row->key >= m_model.lines.size()) {
                    continue;
                }
                const StatLine& l = m_model.lines[static_cast<usize>(row->key)];
                const gui::LayoutRect b = row->bounds;
                const gui::RenderColor color = l.tone == StatTone::Section   ? rc(c.accent)
                                               : l.tone == StatTone::Muted   ? rc(c.muted)
                                               : l.tone == StatTone::Warning ? rc(c.warning)
                                               : l.tone == StatTone::Error   ? rc(c.error)
                                                                             : rc(c.text);
                if (l.label.empty()) {
                    d.push_back(gui::DrawText{{b.x + 6, b.y + 2, fit(b.width - 12), 16}, kFont,
                                              l.tone == StatTone::Section ? 13.5 : 12.5, l.value, color});
                } else {
                    d.push_back(gui::DrawText{{b.x + 6, b.y + 2, fit(labelW - 8), 16}, kFont, 12.5, l.label, rc(c.muted)});
                    d.push_back(gui::DrawText{{b.x + labelW, b.y + 2, fit(b.width - labelW - 6), 16}, kFont, 12.5, l.value,
                                              color});
                }
            }
            out.add(box.component, std::move(d), true);
        }
    }
    return {};
}

} // namespace sbx::editor::gui
