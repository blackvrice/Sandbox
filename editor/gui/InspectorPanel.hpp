#pragma once
// 인스펙터 패널 (UI-R3-03) — ImGui "인스펙터" 창과 같은 원천 (IEditorHost::components · Editor::prefabOverrides) 을 DataForm 의
// 폼 (FormSection · FormField — 종류 · 범위 · 검증, ADR-0163) 으로 보인다.
//
//   보기   InspectorView — 앱이 frame 마다 (첫 선택 개체의 EditorVisible 컴포넌트 = 섹션, 이름 차례 · 복제 여부 · Prefab 오버라이드 ·
//          뺀 컴포넌트 · 더할 수 있는 이름 · 카탈로그). 값은 복사 (개체 하나 — ImGui 창도 frame 마다 같은 값을 읽는다)
//   모양   속성 표 (가상 목록 — 섹션 머리 · 필드 줄 "이름 | 값" · Prefab 에서 뺀 컴포넌트) + 편집 줄 (고른 필드의 글자 칸 · 적용) +
//          컴포넌트 단추 (빼기 · Prefab 값으로 / 되살리기) + 더하기 (이름 칸 · 더하기). 팝업 · 슬라이더 끌기는 아직 — 글자로 쓴다
//          (GraphicsUI 에 팝업이 없다 — GUI-UP-03)
//   쓰기   글자 → 종류별 값 (parseFieldText — 각도는 도, 좌표는 "x, y") → DataForm::setFormField 로 검사 · 범위로 자르기 → 의도
//          SetField (앱이 Editor::setField — 선택한 개체 중 그 컴포넌트가 있는 것 전부, 되돌리기 기록도 거기서). 틀리면 오류 줄
//   키     ↑↓ = 줄, Enter = 값 칸으로 (머리 = 접기), Space · 두 번 누르기 = 켜기 끄기 (bool), Esc (값 칸) = 되돌림,
//          Ctrl+Z · Ctrl+Y = 편집 되돌리기 · 다시 (명령 edit.undo · edit.redo — 툴바와 같은 길)
//   못 하는 것  Prefab 으로 만들기 (Object → Prefab) — "기존 UI 로" (후속)

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/components/core/Identity.hpp"
#include "editor/gui/StudioPanels.hpp"
#include "editor/services/DataForm.hpp"

namespace sbx::editor::gui {

struct InspectorSection {
    enum class Mark : u8 { None, Different, Added }; // Prefab 과 다름 · Prefab 에 없던 것을 더함 (ADR-0158)
    FormSection form;                    // componentForm (지금 값 — 모든 필드가 set)
    bool replicated = true;              // false = 서버의 선택 상세 ("서버")
    Mark mark = Mark::None;
    std::vector<std::string> overridden; // Prefab 과 다른 필드
};

struct InspectorRemoved { // Prefab 에서 뺀 컴포넌트 (되살리기)
    ecs::StableId stableId = 0;
    std::string component;
};

struct InspectorView {
    bool available = false;                     // 편집 호스트가 있다
    NetEntityId entity = kInvalidNetEntityId;   // 첫 선택 (없으면 무효)
    usize selected = 0;                         // 선택한 개체 수
    std::string title;                          // "eco.rabbit #42"
    bool editable = false;                      // 지금 역할로 개체를 고칠 수 있다 (EditPermissions::entities)
    const ecs::ComponentCatalog* catalog = nullptr; // 빌림 (검사 · 더하기)
    std::vector<InspectorSection> sections;     // 이름 차례
    std::vector<InspectorRemoved> removed;
    std::vector<std::string> addable;           // 더할 수 있는 EditorVisible 컴포넌트 (이름 차례)
    std::vector<std::string> prefabs;           // 콘텐츠의 Prefab id (PrefabRef 검사 · 후보 — ImGui 창의 콤보와 같은 목록)
};

class InspectorPanel final : public uihost::IUiScreen {
public:
    static constexpr u64 kDoubleClickFrames = 20; // 목록 패널과 같다 (호스트 frame 차례)

    explicit InspectorPanel(PanelContext& ctx)
        : m_ctx(ctx), m_value("값", 13.0), m_addName("컴포넌트 더하기 (이름)", 13.0) {}

    void setView(InspectorView view);

    Expected<void> mount(uihost::UiHost& host) override;
    Expected<void> layout(uihost::UiHost& host, const gui::ComponentSnapshot& tree, std::vector<gui::LayoutNode>& out) override;
    Expected<void> paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) override;

