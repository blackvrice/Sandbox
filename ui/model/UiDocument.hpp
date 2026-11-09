#pragma once
// 게임 UI 문서 모델 · 검사 (v0.7 M3-T1 — ADR-0079, docs/20-CREATOR-ALPHA.md 2.3).
//
//   팩의 presentation/ui/<이름>.json   문서 하나 {id: "<팩>.<이름>", layer, visible?, root: 노드}
//   팩의 presentation/ui/styles.json   {"styles": {"<팩>.<이름>": {color?, border?, fontSize?, padding?}}}
//
// 검사 규칙 (문제는 UiIssue — rule 은 "U1" ~ "U8"):
//   U1 형식 (JSON · 값의 모양 · 빠진 키)              U2 id 규칙 (문서 id = "<팩>.<파일 이름>", 노드 id · 겹침)
//   U3 모르는 요소 (kind) · 키                         U4 바인딩 식 (var · session · component)
//   U5 없는 스타일 · 문자열 키 (경고)                  U6 신호 · 행동이 콘텐츠에 없다 · ui 대상 노드가 없다
//   U7 상한 (문서마다 노드 512 · 깊이 16 · 문자열 1 KiB) U8 material 이 없다 (경고)
// 콘텐츠를 모른다 (Foundation 만) — 신호 · 행동 · 스타일 · material · 컴포넌트 칸 · 문자열 키는 UiCheckContext 로
// 받는다 (nullptr = 그 검사를 건너뛴다). 서버는 UI 를 읽지 않는다 — contentHash 밖 (presentation/)

#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "foundation/types/Types.hpp"

