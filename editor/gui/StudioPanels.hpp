#pragma once
// Studio 작업 공간의 첫 GraphicsUI 패널 (UI-R2-05 · R3-06 일부) — Dock 작업 공간 (DockWorkspace) 의 탭 하나씩.
//
//   ConsolePanel   로그 최근 줄 (foundation log::recentLines — ImGui "콘솔" 과 같은 원천) · 거르기 · 따라가기 · 가상 목록 · 고른 줄 복사
//   ShortcutPanel  명령 표 (CommandService — 팔레트 · 단축키 표와 같은 원천) · 검색 · Enter = 그 명령 실행 (의도)
//   InfoPanel      판 · 테마 · 배율 · 조작 안내 · 홈으로 · 배치 초기화 (의도)
//   BuildPanel     빌드 창 (UI-R3-07) — 프로필 ‹ › · 검사 · 빌드 (명령 studio.check · studio.build) · 마지막 보고 (BuildReport 그대로)
//   TestPanel      테스트 창 (UI-R3-08) — 클라이언트 · 지연 · 흔들림 · 손실 (− 값 +) · 시작 · 끝내기 (studio.test-*) · 프로세스 목록
//   ScenePanel     장면 (ADR-0179) — 프로젝트 안에서는 아무것도 그리지 않는다 (통과 — 먼저 그린 월드가 보인다), 홈에서는 안내
//   StatsPanel     통계 창 (UI-R3-09) — 프레임 시간 막대 · CPU · GPU · 그리기 · 디바이스 · 네트워크 줄 (진단 창과 같은 원천)
//
// 패널은 고른 일을 WorkspaceIntent 로 돌려준다 — 일은 Application 이 기존 명령 · 처리로 한다.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <graphics-ui/core/controls.hpp>
#include <graphics-ui/core/scroll.hpp>

#include "core/serialization/JsonVisitor.hpp"
#include "editor/gui/TextField.hpp"
#include "editor/services/BuildService.hpp"
#include "editor/services/TestService.hpp"
#include "editor/services/CommandService.hpp"
#include "editor/services/StudioTokens.hpp"
#include "uihost/UiHost.hpp"

namespace sbx::editor::gui {

namespace gui = graphics_ui::core;

// 테스트 세션의 값 (ImGui TestPanelState 와 같은 뜻 — 클라이언트 1 ~ 4, 지연 · 흔들림 0 ~ 2000 ms, 손실 0 ~ 50 %)
struct TestPlanValues {
    int clients = 2;
    double latencyMs = 0;
    double jitterMs = 0;
    double lossPercent = 0;
    bool operator==(const TestPlanValues&) const = default;
};

struct WorkspaceIntent {
    enum class Kind : u8 {
        Home, ResetLayout, RunCommand, SelectProfile, TestPlan,
        // Hierarchy (UI-R3-02) — 앱이 Editor · IEditorHost 로
        SelectEntities, FocusSelection, DeleteSelection, GroupSelection, UngroupSelection, ToggleGroup,
        // 인스펙터 (UI-R3-03) — 앱이 Editor::setField · addComponent · removeComponent · revertToPrefab
        SetField, AddComponent, RemoveComponent, RevertToPrefab,
    };
    Kind kind = Kind::Home;
    std::string command; // RunCommand · GroupSelection (그룹 이름) · ToggleGroup (그룹 이름)
    int value = 0;       // SelectProfile (0 = 프로필 없이, 1 ~ = profiles[value - 1] — ImGui BuildPanelState 와 같은 뜻)
    TestPlanValues test; // TestPlan
    std::vector<u32> entities; // SelectEntities — 새 선택 (NetEntityId, 오름차순 — editor/gui 는 core 를 직접 쓰지 않는다)
    u64 component = 0;         // 인스펙터 — 컴포넌트 stableId (SetField 의 필드 이름은 command)
    ecs::Json json;            // SetField — 검사한 값 (DataForm::setFormField 를 통과한 것)

