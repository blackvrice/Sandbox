#pragma once
// 게임 UI 그리기 명령 (v0.7 M3-T3 — ADR-0079 4). 배치 결과 (UiLayoutNode) + 스타일 → IUiRenderer 호출. 백엔드를 모른다
// — 클라이언트는 ImGui 그리기 목록 (도형 · 글자만 — 위젯 · 상태는 쓰지 않는다), 시험은 호출을 적는 렌더러.
//
//   panel · list   스타일 color 가 있으면 채운 사각형, border 가 있으면 테두리
//   button         채운 사각형 (스타일 color — 없으면 기본 회색, 누르는 중이면 밝게) · 테두리 · 글자 (여백만큼 안쪽)
//   text           글자 (왼쪽 위 — 여백만큼 안쪽)
//   progress       바탕 (스타일 color 의 어두운 판) + 채움 (value / max — 0 ~ 1 로 자름)
//   image          material 이름 그대로 (백엔드가 텍스처 · 대체 색으로)
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

struct UiDrawOptions {
    const std::map<std::string, UiStyle, std::less<>>* styles = nullptr;
    // 보이는 글자 (바인딩을 푼 것). 없으면 node.text
    std::function<std::string(const UiNode& node, i32 listIndex)> textOf{};
    // progress 의 채움 (0 ~ 1). 없으면 value · max 의 고정 수로
    std::function<f32(const UiNode& node, i32 listIndex)> progressOf{};
    // 누르는 중인 노드 (버튼을 밝게) — 없으면 nullptr
    const UiNode* pressed = nullptr;
    i32 pressedIndex = -1;
};

// 배치 결과를 차례대로 그린다 (부모 먼저 — 자식이 위에)
void drawLayout(const std::vector<UiLayoutNode>& nodes, const UiDrawOptions& options, IUiRenderer& renderer);

} // namespace sbx::ui
