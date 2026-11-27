#include "editor/gui/DockWorkspace.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>

namespace sbx::editor::gui {

namespace {

constexpr double kDragThreshold = 6.0; // UI 단위 — 이만큼 움직여야 탭 끌기 (누르기만 = 고르기)
constexpr double kEdge = 0.25;         // 그룹 가장자리 비율 — 그 안에 놓으면 분할

gui::RenderColor rc(services::Rgba8 c, u8 alpha = 255) {
    return {c.r, c.g, c.b, alpha};
}

bool contains(const gui::LayoutRect& r, gui::LayoutPoint p) {
    return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height;
}

double fit(double v) {
    return v > 0.0 ? v : 0.0;
}

// 마우스 이벤트의 자리 (논리 단위) 를 옮긴 사본 — 패널 호스트는 자기 내용 원점 기준
platform::PlatformEvent shifted(const platform::PlatformEvent& e, f32 dx, f32 dy) {
    platform::PlatformEvent out = e;
    std::visit(
        [&](auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, platform::MouseMove> || std::is_same_v<T, platform::MouseButtonDown> ||
                          std::is_same_v<T, platform::MouseButtonUp>) {
                v.position.x -= dx;
                v.position.y -= dy;
            }
        },
        out);
    return out;
}

} // namespace

Expected<std::unique_ptr<DockWorkspace>> DockWorkspace::create(const uihost::ITextMeasure& text,
                                                               std::vector<DockPaneSpec> panes) {
    if (panes.empty()) {
        return makeError(ErrorCode::InvalidArgument, "Dock 작업 공간: 패널이 없다");
    }
    std::unique_ptr<DockWorkspace> ws(new DockWorkspace(text));
    std::vector<gui::DockPaneId> ids;
    for (DockPaneSpec& spec : panes) {
        if (!spec.screen || spec.id == 0) {
            return makeError(ErrorCode::InvalidArgument, "Dock 작업 공간: 패널 id 0 · 화면 없음");
        }
        auto host = uihost::UiHost::create(text);
        if (!host) {
            return std::unexpected(host.error());
        }
        Pane p;
        p.id = spec.id;
        p.title = std::move(spec.title);
        p.screen = std::move(spec.screen);
        p.host = std::move(*host);
        if (auto r = p.host->setScreen(p.screen.get()); !r) {
            return std::unexpected(r.error());
        }
        ids.push_back(p.id);
        ws->m_panes.push_back(std::move(p));
    }
    auto layout = gui::DockLayout::create(ids);
    if (!layout) {
        return std::unexpected(uihost::toError(layout.error(), "DockLayout::create"));
    }
    ws->m_layout = std::move(*layout);
    ws->m_focus = ids.front();
    return ws;
}

DockWorkspace::~DockWorkspace() {
    // 패널 화면보다 호스트를 먼저 닫는다 (호스트의 핸들러가 화면을 가리킨다)
    for (Pane& p : m_panes) {
        if (p.host) {
            (void)p.host->setScreen(nullptr);
        }
    }
}

void DockWorkspace::setPassthrough(gui::DockPaneId id, bool passthrough) {
    if (Pane* p = pane(id)) {
        p->passthrough = passthrough;
    }
}

bool DockWorkspace::passthrough(gui::DockPaneId id) const {
    const Pane* p = pane(id);
    return p != nullptr && p->passthrough;
}

bool DockWorkspace::wantsPointer(gui::LayoutPoint logical) const {
    if (interacting()) {
        return true;
    }
    if (!m_frame) {
        return false;
    }
    const gui::LayoutPoint p{logical.x / m_scale, logical.y / m_scale};
    for (const gui::DockDividerBox& d : m_frame->dividers) {
        if (contains(d.bounds, p)) {
            return true;
        }
    }
    const gui::DockGroupBox* g = groupAt(p);
    if (g == nullptr) {
        return false;
    }
    return !(passthrough(g->active) && contains(g->content, p)); // 통과 칸의 내용 = 월드
}

bool DockWorkspace::wantsKeyboard() const {
    return m_focus != 0 && !passthrough(m_focus);
}

void DockWorkspace::setClipboard(const uihost::Clipboard& clipboard) {
    for (Pane& p : m_panes) {
        p.host->setClipboard(clipboard);
    }
}

DockWorkspace::Pane* DockWorkspace::pane(gui::DockPaneId id) const {
    for (const Pane& p : m_panes) {
        if (p.id == id) {
            return const_cast<Pane*>(&p);
        }
    }
    return nullptr;
}

