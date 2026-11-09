#pragma once
// 게임 UI 그리기 명령 (v0.7 M3-T3 — ADR-0079 4). 배치 결과 (UiLayoutNode) + 스타일 → IUiRenderer 호출. 백엔드를 모른다
// — 클라이언트는 ImGui 그리기 목록 (도형 · 글자만 — 위젯 · 상태는 쓰지 않는다), 시험은 호출을 적는 렌더러.
//
//   panel · list   스타일 color 가 있으면 채운 사각형, border 가 있으면 테두리
//   button         채운 사각형 (스타일 color — 없으면 기본 회색, 누르는 중이면 밝게) · 테두리 · 글자 (여백만큼 안쪽)
//   text           글자 (왼쪽 위 — 여백만큼 안쪽)
//   progress       바탕 (스타일 color 의 어두운 판) + 채움 (value / max — 0 ~ 1 로 자름)
//   image          material 이름 그대로 (백엔드가 텍스처 · 대체 색으로)
//   checkbox       상자 (테두리) + 켜면 안쪽 채움 · 이름 (v0.7 NG-06 (1)), toggle = 2 칸 길 (켜면 밝게) + 손잡이 (켜면
//   오른쪽) slider         가는 길 + 채움 (value 의 min ~ max 비율) + 손잡이. 꺼진 컨트롤 (disabled) 은 흐리게,
//   포커스는 테두리 고리
//   textinput      상자 + 글자 (줄마다 — '\n' 으로만 나눈다, 감싸지 않는다), 비면 placeholder 를 흐리게. 포커스면 캐럿
//                  (깜빡이지 않는다 — 결정적) 이 보이게 가로 · 세로로 민다 (캐럿 줄이 넘치면) (v0.7 NG-06 (2))
//   dropdown       상자 + 고른 option + 오른쪽 화살표 (v0.7 NG-06 (3)) — 열린 목록은 drawOptionList (문서들 위에)
//   tabs           머리 칸마다 바탕 (고른 칸은 밝게 + 밑줄) + label, 몸통은 고른 자식 (배치가 그것만 놓았다)
// 모든 호출에 clip (UiLayoutNode.clip) — 넘친 부분은 백엔드가 자른다

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "ui/layout/UiLayout.hpp"

namespace sbx::ui {

using UiColor = std::array<u8, 4>;

class IUiRenderer {
public:
    virtual ~IUiRenderer() = default;
    virtual void fillRect(const UiRect& rect, UiColor color, const UiRect& clip) = 0;
    virtual void strokeRect(const UiRect& rect, UiColor color, f32 width, const UiRect& clip) = 0;
    virtual void text(const UiRect& rect, std::string_view text, f32 fontSize, UiColor color, const UiRect& clip) = 0;
    virtual void image(const UiRect& rect, std::string_view material, const UiRect& clip) = 0;
};

inline constexpr UiColor kTextColor{235, 235, 235, 255};
inline constexpr UiColor kButtonColor{70, 70, 78, 230};
inline constexpr UiColor kProgressColor{90, 170, 90, 255};
inline constexpr UiColor kControlColor{55, 55, 62, 230};        // 상자 · 길 바탕
inline constexpr UiColor kCheckColor{110, 170, 230, 255};       // 켬 · 채움
inline constexpr UiColor kFocusColor{250, 210, 90, 255};        // 포커스 고리
inline constexpr UiColor kFieldBorderColor{120, 120, 132, 255}; // 글자 칸 테두리
inline constexpr UiColor kPlaceholderColor{140, 140, 150, 255}; // 빈 글자 칸의 안내 글
inline constexpr UiColor kListColor{38, 38, 44, 245};           // 열린 목록 바탕
inline constexpr UiColor kHighlightColor{80, 110, 150, 255};    // 목록의 가리킨 줄

struct UiDrawOptions {
    const std::map<std::string, UiStyle, std::less<>>* styles = nullptr;
    // 보이는 글자 (바인딩을 푼 것). 없으면 node.text
    std::function<std::string(const UiNode& node, i32 listIndex)> textOf{};
    // progress 의 채움 (0 ~ 1). 없으면 value · max 의 고정 수로
    std::function<f32(const UiNode& node, i32 listIndex)> progressOf{};
    // 누르는 중인 노드 (버튼을 밝게) — 없으면 nullptr
    const UiNode* pressed = nullptr;
    i32 pressedIndex = -1;
    f32 scale = 1; // UI 배율 — 테두리 두께에 곱한다 (사각형 · 글꼴은 배치가 이미 곱했다 — v0.7 M3-T6)
    // 입력 컨트롤 (v0.7 NG-06 (1)): 지금 값 (checkbox · toggle 0 이 아니면 켬, slider 의 값), 꺼짐, 포커스. 없으면
    // value 의 고정 수 · 켜짐 · 포커스 없음
    std::function<f32(const UiNode& node, i32 listIndex)> valueOf{};
    std::function<bool(const UiNode& node, i32 listIndex)> disabledOf{};
    const UiNode* focused = nullptr;
    i32 focusedIndex = -1;
    // 글자 칸 (v0.7 NG-06 (2)): 지금 글자 (var — 없으면 빈 칸), 포커스 칸의 캐럿 (코드 포인트 위치), 글자 재기 (캐럿 ·
    // 밀기 — 없으면 캐럿을 그리지 않고 밀지 않는다)
    std::function<std::string(const UiNode& node, i32 listIndex)> textValueOf{};
    usize caret = 0;
    const ITextMeasure* measure = nullptr;
    // dropdown options · tabs labels 의 보이는 글자 (v0.7 NG-06 (3) — 없으면 원문). 고른 번호는 valueOf (반올림)
    std::function<std::string(const UiNode& node, usize option)> optionOf{};
};

// 배치 결과를 차례대로 그린다 (부모 먼저 — 자식이 위에)
void drawLayout(const std::vector<UiLayoutNode>& nodes, const UiDrawOptions& options, IUiRenderer& renderer);

// dropdown 의 열린 목록 (v0.7 NG-06 (3)) — list = optionListRect, 줄마다 바탕 (가리킨 줄 · 고른 줄) + 글자. 자름 = 목록
void drawOptionList(const UiRect& list, const std::vector<std::string>& texts, i32 selected, i32 highlighted,
                    f32 fontSize, f32 scale, IUiRenderer& renderer);

} // namespace sbx::ui
