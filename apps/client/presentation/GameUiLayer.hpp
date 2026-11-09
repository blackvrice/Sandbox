#pragma once
// 게임 UI 층 (v0.7 M3-T3 — ADR-0079, docs/20-CREATOR-ALPHA.md 2.4). 콘텐츠의 presentation/ui 원문 → 문서 · 스타일 →
// 프레임마다 배치 (창 크기 — 논리 단위, ImGui · 마우스와 같다) → 포인터 라우팅 (ui/input) → 버튼 이벤트 → 그리기
// (IUiRenderer — 클라이언트는 ImGui 배경 그리기 목록: 게임 화면 위 · 도구 패널 아래). 그리기 백엔드 · 글자 재기를
// 모른다 — 헤드리스 시험이 같은 길을 돈다.
//
// 이벤트: 버튼 위에서 왼쪽 단추를 누른 프레임에. signal · action 은 앱이 명령으로 (서버가 판정), ui 는 여기서 (show ·
// hide · toggle — "<문서>" 는 문서, "<문서>.<노드>" 는 노드. 숨긴 노드는 자리도 차지하지 않는다). 글자 · progress ·
// list 수: ui/binding (M3-T4) — 데이터는 setData (세션의 복제 월드 · 게임 세션 · var). 글자 "@<키>" 는 팩 문자열 표
// (v0.7 M4-T1 — ui/strings, 로케일은 setLocale). 검사: 문서 · 표의 모양이 틀리면 (U1 · U3 · U7) 그것을 건너뛰고 경고
// 한 번, 표에 없는 키도 경고 한 번 — 규칙 검사는 sbx check 의 몫

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "apps/client/WorldSession.hpp"
#include "ui/binding/UiBinding.hpp"
#include "ui/input/UiInputRouter.hpp"
#include "ui/render/UiDraw.hpp"

namespace sbx::client {

class GameUiLayer {
public:
    // 콘텐츠의 원문 — 바뀌었으면 (원문 지문) 다시 읽는다. nullopt = 게임 UI 없음
    void setSources(const std::optional<GameUiInfo>& info);
    // 바인딩의 데이터 (v0.7 M3-T4 — 세션이 가진다, 프레임마다 그대로). nullptr 이면 바인딩은 "-"
    void setData(const ui::IUiDataSource* data) noexcept { m_data = data; }
    // 이번 프레임의 배치 (논리 단위 — 창 크기)
    void layout(i32 width, i32 height, f32 scale, const ui::ITextMeasure& measure);
    // 포인터 (논리 단위) 를 라우팅한다. pressed = 이 프레임에 왼쪽 단추를 눌렀다, down = 누르고 있다. 누른 버튼의
    // signal · action 이벤트를 out 에 (ui 이벤트는 여기서 적용 — 다음 배치부터 보인다)
    ui::UiPointerRoute route(i32 x, i32 y, bool toolsWantMouse, bool pressed, bool down,
                             std::vector<ui::UiAction>& out);
    void draw(ui::IUiRenderer& renderer) const;

    void apply(const ui::UiAction& action); // ui show · hide · toggle · theme
    // 지금 테마 (v0.7 M3-T6 — 비면 없음). 없는 테마 이름이면 false (그대로)
    bool setTheme(std::string_view theme);
    [[nodiscard]] const std::string& theme() const noexcept { return m_theme; }
    // 사용자 로케일 (v0.7 M4-T1 — "ko" · "en-US" …, 꼴이 아니면 팩의 기본 표만). 다음 배치부터
    void setLocale(std::string_view locale) { m_strings.setLocale(locale); }
    [[nodiscard]] const std::string& locale() const noexcept { return m_strings.locale(); }
    [[nodiscard]] const ui::StringTable& strings() const noexcept { return m_strings; }
    // 콘텐츠의 표시 이름 (행동 · 게임 모드 · 팀 — v0.7 M4-T1): "@<키>" 면 표의 글, "@@…" 면 "@…", 그 밖은 그대로
    [[nodiscard]] std::string displayName(std::string_view name) const {
        return name.starts_with('@') ? ui::resolveText(name, &m_strings) : std::string(name);
    }
    [[nodiscard]] usize documentCount() const noexcept { return m_docs.size(); }
    [[nodiscard]] bool documentVisible(std::string_view id) const;
    [[nodiscard]] bool nodeHidden(std::string_view documentId, std::string_view nodeId) const;
    [[nodiscard]] const std::vector<ui::UiDocumentLayout>& layouts() const noexcept { return m_layouts; }
    [[nodiscard]] u64 buttonPresses() const noexcept { return m_presses; } // 누른 버튼 수 (진단 · 시험)
    // 새로 생긴 경고 (읽지 못한 문서) — 꺼내면 비운다
    [[nodiscard]] std::vector<std::string> takeWarnings();

private:
    std::string m_signature;
    ui::UiStyleTable m_baseStyles;                            // 팩 스타일 (뒤 팩이 이긴다)
    std::map<std::string, ui::UiTheme, std::less<>> m_themes; // 팩 테마
    std::string m_theme;                                      // 지금 테마 (비면 없음)
    ui::UiStyleTable m_styles;                                // 테마를 씌운 것 — 배치 · 그리기가 쓴다
    ui::StringTable m_strings;                                // 팩 문자열 표 (로케일은 setSources 를 지나도 남는다)
    f32 m_scale = 1;
    std::vector<ui::UiDocument> m_docs;
    std::vector<bool> m_visible;
    std::set<std::string, std::less<>> m_hidden; // "<문서>.<노드>"
    std::vector<ui::UiDocumentLayout> m_layouts;
    const ui::UiNode* m_pressed = nullptr;
    i32 m_pressedIndex = -1;
    u64 m_presses = 0;
    std::vector<std::string> m_warnings;
    const ui::IUiDataSource* m_data = nullptr;
};

} // namespace sbx::client