uihost::UiHost* DockWorkspace::paneHost(gui::DockPaneId id) const {
    const Pane* p = pane(id);
    return p ? p->host.get() : nullptr;
}

uihost::IUiScreen* DockWorkspace::paneScreen(gui::DockPaneId id) const {
    const Pane* p = pane(id);
    return p ? p->screen.get() : nullptr;
}

gui::DockMetrics DockWorkspace::metrics() const noexcept {
    gui::DockMetrics m;
    m.tab_height = m_compact ? 26.0 : 30.0;
    m.divider_width = 6.0;
    return m;
}

const gui::DockGroupBox* DockWorkspace::groupAt(gui::LayoutPoint p) const {
    if (!m_frame) {
        return nullptr;
    }
    for (const gui::DockGroupBox& g : m_frame->groups) {
        if (contains(g.bounds, p)) {
            return &g;
        }
    }
    return nullptr;
}

const gui::DockTabBox* DockWorkspace::tabAt(gui::LayoutPoint p) const {
    if (!m_frame) {
        return nullptr;
    }
    for (const gui::DockTabBox& t : m_frame->tabs) {
        if (contains(t.bounds, p)) {
            return &t;
        }
    }
    return nullptr;
}

std::optional<DockDropTarget> DockWorkspace::dropAt(gui::LayoutPoint p) const {
    const gui::DockGroupBox* g = groupAt(p);
    if (g == nullptr || m_pressTab == 0) {
        return std::nullopt;
    }
    // 그 그룹의 탭 (같은 group_active) — 끌고 있는 것 하나뿐이면 자기 자신 위다
    std::vector<const gui::DockTabBox*> tabs;
    for (const gui::DockTabBox& t : m_frame->tabs) {
        if (t.group_active == g->active) {
            tabs.push_back(&t);
        }
    }
    const bool onlySelf = tabs.size() == 1 && tabs.front()->pane == m_pressTab;
    const double tabH = metrics().tab_height;
    DockDropTarget d;
    if (p.y < g->bounds.y + tabH) { // 탭 줄 — 그 탭 앞 · 뒤
        if (onlySelf) {
            return std::nullopt;
        }
        d.preview = {g->bounds.x, g->bounds.y, g->bounds.width, tabH};
        for (const gui::DockTabBox* t : tabs) {
            if (contains(t->bounds, p)) {
                if (t->pane == m_pressTab) {
                    return std::nullopt;
                }
                d.target = t->pane;
                d.side = p.x < t->bounds.x + t->bounds.width / 2 ? gui::DockSide::tab_before : gui::DockSide::tab_after;
                return d;
            }
        }
        const gui::DockTabBox* last = tabs.back();
        if (last->pane == m_pressTab) {
            return std::nullopt;
        }
        d.target = last->pane;
        d.side = gui::DockSide::tab_after;
        return d;
    }
    const gui::LayoutRect c = g->content;
    const double rx = c.width > 0 ? (p.x - c.x) / c.width : 0.5;
    const double ry = c.height > 0 ? (p.y - c.y) / c.height : 0.5;
    const double nearest = std::min({rx, 1.0 - rx, ry, 1.0 - ry});
    const gui::LayoutRect b = g->bounds;
    // 가장자리 → 분할 (그룹 기준). 대상은 그 그룹의 활성 탭 — 끌고 있는 것이면 다른 탭, 혼자면 분할할 수 없다
    gui::DockPaneId target = g->active;
    if (target == m_pressTab) {
        target = 0;
        for (const gui::DockTabBox* t : tabs) {
            if (t->pane != m_pressTab) {
                target = t->pane;
                break;
            }
        }
    }
    if (target == 0) {
        return std::nullopt;
    }
    d.target = target;
    if (nearest < kEdge) {
        if (nearest == rx) {
            d.side = gui::DockSide::left;
            d.preview = {b.x, b.y, b.width / 2, b.height};
        } else if (nearest == 1.0 - rx) {
            d.side = gui::DockSide::right;
            d.preview = {b.x + b.width / 2, b.y, b.width / 2, b.height};
        } else if (nearest == ry) {
            d.side = gui::DockSide::top;
            d.preview = {b.x, b.y, b.width, b.height / 2};
        } else {
            d.side = gui::DockSide::bottom;
            d.preview = {b.x, b.y + b.height / 2, b.width, b.height / 2};
        }
        return d;
    }
    if (g->active == m_pressTab || onlySelf) { // 가운데 = 그 그룹의 탭 — 이미 거기 있다
        return std::nullopt;
    }
    d.side = gui::DockSide::tab_after;
    d.preview = c;
    return d;
}

