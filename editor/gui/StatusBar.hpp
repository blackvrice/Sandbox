#pragma once
// 상태 표시줄 · 알림 (UI-R2-10, ADR-0189) — ImGui 셸의 상태 표시줄 (statusBar) 과 알림 창 (drawNotices) 과 같은 내용.
//
//   StatusBar     작업 공간 아래 띠 (DockWorkspace::setFooter — 도킹 밖, 늘 보인다). 왼쪽: 프로젝트 (없으면 월드) · 배치 · 선택 ·
//                 tick · 일시정지 (경고색) 또는 TPS · 배속. 오른쪽: 진행 표시 (빌드 · 테스트가 도는 동안 — 움직이는 막대, 누르면 그 패널을
//                 보이게) · 알림 단추 (있을 때 — 누르면 알림 패널을 보이게) · 모드 배지 (플레이 ·
//                 재생이면 강조색). 키 포커스를 받지 않는다 (포인터만)
//   NoticesPanel  알림 패널 (도크 탭) — 앱의 시작 알림 줄을 낱말 단위로 감싸 (글꼴 측정), 휠로 굴린다
//
// 모델은 앱이 frame 마다 (ShellInputs 와 같은 원천 — apps/client/ui/GuiStatsModel 의 makeGuiStatusModel).

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <graphics-ui/core/docking.hpp>

#include "editor/gui/StudioPanels.hpp"

namespace sbx::editor::gui {

struct StatusBarModel {
    bool inWorld = false;
    std::string place;  // 프로젝트 이름 (비면 월드 이름)
    std::string layout; // 지금 레이아웃 이름
    usize selected = 0;
    u64 tick = 0;
    bool paused = false;
    f64 tps = 0;
    f32 speed = 1.f;
    std::string mode;   // "Scene — 편집" · "Game — 테스트 플레이" … (workspaceModeName)
    bool live = false;  // 편집이 아닌 모드 (배지 강조)
    usize notices = 0;  // 알림 줄 수 (0 이면 단추 없음)
    gui::DockPaneId noticesPane = 0; // 알림 단추가 보이게 할 패널
    // 진행 (BuildService · TestService — 단계 · 퍼센트가 없어 "도는 중" 만. ImGui 툴바의 "빌드 중…" 과 같은 원천)
    bool building = false;
    usize testsRunning = 0; // 돌고 있는 테스트 프로세스 수
    gui::DockPaneId buildPane = 0, testPane = 0;
    bool operator==(const StatusBarModel&) const = default;
};

class StatusBar final : public uihost::IUiScreen {
public:
    static constexpr double kHeight = 24.0; // 논리 단위 (ImGui frame 높이 + 여백과 비슷하게)

    explicit StatusBar(PanelContext& ctx) : m_ctx(ctx) {}

    void setModel(StatusBarModel model) { m_model = std::move(model); }
    [[nodiscard]] const StatusBarModel& model() const noexcept { return m_model; }
    // 왼쪽 글 (시험 · 접근성) — "sandbox · 배치 default · 선택 2 · tick 120 · 60 TPS · ×1"
    [[nodiscard]] std::string summary() const;
    [[nodiscard]] std::string noticesLabel() const;
    // 진행 표시 글 ("빌드 중…" · "테스트 2 실행 중" · 둘 다) — 비면 표시 없음
    [[nodiscard]] std::string busyLabel() const;
    [[nodiscard]] gui::ComponentHandle busyHandle() const noexcept { return m_busyNode; }
    [[nodiscard]] gui::ComponentHandle noticesHandle() const noexcept { return m_noticesNode; }

    Expected<void> mount(uihost::UiHost& host) override;
    Expected<void> layout(uihost::UiHost& host, const gui::ComponentSnapshot& tree, std::vector<gui::LayoutNode>& out) override;
    Expected<void> paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) override;

private:
    PanelContext& m_ctx;
    StatusBarModel m_model;
    gui::ComponentHandle m_root, m_text, m_busyNode, m_noticesNode, m_mode;
    std::unique_ptr<gui::Button> m_busy, m_notices;
};

class NoticesPanel final : public uihost::IUiScreen {
public:
    explicit NoticesPanel(PanelContext& ctx) : m_ctx(ctx) {}

    void setNotices(std::vector<std::string> notices);
    [[nodiscard]] const std::vector<std::string>& notices() const noexcept { return m_notices; }
    // 마지막 배치의 감싼 줄 (알림마다 첫 줄은 "• ") — 시험
    [[nodiscard]] const std::vector<std::string>& wrapped() const noexcept { return m_wrapped; }
    [[nodiscard]] double scroll() const noexcept { return m_scroll; }
    [[nodiscard]] gui::ComponentHandle listHandle() const noexcept { return m_list; }

    // 낱말 (공백) 단위로 width 안에 감싼다 — 한 낱말이 넓으면 글자 (UTF-8 코드 포인트) 단위로 자른다
    [[nodiscard]] static std::vector<std::string> wrap(const uihost::ITextMeasure& text, std::string_view line, double em,
                                                       double width);

    Expected<void> mount(uihost::UiHost& host) override;
    Expected<void> layout(uihost::UiHost& host, const gui::ComponentSnapshot& tree, std::vector<gui::LayoutNode>& out) override;
    Expected<void> paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) override;

private:
    PanelContext& m_ctx;
    std::vector<std::string> m_notices;
    std::vector<std::string> m_wrapped;
    double m_wrapWidth = -1; // 감싼 폭 (바뀌면 다시)
    double m_scroll = 0;
    double m_viewH = 0;
    gui::ComponentHandle m_root, m_list;
};

} // namespace sbx::editor::gui
