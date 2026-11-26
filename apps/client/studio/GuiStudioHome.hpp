#pragma once
// Studio 홈의 GraphicsUI 화면 (UI-R1-05, ADR-0174) — `--ui graphicsui` 일 때 ImGui 홈 (StudioHome) 대신 그린다.
// 같은 데이터 (ProjectService 의 최근 목록) · 같은 행동 (StudioHomeAction → Application 의 같은 처리) — 로직을 옮기지 않는다.
//
// 범위 (UI-R1 최소): 제목 · 판 · 최근 프로젝트 (열기) · "기존 UI 로" · 종료. 새 프로젝트 · 폴더 열기 · 검색 · 예제 · 복구는
// 글자 입력 (TextInput · IME 연결 — UI-R2 · R5) 이 없어 기존 UI 로 넘긴다 (Switch). 최근 목록은 앞 kMaxRows 개만 (가상 목록은 UI-R3).

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <graphics-ui/core/controls.hpp>

#include "uihost/UiHost.hpp"

namespace sbx::client {

namespace gui = graphics_ui::core;

struct GuiHomeProject {
    std::string name;
    std::string detail; // gameId · 경로
    std::filesystem::path dir;
};

struct GuiHomeModel {
    std::string version;
    std::vector<GuiHomeProject> recent;
};

struct GuiHomeIntent {
    enum class Kind : u8 { OpenRecent, SwitchToClassic, Quit };
    Kind kind = Kind::Quit;
    std::filesystem::path dir; // OpenRecent
};

class GuiStudioHome final : public uihost::IUiScreen {
public:
    static constexpr usize kMaxRows = 8;
    static constexpr gui::FontId kFont = 1; // 글꼴 하나 (렌더 어댑터가 ID → 글꼴)

    // 모델이 바뀌면 (최근 목록) 다시 마운트해야 한다 — Application 이 setScreen 을 다시 부른다
    void setModel(GuiHomeModel model) { m_model = std::move(model); }
    [[nodiscard]] const GuiHomeModel& model() const noexcept { return m_model; }
    // 이번 프레임에 눌린 것 (한 번 읽으면 비운다)
    [[nodiscard]] std::optional<GuiHomeIntent> takeIntent();

    Expected<void> mount(uihost::UiHost& host) override;
    Expected<void> layout(uihost::UiHost& host, const gui::ComponentSnapshot& tree,
                          std::vector<gui::LayoutNode>& out) override;
    Expected<void> paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) override;

    // 시험 · 진단: 컴포넌트 차례 (게시 순서) — 0 루트, 1 제목, 2 판, 3 목록 제목, 4 목록, 그 뒤 행 · 빈 글 · 단추 줄 · 단추 둘 · 알림
    [[nodiscard]] usize rowCount() const noexcept { return m_rows; }

private:
    enum class Role : u8 { Root, Title, Version, Heading, List, Row, Empty, Bar, SwitchButton, QuitButton, Note };
    struct Node {
        Role role = Role::Root;
        usize row = 0; // Row 만
        gui::ComponentHandle handle;
        std::unique_ptr<gui::Button> button; // Row · 단추
    };

    [[nodiscard]] std::string textOf(const Node& n) const;
    [[nodiscard]] double emOf(Role role) const noexcept;

    GuiHomeModel m_model;
    std::vector<Node> m_nodes;
    usize m_rows = 0;
    std::optional<GuiHomeIntent> m_intent;
};

} // namespace sbx::client