void DockWorkspace::forward(gui::DockPaneId id, const platform::PlatformEvent& e) {
    Pane* p = pane(id);
    if (p == nullptr || !m_frame) {
        return;
    }
    gui::LayoutPoint origin;
    for (const gui::DockGroupBox& g : m_frame->groups) {
        if (g.active == id) {
            origin = {g.content.x, g.content.y};
            break;
        }
    }
    const platform::PlatformEvent local =
        shifted(e, static_cast<f32>(origin.x) * m_scale, static_cast<f32>(origin.y) * m_scale);
    p->host->feed(std::span<const platform::PlatformEvent>(&local, 1));
}

void DockWorkspace::cycleTab(int step) {
    if (!m_frame) {
        return;
    }
    gui::DockPaneId groupActive = 0;
    for (const gui::DockTabBox& t : m_frame->tabs) {
        if (t.pane == m_focus) {
            groupActive = t.group_active;
        }
    }
    std::vector<const gui::DockTabBox*> tabs;
    for (const gui::DockTabBox& t : m_frame->tabs) {
        if (t.group_active == groupActive) {
            tabs.push_back(&t);
        }
    }
    if (tabs.size() < 2) {
        return;
    }
    std::sort(tabs.begin(), tabs.end(), [](auto* a, auto* b) { return a->bounds.x < b->bounds.x; });
    const auto it = std::find_if(tabs.begin(), tabs.end(), [&](auto* t) { return t->pane == groupActive; });
    const auto n = static_cast<std::ptrdiff_t>(tabs.size());
    const std::ptrdiff_t at = ((it - tabs.begin()) + step % n + n) % n;
    const gui::DockPaneId next = tabs[static_cast<usize>(at)]->pane;
    if (m_layout->select(next)) {
        m_focus = next;
    }
}

