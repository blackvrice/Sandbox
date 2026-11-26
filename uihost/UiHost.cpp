#include "uihost/UiHost.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <type_traits>
#include <utility>

#include "foundation/text/Utf8.hpp"
#include "uihost/InputRouting.hpp"

namespace sbx::uihost {

namespace {

using Clock = std::chrono::steady_clock; // UI 계측만 (시뮬레이션과 무관 — 04 의 벽시계 금지는 Simulation 에)

f64 msSince(Clock::time_point t0) {
    return std::chrono::duration<f64, std::milli>(Clock::now() - t0).count();
}

} // namespace

std::string_view guiErrorName(gui::ErrorCode code) noexcept {
    switch (code) {
    case gui::ErrorCode::invalid_argument:
        return "invalid_argument";
    case gui::ErrorCode::out_of_memory:
        return "out_of_memory";
    case gui::ErrorCode::wrong_thread:
        return "wrong_thread";
    case gui::ErrorCode::stale_handle:
        return "stale_handle";
    case gui::ErrorCode::closed:
        return "closed";
    case gui::ErrorCode::limit_exceeded:
        return "limit_exceeded";
    case gui::ErrorCode::dependency_cycle:
        return "dependency_cycle";
    case gui::ErrorCode::evaluation_failed:
        return "evaluation_failed";
    case gui::ErrorCode::busy:
        return "busy";
    case gui::ErrorCode::queue_full:
        return "queue_full";
    }
    return "unknown";
}

Error toError(gui::ErrorCode code, std::string_view what) {
    ErrorCode sbxCode = ErrorCode::Unknown;
    switch (code) {
    case gui::ErrorCode::invalid_argument:
    case gui::ErrorCode::stale_handle:
    case gui::ErrorCode::wrong_thread:
        sbxCode = ErrorCode::InvalidArgument;
        break;
    case gui::ErrorCode::limit_exceeded:
    case gui::ErrorCode::queue_full:
        sbxCode = ErrorCode::OutOfRange;
        break;
    case gui::ErrorCode::dependency_cycle:
    case gui::ErrorCode::evaluation_failed:
        sbxCode = ErrorCode::ValidationFailed;
        break;
    default:
        break;
    }
    return Error{sbxCode, std::format("GraphicsUI {}: {}", what, guiErrorName(code)), {}};
}

gui::LayoutSize ApproxTextMeasure::measure(std::string_view utf8, double emSize) const {
    double width = 0.0;
    usize pos = 0;
    while (pos < utf8.size()) {
        const char32_t cp = utf8::decodeNext(utf8, pos);
        width += cp < 0x80 ? 0.55 * emSize : emSize;
    }
    return {width, 1.25 * emSize};
}

void PaintList::add(gui::ComponentHandle component, std::vector<gui::DrawCommand> draws, bool clipToBounds) {
    m_storage.push_back(std::move(draws));
    gui::RenderNode node;
    node.component = component;
    node.clip_to_bounds = clipToBounds;
    node.draws = m_storage.back();
    m_nodes.push_back(node);
}

void PaintList::clear() {
    m_nodes.clear();
    m_storage.clear();
}

Expected<std::unique_ptr<UiHost>> UiHost::create(const ITextMeasure& text) {
    std::unique_ptr<UiHost> host(new UiHost(text));
    auto tree = gui::ComponentTree::create();
    if (!tree) {
        return std::unexpected(toError(tree.error(), "ComponentTree::create"));
    }
    host->m_tree = std::move(*tree);
    auto state = gui::StateStore::create(*host->m_tree);
    if (!state) {
        return std::unexpected(toError(state.error(), "StateStore::create"));
    }
    host->m_state = std::move(*state);
    auto input = gui::InputDispatcher::create(*host->m_tree);
    if (!input) {
        return std::unexpected(toError(input.error(), "InputDispatcher::create"));
    }
    host->m_input = std::move(*input);
    return host;
}

UiHost::~UiHost() {
    // 입력 · 상태가 트리를 본다 — 그것들을 먼저 닫고 트리를 닫는다 (닫힌 뒤의 제거 알림은 화면 핸들러를 부르지 않는다)
    m_display.reset();
    m_layout.reset();
    m_paint.clear();
    if (m_input) {
        (void)m_input->close();
    }
    if (m_state) {
        (void)m_state->close();
    }
    if (m_tree) {
        (void)m_tree->close();
    }
}

Expected<gui::StateSender> UiHost::sender() const {
    auto s = m_state->sender();
    if (!s) {
        return std::unexpected(toError(s.error(), "StateStore::sender"));
    }
    return std::move(*s);
}

Expected<void> UiHost::setScreen(IUiScreen* screen) {
    // 앞 화면의 배치 · 그리기는 그 트리의 세대를 가리킨다 — 먼저 버린다
    m_display.reset();
    m_layout.reset();
    m_paint.clear();
    m_hover.reset();
    m_screen = nullptr;
    if (auto r = m_tree->commit({}); !r) { // 빈 트리 — 구독 · 포커스 · 캡처가 풀린다
        return std::unexpected(toError(r.error(), "ComponentTree::commit (비우기)"));
    }
    if (screen == nullptr) {
        return {};
    }
    if (auto r = screen->mount(*this); !r) {
        (void)m_tree->commit({});
        return std::unexpected(r.error());
    }
    m_screen = screen;
    return {};
}

void UiHost::feed(std::span<const platform::PlatformEvent> events) {
    m_stats.inputEvents = 0;
    if (m_screen == nullptr) {
        return;
    }
    for (const platform::PlatformEvent& e : events) {
        if (const auto* k = std::get_if<platform::KeyDown>(&e)) {
            m_mods = k->mods;
        } else if (const auto* u = std::get_if<platform::KeyUp>(&e)) {
            m_mods = u->mods;
        } else if (std::holds_alternative<platform::FocusLost>(e)) {
            // 창을 떠나면 누른 포인터를 끝낸다 (캡처 손실 = 대상 없는 cancel — CORE_API 입력 계약)
            m_mods = {};
            m_hover.reset();
            (void)m_input->dispatchPointer({gui::PointerAction::cancel, kMousePointer, 0.0, 0.0});
            continue;
        }
        const auto in = translate(e, m_mods);
        if (!in) {
            continue;
        }
        ++m_stats.inputEvents;
        if (const auto* p = std::get_if<gui::PointerEvent>(&*in)) {
            // 직전 배치가 지금 트리의 것일 때만 대상을 준다 — 지난 세대 핸들은 디스패처가 거절한다
            std::optional<gui::ComponentHandle> hit;
            if (m_layout && m_layout->treeRevision() == m_stats.treeRevision) {
                hit = m_layout->hitTest({p->x, p->y});
            }
            m_hover = hit;
            const auto r = m_input->dispatchPointer(*p, hit);
            if (!r) {
                ++m_stats.inputRejected;
            }
        } else if (const auto* key = std::get_if<gui::KeyEvent>(&*in)) {
            const auto r = m_input->dispatchKey(*key);
            if (!r) {
                ++m_stats.inputRejected;
            } else if (!r->handled && key->action == gui::KeyAction::down && key->chord.key == 0x09) {
                // Tab · Shift+Tab — 아무도 처리하지 않으면 포커스 순회 (호스트 몫 — CORE_API 포커스)
                const bool back = (static_cast<unsigned>(key->chord.modifiers) &
                                   static_cast<unsigned>(gui::KeyModifiers::shift)) != 0;
                (void)m_input->moveFocus(back ? gui::FocusDirection::backward : gui::FocusDirection::forward);
            }
        }
    }
}

Expected<const gui::DisplayList*> UiHost::frame(gui::LayoutSize viewport) {
    if (!(viewport.width >= 0.0) || !(viewport.height >= 0.0)) { // NaN 도 여기서 (비교가 거짓)
        return makeError(ErrorCode::InvalidArgument, "GraphicsUI 화면 크기가 음수 · NaN");
    }
    if (m_screen == nullptr) {
        return nullptr;
    }
    m_viewport = viewport;
    // 1. 작업자 결과 (도착 순서). 지난 세대 대상 · 검증 실패는 그 메시지만 버린다
    if (auto drained = m_state->drain(); drained) {
        for (const auto& r : *drained) {
            if (!r) {
                ++m_stats.workerErrors;
            }
        }
    } else {
        return std::unexpected(toError(drained.error(), "StateStore::drain"));
    }

    // 2. 배치
    const auto t0 = Clock::now();
    auto snapshot = m_tree->snapshot();
    if (!snapshot) {
        return std::unexpected(toError(snapshot.error(), "ComponentTree::snapshot"));
    }
    m_nodes.clear();
    if (auto r = m_screen->layout(*this, *snapshot, m_nodes); !r) {
        return std::unexpected(r.error());
    }
    const gui::LayoutConstraints vp{{viewport.width, viewport.width}, {viewport.height, viewport.height}};
    auto measured = m_cache.measure(*snapshot, m_nodes, vp);
    if (!measured) {
        return std::unexpected(toError(measured.error(), "LayoutCache::measure"));
    }
    auto arranged = gui::arrangeLayout(*measured);
    if (!arranged) {
        return std::unexpected(toError(arranged.error(), "arrangeLayout"));
    }
    m_layout = std::make_unique<gui::LayoutFrame>(std::move(*arranged));
    m_stats.layoutMs = msSince(t0);

    // 3. 그리기
    const auto t1 = Clock::now();
    m_paint.clear();
    if (auto r = m_screen->paint(*this, *m_layout, m_paint); !r) {
        return std::unexpected(r.error());
    }
    auto display = gui::buildDisplayList(*m_layout, m_paint.nodes());
    if (!display) {
        return std::unexpected(toError(display.error(), "buildDisplayList"));
    }
    m_display = std::make_unique<gui::DisplayList>(std::move(*display));
    m_stats.paintMs = msSince(t1);

    ++m_stats.frames;
    m_stats.layoutMsTotal += m_stats.layoutMs;
    m_stats.paintMsTotal += m_stats.paintMs;
    m_stats.layoutMsMax = std::max(m_stats.layoutMsMax, m_stats.layoutMs);
    m_stats.treeRevision = snapshot->revision;
    m_stats.components = m_layout->boxes().size();
    m_stats.commands = m_display->commands().size();
    return m_display.get();
}

bool UiHost::hitsUi(f32 x, f32 y) const noexcept {
    if (!m_layout || m_layout->boxes().empty()) {
        return false;
    }
    const auto hit = m_layout->hitTest({x, y});
    return hit.has_value() && *hit != m_layout->boxes().front().component;
}

} // namespace sbx::uihost
