#include "editor/gui/TextField.hpp"

#include <algorithm>

namespace sbx::editor::gui {

namespace {

constexpr double kPad = 7.0;

gui::RenderColor rc(services::Rgba8 c, u8 alpha = 255) {
    return {c.r, c.g, c.b, alpha};
}

double fit(double v) {
    return v > 0.0 ? v : 0.0;
}

} // namespace

gui::TextEditCallbacks TextField::callbacks() {
    return m_host->editCallbacks([this](std::string_view v) -> gui::Result<void> {
        m_value = std::string(v);
        if (m_changed) {
            m_changed(m_value);
        }
        return {};
    });
}

Expected<void> TextField::mount(uihost::UiHost& host, gui::ComponentHandle handle, Changed changed, KeyHook keys) {
    m_host = &host;
    m_handle = handle;
    m_changed = std::move(changed);
    m_keys = std::move(keys);
    m_input = std::make_unique<gui::TextInput>(host.input(), handle, m_maxBytes);
    if (auto r = m_input->setEnabled(true); !r) {
        return std::unexpected(uihost::toError(r.error(), "TextInput::setEnabled"));
    }
    if (auto r = m_input->setText(m_value); !r) {
        return std::unexpected(uihost::toError(r.error(), "TextInput::setText"));
    }
    if (auto r = host.input().setHandler(handle, [this](const gui::RoutedEvent& ev) { return onEvent(ev); }); !r) {
        return std::unexpected(uihost::toError(r.error(), "setHandler"));
    }
    uihost::TextTarget target;
    target.insert = [this](std::string_view utf8) {
        if (auto session = m_input->beginInput(); session && *session) {
            (void)m_input->edit(**session, gui::TextEdit{gui::TextEditAction::replace_selection, utf8, false, std::nullopt}, callbacks());
        }
    };
    target.caret = [this]() -> std::optional<gui::LayoutRect> {
        const usize caret = std::min(m_input->selection().caret, m_value.size());
        const double x = kPad + m_host->text().measure(std::string_view(m_value).substr(0, caret), m_em).width;
        return gui::LayoutRect{x, kPad, 1.0, m_em * 1.25};
    };
    return host.setTextTarget(handle, std::move(target));
}

void TextField::setValue(std::string value) {
    m_value = std::move(value);
    if (m_input) {
        (void)m_input->setText(m_value);
    }
}

gui::EventReply TextField::onEvent(const gui::RoutedEvent& ev) {
    const auto r = m_input->handle(ev, callbacks());
    // 눌러 캐럿 놓기 — Core 의 TextInput 은 포커스만 잡는다, 자리는 호스트의 글자 배치 (그래핌 경계)
    if (const auto* p = std::get_if<gui::PointerEvent>(&ev.input);
        p != nullptr && p->action == gui::PointerAction::down && ev.phase == gui::EventPhase::target) {
        const usize at = uihost::caretFromX(m_host->text(), m_value, m_em, p->x - m_bounds.x - kPad);
        if (auto session = m_input->beginInput(); session && *session) {
            (void)m_input->setSelection(**session, {at, at});
        }
    }
    if (r && r->handled) {
        return *r;
    }
    if (const auto* k = std::get_if<gui::KeyEvent>(&ev.input);
        k != nullptr && ev.phase == gui::EventPhase::target && m_keys && m_keys(*k)) {
        return {true, true};
    }
    return r ? *r : gui::EventReply{};
}

void TextField::paint(const uihost::UiHost& host, const gui::LayoutBox& box, bool focused,
                      const services::StudioColors& c, uihost::PaintList& out) {
    m_bounds = box.bounds;
    const double w = box.bounds.width;
    const double h = box.bounds.height;
    std::vector<gui::DrawCommand> d;
    d.push_back(gui::FillRect{{0, 0, w, h}, rc(c.elevated)});
    d.push_back(gui::FillRect{{0, fit(h - 2), w, 2}, focused ? rc(c.accent) : rc(c.border)});
    const gui::TextSelection sel = m_input ? m_input->selection() : gui::TextSelection{};
    const std::string_view v = m_value;
    const usize a = std::min({sel.anchor, sel.caret, v.size()});
    const usize b = std::min(std::max(sel.anchor, sel.caret), v.size());
    if (focused && a != b) {
        const double x0 = host.text().measure(v.substr(0, a), m_em).width;
        const double x1 = host.text().measure(v.substr(0, b), m_em).width;
        d.push_back(gui::FillRect{{kPad + x0, 4, fit(x1 - x0), fit(h - 8)}, rc(c.accent, 77)});
    }
    const bool empty = m_value.empty();
    d.push_back(gui::DrawText{{kPad, fit((h - m_em * 1.25) / 2), fit(w - 2 * kPad), m_em * 1.25}, 1, m_em,
                              empty ? m_placeholder : m_value, empty ? rc(c.muted) : rc(c.text)});
    if (focused) {
        const double x = host.text().measure(v.substr(0, std::min(sel.caret, v.size())), m_em).width;
        d.push_back(gui::FillRect{{kPad + x, 5, 1.5, fit(h - 10)}, rc(c.text)});
    }
    out.add(m_handle, std::move(d), true);
}

} // namespace sbx::editor::gui