void DockWorkspace::feed(std::span<const platform::PlatformEvent> events) {
    const auto cancelDrag = [this] {
        m_dragging = false;
        m_pressTab = 0;
        m_drop.reset();
        m_divider.reset();
    };
    for (const platform::PlatformEvent& e : events) {
        if (const auto* mv = std::get_if<platform::MouseMove>(&e)) {
            m_pointer = {mv->position.x / m_scale, mv->position.y / m_scale};
            if (m_divider) {
                const gui::DockDividerBox& d = *m_divider;
                const double f = d.axis == gui::DockAxis::horizontal
                                     ? (d.track.width > 0 ? (m_pointer.x - d.track.x) / d.track.width : d.fraction)
                                     : (d.track.height > 0 ? (m_pointer.y - d.track.y) / d.track.height : d.fraction);
                if (m_layout->resize(d.key, std::clamp(f, 0.05, 0.95))) {
                    // 분할 토큰은 개정에 묶인다 — 다시 배치해 같은 차례의 새 토큰으로 이어 끈다
                    if (m_frame) {
                        if (auto fr = m_layout->arrange(m_lastViewport, metrics())) {
                            m_frame = std::move(*fr);
                            for (const gui::DockDividerBox& nd : m_frame->dividers) {
                                if (nd.key.ordinal == d.key.ordinal) {
                                    m_divider = nd;
                                }
                            }
                        }
                    }
                }
                continue;
            }
            if (m_pressTab != 0 && !m_dragging &&
                std::hypot(m_pointer.x - m_pressAt.x, m_pointer.y - m_pressAt.y) > kDragThreshold) {
                m_dragging = true;
            }
            if (m_dragging) {
                m_drop = dropAt(m_pointer);
                continue;
            }
            if (m_capture != 0) {
                forward(m_capture, e);
            } else if (const gui::DockGroupBox* g = groupAt(m_pointer); g && contains(g->content, m_pointer)) {
                forward(g->active, e); // hover 표시 · 휠 자리
            }
            continue;
        }
        if (const auto* down = std::get_if<platform::MouseButtonDown>(&e)) {
            m_pointer = {down->position.x / m_scale, down->position.y / m_scale};
            if (down->button == platform::MouseButton::Left && m_frame) {
                const auto div = std::find_if(m_frame->dividers.begin(), m_frame->dividers.end(),
                                              [&](const gui::DockDividerBox& d) { return contains(d.bounds, m_pointer); });
                if (div != m_frame->dividers.end()) {
                    m_divider = *div;
                    continue;
                }
                if (const gui::DockTabBox* t = tabAt(m_pointer)) {
                    (void)m_layout->select(t->pane);
                    m_focus = t->pane;
                    m_pressTab = t->pane;
                    m_pressAt = m_pointer;
                    continue;
                }
            }
            if (const gui::DockGroupBox* g = groupAt(m_pointer); g && contains(g->content, m_pointer)) {
                m_focus = g->active;
                m_capture = g->active;
                forward(g->active, e);
            }
            continue;
        }
        if (std::holds_alternative<platform::MouseButtonUp>(e)) {
            if (m_divider) {
                m_divider.reset();
                continue;
            }
            if (m_dragging) {
                if (m_drop && m_layout->dock(m_pressTab, m_drop->target, m_drop->side)) {
                    m_focus = m_pressTab;
                }
                cancelDrag();
                continue;
            }
            m_pressTab = 0;
            if (m_capture != 0) {
                forward(m_capture, e);
                m_capture = 0;
            }
            continue;
        }
        if (std::holds_alternative<platform::MouseWheel>(e)) {
            if (const gui::DockGroupBox* g = groupAt(m_pointer); g && contains(g->content, m_pointer)) {
                forward(g->active, e);
            }
            continue;
        }
        if (const auto* k = std::get_if<platform::KeyDown>(&e)) {
            if (k->key == platform::Key::Escape && (m_dragging || m_divider)) {
                cancelDrag(); // 끌기 취소 — 패널에 Esc 를 보내지 않는다
                continue;
            }
            if (k->mods.ctrl() && !k->mods.shift() && (k->key == platform::Key::PageDown || k->key == platform::Key::PageUp)) {
                cycleTab(k->key == platform::Key::PageDown ? +1 : -1);
                continue;
            }
        }
        // 수정자 키 (Ctrl · Shift · Alt · Super) 는 모든 패널로 — 호스트가 키 이벤트로 수정자를 기억하는데, 포커스가 없는
        // 패널을 Ctrl+누르기 할 때도 그 패널이 Ctrl 을 알아야 한다 (수정자 키 자신은 GraphicsUI 키 코드가 없어 다른 일은 없다)
        const auto* kd = std::get_if<platform::KeyDown>(&e);
        const auto* ku = std::get_if<platform::KeyUp>(&e);
        if ((kd != nullptr && platform::modifierBitOf(kd->key) != 0) || (ku != nullptr && platform::modifierBitOf(ku->key) != 0)) {
            for (Pane& p : m_panes) {
                p.host->feed(std::span<const platform::PlatformEvent>(&e, 1));
            }
            continue;
        }
        if (std::holds_alternative<platform::FocusLost>(e)) {
            cancelDrag();
            if (m_capture != 0 && m_capture != m_focus) {
                forward(m_capture, e);
            }
            m_capture = 0;
        }
        forward(m_focus, e); // 키 · 글자 · 포커스 — 포커스된 패널로
    }
}

