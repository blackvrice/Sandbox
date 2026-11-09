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

#include <functional>
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
    // 컨트롤이 바꾼 var 값을 쓰는 곳 (v0.7 NG-06 (1) — ADR-0103 1: 세션의 var 표, client 스크립트와 같은 표). 없으면
    // 버린다
    using VarWriter = std::function<void(std::string_view key, const ui::UiScalar& value)>;
    void setVarWriter(VarWriter writer) { m_writeVar = std::move(writer); }
    // 이번 프레임의 배치 (논리 단위 — 창 크기)
    void layout(i32 width, i32 height, f32 scale, const ui::ITextMeasure& measure);
    // 포인터 (논리 단위) 를 라우팅한다. pressed = 이 프레임에 왼쪽 단추를 눌렀다, down = 누르고 있다. 누른 버튼의
    // signal · action 이벤트를 out 에 (ui 이벤트는 여기서 적용 — 다음 배치부터 보인다). 입력 컨트롤 (NG-06 (1)):
    // checkbox · toggle 은 누르면 바꾸고, slider 는 끄는 동안 값을 쓰고 놓을 때 on.change 신호 하나. menu · modal 의
    // 컨트롤을 누르면 키보드 포커스 (hud 는 주지 않는다 — ADR-0103 4)
    ui::UiPointerRoute route(i32 x, i32 y, bool toolsWantMouse, bool pressed, bool down,
                             std::vector<ui::UiAction>& out);
    // measure = 글자 칸의 캐럿 · 밀기 (v0.7 NG-06 (2) — 없으면 캐럿을 그리지 않는다)
    void draw(ui::IUiRenderer& renderer, const ui::ITextMeasure* measure = nullptr) const;
    // 키 (v0.7 NG-06 (1) — ADR-0103 5): Next · Previous = 포커스 차례, 방향 = 가까운 컨트롤 (slider 면 왼 · 오른 = 한
    // 칸), Activate = 누르기, Release = 포커스 놓기. 키를 썼으면 true (앱은 게임에 주지 않는다). 포커스가 없으면 Next ·
    // Previous 만 (보이는 컨트롤이 있으면 첫째 · 끝에)
    // 글자 칸에 포커스가 있으면 (NG-06 (2) — ADR-0104): 왼 · 오른 = 캐럿, 위 · 아래 = multiline 의 줄 (첫 · 끝 줄이면
    // 탐색), Home · End = 줄 처음 · 끝, Backspace · Delete = 지우기, Activate = multiline 은 줄 바꿈 · 한 줄은 보내기
    // (on.change 신호 — 값 = 코드 포인트 수). 포커스가 떠나면 (Tab · Esc · 다른 곳 클릭) 글자가 바뀐 경우 보내기
    bool key(ui::UiKey key, std::vector<ui::UiAction>& out);
    // 확정된 글자 (UTF-8 — IME 조합이 끝난 것만) 를 포커스 글자 칸의 캐럿에 넣는다. 글자 칸 포커스가 아니면 false
    bool text(std::string_view utf8);
    [[nodiscard]] bool hasFocus() const noexcept { return m_focus != nullptr; } // 키보드는 게임 UI 가 갖는다
    // 포커스가 켜진 글자 칸이다 — 앱은 창의 글자 입력 (IME) 을 켜고, Space 를 누르기로 바꾸지 않는다
    [[nodiscard]] bool editingText() const;
    [[nodiscard]] usize caret() const noexcept { return m_caret; }
    [[nodiscard]] std::string textValue(const ui::UiNode& n) const; // 글자 칸의 지금 글자 (var — 없으면 빈 칸)
    // 고르기 (v0.7 NG-06 (3) — ADR-0105): dropdown · tabs 의 고른 번호 (var 를 반올림 — 범위 밖 · 모름 -1), option ·
    // label 의 보이는 글자 ("@<키>" 는 표의 글), 열린 dropdown 목록 (문서들 위에 그린다)
    [[nodiscard]] i32 selectedIndex(const ui::UiNode& n) const;
    [[nodiscard]] std::string optionText(const ui::UiNode& n, usize option) const {
        return displayName(n.options[option]);
    }
    [[nodiscard]] bool listOpen() const noexcept { return m_open != nullptr; }
    // 휠 (v0.7 NG-06 (3b) — ADR-0106): 포인터 아래 가장 안쪽의 밀 수 있는 scroll 을 notches (위 = 양수) × kWheelStep
    // 만큼 (modal 이 보이면 modal 의 것만). 썼으면 true — 앱은 그 휠을 게임 (카메라 확대) 에 주지 않는다. 다음 배치부터
    // 보인다
    bool wheel(i32 x, i32 y, f32 notches);
    [[nodiscard]] f32 scrollOf(const ui::UiNode& n, i32 listIndex) const;
    static constexpr f32 kWheelStep = 40; // 휠 한 칸 (배율을 곱한다)
    [[nodiscard]] i32 highlighted() const noexcept { return m_highlight; }
    [[nodiscard]] const ui::UiNode* focused() const noexcept { return m_focus; }
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
    // 입력 컨트롤 (v0.7 NG-06 (1))
    [[nodiscard]] bool disabled(const ui::UiNode& n) const;
    [[nodiscard]] f32 controlValue(const ui::UiNode& n) const;
    void setControlValue(const ui::UiNode& n, f32 value); // var 에 쓴다 (checkbox · toggle 은 참거짓)
    void press(const ui::UiLayoutNode& n, usize document, std::vector<ui::UiAction>& out); // 클릭 · Activate 의 같은 길
    void select(const ui::UiNode& n, i32 index, std::vector<ui::UiAction>& out);           // 바뀌면 var · on.change
    bool choiceKey(const ui::UiLayoutNode& l, usize document, ui::UiKey key, std::vector<ui::UiAction>& out);
    void openList(const ui::UiLayoutNode& l, usize document);
    void revealFocus(); // 포커스가 scroll 안에서 가려졌으면 보이게 민다 (가장 안쪽 scroll)
    void closeList() noexcept { m_open = nullptr; }
    void sliderTo(const ui::UiNode& n, const ui::UiRect& rect, i32 x);
    void emitChange(const ui::UiNode& n, std::vector<ui::UiAction>& out) const;
    void setTextValue(const ui::UiNode& n, std::string value);
    bool editText(const ui::UiNode& n, ui::UiKey key, std::vector<ui::UiAction>& out); // 썼으면 true
    // 포커스를 옮긴다 — 떠나는 글자 칸이 바뀌었으면 on.change 신호 (out 이 있으면), 새 글자 칸은 캐럿을 끝에
    void setFocus(const ui::UiNode* n, i32 listIndex, std::vector<ui::UiAction>* out);
    [[nodiscard]] const ui::UiLayoutNode* layoutOf(const ui::UiNode* n, i32 listIndex) const;
    VarWriter m_writeVar;
    std::map<std::string, ui::UiScalar, std::less<>> m_written; // 이 층이 쓴 값 (세션이 없을 때 · 같은 프레임 읽기)
    const ui::UiNode* m_focus = nullptr;
    i32 m_focusIndex = -1;
    const ui::UiNode* m_drag = nullptr; // 끌고 있는 slider
    i32 m_dragIndex = -1;
    const ui::UiNode* m_open = nullptr; // 목록이 열린 dropdown (v0.7 NG-06 (3))
    i32 m_openIndex = -1;
    usize m_openDocument = 0;
    i32 m_highlight = -1; // 열린 목록의 가리킨 줄
    i32 m_height = 0;     // 배치한 화면 높이 (목록을 위 · 아래 어디에 놓나)
    std::map<std::pair<const ui::UiNode*, i32>, f32>
        m_scroll;            // scroll 마다 밀린 양 (노드 · list 번호 — v0.7 NG-06 (3b))
    usize m_caret = 0;       // 글자 칸의 캐럿 (코드 포인트 위치 — v0.7 NG-06 (2))
    std::string m_editStart; // 포커스를 받을 때의 글자 — 떠날 때 다르면 보낸다
};

} // namespace sbx::client