    [[nodiscard]] static WorkspaceIntent of(Kind kind, std::string command = {}, int value = 0) {
        WorkspaceIntent i;
        i.kind = kind;
        i.command = std::move(command);
        i.value = value;
        return i;
    }
};

// 패널들이 같이 보는 것 — 작업 공간이 소유하고 패널에 빌려준다
struct PanelContext {
    services::StudioTheme theme = services::StudioTheme::Dark;
    bool compact = false;
    f32 scale = 1.f;
    bool inWorld = false; // 프로젝트 안 (장면 패널 = 통과, 정보의 "기존 UI 로" — ADR-0179)
    std::optional<WorkspaceIntent> intent; // 이번 frame 에 고른 일 (작업 공간이 꺼낸다)
};

class ConsolePanel final : public uihost::IUiScreen {
public:
    explicit ConsolePanel(PanelContext& ctx) : m_ctx(ctx), m_filter("거르기 — 글자 (대소문자 무시)", 13.0) {}

    Expected<void> mount(uihost::UiHost& host) override;
    Expected<void> layout(uihost::UiHost& host, const gui::ComponentSnapshot& tree, std::vector<gui::LayoutNode>& out) override;
    Expected<void> paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) override;

    // 줄의 수준 (0 그 밖 · 1 경고 · 2 오류 — ImGui 콘솔의 consoleLineLevel 과 같은 규칙)
    [[nodiscard]] static int lineLevel(std::string_view line) noexcept;

    [[nodiscard]] usize listedCount() const noexcept { return m_list.itemCount(); }
    [[nodiscard]] bool following() const noexcept { return m_follow; }
    [[nodiscard]] int level() const noexcept { return m_level; } // 0 전부 · 1 경고 이상 · 2 오류만
    [[nodiscard]] std::optional<std::string> selectedLine() const;
    [[nodiscard]] double listOffset() const noexcept { return m_list.scroll().offset().y; }
    [[nodiscard]] gui::ComponentHandle filterHandle() const noexcept { return m_filter.handle(); }
    [[nodiscard]] gui::ComponentHandle levelHandle() const noexcept { return m_levelNode; }
    [[nodiscard]] gui::ComponentHandle clearHandle() const noexcept { return m_clearNode; }
    [[nodiscard]] gui::ComponentHandle copyHandle() const noexcept { return m_copyNode; }
    [[nodiscard]] gui::ComponentHandle followHandle() const noexcept { return m_followNode; }
    [[nodiscard]] gui::ComponentHandle listHandle() const noexcept { return m_listNode; }

private:
    void refresh(bool force);
    [[nodiscard]] std::optional<std::string> lineOf(gui::ListItemKey key) const;
    [[nodiscard]] std::string levelLabel() const;
    void writeClipboard(const std::string& text) const;
    void copyShown() const;
    gui::EventReply listEvent(const gui::RoutedEvent& ev);

    PanelContext& m_ctx;
    TextField m_filter;
    uihost::UiHost* m_host = nullptr;
    gui::ComponentHandle m_root, m_bar, m_levelNode, m_clearNode, m_copyNode, m_followNode, m_listNode;
    std::unique_ptr<gui::Button> m_levelButton, m_clearButton, m_copyButton, m_followButton;
    gui::ListView m_list;
    gui::LayoutRect m_listBounds;
    std::vector<std::string> m_lines; // 로그 최근 줄 (가장 오래된 것 먼저)
    std::uint64_t m_total = 0;        // log::recentTotal — 바뀌면 다시 읽는다
    std::uint64_t m_first = 0;        // m_lines[0] 의 순번 (키 = 순번 — 오래된 줄이 밀려 나가도 같은 줄은 같은 키)
    std::uint64_t m_clearedAt = 0;    // "지우기" 때의 recentTotal — 그 앞 순번은 숨긴다
    int m_level = 0;
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

class ScenePanel final : public uihost::IUiScreen {
public:
    explicit ScenePanel(PanelContext& ctx) : m_ctx(ctx) {}