Expected<const WorkspaceDraw*> DockWorkspace::frame(gui::LayoutSize logical) {
    if (!(logical.width >= 0.0) || !(logical.height >= 0.0)) {
        return makeError(ErrorCode::InvalidArgument, "작업 공간 크기가 음수 · NaN");
    }
    const gui::LayoutSize vp{logical.width / m_scale, logical.height / m_scale};
    m_lastViewport = vp;
    auto arranged = m_layout->arrange(vp, metrics());
    if (!arranged) {
        return std::unexpected(uihost::toError(arranged.error(), "DockLayout::arrange"));
    }
    m_frame = std::move(*arranged);
    // 포커스가 보이는 패널이 아니면 (옮긴 뒤 · 처음) 그 패널이 활성인 그룹이 없다 — DockLayout 의 포커스로
    const bool visible = std::any_of(m_frame->groups.begin(), m_frame->groups.end(),
                                     [&](const gui::DockGroupBox& g) { return g.active == m_focus; });
    if (!visible) {
        m_focus = m_frame->focused != 0 ? m_frame->focused
                                        : (m_frame->groups.empty() ? 0 : m_frame->groups.front().active);
    }

    const services::StudioColors& c = services::studioColors(m_theme);
    const double tabH = metrics().tab_height;
    WorkspaceDraw& d = m_draw;
    d.chrome.clear();
    d.panes.clear();
    d.overlay.clear();
    // 통과 그룹이 보이면 창 전체 바탕을 칠하지 않는다 (그룹 · 분할선이 창을 덮는다 — 통과 칸만 비운다)
    const bool anyPassthrough = std::any_of(m_frame->groups.begin(), m_frame->groups.end(),
                                            [&](const gui::DockGroupBox& g) { return passthrough(g.active); });
    if (!anyPassthrough) {
        d.chrome.push_back(gui::FillRect{{0, 0, vp.width, vp.height}, rc(c.background)});
    }
    for (const gui::DockGroupBox& g : m_frame->groups) {
        if (!passthrough(g.active)) {
            d.chrome.push_back(gui::FillRect{g.bounds, rc(c.surface)});
        }
        d.chrome.push_back(gui::FillRect{{g.bounds.x, g.bounds.y, g.bounds.width, tabH}, rc(c.background)});
    }
    for (const gui::DockTabBox& t : m_frame->tabs) {
        const bool active = t.pane == t.group_active;
        const bool focused = t.pane == m_focus;
        const gui::LayoutRect b = t.bounds;
        if (active) {
            d.chrome.push_back(gui::FillRect{b, rc(c.surface)});
            d.chrome.push_back(gui::FillRect{{b.x, b.y, b.width, 2}, focused ? rc(c.accent) : rc(c.border)});
        } else if (m_pressTab == 0 && contains(b, m_pointer)) {
            d.chrome.push_back(gui::FillRect{b, rc(c.controlHover)});
        }
        const Pane* p = pane(t.pane);
        const double em = m_compact ? 13.0 : 14.0;
        d.chrome.push_back(gui::DrawText{{b.x + 10, b.y + fit((b.height - em * 1.25) / 2), fit(b.width - 20), em * 1.25}, 1,
                                         em, p ? p->title : std::string("?"), active ? rc(c.text) : rc(c.muted)});
    }
    for (const gui::DockDividerBox& dv : m_frame->dividers) {
        const bool hot = (m_divider && m_divider->key.ordinal == dv.key.ordinal) ||
                         (!m_divider && m_pressTab == 0 && contains(dv.bounds, m_pointer));
        d.chrome.push_back(gui::FillRect{dv.bounds, hot ? rc(c.accent) : rc(c.border)});
    }
    for (const gui::DockGroupBox& g : m_frame->groups) {
        Pane* p = pane(g.active);
        if (p == nullptr) {
            continue;
        }
        p->host->setScale(m_scale);
        auto list = p->host->frame({g.content.width * m_scale, g.content.height * m_scale});
        if (!list) {
            return std::unexpected(list.error());
        }
        if (*list != nullptr) {
            d.panes.push_back({p->id, *list, {g.content.x, g.content.y}});
        }
    }
    if (m_dragging && m_drop) {
        const gui::LayoutRect r = m_drop->preview;
        d.overlay.push_back(gui::FillRect{r, rc(c.accent, 60)});
        d.overlay.push_back(gui::FillRect{{r.x, r.y, r.width, 2}, rc(c.accent)});
        d.overlay.push_back(gui::FillRect{{r.x, r.y + fit(r.height - 2), r.width, 2}, rc(c.accent)});
    }
    return &m_draw;
}

uihost::TextInputState DockWorkspace::textInput() const {
    const Pane* p = pane(m_focus);
    if (p == nullptr || !m_frame) {
        return {};
    }
    uihost::TextInputState s = p->host->textInput();
    if (s.caret) {
        for (const gui::DockGroupBox& g : m_frame->groups) {
            if (g.active == m_focus) {
                s.caret->x += g.content.x * m_scale;
                s.caret->y += g.content.y * m_scale;
            }
        }
    }
    return s;
}

bool DockWorkspace::textFocused() const {
    const Pane* p = pane(m_focus);
    return p != nullptr && p->host->textFocused();
}

Expected<std::vector<std::byte>> DockWorkspace::save() const {
    auto bytes = m_layout->save();
    if (!bytes) {
        return std::unexpected(uihost::toError(bytes.error(), "DockLayout::save"));
    }
    return std::move(*bytes);
}

Expected<void> DockWorkspace::restore(std::span<const std::byte> bytes) {
    if (auto r = m_layout->restore(bytes); !r) {
        return std::unexpected(uihost::toError(r.error(), "DockLayout::restore"));
    }
    return {};
}

} // namespace sbx::editor::gui