    struct Row {
        enum class Kind : u8 { Removed, Header, Field };
        Kind kind = Kind::Field;
        usize section = 0; // Header · Field
        usize field = 0;   // Field
        ecs::StableId component = 0;
        std::string name;  // 머리 = 보이는 글, 필드 = 필드 이름, 뺀 것 = "Prefab 에서 뺌: …"
        std::string value; // 필드의 보이는 값
        bool editable = false;
        bool overridden = false;
    };
    [[nodiscard]] const std::vector<Row>& rows() const noexcept { return m_rows; }
    [[nodiscard]] std::optional<usize> selectedRow() const noexcept { return m_selRow; }
    [[nodiscard]] std::string status() const;
    // 편집 줄 위의 안내 — 오류 (마지막 적용 · 더하기) > 더하기 후보 (이름 칸에 글이 있을 때) > 고른 필드의 종류 · 범위
    [[nodiscard]] std::string message() const;
    [[nodiscard]] bool messageIsError() const noexcept { return !m_error.empty(); }
    [[nodiscard]] gui::ComponentHandle listHandle() const noexcept { return m_listNode; }
    [[nodiscard]] gui::ComponentHandle valueHandle() const noexcept { return m_value.handle(); }
    [[nodiscard]] gui::ComponentHandle applyHandle() const noexcept { return m_applyNode; }
    [[nodiscard]] gui::ComponentHandle removeHandle() const noexcept { return m_removeNode; }
    [[nodiscard]] gui::ComponentHandle revertHandle() const noexcept { return m_revertNode; }
    [[nodiscard]] gui::ComponentHandle addNameHandle() const noexcept { return m_addName.handle(); }
    [[nodiscard]] gui::ComponentHandle addHandle() const noexcept { return m_addNode; }
    [[nodiscard]] const std::string& valueText() const noexcept { return m_value.value(); }
    [[nodiscard]] std::string revertLabel() const;
    // 시험: 그 줄의 목록 안 자리 (목록 상자 기준)
    [[nodiscard]] std::optional<gui::LayoutRect> rowBounds(usize row) const;

    // 보이는 값 (종류별 — 각도는 도, 좌표는 "x, y", 참조 · 그 밖은 읽기만 표시)
    [[nodiscard]] static std::string fieldText(const FormField& field);
    // 글자 → 그 종류의 JSON 값 (범위 자르기 · 검증은 setFormField). 못 읽으면 오류
    [[nodiscard]] static Expected<ecs::Json> parseFieldText(const FormField& field, std::string_view text);
    // 필드 종류 · 범위 안내 ("수 · 범위 0 ~ 64" — 인스펙터 · 콘텐츠 패널의 안내 줄)
    [[nodiscard]] static std::string kindText(const FormField& f);

private:
    void rebuild();
    void applyEnabled();
    void selectRow(std::optional<usize> row);
    void loadValue();
    void apply();
    void toggleBool();
    void addComponent();
    [[nodiscard]] std::vector<std::string> addMatches() const;
    void clickRow(usize row);
    gui::EventReply listEvent(const gui::RoutedEvent& ev);
    [[nodiscard]] const FormField* selectedField() const;
    [[nodiscard]] const InspectorSection* selectedSection() const;

    PanelContext& m_ctx;
    TextField m_value;
    TextField m_addName;
    InspectorView m_view;
    std::vector<Row> m_rows;
    std::vector<gui::ListItemKey> m_keys;
    std::vector<ecs::StableId> m_collapsed;
    // 고른 줄 — 이름으로 잡는다 (값이 바뀌어 다시 만들어도 같은 줄)
    std::optional<usize> m_selRow;
    Row::Kind m_selKind = Row::Kind::Field;
    ecs::StableId m_selComponent = 0;
    std::string m_selField;
    NetEntityId m_selEntity = kInvalidNetEntityId;
    std::string m_error;
    uihost::UiHost* m_host = nullptr;
    gui::ComponentHandle m_root, m_title, m_listNode, m_label, m_valueRow, m_applyNode, m_compRow, m_removeNode,
        m_revertNode, m_addRow, m_addNode;
    std::unique_ptr<gui::Button> m_apply, m_remove, m_revert, m_add;
    gui::ListView m_list;
    gui::LayoutRect m_listBounds;
    std::optional<usize> m_lastClick;
    u64 m_lastClickFrame = 0;
};

} // namespace sbx::editor::gui
