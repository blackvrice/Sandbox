#include "apps/client/studio/GuiStudioHome.hpp"

#include <algorithm>
#include <format>

namespace sbx::client {

namespace {

// 색 — Studio 어두운 테마 (editor/ui/StudioTheme 의 Comfortable/Dark 와 같은 계열 · 토큰 공유는 UI-R2 의 GCSS 테마)
constexpr gui::RenderColor kBackground{24, 26, 30, 255};
constexpr gui::RenderColor kText{232, 234, 238, 255};
constexpr gui::RenderColor kMuted{150, 156, 168, 255};
constexpr gui::RenderColor kRow{36, 39, 46, 255};
constexpr gui::RenderColor kRowHover{48, 53, 63, 255};
constexpr gui::RenderColor kFocus{86, 140, 255, 255};
constexpr gui::RenderColor kAccent{58, 110, 220, 255};
constexpr gui::RenderColor kAccentHover{78, 130, 240, 255};
constexpr gui::RenderColor kNeutral{60, 64, 74, 255};
constexpr gui::RenderColor kNeutralHover{76, 81, 93, 255};

constexpr double kRowHeight = 48.0;

// GraphicsUI 그리기 명령은 0 이상 크기만 받는다 — 좁은 화면에서 안쪽 여백을 빼면 음수가 된다
double fit(double v) {
    return v > 0.0 ? v : 0.0;
}
constexpr double kPad = 12.0;

gui::LayoutNode::Intrinsic textIntrinsic(const uihost::ITextMeasure& text, std::string s, double em) {
    return [&text, s = std::move(s), em](const gui::LayoutConstraints&) -> gui::Result<gui::LayoutSize> {
        return text.measure(s, em);
    };
}

} // namespace

std::optional<GuiHomeIntent> GuiStudioHome::takeIntent() {
    auto out = m_intent;
    m_intent.reset();
    return out;
}

std::string GuiStudioHome::textOf(const Node& n) const {
    switch (n.role) {
    case Role::Title:
        return "Sandbox Studio";
    case Role::Version:
        return m_model.version.empty() ? std::string("GraphicsUI 미리보기")
                                       : std::format("엔진 {} · GraphicsUI 미리보기", m_model.version);
    case Role::Heading:
        return m_model.recent.size() > kMaxRows
                   ? std::format("최근 프로젝트 (앞 {} / {})", kMaxRows, m_model.recent.size())
                   : std::string("최근 프로젝트");
    case Role::Row:
        return m_model.recent[n.row].name;
    case Role::Empty:
        return "최근 프로젝트가 없다 — 기존 UI 에서 새로 만들거나 연다";
    case Role::SwitchButton:
        return "기존 UI 로 (새 프로젝트 · 열기)";
    case Role::QuitButton:
        return "종료";
    case Role::Note:
        return "UI-R1 — GraphicsUI Core 의 배치 · 입력 · 그리기 명령. 그리기는 과도기 경로 (ImGui 렌더러)";
    default:
        return {};
    }
}

double GuiStudioHome::emOf(Role role) const noexcept {
    switch (role) {
    case Role::Title:
        return 28.0;
    case Role::Version:
    case Role::Note:
        return 13.0;
    case Role::Heading:
        return 15.0;
    default:
        return 16.0;
    }
}

Expected<void> GuiStudioHome::mount(uihost::UiHost& host) {
    m_nodes.clear();
    m_intent.reset();
    m_rows = std::min(m_model.recent.size(), kMaxRows);

    // 부모 우선 게시 목록 — 타입 = 역할, 키 = 같은 부모 안의 차례 (행은 행 번호 — 목록이 바뀌어도 같은 행은 같은 키)
    std::vector<gui::ComponentSpec> specs;
    const auto push = [&](std::optional<usize> parent, Role role, gui::ComponentKey key, usize row = 0) {
        specs.push_back({parent, static_cast<gui::ComponentType>(role) + 1, key, true});
        Node n;
        n.role = role;
        n.row = row;
        m_nodes.push_back(std::move(n));
        return specs.size() - 1;
    };
    const usize root = push(std::nullopt, Role::Root, 0);
    push(root, Role::Title, 1);
    push(root, Role::Version, 2);
    push(root, Role::Heading, 3);
    const usize list = push(root, Role::List, 4);
    for (usize i = 0; i < m_rows; ++i) {
        push(list, Role::Row, 100 + i, i);
    }
    if (m_rows == 0) {
        push(list, Role::Empty, 1);
    }
    const usize bar = push(root, Role::Bar, 5);
    push(bar, Role::SwitchButton, 1);
    push(bar, Role::QuitButton, 2);
    push(root, Role::Note, 6);

    if (auto r = host.tree().commit(specs); !r) {
        return std::unexpected(uihost::toError(r.error(), "홈 트리 게시"));
    }
    auto snapshot = host.tree().snapshot();
    if (!snapshot || snapshot->components.size() != m_nodes.size()) {
        return makeError(ErrorCode::Unknown, "GraphicsUI 홈: 게시한 트리를 읽지 못했다");
    }
    std::optional<gui::ComponentHandle> firstFocus;
    for (usize i = 0; i < m_nodes.size(); ++i) {
        Node& n = m_nodes[i];
        n.handle = snapshot->components[i].handle; // snapshot 은 게시 순서 그대로
        if (n.role != Role::Row && n.role != Role::SwitchButton && n.role != Role::QuitButton) {
            continue;
        }
        n.button = std::make_unique<gui::Button>(host.input(), n.handle);
        if (auto r = n.button->setEnabled(true); !r) {
            return std::unexpected(uihost::toError(r.error(), "Button::setEnabled"));
        }
        const auto handler = [this, i](const gui::RoutedEvent& ev) -> gui::EventReply {
            Node& self = m_nodes[i];
            const auto r = self.button->handle(ev);
            if (!r) {
                return {};
            }
            if (r->activated) {
                GuiHomeIntent intent;
                if (self.role == Role::Row) {
                    intent.kind = GuiHomeIntent::Kind::OpenRecent;
                    intent.dir = m_model.recent[self.row].dir;
                } else if (self.role == Role::SwitchButton) {
                    intent.kind = GuiHomeIntent::Kind::SwitchToClassic;
                } else {
                    intent.kind = GuiHomeIntent::Kind::Quit;
                }
                m_intent = std::move(intent);
            }
            return r->reply;
        };
        if (auto r = host.input().setHandler(n.handle, handler); !r) {
            return std::unexpected(uihost::toError(r.error(), "InputDispatcher::setHandler"));
        }
        if (!firstFocus) {
            firstFocus = n.handle;
        }
    }
    if (firstFocus) {
        if (auto r = host.input().setFocus(firstFocus); !r) {
            return std::unexpected(uihost::toError(r.error(), "InputDispatcher::setFocus"));
        }
    }
    return {};
}

Expected<void> GuiStudioHome::layout(uihost::UiHost& host, const gui::ComponentSnapshot& tree,
                                     std::vector<gui::LayoutNode>& out) {
    if (tree.components.size() != m_nodes.size()) {
        return makeError(ErrorCode::Unknown, "GraphicsUI 홈: 트리와 노드 수가 다르다");
    }
    const gui::LayoutSize vp = host.viewport();
    // 목록 폭 = 화면 폭 − 바깥 여백 (최대 720 — 넓은 화면에서 읽기 쉬운 길이)
    const double listWidth = std::clamp(vp.width - 64.0, 0.0, 720.0);
    for (const Node& n : m_nodes) {
        gui::LayoutNode node;
        node.component = n.handle;
        gui::LayoutStyle& st = node.style;
        switch (n.role) {
        case Role::Root:
            st.kind = gui::LayoutKind::column;
            st.padding = {32, 32, 32, 32};
            st.gap = 12;
            break;
        case Role::List:
            st.kind = gui::LayoutKind::column;
            st.gap = 4;
            break;
        case Role::Bar:
            st.kind = gui::LayoutKind::row;
            st.gap = 12;
            break;
        case Role::Row:
            st.preferred_width = listWidth;
            st.preferred_height = kRowHeight;
            break;
        case Role::SwitchButton:
        case Role::QuitButton:
            st.padding = {16, 8, 16, 8};
            node.intrinsic = textIntrinsic(host.text(), textOf(n), emOf(n.role));
            break;
        default:
            node.intrinsic = textIntrinsic(host.text(), textOf(n), emOf(n.role));
            break;
        }
        out.push_back(std::move(node));
    }
    return {};
}

Expected<void> GuiStudioHome::paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) {
    const auto hit = host.pointerHit();
    for (const gui::LayoutBox& box : frame.boxes()) {
        const auto it = std::find_if(m_nodes.begin(), m_nodes.end(),
                                     [&](const Node& n) { return n.handle == box.component; });
        if (it == m_nodes.end()) {
            continue;
        }
        const Node& n = *it;
        const double w = box.bounds.width;
        const double h = box.bounds.height;
        std::vector<gui::DrawCommand> draws;
        gui::ControlVisualState vs;
        if (n.button) {
            if (auto v = n.button->visualState(hit)) {
                vs = *v;
            }
        }
        switch (n.role) {
        case Role::Root:
            draws.push_back(gui::FillRect{{0, 0, w, h}, kBackground});
            break;
        case Role::Row: {
            draws.push_back(gui::FillRect{{0, 0, w, h}, vs.hovered || vs.pressed ? kRowHover : kRow});
            if (vs.focused) {
                draws.push_back(gui::FillRect{{0, 0, 3, h}, kFocus}); // 키보드 포커스 표시 (왼쪽 띠)
            }
            const GuiHomeProject& p = m_model.recent[n.row];
            draws.push_back(gui::DrawText{{kPad, 5, fit(w - 2 * kPad), 22}, kFont, 16.0, p.name, kText});
            draws.push_back(gui::DrawText{{kPad, 27, fit(w - 2 * kPad), 18}, kFont, 12.0, p.detail, kMuted});
            break;
        }
        case Role::SwitchButton:
        case Role::QuitButton: {
            const bool accent = n.role == Role::SwitchButton;
            const bool hot = vs.hovered || vs.pressed;
            draws.push_back(gui::FillRect{{0, 0, w, h}, accent ? (hot ? kAccentHover : kAccent)
                                                                : (hot ? kNeutralHover : kNeutral)});
            if (vs.focused) {
                draws.push_back(gui::FillRect{{0, fit(h - 2), w, 2}, kFocus});
            }
            draws.push_back(gui::DrawText{{16, 8, fit(w - 32), fit(h - 16)}, kFont, emOf(n.role), textOf(n), kText});
            break;
        }
        case Role::List:
        case Role::Bar:
            break;
        default:
            draws.push_back(gui::DrawText{{0, 0, w, h}, kFont, emOf(n.role), textOf(n),
                                          n.role == Role::Title ? kText : kMuted});
            break;
        }
        if (!draws.empty()) {
            out.add(n.handle, std::move(draws), n.role == Role::Row);
        }
    }
    return {};
}

} // namespace sbx::client
