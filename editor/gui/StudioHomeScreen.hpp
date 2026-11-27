#pragma once
// Studio 홈의 GraphicsUI 화면 (UI-R1-05 · R2-09, ADR-0174 · 0175 — editor/gui 로 옮김) — `--ui graphicsui` 일 때 ImGui 홈
// (apps/client/studio/StudioHome) 대신 그린다.
// 같은 데이터 (ProjectService 의 최근 목록 · 템플릿 · 예제, AutosaveService 의 복구) · 같은 도우미 (filterRecent · suggestGameId ·
// suggestProjectDir · displayTime) · 같은 행동 (StudioHomeAction — Application 이 같은 명령 · 처리로) — 로직을 옮기지 않는다.
//
// 화면: 최근 프로젝트 (검색 글자 칸 + 가상 목록 — ListView, 휠 · ↑↓ · Home · End · Enter · Delete · 클릭, 복구 가능 표시, 목록에서 빼기) |
// 새 프로젝트 (템플릿 ‹ › · 이름 · gameId · 폴더 — 이름을 쓰면 gameId · 폴더를 제안, 손으로 고친 칸은 그대로) · 폴더 열기 · 예제 복사
// (예제 ‹ › · 복사할 곳) | 결과 줄 | 기존 UI 로 · 간격 · 설정 (GXML 화면 — UI-R2-02) · 종료.

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <graphics-ui/core/controls.hpp>
#include <graphics-ui/core/scroll.hpp>

#include "editor/services/ProjectService.hpp"
#include "editor/services/StudioTokens.hpp"
#include "uihost/UiHost.hpp"

namespace sbx::editor::gui {

namespace gui = graphics_ui::core;

struct StudioHomeModel {
    std::string version;
    std::vector<services::RecentProject> recent;
    std::vector<services::ProjectTemplate> templates;
    std::vector<services::ExampleProject> examples;
    // recent 와 같은 차례 — 빈 글자 = 복구할 것 없음, 아니면 자동 저장 시각 (AutosaveService::peek — 비정상 종료 뒤)
    std::vector<std::string> recovery;
};

struct StudioHomeIntent {
    enum class Kind : u8 { OpenRecent, ForgetRecent, NewProject, OpenFolder, CopyExample, Density, OpenSettings, SwitchToClassic, Quit };
    Kind kind = Kind::Quit;
    std::filesystem::path dir;  // OpenRecent · ForgetRecent · NewProject · OpenFolder · CopyExample (예제)
    std::filesystem::path dest; // CopyExample (복사할 곳)
    std::string gameId;         // NewProject
    std::string name;           // NewProject
    std::string templateId;     // NewProject
    bool compact = false;       // Density
};

class StudioHomeScreen final : public uihost::IUiScreen {
public:
    static constexpr gui::FontId kFont = 1;    // 글꼴 하나 (렌더 어댑터가 ID → 글꼴)
    static constexpr double kRowHeight = 48.0; // 보통 간격의 행 높이 (촘촘 = kCompactRowHeight)
    static constexpr double kCompactRowHeight = 38.0;

    // 시험 · 자동화가 컴포넌트를 찾는 이름
    enum class Part : u8 {
        Search, List, Forget, Name, GameId, Dir, Create, OpenDir, Open, ExampleDir, CopyExample, TemplatePrev,
        TemplateNext, ExamplePrev, ExampleNext, Switch, Density, Settings, Quit,
    };

    // 모델이 바뀌면 (최근 목록 · 템플릿 · 예제) 다시 마운트해야 한다 — Application 이 setScreen 을 다시 부른다. 쓰던 글자는 남긴다
    void setModel(StudioHomeModel model);
    [[nodiscard]] const StudioHomeModel& model() const noexcept { return m_model; }
    // 결과 줄 · 간격 (마운트 없이 — Application 의 StudioHomeState::message · failed, Studio 간격)
    void setStatus(std::string message, bool failed);
    void setCompact(bool compact) noexcept { m_compact = compact; }
    // 색 = Studio 디자인 토큰의 테마 (UI-R2-03 — ImGui 셸과 같은 테마 · 같은 값, 마운트 없이)
    void setTheme(services::StudioTheme theme) noexcept { m_theme = theme; }
    [[nodiscard]] services::StudioTheme theme() const noexcept { return m_theme; }
    [[nodiscard]] bool compact() const noexcept { return m_compact; }
    [[nodiscard]] double rowHeight() const noexcept { return m_compact ? kCompactRowHeight : kRowHeight; }
    // 이번 프레임에 고른 것 (한 번 읽으면 비운다)
    [[nodiscard]] std::optional<StudioHomeIntent> takeIntent();

