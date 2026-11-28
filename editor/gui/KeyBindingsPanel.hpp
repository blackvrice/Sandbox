#pragma once
// "키" 패널 (UI-R2-08 — 단축키 바꾸기 · 충돌 표시, ADR-0194) — ImGui "키" 창 (apps/client/ui/KeysPanel — drawKeysPanel) 과 같은 일.
//
//   목록  겹침 줄 ("겹침 Q — a · b", 경고색 — 엔진 · 게임 키를 함께 본다) · "엔진 키" 머리 · 엔진 액션 (이름 순) · "게임 키 (팩)" 머리 ·
//         play.* (지금 쓰는 값). 줄 = "액션 | 키" — 기본과 다르면 강조색
//   막대  바꾸기 (고른 줄 — 다음에 누른 키 · 버튼이 그 바인딩, Esc = 취소. 바꾸는 중이면 같은 자리가 "취소") · 기본 (고른 줄이 바뀐
//         것일 때) · 모두 기본값 · 저장 (설정 폴더가 있고 저장 안 한 것이 있을 때)
//   키    ↑↓ 줄 · Enter = 바꾸기. 바꾸는 중에는 패널이 키를 쓰지 않는다 (누른 키는 앱이 잡는다 — 입력의 날 값)
//   일    의도 KeyBinding (value = KeysPanelRequest::Kind 차례 — 앱이 static_assert, command = 액션) → 앱의 applyKeysRequest
//         (KeyBindingEditor · input.json 저장 — ImGui 창과 같은 함수)

#include <optional>
#include <string>
#include <vector>

#include "editor/gui/StudioPanels.hpp"

namespace sbx::editor::gui {

struct KeyBindingRow {
    std::string action;
    std::string keys;     // "W / Up" (없으면 빈 글)
    bool play = false;    // 팩 게임 키 (play.*)
    bool changed = false; // 기본과 다르다
    bool operator==(const KeyBindingRow&) const = default;
};

struct KeyBindingsModel {
    // client::KeysPanelRequest::Kind 와 같은 차례 (앱이 static_assert)
    static constexpr int kCapture = 0, kCancel = 1, kReset = 2, kResetAll = 3, kSave = 4;

    std::vector<KeyBindingRow> rows;    // 엔진 (이름 순) 다음 게임 키 (이름 순)
    std::vector<std::string> conflicts; // "Q — a · b"
    bool dirty = false;                 // 저장 안 한 것이 있다
    bool canSave = true;                // 설정 폴더가 없으면 (안전 모드) false
    std::string capturing;              // 바꾸는 중인 액션 (비면 없음)
    std::string status;                 // 마지막 결과
    bool operator==(const KeyBindingsModel&) const = default;
};

class KeyBindingsPanel final : public uihost::IUiScreen {
public:
    explicit KeyBindingsPanel(PanelContext& ctx) : m_ctx(ctx) {}

    void setModel(KeyBindingsModel model);
    [[nodiscard]] const KeyBindingsModel& model() const noexcept { return m_model; }

    // 목록 줄 — 겹침 · 머리 · 액션 (row = model().rows 번호)
    struct Line {
        enum class Kind : u8 { Conflict, Header, Row };
        Kind kind = Kind::Row;
        usize index = 0; // Conflict = conflicts 번호, Header = 0 엔진 · 1 게임, Row = rows 번호
    };
    [[nodiscard]] const std::vector<Line>& lines() const noexcept { return m_lines; }
    [[nodiscard]] std::string lineText(usize line) const;
    [[nodiscard]] const std::string& selected() const noexcept { return m_selected; } // 고른 액션
    [[nodiscard]] std::string title() const;

    Expected<void> mount(uihost::UiHost& host) override;
    Expected<void> layout(uihost::UiHost& host, const gui::ComponentSnapshot& tree, std::vector<gui::LayoutNode>& out) override;
    Expected<void> paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) override;

    [[nodiscard]] gui::ComponentHandle captureHandle() const noexcept { return m_captureNode; }
    [[nodiscard]] gui::ComponentHandle resetHandle() const noexcept { return m_resetNode; }
    [[nodiscard]] gui::ComponentHandle resetAllHandle() const noexcept { return m_resetAllNode; }
    [[nodiscard]] gui::ComponentHandle saveHandle() const noexcept { return m_saveNode; }
    [[nodiscard]] gui::ComponentHandle listHandle() const noexcept { return m_listNode; }
    [[nodiscard]] std::optional<gui::LayoutRect> lineBounds(usize line) const;

private:
    void request(int kind, std::string action = {});
    void captureOrCancel();
    void select(usize line);
    [[nodiscard]] const KeyBindingRow* selectedRow() const;
    void applyEnabled();
    gui::EventReply listEvent(const gui::RoutedEvent& ev);

    PanelContext& m_ctx;
    KeyBindingsModel m_model;
    std::vector<Line> m_lines;
    std::vector<gui::ListItemKey> m_keys;
    std::string m_selected;
    uihost::UiHost* m_host = nullptr;
    gui::ComponentHandle m_root, m_info, m_bar, m_captureNode, m_resetNode, m_resetAllNode, m_saveNode, m_listNode, m_statusNode;
    std::unique_ptr<gui::Button> m_capture, m_reset, m_resetAll, m_save;
    gui::ListView m_list;
    gui::LayoutRect m_listBounds;
};

} // namespace sbx::editor::gui