    Expected<void> mount(uihost::UiHost& host) override;
    Expected<void> layout(uihost::UiHost& host, const gui::ComponentSnapshot& tree, std::vector<gui::LayoutNode>& out) override;
    Expected<void> paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) override;

private:
    PanelContext& m_ctx;
    gui::ComponentHandle m_root;
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

// 빌드 창이 보이는 것 — Application 이 frame 마다 채운다 (BuildService · 프로젝트 · export 프로필 — ImGui "빌드" 창과 같은 원천)
struct BuildPanelModel {
    bool available = false; // 프로젝트가 열렸고 빌드 서비스가 있다 (아니면 단추를 끈다)
    std::string project;    // 표시용 경로
    std::vector<std::string> profiles;
    int profileIndex = 0; // 0 = 프로필 없이
    bool running = false;
    std::optional<services::BuildReport> last;
    bool operator==(const BuildPanelModel&) const = default;
};

class BuildPanel final : public uihost::IUiScreen {
public:
    // 보고 줄의 색 종류
    enum class Tone : u8 { Text, Muted, Ok, Error, Warning };
    struct Line {
        std::string text;
        Tone tone = Tone::Text;
    };

    explicit BuildPanel(PanelContext& ctx) : m_ctx(ctx) {}

    // 바뀌었을 때만 보고 줄을 다시 만든다 (목록 스크롤 · 고르기는 같은 보고 동안 남는다)
    void setModel(BuildPanelModel model);
    [[nodiscard]] const BuildPanelModel& model() const noexcept { return m_model; }
    // 보고 → 줄 (ImGui "빌드" 창과 같은 내용 · 차례: 요약 · 패키지 · 에셋 그룹 · 오류 · 경고)
    [[nodiscard]] static std::vector<Line> reportLines(const services::BuildReport& report);

    Expected<void> mount(uihost::UiHost& host) override;
    Expected<void> layout(uihost::UiHost& host, const gui::ComponentSnapshot& tree, std::vector<gui::LayoutNode>& out) override;
    Expected<void> paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) override;

    [[nodiscard]] const std::vector<Line>& lines() const noexcept { return m_lines; }
    [[nodiscard]] std::string profileLabel() const;
    [[nodiscard]] gui::ComponentHandle prevHandle() const noexcept { return m_prevNode; }
    [[nodiscard]] gui::ComponentHandle nextHandle() const noexcept { return m_nextNode; }
    [[nodiscard]] gui::ComponentHandle checkHandle() const noexcept { return m_checkNode; }
    [[nodiscard]] gui::ComponentHandle buildHandle() const noexcept { return m_buildNode; }
    [[nodiscard]] gui::ComponentHandle listHandle() const noexcept { return m_listNode; }

private:
    void stepProfile(int step);
    void applyEnabled();

    PanelContext& m_ctx;
    BuildPanelModel m_model;
    std::vector<Line> m_lines;
    uihost::UiHost* m_host = nullptr;
    gui::ComponentHandle m_root, m_title, m_bar, m_prevNode, m_profileNode, m_nextNode, m_checkNode, m_buildNode, m_status,
        m_listNode;
    std::unique_ptr<gui::Button> m_prev, m_next, m_check, m_build;
    gui::ListView m_list;
};


// 테스트 창이 보이는 것 · 고른 값 (UI-R3-08) — ImGui TestPanelState 와 같은 뜻 · 같은 범위 (applyTestPanel 이 다시 자른다)
struct TestPanelModel {
    bool available = false; // 프로젝트가 열렸고 테스트 서비스가 있다
    bool running = false;
    TestPlanValues plan;
    std::vector<services::TestProcess> processes;
    bool operator==(const TestPanelModel&) const = default;
};