    Expected<void> mount(uihost::UiHost& host) override;
    Expected<void> layout(uihost::UiHost& host, const gui::ComponentSnapshot& tree,
                          std::vector<gui::LayoutNode>& out) override;
    Expected<void> paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) override;

    [[nodiscard]] std::optional<gui::ComponentHandle> handleOf(Part part) const;
    // 검색을 거친 목록의 항목 수 · 지금 고른 최근 프로젝트 차례 (model().recent 의 index)
    [[nodiscard]] usize listedCount() const noexcept { return m_list.itemCount(); }
    [[nodiscard]] std::optional<usize> selectedRecent() const noexcept;
    [[nodiscard]] const std::string& fieldText(Part part) const;
    [[nodiscard]] usize templateIndex() const noexcept { return m_template; }
    [[nodiscard]] usize exampleIndex() const noexcept { return m_example; }
    [[nodiscard]] double listOffset() const noexcept { return m_list.scroll().offset().y; }

private:
    enum class Role : u8 {
        Root, Title, Version, Body, Left, Heading, Field, List, ListBar, Forget, Right, NewHeading, Picker, PickPrev,
        PickLabel, PickNext, Create, OpenHeading, Open, ExampleHeading, CopyExample, Message, Bar, Switch, Density, Settings,
        Quit, Note,
    };
    // 넘기기 (‹ 이름 ›) 의 대상 — Node::index 가 이것
    static constexpr usize kTemplatePicker = 0;
    static constexpr usize kExamplePicker = 1;

    struct Field {
        Part part = Part::Search;
        std::string value;
        std::string placeholder;
        bool edited = false; // 사용자가 직접 고쳤다 (제안으로 덮어쓰지 않는다)
        std::unique_ptr<gui::TextInput> input;
    };
    struct Node {
        Role role = Role::Root;
        usize index = 0; // Field: m_fields 차례, Picker · PickPrev · PickLabel · PickNext: k*Picker
        gui::ComponentHandle handle;
        std::unique_ptr<gui::Button> button;
        gui::LayoutRect bounds; // 마지막 paint 의 화면 좌표 (포인터 → 컴포넌트 안 좌표)
    };

    [[nodiscard]] std::string textOf(const Node& n) const;
    [[nodiscard]] double emOf(Role role) const noexcept;
    [[nodiscard]] Field& field(Part part);
    [[nodiscard]] const Node* nodeOf(gui::ComponentHandle h) const;
    void refilter();
    void fieldChanged(Field& f);
    void suggestExampleDir();
    void submit(Part from);
    void activate(const Node& n);
    void forgetSelected();
    gui::EventReply listEvent(const gui::RoutedEvent& ev);
    gui::EventReply fieldEvent(usize fieldIndex, const gui::RoutedEvent& ev);
    void moveSelection(std::ptrdiff_t delta, bool toEnd = false, bool toStart = false);

    StudioHomeModel m_model;
    std::string m_status;
    bool m_failed = false;
    bool m_compact = false;
    services::StudioTheme m_theme = services::StudioTheme::Dark;
    uihost::UiHost* m_host = nullptr; // mount 한 호스트 (핸들러 · 편집 콜백) — 호스트가 핸들러를 소유한다
    std::vector<Node> m_nodes;
    std::vector<Field> m_fields;
    gui::ListView m_list;
    std::optional<gui::ListItemKey> m_pressKey; // 누른 행 — 같은 행에서 떼면 연다
    usize m_template = 0;
    usize m_example = 0;
    std::optional<StudioHomeIntent> m_intent;
};

} // namespace sbx::editor::gui
