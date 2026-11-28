#include "editor/gui/KeyBindingsPanel.hpp"

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

void KeyBindingsPanel::setModel(KeyBindingsModel model) {
    if (model == m_model && !m_lines.empty()) {
        return; // 같은 표 — 줄 · 스크롤 그대로
    }
    m_model = std::move(model);
    m_lines.clear();
    for (usize i = 0; i < m_model.conflicts.size(); ++i) {
        m_lines.push_back({Line::Kind::Conflict, i});
    }
    bool engineHeader = false;
    bool playHeader = false;
    for (usize i = 0; i < m_model.rows.size(); ++i) {
        if (!m_model.rows[i].play && !engineHeader) {
            engineHeader = true;
            m_lines.push_back({Line::Kind::Header, 0});
        }
        if (m_model.rows[i].play && !playHeader) { // ImGui 창과 같다 — 게임 키 앞에 머리
            playHeader = true;
            m_lines.push_back({Line::Kind::Header, 1});
        }
        m_lines.push_back({Line::Kind::Row, i});
    }
    kit::setIndexKeys(m_list, m_keys, m_lines.size());
    if (!m_selected.empty() && selectedRow() == nullptr) {
        m_selected.clear(); // 표에서 사라졌다
    }
}

const KeyBindingRow* KeyBindingsPanel::selectedRow() const {
    const auto it = std::ranges::find(m_model.rows, m_selected, &KeyBindingRow::action);
    return m_selected.empty() || it == m_model.rows.end() ? nullptr : &*it;
}

std::string KeyBindingsPanel::lineText(usize line) const {
    if (line >= m_lines.size()) {
        return {};
    }
    const Line& l = m_lines[line];
    switch (l.kind) {
    case Line::Kind::Conflict:
        return std::format("겹침 {}", m_model.conflicts[l.index]);
    case Line::Kind::Header:
        return l.index == 0 ? "엔진 키" : "게임 키 (팩)";
    case Line::Kind::Row:
        break;
    }
    const KeyBindingRow& r = m_model.rows[l.index];
    return std::format("{}  —  {}", r.action, r.keys.empty() ? std::string("-") : r.keys);
}

std::string KeyBindingsPanel::title() const {
    if (!m_model.capturing.empty()) {
        return std::format("{} — 새 키를 누르세요 (Esc 취소)", m_model.capturing);
    }
    return m_model.dirty ? "키 (저장 안 함) — 줄을 고르고 바꾸기 (또는 Enter)" : "키 — 줄을 고르고 바꾸기 (또는 Enter)";
}

void KeyBindingsPanel::request(int kind, std::string action) {
    m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::KeyBinding, std::move(action), kind);
}

void KeyBindingsPanel::captureOrCancel() {
    if (!m_model.capturing.empty()) {
        request(KeyBindingsModel::kCancel);
        return;
    }
    if (selectedRow() != nullptr) {
        request(KeyBindingsModel::kCapture, m_selected); // 다음 frame 부터 앱이 누른 키를 잡는다
    }
}

void KeyBindingsPanel::select(usize line) {
    if (line < m_lines.size() && m_lines[line].kind == Line::Kind::Row) {
        m_selected = m_model.rows[m_lines[line].index].action;
        (void)m_list.reveal(m_keys[line]);
    }
}

void KeyBindingsPanel::applyEnabled() {
    if (!m_capture) {
        return;
    }
    const bool capturing = !m_model.capturing.empty();
    const KeyBindingRow* row = selectedRow();
    (void)m_capture->setEnabled(capturing || row != nullptr);
    (void)m_reset->setEnabled(!capturing && row != nullptr && row->changed);
    (void)m_resetAll->setEnabled(!capturing);
    (void)m_save->setEnabled(!capturing && m_model.canSave && m_model.dirty);
}