class TestPanel final : public uihost::IUiScreen {
public:
    // 값 줄 — 클라이언트 · 지연 · 흔들림 · 손실
    enum class Field : u8 { Clients, Latency, Jitter, Loss };
    static constexpr usize kFields = 4;

    explicit TestPanel(PanelContext& ctx) : m_ctx(ctx) {}

    void setModel(TestPanelModel model);
    [[nodiscard]] const TestPanelModel& model() const noexcept { return m_model; }
    // 프로세스 한 줄 ("서버 — 도는 중", "클라이언트 1 — 끝남 (종료 코드 3)" — ImGui 창과 같은 글)
    [[nodiscard]] static std::string processLine(const services::TestProcess& p);
    // 값 줄의 글 ("2", "120 ms", "1.5 %")
    [[nodiscard]] std::string valueText(Field f) const;

    Expected<void> mount(uihost::UiHost& host) override;
    Expected<void> layout(uihost::UiHost& host, const gui::ComponentSnapshot& tree, std::vector<gui::LayoutNode>& out) override;
    Expected<void> paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) override;

    [[nodiscard]] gui::ComponentHandle minusHandle(Field f) const noexcept { return m_rows[static_cast<usize>(f)].minus; }
    [[nodiscard]] gui::ComponentHandle plusHandle(Field f) const noexcept { return m_rows[static_cast<usize>(f)].plus; }
    [[nodiscard]] gui::ComponentHandle startHandle() const noexcept { return m_startNode; }
    [[nodiscard]] gui::ComponentHandle stopHandle() const noexcept { return m_stopNode; }
    [[nodiscard]] usize listedCount() const noexcept { return m_list.itemCount(); }

private:
    struct Row {
        gui::ComponentHandle row, label, minus, value, plus;
        std::unique_ptr<gui::Button> minusButton, plusButton;
    };
    void step(Field f, int dir);
    void applyEnabled();

    PanelContext& m_ctx;
    TestPanelModel m_model;
    std::array<Row, kFields> m_rows;
    gui::ComponentHandle m_root, m_bar, m_startNode, m_stopNode, m_note, m_listNode;
    std::unique_ptr<gui::Button> m_start, m_stop;
    gui::ListView m_list;
};

// 통계 창이 보이는 것 (UI-R3-09) — Application 이 진단 창 ("통계" · "네트워크") 과 같은 원천으로 만든다 (apps/client GuiStatsModel)
enum class StatTone : u8 { Text, Muted, Section, Warning, Error };
struct StatLine {
    std::string label; // 빈 글자 = 줄 전체가 value
    std::string value;
    StatTone tone = StatTone::Text;
    bool operator==(const StatLine&) const = default;
};
struct StatsModel {
    std::string headline;       // "60 fps · 프레임 16.67 ms"
    std::vector<float> frameMs; // 최근 프레임 시간 (오래된 것 먼저) — 막대 그래프
    std::vector<StatLine> lines;
    bool operator==(const StatsModel&) const = default;
};

class StatsPanel final : public uihost::IUiScreen {
public:
    explicit StatsPanel(PanelContext& ctx) : m_ctx(ctx) {}

    void setModel(StatsModel model);
    [[nodiscard]] const StatsModel& model() const noexcept { return m_model; }

    Expected<void> mount(uihost::UiHost& host) override;
    Expected<void> layout(uihost::UiHost& host, const gui::ComponentSnapshot& tree, std::vector<gui::LayoutNode>& out) override;
    Expected<void> paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) override;

    [[nodiscard]] usize listedCount() const noexcept { return m_list.itemCount(); }
    [[nodiscard]] usize barsDrawn() const noexcept { return m_bars; }

private:
    PanelContext& m_ctx;
    StatsModel m_model;
    gui::ComponentHandle m_root, m_headline, m_graph, m_listNode;
    gui::ListView m_list;
    usize m_bars = 0; // 마지막 그리기의 막대 수 (시험)
};

} // namespace sbx::editor::gui
