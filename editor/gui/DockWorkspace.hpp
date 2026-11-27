#pragma once
// Dock 작업 공간 (UI-R2-05) — GraphicsUI DockLayout (분할 · 탭 · 저장 바이트 — 그리기 없는 모델) 위의 Studio 작업 공간 한 OS 창.
//
//   패널         IUiScreen 하나 = UiHost 하나 (패널마다 자기 트리 · 입력 · 포커스 — Core 컨트롤을 그대로 쓴다). 그룹의 활성 탭만 배치 · 그린다
//   크롬         배경 · 탭 줄 · 탭 · 분할선 — 그리기 명령 묶음 (UI 단위)
//   입력         탭 누르기 = 고르기 · 끌기 = 옮기기 (놓을 자리: 탭 줄 = 그 탭 앞 · 뒤, 그룹 가장자리 25 % = 상 · 하 · 좌 · 우 분할, 가운데 = 그
//                그룹의 탭), 분할선 끌기 = 비율, Esc · 창 포커스 잃음 = 끌기 취소, Ctrl+PageUp · PageDown = 그룹 안 탭 순회.
//                내용 영역의 포인터 · 휠은 그 패널 호스트로 (좌표를 내용 원점 기준으로), 키 · 글자는 포커스된 패널로
//   분리 창      DockLayout 은 안다 (undock) — 다중 OS 창 (S-01) 이 없어 지금은 쓰지 않는다 (UI-R2-06)
//   통과 패널    setPassthrough — 그 패널이 활성인 그룹의 내용 칸 (과 창 전체 바탕) 을 칠하지 않는다. 먼저 그린 월드가 보이고, 그 칸 위의
//                포인터 · 그 패널에 포커스가 있을 때의 키는 앱이 게임 · 편집으로 보낸다 (wantsPointer · wantsKeyboard — ADR-0179)
//
// ImGui · Render 를 모른다 — 그리기는 앱의 렌더 어댑터가 draw() 결과로 (크롬 → 패널 DisplayList (원점 이동) → 끌기 미리보기).

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <graphics-ui/core/docking.hpp>

#include "editor/services/StudioTokens.hpp"
#include "uihost/UiHost.hpp"

namespace sbx::editor::gui {

namespace gui = graphics_ui::core;

struct DockPaneSpec {
    gui::DockPaneId id = 0;
    std::string title;
    std::unique_ptr<uihost::IUiScreen> screen;
};

struct WorkspaceDraw {
    struct Pane {
        gui::DockPaneId id = 0;
        const gui::DisplayList* list = nullptr; // 패널 기준 UI 단위 — origin 만큼 옮겨 그린다
        gui::LayoutPoint origin;
    };
    std::vector<gui::RenderCommand> chrome;  // UI 단위
    std::vector<Pane> panes;
    std::vector<gui::RenderCommand> overlay; // 끌기 중 놓을 자리 (UI 단위)
};

struct DockDropTarget {
    gui::DockPaneId target = 0;
    gui::DockSide side = gui::DockSide::tab_after;
    gui::LayoutRect preview; // 놓으면 그 패널이 차지할 대략의 자리 (UI 단위)
};

class DockWorkspace {
public:
    [[nodiscard]] static Expected<std::unique_ptr<DockWorkspace>> create(const uihost::ITextMeasure& text,
                                                                         std::vector<DockPaneSpec> panes);
    ~DockWorkspace();
    DockWorkspace(const DockWorkspace&) = delete;
    DockWorkspace& operator=(const DockWorkspace&) = delete;

