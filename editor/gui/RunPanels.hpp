#pragma once
// 실행 · 접속자 패널 (UI-R3-09 나머지, ADR-0183) — ImGui 개발자 창 "시뮬레이션" (drawDebugPanels 의 simulationPanel) 과 "접속자"
// (drawPlayersPanel) 의 일을 작업 공간 패널로.
//
//   SimulationPanel  월드 · tick · 게임 · 개체 · TPS (따라가지 못하면 경고색) · 틱 시간, 일시정지 · 한 틱 · 속도 − + (역할이 못 쓰면
//                    꺼짐 — 판단은 서버), 격자 · 자세히 · 월드 맞춤 · 줌, 선택 · 선택 해제. 일 = 의도 Simulation (value =
//                    SimAction) — 앱이 ImGui 창과 같은 PanelActions → applyPanelActions
//   PlayersPanel     내 역할 · 접속자 줄 ("이름 #번호 (나)" | 역할) · 고른 사람의 역할 단추 5 (바꿀 수 있는 역할만 — 서버와 같은
//                    canAssignRole) · 최근 결과. 콤보 대신 단추 (팝업이 없다 — GUI-UP-03). 일 = 의도 SetRole → requestRole
//   보기  앱이 frame 마다 (apps/client GuiStatsModel — 진단 창과 같은 PanelInputs · PlayersInfo)

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "editor/gui/StudioPanels.hpp"

namespace sbx::editor::gui {

// 실행 패널의 일 (WorkspaceIntent::value) — ImGui PanelActions 의 칸
enum class SimAction : u8 { TogglePause = 1, Step, Slower, Faster, ToggleGrid, ToggleDetails, FitCamera, ClearSelection };

struct SimulationModel {
    bool available = false;      // 월드가 있다
    std::string headline;        // "ecosystem_small · tick 1234"
    std::vector<StatLine> lines; // 게임 · 개체 · TPS · 틱 시간 (진단 창과 같은 글)
    bool paused = false;
    bool canControl = false; // 일시정지 · 한 틱 · 속도 (WorldInfo::canControl)
    f32 speed = 1;
    bool grid = false;
    bool details = true;
    f32 zoom = 0;          // px/칸
    std::string selection; // 세션의 selectionStatus (빈 글 = 선택 없음)
    bool operator==(const SimulationModel&) const = default;
};

class SimulationPanel final : public uihost::IUiScreen {
public:
    explicit SimulationPanel(PanelContext& ctx) : m_ctx(ctx) {}

    void setModel(SimulationModel model) { m_model = std::move(model); }
    [[nodiscard]] const SimulationModel& model() const noexcept { return m_model; }
    [[nodiscard]] std::string pauseLabel() const;
    [[nodiscard]] std::string speedText() const;

    Expected<void> mount(uihost::UiHost& host) override;
    Expected<void> layout(uihost::UiHost& host, const gui::ComponentSnapshot& tree, std::vector<gui::LayoutNode>& out) override;
    Expected<void> paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) override;

    [[nodiscard]] gui::ComponentHandle pauseHandle() const noexcept { return m_pauseNode; }
    [[nodiscard]] gui::ComponentHandle stepHandle() const noexcept { return m_stepNode; }
    [[nodiscard]] gui::ComponentHandle slowerHandle() const noexcept { return m_slowerNode; }
    [[nodiscard]] gui::ComponentHandle fasterHandle() const noexcept { return m_fasterNode; }
    [[nodiscard]] gui::ComponentHandle gridHandle() const noexcept { return m_gridNode; }
    [[nodiscard]] gui::ComponentHandle detailsHandle() const noexcept { return m_detailsNode; }
    [[nodiscard]] gui::ComponentHandle fitHandle() const noexcept { return m_fitNode; }
    [[nodiscard]] gui::ComponentHandle clearHandle() const noexcept { return m_clearNode; }

private:
    void emit(SimAction a);
    void applyEnabled();

    PanelContext& m_ctx;
    SimulationModel m_model;
    gui::ComponentHandle m_root, m_headline, m_info, m_runRow, m_pauseNode, m_stepNode, m_speedRow, m_slowerNode, m_speedNode,
        m_fasterNode, m_note, m_viewRow, m_gridNode, m_detailsNode, m_fitRow, m_fitNode, m_zoomNode, m_selection, m_clearNode;
    std::unique_ptr<gui::Button> m_pause, m_step, m_slower, m_faster, m_grid, m_details, m_fit, m_clear;
};

struct PlayerEntry {
    u16 clientId = 0;
    std::string who; // "player #1 (나)"
    u8 role = 0;
    u8 assignable = 0; // 내가 이 사람에게 줄 수 있는 역할 (비트 r — canAssignRole, 지금 역할은 빼고)
    bool self = false;
    bool operator==(const PlayerEntry&) const = default;
};

struct PlayersModel {
    static constexpr usize kRoles = 5; // net::Role observer ~ owner
    bool available = false;            // 접속자 정보가 있다 (세션이 아는 경우)
    u8 myRole = 0;
    std::array<std::string, kRoles> roleNames; // roleLabel ("owner (주인)")
    std::vector<PlayerEntry> players;          // clientId 오름차순
    std::string message;                       // 가장 최근 역할 바꾸기 결과
    bool operator==(const PlayersModel&) const = default;
};

class PlayersPanel final : public uihost::IUiScreen {
public:
    static constexpr usize kRoles = PlayersModel::kRoles;

    explicit PlayersPanel(PanelContext& ctx) : m_ctx(ctx) {}

    void setModel(PlayersModel model);
    [[nodiscard]] const PlayersModel& model() const noexcept { return m_model; }
    [[nodiscard]] std::optional<usize> selectedRow() const;
    [[nodiscard]] std::string hint() const;

    Expected<void> mount(uihost::UiHost& host) override;
    Expected<void> layout(uihost::UiHost& host, const gui::ComponentSnapshot& tree, std::vector<gui::LayoutNode>& out) override;
    Expected<void> paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) override;

    [[nodiscard]] gui::ComponentHandle listHandle() const noexcept { return m_listNode; }
    [[nodiscard]] gui::ComponentHandle roleHandle(usize r) const noexcept { return m_roleNodes[r]; }
    [[nodiscard]] std::optional<gui::LayoutRect> rowBounds(usize row) const;

private:
    void select(std::optional<usize> row);
    void applyEnabled();
    gui::EventReply listEvent(const gui::RoutedEvent& ev);

    PanelContext& m_ctx;
    PlayersModel m_model;
    std::optional<u16> m_selected; // 고른 사람 (clientId — 줄이 바뀌어도 그 사람)
    std::vector<gui::ListItemKey> m_keys;
    uihost::UiHost* m_host = nullptr;
    gui::ComponentHandle m_root, m_me, m_listNode, m_hint, m_roleRow, m_message;
    std::array<gui::ComponentHandle, kRoles> m_roleNodes{};
    std::array<std::unique_ptr<gui::Button>, kRoles> m_roles;
    gui::ListView m_list;
    gui::LayoutRect m_listBounds;
};

} // namespace sbx::editor::gui
