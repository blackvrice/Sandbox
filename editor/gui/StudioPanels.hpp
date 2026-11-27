#pragma once
// Studio 작업 공간의 첫 GraphicsUI 패널 (UI-R2-05 · R3-06 일부) — Dock 작업 공간 (DockWorkspace) 의 탭 하나씩.
//
//   ConsolePanel   로그 최근 줄 (foundation log::recentLines — ImGui "콘솔" 과 같은 원천) · 거르기 · 따라가기 · 가상 목록 · 고른 줄 복사
//   ShortcutPanel  명령 표 (CommandService — 팔레트 · 단축키 표와 같은 원천) · 검색 · Enter = 그 명령 실행 (의도)
//   InfoPanel      판 · 테마 · 배율 · 조작 안내 · 홈으로 · 배치 초기화 (의도)
//
// 패널은 고른 일을 WorkspaceIntent 로 돌려준다 — 일은 Application 이 기존 명령 · 처리로 한다.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <graphics-ui/core/controls.hpp>
#include <graphics-ui/core/scroll.hpp>

#include "editor/gui/TextField.hpp"
#include "editor/services/CommandService.hpp"
#include "editor/services/StudioTokens.hpp"
#include "uihost/UiHost.hpp"

namespace sbx::editor::gui {

namespace gui = graphics_ui::core;

struct WorkspaceIntent {
    enum class Kind : u8 { Home, ResetLayout, RunCommand };
    Kind kind = Kind::Home;
    std::string command; // RunCommand
};

// 패널들이 같이 보는 것 — 작업 공간이 소유하고 패널에 빌려준다
struct PanelContext {
    services::StudioTheme theme = services::StudioTheme::Dark;
    bool compact = false;
    f32 scale = 1.f;
    std::optional<WorkspaceIntent> intent; // 이번 frame 에 고른 일 (작업 공간이 꺼낸다)
};

class ConsolePanel final : public uihost::IUiScreen {
public:
    explicit ConsolePanel(PanelContext& ctx) : m_ctx(ctx), m_filter("거르기 — 글자 (대소문자 무시)", 13.0) {}

    Expected<void> mount(uihost::UiHost& host) override;
    Expected<void> layout(uihost::UiHost& host, const gui::ComponentSnapshot& tree, std::vector<gui::LayoutNode>& out) override;
    Expected<void> paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) override;

    [[nodiscard]] usize listedCount() const noexcept { return m_list.itemCount(); }
    [[nodiscard]] bool following() const noexcept { return m_follow; }
    [[nodiscard]] std::optional<std::string> selectedLine() const;
    [[nodiscard]] double listOffset() const noexcept { return m_list.scroll().offset().y; }
    [[nodiscard]] gui::ComponentHandle filterHandle() const noexcept { return m_filter.handle(); }
    [[nodiscard]] gui::ComponentHandle followHandle() const noexcept { return m_followNode; }
    [[nodiscard]] gui::ComponentHandle listHandle() const noexcept { return m_listNode; }

private:
    void refresh(bool force);
    gui::EventReply listEvent(const gui::RoutedEvent& ev);

    PanelContext& m_ctx;
    TextField m_filter;
    uihost::UiHost* m_host = nullptr;
    gui::ComponentHandle m_root, m_bar, m_followNode, m_listNode;
    std::unique_ptr<gui::Button> m_followButton;
    gui::ListView m_list;
    gui::LayoutRect m_listBounds;
    std::vector<std::string> m_lines; // 로그 최근 줄 (가장 오래된 것 먼저)
    std::uint64_t m_total = 0;        // log::recentTotal — 바뀌면 다시 읽는다
    std::uint64_t m_first = 0;        // m_lines[0] 의 순번 (키 = 순번 — 오래된 줄이 밀려 나가도 같은 줄은 같은 키)
    bool m_follow = true;
    std::string m_applied; // 마지막으로 거른 글자
};

class ShortcutPanel final : public uihost::IUiScreen {
public:
    ShortcutPanel(PanelContext& ctx, const services::CommandService& commands)
        : m_ctx(ctx), m_commands(commands), m_search("명령 · 단축키 검색", 13.0) {}

    Expected<void> mount(uihost::UiHost& host) override;
    Expected<void> layout(uihost::UiHost& host, const gui::ComponentSnapshot& tree, std::vector<gui::LayoutNode>& out) override;
    Expected<void> paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) override;

    [[nodiscard]] usize listedCount() const noexcept { return m_list.itemCount(); }
    [[nodiscard]] gui::ComponentHandle searchHandle() const noexcept { return m_search.handle(); }
    [[nodiscard]] gui::ComponentHandle listHandle() const noexcept { return m_listNode; }

private:
    void refilter();
    void runSelected();
    gui::EventReply listEvent(const gui::RoutedEvent& ev);

    PanelContext& m_ctx;
    const services::CommandService& m_commands;
    TextField m_search;
    uihost::UiHost* m_host = nullptr;
    gui::ComponentHandle m_root, m_listNode;
    gui::ListView m_list;
    gui::LayoutRect m_listBounds;
    std::vector<const services::CommandSpec*> m_rows; // 검색 결과 — 키 = 차례
};

class InfoPanel final : public uihost::IUiScreen {
public:
    InfoPanel(PanelContext& ctx, std::string engineVersion) : m_ctx(ctx), m_version(std::move(engineVersion)) {}

    Expected<void> mount(uihost::UiHost& host) override;
    Expected<void> layout(uihost::UiHost& host, const gui::ComponentSnapshot& tree, std::vector<gui::LayoutNode>& out) override;
    Expected<void> paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) override;

    [[nodiscard]] gui::ComponentHandle homeHandle() const noexcept { return m_homeNode; }
    [[nodiscard]] gui::ComponentHandle resetHandle() const noexcept { return m_resetNode; }

private:
    [[nodiscard]] std::vector<std::string> lines() const;

    PanelContext& m_ctx;
    std::string m_version;
    gui::ComponentHandle m_root, m_text, m_bar, m_homeNode, m_resetNode;
    std::unique_ptr<gui::Button> m_home, m_reset;
};

} // namespace sbx::editor::gui
