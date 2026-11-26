#include "uihost/DocumentView.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

#include "foundation/text/Utf8.hpp"
#include "uihost/InputRouting.hpp"

namespace sbx::uihost {

namespace {

using Clock = std::chrono::steady_clock; // UI 계측만 (시뮬레이션과 무관)

} // namespace

Expected<std::unique_ptr<DocumentView>> DocumentView::create(gui::Document document, gui::DocumentSession::Setup setup) {
    std::unique_ptr<DocumentView> view(new DocumentView());
    for (const gui::DocumentNode& n : document.nodes) {
        if (n.type == gui::DocumentKind::text_box) {
            view->m_textBoxes.push_back(n.id);
        }
    }
    auto session = gui::DocumentSession::create(std::move(document), std::move(setup));
    if (!session) {
        return std::unexpected(toError(session.error(), "DocumentSession::create"));
    }
    view->m_session = std::move(*session);
    return view;
}

std::optional<std::string> DocumentView::textBoxOf(gui::ComponentHandle h) const {
    for (const std::string& id : m_textBoxes) {
        const auto found = m_session->find(id);
        if (found && *found && **found == h) {
            return id;
        }
    }
    return std::nullopt;
}

void DocumentView::feed(std::span<const platform::PlatformEvent> events) {
    m_stats.inputEvents = 0;
    std::string pending;
    const auto deliver = [&] {
        if (pending.empty()) {
            return;
        }
        if (m_focusText) {
            ++m_stats.inputEvents;
            const auto r = m_session->editText(*m_focusText, {gui::TextEditAction::replace_selection, pending});
            if (!r) {
                ++m_stats.inputRejected; // 문서 검증기가 거절 · 포커스가 떠났다
            }
        }
        pending.clear();
    };
    for (const platform::PlatformEvent& e : events) {
        if (const auto* t = std::get_if<platform::TextInput>(&e)) {
            utf8::append(pending, t->codepoint);
            continue;
        }
        deliver();
        if (const auto* k = std::get_if<platform::KeyDown>(&e)) {
            m_mods = k->mods;
        } else if (const auto* u = std::get_if<platform::KeyUp>(&e)) {
            m_mods = u->mods;
        } else if (std::holds_alternative<platform::FocusLost>(e)) {
            m_mods = {};
            (void)m_session->pointer({gui::PointerAction::cancel, kMousePointer, 0.0, 0.0});
            continue;
        }
        auto in = translate(e, m_mods);
        if (!in) {
            continue;
        }
        ++m_stats.inputEvents;
        if (auto* p = std::get_if<gui::PointerEvent>(&*in)) {
            p->x /= static_cast<double>(m_scale);
            p->y /= static_cast<double>(m_scale);
            if (!m_rendered) {
                continue; // 아직 배치가 없다 — 누를 자리가 없다
            }
            const auto r = m_session->pointer(*p);
            if (!r) {
                ++m_stats.inputRejected;
            } else if (p->action == gui::PointerAction::down) {
                // 누른 곳이 TextBox 면 거기가 포커스 (Core 의 TextBox 는 누르면 포커스를 잡는다), 아니면 글자 대상 없음
                m_focusText = r->target ? textBoxOf(*r->target) : std::nullopt;
            }
        } else if (const auto* key = std::get_if<gui::KeyEvent>(&*in)) {
            const auto r = m_session->key(*key);
            if (!r) {
                ++m_stats.inputRejected;
            }
        }
    }
    deliver();
}

Expected<const gui::DisplayList*> DocumentView::frame(gui::LayoutSize logical) {
    if (!(logical.width >= 0.0) || !(logical.height >= 0.0)) {
        return makeError(ErrorCode::InvalidArgument, "GXML 화면 크기가 음수 · NaN");
    }
    const double w = logical.width / m_scale;
    const double h = logical.height / m_scale;
    const auto t0 = Clock::now();
    if (auto r = m_session->render({{w, w}, {h, h}}); !r) {
        return std::unexpected(toError(r.error(), "DocumentSession::render"));
    }
    m_stats.layoutMs = std::chrono::duration<f64, std::milli>(Clock::now() - t0).count();
    m_stats.layoutMsTotal += m_stats.layoutMs;
    m_stats.layoutMsMax = std::max(m_stats.layoutMsMax, m_stats.layoutMs);
    m_rendered = true;
    ++m_stats.frames;
    m_stats.components = m_session->layout().boxes().size();
    m_stats.commands = m_session->display().commands().size();
    m_stats.treeRevision = m_session->layout().treeRevision();
    return &m_session->display();
}

TextInputState DocumentView::textInput() const {
    TextInputState out;
    if (!m_focusText || !m_rendered) {
        return out;
    }
    out.active = true;
    const auto h = m_session->find(*m_focusText);
    if (!h || !*h) {
        return out;
    }
    for (const gui::LayoutBox& b : m_session->layout().boxes()) {
        if (b.component == **h) {
            // 캐럿을 모른다 — 상자의 왼쪽 아래 (후보 창이 글자 칸을 가리지 않는다)
            const double s = m_scale;
            out.caret = gui::LayoutRect{b.bounds.x * s, b.bounds.y * s, 1.0, b.bounds.height * s};
            break;
        }
    }
    return out;
}

bool DocumentView::hitsUi(f32 x, f32 y) const noexcept {
    if (!m_rendered || m_session->layout().boxes().empty()) {
        return false;
    }
    const auto hit = m_session->layout().hitTest({static_cast<double>(x) / m_scale, static_cast<double>(y) / m_scale});
    return hit.has_value() && *hit != m_session->layout().boxes().front().component;
}

} // namespace sbx::uihost
