#pragma once
// "게임" 패널 (ADR-0186) — ImGui "게임" 창 (drawGamePanel) 과 "행동" 창 (drawActionPanel) 의 일을 한 패널에 (ImGui 셸도 보기 메뉴의 "게임"
// 하나로 둘을 켠다).
//
//   게임  모드 이름 · 모드 정보 (id · 참가 수 · 자동 재시작) · 상태 한 줄 (Lobby · 카운트다운 · 경과 · 결과) · 점수 · 자동 재시작 안내 ·
//         시작 (Lobby 에서만) · 중단 (Starting · Running) · 재시작 — admin 아래면 꺼짐 (판단은 서버) · 결과 줄
//   행동  PlayerAction 목록 ("[Q] 이름 (반경 · 개수)") — 누르면 쥐고, 다시 누르면 놓는다 (그동안 장면을 누르면 그 자리에 — 앱의 기존 처리).
//         역할 player 아래면 고를 수 없다
//   일    의도 GameCommand (value = client::GameCommand) · ArmAction (command = id, 빈 글 = 놓기)
//   보기  앱이 frame 마다 (세션의 GameInfo · ActionsInfo — 표시 이름 바꾸기 (localizeNames) 뒤, 쥔 행동 = ActionPanelState)

#include <optional>
#include <string>
#include <vector>

#include "editor/gui/StudioPanels.hpp"

namespace sbx::editor::gui {

struct ActionEntry {
    std::string id;
    std::string text; // "[Q] 먹기  (반경 1.5 · 1개)"
    bool operator==(const ActionEntry&) const = default;
};

struct GameModel {
    static constexpr int kStart = 1, kAbort = 2, kRestart = 3; // client::GameCommand (앱이 static_assert)

    bool available = false; // 세션에 게임이 있다
    std::string modeName;
    std::string modeInfo;    // "eco.survival · 참가 1 ~ 8 · 자동 재시작"
    std::string stateLine;   // gameStateLine
    std::string scoreLine;   // "빨강 3 · 파랑 5"
    std::string restartLine; // "0:05 뒤 자동 재시작" (끝났고 자동 재시작일 때)
    u8 state = 0;            // 0 Lobby · 1 Starting · 2 Running · 3 Finished
    bool canControl = false;
    std::string message;
    bool actionsAvailable = false; // 세션에 PlayerAction 이 있다
    bool canAct = false;           // 역할 player 부터
    bool hasKeys = false;          // 팩의 게임 키가 있다 (안내 글)
    std::vector<ActionEntry> actions;
    std::string armed; // 쥐고 있는 행동 id
    std::string actionMessage;
    bool operator==(const GameModel&) const = default;
};

class GamePanel final : public uihost::IUiScreen {
public:
    explicit GamePanel(PanelContext& ctx) : m_ctx(ctx) {}

    void setModel(GameModel model);
    [[nodiscard]] const GameModel& model() const noexcept { return m_model; }
    [[nodiscard]] std::string actionHint() const;

    Expected<void> mount(uihost::UiHost& host) override;
    Expected<void> layout(uihost::UiHost& host, const gui::ComponentSnapshot& tree, std::vector<gui::LayoutNode>& out) override;
    Expected<void> paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) override;

    [[nodiscard]] gui::ComponentHandle startHandle() const noexcept { return m_startNode; }
    [[nodiscard]] gui::ComponentHandle abortHandle() const noexcept { return m_abortNode; }
    [[nodiscard]] gui::ComponentHandle restartHandle() const noexcept { return m_restartNode; }
    [[nodiscard]] gui::ComponentHandle listHandle() const noexcept { return m_listNode; }
    [[nodiscard]] std::optional<gui::LayoutRect> rowBounds(usize row) const;

private:
    void command(int c);
    void toggle(usize row);
    void applyEnabled();
    gui::EventReply listEvent(const gui::RoutedEvent& ev);

    PanelContext& m_ctx;
    GameModel m_model;
    std::vector<gui::ListItemKey> m_keys;
    std::optional<usize> m_cursor; // 키보드 줄 (↑↓ — Enter · Space 로 쥐고 놓기)
    uihost::UiHost* m_host = nullptr;
    gui::ComponentHandle m_root, m_info, m_bar, m_startNode, m_abortNode, m_restartNode, m_note, m_actionsTitle, m_listNode, m_hint;
    std::unique_ptr<gui::Button> m_start, m_abort, m_restart;
    gui::ListView m_list;
    gui::LayoutRect m_listBounds;
};

} // namespace sbx::editor::gui