namespace sbx::ui {

inline constexpr usize kMaxNodes = 512;
inline constexpr usize kMaxDepth = 16;
inline constexpr usize kMaxStringBytes = 1024;
inline constexpr usize kMaxDocumentBytes = 256 * 1024;

struct UiIssue {
    enum class Severity : u8 { Error, Warning };
    Severity severity = Severity::Error;
    std::string rule;    // "U1" … "U8"
    std::string pointer; // JSON 포인터 ("/root/children/2/on/click/signal")
    std::string message;
};

enum class UiLayer : u8 { Hud, Menu, Modal };
// Checkbox · Toggle · Slider = 입력 컨트롤 (v0.7 NG-06 (1) — ADR-0103: 값은 "var:<키>", 바뀜은 on.change 신호)
enum class UiKind : u8 { Panel, Text, Image, Button, Progress, List, Checkbox, Toggle, Slider };

// 누르거나 포커스를 받는 노드 (버튼 · 입력 컨트롤)
[[nodiscard]] constexpr bool isControl(UiKind k) noexcept {
    return k == UiKind::Button || k == UiKind::Checkbox || k == UiKind::Toggle || k == UiKind::Slider;
}
enum class UiStack : u8 { None, Row, Column };
enum class UiAlign : u8 { Start, Center, End, Stretch };
// 글자가 쓸 수 있는 너비를 넘을 때 (v0.7 M4-T2 — 번역이 길어진다): 자름 (기본) · 줄 바꿈 · 글꼴 줄이기 (절반까지)
enum class UiOverflow : u8 { Clip, Wrap, Fit };

// 값 — 고정 수 또는 바인딩 ("var:…" · "session:…" · "component:…")
struct UiValue {
    f32 number = 0;
    std::string bind; // 비면 number
};

struct UiAction {
    enum class Kind : u8 { None, Signal, Action, Ui };
    Kind kind = Kind::None;
    std::string target;       // 신호 id · 행동 id · "<문서>.<노드>"
    std::optional<i32> value; // signal 의 값
    std::string op;           // ui: show · hide · toggle · theme (v0.7 M3-T6 — target = 테마 이름 · "none")
};

struct UiNode {
    UiKind kind = UiKind::Panel;
    std::string id; // 선택 — 문서 안에서 겹치지 않음
    f32 anchorMin[2] = {0, 0};
    f32 anchorMax[2] = {0, 0};
    f32 offset[4] = {0, 0, 0, 0};           // 왼 · 위 · 오른 · 아래 (픽셀)
    std::optional<std::array<f32, 2>> size; // 없으면 내용에 맞춤
    std::string style;
    UiStack stack = UiStack::None;
    f32 gap = 0;
    f32 padding = 0;
    UiAlign align = UiAlign::Start;
    std::string text;                       // text · button — 고정 문자열 또는 "@<문자열 키>"
    std::vector<UiValue> args;              // text 인자
    UiOverflow overflow = UiOverflow::Clip; // text · button (v0.7 M4-T2)
    UiValue value;                          // progress
    UiValue max{1, {}};                     // progress
    std::string material;                   // image
    UiAction click;                         // button
    // 입력 컨트롤 (v0.7 NG-06 (1) — ADR-0103). value = "var:<키>" (checkbox · toggle · slider), slider 의 범위 min ~
    // max.max (고정 수) · step (0 = 끊김 없음), 바뀜 on.change = 신호 (값은 컨트롤이 — 0 · 1 · 반올림한 i32)
    f32 min = 0;
    f32 step = 0;
    UiAction change;
    UiValue disabled;             // 참이면 (0 이 아니면) 포인터 · 키를 받지 않고 포커스 차례에서 빠진다 — 버튼 · 컨트롤
    i32 tabIndex = 0;             // 포커스 차례 — 작은 것 먼저, 같으면 문서 차례
    std::string items;            // list — 바인딩 (목록)
    std::vector<UiNode> children; // panel · list (list 는 item 하나 — 반복할 틀)
};

struct UiDocument {
    std::string id; // "<팩>.<이름>"
    UiLayer layer = UiLayer::Hud;
    bool visible = true;
    UiNode root;
    usize nodeCount = 0;
};

struct UiStyle {
    std::optional<std::array<u8, 4>> color;
    std::optional<std::array<u8, 4>> borderColor;
    f32 borderWidth = 0;
    f32 fontSize = 0; // 0 = 기본
    f32 padding = 0;
};

// 스타일 덮기 (v0.7 M3-T6 — 테마) — 적은 칸만 바꾼다, 적지 않은 칸은 바탕 스타일 그대로
struct UiStylePatch {
    std::optional<std::array<u8, 4>> color;
    std::optional<std::array<u8, 4>> borderColor;
    std::optional<f32> borderWidth;
    std::optional<f32> fontSize;
    std::optional<f32> padding;
    void applyTo(UiStyle& style) const;
};
using UiStyleTable = std::map<std::string, UiStyle, std::less<>>;
using UiTheme = std::map<std::string, UiStylePatch, std::less<>>; // 스타일 이름 → 덮기

// 검사가 콘텐츠에서 받는 것. nullptr 이면 그 검사를 건너뛴다
struct UiCheckContext {
    std::string_view packId; // 문서 id 앞 ("<팩>.")
    const std::set<std::string, std::less<>>* signals = nullptr;
    const std::set<std::string, std::less<>>* actions = nullptr;
    const std::set<std::string, std::less<>>* styles = nullptr;
    const std::set<std::string, std::less<>>* materials = nullptr;
    const std::set<std::string, std::less<>>* componentFields = nullptr; // "<컴포넌트 id>.<칸>"
    const std::set<std::string, std::less<>>* strings = nullptr;         // 문자열 키 (M4-T1 전에는 nullptr)
};

struct UiParseResult {
    std::optional<UiDocument> document; // U1 · U3 · U7 오류가 없으면 (다른 규칙의 오류가 있어도 모양은 있다)
    std::vector<UiIssue> issues;
    [[nodiscard]] bool ok() const noexcept; // 오류 없음
};

// fileStem = presentation/ui/ 의 파일 이름 (.json 앞 — 문서 id 의 뒤와 같아야 한다)
[[nodiscard]] UiParseResult parseUiDocument(std::string_view fileStem, std::string_view jsonText,
                                            const UiCheckContext& context);

struct UiStylesResult {
    UiStyleTable styles;
    std::map<std::string, UiTheme, std::less<>> themes; // v0.7 M3-T6 — "<팩>.<테마>" → 스타일마다 덮기
    std::vector<UiIssue> issues;
    [[nodiscard]] bool ok() const noexcept;
};
// styles.json {"styles": {...}, "themes"?: {"<팩>.<테마>": {"<스타일>": {덮을 칸}}}} — 이름은 "<팩>.<이름>" (U2), 키 ·
// 값 (U1 · U3). 테마가 부르는 스타일이 있는지는 팩을 모두 모은 쪽 (프로젝트 검사 — U5 경고) 이 본다
[[nodiscard]] UiStylesResult parseUiStyles(std::string_view jsonText, std::string_view packId);

// 테마를 씌운 스타일 표 (theme 이 nullptr 이면 그대로). 테마의 없는 스타일은 건너뛴다
[[nodiscard]] UiStyleTable applyTheme(const UiStyleTable& styles, const UiTheme* theme);
inline constexpr std::string_view kNoTheme = "none"; // ui theme 이벤트의 "테마 없음"

// "<팩>.<이름>" 모양 (id 글자 · 점 하나 이상 · 64 바이트까지) — 문서 · 스타일 · 테마 · 문자열 키가 같은 규칙
[[nodiscard]] bool isUiKey(std::string_view s);

// 바인딩 식 하나 — 문제면 그 이유 (U4). componentFields 가 있으면 칸도 본다
[[nodiscard]] std::optional<std::string> checkBinding(std::string_view bind,
                                                      const std::set<std::string, std::less<>>* componentFields);

// 문서들 사이: ui 이벤트 대상 "<문서>.<노드>" 가 있는가 (U6) — 문서 · 노드 id 를 모은 뒤 부른다
struct UiTargetRef {
    std::string documentId;
    std::string pointer;
    std::string target;
    bool theme = false; // ui theme 이벤트 — target 은 테마 이름 (v0.7 M3-T6)
};
[[nodiscard]] std::vector<UiTargetRef> uiTargets(const UiDocument& document);
[[nodiscard]] std::vector<std::string> nodeIds(const UiDocument& document); // "<문서>.<노드>" (id 있는 노드만)

} // namespace sbx::ui
