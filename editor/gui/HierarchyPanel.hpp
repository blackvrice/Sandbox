#pragma once
// Hierarchy ("목록") 패널 (UI-R3-02) — ImGui "목록" 창과 같은 서비스 (HierarchyService — 검색 · 편집기 그룹 · 줄 만들기).
//
//   보기   HierarchyView — 앱이 frame 마다 (IEditorHost::listEntities 의 줄 · Editor 의 그룹 · 콘텐츠 · 선택). 줄의 prefab 은
//          빌린 글자 (그 frame 동안) — 패널은 배치 때 자기 줄 (글 · id) 로 옮겨 쓴다
//   일     의도 (WorkspaceIntent) — 선택 · 카메라 맞추기 (두 번 누르기 · F · Enter) · 지우기 (Delete) · 묶기 · 빼기 · 그룹 접기.
//          Editor · IEditorHost 는 앱이 (editor/gui 는 나머지 editor/ 를 모른다 — ADR-0175)
//   못 하는 것  맥락 메뉴 (그룹 지우기 · 이것만 선택) — GraphicsUI 에 팝업이 없다 (GUI-UP-03). 그 일은 "기존 UI 로"

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "editor/gui/StudioPanels.hpp"
#include "editor/services/HierarchyService.hpp"

namespace sbx::content {
class ContentDatabase;
}

namespace sbx::editor::gui {

struct HierarchyView {
    bool available = false;                       // 편집 호스트가 있다 (로컬 서버 · 편집 권한)
    std::span<const HierarchyRow> rows;           // 관심 영역 안 개체 (빌림 — 이 frame 동안)
    const EditorGroups* groups = nullptr;         // 빌림
    const content::ContentDatabase* content = nullptr; // tag: 검색
    std::vector<NetEntityId> selection;           // 오름차순
    bool canDelete = false;                       // 편집 권한 (entities)
};

class HierarchyPanel final : public uihost::IUiScreen {
public:
    // 두 번 누르기 — 같은 줄을 이 frame 수 안에 다시 누르면 (호스트 frame 차례 — 결정적)
    static constexpr u64 kDoubleClickFrames = 20;

    explicit HierarchyPanel(PanelContext& ctx)
        : m_ctx(ctx), m_search("검색 (낱말 · #번호 · prefab: · tag: · group:)", 13.0), m_groupName("그룹 이름", 13.0) {}

    // frame 마다 — 빌린 줄은 다음 setView 까지만 쓴다
    void setView(HierarchyView view);

    Expected<void> mount(uihost::UiHost& host) override;
    Expected<void> layout(uihost::UiHost& host, const gui::ComponentSnapshot& tree, std::vector<gui::LayoutNode>& out) override;
    Expected<void> paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) override;

    // 줄 — 개체 줄의 글은 보이는 줄만 그릴 때 만든다 (frame 마다 10 만 줄의 글을 만들지 않는다 — UI-R9-03, ADR-0188)
    //      줄은 작게 (글자 없이 16 바이트) — 그룹 머리의 이름 · 글은 따로 (그룹 수만큼)
    struct Line {
        bool group = false;
        bool inGroup = false;
        NetEntityId id = kInvalidNetEntityId; // 개체 줄
        usize row = 0;                        // 개체 줄 = 보기의 rows 번호, 그룹 줄 = 그룹 머리 번호 (groupName)
    };
    struct GroupHead {
        std::string name;
        std::string text; // "▾ wolves  (3)"
    };
    [[nodiscard]] const std::vector<Line>& lines() const noexcept { return m_lines; }
    // 보이는 글 ("▾ wolves  (3)", "eco.rabbit #42") — 개체 줄은 빌린 rows 로 만든다 (다음 setView 까지)
    [[nodiscard]] std::string lineText(usize line) const;
    // 그룹 줄의 그룹 이름 (아니면 빈 글)
    [[nodiscard]] std::string_view groupName(usize line) const;
    [[nodiscard]] usize shownEntities() const noexcept { return m_shown; }
    [[nodiscard]] std::string status() const;
    [[nodiscard]] gui::ComponentHandle searchHandle() const noexcept { return m_search.handle(); }
    [[nodiscard]] gui::ComponentHandle groupNameHandle() const noexcept { return m_groupName.handle(); }
    [[nodiscard]] gui::ComponentHandle groupHandle() const noexcept { return m_groupNode; }
    [[nodiscard]] gui::ComponentHandle ungroupHandle() const noexcept { return m_ungroupNode; }
    [[nodiscard]] gui::ComponentHandle listHandle() const noexcept { return m_listNode; }
    // 시험: 그 줄의 목록 안 자리 (목록 상자 기준, 없으면 nullopt)
    [[nodiscard]] std::optional<gui::LayoutRect> rowBounds(usize line) const;

private:
    void rebuild();
    void applyEnabled();
    void clickLine(usize line, gui::KeyModifiers mods);
    void selectOnly(NetEntityId id);
    gui::EventReply listEvent(const gui::RoutedEvent& ev);
    [[nodiscard]] bool selected(NetEntityId id) const;

    PanelContext& m_ctx;
    TextField m_search;
    TextField m_groupName;
    HierarchyView m_view;
    std::vector<Line> m_lines;
    std::vector<GroupHead> m_heads;
    std::vector<gui::ListItemKey> m_keys;
    std::vector<HierarchyLine> m_built;        // frame 마다 쓰는 자리 (할당을 되풀이하지 않는다)
    std::vector<gui::ListItemKey> m_nextKeys;
    usize m_shown = 0;
    uihost::UiHost* m_host = nullptr;
    gui::ComponentHandle m_root, m_status, m_bar, m_groupNode, m_ungroupNode, m_listNode;
    std::unique_ptr<gui::Button> m_group, m_ungroup;
    gui::ListView m_list;
    gui::LayoutRect m_listBounds;
    usize m_cursor = 0;            // 키보드 줄 (↑↓)
    std::optional<usize> m_lastClick;
    u64 m_lastClickFrame = 0;
};

} // namespace sbx::editor::gui
