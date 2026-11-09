#pragma once
// 게임 UI 배치 (v0.7 M3-T2 — ADR-0079 2, docs/20-CREATOR-ALPHA.md 2.3). 헤드리스 — 같은 입력이면 같은 정수 사각형.
//
// 가장자리: x0 = 부모.x + anchorMin.x · 부모.w + offset.왼 · 배율, x1 = 부모.x + anchorMax.x · 부모.w + offset.오른 ·
// 배율 (세로도 같다). 예: anchor {min [0, 0], max [1, 0]} · offset [8, 8, -8, 48] = 가로 8 ~ W-8, 높이 40. size 가
// 있으면 x1 = x0 + size.w · 배율 (오른 offset 대신). 없고 x1 ≤ x0 이면 내용에 맞춤 (글자 · 쌓은 자식 · list 항목).
// 쌓기: 부모가 stack row · column 이면 자식은 앵커 · offset 대신 차례로 — 주 축 = 앞 자식 끝 + gap, 교차 축 = align
// (start · center · end · stretch). list 는 item 을 항목 수만큼 세로로 (stack row 면 가로로). 여백: panel padding ·
// 스타일 padding 중 큰 쪽만큼 안쪽이 자식의 부모 사각형. 자름: 노드마다 clip = 조상들의 사각형의 교집합 — 넘친 부분은
// 그리지 않는다 (스크롤은 [계획]). 반올림: 가장자리마다 floor(v + 0.5) — 너비가 아니라 가장자리를 반올림해 이웃 사이에
// 틈 · 겹침이 없다. 글자 크기는 ITextMeasure, 스타일은 이름 표, 글자 · list 항목 수는 콜백으로 받는다 (바인딩 평가는
// M3-T4).
//
// 글자 넘침 (v0.7 M4-T2 — overflow): 글자가 쓸 수 있는 너비 = size 가 있으면 그 너비, 쌓는 row 면 남은 자리 (부모 안쪽
// 끝 - 이 자식의 시작), column 이면 부모 안쪽 너비, 그 밖은 부모 안쪽 오른 끝까지 — 여백을 뺀 값. 넘치면 clip = 자름,
// wrap = 빈칸에서 줄 바꿈 (한 낱말이 넘치면 글자마다 — 줄은 배치가 정한다, 그리기는 줄마다), fit = 글꼴을 너비에 맞게
// 줄인다 (원래의 절반까지 — 그래도 넘치면 자른다). 줄 바꿈 '\n' 은 wrap 에서만 줄을 나눈다

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "ui/model/UiDocument.hpp"

namespace sbx::ui {

struct UiRect {
    i32 x = 0;
    i32 y = 0;
    i32 w = 0;
    i32 h = 0;
    bool operator==(const UiRect&) const = default;
};

// 글자 한 줄의 크기 (픽셀, 배율을 곱한 글꼴 크기로). 결정적이어야 한다 — 같은 입력 = 같은 값
class ITextMeasure {
public:
    virtual ~ITextMeasure() = default;
    [[nodiscard]] virtual std::array<f32, 2> measure(std::string_view text, f32 fontSize) const = 0;
};

// 헤드리스 · 시험용 — 코드 포인트마다 advance · 글꼴 크기, 높이 = lineHeight · 글꼴 크기
class FixedTextMeasure final : public ITextMeasure {
public:
    explicit FixedTextMeasure(f32 advance = 0.5f, f32 lineHeight = 1.25f)
        : m_advance(advance), m_lineHeight(lineHeight) {}
    [[nodiscard]] std::array<f32, 2> measure(std::string_view text, f32 fontSize) const override;

private:
    f32 m_advance;
    f32 m_lineHeight;
};

inline constexpr f32 kDefaultFontSize = 16;

struct UiLayoutOptions {
    i32 width = 0;
    i32 height = 0;
    f32 scale = 1; // UI 배율 (0.5 ~ 4) — offset · size · gap · padding · 글꼴에 곱한다
    const std::map<std::string, UiStyle, std::less<>>* styles = nullptr;
    // 노드의 보이는 글자 (바인딩을 푼 것). 없으면 node.text 그대로. listIndex = list 항목 안이면 그 번호, 아니면 -1
    std::function<std::string(const UiNode& node, i32 listIndex)> textOf{};
    // list 의 항목 수 (바인딩 결과). 없으면 0
    std::function<usize(const UiNode& list)> listCount{};
    // 숨긴 노드 (ui hide · toggle — v0.7 M3-T3). 참이면 그 노드와 아래를 배치하지 않는다 (쌓기에서 자리도 차지하지
    // 않는다)
    std::function<bool(const UiNode& node)> hidden{};
};

struct UiLayoutNode {
    const UiNode* node = nullptr;
    UiRect rect;
    UiRect clip;                     // 그릴 수 있는 영역 (조상의 교집합)
    u32 depth = 0;                   // root = 0
    i32 listIndex = -1;              // list 항목 안이면 그 번호
    f32 fontSize = kDefaultFontSize; // 배율을 곱한 값 (text · button)
    f32 padding = 0;                 // 배율을 곱한 안쪽 여백 (panel padding · 스타일 padding 중 큰 쪽)
    std::vector<std::string> lines;  // overflow wrap 이 나눈 줄 (v0.7 M4-T2 — 비면 한 줄, 글자는 textOf)
    f32 lineHeight = 0;              // 줄 높이 (wrap)
};

inline constexpr f32 kMinFitScale = 0.5f; // overflow fit — 글꼴을 원래의 절반까지 줄인다
inline constexpr f32 kSliderWidth = 160;  // slider 의 기본 너비 (size 가 없으면 — 배율을 곱한다, v0.7 NG-06 (1))

// checkbox · toggle 의 상자 · slider 의 높이 = 글꼴 × 1.25 (반올림 — 배치와 그리기가 같은 값, 글자 재기와 무관)
[[nodiscard]] inline f32 controlBox(f32 fontSize) noexcept {
    return static_cast<f32>(static_cast<i32>(fontSize * 1.25f + 0.5f));
}

// 글자를 너비 maxWidth 안의 줄로 (빈칸에서, 한 낱말이 넘치면 코드 포인트마다 — '\n' 은 늘 나눈다). 결정적
[[nodiscard]] std::vector<std::string> wrapText(std::string_view text, f32 fontSize, f32 maxWidth,
                                                const ITextMeasure& measure);

// 문서를 배치한다 — 앞 순서 (부모 먼저, 자식은 문서 차례). list 는 item 을 항목마다 한 벌. 상한: 결과 4,096 노드
// (넘으면 자른다)
[[nodiscard]] std::vector<UiLayoutNode> layoutDocument(const UiDocument& document, const UiLayoutOptions& options,
                                                       const ITextMeasure& measure);

} // namespace sbx::ui
