#pragma once
// 콘텐츠 패널 (FEATURE_MATRIX 11, ADR-0182) — ImGui "콘텐츠" 창 (drawContentPanel) 의 첫 조각: 종류 (규칙 · 행동 · Prefab · 트리거 ·
// 스크립트) · 목록 (팩별 묶음 · 거르기 · "●" = 오버레이) · 고른 항목의 요약과 내용 · 검사 · 적용 · 고친 것 버리기 · 팩 값으로 · 새 항목.
//
//   Prefab   폼 (DataForm prefabForm — 인스펙터와 같은 "이름 | 값" 줄 + 편집 줄) 으로 고친다: 값 칸 → parseFieldText → setFormField
//            (범위 · 반올림 · 검증), 기본값으로 (resetFormField), 컴포넌트 빼기 · 더하기 (removePrefabComponent · addPrefabComponent)
//   그 밖    내용 (JSON · 스크립트 소스) 을 줄로 읽기만 — GraphicsUI 글 칸은 한 줄이라 여러 줄 고치기는 아직 "기존 UI 로"
//   보기     ContentView — 앱이 frame 마다 패널이 고른 종류 · id 로 (contentItems · groupByPack · currentContentText · 요약 · 역할)
//   일       의도 — ContentCheck · ContentApply · ContentRevert · ContentNew. 앱은 checkContentEdit · Editor::editContent ·
//            contentTemplate 를 부르고 결과를 setResult · openItem 으로 돌려준다 (ImGui 창과 같은 함수)
//   다시 불러오기  ImGui 창과 같은 규칙 — 보낸 것이 받아들여지면 (지금 값이 보낼 때와 달라짐) 새 값, 고치지 않은 채 남이 바꾸면 새 값,
//            항목이 없어지면 놓는다

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/content/ContentOverlay.hpp"
#include "editor/gui/StudioPanels.hpp"
#include "editor/services/DataForm.hpp"

namespace sbx::editor::gui {

struct ContentListItem {
    std::string id;
    bool overlay = false; // 이 월드의 오버레이가 바꾸거나 더한 것 ("●")
};

struct ContentListGroup {
    std::string label; // groupByPack 의 label
    std::vector<ContentListItem> items;
};

struct ContentView {
    bool available = false; // 콘텐츠가 있다 (프로젝트 안 · 편집 호스트)
    bool editable = false;  // 역할이 콘텐츠를 바꿀 수 있다 (permissions().content)
    content::ContentKind kind = content::ContentKind::Rule; // 이 보기가 만든 종류 (패널의 종류와 다르면 목록을 쓰지 않는다)
    usize overlayCount = 0;
    std::vector<ContentListGroup> groups; // 그 종류의 항목 전부, 팩별 (groupByPack 차례)
    std::string selected;                 // 아래 값이 가리키는 id
    std::optional<std::string> current;   // 그 항목의 지금 값 (dump(2) · 스크립트 소스), 콘텐츠에 없으면 nullopt
    bool inOverlay = false;
    std::vector<std::string> summary;               // 요약 줄 (ImGui 창의 요약과 같은 내용)
    const ecs::ComponentCatalog* catalog = nullptr; // Prefab 폼 (빌림 — 이 frame 동안)
    std::vector<std::string> prefabs;               // PrefabRef 검사
    std::vector<std::string> addable;               // Prefab 에 더할 수 있는 컴포넌트 (EditorVisible · 엔진 관리 아님)
    std::string message;                            // Editor::lastMessage 중 콘텐츠 거절
};

struct ContentIssueLine {
    bool error = true;
    std::string text;
    bool operator==(const ContentIssueLine&) const = default;
};

class ContentPanel final : public uihost::IUiScreen {
public:
    static constexpr usize kKinds = 5;
    static constexpr std::array<content::ContentKind, kKinds> kKindOrder{
        content::ContentKind::Rule, content::ContentKind::Behavior, content::ContentKind::Prefab,
        content::ContentKind::Trigger, content::ContentKind::Script};

    explicit ContentPanel(PanelContext& ctx)
        : m_ctx(ctx), m_filter("거르기", 13.0), m_newId("새 id (팩.이름)", 13.0), m_value("값", 13.0),
          m_addName("컴포넌트 더하기 (이름)", 13.0) {}

