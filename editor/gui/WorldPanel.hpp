#pragma once
// "월드" 패널 (UI-R4-06, ADR-0185) — ImGui "월드" 창 (drawWorldPanel) 과 같은 일: 상태 한 줄 (보통 · 기록 중 · 재생 · 테스트 플레이) ·
// 재생 탐색 · 이름 · 저장 · 기록 시작 / 끝 · 새로고침 · 테스트 플레이 / 멈추기 (되돌리기) · 세이브 · 리플레이 목록 · 불러오기 / 재생 · 결과.
//
//   보기  WorldModel — 앱이 frame 마다 (세션의 WorldOpsInfo · worldModeLine · 이름 (Ctrl+S 와 같은 m_worldPanel.name))
//   일    의도 WorldOp (value = client::WorldOp 값, command = 이름, component = 탐색 call) · WorldName (이름 칸 — Ctrl+S 가 같은 이름)
//         — 앱이 ImGui 창과 같은 세션 요청 (테스트 플레이 전 자동 저장 포함)
//   다른 점  재생 타임라인 슬라이더 대신 call 칸 + 탐색 (끌기 미리보기 없음)

#include <optional>
#include <string>
#include <vector>

#include "editor/gui/StudioPanels.hpp"

namespace sbx::editor::gui {

struct SavedEntry {
    std::string name;
    bool replay = false;
    std::string text; // "[리플레이] x · tick 1 · 개체 2"
    bool operator==(const SavedEntry&) const = default;
};

struct WorldModel {
    // client::WorldOp 값 (apps/client WorldSession.hpp — 앱이 static_assert)
    static constexpr int kList = 1, kSave = 2, kLoad = 3, kRecordStart = 4, kRecordStop = 5, kPlay = 6, kTestPlay = 8, kTestStop = 9,
                         kSeek = 10;

    bool available = false; // 세션이 월드 일을 한다
    bool canChange = false; // 저장 · 불러오기 · 기록 · 재생 · 테스트 플레이 (admin 부터 — 판단은 서버)
    bool listed = false;
    u8 mode = 0; // 0 보통 · 1 기록 중 · 2 재생 · 3 테스트 플레이
    std::string modeLine;
    std::string modeName;
    u64 replayCall = 0;
    u64 replayCalls = 0;
    u32 generation = 0;
    std::vector<SavedEntry> saved;
    std::string message;
    std::string name;    // 저장 · 기록 이름 (Ctrl+S 도 이 이름)
    bool nameOk = false; // net::isValidSaveName(name)
    bool operator==(const WorldModel&) const = default;
};

class WorldPanel final : public uihost::IUiScreen {
public:
    explicit WorldPanel(PanelContext& ctx) : m_ctx(ctx), m_name("이름 (영문 · 숫자 · _ -)", 13.0), m_seek("call", 13.0) {}

    void setModel(WorldModel model);
    [[nodiscard]] const WorldModel& model() const noexcept { return m_model; }
    [[nodiscard]] std::optional<usize> selectedRow() const;
    [[nodiscard]] std::string status() const;

    Expected<void> mount(uihost::UiHost& host) override;
    Expected<void> layout(uihost::UiHost& host, const gui::ComponentSnapshot& tree, std::vector<gui::LayoutNode>& out) override;
    Expected<void> paint(uihost::UiHost& host, const gui::LayoutFrame& frame, uihost::PaintList& out) override;

    [[nodiscard]] gui::ComponentHandle nameHandle() const noexcept { return m_name.handle(); }
    [[nodiscard]] gui::ComponentHandle seekFieldHandle() const noexcept { return m_seek.handle(); }
    [[nodiscard]] gui::ComponentHandle seekHandle() const noexcept { return m_seekNode; }
    [[nodiscard]] gui::ComponentHandle saveHandle() const noexcept { return m_saveNode; }
    [[nodiscard]] gui::ComponentHandle recordHandle() const noexcept { return m_recordNode; }
    [[nodiscard]] gui::ComponentHandle refreshHandle() const noexcept { return m_refreshNode; }
    [[nodiscard]] gui::ComponentHandle testHandle() const noexcept { return m_testNode; }
    [[nodiscard]] gui::ComponentHandle listHandle() const noexcept { return m_listNode; }
    [[nodiscard]] gui::ComponentHandle openHandle() const noexcept { return m_openNode; }
    [[nodiscard]] std::optional<gui::LayoutRect> rowBounds(usize row) const;

private:
    void emit(int op, std::string name, u64 target = 0);
    void seek();
    void openSelected();
    void applyEnabled();
    gui::EventReply listEvent(const gui::RoutedEvent& ev);

    PanelContext& m_ctx;
    TextField m_name, m_seek;
    WorldModel m_model;
    std::optional<std::string> m_selected; // 고른 세이브 · 리플레이 이름
    std::string m_seekError;
    std::vector<gui::ListItemKey> m_keys;
    uihost::UiHost* m_host = nullptr;
    gui::ComponentHandle m_root, m_status, m_seekRow, m_seekNode, m_seekLabel, m_bar, m_saveNode, m_recordNode, m_refreshNode,
        m_testNode, m_listNode, m_openNode, m_message;
    std::unique_ptr<gui::Button> m_seekButton, m_save, m_record, m_refresh, m_test, m_open;
    gui::ListView m_list;
    gui::LayoutRect m_listBounds;
};

} // namespace sbx::editor::gui