std::optional<gui::LayoutRect> KeyBindingsPanel::lineBounds(usize line) const {
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

gui::EventReply KeyBindingsPanel::listEvent(const gui::RoutedEvent& ev) {
    if (ev.phase != gui::EventPhase::target) {
        return {};
    }
    if (const auto* p = std::get_if<gui::PointerEvent>(&ev.input)) {
        if (p->action == gui::PointerAction::down && p->button == gui::PointerButton::primary) {
            (void)m_host->input().setFocus(m_listNode);
            if (m_model.capturing.empty()) { // 바꾸는 중에 누른 것은 앱이 (목록 위면 바인딩이 아니다)
                if (const auto row = m_list.hitTest({p->x - m_listBounds.x, p->y - m_listBounds.y});
                    row && row->key < m_lines.size()) {
                    select(static_cast<usize>(row->key));
                }
            }
            return {true, true};
        }
        return {};
    }
    if (!m_model.capturing.empty() || m_lines.empty()) {
        return {}; // 바꾸는 중 — 누른 키는 앱이 바인딩으로 잡는다
    }
    const bool up = kit::keyDown(ev, gui::up_key), down = kit::keyDown(ev, gui::down_key);
    if (up || down) {
        // 액션 줄만 오간다 (겹침 · 머리는 건너뛴다)
        std::optional<usize> at;
        for (usize i = 0; i < m_lines.size(); ++i) {
            if (m_lines[i].kind == Line::Kind::Row && m_model.rows[m_lines[i].index].action == m_selected) {
                at = i;
            }
        }
        if (up) {
            for (usize i = at.value_or(m_lines.size()); i-- > 0;) {
                if (m_lines[i].kind == Line::Kind::Row) {
                    select(i);
                    break;
                }
            }
        } else {
            for (usize i = at ? *at + 1 : 0; i < m_lines.size(); ++i) {
                if (m_lines[i].kind == Line::Kind::Row) {
                    select(i);
                    break;
                }
            }
        }
        return {true, true};
    }
    if (kit::keyDown(ev, gui::enter_key) && selectedRow() != nullptr) {
        captureOrCancel();
        return {true, true};
    }
    return {};
}

Expected<void> KeyBindingsPanel::mount(uihost::UiHost& host) {
    m_host = &host;
    // 뿌리 · 안내 (두 줄) · 막대 (바꾸기 · 기본 · 모두 기본값 · 저장) · 목록 · 결과 (종류: 1 뿌리 · 2 글 · 3 줄 · 4 단추 · 5 목록)
    std::vector<gui::ComponentSpec> specs;
    const auto add = [&specs](std::optional<usize> parent, gui::ComponentType kind, gui::ComponentKey order) {
        specs.push_back({parent, kind, order, true});
        return specs.size() - 1;
    };
    const usize root = add(std::nullopt, 1, 0);
    const usize info = add(root, 2, 1);
    const usize bar = add(root, 3, 2);
    const usize capture = add(bar, 4, 1);
    const usize reset = add(bar, 4, 2);
    const usize resetAll = add(bar, 4, 3);
    const usize save = add(bar, 4, 4);
    const usize list = add(root, 5, 3);
    const usize status = add(root, 2, 4);
    auto h = kit::commitTree(host, specs);
    if (!h) {
        return std::unexpected(h.error());
    }
    const auto& H = *h;
    m_root = H[root];
    m_info = H[info];
    m_bar = H[bar];
    m_captureNode = H[capture];
    m_resetNode = H[reset];
    m_resetAllNode = H[resetAll];
    m_saveNode = H[save];
    m_listNode = H[list];
    m_statusNode = H[status];
    m_capture = kit::bindButton(host, m_captureNode, [this] { captureOrCancel(); });
    m_reset = kit::bindButton(host, m_resetNode, [this] {
        if (selectedRow() != nullptr) {
            request(KeyBindingsModel::kReset, m_selected);
        }
    });
    m_resetAll = kit::bindButton(host, m_resetAllNode, [this] { request(KeyBindingsModel::kResetAll); });
    m_save = kit::bindButton(host, m_saveNode, [this] { request(KeyBindingsModel::kSave); });
    applyEnabled();
    (void)host.input().setFocusable(m_listNode, true);
    (void)host.input().setHandler(m_listNode, [this](const gui::RoutedEvent& ev) { return listEvent(ev); });
    (void)host.setWheelHandler(m_listNode, [this](f32, f32 dy) {
        (void)m_list.scrollBy(-static_cast<double>(dy) * kRowH * 3.0);
        return true;
    });
    return {};
}

Expected<void> KeyBindingsPanel::layout(uihost::UiHost& host, const gui::ComponentSnapshot&, std::vector<gui::LayoutNode>& out) {
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
    leaf(m_info, inner, 2 * kTextH);
    gui::LayoutNode bar;
    bar.component = m_bar;
    bar.style.kind = gui::LayoutKind::row;
    bar.style.gap = kGap;
    out.push_back(bar);
    const double quarter = fit((inner - 3 * kGap) / 4);
    leaf(m_captureNode, quarter, kLine);
    leaf(m_resetNode, quarter, kLine);
    leaf(m_resetAllNode, quarter, kLine);
    leaf(m_saveNode, quarter, kLine);
    leaf(m_listNode, inner, fit(vp.height - 16 - 2 * kTextH - kLine - 2 * kTextH - 3 * kGap));
    leaf(m_statusNode, inner, 2 * kTextH);
    return {};
}

Expected<void> KeyBindingsPanel::paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) {
    const services::StudioColors& c = services::studioColors(m_ctx.theme);
    const auto focusR = host.input().focus();
    const bool listFocused = focusR && *focusR && **focusR == m_listNode;
    const auto btn = [&](const gui::LayoutBox& box, const gui::Button& b, const std::string& label) {
        out.add(box.component, b.enabled() ? kit::button(host, box, kit::visualOf(b, host), label, 13.0, false, c)
                                           : kit::disabledButton(host, box, label, 13.0, c));
    };
    const bool capturing = !m_model.capturing.empty();
    for (const gui::LayoutBox& box : frame.boxes()) {
        const double w = box.bounds.width;
        const double h = box.bounds.height;
        if (box.component == m_root) {
            out.add(box.component, {gui::FillRect{{0, 0, w, h}, rc(c.surface)}});
        } else if (box.component == m_info) {
            std::vector<gui::DrawCommand> d;
            d.push_back(gui::DrawText{{0, 0, w, 16}, kFont, 13.0, title(), capturing ? rc(c.accent) : rc(c.text)});
            if (!m_model.canSave) {
                d.push_back(gui::DrawText{{0, kTextH, w, 16}, kFont, 12.5, "안전 모드 — 저장하지 않는다 (이 실행에만)", rc(c.muted)});
            }
            out.add(box.component, std::move(d), true);
        } else if (box.component == m_captureNode) {
            btn(box, *m_capture, capturing ? "취소" : "바꾸기");
        } else if (box.component == m_resetNode) {
            btn(box, *m_reset, "기본");
        } else if (box.component == m_resetAllNode) {
            btn(box, *m_resetAll, "모두 기본값");
        } else if (box.component == m_saveNode) {
            btn(box, *m_save, "저장");
        } else if (box.component == m_statusNode) {
            out.add(box.component, {gui::DrawText{{0, 0, w, 16}, kFont, 12.5, m_model.status, rc(c.muted)}}, true);
        } else if (box.component == m_listNode) {
            m_listBounds = box.bounds;
            (void)m_list.resize({w, h}, kRowH);
            const double keyX = fit(std::min(w * 0.55, 260.0)); // 둘째 열 (키)
            std::vector<gui::DrawCommand> d;
            d.push_back(gui::FillRect{{0, 0, w, h}, rc(c.background)});
            const gui::ListRange range = m_list.visibleRange();
            for (usize i = range.first; i < range.past; ++i) {
                const auto vr = m_list.visibleRow(i);
                if (!vr || vr->key >= m_lines.size()) {
                    continue;
                }
                const Line& l = m_lines[static_cast<usize>(vr->key)];
                const gui::LayoutRect b = vr->bounds;
                if (l.kind == Line::Kind::Conflict) {
                    d.push_back(gui::DrawText{{b.x + 8, b.y + 3, fit(b.width - 14), 16}, kFont, 12.5,
                                              lineText(static_cast<usize>(vr->key)), rc(c.warning)});
                    continue;
                }
                if (l.kind == Line::Kind::Header) {
                    d.push_back(gui::FillRect{{b.x, b.y + 1, b.width, fit(b.height - 2)}, rc(c.elevated)});
                    d.push_back(gui::DrawText{{b.x + 6, b.y + 3, fit(b.width - 12), 16}, kFont, 12.5,
                                              lineText(static_cast<usize>(vr->key)), rc(c.muted)});
                    continue;
                }
                const KeyBindingRow& r = m_model.rows[l.index];
                const bool sel = r.action == m_selected;
                const bool cap = r.action == m_model.capturing;
                if (sel || cap) {
                    d.push_back(gui::FillRect{b, listFocused || cap ? rc(c.header) : rc(c.headerHover)});
                    d.push_back(gui::FillRect{{b.x, b.y, 3, b.height}, rc(c.accent)});
                }
                d.push_back(gui::DrawText{{b.x + 8, b.y + 3, fit(keyX - 14), 16}, kFont, 12.5, r.action,
                                          r.changed ? rc(c.accent) : rc(c.text)});
                d.push_back(gui::DrawText{{b.x + keyX, b.y + 3, fit(b.width - keyX - 8), 16}, kFont, 12.5,
                                          cap ? std::string("...") : (r.keys.empty() ? std::string("-") : r.keys),
                                          cap ? rc(c.accent) : rc(c.muted)});
            }
            if (m_lines.empty()) {
                d.push_back(gui::DrawText{{8, 8, fit(w - 16), 18}, kFont, 13.0, "키 표가 없다", rc(c.muted)});
            }
            out.add(box.component, std::move(d), true);
        }
    }
    return {};
}

} // namespace sbx::editor::gui
