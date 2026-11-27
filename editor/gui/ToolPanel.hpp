#pragma once
// 편집 (툴) 패널 (UI-R3 — FEATURE_MATRIX 10, ADR-0181) — ImGui "편집" 창 (drawEditorPanel) 과 같은 일: 툴 고르기 · 툴마다 설정
// (이동 · 배치 격자 맞춤, 배치 Prefab — 팩별 묶음 · 거르기 · 끌기 간격, 지형 머티리얼 · 붓 반지름 · 모양) · 되돌리기 · 다시 ·
// 명령 수 · 가장 최근 거절 사유.
//
//   보기   ToolPanelView — 앱이 frame 마다 (Editor 의 툴 · 설정 · 기록 · 통계, 역할, 콘텐츠의 Prefab 묶음 (groupByPack) · 머티리얼)
//   일     의도 — SelectTool (value = Tool 차례) · ToolSettings (고친 설정 전부) · RunCommand edit.undo · edit.redo. Editor 는 앱이
//          (editor/gui 는 나머지 editor/ 를 모른다 — ADR-0175)
//   트리   고정 (ADR-0180 과 같은 결) — 툴 막대 · 안내 · 거르기 · 목록 · 값 줄 (− 값 +) · 켜고 끄기 · 되돌리기 줄 · 수 · 사유.
//          툴마다 쓰지 않는 칸은 끄고 이름만 바꾼다

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "editor/gui/StudioPanels.hpp"

namespace sbx::editor::gui {

// 한 팩의 Prefab (묶음 머리 — groupByPack 의 label, 그 차례)
struct ToolPrefabGroup {
    std::string label;
    std::vector<std::string> ids;
};

struct ToolPanelView {
    static constexpr usize kTools = 5; // editor::kToolCount 와 같다 (앱이 static_assert)

    bool available = false; // 편집 호스트가 있다 (프로젝트 안)
    bool canEdit = false;   // 지금 역할로 편집할 수 있다 (EditPermissions::anyEdit)
    int tool = 0;           // editor::Tool 차례 — 0 선택 · 1 이동 · 2 배치 · 3 지형 · 4 지우기
    std::array<std::string, kTools> toolNames; // editor::toolName
    u32 toolMask = 1;                          // 이 역할로 쓸 수 있는 툴 (toolAllowed — 비트 i)
    ToolSettingsValues settings;
    int maxRadius = 8;                         // Editor::kMaxBrushRadius
    std::vector<ToolPrefabGroup> prefabGroups; // 콘텐츠 Prefab 전부, 팩별 (groupByPack 차례)
    std::vector<std::string> materials;        // 지형 머티리얼 id (콘텐츠 차례)
    bool canUndo = false;
    bool canRedo = false;
    std::string undoLabel;
    std::string redoLabel;
    usize undoDepth = 0;
    usize redoDepth = 0;
    u64 sent = 0;
    u64 accepted = 0;
    u64 rejected = 0;
    std::string message; // Editor::lastMessage
};

class ToolPanel final : public uihost::IUiScreen {
public:
    static constexpr usize kTools = ToolPanelView::kTools;
    static constexpr int kSelect = 0, kMove = 1, kPlace = 2, kTerrain = 3, kErase = 4;
    static constexpr double kSpacingMin = 0.5, kSpacingMax = 8.0, kSpacingStep = 0.5; // ImGui 슬라이더와 같은 범위

    explicit ToolPanel(PanelContext& ctx) : m_ctx(ctx), m_filter("거르기 — Prefab (배치 툴)", 13.0) {}

    void setView(ToolPanelView view);
    [[nodiscard]] const ToolPanelView& view() const noexcept { return m_view; }

    Expected<void> mount(uihost::UiHost& host) override;
    Expected<void> layout(uihost::UiHost& host, const gui::ComponentSnapshot& tree, std::vector<gui::LayoutNode>& out) override;
    Expected<void> paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) override;

    // 목록 한 줄 — 팩 머리 · 배치 Prefab · 지형 머티리얼
    struct Line {
        enum class Kind : u8 { Header, Prefab, Material };
        Kind kind = Kind::Prefab;
        std::string text; // 머리 글 · id
    };
    [[nodiscard]] const std::vector<Line>& lines() const noexcept { return m_lines; }
    [[nodiscard]] std::optional<usize> selectedLine() const; // 지금 설정 (prefab · material) 의 줄
    [[nodiscard]] std::string title() const;                 // "툴: 배치 (3)"
    [[nodiscard]] std::string hint() const;                  // 툴의 쓰는 법 · 역할 안내
    [[nodiscard]] std::string stepLabel() const;             // "끌기 간격 (칸)" · "붓 반지름" · "—"
    [[nodiscard]] std::string stepValue() const;             // "2.0" · "3"
    [[nodiscard]] std::string toggleLabel() const;           // "격자 맞춤: 끔" · "모양: 원"
    [[nodiscard]] std::string countLine() const;             // "명령 3 · 수락 2 · 거절 1 — 기록 2 · 0"
    [[nodiscard]] bool stepEnabled() const;
    [[nodiscard]] bool toggleEnabled() const;

    [[nodiscard]] gui::ComponentHandle toolHandle(usize i) const noexcept { return m_toolNodes[i]; }
    [[nodiscard]] gui::ComponentHandle filterHandle() const noexcept { return m_filter.handle(); }
    [[nodiscard]] gui::ComponentHandle listHandle() const noexcept { return m_listNode; }
    [[nodiscard]] gui::ComponentHandle minusHandle() const noexcept { return m_minusNode; }
    [[nodiscard]] gui::ComponentHandle plusHandle() const noexcept { return m_plusNode; }
    [[nodiscard]] gui::ComponentHandle toggleHandle() const noexcept { return m_toggleNode; }
    [[nodiscard]] gui::ComponentHandle undoHandle() const noexcept { return m_undoNode; }
    [[nodiscard]] gui::ComponentHandle redoHandle() const noexcept { return m_redoNode; }
    // 시험: 그 줄의 목록 안 자리 (목록 상자 기준, 보이지 않으면 nullopt)
    [[nodiscard]] std::optional<gui::LayoutRect> rowBounds(usize line) const;

private:
    void rebuild();
    void applyEnabled();
    void pick(usize line);
    void pickFirst();
    void step(int dir);
    void toggle();
    void emit(const ToolSettingsValues& s);
    gui::EventReply listEvent(const gui::RoutedEvent& ev);

    PanelContext& m_ctx;
    TextField m_filter;
    ToolPanelView m_view;
    std::vector<Line> m_lines;
    std::vector<gui::ListItemKey> m_keys;
    uihost::UiHost* m_host = nullptr;
    gui::ComponentHandle m_root, m_title, m_bar, m_hint, m_listNode, m_stepRow, m_stepLabel, m_minusNode, m_valueNode,
        m_plusNode, m_toggleNode, m_undoRow, m_undoNode, m_redoNode, m_count, m_message;
    std::array<gui::ComponentHandle, kTools> m_toolNodes{};
    std::array<std::unique_ptr<gui::Button>, kTools> m_tools;
    std::unique_ptr<gui::Button> m_minus, m_plus, m_toggle, m_undo, m_redo;
    gui::ListView m_list;
    gui::LayoutRect m_listBounds;
};

} // namespace sbx::editor::gui