    // 앱이 보기를 만들 때 — 패널이 고른 종류 · 항목
    [[nodiscard]] content::ContentKind kind() const noexcept { return m_kind; }
    [[nodiscard]] const std::string& selected() const noexcept { return m_selected; }
    void setView(ContentView view);
    [[nodiscard]] const ContentView& view() const noexcept { return m_view; }
    // 검사 · 적용 · 팩 값으로의 결과 (앱) — sent = 명령을 보냈다 (그때의 지금 값과 달라지면 다시 불러온다)
    void setResult(std::vector<ContentIssueLine> issues, bool sent);
    // 새 항목 (앱 — contentTemplate · 이미 있는 id 면 그 값)
    void openItem(std::string id, std::string text, bool isNew);

    Expected<void> mount(uihost::UiHost& host) override;
    Expected<void> layout(uihost::UiHost& host, const gui::ComponentSnapshot& tree, std::vector<gui::LayoutNode>& out) override;
    Expected<void> paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) override;

    struct Item { // 왼쪽 목록 한 줄
        bool header = false;
        std::string text; // 머리 글 · id
        bool overlay = false;
    };
    struct Row { // 오른쪽 내용 한 줄
        enum class Kind : u8 { Summary, Header, Field, Text };
        Kind kind = Kind::Text;
        usize section = 0;
        usize field = 0;
        std::string component; // Header · Field
        std::string name;      // 필드 이름 · 머리 글 · 글 줄
        std::string value;
        bool set = false; // 원본에 쓴 필드 (아니면 기본값)
        bool editable = false;
    };
    [[nodiscard]] const std::vector<Item>& items() const noexcept { return m_items; }
    [[nodiscard]] const std::vector<Row>& rows() const noexcept { return m_rows; }
    [[nodiscard]] const std::vector<ContentIssueLine>& issues() const noexcept { return m_issues; }
    [[nodiscard]] const std::vector<ContentIssueLine>& issueLines() const noexcept { return m_issueLines; }
    [[nodiscard]] const std::string& text() const noexcept { return m_text; }
    [[nodiscard]] bool isNew() const noexcept { return m_isNew; }
    [[nodiscard]] bool dirty() const noexcept { return m_isNew || m_text != m_loaded; }
    [[nodiscard]] std::optional<usize> selectedRow() const noexcept { return m_selRow; }
    [[nodiscard]] std::string title() const;
    [[nodiscard]] std::string message() const; // 편집 줄 안내 (필드 종류 · 범위 / 오류)
    [[nodiscard]] bool formMode() const;        // Prefab 폼으로 고친다