    [[nodiscard]] gui::DockLayout& layout() noexcept { return *m_layout; }
    void setTheme(services::StudioTheme theme) noexcept { m_theme = theme; }
    void setCompact(bool compact) noexcept { m_compact = compact; }
    void setScale(f32 scale) noexcept { m_scale = scale > 0.f ? scale : 1.f; }
    [[nodiscard]] f32 scale() const noexcept { return m_scale; }
    void setClipboard(const uihost::Clipboard& clipboard);
    void setPassthrough(gui::DockPaneId id, bool passthrough);
    [[nodiscard]] bool passthrough(gui::DockPaneId id) const;
    // 이 포인터 (창 논리 좌표) 를 작업 공간이 가져가나 — 그룹 · 분할선 위이고 통과 칸 밖, 또는 끄는 중 (지난 frame 의 배치)
    [[nodiscard]] bool wantsPointer(gui::LayoutPoint logical) const;
    // 키를 작업 공간이 가져가나 — 포커스 패널이 통과 패널이 아니다
    [[nodiscard]] bool wantsKeyboard() const;

    void feed(std::span<const platform::PlatformEvent> events);
    // logical = 창의 논리 크기. 반환 포인터는 다음 frame 까지 유효
    Expected<const WorkspaceDraw*> frame(gui::LayoutSize logical);

    [[nodiscard]] uihost::TextInputState textInput() const;
    [[nodiscard]] bool textFocused() const;
    [[nodiscard]] gui::DockPaneId focusedPane() const noexcept { return m_focus; }
    [[nodiscard]] bool dragging() const noexcept { return m_dragging; }
    // 탭을 누르고 있거나 · 끌거나 · 분할선을 끄는 중 (배치 저장은 놓은 뒤에)
    [[nodiscard]] bool interacting() const noexcept { return m_dragging || m_divider.has_value() || m_pressTab != 0; }
    [[nodiscard]] const std::optional<DockDropTarget>& dropTarget() const noexcept { return m_drop; }
    [[nodiscard]] const gui::DockFrame* lastFrame() const noexcept { return m_frame ? &*m_frame : nullptr; }
    [[nodiscard]] uihost::UiHost* paneHost(gui::DockPaneId id) const;
    [[nodiscard]] uihost::IUiScreen* paneScreen(gui::DockPaneId id) const;

    [[nodiscard]] Expected<std::vector<std::byte>> save() const;
    Expected<void> restore(std::span<const std::byte> bytes);

private:
    struct Pane {
        gui::DockPaneId id = 0;
        std::string title;
        std::unique_ptr<uihost::IUiScreen> screen;
        std::unique_ptr<uihost::UiHost> host;
        bool passthrough = false;
    };
    DockWorkspace(const uihost::ITextMeasure& text) noexcept : m_text(text) {}

    [[nodiscard]] Pane* pane(gui::DockPaneId id) const;
    [[nodiscard]] gui::DockMetrics metrics() const noexcept;
    [[nodiscard]] const gui::DockGroupBox* groupAt(gui::LayoutPoint p) const;
    [[nodiscard]] const gui::DockTabBox* tabAt(gui::LayoutPoint p) const;
    [[nodiscard]] std::optional<DockDropTarget> dropAt(gui::LayoutPoint p) const;
    void forward(gui::DockPaneId id, const platform::PlatformEvent& e);
    void cycleTab(int step);

    const uihost::ITextMeasure& m_text;
    std::unique_ptr<gui::DockLayout> m_layout;
    std::vector<Pane> m_panes;
    std::optional<gui::DockFrame> m_frame;
    gui::LayoutSize m_lastViewport; // 마지막 frame 의 크기 (UI 단위) — 분할선을 끄는 동안 다시 배치할 때
    WorkspaceDraw m_draw;
    services::StudioTheme m_theme = services::StudioTheme::Dark;
    bool m_compact = false;
    f32 m_scale = 1.f;
    gui::DockPaneId m_focus = 0;

    // 포인터 상태 (UI 단위)
    gui::LayoutPoint m_pointer;
    std::optional<gui::DockDividerBox> m_divider; // 끌고 있는 분할선
    gui::DockPaneId m_pressTab = 0;              // 누른 탭 (끌기 전)
    gui::LayoutPoint m_pressAt;
    bool m_dragging = false;
    std::optional<DockDropTarget> m_drop;
    gui::DockPaneId m_capture = 0; // 내용 영역에서 누른 패널 — 뗄 때까지 그 패널로
};

} // namespace sbx::editor::gui
