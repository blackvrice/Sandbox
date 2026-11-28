#include "editor/gui/WorldPanel.hpp"

#include <algorithm>
#include <charconv>
#include <format>

#include "editor/gui/PanelKit.hpp"

namespace sbx::editor::gui {

using kit::fit;
using kit::kFont;
using kit::rc;

namespace {

constexpr double kLine = 26.0;
constexpr double kRowH = 22.0;

} // namespace

void WorldPanel::setModel(WorldModel model) {
    m_model = std::move(model);
    std::vector<gui::ListItemKey> keys(m_model.saved.size());
    for (usize i = 0; i < keys.size(); ++i) {
        keys[i] = i; // 이름 차례 (서버 목록) — 목록이 같으면 그대로
    }
    if (keys != m_keys) {
        m_keys = std::move(keys);
        (void)m_list.setItems(m_keys);
    }
    if (m_selected && !selectedRow()) {
        m_selected.reset(); // 목록에서 사라졌다
    }
    // 이름 칸을 쓰는 중이 아니면 앱의 이름 (Ctrl+S · 목록 고르기) 을 따라간다
    if (m_host != nullptr) {
        const auto focus = m_host->input().focus();
        if (!(focus && *focus && **focus == m_name.handle()) && m_name.value() != m_model.name) {
            m_name.setValue(m_model.name);
        }
    }
    // 재생이 아니면 탐색 칸은 비운다, 재생이면 쓰는 중이 아닐 때 지금 call 을
    if (m_model.mode != 2) {
        m_seek.setValue({});
        m_seekError.clear();
    }
}

std::optional<usize> WorldPanel::selectedRow() const {
    if (!m_selected) {
        return std::nullopt;
    }
    for (usize i = 0; i < m_model.saved.size(); ++i) {
        if (m_model.saved[i].name == *m_selected) {
            return i;
        }
    }
    return std::nullopt;
}

std::string WorldPanel::status() const {
    if (!m_model.available) {
        return "월드 — 프로젝트를 열면 저장 · 기록 · 테스트 플레이";
    }
    return std::format("{} · 월드 #{}", m_model.modeLine, m_model.generation);
}

void WorldPanel::emit(int op, std::string name, u64 target) {
    WorkspaceIntent i = WorkspaceIntent::of(WorkspaceIntent::Kind::WorldOp, std::move(name), op);
    i.component = target;
    m_ctx.intent = std::move(i);
}

void WorldPanel::seek() {
    if (!m_seekButton->enabled()) {
        return;
    }
    const std::string& t = m_seek.value();
    u64 call = 0;
    const auto [end, ec] = std::from_chars(t.data(), t.data() + t.size(), call);
    if (t.empty() || ec != std::errc{} || end != t.data() + t.size()) {
        m_seekError = "call 은 0 이상의 정수";
        return;
    }
    m_seekError.clear();
    // ImGui 슬라이더처럼 0 ~ 전체로 자른다 (닿으면 멈춘 채 — 이어 보기는 실행 제어)
    emit(WorldModel::kSeek, m_model.modeName, std::min(call, m_model.replayCalls));
}

void WorldPanel::openSelected() {
    const auto row = selectedRow();
    if (!row || !m_model.canChange) {
        return;
    }
    const SavedEntry& e = m_model.saved[*row];
    emit(e.replay ? WorldModel::kPlay : WorldModel::kLoad, e.name);
}

void WorldPanel::applyEnabled() {
    if (!m_save) {
        return;
    }
    const bool ok = m_model.available;
    const bool change = ok && m_model.canChange;
    m_name.setEnabled(change);
    // 이름 검사는 앱 (net::isValidSaveName) 이 지금 이름으로 — 쓰는 중이면 그 글로 다음 frame 에 (앱이 다시 검사한다)
    const bool nameOk = m_model.nameOk && m_name.value() == m_model.name;
    (void)m_save->setEnabled(change && nameOk);
    (void)m_record->setEnabled(change && (m_model.mode == 1 || (nameOk && m_model.mode != 2)));
    (void)m_refresh->setEnabled(ok);
    (void)m_test->setEnabled(change && m_model.mode != 1 && m_model.mode != 2);
    (void)m_open->setEnabled(change && selectedRow().has_value());
    const bool replaying = change && m_model.mode == 2 && m_model.replayCalls > 0;
    m_seek.setEnabled(replaying);
    (void)m_seekButton->setEnabled(replaying);
}

std::optional<gui::LayoutRect> WorldPanel::rowBounds(usize row) const {
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

gui::EventReply WorldPanel::listEvent(const gui::RoutedEvent& ev) {
    if (ev.phase != gui::EventPhase::target) {
        return {};
    }
    const auto pick = [this](usize row) {
        const SavedEntry& e = m_model.saved[row];
        m_selected = e.name;
        (void)m_list.reveal(m_keys[row]);
        if (!e.replay && m_model.canChange) { // 같은 이름으로 덮어 저장하기 쉽게 (ImGui 창과 같다)
            m_name.setValue(e.name);
            m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::WorldName, e.name);
        }
        applyEnabled();
    };
    if (const auto* p = std::get_if<gui::PointerEvent>(&ev.input)) {
        if (p->action == gui::PointerAction::down && p->button == gui::PointerButton::primary) {
            (void)m_host->input().setFocus(m_listNode);
            if (const auto row = m_list.hitTest({p->x - m_listBounds.x, p->y - m_listBounds.y});
                row && row->key < m_model.saved.size()) {
                pick(static_cast<usize>(row->key));
            }
            return {true, true};
        }
        return {};
    }
    if (m_model.saved.empty()) {
        return {};
    }
    const bool up = kit::keyDown(ev, gui::up_key), down = kit::keyDown(ev, gui::down_key);
    if (up || down) {
        usize at = selectedRow().value_or(up ? m_model.saved.size() : static_cast<usize>(-1));
        at = up ? (at == 0 ? 0 : at - 1) : std::min(at + 1, m_model.saved.size() - 1);
        pick(at);
        return {true, true};
    }
    if (kit::keyDown(ev, gui::enter_key)) {
        openSelected(); // Enter = 불러오기 · 재생
        return {true, true};
    }
    return {};
}

Expected<void> WorldPanel::mount(uihost::UiHost& host) {
    m_host = &host;
    // 뿌리 · 상태 · 탐색 줄 (call 칸 · 탐색 · 전체) · 이름 칸 · 막대 (저장 · 기록 · 새로고침) · 테스트 플레이 · 목록 · 불러오기 / 재생 · 결과
    // (종류: 1 뿌리 · 2 글 · 3 줄 · 4 단추 · 5 목록 · 6 글 칸)
    std::vector<gui::ComponentSpec> specs;
    const auto add = [&specs](std::optional<usize> parent, gui::ComponentType kind, gui::ComponentKey order) {
        specs.push_back({parent, kind, order, true});
        return specs.size() - 1;
    };
    const usize root = add(std::nullopt, 1, 0);
    const usize status = add(root, 2, 1);
    const usize seekRow = add(root, 3, 2);
    const usize seekField = add(seekRow, 6, 1);
    const usize seekBtn = add(seekRow, 4, 2);
    const usize seekLabel = add(seekRow, 2, 3);
    const usize name = add(root, 6, 3);
    const usize bar = add(root, 3, 4);
    const usize save = add(bar, 4, 1);
    const usize record = add(bar, 4, 2);
    const usize refresh = add(bar, 4, 3);
    const usize test = add(root, 4, 5);
    const usize list = add(root, 5, 6);
    const usize open = add(root, 4, 7);
    const usize message = add(root, 2, 8);
    auto h = kit::commitTree(host, specs);
    if (!h) {
        return std::unexpected(h.error());
    }
    const auto& H = *h;
    m_root = H[root];
    m_status = H[status];
    m_seekRow = H[seekRow];
    m_seekNode = H[seekBtn];
    m_seekLabel = H[seekLabel];
    m_bar = H[bar];
    m_saveNode = H[save];
    m_recordNode = H[record];
    m_refreshNode = H[refresh];
    m_testNode = H[test];
    m_listNode = H[list];
    m_openNode = H[open];
    m_message = H[message];
    const auto enterKey = [](const gui::KeyEvent& k) {
        return k.action == gui::KeyAction::down && k.chord.key == gui::enter_key && k.chord.modifiers == gui::KeyModifiers::none;
    };
    const auto nameKeys = [this, enterKey](const gui::KeyEvent& k) {
        if (enterKey(k)) {
            if (m_save->enabled()) {
                emit(WorldModel::kSave, m_name.value()); // Enter = 저장
            }
            return true;
        }
        return false;
    };
    if (auto r = m_name.mount(host, H[name],
                              [this](const std::string& v) {
                                  m_ctx.intent = WorkspaceIntent::of(WorkspaceIntent::Kind::WorldName, v); // Ctrl+S 와 같은 이름
                              },
                              nameKeys);
        !r) {
        return r;
    }
    const auto seekKeys = [this, enterKey](const gui::KeyEvent& k) {
        if (enterKey(k)) {
            seek();
            return true;
        }
        return false;
    };
    if (auto r = m_seek.mount(host, H[seekField], [this](const std::string&) { m_seekError.clear(); }, seekKeys); !r) {
        return r;
    }
    m_seekButton = kit::bindButton(host, m_seekNode, [this] { seek(); });
    m_save = kit::bindButton(host, m_saveNode, [this] { emit(WorldModel::kSave, m_name.value()); });
    m_record = kit::bindButton(host, m_recordNode, [this] {
        if (m_model.mode == 1) {
            emit(WorldModel::kRecordStop, m_model.modeName);
        } else {
            emit(WorldModel::kRecordStart, m_name.value());
        }
    });
    m_refresh = kit::bindButton(host, m_refreshNode, [this] { emit(WorldModel::kList, {}); });
    m_test = kit::bindButton(host, m_testNode, [this] {
        emit(m_model.mode == 3 ? WorldModel::kTestStop : WorldModel::kTestPlay, {}); // 멈추면 편집 월드로 (저장은 멈추기 전에)
    });
    m_open = kit::bindButton(host, m_openNode, [this] { openSelected(); });
    applyEnabled();
    (void)host.input().setFocusable(m_listNode, true);
    (void)host.input().setHandler(m_listNode, [this](const gui::RoutedEvent& ev) { return listEvent(ev); });
    (void)host.setWheelHandler(m_listNode, [this](f32, f32 dy) {
        (void)m_list.scrollBy(-static_cast<double>(dy) * kRowH * 3.0);
        return true;
    });
    return {};
}

Expected<void> WorldPanel::layout(uihost::UiHost& host, const gui::ComponentSnapshot&, std::vector<gui::LayoutNode>& out) {
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
    leaf(m_status, inner, 16);
    row(m_seekRow);
    leaf(m_seek.handle(), 90, kLine);
    leaf(m_seekNode, 56, kLine);
    leaf(m_seekLabel, fit(inner - 90 - 56 - 2 * kGap), kLine);
    leaf(m_name.handle(), inner, kLine);
    row(m_bar);
    const double third = fit((inner - 2 * kGap) / 3);
    leaf(m_saveNode, third, kLine);
    leaf(m_recordNode, third, kLine);
    leaf(m_refreshNode, third, kLine);
    leaf(m_testNode, inner, kLine);
    // 고정 높이: 여백 16 · 글 줄 2 (16) · 칸 줄 5 (26) · 사이 7
    leaf(m_listNode, inner, fit(vp.height - 16 - 2 * 16 - 5 * kLine - 7 * kGap));
    leaf(m_openNode, inner, kLine);
    leaf(m_message, inner, 16);
    return {};
}

Expected<void> WorldPanel::paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) {
    const services::StudioColors& c = services::studioColors(m_ctx.theme);
    const auto focusR = host.input().focus();
    const std::optional<gui::ComponentHandle> focus = focusR ? *focusR : std::nullopt;
    const auto btn = [&](const gui::LayoutBox& box, const gui::Button& b, const std::string& label, bool accent) {
        out.add(box.component, b.enabled() ? kit::button(host, box, kit::visualOf(b, host), label, 13.0, accent, c)
                                           : kit::disabledButton(host, box, label, 13.0, c));
    };
    const auto row = selectedRow();
    for (const gui::LayoutBox& box : frame.boxes()) {
        const double w = box.bounds.width;
        const double h = box.bounds.height;
        if (box.component == m_root) {
            out.add(box.component, {gui::FillRect{{0, 0, w, h}, rc(c.surface)}});
        } else if (box.component == m_status) {
            out.add(box.component,
                    {gui::DrawText{{0, 0, w, h}, kFont, 13.0, status(),
                                   !m_model.available ? rc(c.muted) : m_model.mode == 0 ? rc(c.text) : rc(c.accent)}},
                    true);
        } else if (box.component == m_seekLabel) {
            const std::string t = !m_seekError.empty() ? m_seekError
                                  : m_model.mode == 2  ? std::format("/ {} call (지금 {})", m_model.replayCalls, m_model.replayCall)
                                                       : std::string("재생 중에 탐색");
            out.add(box.component,
                    {gui::DrawText{{0, fit((h - 16) / 2), w, 16}, kFont, 12.5, t, m_seekError.empty() ? rc(c.muted) : rc(c.error)}},
                    true);
        } else if (box.component == m_name.handle()) {
            m_name.paint(host, box, focus && *focus == box.component, c, out);
        } else if (box.component == m_seek.handle()) {
            m_seek.paint(host, box, focus && *focus == box.component, c, out);
        } else if (box.component == m_seekNode) {
            btn(box, *m_seekButton, "탐색", false);
        } else if (box.component == m_saveNode) {
            btn(box, *m_save, "저장", false);
        } else if (box.component == m_recordNode) {
            btn(box, *m_record, m_model.mode == 1 ? "기록 끝" : "기록 시작", false);
        } else if (box.component == m_refreshNode) {
            btn(box, *m_refresh, "새로고침", false);
        } else if (box.component == m_testNode) {
            btn(box, *m_test, m_model.mode == 3 ? "테스트 플레이 멈추기 (되돌리기)" : "테스트 플레이", m_model.mode != 3);
        } else if (box.component == m_openNode) {
            btn(box, *m_open, row && m_model.saved[*row].replay ? "재생" : "불러오기", false);
        } else if (box.component == m_message) {
            out.add(box.component, {gui::DrawText{{0, 0, w, h}, kFont, 12.5, m_model.message, rc(c.warning)}}, true);
        } else if (box.component == m_listNode) {
            m_listBounds = box.bounds;
            (void)m_list.resize({w, h}, kRowH);
            std::vector<gui::DrawCommand> d;
            d.push_back(gui::FillRect{{0, 0, w, h}, rc(c.background)});
            const bool listFocused = focus && *focus == m_listNode;
            const gui::ListRange range = m_list.visibleRange();
            for (usize i = range.first; i < range.past; ++i) {
                const auto vr = m_list.visibleRow(i);
                if (!vr || vr->key >= m_model.saved.size()) {
                    continue;
                }
                const usize index = static_cast<usize>(vr->key);
                const SavedEntry& e = m_model.saved[index];
                const gui::LayoutRect b = vr->bounds;
                if (row == index) {
                    d.push_back(gui::FillRect{b, listFocused ? rc(c.header) : rc(c.headerHover)});
                    d.push_back(gui::FillRect{{b.x, b.y, 2, b.height}, rc(c.accent)});
                }
                d.push_back(gui::DrawText{{b.x + 8, b.y + 3, fit(b.width - 14), 16}, kFont, 12.5, e.text,
                                          e.replay ? rc(c.accent) : rc(c.text)});
            }
            if (m_model.saved.empty()) {
                d.push_back(gui::DrawText{{8, 8, fit(w - 16), 18}, kFont, 13.0,
                                          !m_model.available ? "월드 없음"
                                          : !m_model.listed  ? "목록을 받는 중…"
                                                             : "저장된 세이브 · 리플레이가 없습니다",
                                          rc(c.muted)});
            }
            out.add(box.component, std::move(d), true);
        }
    }
    return {};
}

} // namespace sbx::editor::gui