    [[nodiscard]] gui::ComponentHandle kindHandle(usize i) const noexcept { return m_kindNodes[i]; }
    [[nodiscard]] gui::ComponentHandle filterHandle() const noexcept { return m_filter.handle(); }
    [[nodiscard]] gui::ComponentHandle itemListHandle() const noexcept { return m_itemsNode; }
    [[nodiscard]] gui::ComponentHandle newIdHandle() const noexcept { return m_newId.handle(); }
    [[nodiscard]] gui::ComponentHandle newHandle() const noexcept { return m_newNode; }
    [[nodiscard]] gui::ComponentHandle rowListHandle() const noexcept { return m_rowsNode; }
    [[nodiscard]] gui::ComponentHandle valueHandle() const noexcept { return m_value.handle(); }
    [[nodiscard]] gui::ComponentHandle setHandle() const noexcept { return m_setNode; }
    [[nodiscard]] gui::ComponentHandle resetHandle() const noexcept { return m_resetNode; }
    [[nodiscard]] gui::ComponentHandle removeHandle() const noexcept { return m_removeNode; }
    [[nodiscard]] gui::ComponentHandle addNameHandle() const noexcept { return m_addName.handle(); }
    [[nodiscard]] gui::ComponentHandle addHandle() const noexcept { return m_addNode; }
    [[nodiscard]] gui::ComponentHandle checkHandle() const noexcept { return m_checkNode; }
    [[nodiscard]] gui::ComponentHandle applyHandle() const noexcept { return m_applyNode; }
    [[nodiscard]] gui::ComponentHandle discardHandle() const noexcept { return m_discardNode; }
    [[nodiscard]] gui::ComponentHandle revertHandle() const noexcept { return m_revertNode; }
    // 시험: 왼쪽 목록 · 오른쪽 줄의 목록 안 자리 (보이지 않으면 nullopt)
    [[nodiscard]] std::optional<gui::LayoutRect> itemBounds(usize item) const;
    [[nodiscard]] std::optional<gui::LayoutRect> rowBounds(usize row) const;

private:
    void selectKind(content::ContentKind k);
    void selectItem(usize item);
    void syncFromView();
    void rebuildItems();
    void rebuildRows();
    void applyEnabled();
    void selectRow(std::optional<usize> row);
    void loadValue();
    [[nodiscard]] const FormField* selectedField() const;
    [[nodiscard]] const FormSection* selectedSection() const;
    [[nodiscard]] std::vector<std::string> addMatches() const;
    // Prefab 폼의 일 — 고친 JSON 을 m_text 에 (검사 · 적용은 단추로)
    bool editPrefab(const std::function<Expected<void>(ecs::Json& prefab)>& f);
    void setField();
    void resetField();
    void toggleBool();
    void removeComponent();
    void addComponent();
    void emitContent(WorkspaceIntent::Kind kind);
    gui::EventReply itemEvent(const gui::RoutedEvent& ev);
    gui::EventReply rowEvent(const gui::RoutedEvent& ev);

    PanelContext& m_ctx;
    TextField m_filter, m_newId, m_value, m_addName;
    ContentView m_view;
    // 편집 상태 (ImGui ContentPanelState 와 같은 뜻)
    content::ContentKind m_kind = content::ContentKind::Prefab;
    std::string m_selected;
    std::string m_text;   // 고치는 글
    std::string m_loaded; // 마지막으로 불러온 글
    bool m_isNew = false;
    bool m_needLoad = false;                  // 고른 뒤 아직 지금 값을 받지 못했다 (다음 보기에서)
    std::optional<std::string> m_pendingFrom; // 적용 · 팩 값으로를 보낸 때의 지금 값
    std::optional<std::string> m_sendFrom;    // 의도를 낸 때의 지금 값 (setResult 의 sent 면 m_pendingFrom 으로)
    std::vector<ContentIssueLine> m_issues;
    std::vector<ContentIssueLine> m_issueLines; // 보이는 줄 (문제의 줄바꿈마다 · Editor 거절)
    std::string m_formError;
    // 폼 (m_text 가 바뀔 때만 다시)
    std::string m_formText;
    std::vector<FormSection> m_sections;
    std::vector<std::string> m_formWarnings;
    std::vector<std::string> m_collapsed;
    std::vector<Item> m_items;
    std::vector<Row> m_rows;
    std::vector<gui::ListItemKey> m_itemKeys, m_rowKeys, m_issueKeys;
    std::optional<usize> m_selRow;
    Row::Kind m_selKind = Row::Kind::Text;
    std::string m_selComponent, m_selField;
    std::string m_appliedFilter;
    uihost::UiHost* m_host = nullptr;
    gui::ComponentHandle m_root, m_kindBar, m_overlayNode, m_body, m_left, m_itemsNode, m_newRow, m_newNode, m_right,
        m_titleNode, m_rowsNode, m_label, m_editRow, m_setNode, m_resetNode, m_compRow, m_removeNode, m_addNode, m_actionRow,
        m_checkNode, m_applyNode, m_discardNode, m_revertNode, m_issuesNode;
    std::array<gui::ComponentHandle, kKinds> m_kindNodes{};
    std::array<std::unique_ptr<gui::Button>, kKinds> m_kindButtons;
    std::unique_ptr<gui::Button> m_new, m_set, m_reset, m_remove, m_add, m_check, m_apply, m_discard, m_revert;
    gui::ListView m_itemList, m_rowList, m_issueList;
    gui::LayoutRect m_itemBounds, m_rowBounds;
};

} // namespace sbx::editor::gui
